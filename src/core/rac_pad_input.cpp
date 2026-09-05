#include "openrc/rac_pad_input.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace openrc::game {

float decode_rac_pad_axis_v1(const std::uint8_t raw) noexcept {
  const auto delta = static_cast<std::int32_t>(raw) -
                     static_cast<std::int32_t>(kRacPadAxisCenterV1);
  const auto magnitude = delta < 0 ? -delta : delta;
  if (magnitude < static_cast<std::int32_t>(kRacPadAxisDeadZoneV1)) {
    return 0.0F;
  }

  auto output = static_cast<float>(magnitude - static_cast<std::int32_t>(
                                                   kRacPadAxisDeadZoneV1)) /
                static_cast<float>(kRacPadAxisResponseDivisorV1);
  output = std::min(output, 1.0F);
  return raw < kRacPadAxisCenterV1 ? -output : output;
}

std::uint8_t
quantize_game_input_axis_to_rac_pad_v1(const std::int16_t axis) noexcept {
  const auto canonical = canonical_game_input_axis_v1(axis);
  const auto magnitude = canonical < 0 ? -static_cast<std::int32_t>(canonical)
                                       : static_cast<std::int32_t>(canonical);
  const auto source_span = canonical < 0 ? 127 : 128;
  const auto source_magnitude =
      (magnitude * source_span + kGameInputAxisMagnitudeV1 / 2) /
      kGameInputAxisMagnitudeV1;
  const auto raw =
      canonical < 0
          ? static_cast<std::int32_t>(kRacPadAxisCenterV1) - source_magnitude
          : static_cast<std::int32_t>(kRacPadAxisCenterV1) + source_magnitude;
  return static_cast<std::uint8_t>(raw);
}

std::int16_t apply_rac_pad_axis_response_v1(const std::int16_t axis) noexcept {
  const auto response =
      decode_rac_pad_axis_v1(quantize_game_input_axis_to_rac_pad_v1(axis));
  const auto scaled =
      std::lround(static_cast<double>(response) * kGameInputAxisMagnitudeV1);
  return canonical_game_input_axis_v1(static_cast<std::int32_t>(scaled));
}

RacPadAxesResponseV1
decode_rac_pad_axes_response_v1(const GameInputAxesV1 axes) noexcept {
  return {
      decode_rac_pad_axis_v1(
          quantize_game_input_axis_to_rac_pad_v1(axes.move_x)),
      decode_rac_pad_axis_v1(
          quantize_game_input_axis_to_rac_pad_v1(axes.move_y)),
      decode_rac_pad_axis_v1(
          quantize_game_input_axis_to_rac_pad_v1(axes.look_x)),
      decode_rac_pad_axis_v1(
          quantize_game_input_axis_to_rac_pad_v1(axes.look_y)),
  };
}

GameInputAxesV1
apply_rac_pad_axes_response_v1(const GameInputAxesV1 axes) noexcept {
  const auto response = decode_rac_pad_axes_response_v1(axes);
  const auto quantize = [](const float value) noexcept {
    const auto scaled = std::lround(static_cast<double>(value) *
                                    kGameInputAxisMagnitudeV1);
    return canonical_game_input_axis_v1(static_cast<std::int32_t>(scaled));
  };
  return {quantize(response.move_x), quantize(response.move_y),
          quantize(response.look_x), quantize(response.look_y)};
}

} // namespace openrc::game
