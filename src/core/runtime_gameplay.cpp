#include "openrc/runtime_gameplay.hpp"

#include "openrc/rac_pad_input.hpp"
#include "openrc/rac_player_locomotion.hpp"

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

[[nodiscard]] SpawnPointIdV1 resolve_runtime_spawn_point_id(
    const RuntimeLevelFoundationV1 &foundation,
    const std::optional<SpawnPointIdV1> requested_spawn_point_id) noexcept {
  return requested_spawn_point_id.value_or(
      foundation.bootstrap.default_spawn_id);
}

void validate_state(
    const RuntimeLevelFoundationV1 &foundation,
    const RuntimeGameplayProfileV1 &profile, const GameSessionV1 &session,
    const WorldV1 &world, const PlayerSimulationV1 &player,
    const GameInputStateV1 &input, const FixedStepAccumulatorV1 &fixed_step,
    const PlayerCombatV1 &combat,
    const std::vector<EntityGameplayItemTotalV1> &item_totals,
    const std::optional<EntityGameplayRuntimeV1> &entity_gameplay) {
  const auto player_snapshot = player.snapshot();
  const auto &active_level = world.active_level();
  const auto expected_tick = session.next_tick_index();

  if (!active_level || !session.active_level_id() ||
      session.pending_level_request() ||
      active_level->level_id != foundation.level_id ||
      *session.active_level_id() != foundation.level_id ||
      active_level->spawn_point_id != session.active_spawn_point_id() ||
      active_level->instance_sequence !=
          session.level_instance_sequence()) {
    fail("Runtime gameplay has inconsistent active-level state");
  }
  if (foundation.bootstrap.level_id != foundation.level_id) {
    fail("Runtime gameplay foundation identity changed after loading");
  }
  if (player_snapshot.next_tick_index != expected_tick ||
      input.next_tick_index() != expected_tick ||
      fixed_step.next_tick_index() != expected_tick ||
      combat.snapshot().next_tick_index != expected_tick) {
    fail("Runtime gameplay fixed-tick sequences diverged");
  }
  if (fixed_step.config() != profile.fixed_step ||
      player.profile().fixed_ticks_per_second !=
          profile.fixed_step.ticks_per_second ||
      player.profile().character != profile.character ||
      player.profile().death_height_world !=
          foundation.bootstrap.death_height_world ||
      combat.profile() != profile.combat) {
    fail("Runtime gameplay policy diverged from the active player");
  }
  if (entity_gameplay &&
      (!entity_gameplay->loaded() ||
       entity_gameplay->next_tick_index() != expected_tick ||
       !entity_gameplay->world().active_level() ||
       entity_gameplay->world().active_level()->level_id !=
           active_level->level_id ||
       entity_gameplay->world().active_level()->instance_sequence !=
           active_level->instance_sequence ||
       entity_gameplay->item_totals() != item_totals)) {
    fail("Runtime entity gameplay level or fixed-tick sequence diverged");
  }
}

[[nodiscard]] RuntimeGameplaySnapshotV1 make_runtime_gameplay_snapshot(
    const GameSessionV1 &session, const WorldV1 &world,
    const PlayerSimulationV1 &player, const GameInputStateV1 &input,
    const FixedStepAccumulatorV1 &fixed_step, const PlayerCombatV1 &combat,
    const std::vector<EntityGameplayItemTotalV1> &item_totals,
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
      combat.snapshot(),
      item_totals,
      std::move(entity_gameplay_snapshot),
  };
}

void load_entity_gameplay_content(EntityGameplayRuntimeV1 &runtime,
                                  const RuntimeGameplayEntityContentV1 &content,
                                  const std::uint32_t foundation_level_id,
                                  const std::uint64_t next_tick_index,
                                  const std::uint64_t level_instance_sequence) {
  if (content.entity_scene.level_id != foundation_level_id ||
      content.gameplay_scene.level_id != foundation_level_id ||
      (content.destructible_scene &&
       content.destructible_scene->level_id != foundation_level_id)) {
    fail("Runtime entity/gameplay content belongs to a different foundation");
  }
  try {
    if (content.destructible_scene) {
      runtime.load_scene(content.entity_scene, content.gameplay_scene,
                         *content.destructible_scene, content.limits,
                         next_tick_index, level_instance_sequence);
    } else {
      runtime.load_scene(content.entity_scene, content.gameplay_scene,
                         content.limits, next_tick_index,
                         level_instance_sequence);
    }
  } catch (const EntityGameplayRuntimeError &error) {
    fail("Cannot load runtime entity/gameplay content: " +
         std::string(error.what()));
  }
}

[[nodiscard]] std::int16_t quantize_unit_axis(const double value) noexcept {
  const auto scaled = std::clamp(value, -1.0, 1.0) *
                      static_cast<double>(kGameInputAxisMagnitudeV1);
  return canonical_game_input_axis_v1(
      static_cast<std::int32_t>(std::lround(scaled)));
}

[[nodiscard]] RuntimeMovementAxesV1
canonical_source_movement(const RacPadAxesResponseV1 &source_axes) noexcept {
  RuntimeMovementAxesV1 result{source_axes.move_x, source_axes.move_y};
  const auto length = std::hypot(result.move_x, result.move_y);
  if (length > 1.0) {
    result.move_x /= length;
    result.move_y /= length;
  }
  return result;
}

struct RuntimeTickMovementV1 {
  GameInputCommandV1 command;
  CharacterMotionV1 player_motion;
  RacPlayerGroundMovementV1 source_standard_ground_movement;
};

[[nodiscard]] RuntimeTickMovementV1
prepare_movement_for_tick(GameInputCommandV1 command,
                          const bool player_was_grounded,
                          const double fixed_delta_seconds,
                          const RuntimeMovementMapperV1 &movement_mapper) {
  const auto source_axes = decode_rac_pad_axes_response_v1(command.axes);
  const auto source_movement = canonical_source_movement(source_axes);
  const auto source_standard_ground_movement =
      map_rac_player_standard_ground_movement_v1(source_axes.move_x,
                                                 source_axes.move_y);

  // Keep the deterministic command useful for replay/debugging: it records
  // the recovered source response (and, below, its world-space rotation), not
  // the platform's pre-response stick value.
  command.axes = {
      quantize_unit_axis(source_movement.move_x),
      quantize_unit_axis(source_movement.move_y),
      quantize_unit_axis(source_axes.look_x),
      quantize_unit_axis(source_axes.look_y),
  };

  auto mapped_movement = source_movement;
  if (movement_mapper) {
    try {
      mapped_movement =
          movement_mapper(command, source_movement, fixed_delta_seconds);
    } catch (const std::exception &error) {
      fail("Runtime movement mapper failed: " + std::string(error.what()));
    } catch (...) {
      fail("Runtime movement mapper failed with an unknown exception");
    }
  }

  if (!std::isfinite(mapped_movement.move_x) ||
      !std::isfinite(mapped_movement.move_y)) {
    fail("Runtime movement mapper returned a non-finite axis");
  }
  const auto source_length =
      std::hypot(source_movement.move_x, source_movement.move_y);
  const auto mapped_length =
      std::hypot(mapped_movement.move_x, mapped_movement.move_y);
  constexpr double kRotationLengthTolerance = 1.0e-9;
  if (mapped_length > 1.0 + kRotationLengthTolerance ||
      std::abs(mapped_length - source_length) > kRotationLengthTolerance) {
    fail("Runtime movement mapper changed the source movement magnitude");
  }

  command.axes.move_x = quantize_unit_axis(mapped_movement.move_x);
  command.axes.move_y = quantize_unit_axis(mapped_movement.move_y);
  try {
    validate_game_input_command_v1(command);
  } catch (const GameInputError &error) {
    fail("Runtime movement mapping produced an invalid command: " +
         std::string(error.what()));
  }

  CharacterMotionV1 player_motion;
  player_motion.move_x = mapped_movement.move_x;
  player_motion.move_y = mapped_movement.move_y;
  if (player_was_grounded) {
    player_motion.target_horizontal_speed = static_cast<double>(
        source_standard_ground_movement.target_ground_speed);
    if (mapped_length > 0.0) {
      player_motion.move_x /= mapped_length;
      player_motion.move_y /= mapped_length;
    }
  }
  return {std::move(command), player_motion, source_standard_ground_movement};
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
  character.maximum_ground_speed = kRacPlayerStandardFastGroundSpeedV1;
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

  PlayerCombatProfileV1 combat;
  combat.semantic_key = "openrc.attack/wrench-primary";
  combat.source_authored_id = 0U;
  combat.damage_channel = kDamageChannelMeleeV1;
  combat.damage = 1U;
  combat.startup_ticks = 2U;
  combat.active_ticks = 3U;
  combat.recovery_ticks = 13U;
  combat.forward_start = 0.20;
  combat.forward_end = 1.40;
  combat.vertical_offset = 0.85;
  combat.radius = 0.55;

  return RuntimeGameplayProfileV1{
      FixedStepConfigV1{60U, 8U, 250'000'000U},
      character,
      std::move(combat),
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
  if (profile.character.maximum_ground_speed <
      static_cast<double>(kRacPlayerStandardFastGroundSpeedV1)) {
    fail("Runtime gameplay character policy cannot represent the recovered "
         "standard grounded speed");
  }
  try {
    validate_player_combat_profile_v1(profile.combat);
  } catch (const PlayerCombatError &error) {
    fail("Invalid runtime gameplay combat policy: " +
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

namespace {

[[nodiscard]] GameSessionV1
make_game_session(const RuntimeGameplaySessionOptionsV1 &options) {
  try {
    if (options.initial_persistent_state) {
      return GameSessionV1(options.deterministic_seed,
                           *options.initial_persistent_state,
                           options.persistent_state_limits);
    }
    return GameSessionV1(options.deterministic_seed);
  } catch (const GameWorldError &error) {
    fail("Cannot initialize runtime session state: " +
         std::string(error.what()));
  }
}

class RuntimeFrameAdvanceGuard final {
public:
  explicit RuntimeFrameAdvanceGuard(bool &active) : active_(active) {
    if (active_) {
      fail("Runtime frame advancement cannot be reentered");
    }
    active_ = true;
  }
  ~RuntimeFrameAdvanceGuard() { active_ = false; }
  RuntimeFrameAdvanceGuard(const RuntimeFrameAdvanceGuard &) = delete;
  RuntimeFrameAdvanceGuard &
  operator=(const RuntimeFrameAdvanceGuard &) = delete;

private:
  bool &active_;
};

} // namespace

RuntimeGameplaySessionV1::RuntimeGameplaySessionV1(
    RuntimeLevelFoundationV1 foundation,
    RuntimeGameplaySessionOptionsV1 options)
    : foundation_(std::move(foundation)),
      profile_(validated_profile(std::move(options.profile))),
      session_(make_game_session(options)),
      player_(make_checked_level_player(
          foundation_, profile_,
          resolve_runtime_spawn_point_id(foundation_, options.spawn_point_id),
          0U)),
      input_(0U), fixed_step_(profile_.fixed_step), combat_(profile_.combat) {
  const auto resolved_spawn_point_id =
      resolve_runtime_spawn_point_id(foundation_, options.spawn_point_id);
  const auto request =
      session_.request_level(foundation_.level_id, resolved_spawn_point_id,
                             options.level_request_reason);
  world_.load_level(session_, request);
  if (options.entity_gameplay) {
    const auto &active_level = world_.active_level();
    if (!active_level) {
      fail("Runtime gameplay lost its initial active-level identity");
    }
    entity_gameplay_.emplace();
    load_entity_gameplay_content(
        *entity_gameplay_, *options.entity_gameplay, foundation_.level_id,
        session_.next_tick_index(), active_level->instance_sequence);
    if (entity_gameplay_->item_totals() != item_totals_) {
      fail("Runtime gameplay initialized inconsistent persistent item totals");
    }
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
  const auto resolved_spawn_point_id =
      resolve_runtime_spawn_point_id(foundation, spawn_point_id);
  auto next_player =
      make_checked_level_player(foundation, profile_, resolved_spawn_point_id,
                                session_.next_tick_index());
  auto next_session = session_;
  auto next_world = world_;
  auto next_input = input_;
  const auto previous_combat = combat_.snapshot();
  auto next_combat = PlayerCombatV1(
      profile_.combat, PlayerCombatSnapshotV1{session_.next_tick_index(),
                                              previous_combat.attack_sequence,
                                              PlayerCombatPhaseV1::idle, 0U});
  auto next_entity_gameplay = entity_gameplay_;
  auto next_item_totals = item_totals_;

  const auto request = next_session.request_level(
      foundation.level_id, resolved_spawn_point_id, reason);
  next_world.load_level(next_session, request);
  const auto &next_active_level = next_world.active_level();
  if (!next_active_level) {
    fail("Runtime gameplay lost its replacement active-level identity");
  }
  if (entity_gameplay) {
    if (!next_entity_gameplay) {
      next_entity_gameplay.emplace();
    }
    load_entity_gameplay_content(
        *next_entity_gameplay, *entity_gameplay, foundation.level_id,
        next_session.next_tick_index(), next_active_level->instance_sequence);
    try {
      next_entity_gameplay->restore_item_totals(next_item_totals);
    } catch (const EntityGameplayRuntimeError &error) {
      fail("Cannot carry persistent item totals into runtime gameplay: " +
           std::string(error.what()));
    }
  } else {
    next_entity_gameplay.reset();
  }
  next_input.reset(next_session.next_tick_index());
  validate_state(foundation, profile_, next_session, next_world, next_player,
                 next_input, fixed_step_, next_combat, next_item_totals,
                 next_entity_gameplay);

  foundation_ = std::move(foundation);
  session_ = std::move(next_session);
  world_ = std::move(next_world);
  player_ = std::move(next_player);
  input_ = std::move(next_input);
  combat_ = std::move(next_combat);
  item_totals_ = std::move(next_item_totals);
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
  const RuntimeFrameAdvanceGuard frame_guard(advancing_frame_);
  validate_tick_invariants();

  auto next_fixed_step = fixed_step_;
  auto next_input = input_;
  auto next_player = player_;
  auto next_session = session_;
  auto next_combat = combat_;
  auto next_entity_gameplay = entity_gameplay_;
  auto next_item_totals = item_totals_;
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
    const auto raw_command = next_input.consume_for_tick(tick_index);
    auto movement = prepare_movement_for_tick(
        raw_command, next_player.snapshot().character.grounded,
        fixed_delta_seconds, movement_mapper);
    const auto player_step =
        next_player.fixed_update(foundation_.collision_world, movement.command,
                                 std::move(movement.player_motion));
    PlayerCombatStepV1 combat_step;
    try {
      combat_step =
          next_combat.fixed_update(movement.command, next_player.snapshot());
    } catch (const PlayerCombatError &error) {
      fail("Runtime player combat tick failed: " + std::string(error.what()));
    }
    std::vector<EntityGameplayEventV1> gameplay_events;
    if (next_entity_gameplay) {
      const auto player_snapshot = next_player.snapshot();
      try {
        const auto player_capsule = EntityGameplayPlayerCapsuleV1{
            player_snapshot.character.feet_position,
            profile_.character.capsule_radius,
            profile_.character.capsule_height};
        if (combat_step.damage_pulse) {
          gameplay_events = next_entity_gameplay->fixed_tick(
              tick_index, player_capsule,
              std::span<const GameplayDamagePulseV1>(&*combat_step.damage_pulse,
                                                     1U));
        } else {
          gameplay_events =
              next_entity_gameplay->fixed_tick(tick_index, player_capsule);
        }
      } catch (const EntityGameplayRuntimeError &error) {
        fail("Runtime entity gameplay tick failed: " +
             std::string(error.what()));
      }
    }
    next_session.commit_simulation_tick(movement.command);
    result.ticks.push_back(RuntimeGameplayTickV1{
        std::move(movement.command), movement.source_standard_ground_movement,
        player_step, next_player.snapshot(), std::move(combat_step),
        std::move(gameplay_events)});
  }

  if (next_entity_gameplay) {
    next_item_totals = next_entity_gameplay->item_totals();
  }

  validate_state(foundation_, profile_, next_session, world_, next_player,
                 next_input, next_fixed_step, next_combat, next_item_totals,
                 next_entity_gameplay);
  result.snapshot = make_runtime_gameplay_snapshot(
      next_session, world_, next_player, next_input, next_fixed_step,
      next_combat, next_item_totals, next_entity_gameplay);

  static_assert(
      std::is_nothrow_move_assignable_v<FixedStepAccumulatorV1> &&
      std::is_nothrow_move_assignable_v<GameInputStateV1> &&
      std::is_nothrow_move_assignable_v<PlayerSimulationV1> &&
      std::is_nothrow_move_assignable_v<GameSessionV1> &&
      std::is_nothrow_move_assignable_v<PlayerCombatV1> &&
      std::is_nothrow_move_assignable_v<
          std::vector<EntityGameplayItemTotalV1>> &&
      std::is_nothrow_move_assignable_v<
          std::optional<EntityGameplayRuntimeV1>> &&
      std::is_nothrow_move_constructible_v<RuntimeGameplayFrameAdvanceV1>);
  fixed_step_ = std::move(next_fixed_step);
  input_ = std::move(next_input);
  player_ = std::move(next_player);
  session_ = std::move(next_session);
  combat_ = std::move(next_combat);
  item_totals_ = std::move(next_item_totals);
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
    // Pass an lvalue so the entity runtime validates and commits a copy first;
    // moving the same canonical vector into session ownership cannot fail.
    entity_gameplay_->restore_item_totals(totals);
  } catch (const EntityGameplayRuntimeError &error) {
    fail("Cannot restore runtime gameplay item totals: " +
         std::string(error.what()));
  }
  static_assert(std::is_nothrow_move_assignable_v<
                std::vector<EntityGameplayItemTotalV1>>);
  item_totals_ = std::move(totals);
  validate_tick_invariants();
}

void RuntimeGameplaySessionV1::apply_persistent_state_writes(
    const std::span<const SessionStateWriteV1> writes,
    const std::uint64_t expected_revision) {
  if (advancing_frame_) {
    fail("Persistent-state writes cannot reenter an active runtime frame");
  }
  try {
    session_.apply_persistent_state_writes(writes, expected_revision);
  } catch (const GameWorldError &error) {
    fail("Cannot update runtime persistent state: " +
         std::string(error.what()));
  }
}

RuntimeGameplaySnapshotV1 RuntimeGameplaySessionV1::snapshot() const {
  return make_runtime_gameplay_snapshot(session_, world_, player_, input_,
                                        fixed_step_, combat_, item_totals_,
                                        entity_gameplay_);
}

std::uint64_t
RuntimeGameplaySessionV1::interpolation_numerator() const noexcept {
  return fixed_step_.interpolation_numerator();
}

std::uint64_t RuntimeGameplaySessionV1::item_total(
    const std::string_view item_key) const noexcept {
  const auto found = std::lower_bound(
      item_totals_.begin(), item_totals_.end(), item_key,
      [](const EntityGameplayItemTotalV1 &total, const std::string_view key) {
        return total.item_key < key;
      });
  return found != item_totals_.end() && found->item_key == item_key
             ? found->amount
             : 0U;
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

const PlayerCombatV1 &RuntimeGameplaySessionV1::combat() const noexcept {
  return combat_;
}

void RuntimeGameplaySessionV1::validate_tick_invariants() const {
  validate_state(foundation_, profile_, session_, world_, player_, input_,
                 fixed_step_, combat_, item_totals_, entity_gameplay_);
}

} // namespace openrc::game
