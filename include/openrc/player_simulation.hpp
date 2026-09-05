#pragma once

#include "openrc/character_controller.hpp"
#include "openrc/game_input.hpp"

#include <cstdint>
#include <stdexcept>

namespace openrc::game {

struct PlayerCheckpointV1 {
  std::uint64_t checkpoint_id = 0U;
  CollisionVectorV1 feet_position;
  double facing_yaw_radians = 0.0;

  [[nodiscard]] bool operator==(const PlayerCheckpointV1 &) const = default;
};

struct PlayerSimulationProfileV1 {
  CharacterControllerProfileV1 character;
  std::uint32_t fixed_ticks_per_second = 0U;
  // Absolute world-space Z supplied by the active level package. It is not
  // derived from a level ID or from the checkpoint, because both values are
  // authored independently by the source game.
  double death_height_world = 0.0;

  [[nodiscard]] bool
  operator==(const PlayerSimulationProfileV1 &) const = default;
};

enum class PlayerResetReasonV1 : std::uint8_t {
  none = 0U,
  manual,
  checkpoint_activated,
  fell_below_death_height,
};

struct PlayerSimulationSnapshotV1 {
  CharacterControllerStateV1 character;
  PlayerCheckpointV1 checkpoint;
  double facing_yaw_radians = 0.0;
  std::uint64_t next_tick_index = 0U;
  std::uint64_t reset_count = 0U;
  PlayerResetReasonV1 last_reset_reason = PlayerResetReasonV1::none;

  [[nodiscard]] bool
  operator==(const PlayerSimulationSnapshotV1 &) const = default;
};

struct PlayerSimulationStepV1 {
  CharacterStepResultV1 character;
  PlayerResetReasonV1 reset_reason = PlayerResetReasonV1::none;

  [[nodiscard]] bool operator==(const PlayerSimulationStepV1 &) const = default;
};

class PlayerSimulationError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void validate_player_checkpoint_v1(const PlayerCheckpointV1 &checkpoint);
void validate_player_simulation_profile_v1(
    const PlayerSimulationProfileV1 &profile);
void validate_player_simulation_snapshot_v1(
    const PlayerSimulationSnapshotV1 &snapshot);

[[nodiscard]] std::uint64_t
hash_player_simulation_snapshot_v1(const PlayerSimulationSnapshotV1 &snapshot);

class PlayerSimulationV1 final {
public:
  PlayerSimulationV1(PlayerSimulationProfileV1 profile,
                     PlayerCheckpointV1 initial_checkpoint,
                     std::uint64_t next_tick_index = 0U);
  PlayerSimulationV1(PlayerSimulationProfileV1 profile,
                     const PlayerSimulationSnapshotV1 &snapshot);

  [[nodiscard]] PlayerSimulationStepV1
  fixed_update(const CollisionWorldV1 &collision_world,
               const GameInputCommandV1 &input);
  [[nodiscard]] PlayerSimulationStepV1
  fixed_update(const CollisionWorldV1 &collision_world,
               const GameInputCommandV1 &input, CharacterMotionV1 motion);

  void set_checkpoint(PlayerCheckpointV1 checkpoint, bool reset_immediately);
  void reset_to_checkpoint(PlayerResetReasonV1 reason);

  [[nodiscard]] PlayerSimulationSnapshotV1 snapshot() const noexcept;
  [[nodiscard]] const PlayerSimulationProfileV1 &profile() const noexcept;

private:
  PlayerSimulationProfileV1 profile_;
  CharacterControllerV1 character_;
  PlayerCheckpointV1 checkpoint_;
  double facing_yaw_radians_ = 0.0;
  std::uint64_t next_tick_index_ = 0U;
  std::uint64_t reset_count_ = 0U;
  PlayerResetReasonV1 last_reset_reason_ = PlayerResetReasonV1::none;
};

} // namespace openrc::game
