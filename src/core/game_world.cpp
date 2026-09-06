#include "openrc/game_world.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <utility>

namespace openrc::game {
namespace {

[[nodiscard]] bool finite_array(const auto &values) noexcept {
  return std::ranges::all_of(
      values, [](const float value) { return std::isfinite(value); });
}

[[nodiscard]] bool
valid_level_request_reason(const LevelRequestReasonV1 reason) noexcept {
  switch (reason) {
  case LevelRequestReasonV1::new_game:
  case LevelRequestReasonV1::transition:
  case LevelRequestReasonV1::checkpoint_restart:
  case LevelRequestReasonV1::developer:
    return true;
  }
  return false;
}

void validate_session_snapshot(const GameSessionSnapshotV1 &snapshot) {
  if (snapshot.pending_level_request) {
    if (snapshot.pending_level_request->sequence ==
            std::numeric_limits<std::uint64_t>::max() ||
        snapshot.pending_level_request->sequence !=
            snapshot.next_level_commit_sequence ||
        snapshot.next_level_request_sequence !=
            snapshot.pending_level_request->sequence + 1U ||
        !valid_level_request_reason(snapshot.pending_level_request->reason)) {
      throw GameWorldError(
          "A game-session snapshot has an inconsistent pending level request");
    }
  } else if (snapshot.next_level_commit_sequence !=
             snapshot.next_level_request_sequence) {
    throw GameWorldError(
        "A game-session snapshot omits an unresolved level request");
  }
  if (snapshot.level_instance_sequence != snapshot.next_level_commit_sequence) {
    throw GameWorldError(
        "A game-session snapshot has inconsistent level sequences");
  }
  if (snapshot.level_instance_sequence == 0U) {
    if (snapshot.active_level_id || snapshot.active_spawn_point_id) {
      throw GameWorldError(
          "A game-session snapshot has an active level without an instance");
    }
  } else if (!snapshot.active_level_id) {
    throw GameWorldError(
        "A game-session snapshot has a level instance without a level ID");
  }
  if (snapshot.active_spawn_point_id && !snapshot.active_level_id) {
    throw GameWorldError(
        "A game-session snapshot has a spawn without an active level");
  }
}

} // namespace

GameSessionV1::GameSessionV1(const std::uint64_t deterministic_seed) noexcept
    : deterministic_seed_(deterministic_seed) {}

void GameSessionV1::restore_metadata(const GameSessionSnapshotV1 &snapshot) {
  validate_session_snapshot(snapshot);
  deterministic_seed_ = snapshot.deterministic_seed;
  next_tick_index_ = snapshot.next_tick_index;
  next_level_request_sequence_ = snapshot.next_level_request_sequence;
  next_level_commit_sequence_ = snapshot.next_level_commit_sequence;
  pending_level_request_ = snapshot.pending_level_request;
  level_instance_sequence_ = snapshot.level_instance_sequence;
  active_level_id_ = snapshot.active_level_id;
  active_spawn_point_id_ = snapshot.active_spawn_point_id;
}

GameSessionV1::GameSessionV1(const GameSessionSnapshotV1 &snapshot) {
  if (snapshot.persistent_state) {
    throw GameWorldError(
        "Restoring persistent state requires its prepared schema and limits");
  }
  restore_metadata(snapshot);
}

GameSessionV1::GameSessionV1(const std::uint64_t deterministic_seed,
                             const SessionStateInitialV1 &initial_state,
                             const SessionStateLimitsV1 &limits)
    : deterministic_seed_(deterministic_seed) {
  try {
    persistent_state_.emplace(initial_state, limits);
  } catch (const SessionStateError &error) {
    throw GameWorldError("Cannot initialize session persistent state: " +
                         std::string(error.what()));
  }
}

GameSessionV1::GameSessionV1(const GameSessionSnapshotV1 &snapshot,
                             const SessionStateSchemaV1 &schema,
                             const SessionStateLimitsV1 &limits) {
  if (!snapshot.persistent_state) {
    throw GameWorldError(
        "A persistent-state restore cannot invent missing snapshot bytes");
  }
  restore_metadata(snapshot);
  try {
    persistent_state_.emplace(schema, *snapshot.persistent_state, limits);
  } catch (const SessionStateError &error) {
    throw GameWorldError("Cannot restore session persistent state: " +
                         std::string(error.what()));
  }
}

LevelRequestV1
GameSessionV1::request_level(const LevelIdV1 level_id,
                             const std::optional<SpawnPointIdV1> spawn_point_id,
                             const LevelRequestReasonV1 reason) {
  if (!valid_level_request_reason(reason)) {
    throw GameWorldError("A level request has an unknown reason");
  }
  if (pending_level_request_) {
    throw GameWorldError("A game session already has a pending level request");
  }
  if (next_level_request_sequence_ ==
      std::numeric_limits<std::uint64_t>::max()) {
    throw GameWorldError("The level-request sequence is exhausted");
  }
  const LevelRequestV1 result{
      next_level_request_sequence_,
      level_id,
      spawn_point_id,
      reason,
  };
  ++next_level_request_sequence_;
  pending_level_request_ = result;
  return result;
}

void GameSessionV1::commit_simulation_tick(const GameInputCommandV1 &command) {
  try {
    validate_game_input_command_v1(command);
  } catch (const GameInputError &) {
    throw GameWorldError(
        "A game session received an invalid replay input command");
  }
  if (command.tick_index != next_tick_index_) {
    throw GameWorldError(
        "A game session received an out-of-order simulation tick");
  }
  if (next_tick_index_ == std::numeric_limits<std::uint64_t>::max()) {
    throw GameWorldError("The game-session tick sequence is exhausted");
  }
  ++next_tick_index_;
}

ActiveLevelV1
GameSessionV1::commit_level_request(const LevelRequestV1 &request) {
  if (!pending_level_request_ || request != *pending_level_request_ ||
      request.sequence != next_level_commit_sequence_ ||
      request.sequence >= next_level_request_sequence_) {
    throw GameWorldError(
        "Level requests must be committed once in issue order");
  }
  if (next_level_commit_sequence_ ==
          std::numeric_limits<std::uint64_t>::max() ||
      level_instance_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    throw GameWorldError("The level-instance sequence is exhausted");
  }

  ++next_level_commit_sequence_;
  pending_level_request_.reset();
  ++level_instance_sequence_;
  active_level_id_ = request.level_id;
  active_spawn_point_id_ = request.spawn_point_id;
  return ActiveLevelV1{
      request.level_id,
      request.spawn_point_id,
      level_instance_sequence_,
  };
}

GameSessionSnapshotV1 GameSessionV1::snapshot() const {
  return {
      deterministic_seed_,
      next_tick_index_,
      next_level_request_sequence_,
      next_level_commit_sequence_,
      pending_level_request_,
      level_instance_sequence_,
      active_level_id_,
      active_spawn_point_id_,
      persistent_state_ ? std::optional{persistent_state_->snapshot()}
                        : std::nullopt,
  };
}

const SessionStateV1 *GameSessionV1::persistent_state() const noexcept {
  return persistent_state_ ? &*persistent_state_ : nullptr;
}

void GameSessionV1::apply_persistent_state_writes(
    const std::span<const SessionStateWriteV1> writes,
    const std::uint64_t expected_revision) {
  if (!persistent_state_) {
    throw GameWorldError(
        "The game session has no prepared persistent-state contract");
  }
  try {
    persistent_state_->apply_batch(writes, expected_revision);
  } catch (const SessionStateError &error) {
    throw GameWorldError("Cannot update session persistent state: " +
                         std::string(error.what()));
  }
}

std::uint64_t GameSessionV1::deterministic_seed() const noexcept {
  return deterministic_seed_;
}

std::uint64_t GameSessionV1::next_tick_index() const noexcept {
  return next_tick_index_;
}

const std::optional<LevelIdV1> &
GameSessionV1::active_level_id() const noexcept {
  return active_level_id_;
}

const std::optional<SpawnPointIdV1> &
GameSessionV1::active_spawn_point_id() const noexcept {
  return active_spawn_point_id_;
}

const std::optional<LevelRequestV1> &
GameSessionV1::pending_level_request() const noexcept {
  return pending_level_request_;
}

void validate_world_transform_v1(const WorldTransformV1 &transform) {
  if (!finite_array(transform.position) || !finite_array(transform.rotation) ||
      !finite_array(transform.scale)) {
    throw GameWorldError("A world transform contains a non-finite value");
  }
  const auto rotation_length_squared =
      transform.rotation[0U] * transform.rotation[0U] +
      transform.rotation[1U] * transform.rotation[1U] +
      transform.rotation[2U] * transform.rotation[2U] +
      transform.rotation[3U] * transform.rotation[3U];
  if (!(rotation_length_squared > 0.0F) ||
      std::ranges::any_of(transform.scale,
                          [](const float value) { return value == 0.0F; })) {
    throw GameWorldError(
        "A world transform has a zero rotation or scale component");
  }
}

void WorldV1::load_level(GameSessionV1 &session,
                         const LevelRequestV1 &request) {
  const auto active = session.commit_level_request(request);
  clear_entities();
  active_level_ = active;
}

void WorldV1::unload_level() noexcept {
  clear_entities();
  active_level_.reset();
}

EntityIdV1 WorldV1::spawn_entity(const WorldEntityDefinitionV1 &definition) {
  if (!active_level_) {
    throw GameWorldError("An entity cannot spawn without a loaded level");
  }
  validate_world_transform_v1(definition.transform);

  std::uint32_t slot_index = 0U;
  if (!free_slots_.empty()) {
    slot_index = free_slots_.back();
    free_slots_.pop_back();
  } else {
    if (entity_slots_.size() >=
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
      throw GameWorldError("The world entity-slot domain is exhausted");
    }
    slot_index = static_cast<std::uint32_t>(entity_slots_.size());
    entity_slots_.push_back({});
  }

  auto &slot = entity_slots_[slot_index];
  const EntityIdV1 id{
      active_level_->instance_sequence,
      slot_index,
      slot.generation,
  };
  slot.entity =
      WorldEntityV1{id, definition.archetype_id, definition.transform};
  ++entity_count_;
  return id;
}

bool WorldV1::destroy_entity(const EntityIdV1 id) {
  auto *const entity = find_entity(id);
  if (entity == nullptr) {
    return false;
  }
  static_cast<void>(entity);

  auto &slot = entity_slots_[id.slot];
  slot.entity.reset();
  --entity_count_;
  if (slot.generation != std::numeric_limits<std::uint32_t>::max()) {
    ++slot.generation;
    const auto insertion = std::lower_bound(
        free_slots_.begin(), free_slots_.end(), id.slot, std::greater<>{});
    free_slots_.insert(insertion, id.slot);
  }
  return true;
}

WorldEntityV1 *WorldV1::find_entity(const EntityIdV1 id) noexcept {
  if (!active_level_ ||
      id.level_instance_sequence != active_level_->instance_sequence ||
      id.slot >= entity_slots_.size()) {
    return nullptr;
  }
  auto &slot = entity_slots_[id.slot];
  if (!slot.entity || slot.generation != id.generation) {
    return nullptr;
  }
  return &*slot.entity;
}

const WorldEntityV1 *WorldV1::find_entity(const EntityIdV1 id) const noexcept {
  if (!active_level_ ||
      id.level_instance_sequence != active_level_->instance_sequence ||
      id.slot >= entity_slots_.size()) {
    return nullptr;
  }
  const auto &slot = entity_slots_[id.slot];
  if (!slot.entity || slot.generation != id.generation) {
    return nullptr;
  }
  return &*slot.entity;
}

const WorldEntityV1 *
WorldV1::entity_at_slot(const std::uint32_t slot) const noexcept {
  if (slot >= entity_slots_.size() || !entity_slots_[slot].entity) {
    return nullptr;
  }
  return &*entity_slots_[slot].entity;
}

std::size_t WorldV1::slot_count() const noexcept {
  return entity_slots_.size();
}

std::size_t WorldV1::entity_count() const noexcept { return entity_count_; }

const std::optional<ActiveLevelV1> &WorldV1::active_level() const noexcept {
  return active_level_;
}

void WorldV1::clear_entities() noexcept {
  entity_slots_.clear();
  free_slots_.clear();
  entity_count_ = 0U;
}

} // namespace openrc::game
