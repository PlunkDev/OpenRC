#include "openrc/player_simulation.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace openrc::game {
namespace {

constexpr double kAxisMagnitude =
    static_cast<double>(kGameInputAxisMagnitudeV1);
constexpr double kMovementEpsilon = 1.0e-12;

[[nodiscard]] bool finite(const CollisionVectorV1 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] bool
valid_reset_reason(const PlayerResetReasonV1 reason) noexcept {
  switch (reason) {
  case PlayerResetReasonV1::none:
  case PlayerResetReasonV1::manual:
  case PlayerResetReasonV1::checkpoint_activated:
  case PlayerResetReasonV1::fell_below_death_height:
    return true;
  }
  return false;
}

void validate_checkpoint_above_death_height(
    const PlayerCheckpointV1 &checkpoint,
    const PlayerSimulationProfileV1 &profile) {
  if (!(checkpoint.feet_position.z > profile.death_height_world)) {
    throw PlayerSimulationError(
        "A player checkpoint must be above the active death height");
  }
}

[[nodiscard]] CharacterControllerStateV1
checkpoint_character_state(const PlayerCheckpointV1 &checkpoint) noexcept {
  CharacterControllerStateV1 state;
  state.feet_position = checkpoint.feet_position;
  return state;
}

[[nodiscard]] PlayerSimulationProfileV1
validated_profile(PlayerSimulationProfileV1 profile) {
  validate_player_simulation_profile_v1(profile);
  return profile;
}

[[nodiscard]] CharacterControllerStateV1
validated_checkpoint_state(const PlayerCheckpointV1 &checkpoint,
                           const PlayerSimulationProfileV1 &profile) {
  validate_player_checkpoint_v1(checkpoint);
  validate_checkpoint_above_death_height(checkpoint, profile);
  return checkpoint_character_state(checkpoint);
}

[[nodiscard]] CharacterControllerStateV1
validated_snapshot_state(const PlayerSimulationSnapshotV1 &snapshot,
                         const PlayerSimulationProfileV1 &profile) {
  validate_player_simulation_snapshot_v1(snapshot);
  validate_checkpoint_above_death_height(snapshot.checkpoint, profile);
  return snapshot.character;
}

[[nodiscard]] double axis_to_unit(const std::int16_t axis) noexcept {
  return static_cast<double>(axis) / kAxisMagnitude;
}

void hash_byte(std::uint64_t &hash, const std::uint8_t value) noexcept {
  hash ^= value;
  hash *= UINT64_C(1099511628211);
}

void hash_u64(std::uint64_t &hash, const std::uint64_t value) noexcept {
  for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
    hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

void hash_double(std::uint64_t &hash, double value) noexcept {
  if (value == 0.0) {
    value = 0.0;
  }
  hash_u64(hash, std::bit_cast<std::uint64_t>(value));
}

} // namespace

void validate_player_checkpoint_v1(const PlayerCheckpointV1 &checkpoint) {
  if (!finite(checkpoint.feet_position) ||
      !std::isfinite(checkpoint.facing_yaw_radians)) {
    throw PlayerSimulationError(
        "A player checkpoint contains a non-finite transform");
  }
}

void validate_player_simulation_profile_v1(
    const PlayerSimulationProfileV1 &profile) {
  try {
    validate_character_controller_profile_v1(profile.character);
  } catch (const CharacterControllerError &error) {
    throw PlayerSimulationError(error.what());
  }
  if (profile.fixed_ticks_per_second == 0U ||
      !std::isfinite(profile.death_height_world)) {
    throw PlayerSimulationError(
        "A player-simulation profile has invalid tick or death policy");
  }
}

void validate_player_simulation_snapshot_v1(
    const PlayerSimulationSnapshotV1 &snapshot) {
  try {
    validate_character_controller_state_v1(snapshot.character);
  } catch (const CharacterControllerError &error) {
    throw PlayerSimulationError(error.what());
  }
  validate_player_checkpoint_v1(snapshot.checkpoint);
  if (!std::isfinite(snapshot.facing_yaw_radians) ||
      !valid_reset_reason(snapshot.last_reset_reason)) {
    throw PlayerSimulationError(
        "A player-simulation snapshot contains invalid state");
  }
}

std::uint64_t
hash_player_simulation_snapshot_v1(const PlayerSimulationSnapshotV1 &snapshot) {
  validate_player_simulation_snapshot_v1(snapshot);
  std::uint64_t hash = UINT64_C(14695981039346656037);
  hash_u64(hash, hash_character_controller_state_v1(snapshot.character));
  hash_u64(hash, snapshot.checkpoint.checkpoint_id);
  hash_double(hash, snapshot.checkpoint.feet_position.x);
  hash_double(hash, snapshot.checkpoint.feet_position.y);
  hash_double(hash, snapshot.checkpoint.feet_position.z);
  hash_double(hash, snapshot.checkpoint.facing_yaw_radians);
  hash_double(hash, snapshot.facing_yaw_radians);
  hash_u64(hash, snapshot.next_tick_index);
  hash_u64(hash, snapshot.reset_count);
  hash_byte(hash, static_cast<std::uint8_t>(snapshot.last_reset_reason));
  return hash;
}

PlayerSimulationV1::PlayerSimulationV1(PlayerSimulationProfileV1 profile,
                                       PlayerCheckpointV1 initial_checkpoint,
                                       const std::uint64_t next_tick_index)
    : profile_(validated_profile(std::move(profile))),
      character_(profile_.character,
                 validated_checkpoint_state(initial_checkpoint, profile_)),
      checkpoint_(std::move(initial_checkpoint)),
      facing_yaw_radians_(checkpoint_.facing_yaw_radians),
      next_tick_index_(next_tick_index) {}

PlayerSimulationV1::PlayerSimulationV1(
    PlayerSimulationProfileV1 profile,
    const PlayerSimulationSnapshotV1 &snapshot)
    : profile_(validated_profile(std::move(profile))),
      character_(profile_.character,
                 validated_snapshot_state(snapshot, profile_)),
      checkpoint_(snapshot.checkpoint),
      facing_yaw_radians_(snapshot.facing_yaw_radians),
      next_tick_index_(snapshot.next_tick_index),
      reset_count_(snapshot.reset_count),
      last_reset_reason_(snapshot.last_reset_reason) {}

PlayerSimulationStepV1
PlayerSimulationV1::fixed_update(const CollisionWorldV1 &collision_world,
                                 const GameInputCommandV1 &input) {
  CharacterMotionV1 motion;
  motion.move_x = axis_to_unit(input.axes.move_x);
  motion.move_y = axis_to_unit(input.axes.move_y);
  const auto movement_length = std::hypot(motion.move_x, motion.move_y);
  if (movement_length > 1.0) {
    motion.move_x /= movement_length;
    motion.move_y /= movement_length;
  }
  return fixed_update(collision_world, input, motion);
}

PlayerSimulationStepV1
PlayerSimulationV1::fixed_update(const CollisionWorldV1 &collision_world,
                                 const GameInputCommandV1 &input,
                                 CharacterMotionV1 motion) {
  try {
    validate_game_input_command_v1(input);
  } catch (const GameInputError &error) {
    throw PlayerSimulationError(error.what());
  }
  if (input.tick_index != next_tick_index_) {
    throw PlayerSimulationError(
        "Player input must be consumed in exact fixed-tick order");
  }
  if (next_tick_index_ == std::numeric_limits<std::uint64_t>::max()) {
    throw PlayerSimulationError(
        "The player-simulation tick sequence is exhausted");
  }

  PlayerSimulationStepV1 result;
  if ((input.pressed_buttons &
       game_button_mask_v1(GameButtonV1::reset_checkpoint)) != 0U) {
    reset_to_checkpoint(PlayerResetReasonV1::manual);
    result.reset_reason = last_reset_reason_;
    ++next_tick_index_;
    return result;
  }

  const auto movement_length = std::hypot(motion.move_x, motion.move_y);
  motion.jump_pressed =
      (input.pressed_buttons & game_button_mask_v1(GameButtonV1::jump)) != 0U;

  auto next_facing_yaw = facing_yaw_radians_;
  if (movement_length > kMovementEpsilon) {
    next_facing_yaw = std::atan2(motion.move_y, motion.move_x);
    if (next_facing_yaw == 0.0) {
      next_facing_yaw = 0.0;
    }
  }

  auto next_character = character_;
  try {
    result.character = next_character.fixed_update(
        collision_world, motion,
        1.0 / static_cast<double>(profile_.fixed_ticks_per_second));
  } catch (const CharacterControllerError &error) {
    throw PlayerSimulationError(error.what());
  } catch (const CollisionWorldError &error) {
    throw PlayerSimulationError(error.what());
  }

  const auto fell_below_death_height =
      next_character.state().feet_position.z < profile_.death_height_world;
  if (fell_below_death_height &&
      reset_count_ == std::numeric_limits<std::uint64_t>::max()) {
    throw PlayerSimulationError("The player reset counter is exhausted");
  }

  character_ = std::move(next_character);
  facing_yaw_radians_ = next_facing_yaw;
  last_reset_reason_ = PlayerResetReasonV1::none;
  if (fell_below_death_height) {
    reset_to_checkpoint(PlayerResetReasonV1::fell_below_death_height);
    result.reset_reason = last_reset_reason_;
  }
  ++next_tick_index_;
  return result;
}

void PlayerSimulationV1::set_checkpoint(PlayerCheckpointV1 checkpoint,
                                        const bool reset_immediately) {
  validate_player_checkpoint_v1(checkpoint);
  validate_checkpoint_above_death_height(checkpoint, profile_);
  if (reset_immediately &&
      reset_count_ == std::numeric_limits<std::uint64_t>::max()) {
    throw PlayerSimulationError("The player reset counter is exhausted");
  }
  checkpoint_ = std::move(checkpoint);
  if (reset_immediately) {
    reset_to_checkpoint(PlayerResetReasonV1::checkpoint_activated);
  }
}

void PlayerSimulationV1::reset_to_checkpoint(const PlayerResetReasonV1 reason) {
  if (!valid_reset_reason(reason) || reason == PlayerResetReasonV1::none) {
    throw PlayerSimulationError("A player reset requires a known reason");
  }
  if (reset_count_ == std::numeric_limits<std::uint64_t>::max()) {
    throw PlayerSimulationError("The player reset counter is exhausted");
  }
  character_.set_state(checkpoint_character_state(checkpoint_));
  facing_yaw_radians_ = checkpoint_.facing_yaw_radians;
  ++reset_count_;
  last_reset_reason_ = reason;
}

PlayerSimulationSnapshotV1 PlayerSimulationV1::snapshot() const noexcept {
  return {
      character_.state(), checkpoint_,  facing_yaw_radians_,
      next_tick_index_,   reset_count_, last_reset_reason_,
  };
}

const PlayerSimulationProfileV1 &PlayerSimulationV1::profile() const noexcept {
  return profile_;
}

namespace {

[[nodiscard]] float source_float(const std::uint32_t bits) noexcept {
  return std::bit_cast<float>(bits);
}

[[nodiscard]] bool finite_jump_state(const RacJumpVerticalStateV1 &state) {
  return std::isfinite(state.vertical_velocity) &&
         std::isfinite(state.pending_impulse) &&
         std::isfinite(state.applied_impulse) &&
         std::isfinite(state.target_height) && std::isfinite(state.gravity) &&
         std::isfinite(state.apex_gravity);
}

} // namespace

void validate_rac_jump_vertical_profile_v1(
    const RacJumpVerticalProfileV1 &profile) {
  if (profile.takeoff_frames == 0U) {
    throw PlayerSimulationError("RAC jump takeoff window must be non-zero");
  }
  // The source divides by CVT.S.W of the halfword +0x498.
  if (profile.height_ramp_frames == 0U ||
      profile.height_ramp_frames >
          static_cast<std::uint32_t>(std::numeric_limits<std::int16_t>::max())) {
    throw PlayerSimulationError(
        "RAC jump height ramp must be an explicit positive halfword");
  }
}

RacJumpVerticalStateV1
enter_rac_jump_vertical_v1(const float measured_vertical_displacement) {
  if (!std::isfinite(measured_vertical_displacement)) {
    throw PlayerSimulationError("RAC jump measured displacement must be finite");
  }
  RacJumpVerticalStateV1 state;
  // 0x224d68..0x224d78: +0xe0 = +0x110 (quadword copy).
  state.vertical_velocity = measured_vertical_displacement;
  // 0x224e18: +0x4a0 = [0x15ee70] * 29.7.
  state.gravity = source_float(kRacPalFrameStepSquaredBitsV1) *
                  source_float(kRacJumpGravityScaleBitsV1);
  // 0x224edc/0x224ef0 and 0x224f44: +0x430 = +0x488 = 1.47.
  state.target_height = source_float(kRacJumpInitialHeightBitsV1);
  // 0x224f48..0x224f74: +0x3f8 = +0x4a0 * 1.17.
  state.apex_gravity =
      state.gravity * source_float(kRacJumpApexGravityFactorBitsV1);
  // 0x224e4c..0x224e58: +0x41e, +0x428 and +0x42c are cleared;
  // set_state zeroes +0x198.
  return state;
}

RacJumpVerticalStateV1
step_rac_jump_vertical_v1(const RacJumpVerticalStateV1 &state,
                          const RacJumpVerticalProfileV1 &profile,
                          const RacJumpVerticalInputV1 &input) {
  validate_rac_jump_vertical_profile_v1(profile);
  if (!finite_jump_state(state) ||
      !std::isfinite(input.measured_vertical_displacement)) {
    throw PlayerSimulationError("RAC jump vertical state must be finite");
  }
  if (state.frames_in_state == std::numeric_limits<std::uint32_t>::max()) {
    throw PlayerSimulationError("RAC jump frame counter overflow");
  }

  auto next = state;
  const auto frames = state.frames_in_state;
  const auto dt = source_float(kRacPalFrameStepBitsV1);
  const auto dt2 = source_float(kRacPalFrameStepSquaredBitsV1);
  const auto initial_height = source_float(kRacJumpInitialHeightBitsV1);
  const auto maximum_height = source_float(kRacJumpMaximumHeightBitsV1);

  // 0x21f830..0x21f8ac runs in the state update before 0x2147c0: once past
  // the takeoff window, a vertical velocity below 0.001 marks the apex and
  // switches gravity to the stored +0x3f8 when it is non-zero.
  if (profile.takeoff_frames < frames &&
      next.vertical_velocity <
          source_float(kRacJumpApexVelocityThresholdBitsV1)) {
    next.apex_reached = true;
    if (next.apex_gravity != 0.0F) {
      next.gravity = next.apex_gravity;
    }
  }

  // 0x2149d8..0x214ad4: while the jump button is held, or before any impulse
  // exists, ramp the target height for frames <= frames(15) and recompute
  // the pending impulse as sqrt((h + h) * g) minus the impulse already used.
  const bool no_impulse_yet =
      next.pending_impulse == 0.0F && next.applied_impulse == 0.0F;
  if ((input.jump_held || no_impulse_yet) &&
      next.target_height < maximum_height &&
      !(profile.height_ramp_frames < frames)) {
    const auto ramp_step = (maximum_height - initial_height) /
                           static_cast<float>(profile.height_ramp_frames);
    const auto height = next.target_height + ramp_step;
    next.target_height = maximum_height < height ? maximum_height : height;
    // 0x1ff190 is a VU0 VSQRT; std::sqrt is the host stand-in.
    const auto speed = std::sqrt(
        (next.target_height + next.target_height) * next.gravity);
    next.pending_impulse =
        next.pending_impulse +
        ((speed - next.applied_impulse) - next.pending_impulse);
  }

  // 0x214ad8..0x214b24: after the takeoff window a positive pending impulse
  // is added to the vertical velocity and moved into the applied total.
  if (!(frames < profile.takeoff_frames) && 0.0F < next.pending_impulse) {
    next.vertical_velocity = next.vertical_velocity + next.pending_impulse;
    next.applied_impulse = next.applied_impulse + next.pending_impulse;
    next.pending_impulse = 0.0F;
  }

  if (frames < profile.takeoff_frames) {
    // 0x214c7c..0x214cac: hold the player down during takeoff.
    next.vertical_velocity =
        0.0F - dt2 * source_float(kRacJumpTakeoffPressScaleBitsV1);
  } else {
    // 0x214cb8..0x214d3c: gravity, then never fall more than 0.1 below the
    // measured displacement, then the terminal speed -(dt * 50).
    auto velocity = next.vertical_velocity - next.gravity;
    const auto measured_floor =
        input.measured_vertical_displacement -
        source_float(kRacJumpMeasuredDropLimitBitsV1);
    if (velocity < measured_floor) {
      velocity = measured_floor;
    }
    const auto terminal = -(dt * source_float(kRacJumpTerminalSpeedScaleBitsV1));
    if (velocity < terminal) {
      velocity = terminal;
    }
    next.vertical_velocity = velocity;
  }

  next.frames_in_state = frames + 1U;
  if (!finite_jump_state(next)) {
    throw PlayerSimulationError("RAC jump vertical step became non-finite");
  }
  return next;
}

float step_rac_fall_vertical_v1(const float vertical_velocity) {
  if (!std::isfinite(vertical_velocity)) {
    throw PlayerSimulationError("RAC fall vertical velocity must be finite");
  }
  // +0x940 is written by the state-6 entry as [0x15ee70] * 24.0.
  const auto gravity = source_float(kRacPalFrameStepSquaredBitsV1) *
                       source_float(kRacFallGravityScaleBitsV1);
  const auto velocity = vertical_velocity - gravity;
  const auto terminal = -(source_float(kRacPalFrameStepBitsV1) *
                          source_float(kRacJumpTerminalSpeedScaleBitsV1));
  return velocity < terminal ? terminal : velocity;
}

float land_rac_fall_vertical_v1(const float vertical_velocity) {
  if (!std::isfinite(vertical_velocity)) {
    throw PlayerSimulationError("RAC fall vertical velocity must be finite");
  }
  const auto limit = source_float(kRacPalFrameStepBitsV1) *
                     source_float(kRacFallLandingSpeedScaleBitsV1);
  return vertical_velocity < limit ? limit : vertical_velocity;
}

bool rac_fall_enters_long_phase_v1(const std::uint32_t frames_in_state,
                                   const float height_above_ground) {
  if (!std::isfinite(height_above_ground)) {
    throw PlayerSimulationError("RAC fall height must be finite");
  }
  if (frames_in_state < kRacFallLongPhaseFramesPalV1) {
    return source_float(kRacFallLongPhaseHeightBitsV1) < height_above_ground;
  }
  return true;
}

RacFallLandingV1 select_rac_fall_landing_v1(const RacFallLandingInputV1 &input) {
  if (!std::isfinite(input.previous_stick_magnitude) ||
      !std::isfinite(input.planar_speed)) {
    throw PlayerSimulationError("RAC fall landing input must be finite");
  }
  RacFallLandingV1 result;
  // 0x22e688..0x22e698: no hit points leaves through state 61.
  if (!input.hit_points_positive) {
    result.next_state = 61U;
    result.entry_selects_sequence = true;
    return result;
  }
  if (!(input.frames_in_state < kRacFallHardLandingFramesPalV1)) {
    // 0x22e6bc..0x22e700: hard landing after frames(90) frames of falling.
    result.next_state = 0U;
    result.sequence_selected = true;
    result.sequence_slot = 12U;
    result.sequence_argument = 4U;
    result.sequence_blend_frames = 5.0F;  // frames(6)
    if (input.lock_extension_allowed) {
      result.input_lock_written = true;
      result.input_lock_frames = kRacFallHardLockFramesPalV1;
    }
    return result;
  }
  if (input.long_phase) {
    // 0x22e730..0x22e7d4: landing from the slot-11 phase.
    result.next_state = 0U;
    result.sequence_selected = true;
    result.sequence_slot = 12U;
    result.sequence_argument = 9U;
    result.sequence_blend_frames = -1.0F;
    result.input_lock_written = true;
    result.input_lock_frames = input.state_is_45
                                   ? kRacFallLongLockFromState45PalV1
                                   : kRacFallLongLockFramesPalV1;
    // 0x22e7ac..0x22e7d4 would extend the lock to frames(18) when +0x2fc is
    // zero and frames(50) < +0x198, but the accepted set_state(0, 0) above
    // already zeroed +0x198 at 0x227db0, so the comparison cannot pass.
    return result;
  }
  // 0x22e7ec..0x22e81c: a short fall with the stick still pushed walks on.
  if (source_float(kRacFallWalkMagnitudeBitsV1) <
          input.previous_stick_magnitude &&
      input.input_lock_frames == 0U) {
    result.next_state = 2U;
    result.entry_selects_sequence = true;
    return result;
  }
  // 0x22e838..0x22e884: otherwise skid when faster than 3 * dt.
  if (source_float(kRacPalFrameStepBitsV1) *
          source_float(kRacFallSkidSpeedScaleBitsV1) <
      input.planar_speed) {
    result.next_state = 3U;
    result.sequence_selected = true;
    result.sequence_slot = 6U;
    result.sequence_blend_frames = 10.0F;  // frames(12)
    return result;
  }
  // 0x22e8b0..0x22e8b8: idle.
  result.next_state = 0U;
  result.entry_selects_sequence = true;
  return result;
}

} // namespace openrc::game
