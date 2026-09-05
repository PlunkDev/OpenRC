#pragma once

#include "openrc/game_input.hpp"

#include <cstdint>

namespace openrc::game {

inline constexpr std::uint8_t kRacPadAxisCenterV1 = 127U;
inline constexpr std::uint8_t kRacPadAxisDeadZoneV1 = 48U;
inline constexpr std::uint8_t kRacPadAxisResponseDivisorV1 = 76U;

struct RacPadAxesResponseV1 {
  float move_x = 0.0F;
  float move_y = 0.0F;
  float look_x = 0.0F;
  float look_y = 0.0F;

  [[nodiscard]] bool operator==(const RacPadAxesResponseV1 &) const = default;
};

// Exact scalar response recovered from SCES-50916 PAL v2.00 at
// 0x2182b0..0x218330. The caller owns protocol ordering: packet bytes +2..+5
// are written to four consecutive floats at state +0x100..+0x10c.
[[nodiscard]] float decode_rac_pad_axis_v1(std::uint8_t raw) noexcept;

// Modern controller backends use a symmetric signed domain. This explicit
// bridge first samples that domain onto the source DualShock byte lattice;
// it does not itself apply the source response curve.
[[nodiscard]] std::uint8_t
quantize_game_input_axis_to_rac_pad_v1(std::int16_t axis) noexcept;

// Runs the source byte response and returns it in the deterministic signed
// input domain used by the current runtime. The only extra quantization is the
// final representation conversion from [-1, 1] to [-32767, 32767].
[[nodiscard]] std::int16_t
apply_rac_pad_axis_response_v1(std::int16_t axis) noexcept;

// Samples all four deterministic signed axes onto the source DualShock byte
// lattice and keeps the source float results. Gameplay consumers should use
// this form when another int16 round trip would discard recovered policy.
[[nodiscard]] RacPadAxesResponseV1
decode_rac_pad_axes_response_v1(GameInputAxesV1 axes) noexcept;

[[nodiscard]] GameInputAxesV1
apply_rac_pad_axes_response_v1(GameInputAxesV1 axes) noexcept;

} // namespace openrc::game
