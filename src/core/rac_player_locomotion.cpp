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

// Each binary32 operation below is a separate statement, and contraction is
// disabled for the rest of this file, so no fused multiply-add can form.
#pragma STDC FP_CONTRACT OFF

float rac_player_planar_length_v1(const float x, const float y) noexcept {
  const auto x_squared = x * x;
  const auto y_squared = y * y;
  const auto sum = x_squared + y_squared;
  return std::sqrt(sum);
}

RacPlayerStickSampleV1
gate_rac_player_stick_sample_v1(const float analog_x, const float analog_y,
                                const RacPadDirectionBitsV1 digital) noexcept {
  const auto length = rac_player_planar_length_v1(analog_x, analog_y);
  if (!(length < kRacPlayerStickDigitalFallbackMagnitudeV1)) {
    return {analog_x, analog_y};
  }
  const auto x =
      static_cast<int>(digital.bit13) - static_cast<int>(digital.bit15);
  const auto y =
      static_cast<int>(digital.bit14) - static_cast<int>(digital.bit12);
  return {static_cast<float>(x), static_cast<float>(y)};
}

float rac_player_previous_stick_magnitude_v1(
    const RacPlayerStickSampleV1 previous_sample) noexcept {
  const auto length =
      rac_player_planar_length_v1(previous_sample.x, previous_sample.y);
  return 1.0F < length ? 1.0F : length;
}

bool rac_player_idle_requests_ground_move_v1(
    const float previous_magnitude) noexcept {
  return kRacPlayerGroundMoveEnterMagnitudeV1 < previous_magnitude;
}

bool rac_player_ground_move_opens_stop_path_v1(
    const float previous_magnitude) noexcept {
  return previous_magnitude < kRacPlayerGroundStopMagnitudeV1;
}

float rac_player_standard_target_pace_v1(
    const RacPlayerStickSampleV1 current_sample,
    const RacPlayerFrameTimingV1 &timing) noexcept {
  if (current_sample.x == 0.0F && current_sample.y == 0.0F) {
    return 0.0F;
  }
  // 0x211f80 normalizes a sample longer than one before measuring it; the
  // normalized length still selects the fast pace, so the raw length is
  // sufficient for the strict 0.82 comparison.
  const auto magnitude =
      rac_player_planar_length_v1(current_sample.x, current_sample.y);
  if (!(0.0F < magnitude)) {
    return 0.0F;
  }
  const auto coefficient = magnitude < kRacPlayerStandardFastPaceThresholdV1
                               ? kRacPlayerStandardSlowGroundSpeedV1
                               : kRacPlayerStandardFastGroundSpeedV1;
  return coefficient * timing.frame_step;
}

float approach_rac_value_v1(float &value, const float target,
                            const float step) noexcept {
  const auto delta = target - value;
  auto applied = delta;
  if (step < delta) {
    applied = step;
  } else {
    const auto negative_step = -step;
    if (delta < negative_step) {
      applied = negative_step;
    }
  }
  value = value + applied;
  const auto remaining = target - value;
  return std::abs(remaining);
}

namespace {

[[nodiscard]] float wrap_once(float angle) noexcept {
  if (!(angle < kRacPiV1)) {
    angle = angle - kRacPiV1;
    angle = angle - kRacPiV1;
  } else if (angle < -kRacPiV1) {
    angle = angle + kRacPiV1;
    angle = angle + kRacPiV1;
  }
  return angle;
}

// 0x25c7f0.
void step_turn_rate(float &angular_velocity, const float delta,
                    const RacPlayerTurnParametersV1 &parameters) noexcept {
  const auto pull = parameters.acceleration * delta;
  const auto drag = parameters.damping * angular_velocity;
  const auto change = pull - drag;
  angular_velocity = angular_velocity + change;
  if (0.0F < parameters.maximum_rate) {
    if (parameters.maximum_rate < angular_velocity) {
      angular_velocity = parameters.maximum_rate;
    } else if (angular_velocity < -parameters.maximum_rate) {
      angular_velocity = -parameters.maximum_rate;
    }
  }
  const auto remaining = std::abs(delta);
  if (remaining < angular_velocity) {
    angular_velocity = remaining;
  } else if (angular_velocity < -remaining) {
    angular_velocity = -remaining;
  }
}

} // namespace

float rac_wrap_angle_sum_v1(const float a, const float b) noexcept {
  return wrap_once(a + b);
}

float rac_wrap_angle_difference_v1(const float a, const float b) noexcept {
  return wrap_once(a - b);
}

float step_rac_player_turn_v1(
    RacPlayerTurnStateV1 &state, const float target_yaw,
    const RacPlayerTurnParametersV1 &parameters) noexcept {
  const auto delta = rac_wrap_angle_difference_v1(target_yaw, state.facing);
  step_turn_rate(state.angular_velocity, delta, parameters);
  state.facing = rac_wrap_angle_sum_v1(state.facing, state.angular_velocity);
  const auto remaining =
      rac_wrap_angle_difference_v1(target_yaw, state.facing);
  const auto snap = parameters.maximum_rate * kRacPlayerTurnSnapFractionV1;
  if (std::abs(remaining) < snap) {
    state.facing = target_yaw;
    state.angular_velocity = 0.0F;
    return state.angular_velocity;
  }
  return remaining;
}

RacPlayerTurnParametersV1 rac_player_ground_turn_parameters_v1(
    const std::uint8_t substate, const float previous_magnitude,
    const RacPlayerFrameTimingV1 &timing) noexcept {
  const auto scale = timing.time_scale_squared;
  if (substate == 1U) {
    return {
        scale * kRacPlayerFullStrideTurnAccelerationV1,
        scale * kRacPlayerFullStrideTurnDampingV1,
        timing.frame_step * kRacPlayerFastTurnRateV1,
    };
  }
  if (kRacPlayerStandardFastPaceThresholdV1 < previous_magnitude) {
    auto blend = previous_magnitude + 1.0F;
    blend = blend * 0.5F;
    auto acceleration = scale * kRacPlayerFastStickTurnAccelerationV1;
    acceleration = acceleration * blend;
    auto rate = timing.frame_step * kRacPlayerFastTurnRateV1;
    rate = rate * blend;
    return {acceleration, scale * kRacPlayerStickTurnDampingV1, rate};
  }
  const auto blend = previous_magnitude + kRacPlayerSlowStickTurnOffsetV1;
  auto acceleration = scale * kRacPlayerSlowStickTurnAccelerationV1;
  acceleration = acceleration * blend;
  auto rate = timing.frame_step * kRacPlayerSlowTurnRateV1;
  rate = rate * blend;
  return {acceleration, scale * kRacPlayerStickTurnDampingV1, rate};
}

float rac_player_ground_acceleration_scale_v1(
    const std::uint8_t substate, const float previous_magnitude,
    const float previous_remaining_turn) noexcept {
  if (substate != 1U &&
      previous_magnitude < kRacPlayerAccelerationTurnMagnitudeV1 &&
      kRacPlayerAccelerationTurnAngleV1 < previous_remaining_turn) {
    return 0.0F;
  }
  return 1.0F;
}

float step_rac_player_ground_pace_v1(
    float actual_pace, const float target_pace,
    const float acceleration_scale,
    const RacPlayerFrameTimingV1 &timing) noexcept {
  auto acceleration =
      timing.frame_step_squared * kRacPlayerGroundAccelerationV1;
  acceleration = acceleration * acceleration_scale;
  const auto deceleration =
      timing.frame_step_squared * kRacPlayerGroundDecelerationV1;
  static_cast<void>(approach_rac_value_v1(
      actual_pace, target_pace,
      actual_pace < target_pace ? acceleration : deceleration));
  return actual_pace;
}

float step_rac_player_idle_pace_v1(
    float actual_pace, const RacPlayerFrameTimingV1 &timing) noexcept {
  const auto deceleration =
      timing.frame_step_squared * kRacPlayerIdleDecelerationV1;
  static_cast<void>(approach_rac_value_v1(
      actual_pace, 0.0F, actual_pace < 0.0F ? 0.0F : deceleration));
  return actual_pace;
}

RacPlayerGroundFrameResultV1
step_rac_player_ground_frame_v1(const RacPlayerGroundFrameStateV1 &state,
                                const RacPlayerGroundFrameInputV1 &input,
                                const RacPlayerFrameTimingV1 &timing) noexcept {
  RacPlayerGroundFrameResultV1 result;
  result.state = state;
  result.previous_magnitude =
      rac_player_previous_stick_magnitude_v1(state.stored_sample);
  result.state.stored_sample = input.sample;

  if (state.state != RacPlayerGroundStateV1::ground_move) {
    result.state.actual_pace =
        step_rac_player_idle_pace_v1(state.actual_pace, timing);
    result.idle_requests_ground_move =
        rac_player_idle_requests_ground_move_v1(result.previous_magnitude);
    return result;
  }

  result.target_pace = rac_player_standard_target_pace_v1(input.sample, timing);
  // A neutral sample retargets +0x180 to the current facing.
  const auto neutral = input.sample.x == 0.0F && input.sample.y == 0.0F;
  const auto target_yaw = neutral ? state.turn.facing : input.target_yaw;
  const auto acceleration_scale = rac_player_ground_acceleration_scale_v1(
      state.substate, result.previous_magnitude, state.remaining_turn);
  const auto parameters = rac_player_ground_turn_parameters_v1(
      state.substate, result.previous_magnitude, timing);
  result.state.remaining_turn =
      step_rac_player_turn_v1(result.state.turn, target_yaw, parameters);
  result.state.actual_pace = step_rac_player_ground_pace_v1(
      state.actual_pace, result.target_pace, acceleration_scale, timing);
  result.ground_move_opens_stop_path =
      rac_player_ground_move_opens_stop_path_v1(result.previous_magnitude);
  return result;
}

RacPlayerGroundFrameStateV1
enter_rac_player_ground_move_v1(RacPlayerGroundFrameStateV1 state,
                                const float planar_velocity_length,
                                const RacPlayerFrameTimingV1 &timing) noexcept {
  const auto limit = timing.frame_step * kRacPlayerGroundEntryPaceLimitV1;
  state.state = RacPlayerGroundStateV1::ground_move;
  state.substate = 0U;
  state.actual_pace =
      limit < planar_velocity_length ? limit : planar_velocity_length;
  return state;
}

} // namespace openrc::game
