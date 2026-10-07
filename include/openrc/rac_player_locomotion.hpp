#pragma once

#include "openrc/game_input.hpp"

#include <bit>
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

// Source ground-frame primitives recovered from the loaded Veldin overlay of
// SCES-50916 PAL v2.00. docs/RAC_PLAYER_LOCOMOTION_V1.md lists every address.
// Arithmetic is host binary32 in source operation order; the EE FPU and VU0
// rounding modes are not emulated.

// 0x2623d0(a0 != 0) writes the 50 Hz frame-time block.
inline constexpr float kRacPalFrameStepV1 = std::bit_cast<float>(0x3ca3d70bU);
inline constexpr float kRacPalFrameStepSquaredV1 =
    std::bit_cast<float>(0x39d1b718U);
inline constexpr float kRacPalTimeScaleSquaredV1 =
    std::bit_cast<float>(0x3fb851ecU);

// 0x211d78: an analog sample shorter than this is replaced by the digital
// direction bits.
inline constexpr float kRacPlayerStickDigitalFallbackMagnitudeV1 =
    std::bit_cast<float>(0x3e800000U);
// 0x21ca80/0x21ca88: standard ground pace steps, multiplied by dt squared.
inline constexpr float kRacPlayerGroundAccelerationV1 =
    std::bit_cast<float>(0x40f00000U);
inline constexpr float kRacPlayerGroundDecelerationV1 =
    std::bit_cast<float>(0x41080000U);
// 0x218714: idle pace decay for states 0 and 1, multiplied by dt squared.
inline constexpr float kRacPlayerIdleDecelerationV1 =
    std::bit_cast<float>(0x4149999aU);
// 0x223aa0: state-2 entry pace limit, multiplied by dt.
inline constexpr float kRacPlayerGroundEntryPaceLimitV1 =
    std::bit_cast<float>(0x40e00000U);
// 0x21c998..0x21c9d8: acceleration is suppressed below this previous stick
// magnitude while the signed remaining turn exceeds the angle below.
inline constexpr float kRacPlayerAccelerationTurnMagnitudeV1 =
    std::bit_cast<float>(0x3f4ccccdU);
inline constexpr float kRacPlayerAccelerationTurnAngleV1 =
    std::bit_cast<float>(0x3f060a92U);

// Turn coefficients: acceleration and damping are multiplied by the time
// scale squared, the maximum rate by dt.
inline constexpr float kRacPlayerFullStrideTurnAccelerationV1 =
    std::bit_cast<float>(0x3c03126fU);
inline constexpr float kRacPlayerFullStrideTurnDampingV1 =
    std::bit_cast<float>(0x3e19999aU);
inline constexpr float kRacPlayerFastStickTurnAccelerationV1 =
    std::bit_cast<float>(0x3c9ba5e3U);
inline constexpr float kRacPlayerSlowStickTurnAccelerationV1 =
    std::bit_cast<float>(0x3ba3d70aU);
inline constexpr float kRacPlayerStickTurnDampingV1 =
    std::bit_cast<float>(0x3dcccccdU);
inline constexpr float kRacPlayerFastTurnRateV1 =
    std::bit_cast<float>(0x411f2c8dU);
inline constexpr float kRacPlayerSlowTurnRateV1 =
    std::bit_cast<float>(0x4096cbe4U);
inline constexpr float kRacPlayerSlowStickTurnOffsetV1 =
    std::bit_cast<float>(0x3eb33333U);
// 0x25cdf8: the turn snaps to its target below this fraction of the rate.
inline constexpr float kRacPlayerTurnSnapFractionV1 =
    std::bit_cast<float>(0x3c23d70aU);
inline constexpr float kRacPiV1 = std::bit_cast<float>(0x40490fdbU);

struct RacPlayerFrameTimingV1 {
  float frame_step = kRacPalFrameStepV1;                 // [0x15ee6c]
  float frame_step_squared = kRacPalFrameStepSquaredV1;  // [0x15ee70]
  float time_scale_squared = kRacPalTimeScaleSquaredV1;  // [0x15ee64]

  [[nodiscard]] bool
  operator==(const RacPlayerFrameTimingV1 &) const = default;
};

// The source movement vector at player +0x1d20/+0x1d24.
struct RacPlayerStickSampleV1 {
  float x = 0.0F;
  float y = 0.0F;

  [[nodiscard]] bool
  operator==(const RacPlayerStickSampleV1 &) const = default;
};

// Bits 12..15 of the pad word read at 0x211d94. The source forms
// x = bit13 - bit15 and y = bit14 - bit12; their button names are not
// claimed here.
struct RacPadDirectionBitsV1 {
  bool bit12 = false;
  bool bit13 = false;
  bool bit14 = false;
  bool bit15 = false;
};

// 0x1ff3b8: sqrt(x * x + y * y).
[[nodiscard]] float rac_player_planar_length_v1(float x, float y) noexcept;

// 0x211d70..0x211ddc: keeps the analog sample unless its length is strictly
// below 0.25, in which case the digital direction replaces both components.
[[nodiscard]] RacPlayerStickSampleV1
gate_rac_player_stick_sample_v1(float analog_x, float analog_y,
                                RacPadDirectionBitsV1 digital) noexcept;

// 0x211ee8: min(length, 1) of the sample still stored from the previous
// frame. The result at +0x229c drives the state transitions below.
[[nodiscard]] float rac_player_previous_stick_magnitude_v1(
    RacPlayerStickSampleV1 previous_sample) noexcept;

// 0x22aef0: state 0 requests state 2 only when 0.22 < previous magnitude.
[[nodiscard]] bool
rac_player_idle_requests_ground_move_v1(float previous_magnitude) noexcept;

// 0x22c114: state 2 opens its stop path only when previous magnitude < 0.17.
[[nodiscard]] bool
rac_player_ground_move_opens_stop_path_v1(float previous_magnitude) noexcept;

// 0x211f80 and the standard branch of 0x2122a0: the per-frame target pace
// for the current sample, zero when both components are zero.
[[nodiscard]] float rac_player_standard_target_pace_v1(
    RacPlayerStickSampleV1 current_sample,
    const RacPlayerFrameTimingV1 &timing) noexcept;

// 0x25c8c0: moves value toward target by at most step and returns the
// remaining absolute difference.
[[nodiscard]] float approach_rac_value_v1(float &value, float target,
                                          float step) noexcept;

// 0x200098 and 0x2000e0: a + b and a - b wrapped once into [-pi, pi).
[[nodiscard]] float rac_wrap_angle_sum_v1(float a, float b) noexcept;
[[nodiscard]] float rac_wrap_angle_difference_v1(float a, float b) noexcept;

struct RacPlayerTurnParametersV1 {
  float acceleration = 0.0F;
  float damping = 0.0F;
  float maximum_rate = 0.0F;

  [[nodiscard]] bool
  operator==(const RacPlayerTurnParametersV1 &) const = default;
};

struct RacPlayerTurnStateV1 {
  float facing = 0.0F;            // player +0x98
  float angular_velocity = 0.0F;  // player +0x184

  [[nodiscard]] bool
  operator==(const RacPlayerTurnStateV1 &) const = default;
};

// 0x25ccf0 with direction 0 and its rate helper 0x25c7f0. Returns the
// signed remaining turn stored at +0x188.
[[nodiscard]] float
step_rac_player_turn_v1(RacPlayerTurnStateV1 &state, float target_yaw,
                        const RacPlayerTurnParametersV1 &parameters) noexcept;

// State-2 turn selection at 0x21c8f0..0x21ca60 and 0x2125f0 without the
// skid, +0x20b3, and +0x20a8 exceptions.
[[nodiscard]] RacPlayerTurnParametersV1 rac_player_ground_turn_parameters_v1(
    std::uint8_t substate, float previous_magnitude,
    const RacPlayerFrameTimingV1 &timing) noexcept;

// 0x21c998..0x21c9d8: 0 or 1. The comparison uses the signed remaining turn
// left by the previous frame.
[[nodiscard]] float rac_player_ground_acceleration_scale_v1(
    std::uint8_t substate, float previous_magnitude,
    float previous_remaining_turn) noexcept;

// 0x212740 with the state-2 steps from 0x21ca74..0x21cb08.
[[nodiscard]] float step_rac_player_ground_pace_v1(
    float actual_pace, float target_pace, float acceleration_scale,
    const RacPlayerFrameTimingV1 &timing) noexcept;

// 0x218594..0x218730 for state 0: target zero, decay step 12.6 * dt squared.
[[nodiscard]] float
step_rac_player_idle_pace_v1(float actual_pace,
                             const RacPlayerFrameTimingV1 &timing) noexcept;

enum class RacPlayerGroundStateV1 : std::uint8_t {
  idle = 0U,
  ground_move = 2U,
};

struct RacPlayerGroundFrameStateV1 {
  RacPlayerGroundStateV1 state = RacPlayerGroundStateV1::idle;
  std::uint8_t substate = 0U;            // +0x2088, animation slot 3 + substate
  RacPlayerStickSampleV1 stored_sample;  // +0x1d20/+0x1d24
  float actual_pace = 0.0F;              // +0x194, distance per frame
  RacPlayerTurnStateV1 turn;
  float remaining_turn = 0.0F;           // +0x188

  [[nodiscard]] bool
  operator==(const RacPlayerGroundFrameStateV1 &) const = default;
};

struct RacPlayerGroundFrameInputV1 {
  RacPlayerStickSampleV1 sample;  // already gated
  float target_yaw = 0.0F;        // +0x180 for a non-zero sample
};

struct RacPlayerGroundFrameResultV1 {
  RacPlayerGroundFrameStateV1 state;
  float previous_magnitude = 0.0F;  // +0x229c
  float target_pace = 0.0F;         // +0x190
  bool idle_requests_ground_move = false;
  bool ground_move_opens_stop_path = false;

  [[nodiscard]] bool
  operator==(const RacPlayerGroundFrameResultV1 &) const = default;
};

// One PAL frame of the standard idle/ground-move update. Neither the state nor
// the substate is switched: transition guards outside these predicates and
// the counter-gated substate helper 0x2293e8 stay with the caller, which
// applies enter_rac_player_ground_move_v1 or returns to idle. The edge probe
// 0x2178a0 is not modeled either: it can zero the pace after this step in
// state 2 while the +0x1f4 countdown runs, and before the idle decay in
// state 0.
[[nodiscard]] RacPlayerGroundFrameResultV1
step_rac_player_ground_frame_v1(const RacPlayerGroundFrameStateV1 &state,
                                const RacPlayerGroundFrameInputV1 &input,
                                const RacPlayerFrameTimingV1 &timing) noexcept;

// 0x223a60 standard branch: substate 0 and the measured planar velocity
// length as pace, limited to 7 * dt.
[[nodiscard]] RacPlayerGroundFrameStateV1
enter_rac_player_ground_move_v1(RacPlayerGroundFrameStateV1 state,
                                float planar_velocity_length,
                                const RacPlayerFrameTimingV1 &timing) noexcept;

} // namespace openrc::game
