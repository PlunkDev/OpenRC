#include "openrc/third_person_camera.hpp"

#include "openrc/game_input.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numbers>
#include <utility>

namespace openrc::game {
namespace {

constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kHalfPi = kPi * 0.5;
constexpr double kAxisMagnitude =
    static_cast<double>(kGameInputAxisMagnitudeV1);

[[nodiscard]] bool finite(const CollisionVectorV1 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

void validate_axis(const std::int16_t value, const char *const description) {
  if (value < -kGameInputAxisMagnitudeV1 || value > kGameInputAxisMagnitudeV1) {
    throw ThirdPersonCameraError(description);
  }
}

[[nodiscard]] double axis_to_unit(const std::int16_t value) noexcept {
  return static_cast<double>(value) / kAxisMagnitude;
}

[[nodiscard]] double wrapped_yaw(const double yaw) {
  if (!std::isfinite(yaw)) {
    throw ThirdPersonCameraError(
        "A third-person camera update exceeded its yaw domain");
  }
  auto result = std::remainder(yaw, 2.0 * kPi);
  if (result == 0.0) {
    result = 0.0;
  }
  return result;
}

[[nodiscard]] ThirdPersonCameraProfileV1
validated_profile(ThirdPersonCameraProfileV1 profile) {
  validate_third_person_camera_profile_v1(profile);
  return profile;
}

[[nodiscard]] ThirdPersonCameraStateV1
validated_state(const ThirdPersonCameraProfileV1 &profile,
                ThirdPersonCameraStateV1 state) {
  validate_third_person_camera_state_v1(profile, state);
  return state;
}

} // namespace

void validate_third_person_camera_profile_v1(
    const ThirdPersonCameraProfileV1 &profile) {
  const std::array finite_values{
      profile.minimum_distance,
      profile.maximum_distance,
      profile.target_height,
      profile.minimum_pitch_radians,
      profile.maximum_pitch_radians,
      profile.yaw_radians_per_second,
      profile.pitch_radians_per_second,
      profile.zoom_world_units_per_second,
      profile.vertical_field_of_view_radians,
      profile.aspect_ratio,
      profile.near_plane_distance,
      profile.far_plane_distance,
  };
  if (!std::ranges::all_of(finite_values, [](const double value) {
        return std::isfinite(value);
      })) {
    throw ThirdPersonCameraError(
        "A third-person camera profile contains a non-finite value");
  }
  if (!(profile.minimum_distance > 0.0) ||
      profile.maximum_distance < profile.minimum_distance ||
      profile.target_height < 0.0) {
    throw ThirdPersonCameraError(
        "A third-person camera profile has an invalid distance policy");
  }
  if (!(profile.minimum_pitch_radians > -kHalfPi) ||
      !(profile.maximum_pitch_radians < kHalfPi) ||
      profile.maximum_pitch_radians < profile.minimum_pitch_radians) {
    throw ThirdPersonCameraError(
        "A third-person camera profile has an invalid pitch policy");
  }
  if (profile.yaw_radians_per_second < 0.0 ||
      profile.pitch_radians_per_second < 0.0 ||
      profile.zoom_world_units_per_second < 0.0) {
    throw ThirdPersonCameraError(
        "A third-person camera profile has a negative control rate");
  }
  if (!(profile.vertical_field_of_view_radians > 0.0) ||
      !(profile.vertical_field_of_view_radians < kPi) ||
      !(profile.aspect_ratio > 0.0)) {
    throw ThirdPersonCameraError(
        "A third-person camera profile has an invalid projection policy");
  }
  if (!(profile.near_plane_distance > 0.0) ||
      !(profile.near_plane_distance < profile.minimum_distance) ||
      !(profile.far_plane_distance > profile.maximum_distance)) {
    throw ThirdPersonCameraError(
        "A third-person camera profile has an invalid clipping policy");
  }
}

void validate_third_person_camera_state_v1(
    const ThirdPersonCameraProfileV1 &profile,
    const ThirdPersonCameraStateV1 &state) {
  validate_third_person_camera_profile_v1(profile);
  if (!std::isfinite(state.yaw_radians) ||
      !std::isfinite(state.pitch_radians) || !std::isfinite(state.distance)) {
    throw ThirdPersonCameraError(
        "A third-person camera state contains a non-finite value");
  }
  if (state.yaw_radians < -kPi || state.yaw_radians > kPi) {
    throw ThirdPersonCameraError(
        "A third-person camera yaw is outside its canonical domain");
  }
  if (state.pitch_radians < profile.minimum_pitch_radians ||
      state.pitch_radians > profile.maximum_pitch_radians ||
      state.distance < profile.minimum_distance ||
      state.distance > profile.maximum_distance) {
    throw ThirdPersonCameraError(
        "A third-person camera state violates its profile");
  }
}

void validate_third_person_camera_input_v1(
    const ThirdPersonCameraInputV1 &input) {
  validate_axis(input.look_x,
                "A third-person camera look-X axis is outside its domain");
  validate_axis(input.look_y,
                "A third-person camera look-Y axis is outside its domain");
  validate_axis(input.zoom,
                "A third-person camera zoom axis is outside its domain");
}

ThirdPersonCameraV1::ThirdPersonCameraV1(ThirdPersonCameraProfileV1 profile,
                                         ThirdPersonCameraStateV1 initial_state)
    : profile_(validated_profile(std::move(profile))),
      state_(validated_state(profile_, std::move(initial_state))) {}

void ThirdPersonCameraV1::fixed_update(const ThirdPersonCameraInputV1 input,
                                       const double fixed_delta_seconds) {
  validate_third_person_camera_input_v1(input);
  if (!std::isfinite(fixed_delta_seconds) || fixed_delta_seconds < 0.0) {
    throw ThirdPersonCameraError(
        "A third-person camera tick has an invalid duration");
  }
  if (fixed_delta_seconds == 0.0 || input == ThirdPersonCameraInputV1{}) {
    return;
  }

  const auto seconds_per_axis_unit = fixed_delta_seconds / kAxisMagnitude;
  auto next = state_;
  if (input.look_x != 0) {
    const auto yaw_delta = static_cast<double>(input.look_x) *
                           profile_.yaw_radians_per_second *
                           seconds_per_axis_unit;
    next.yaw_radians = wrapped_yaw(next.yaw_radians + yaw_delta);
  }
  if (input.look_y != 0) {
    const auto pitch_delta = static_cast<double>(input.look_y) *
                             profile_.pitch_radians_per_second *
                             seconds_per_axis_unit;
    if (!std::isfinite(pitch_delta)) {
      throw ThirdPersonCameraError(
          "A third-person camera update exceeded its pitch domain");
    }
    next.pitch_radians = std::clamp(next.pitch_radians + pitch_delta,
                                    profile_.minimum_pitch_radians,
                                    profile_.maximum_pitch_radians);
  }
  if (input.zoom != 0) {
    const auto distance_delta = static_cast<double>(input.zoom) *
                                profile_.zoom_world_units_per_second *
                                seconds_per_axis_unit;
    if (!std::isfinite(distance_delta)) {
      throw ThirdPersonCameraError(
          "A third-person camera update exceeded its distance domain");
    }
    next.distance =
        std::clamp(next.distance - distance_delta, profile_.minimum_distance,
                   profile_.maximum_distance);
  }

  validate_third_person_camera_state_v1(profile_, next);
  state_ = next;
}

void ThirdPersonCameraV1::set_state(const ThirdPersonCameraStateV1 &state) {
  validate_third_person_camera_state_v1(profile_, state);
  state_ = state;
}

ThirdPersonCameraViewV1
ThirdPersonCameraV1::view(const CollisionVectorV1 focus_position) const {
  if (!finite(focus_position)) {
    throw ThirdPersonCameraError(
        "A third-person camera focus contains a non-finite value");
  }

  ThirdPersonCameraViewV1 result;
  result.target = focus_position;
  result.target.z += profile_.target_height;

  const auto cosine_pitch = std::cos(state_.pitch_radians);
  const auto sine_pitch = std::sin(state_.pitch_radians);
  const auto cosine_yaw = std::cos(state_.yaw_radians);
  const auto sine_yaw = std::sin(state_.yaw_radians);
  result.eye = {
      result.target.x - state_.distance * cosine_pitch * cosine_yaw,
      result.target.y - state_.distance * cosine_pitch * sine_yaw,
      result.target.z + state_.distance * sine_pitch,
  };
  result.up = {
      cosine_yaw * sine_pitch,
      sine_yaw * sine_pitch,
      cosine_pitch,
  };
  result.vertical_field_of_view_radians =
      profile_.vertical_field_of_view_radians;
  result.aspect_ratio = profile_.aspect_ratio;
  result.near_plane_distance = profile_.near_plane_distance;
  result.far_plane_distance = profile_.far_plane_distance;

  if (!finite(result.eye) || !finite(result.target) || !finite(result.up)) {
    throw ThirdPersonCameraError(
        "A third-person camera view exceeded its numeric domain");
  }
  return result;
}

CameraRelativeMovementV1
ThirdPersonCameraV1::map_movement(const std::int16_t move_x,
                                  const std::int16_t move_y) const {
  validate_axis(move_x, "A camera-relative move-X axis is outside its domain");
  validate_axis(move_y, "A camera-relative move-Y axis is outside its domain");
  return map_unit_movement(axis_to_unit(move_x), axis_to_unit(move_y));
}

CameraRelativeMovementV1
ThirdPersonCameraV1::map_unit_movement(const double move_x,
                                       const double move_y) const {
  if (!std::isfinite(move_x) || !std::isfinite(move_y)) {
    throw ThirdPersonCameraError(
        "Camera-relative movement contains a non-finite axis");
  }
  if (move_x < -1.0 || move_x > 1.0 || move_y < -1.0 || move_y > 1.0) {
    throw ThirdPersonCameraError(
        "A camera-relative movement axis is outside its unit domain");
  }
  if (move_x == 0.0 && move_y == 0.0) {
    return {};
  }

  auto local_x = move_x;
  auto local_y = move_y;
  const auto local_length = std::hypot(local_x, local_y);
  if (local_length > 1.0) {
    local_x /= local_length;
    local_y /= local_length;
  }

  const auto cosine_yaw = std::cos(state_.yaw_radians);
  const auto sine_yaw = std::sin(state_.yaw_radians);
  CameraRelativeMovementV1 result{
      local_y * cosine_yaw + local_x * sine_yaw,
      local_y * sine_yaw - local_x * cosine_yaw,
  };
  const auto world_length = std::hypot(result.move_x, result.move_y);
  if (world_length > 1.0) {
    result.move_x /= world_length;
    result.move_y /= world_length;
  }
  if (!std::isfinite(result.move_x) || !std::isfinite(result.move_y)) {
    throw ThirdPersonCameraError(
        "Camera-relative movement exceeded its numeric domain");
  }
  return result;
}

const ThirdPersonCameraProfileV1 &
ThirdPersonCameraV1::profile() const noexcept {
  return profile_;
}

const ThirdPersonCameraStateV1 &ThirdPersonCameraV1::state() const noexcept {
  return state_;
}

namespace {

constexpr float kRacFocusStiffness = std::bit_cast<float>(0x3c75c28fU);
constexpr float kRacFocusDamping = std::bit_cast<float>(0x3e4ccccdU);
constexpr float kRacPitchDeadZone = std::bit_cast<float>(0x3e99999aU);
constexpr float kRacPitchResponseScale = std::bit_cast<float>(0x3fb6db6eU);

[[nodiscard]] bool finite(const RacVector3fV1 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

void validate_spring(const RacCameraSpringV1 &spring) {
  if (!std::isfinite(spring.stiffness) || !std::isfinite(spring.damping) ||
      !std::isfinite(spring.maximum_step) || spring.maximum_step < 0.0F) {
    throw ThirdPersonCameraError("An original camera spring is invalid");
  }
}

[[nodiscard]] RacGameplayCameraProfileV1
validated_profile(RacGameplayCameraProfileV1 profile) {
  validate_spring(profile.focus_spring);
  return profile;
}

[[nodiscard]] RacGameplayCameraStateV1
validated_state(RacGameplayCameraStateV1 state) {
  if (!finite(state.focus) || !finite(state.focus_velocity)) {
    throw ThirdPersonCameraError(
        "An original gameplay camera state contains a non-finite value");
  }
  return state;
}

} // namespace

float rac_vertical_tangent_factor_v1(
    const RacProjectionSelectorV1 selector) noexcept {
  return std::bit_cast<float>(selector == RacProjectionSelectorV1::pal
                                  ? kRacPalVerticalTangentFactorBitsV1
                                  : kRacOtherVerticalTangentFactorBitsV1);
}

RacGameplayProjectionInputV1
rac_gameplay_projection_defaults_v1(const RacProjectionSelectorV1 selector,
                                    const float viewport_half_width,
                                    const float viewport_half_height) noexcept {
  return {
      selector,
      std::bit_cast<float>(kRacGameplayHorizontalTangentBitsV1),
      std::bit_cast<float>(kRacGameplayNearBitsV1),
      std::bit_cast<float>(kRacGameplayFarBitsV1),
      viewport_half_width,
      viewport_half_height,
  };
}

RacGameplayProjectionV1
rac_gameplay_projection_v1(const RacGameplayProjectionInputV1 &input) {
  const std::array values{input.horizontal_tangent, input.near_distance,
                          input.far_distance, input.viewport_half_width,
                          input.viewport_half_height};
  if (!std::ranges::all_of(values,
                           [](const float value) {
                             return std::isfinite(value) && value > 0.0F;
                           }) ||
      !(input.far_distance > input.near_distance) ||
      (input.selector != RacProjectionSelectorV1::pal &&
       input.selector != RacProjectionSelectorV1::other)) {
    throw ThirdPersonCameraError("An original projection input is invalid");
  }

  const float near = input.near_distance;
  const float far = input.far_distance;
  const float depth_scale = std::bit_cast<float>(kRacProjectionDepthScaleBitsV1);

  RacGameplayProjectionV1 result;
  result.horizontal_tangent = input.horizontal_tangent;
  // 1f7d6c: +b4 = +b0 * factor.
  result.vertical_tangent =
      input.horizontal_tangent * rac_vertical_tangent_factor_v1(input.selector);
  // 1f7f8c..1f8058, in the source operand order.
  const float depth_span = far - near;
  const float depth_denominator = near * depth_span;
  const float near_twice_negative = near * -2.0F;
  const float horizontal_extent = result.horizontal_tangent * near;
  const float vertical_extent = result.vertical_tangent * near;
  const float depth_numerator = near_twice_negative * far;
  float depth_w = depth_numerator / depth_denominator;
  const float depth_sum = far + near;
  float depth_z = depth_sum / depth_denominator;
  result.scale_x = input.viewport_half_width / horizontal_extent;
  result.scale_y = input.viewport_half_height / vertical_extent;
  depth_w = depth_w * depth_scale;
  depth_z = depth_z * depth_scale;
  result.depth_z = depth_z;
  result.depth_w = depth_w;

  const std::array outputs{result.vertical_tangent, result.scale_x,
                           result.scale_y, result.depth_z, result.depth_w};
  if (!std::ranges::all_of(outputs, [](const float value) {
        return std::isfinite(value);
      })) {
    throw ThirdPersonCameraError(
        "An original projection exceeded its numeric domain");
  }
  return result;
}

float rac_camera_spring_step_v1(const float current, const float target,
                                float &velocity,
                                const RacCameraSpringV1 &spring) {
  validate_spring(spring);
  if (!std::isfinite(current) || !std::isfinite(target) ||
      !std::isfinite(velocity)) {
    throw ThirdPersonCameraError(
        "An original camera spring operand is non-finite");
  }

  const float delta = target - current;
  float acceleration = spring.stiffness * delta;
  const float drag = spring.damping * velocity;
  acceleration = acceleration - drag;
  float next = velocity + acceleration;
  if (spring.maximum_step != 0.0F) {
    if (spring.maximum_step < next) {
      next = spring.maximum_step;
    } else if (next < -spring.maximum_step) {
      next = -spring.maximum_step;
    }
  }
  const float distance = std::abs(delta);
  if (distance < next) {
    next = distance;
  } else if (next < -distance) {
    next = -distance;
  }
  const float result = current + next;
  if (!std::isfinite(next) || !std::isfinite(result)) {
    throw ThirdPersonCameraError(
        "An original camera spring exceeded its numeric domain");
  }
  velocity = next;
  return result;
}

RacGameplayCameraProfileV1 rac_gameplay_camera_profile_v1() {
  return {{kRacFocusStiffness, kRacFocusDamping, 0.0F}};
}

RacGameplayCameraStateV1
rac_gameplay_camera_initial_state_v1(const RacVector3fV1 player_position) {
  return validated_state({player_position, {}});
}

float rac_gameplay_camera_pitch_response_v1(const float axis) {
  if (!std::isfinite(axis) || axis < -1.0F || axis > 1.0F) {
    throw ThirdPersonCameraError(
        "An original camera pitch axis is outside its pad domain");
  }
  if (axis < -kRacPitchDeadZone) {
    return (axis + kRacPitchDeadZone) * kRacPitchResponseScale;
  }
  if (kRacPitchDeadZone < axis) {
    return (axis - kRacPitchDeadZone) * kRacPitchResponseScale;
  }
  return 0.0F;
}

RacGameplayCameraV1::RacGameplayCameraV1(RacGameplayCameraProfileV1 profile,
                                         RacGameplayCameraStateV1 initial_state)
    : profile_(validated_profile(std::move(profile))),
      state_(validated_state(std::move(initial_state))) {}

void RacGameplayCameraV1::step_pal_frame(const RacVector3fV1 player_position) {
  if (!finite(player_position)) {
    throw ThirdPersonCameraError(
        "An original gameplay camera target is non-finite");
  }
  auto next = state_;
  // 2e6da8..2e6e64: vertical Z first, then horizontal X and Y.
  next.focus.z =
      rac_camera_spring_step_v1(next.focus.z, player_position.z,
                                next.focus_velocity.z, profile_.focus_spring);
  next.focus.x =
      rac_camera_spring_step_v1(next.focus.x, player_position.x,
                                next.focus_velocity.x, profile_.focus_spring);
  next.focus.y =
      rac_camera_spring_step_v1(next.focus.y, player_position.y,
                                next.focus_velocity.y, profile_.focus_spring);
  state_ = next;
}

const RacGameplayCameraProfileV1 &
RacGameplayCameraV1::profile() const noexcept {
  return profile_;
}

const RacGameplayCameraStateV1 &RacGameplayCameraV1::state() const noexcept {
  return state_;
}

} // namespace openrc::game
