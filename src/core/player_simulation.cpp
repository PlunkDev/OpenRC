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

} // namespace openrc::game
