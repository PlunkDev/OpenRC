#include "openrc/third_person_camera.hpp"

#include "openrc/game_input.hpp"

#include <algorithm>
#include <array>
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
  if (move_x == 0 && move_y == 0) {
    return {};
  }

  auto local_x = axis_to_unit(move_x);
  auto local_y = axis_to_unit(move_y);
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

} // namespace openrc::game
