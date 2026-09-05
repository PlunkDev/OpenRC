#pragma once

#include "openrc/game_input.hpp"

#include <cstdint>

namespace openrc::game {

// Standard grounded movement constants recovered from SCES-50916 PAL v2.00
// at 0x212308..0x21234c. There is no run button in this path: any non-zero
// post-dead-zone input selects the slow pace until the radial stick magnitude
// reaches 0.82, then selects the fast pace.
inline constexpr float kRacPlayerStandardFastPaceThresholdV1 = 0.82F;
inline constexpr float kRacPlayerStandardSlowGroundSpeedV1 = 0.9F;
inline constexpr float kRacPlayerStandardFastGroundSpeedV1 = 5.7F;
inline constexpr float kRacPlayerGroundMoveEnterMagnitudeV1 = 0.22F;
inline constexpr float kRacPlayerGroundStopMagnitudeV1 = 0.17F;
inline constexpr float kRacPlayerSlowToFullAnimationSpeedV1 = 2.35F;
inline constexpr float kRacPlayerFullToSlowAnimationSpeedV1 = 1.9F;

enum class RacPlayerGroundPaceV1 : std::uint8_t {
  stationary = 0U,
  slow,
  fast,
};

struct RacPlayerGroundMovementV1 {
  double direction_x = 0.0;
  double direction_y = 0.0;
  float source_input_magnitude = 0.0F;
  float target_ground_speed = 0.0F;
  RacPlayerGroundPaceV1 pace = RacPlayerGroundPaceV1::stationary;

  [[nodiscard]] bool
  operator==(const RacPlayerGroundMovementV1 &) const = default;
};

// source_move_x/source_move_y must come from
// decode_rac_pad_axes_response_v1. The original routine preserves their radial
// direction, clamps a diagonal magnitude above one, and then replaces every
// non-zero magnitude with one of the two source pace coefficients.
[[nodiscard]] RacPlayerGroundMovementV1
map_rac_player_standard_ground_movement_v1(float source_move_x,
                                           float source_move_y) noexcept;

} // namespace openrc::game
