#include "openrc/rac_player_locomotion.hpp"

#include <cmath>

namespace openrc::game {
namespace {

struct CanonicalMovement {
  float x = 0.0F;
  float y = 0.0F;
  float magnitude = 0.0F;
};

[[nodiscard]] CanonicalMovement canonical_movement(
    const float source_move_x, const float source_move_y) noexcept {
  if (!std::isfinite(source_move_x) || !std::isfinite(source_move_y)) {
    return {};
  }
  auto x = source_move_x;
  auto y = source_move_y;
  auto magnitude = std::sqrt(x * x + y * y);
  if (magnitude > 1.0F) {
    x /= magnitude;
    y /= magnitude;
    magnitude = 1.0F;
  }
  return {x, y, magnitude};
}

} // namespace

RacPlayerGroundMovementV1 map_rac_player_standard_ground_movement_v1(
    const float source_move_x, const float source_move_y) noexcept {
  const auto canonical = canonical_movement(source_move_x, source_move_y);
  const auto magnitude = canonical.magnitude;
  if (!(magnitude > 0.0F)) {
    return {};
  }

  const auto pace = magnitude < kRacPlayerStandardFastPaceThresholdV1
                        ? RacPlayerGroundPaceV1::slow
                        : RacPlayerGroundPaceV1::fast;
  const auto target_speed =
      pace == RacPlayerGroundPaceV1::slow
          ? kRacPlayerStandardSlowGroundSpeedV1
          : kRacPlayerStandardFastGroundSpeedV1;
  const auto inverse_magnitude = 1.0F / magnitude;
  return {
      static_cast<double>(canonical.x * inverse_magnitude),
      static_cast<double>(canonical.y * inverse_magnitude),
      magnitude,
      target_speed,
      pace,
  };
}

} // namespace openrc::game
