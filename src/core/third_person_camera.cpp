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
constexpr float source_float(const std::uint32_t bits) noexcept {
  return std::bit_cast<float>(bits);
}
constexpr float kRacPi = source_float(kRacCameraPiBitsV1);
constexpr float kRacHalfPi = source_float(0x3fc90fdbU);
constexpr float kRacPitchTravel = source_float(0x3f32b8c2U);
constexpr float kRacPitchSlow = source_float(0x3c23d70aU);
constexpr float kRacPitchFast = source_float(0x3ca3d70aU);
constexpr float kRacPitchRotationStep = source_float(0x3cfa35ddU);
constexpr float kRacEyeHeight = source_float(0x40000000U);
constexpr float kRacLookHeight = source_float(0x3fc00000U);

RacVector3fV1 add(const RacVector3fV1 a, const RacVector3fV1 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
RacVector3fV1 subtract(const RacVector3fV1 a, const RacVector3fV1 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
RacVector3fV1 scale(const RacVector3fV1 a, const float s) {
  return {a.x * s, a.y * s, a.z * s};
}
float dot(const RacVector3fV1 a, const RacVector3fV1 b) {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
RacVector3fV1 cross(const RacVector3fV1 a, const RacVector3fV1 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
// 1ff370(out, a1, a2): VOPMULA has fs=vf2 (a2), ft=vf1 (a1) and VOPMSUB
// swaps them, so the stored vector is a2 x a1. Call sites below keep the
// source argument order.
RacVector3fV1 source_cross(const RacVector3fV1 a1, const RacVector3fV1 a2) {
  return cross(a2, a1);
}
float length(const RacVector3fV1 a) { return std::sqrt(dot(a, a)); }
// 1ff4b0: desired / sqrt(dot); an all-zero dot yields the zero vector.
RacVector3fV1 with_length(const RacVector3fV1 a, const float desired) {
  const float magnitude = length(a);
  return magnitude == 0.0F ? RacVector3fV1{} : scale(a, desired / magnitude);
}

constexpr RacVector3fV1 kRacUp{0.0F, 0.0F, 1.0F};

// v - up*dot(v, up), the 1ff348/1ff2b0/1ff258 decomposition.
RacVector3fV1 horizontal_part(const RacVector3fV1 v) {
  return subtract(v, scale(kRacUp, dot(v, kRacUp)));
}

// 200098/2000e0 use two SUB/ADD operations, not remainder/fmod. The
// negative comparison observes the original sum, including its delay slot.
float wrap_source_angle(float value) {
  const bool below = value < -kRacPi;
  if (!(value < kRacPi)) {
    value = value - kRacPi;
    value = value - kRacPi;
  }
  if (below) {
    value = value + kRacPi;
    value = value + kRacPi;
  }
  return value;
}

// Host IEEE evaluation of 1ff7c8's asin approximation (160820): the
// ACC chain c0 + x*c1 + x^2*c2 + x^3*c3 times sqrt(1 - |x|), subtracted from
// pi/2. The VU SQRT consumes |1 - |x||, so ratios rounded above one stay
// finite as in the source; asin(0) keeps the source residual 6.76e-5.
float source_asin(const float value) {
  const float x = std::abs(value);
  const float x2 = x * x;
  const float x3 = x * x2;
  float polynomial = source_float(0x3fc90da4U);
  polynomial = polynomial + x * source_float(0xbe593484U);
  polynomial = polynomial + x2 * source_float(0x3d981627U);
  polynomial = polynomial + x3 * source_float(0xbc996e30U);
  const float result =
      kRacHalfPi - polynomial * std::sqrt(std::abs(1.0F - x));
  return value < 0.0F ? -result : result;
}

// 1ff860 uses f12=x, f13=y, polynomial at 1c27a0 and octants at 1c27c0.
float source_atan2(const float y, const float x) {
  const float ax = std::abs(x);
  const float ay = std::abs(y);
  const bool swap = ax < ay;
  const float small = swap ? ax : ay;
  const float big = swap ? ay : ax;
  if (big <= 0.0F) {
    return 0.0F;
  }
  const float t = (small - big) / (small + big);
  const float t2 = t * t;
  const float t4 = t2 * t2;
  const float t8 = t4 * t4;
  const std::array powers{t, t * t2, t * t4, (t * t4) * t2};
  constexpr std::array<std::uint32_t, 8> coefficients{
      0x3f7ffff5U, 0xbeaaa61cU, 0x3e4c40a6U, 0xbe0e6c63U,
      0x3dc577dfU, 0xbd6501c4U, 0x3cb31652U, 0xbb84d7e7U};
  float polynomial = powers[0] * source_float(coefficients[0]);
  for (std::size_t i = 1; i < 4; ++i) {
    polynomial = polynomial + powers[i] * source_float(coefficients[i]);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    polynomial = polynomial + (powers[i] * t8) * source_float(coefficients[i + 4]);
  }
  constexpr std::array<std::uint32_t, 8> multiplier{
      0x3f800000U, 0xbf800000U, 0xbf800000U, 0x3f800000U,
      0xbf800000U, 0x3f800000U, 0x3f800000U, 0xbf800000U};
  constexpr std::array<std::uint32_t, 8> offset{
      0U, 0x3fc90fdbU, 0U, 0xbfc90fdbU,
      0x40490fdbU, 0x3fc90fdbU, 0xc0490fdbU, 0xbfc90fdbU};
  const std::size_t octant = (swap ? 1U : 0U) + (std::signbit(y) ? 2U : 0U) +
                             (std::signbit(x) ? 4U : 0U);
  const float angle = source_float(0x3f490fdbU) + polynomial;
  return angle * source_float(multiplier[octant]) + source_float(offset[octant]);
}

struct SourceQuaternion {
  RacVector3fV1 v;
  float w = 0.0F;
};

// 1ffe18(out, a, b) = a (x) b: xyz = (b*a.w + a*b.w) + a x b,
// w = a.w*b.w - a.b (VOPMULA fs=a, ft=b; VOPMSUB swapped).
SourceQuaternion source_quaternion_product(const SourceQuaternion a,
                                           const SourceQuaternion b) {
  const auto xyz = add(add(scale(b.v, a.w), scale(a.v, b.w)), cross(a.v, b.v));
  return {xyz, a.w * b.w - dot(a.v, b.v)};
}

// 260c80: below 3727c5ac the vector is copied. Otherwise 25e228 builds
// q = (n*sin(angle/2), cos(angle/2)) and 260bf0 stores (q (x) v) (x) conj(q):
// a positive angle is a right-handed rotation about the axis. Host sin/cos
// replace the resident VU0 microprograms (not EE bit-exact).
RacVector3fV1 rotate_source(const RacVector3fV1 v, const RacVector3fV1 axis,
                            const float angle) {
  if (std::abs(angle) < source_float(0x3727c5acU)) {
    return v;
  }
  const auto normal = with_length(axis, 1.0F);
  const float half = angle * 0.5F;
  const SourceQuaternion rotation{scale(normal, std::sin(half)),
                                  std::cos(half)};
  const SourceQuaternion conjugate{scale(rotation.v, -1.0F), rotation.w};
  const auto applied = source_quaternion_product(
      source_quaternion_product(rotation, {v, 0.0F}), conjugate);
  return applied.v;
}

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
  validate_spring(profile.eye_distance_spring);
  validate_spring(profile.eye_angle_spring);
  validate_spring(profile.height_spring);
  validate_spring(profile.look_pitch_spring);
  return profile;
}

[[nodiscard]] RacGameplayCameraStateV1
validated_state(RacGameplayCameraStateV1 state) {
  const std::array scalars{
      state.yaw_response, state.yaw_response_velocity, state.pitch_response,
      state.pitch_response_velocity, state.applied_yaw_step, state.applied_pitch_step,
      state.eye_distance_velocity, state.eye_elevation_velocity,
      state.target_distance, state.target_distance_velocity,
      state.offset_shortfall, state.movement_distance_maximum, state.player_yaw,
      state.eye_azimuth_velocity, state.eye_height, state.eye_height_velocity,
      state.look_height, state.look_height_velocity, state.look_pitch,
      state.look_pitch_velocity, state.view_pitch_difference};
  if (!finite(state.focus) || !finite(state.focus_velocity) ||
      !finite(state.desired_offset) || !finite(state.smoothed_offset) ||
      !finite(state.unfiltered_eye) || !finite(state.previous_player_position) ||
      !finite(state.eye) ||
      !finite(state.forward) || !finite(state.up) ||
      !std::ranges::all_of(scalars, [](float v) { return std::isfinite(v); }) ||
      state.look_pitch < -kRacPi || state.look_pitch >= kRacPi ||
      std::abs(state.yaw_response) > 1.0F || std::abs(state.pitch_response) > 1.0F) {
    throw ThirdPersonCameraError(
        "An original gameplay camera state contains a non-finite value");
  }
  return state;
}

} // namespace

RacProjectionSelectorV1
rac_projection_selector_v1(const std::uint32_t source_word) noexcept {
  return source_word == 0U ? RacProjectionSelectorV1::other
                           : RacProjectionSelectorV1::pal;
}

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

RacGameplayProjectionInputV1 rac_gameplay_pal_projection_defaults_v1() noexcept {
  return rac_gameplay_projection_defaults_v1(RacProjectionSelectorV1::pal,
                                             source_float(0x43800000U),
                                             source_float(0x43600000U));
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
  return {{kRacFocusStiffness, kRacFocusDamping, 0.0F},
          {source_float(0x3ca3d70aU), kRacFocusDamping, 0.0F},
          {kRacFocusStiffness, kRacFocusDamping, 0.0F},
          {source_float(0x3b83126fU), kRacFocusDamping, 0.0F},
          {source_float(0x3ba3d70aU), kRacFocusDamping, 0.0F}};
}

RacGameplayCameraStateV1
rac_gameplay_camera_initial_state_v1(const RacVector3fV1 player_position) {
  return rac_gameplay_camera_initial_state_v1(player_position, 0.0F);
}

RacGameplayCameraStateV1 rac_gameplay_camera_initial_state_v1(
    const RacVector3fV1 player_position, const float player_yaw_radians) {
  if (!finite(player_position) || !std::isfinite(player_yaw_radians) ||
      player_yaw_radians < -kRacPi || player_yaw_radians > kRacPi) {
    throw ThirdPersonCameraError("An original camera entry transform is invalid");
  }
  RacGameplayCameraStateV1 result;
  // 2e58e0 with an unobstructed 2e5770 result: focus +160 = target +64.
  result.focus = player_position;
  result.previous_player_position = player_position; // 1ecdf0: 166f70
  result.player_yaw = player_yaw_radians;
  const float distance = source_float(kRacCameraOffsetLimitBitsV1);
  result.target_distance = distance;
  result.movement_distance_maximum = distance;
  // 2e7b68(1): local (-+348, 0, +352) through the player Moby rows +c0/+d0/
  // +e0 (1ff680: x*row0 + y*row1 + z*row2), then eye = target + that.
  // INFERRED neutral rows of an upright Moby yawed about +Z; host cos/sin
  // stand in for the source matrix (the 2e7c68/2e7cc4 probes are omitted).
  const float cosine = std::cos(player_yaw_radians);
  const float sine = std::sin(player_yaw_radians);
  const RacVector3fV1 row0{cosine, sine, 0.0F};
  const RacVector3fV1 row1{-sine, cosine, 0.0F};
  const auto eye_offset =
      add(add(scale(row0, -distance), scale(row1, 0.0F)),
          scale(kRacUp, kRacEyeHeight));
  result.eye = add(player_position, eye_offset);
  // 2e7fe0..2e8064: forward toward target + 1.5*up, left = up x forward,
  // up = forward x left.
  const auto reference =
      add(with_length(kRacUp, kRacLookHeight), player_position);
  result.forward = with_length(subtract(reference, result.eye), 1.0F);
  const auto left = with_length(source_cross(result.forward, kRacUp), 1.0F);
  result.up = source_cross(left, result.forward);
  // 2e8500..2e8594: +304 = eye - pivot +144, copied to +320; +0 = eye.
  const auto pivot = add(scale(kRacUp, kRacEyeHeight), player_position);
  result.desired_offset = subtract(result.eye, pivot);
  result.smoothed_offset = result.desired_offset;
  result.unfiltered_eye = result.eye;
  // +512 = +348 - |O - g*dot(O, g)| with g = 13f6e0 = (0,0,-1).
  const RacVector3fV1 gravity{0.0F, 0.0F, -1.0F};
  const auto level_offset = subtract(
      result.desired_offset, scale(gravity, dot(result.desired_offset, gravity)));
  result.offset_shortfall = distance - length(level_offset);
  result.eye_height = kRacEyeHeight;   // +40 = +352
  result.look_height = kRacLookHeight; // +36 = +240
  result.eye_initialized = true;
  return validated_state(result);
}

float rac_camera_angular_spring_step_v1(const float current, const float target,
                                       float &velocity,
                                       const RacCameraSpringV1 &spring) {
  if (!std::isfinite(current) || !std::isfinite(target) ||
      current < -kRacPi || current > kRacPi || target < -kRacPi || target > kRacPi) {
    throw ThirdPersonCameraError("An original angular spring is outside its domain");
  }
  const float delta = wrap_source_angle(target - current);
  float next_velocity = velocity;
  static_cast<void>(rac_camera_spring_step_v1(0.0F, delta, next_velocity, spring));
  const float result = wrap_source_angle(current + next_velocity);
  velocity = next_velocity;
  return result;
}

float rac_gameplay_camera_yaw_step_v1(const std::uint32_t selector) {
  constexpr std::array<std::uint32_t, 3> bits{0x3c8efa35U, 0x3cb9dedeU,
                                              0x3ce4c388U};
  if (selector >= bits.size()) {
    throw ThirdPersonCameraError("An original yaw selector is outside its table");
  }
  return source_float(bits[selector]);
}

float rac_gameplay_camera_clamp_pitch_v1(const float difference) {
  if (!std::isfinite(difference)) {
    throw ThirdPersonCameraError("An original camera pitch difference is non-finite");
  }
  const float limit = source_float(kRacCameraPitchLimitBitsV1);
  return std::clamp(difference, -limit, limit);
}

float rac_gameplay_camera_probe_height_v1(const std::uint8_t byte) {
  return source_float(byte == 2U ? 0x40400000U :
                      byte == 1U ? 0x3ecccccdU : 0x3f000000U);
}

RacVector3fV1 rac_gameplay_camera_limit_offset_v1(const RacVector3fV1 offset) {
  if (!finite(offset) || !std::isfinite(length(offset))) {
    throw ThirdPersonCameraError("An original camera offset is non-finite");
  }
  const float limit = source_float(kRacCameraOffsetLimitBitsV1);
  return length(offset) > limit ? with_length(offset, limit) : offset;
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
  step_pal_frame(player_position, {});
}

void RacGameplayCameraV1::step_pal_frame(const RacVector3fV1 player_position,
                                         const RacGameplayCameraInputV1 input) {
  if (!finite(player_position)) {
    throw ThirdPersonCameraError(
        "An original gameplay camera target is non-finite");
  }
  if (!std::isfinite(input.right_x) || !std::isfinite(input.right_y) ||
      std::abs(input.right_x) > 1.0F || std::abs(input.right_y) > 1.0F ||
      input.special_override_active ||
      (input.player_yaw_radians &&
       (!std::isfinite(*input.player_yaw_radians) ||
        std::abs(*input.player_yaw_radians) > kRacPi))) {
    throw ThirdPersonCameraError("An original camera requires ordinary-mode pad input");
  }
  const float yaw_rate = rac_gameplay_camera_yaw_step_v1(input.yaw_selector);
  auto next = state_;
  if (input.player_yaw_radians) {
    next.player_yaw = *input.player_yaw_radians;
  }
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
  // 1ecdf0: horizontal movement since the previous update (166f80 minus
  // its 166f40 component), length 166fb4; 166f70 then holds the player.
  const auto movement = horizontal_part(
      subtract(player_position, next.previous_player_position));
  const float movement_length = length(movement);
  next.previous_player_position = player_position;
  if (!next.eye_initialized) {
    state_ = validated_state(next);
    return;
  }

  // 2e6498 mode 0: target +64 = player. 2e6ce0: reference +128 =
  // 1.5*up + target (+216 counter zero); pivot +144 = 2*up + target when
  // the 2e70c8/2e7140 probes find no ceiling and no floor above the target.
  const auto look_reference = add(scale(kRacUp, kRacLookHeight), player_position);
  const auto pivot = add(scale(kRacUp, kRacEyeHeight), player_position);

  // 2ea3f0 (+16 is set to 1 by every 2e72e8): +304 moves 0.75 of the way to
  // the previous unfiltered eye seen from the current pivot, then its length
  // is kept in [+348 - +512, +348] and +512 = +348 - length.
  {
    const auto previous = subtract(next.unfiltered_eye, pivot);
    auto offset = add(next.desired_offset,
                      scale(subtract(previous, next.desired_offset),
                            source_float(0x3f400000U)));
    float offset_length = length(offset);
    const float limit = next.target_distance;
    if (limit < offset_length) {
      offset_length = limit;
      offset = with_length(offset, offset_length);
    } else {
      const float lower = limit - next.offset_shortfall;
      if (offset_length < lower) {
        offset_length = lower;
        offset = with_length(offset, offset_length);
      }
    }
    next.offset_shortfall = limit - offset_length;
    next.desired_offset = offset;
  }

  // 2e9e60: decoded pad floats; X is negated for a zero 15eee0, Y is
  // negated for a nonzero 15eedc. Neutral axes fall back to +452/+460,
  // which 2e72e8 clears every update (zero in this domain).
  const float yaw_input = input.yaw_option_nonzero ? input.right_x : -input.right_x;
  const float pitch_input = input.pitch_option_nonzero ? -input.right_y : input.right_y;

  // 2ea068: yaw response k=d=1 without a maximum, step = +444 * response.
  next.yaw_response = rac_camera_spring_step_v1(
      next.yaw_response, yaw_input, next.yaw_response_velocity, {1.0F, 1.0F, 0.0F});
  next.applied_yaw_step = yaw_rate * next.yaw_response;
  const float response = rac_gameplay_camera_pitch_response_v1(pitch_input);
  // 2ea168..2ea1dc (with delay slots): 0.01 for a zero response or a
  // same-sign magnitude decrease, otherwise 0.02.
  const bool same_sign_decrease =
      std::abs(response) < std::abs(next.pitch_response) &&
      ((0.0F < response && 0.0F < next.pitch_response) ||
       (response < 0.0F && next.pitch_response < 0.0F));
  const float maximum = response == 0.0F || same_sign_decrease
                            ? kRacPitchSlow : kRacPitchFast;
  next.pitch_response = rac_camera_spring_step_v1(
      next.pitch_response, response, next.pitch_response_velocity,
      {1.0F, 1.0F, maximum});
  const float target_pitch = next.pitch_response * kRacPitchTravel;
  // 2ea20c: a nonzero yaw input rotates +304 about up by +356; a zero input
  // uses +456 (2e5b68 platform yaw delta, zero on static ground).
  if (yaw_input != 0.0F) {
    next.desired_offset = rotate_source(next.desired_offset, kRacUp,
                                        next.applied_yaw_step);
  }
  const float offset_vertical = dot(next.desired_offset, kRacUp);
  const float offset_length = length(next.desired_offset);
  const auto pitch_axis =
      with_length(source_cross(kRacUp, next.desired_offset), 1.0F);
  // 1ff860(x=length, y=dot): atan2(dot, length), not asin.
  const float offset_pitch = source_atan2(offset_vertical, offset_length);
  if (std::abs(offset_pitch) < std::abs(target_pitch) &&
      maximum == kRacPitchSlow) {
    next.pitch_response = offset_pitch / kRacPitchTravel; // 2ea2f0
  }
  // 2ea2f4..2ea384: approach the target by at most 1.75 degrees; the
  // +520 guard of the decreasing branch is zero in this domain.
  const float pitch_step = kRacPitchRotationStep;
  if (offset_pitch < target_pitch) {
    next.applied_pitch_step =
        target_pitch < wrap_source_angle(offset_pitch + pitch_step)
            ? wrap_source_angle(target_pitch - offset_pitch)
            : pitch_step;
  } else if (target_pitch < offset_pitch) {
    next.applied_pitch_step =
        wrap_source_angle(offset_pitch + -pitch_step) < target_pitch
            ? wrap_source_angle(target_pitch - offset_pitch)
            : -pitch_step;
  } else {
    next.applied_pitch_step = 0.0F;
  }
  next.desired_offset = rotate_source(next.desired_offset, pitch_axis,
                                      next.applied_pitch_step);

  // 2e91d0 (Moby avoidance) is excluded: collision_modeled=false.

  // 2eabd0: +0 = pivot + +304 before filtering.
  next.unfiltered_eye = add(pivot, next.desired_offset);
  const float target_length = next.target_distance - next.offset_shortfall;
  const float current_radius = length(next.smoothed_offset);

  // 2ea9c8: radius spring selection. 166f90 = movement / 166fb4; slot +0
  // is still the previous update's forward. A nonzero +548 with exactly
  // zero movement compares the player Moby +c0 row instead.
  const float backward_threshold = source_float(0xbe99999aU);
  const RacVector3fV1 player_forward{std::cos(next.player_yaw),
                                     std::sin(next.player_yaw), 0.0F};
  bool backward_motion =
      next.movement_distance_active && movement_length == 0.0F &&
      dot(player_forward, state_.forward) <= backward_threshold;
  if (!backward_motion && source_float(0x38d1b717U) < std::abs(movement_length)) {
    backward_motion = dot(scale(movement, 1.0F / movement_length),
                          state_.forward) <= backward_threshold;
  }
  auto distance_spring = profile_.eye_distance_spring;
  if (backward_motion) {
    next.movement_distance_active = true; // +548 = 1feed0(120)
    float weight = movement_length * source_float(0x4164923aU);
    if (1.0F < weight) {
      weight = 1.0F;
    }
    // +552 = max(+552, +344+88 + (6.0 - +344+88) * weight); 1414f4 != 1.
    const float default_distance = source_float(kRacCameraOffsetLimitBitsV1);
    const float candidate =
        default_distance + (source_float(0x40c00000U) - default_distance) * weight;
    if (next.movement_distance_maximum < candidate) {
      next.movement_distance_maximum = candidate;
    }
    distance_spring.stiffness =
        distance_spring.stiffness +
        (source_float(0x3d23d70aU) - distance_spring.stiffness) * weight;
    distance_spring.damping =
        distance_spring.damping +
        (source_float(0x3e99999aU) - distance_spring.damping) * weight;
  } else if (next.movement_distance_active) {
    next.movement_distance_active = false;
    next.movement_distance_maximum = source_float(kRacCameraOffsetLimitBitsV1);
  }

  const float radius = rac_camera_spring_step_v1(
      current_radius, target_length, next.eye_distance_velocity, distance_spring);
  if (!(target_length > 0.0F) || !(radius > 0.0F)) {
    throw ThirdPersonCameraError("An original camera eye offset is degenerate");
  }
  const float desired_vertical = dot(next.desired_offset, kRacUp);
  const float target_elevation = source_asin(desired_vertical / target_length);
  const float smoothed_vertical = dot(next.smoothed_offset, kRacUp);
  const float current_elevation = source_asin(smoothed_vertical / radius);
  const float elevation = rac_camera_angular_spring_step_v1(
      current_elevation, target_elevation, next.eye_elevation_velocity,
      profile_.eye_angle_spring);
  const auto desired_horizontal =
      subtract(next.desired_offset, scale(kRacUp, desired_vertical));
  const auto smoothed_horizontal =
      subtract(next.smoothed_offset, scale(kRacUp, smoothed_vertical));
  const float horizontal_dot = dot(smoothed_horizontal, desired_horizontal);
  float horizontal_product = length(smoothed_horizontal);
  horizontal_product = horizontal_product * length(desired_horizontal);
  if (!(horizontal_product > 0.0F)) {
    throw ThirdPersonCameraError(
        "An original camera horizontal offset is degenerate");
  }
  const float azimuth =
      kRacHalfPi - source_asin(horizontal_dot / horizontal_product);
  float azimuth_step = rac_camera_angular_spring_step_v1(
      0.0F, azimuth, next.eye_azimuth_velocity, profile_.eye_angle_spring);
  // 2ead9c..2eadd0: the sign is +1 when dot(up x smoothed, desired) >= 0.
  const auto azimuth_side = source_cross(smoothed_horizontal, kRacUp);
  azimuth_step = 0.0F <= dot(azimuth_side, desired_horizontal)
                     ? azimuth_step * 1.0F
                     : azimuth_step * -1.0F;
  auto rotated = rotate_source(smoothed_horizontal, kRacUp, azimuth_step);
  const auto elevation_axis = source_cross(kRacUp, rotated);
  rotated = rotate_source(rotated, elevation_axis, elevation);
  next.smoothed_offset = with_length(rotated, radius);
  const auto anchor = add(scale(kRacUp, kRacEyeHeight), next.focus);
  // 2eae50..2eaeec: keep +320 at least 15 degrees from up and clear the
  // three velocities; the eye at 2eaef8 uses the corrected +320.
  const float smoothed_length = length(next.smoothed_offset);
  if (smoothed_length != 0.0F) {
    const float from_up = kRacHalfPi - source_asin(
        dot(next.smoothed_offset, kRacUp) / smoothed_length);
    const float pole_limit = source_float(0x3e860a92U);
    if (std::abs(from_up) < pole_limit) {
      const auto pole_axis = source_cross(next.smoothed_offset, kRacUp);
      next.smoothed_offset = rotate_source(
          next.smoothed_offset, pole_axis,
          wrap_source_angle(pole_limit - std::abs(from_up)));
      next.eye_elevation_velocity = 0.0F;
      next.eye_distance_velocity = 0.0F;
      next.eye_azimuth_velocity = 0.0F;
    }
  }
  next.eye = add(anchor, next.smoothed_offset);

  // 2e9828 is empty. 2ea4c0, mode byte +260 != 11: horizontal forward
  // from the eye to +128; below 0.05 the previous forward is retained.
  const auto look_horizontal =
      subtract(horizontal_part(look_reference), horizontal_part(next.eye));
  const float look_horizontal_length = length(look_horizontal);
  if (source_float(0x3d4ccccdU) <= look_horizontal_length) {
    next.forward = scale(look_horizontal, 1.0F / look_horizontal_length);
  }
  auto left = with_length(source_cross(next.forward, kRacUp), 1.0F);
  next.eye_height = rac_camera_spring_step_v1(
      next.eye_height, kRacEyeHeight, next.eye_height_velocity,
      profile_.height_spring);
  next.look_height = rac_camera_spring_step_v1(
      next.look_height, kRacLookHeight, next.look_height_velocity,
      profile_.height_spring);
  // 2ea6e0..2ea750 in source order: V = horizontal look + up *
  // (dot(+320, up) + (+40 - +36)), i.e. eye height above the reference.
  const float height_difference = next.eye_height - next.look_height;
  auto composed = scale(kRacUp, -dot(next.smoothed_offset, kRacUp));
  composed = subtract(composed, look_horizontal);
  auto shifted = subtract(next.eye, composed);
  shifted = subtract(shifted, scale(kRacUp, -height_difference));
  const auto look = subtract(shifted, next.eye);
  const float look_length = length(look);
  next.view_pitch_difference = 0.0F;
  if (look_length != 0.0F) {
    float facing_pitch =
        kRacHalfPi - source_asin(dot(next.forward, look) / look_length);
    const auto basis_up = source_cross(left, next.forward);
    if (dot(basis_up, look) < 0.0F) {
      facing_pitch = -facing_pitch;
    }
    // 2ea7c0..2ea84c: look-pitch bias from atan2(dot(+304,up), |+304|).
    const float desired_pitch_ratio =
        source_atan2(dot(next.desired_offset, kRacUp),
                     length(next.desired_offset)) / kRacPitchTravel;
    float bias = 0.0F;
    if (desired_pitch_ratio < source_float(0xbdcccccdU)) {
      float magnitude = -desired_pitch_ratio;
      if (source_float(0x3f000000U) < magnitude) {
        magnitude = 1.0F - magnitude;
      }
      magnitude = magnitude + magnitude;
      bias = magnitude * source_float(0x3e860a92U) + 0.0F;
    }
    next.look_pitch = rac_camera_angular_spring_step_v1(
        next.look_pitch, bias, next.look_pitch_velocity,
        profile_.look_pitch_spring);
    next.view_pitch_difference = rac_gameplay_camera_clamp_pitch_v1(
        wrap_source_angle(facing_pitch - next.look_pitch));
    next.forward = with_length(
        rotate_source(next.forward, left, next.view_pitch_difference), 1.0F);
  }
  // 2ea950..2ea984: left = normalize(up x forward), up = forward x left.
  left = with_length(source_cross(next.forward, kRacUp), 1.0F);
  next.up = source_cross(left, next.forward);

  // 2e72e8: +348 follows +364 (2e9900 override from 2ea9c8) or the 4.64
  // snapshot with k=+372 0.003, d=0.2; this first affects the next update.
  next.target_distance = rac_camera_spring_step_v1(
      next.target_distance,
      next.movement_distance_active ? next.movement_distance_maximum
                                    : source_float(kRacCameraOffsetLimitBitsV1),
      next.target_distance_velocity,
      {source_float(0x3b449ba6U), kRacFocusDamping, 0.0F});
  state_ = validated_state(next);
}

RacGameplayCameraStateV1 rac_gameplay_camera_level_enter_state_v1(
    const RacVector3fV1 player_position, const float player_yaw_radians,
    const RacGameplayCameraInputV1 input) {
  RacGameplayCameraV1 camera(rac_gameplay_camera_profile_v1(),
                             rac_gameplay_camera_initial_state_v1(player_position,
                                                                  player_yaw_radians));
  camera.step_pal_frame(player_position, input);
  return camera.state();
}

ThirdPersonCameraViewV1 rac_gameplay_camera_renderer_view_v1(
    const RacGameplayCameraStateV1 &state,
    const RacGameplayProjectionInputV1 &projection,
    const float world_projection_scale) {
  static_cast<void>(validated_state(state));
  if (!state.eye_initialized || !std::isfinite(world_projection_scale) ||
      !(world_projection_scale > 0.0F) || length(state.forward) == 0.0F ||
      length(cross(state.forward, state.up)) == 0.0F) {
    throw ThirdPersonCameraError("An original camera cannot produce a renderer view");
  }
  const auto tangents = rac_gameplay_projection_v1(projection);
  ThirdPersonCameraViewV1 result;
  result.eye = {state.eye.x, state.eye.y, state.eye.z};
  // Add in binary64 so a distant eye does not erase a unit direction.
  result.target = {result.eye.x + state.forward.x, result.eye.y + state.forward.y,
                   result.eye.z + state.forward.z};
  result.up = {state.up.x, state.up.y, state.up.z};
  result.vertical_field_of_view_radians =
      2.0 * std::atan(static_cast<double>(tangents.vertical_tangent));
  result.aspect_ratio = static_cast<double>(tangents.horizontal_tangent) /
                        tangents.vertical_tangent;
  result.near_plane_distance = static_cast<double>(projection.near_distance) /
                               world_projection_scale;
  result.far_plane_distance = static_cast<double>(projection.far_distance) /
                              world_projection_scale;
  return result;
}

ThirdPersonCameraViewV1 RacGameplayCameraV1::view() const {
  return rac_gameplay_camera_renderer_view_v1(state_,
      rac_gameplay_pal_projection_defaults_v1(),
      source_float(kRacWorldProjectionScaleBitsV1));
}

const RacGameplayCameraProfileV1 &
RacGameplayCameraV1::profile() const noexcept {
  return profile_;
}

const RacGameplayCameraStateV1 &RacGameplayCameraV1::state() const noexcept {
  return state_;
}

} // namespace openrc::game
