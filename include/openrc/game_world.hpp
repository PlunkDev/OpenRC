#pragma once

#include "openrc/game_input.hpp"
#include "openrc/placement_admission.hpp"
#include "openrc/session_state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace openrc::game {

using LevelIdV1 = std::uint32_t;
using SpawnPointIdV1 = std::uint32_t;
using EntityArchetypeIdV1 = std::uint32_t;

enum class LevelRequestReasonV1 : std::uint8_t {
  new_game = 0U,
  transition,
  checkpoint_restart,
  developer,
};

struct LevelRequestV1 {
  std::uint64_t sequence = 0U;
  LevelIdV1 level_id = 0U;
  std::optional<SpawnPointIdV1> spawn_point_id;
  LevelRequestReasonV1 reason = LevelRequestReasonV1::transition;

  [[nodiscard]] bool operator==(const LevelRequestV1 &) const = default;
};

struct ActiveLevelV1 {
  LevelIdV1 level_id = 0U;
  std::optional<SpawnPointIdV1> spawn_point_id;
  std::uint64_t instance_sequence = 0U;

  [[nodiscard]] bool operator==(const ActiveLevelV1 &) const = default;
};

struct GameSessionSnapshotV1 {
  std::uint64_t deterministic_seed = 0U;
  std::uint64_t next_tick_index = 0U;
  std::uint64_t next_level_request_sequence = 0U;
  std::uint64_t next_level_commit_sequence = 0U;
  std::optional<LevelRequestV1> pending_level_request;
  std::uint64_t level_instance_sequence = 0U;
  std::optional<LevelIdV1> active_level_id;
  std::optional<SpawnPointIdV1> active_spawn_point_id;
  // Absence means no prepared persistent-state contract was supplied.
  std::optional<SessionStateSnapshotV1> persistent_state;

  [[nodiscard]] bool operator==(const GameSessionSnapshotV1 &) const = default;
};

class GameWorldError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Persistent state survives level replacement. The seed, request sequences,
// and global tick sequence are explicit so a replay can validate all three
// rather than depending on allocation order or wall-clock time.
class GameSessionV1 final {
public:
  explicit GameSessionV1(std::uint64_t deterministic_seed = 0U) noexcept;
  explicit GameSessionV1(const GameSessionSnapshotV1 &snapshot);
  GameSessionV1(std::uint64_t deterministic_seed,
                const SessionStateInitialV1 &initial_state,
                const SessionStateLimitsV1 &limits);
  // A snapshot containing persistent bytes needs the trusted prepared schema.
  // Restoration never replays initial values or level-load side effects.
  GameSessionV1(const GameSessionSnapshotV1 &snapshot,
                const SessionStateSchemaV1 &schema,
                const SessionStateLimitsV1 &limits);

  [[nodiscard]] LevelRequestV1
  request_level(LevelIdV1 level_id,
                std::optional<SpawnPointIdV1> spawn_point_id = std::nullopt,
                LevelRequestReasonV1 reason = LevelRequestReasonV1::transition);

  void commit_simulation_tick(const GameInputCommandV1 &command);

  [[nodiscard]] GameSessionSnapshotV1 snapshot() const;
  [[nodiscard]] const SessionStateV1 *persistent_state() const noexcept;
  void
  apply_persistent_state_writes(std::span<const SessionStateWriteV1> writes,
                                std::uint64_t expected_revision);
  // One source-compiled neutral decision on current state; does not spawn an
  // entity or replace the required accepted-construction phase.
  [[nodiscard]] PlacementAdmissionResultV1
  apply_placement_admission(const PlacementAdmissionPlanV1 &plan,
                            std::uint64_t expected_revision,
                            PlacementAdmissionLimitsV1 limits);
  [[nodiscard]] std::uint64_t deterministic_seed() const noexcept;
  [[nodiscard]] std::uint64_t next_tick_index() const noexcept;
  [[nodiscard]] std::uint64_t level_instance_sequence() const noexcept;
  [[nodiscard]] const std::optional<LevelIdV1> &
  active_level_id() const noexcept;
  [[nodiscard]] const std::optional<SpawnPointIdV1> &
  active_spawn_point_id() const noexcept;
  [[nodiscard]] const std::optional<LevelRequestV1> &
  pending_level_request() const noexcept;

private:
  friend class WorldV1;

  [[nodiscard]] ActiveLevelV1
  commit_level_request(const LevelRequestV1 &request);
  void restore_metadata(const GameSessionSnapshotV1 &snapshot);

  std::uint64_t deterministic_seed_ = 0U;
  std::uint64_t next_tick_index_ = 0U;
  std::uint64_t next_level_request_sequence_ = 0U;
  std::uint64_t next_level_commit_sequence_ = 0U;
  std::optional<LevelRequestV1> pending_level_request_;
  std::uint64_t level_instance_sequence_ = 0U;
  std::optional<LevelIdV1> active_level_id_;
  std::optional<SpawnPointIdV1> active_spawn_point_id_;
  std::optional<SessionStateV1> persistent_state_;
};

struct WorldTransformV1 {
  std::array<float, 3U> position{};
  std::array<float, 4U> rotation{0.0F, 0.0F, 0.0F, 1.0F};
  std::array<float, 3U> scale{1.0F, 1.0F, 1.0F};

  [[nodiscard]] bool operator==(const WorldTransformV1 &) const = default;
};

struct WorldEntityDefinitionV1 {
  EntityArchetypeIdV1 archetype_id = 0U;
  WorldTransformV1 transform;

  [[nodiscard]] bool
  operator==(const WorldEntityDefinitionV1 &) const = default;
};

struct EntityIdV1 {
  std::uint64_t level_instance_sequence = 0U;
  std::uint32_t slot = 0U;
  std::uint32_t generation = 0U;

  [[nodiscard]] bool operator==(const EntityIdV1 &) const = default;
};

struct WorldEntityV1 {
  EntityIdV1 id;
  EntityArchetypeIdV1 archetype_id = 0U;
  WorldTransformV1 transform;

  [[nodiscard]] bool operator==(const WorldEntityV1 &) const = default;
};

// A World owns only one loaded level and its transient entities. Persistent
// inventory/progression belongs to GameSession, so changing levels cannot
// accidentally retain pointers into a previous level package.
class WorldV1 final {
public:
  WorldV1() = default;

  void load_level(GameSessionV1 &session, const LevelRequestV1 &request);
  void unload_level() noexcept;

  [[nodiscard]] EntityIdV1
  spawn_entity(const WorldEntityDefinitionV1 &definition);
  [[nodiscard]] bool destroy_entity(EntityIdV1 id);

  [[nodiscard]] WorldEntityV1 *find_entity(EntityIdV1 id) noexcept;
  [[nodiscard]] const WorldEntityV1 *find_entity(EntityIdV1 id) const noexcept;

  // Slot-order iteration is stable and independent of memory addresses.
  [[nodiscard]] const WorldEntityV1 *
  entity_at_slot(std::uint32_t slot) const noexcept;
  [[nodiscard]] std::size_t slot_count() const noexcept;
  [[nodiscard]] std::size_t entity_count() const noexcept;
  [[nodiscard]] const std::optional<ActiveLevelV1> &
  active_level() const noexcept;

private:
  struct EntitySlotV1 {
    std::uint32_t generation = 1U;
    std::optional<WorldEntityV1> entity;
  };

  void clear_entities() noexcept;

  std::optional<ActiveLevelV1> active_level_;
  std::vector<EntitySlotV1> entity_slots_;
  // Kept in descending order so pop_back deterministically reuses the lowest
  // available slot.
  std::vector<std::uint32_t> free_slots_;
  std::size_t entity_count_ = 0U;
};

void validate_world_transform_v1(const WorldTransformV1 &transform);

} // namespace openrc::game
