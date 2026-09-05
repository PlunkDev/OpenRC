#include "openrc/runtime_gameplay.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>
#include <string>
#include <type_traits>
#include <utility>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimeGameplayError(message);
}

[[nodiscard]] RuntimeGameplayProfileV1
validated_profile(RuntimeGameplayProfileV1 profile) {
  validate_runtime_gameplay_profile_v1(profile);
  return profile;
}

void validate_checkpoint_collision_envelope(
    const PlayerCheckpointV1 &checkpoint,
    const RuntimeGameplayProfileV1 &profile) {
  const auto ticks_per_second =
      static_cast<double>(profile.fixed_step.ticks_per_second);
  const auto maximum_vertical_speed = std::max(
      profile.character.jump_speed, profile.character.maximum_fall_speed);
  const auto slope_cosine = std::cos(profile.character.maximum_slope_degrees *
                                     std::numbers::pi_v<double> / 180.0);
  const auto maximum_horizontal_tick = profile.character.maximum_ground_speed /
                                       (ticks_per_second * slope_cosine);
  const auto maximum_vertical_tick = maximum_vertical_speed / ticks_per_second;
  const auto effective_radius =
      profile.character.capsule_radius + profile.character.skin_width;
  constexpr double kEnvelopeEpsilon = 1.0e-6;
  const auto horizontal_margin =
      effective_radius + maximum_horizontal_tick + kEnvelopeEpsilon;
  const auto below_margin = std::max(profile.character.skin_width,
                                     profile.character.ground_probe_distance) +
                            maximum_horizontal_tick + maximum_vertical_tick +
                            kEnvelopeEpsilon;
  const auto above_margin =
      profile.character.capsule_height + profile.character.skin_width +
      profile.character.step_height + maximum_horizontal_tick +
      maximum_vertical_tick + kEnvelopeEpsilon;
  try {
    static_cast<void>(collision_world_aabb_to_q6_v1(
        {checkpoint.feet_position.x - horizontal_margin,
         checkpoint.feet_position.y - horizontal_margin,
         checkpoint.feet_position.z - below_margin},
        {checkpoint.feet_position.x + horizontal_margin,
         checkpoint.feet_position.y + horizontal_margin,
         checkpoint.feet_position.z + above_margin}));
  } catch (const CollisionWorldError &error) {
    fail("A runtime player checkpoint exceeds the collision coordinate "
         "domain: " +
         std::string(error.what()));
  }
}

[[nodiscard]] PlayerSimulationV1
make_checked_level_player(const RuntimeLevelFoundationV1 &foundation,
                          const RuntimeGameplayProfileV1 &profile,
                          const std::optional<SpawnPointIdV1> spawn_point_id,
                          const std::uint64_t next_tick_index) {
  try {
    auto player = make_runtime_level_player_simulation_v1(
        foundation, profile.character, profile.fixed_step.ticks_per_second,
        spawn_point_id, next_tick_index);
    validate_checkpoint_collision_envelope(player.snapshot().checkpoint,
                                           profile);
    return player;
  } catch (const RuntimeGameplayError &) {
    throw;
  } catch (const std::exception &error) {
    fail("Cannot create the runtime player from the prepared level: " +
         std::string(error.what()));
  }
}

void validate_state(
    const RuntimeLevelFoundationV1 &foundation,
    const RuntimeGameplayProfileV1 &profile, const GameSessionV1 &session,
    const WorldV1 &world, const PlayerSimulationV1 &player,
    const GameInputStateV1 &input, const FixedStepAccumulatorV1 &fixed_step,
    const std::optional<EntityGameplayRuntimeV1> &entity_gameplay) {
  const auto player_snapshot = player.snapshot();
  const auto session_snapshot = session.snapshot();
  const auto &active_level = world.active_level();
  const auto expected_tick = session.next_tick_index();

  if (!active_level || !session.active_level_id() ||
      session.pending_level_request() ||
      active_level->level_id != foundation.level_id ||
      *session.active_level_id() != foundation.level_id ||
      active_level->spawn_point_id != session.active_spawn_point_id() ||
      active_level->instance_sequence !=
          session_snapshot.level_instance_sequence) {
    fail("Runtime gameplay has inconsistent active-level state");
  }
  if (foundation.bootstrap.level_id != foundation.level_id) {
    fail("Runtime gameplay foundation identity changed after loading");
  }
  if (player_snapshot.next_tick_index != expected_tick ||
      input.next_tick_index() != expected_tick ||
      fixed_step.next_tick_index() != expected_tick) {
    fail("Runtime gameplay fixed-tick sequences diverged");
  }
  if (fixed_step.config() != profile.fixed_step ||
      player.profile().fixed_ticks_per_second !=
          profile.fixed_step.ticks_per_second ||
      player.profile().character != profile.character ||
      player.profile().death_height_world !=
          foundation.bootstrap.death_height_world) {
    fail("Runtime gameplay policy diverged from the active player");
  }
  if (entity_gameplay &&
      (!entity_gameplay->loaded() ||
       entity_gameplay->next_tick_index() != expected_tick)) {
    fail("Runtime entity gameplay fixed-tick sequence diverged");
  }
}

[[nodiscard]] RuntimeGameplaySnapshotV1 make_runtime_gameplay_snapshot(
    const GameSessionV1 &session, const WorldV1 &world,
    const PlayerSimulationV1 &player, const GameInputStateV1 &input,
    const FixedStepAccumulatorV1 &fixed_step,
    const std::optional<EntityGameplayRuntimeV1> &entity_gameplay) {
  std::optional<EntityGameplaySnapshotV1> entity_gameplay_snapshot;
  if (entity_gameplay) {
    try {
      entity_gameplay_snapshot = entity_gameplay->snapshot();
    } catch (const EntityGameplayRuntimeError &error) {
      fail("Cannot snapshot runtime entity gameplay: " +
           std::string(error.what()));
    }
  }
  return RuntimeGameplaySnapshotV1{
      session.snapshot(),
      world.active_level(),
      player.snapshot(),
      input.current_sample(),
      input.pending_pressed_buttons(),
      input.pending_released_buttons(),
      input.next_tick_index(),
      fixed_step.next_tick_index(),
      fixed_step.interpolation_numerator(),
      fixed_step.total_dropped_step_count(),
      fixed_step.total_discarded_elapsed_nanoseconds(),
      std::move(entity_gameplay_snapshot),
  };
}

void load_entity_gameplay_content(EntityGameplayRuntimeV1 &runtime,
                                  const RuntimeGameplayEntityContentV1 &content,
                                  const std::uint32_t foundation_level_id,
                                  const std::uint64_t next_tick_index) {
  if (content.entity_scene.level_id != foundation_level_id ||
      content.gameplay_scene.level_id != foundation_level_id) {
    fail("Runtime entity/gameplay content belongs to a different foundation");
  }
  try {
    runtime.load_scene(content.entity_scene, content.gameplay_scene,
                       content.limits, next_tick_index);
  } catch (const EntityGameplayRuntimeError &error) {
    fail("Cannot load runtime entity/gameplay content: " +
         std::string(error.what()));
  }
}

[[nodiscard]] GameInputCommandV1
map_movement_for_tick(GameInputCommandV1 command,
                      const double fixed_delta_seconds,
                      const RuntimeMovementMapperV1 &movement_mapper) {
  if (!movement_mapper) {
    return command;
  }

  RuntimeMovementAxesV1 mapped;
  try {
    mapped = movement_mapper(command, fixed_delta_seconds);
  } catch (const std::exception &error) {
    fail("Runtime movement mapper failed: " + std::string(error.what()));
  } catch (...) {
    fail("Runtime movement mapper failed with an unknown exception");
  }
  command.axes.move_x = mapped.move_x;
  command.axes.move_y = mapped.move_y;
  try {
    validate_game_input_command_v1(command);
  } catch (const GameInputError &error) {
    fail("Runtime movement mapper returned invalid quantized axes: " +
         std::string(error.what()));
  }
  return command;
}

} // namespace

RuntimeGameplayProfileV1 make_runtime_gameplay_profile_v1() {
  CharacterControllerProfileV1 character;
  character.capsule_radius = 0.35;
  character.capsule_height = 1.65;
  character.skin_width = 0.02;
  character.ground_probe_distance = 0.30;
  character.step_height = 0.55;
  character.maximum_slope_degrees = 50.0;
  character.maximum_ground_speed = 6.0;
  character.ground_acceleration = 30.0;
  character.ground_deceleration = 40.0;
  character.air_acceleration = 10.0;
  character.gravity = 18.0;
  character.jump_speed = 7.0;
  character.maximum_fall_speed = 40.0;
  character.maximum_substep_distance = 0.05;
  character.maximum_motion_substeps = 256U;
  character.maximum_slide_iterations = 4U;
  character.maximum_depenetration_iterations = 64U;
  character.collision_layers = kCollisionAllLayersMaskV1;
  character.query_limits = CollisionQueryLimitsV1{4096U, 8192U};

  return RuntimeGameplayProfileV1{
      FixedStepConfigV1{60U, 8U, 250'000'000U},
      character,
  };
}

void validate_runtime_gameplay_profile_v1(
    const RuntimeGameplayProfileV1 &profile) {
  if (profile.fixed_step.max_steps_per_advance >
      kRuntimeGameplayMaximumStepsPerAdvanceV1) {
    fail("Runtime gameplay fixed-step catch-up limit is too large");
  }
  try {
    static_cast<void>(FixedStepAccumulatorV1(profile.fixed_step));
  } catch (const FixedStepError &error) {
    fail("Invalid runtime gameplay fixed-step policy: " +
         std::string(error.what()));
  }
  try {
    validate_character_controller_profile_v1(profile.character);
  } catch (const CharacterControllerError &error) {
    fail("Invalid runtime gameplay character policy: " +
         std::string(error.what()));
  }

  const auto ticks_per_second =
      static_cast<double>(profile.fixed_step.ticks_per_second);
  const auto maximum_vertical_speed = std::max(
      profile.character.jump_speed, profile.character.maximum_fall_speed);
  const auto slope_cosine = std::cos(profile.character.maximum_slope_degrees *
                                     std::numbers::pi_v<double> / 180.0);
  const auto maximum_motion_distance =
      profile.character.maximum_substep_distance *
      static_cast<double>(profile.character.maximum_motion_substeps);
  const auto maximum_horizontal_tick = profile.character.maximum_ground_speed /
                                       (ticks_per_second * slope_cosine);
  const auto maximum_vertical_tick = maximum_vertical_speed / ticks_per_second;
  if (!std::isfinite(maximum_motion_distance) ||
      !std::isfinite(maximum_horizontal_tick) ||
      !std::isfinite(maximum_vertical_tick) ||
      maximum_horizontal_tick > maximum_motion_distance ||
      maximum_vertical_tick > maximum_motion_distance) {
    fail("Runtime gameplay tick rate can exceed the character motion-substep "
         "budget");
  }
}

RuntimeGameplaySessionV1::RuntimeGameplaySessionV1(
    RuntimeLevelFoundationV1 foundation,
    RuntimeGameplaySessionOptionsV1 options)
    : foundation_(std::move(foundation)),
      profile_(validated_profile(std::move(options.profile))),
      session_(options.deterministic_seed),
      player_(make_checked_level_player(foundation_, profile_,
                                        options.spawn_point_id, 0U)),
      input_(0U), fixed_step_(profile_.fixed_step) {
  const auto request =
      session_.request_level(foundation_.level_id, options.spawn_point_id,
                             options.level_request_reason);
  world_.load_level(session_, request);
  if (options.entity_gameplay) {
    entity_gameplay_.emplace();
    load_entity_gameplay_content(*entity_gameplay_, *options.entity_gameplay,
                                 foundation_.level_id,
                                 session_.next_tick_index());
  }
  validate_tick_invariants();
}

void RuntimeGameplaySessionV1::load_level(
    RuntimeLevelFoundationV1 foundation,
    const std::optional<SpawnPointIdV1> spawn_point_id,
    const LevelRequestReasonV1 reason) {
  load_level_impl(std::move(foundation), std::nullopt, spawn_point_id, reason);
}

void RuntimeGameplaySessionV1::load_level(
    RuntimeLevelFoundationV1 foundation,
    RuntimeGameplayEntityContentV1 entity_gameplay,
    const std::optional<SpawnPointIdV1> spawn_point_id,
    const LevelRequestReasonV1 reason) {
  load_level_impl(std::move(foundation), std::move(entity_gameplay),
                  spawn_point_id, reason);
}

void RuntimeGameplaySessionV1::load_level_impl(
    RuntimeLevelFoundationV1 foundation,
    std::optional<RuntimeGameplayEntityContentV1> entity_gameplay,
    const std::optional<SpawnPointIdV1> spawn_point_id,
    const LevelRequestReasonV1 reason) {
  auto next_player = make_checked_level_player(
      foundation, profile_, spawn_point_id, session_.next_tick_index());
  auto next_session = session_;
  auto next_world = world_;
  auto next_input = input_;
  auto next_entity_gameplay = entity_gameplay_;

  if (entity_gameplay) {
    if (!next_entity_gameplay) {
      next_entity_gameplay.emplace();
    }
    load_entity_gameplay_content(*next_entity_gameplay, *entity_gameplay,
                                 foundation.level_id,
                                 session_.next_tick_index());
  } else {
    next_entity_gameplay.reset();
  }

  const auto request =
      next_session.request_level(foundation.level_id, spawn_point_id, reason);
  next_world.load_level(next_session, request);
  next_input.reset(next_session.next_tick_index());
  validate_state(foundation, profile_, next_session, next_world, next_player,
                 next_input, fixed_step_, next_entity_gameplay);

  foundation_ = std::move(foundation);
  session_ = std::move(next_session);
  world_ = std::move(next_world);
  player_ = std::move(next_player);
  input_ = std::move(next_input);
  entity_gameplay_ = std::move(next_entity_gameplay);
  validate_tick_invariants();
}

void RuntimeGameplaySessionV1::submit_input_sample(
    const GameInputSampleV1 &sample) {
  input_.submit_sample(sample);
}

void RuntimeGameplaySessionV1::release_input() noexcept {
  input_.release_all();
}

RuntimeGameplayFrameAdvanceV1 RuntimeGameplaySessionV1::advance_frame(
    const std::uint64_t elapsed_nanoseconds,
    const RuntimeMovementMapperV1 &movement_mapper) {
  return advance_frame_impl(elapsed_nanoseconds, std::nullopt, movement_mapper);
}

RuntimeGameplayFrameAdvanceV1 RuntimeGameplaySessionV1::advance_frame(
    const std::uint64_t elapsed_nanoseconds, const GameInputSampleV1 &sample,
    const RuntimeMovementMapperV1 &movement_mapper) {
  return advance_frame_impl(elapsed_nanoseconds, sample, movement_mapper);
}

RuntimeGameplayFrameAdvanceV1 RuntimeGameplaySessionV1::advance_frame_impl(
    const std::uint64_t elapsed_nanoseconds,
    const std::optional<GameInputSampleV1> &new_sample,
    const RuntimeMovementMapperV1 &movement_mapper) {
  validate_tick_invariants();

  auto next_fixed_step = fixed_step_;
  auto next_input = input_;
  auto next_player = player_;
  auto next_session = session_;
  auto next_entity_gameplay = entity_gameplay_;
  if (new_sample) {
    next_input.submit_sample(*new_sample);
  }

  RuntimeGameplayFrameAdvanceV1 result;
  result.fixed_step = next_fixed_step.advance(elapsed_nanoseconds);
  if (result.fixed_step.first_tick_index != session_.next_tick_index()) {
    fail("Runtime gameplay fixed-step advance started at the wrong tick");
  }
  result.ticks.reserve(result.fixed_step.step_count);
  const auto fixed_delta_seconds =
      1.0 / static_cast<double>(profile_.fixed_step.ticks_per_second);
  for (std::uint32_t offset = 0U; offset < result.fixed_step.step_count;
       ++offset) {
    const auto tick_index = result.fixed_step.first_tick_index + offset;
    auto command = next_input.consume_for_tick(tick_index);
    command = map_movement_for_tick(std::move(command), fixed_delta_seconds,
                                    movement_mapper);
    const auto player_step =
        next_player.fixed_update(foundation_.collision_world, command);
    std::vector<EntityGameplayEventV1> gameplay_events;
    if (next_entity_gameplay) {
      const auto player_snapshot = next_player.snapshot();
      try {
        gameplay_events = next_entity_gameplay->fixed_tick(
            tick_index, EntityGameplayPlayerCapsuleV1{
                            player_snapshot.character.feet_position,
                            profile_.character.capsule_radius,
                            profile_.character.capsule_height});
      } catch (const EntityGameplayRuntimeError &error) {
        fail("Runtime entity gameplay tick failed: " +
             std::string(error.what()));
      }
    }
    next_session.commit_simulation_tick(command);
    result.ticks.push_back(RuntimeGameplayTickV1{command, player_step,
                                                 std::move(gameplay_events)});
  }

  validate_state(foundation_, profile_, next_session, world_, next_player,
                 next_input, next_fixed_step, next_entity_gameplay);
  result.snapshot = make_runtime_gameplay_snapshot(
      next_session, world_, next_player, next_input, next_fixed_step,
      next_entity_gameplay);

  static_assert(
      std::is_nothrow_move_assignable_v<FixedStepAccumulatorV1> &&
      std::is_nothrow_move_assignable_v<GameInputStateV1> &&
      std::is_nothrow_move_assignable_v<PlayerSimulationV1> &&
      std::is_nothrow_move_assignable_v<GameSessionV1> &&
      std::is_nothrow_move_assignable_v<
          std::optional<EntityGameplayRuntimeV1>> &&
      std::is_nothrow_move_constructible_v<RuntimeGameplayFrameAdvanceV1>);
  fixed_step_ = std::move(next_fixed_step);
  input_ = std::move(next_input);
  player_ = std::move(next_player);
  session_ = std::move(next_session);
  entity_gameplay_ = std::move(next_entity_gameplay);
  return result;
}

void RuntimeGameplaySessionV1::set_checkpoint(PlayerCheckpointV1 checkpoint,
                                              const bool reset_immediately) {
  validate_checkpoint_collision_envelope(checkpoint, profile_);
  player_.set_checkpoint(std::move(checkpoint), reset_immediately);
  validate_tick_invariants();
}

void RuntimeGameplaySessionV1::restore_item_totals(
    std::vector<EntityGameplayItemTotalV1> totals) {
  if (!entity_gameplay_) {
    fail("Runtime gameplay cannot restore item totals without neutral content");
  }
  try {
    entity_gameplay_->restore_item_totals(std::move(totals));
  } catch (const EntityGameplayRuntimeError &error) {
    fail("Cannot restore runtime gameplay item totals: " +
         std::string(error.what()));
  }
  validate_tick_invariants();
}

RuntimeGameplaySnapshotV1 RuntimeGameplaySessionV1::snapshot() const {
  return make_runtime_gameplay_snapshot(session_, world_, player_, input_,
                                        fixed_step_, entity_gameplay_);
}

std::uint64_t
RuntimeGameplaySessionV1::interpolation_numerator() const noexcept {
  return fixed_step_.interpolation_numerator();
}

std::uint64_t RuntimeGameplaySessionV1::item_total(
    const std::string_view item_key) const noexcept {
  return entity_gameplay_ ? entity_gameplay_->item_total(item_key) : 0U;
}

const EntityGameplayRuntimeV1 *
RuntimeGameplaySessionV1::entity_gameplay() const noexcept {
  return entity_gameplay_ ? &*entity_gameplay_ : nullptr;
}

const RuntimeLevelFoundationV1 &
RuntimeGameplaySessionV1::foundation() const noexcept {
  return foundation_;
}

const RuntimeGameplayProfileV1 &
RuntimeGameplaySessionV1::profile() const noexcept {
  return profile_;
}

const GameSessionV1 &RuntimeGameplaySessionV1::session() const noexcept {
  return session_;
}

const WorldV1 &RuntimeGameplaySessionV1::world() const noexcept {
  return world_;
}

const PlayerSimulationV1 &RuntimeGameplaySessionV1::player() const noexcept {
  return player_;
}

void RuntimeGameplaySessionV1::validate_tick_invariants() const {
  validate_state(foundation_, profile_, session_, world_, player_, input_,
                 fixed_step_, entity_gameplay_);
}

} // namespace openrc::game
