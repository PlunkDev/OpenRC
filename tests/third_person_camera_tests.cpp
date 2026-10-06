#include "openrc/third_person_camera.hpp"

#include "openrc/game_input.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kTolerance = 1.0e-12;

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] bool near(const double first, const double second,
                        const double tolerance = kTolerance) noexcept {
  return std::abs(first - second) <= tolerance;
}

[[nodiscard]] bool finite(const openrc::CollisionVectorV1 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

template <typename Callback>
void expect_camera_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::ThirdPersonCameraError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::game::ThirdPersonCameraProfileV1 profile() {
  return {
      2.0, 8.0,  1.2,       -kPi / 4.0, kPi / 3.0, kPi,
      kPi, 12.0, kPi / 3.0, 16.0 / 9.0, 0.1,       100.0,
  };
}

[[nodiscard]] openrc::game::ThirdPersonCameraV1
camera(const double yaw = 0.0, const double pitch = 0.0,
       const double distance = 5.0) {
  return {profile(), {yaw, pitch, distance}};
}

void expect_movement(const openrc::game::CameraRelativeMovementV1 movement,
                     const double expected_x, const double expected_y,
                     const std::string &message) {
  expect(near(movement.move_x, expected_x) && near(movement.move_y, expected_y),
         message);
}

void test_camera_relative_movement_at_cardinal_yaws() {
  using namespace openrc::game;
  constexpr auto kMaximumAxis = kGameInputAxisMagnitudeV1;

  auto subject = camera();
  expect_movement(subject.map_movement(0, kMaximumAxis), 1.0, 0.0,
                  "yaw zero did not map forward input to world +X");
  expect_movement(subject.map_movement(kMaximumAxis, 0), 0.0, -1.0,
                  "yaw zero did not map right input to world -Y");

  subject.set_state({kPi / 2.0, 0.0, 5.0});
  expect_movement(subject.map_movement(0, kMaximumAxis), 0.0, 1.0,
                  "yaw 90 did not map forward input to world +Y");
  expect_movement(subject.map_movement(kMaximumAxis, 0), 1.0, 0.0,
                  "yaw 90 did not map right input to world +X");

  subject.set_state({kPi, 0.0, 5.0});
  expect_movement(subject.map_movement(0, kMaximumAxis), -1.0, 0.0,
                  "yaw 180 did not map forward input to world -X");
  expect_movement(subject.map_movement(kMaximumAxis, 0), 0.0, 1.0,
                  "yaw 180 did not map right input to world +Y");

  const auto diagonal = subject.map_movement(kMaximumAxis, kMaximumAxis);
  expect(near(std::hypot(diagonal.move_x, diagonal.move_y), 1.0),
         "diagonal camera-relative movement escaped the unit circle");

  subject.set_state({0.0, 0.0, 5.0});
  constexpr std::int16_t kPartialAxis = 8'192;
  const auto partial = subject.map_movement(0, kPartialAxis);
  expect_movement(
      partial,
      static_cast<double>(kPartialAxis) /
          static_cast<double>(kGameInputAxisMagnitudeV1),
      0.0,
      "camera-relative movement expanded partial analog travel to full speed");

  constexpr double kSourcePrecisionX = 0.123456789012345;
  constexpr double kSourcePrecisionY = 0.234567890123456;
  const auto precise =
      subject.map_unit_movement(kSourcePrecisionX, kSourcePrecisionY);
  expect_movement(precise, kSourcePrecisionY, -kSourcePrecisionX,
                  "camera mapping requantized source-response movement");
}

void test_pitch_distance_clamps_and_elapsed_time() {
  using namespace openrc::game;
  constexpr auto kMaximumAxis = kGameInputAxisMagnitudeV1;

  auto subject = camera();
  subject.fixed_update({0, kMaximumAxis, kMaximumAxis}, 1.0);
  expect(subject.state().pitch_radians == profile().maximum_pitch_radians &&
             subject.state().distance == profile().minimum_distance,
         "positive camera controls did not stop at the pitch/zoom limits");

  subject.fixed_update({0, -kMaximumAxis, -kMaximumAxis}, 1.0);
  expect(subject.state().pitch_radians == profile().minimum_pitch_radians &&
             subject.state().distance == profile().maximum_distance,
         "negative camera controls did not stop at the pitch/zoom limits");

  auto split_ticks = camera(0.1);
  auto joined_tick = camera(0.1);
  split_ticks.fixed_update({kMaximumAxis, 0, 0}, 0.25);
  split_ticks.fixed_update({kMaximumAxis, 0, 0}, 0.25);
  joined_tick.fixed_update({kMaximumAxis, 0, 0}, 0.5);
  expect(near(split_ticks.state().yaw_radians, joined_tick.state().yaw_radians),
         "camera rotation depended on presentation update count instead of "
         "explicit elapsed time");
}

void test_neutral_input_has_no_drift() {
  auto subject = camera(-0.0, 0.25, 5.0);
  const auto before = subject.state();
  subject.fixed_update({}, 1.0 / 60.0);
  expect(subject.state() == before && std::signbit(subject.state().yaw_radians),
         "neutral camera input changed its replay state");

  subject.fixed_update({1234, -2345, 6789}, 0.0);
  expect(subject.state() == before && std::signbit(subject.state().yaw_radians),
         "a zero-duration camera tick changed its replay state");
}

void test_view_is_finite_and_orthonormal() {
  auto subject = camera(0.7, 0.35, 5.0);
  const auto result = subject.view({10.0, -4.0, 2.0});
  expect(finite(result.eye) && finite(result.target) && finite(result.up),
         "a valid camera state produced a non-finite view");
  expect(near(result.target.x, 10.0) && near(result.target.y, -4.0) &&
             near(result.target.z, 3.2),
         "the camera did not apply the authored target height");

  const openrc::CollisionVectorV1 forward{
      result.target.x - result.eye.x,
      result.target.y - result.eye.y,
      result.target.z - result.eye.z,
  };
  const auto forward_length = std::sqrt(
      forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
  const auto up_length =
      std::sqrt(result.up.x * result.up.x + result.up.y * result.up.y +
                result.up.z * result.up.z);
  const auto dot = forward.x * result.up.x + forward.y * result.up.y +
                   forward.z * result.up.z;
  expect(near(forward_length, subject.state().distance) &&
             near(up_length, 1.0) && near(dot, 0.0),
         "the camera view basis is not finite and orthonormal");
  expect(result.vertical_field_of_view_radians ==
                 profile().vertical_field_of_view_radians &&
             result.aspect_ratio == profile().aspect_ratio &&
             result.near_plane_distance == profile().near_plane_distance &&
             result.far_plane_distance == profile().far_plane_distance,
         "the camera view lost its explicit projection policy");
}

void test_invalid_profiles_states_and_updates_are_rejected() {
  using namespace openrc::game;

  auto invalid_profile = profile();
  invalid_profile.target_height = std::numeric_limits<double>::quiet_NaN();
  expect_camera_error(
      [&] { validate_third_person_camera_profile_v1(invalid_profile); },
      "a non-finite camera profile was accepted");

  invalid_profile = profile();
  invalid_profile.minimum_distance = 0.0;
  expect_camera_error(
      [&] { validate_third_person_camera_profile_v1(invalid_profile); },
      "a zero minimum camera distance was accepted");

  invalid_profile = profile();
  invalid_profile.maximum_pitch_radians = kPi / 2.0;
  expect_camera_error(
      [&] { validate_third_person_camera_profile_v1(invalid_profile); },
      "a singular camera pitch limit was accepted");

  invalid_profile = profile();
  invalid_profile.near_plane_distance = invalid_profile.minimum_distance;
  expect_camera_error(
      [&] { validate_third_person_camera_profile_v1(invalid_profile); },
      "a clipping plane beyond the nearest target was accepted");

  invalid_profile = profile();
  invalid_profile.far_plane_distance = invalid_profile.maximum_distance;
  expect_camera_error(
      [&] { validate_third_person_camera_profile_v1(invalid_profile); },
      "a clipping plane before the farthest target was accepted");

  expect_camera_error(
      [&] {
        validate_third_person_camera_state_v1(profile(),
                                              {kPi + 0.01, 0.0, 5.0});
      },
      "a non-canonical camera yaw was accepted");
  expect_camera_error(
      [&] {
        validate_third_person_camera_state_v1(profile(), {0.0, 0.0, 9.0});
      },
      "a camera distance outside its profile was accepted");

  auto subject = camera();
  const auto before = subject.state();
  expect_camera_error(
      [&] {
        subject.fixed_update({std::numeric_limits<std::int16_t>::min(), 0, 0},
                             1.0 / 60.0);
      },
      "an asymmetric minimum camera axis was accepted");
  expect_camera_error([&] { subject.fixed_update({}, -1.0); },
                      "a negative camera tick duration was accepted");
  expect_camera_error(
      [&] {
        static_cast<void>(
            subject.view({std::numeric_limits<double>::infinity(), 0.0, 0.0}));
      },
      "a non-finite camera focus was accepted");
  expect_camera_error(
      [&] {
        static_cast<void>(subject.map_unit_movement(
            std::numeric_limits<double>::quiet_NaN(), 0.0));
      },
      "non-finite source-response movement was accepted");
  expect_camera_error(
      [&] { static_cast<void>(subject.map_unit_movement(1.01, 0.0)); },
      "source-response movement outside the unit domain was accepted");
  expect(subject.state() == before,
         "a rejected camera operation partially changed its state");
}

[[nodiscard]] bool near_relative(const double actual, const double expected,
                                 const double tolerance) noexcept {
  return std::abs(actual - expected) <= tolerance * std::abs(expected);
}

void test_original_projection_uses_source_factor_bits() {
  using namespace openrc::game;

  expect(std::bit_cast<std::uint32_t>(rac_vertical_tangent_factor_v1(
             RacProjectionSelectorV1::pal)) == 0x3f418937U,
         "the PAL vertical tangent factor differs from source bits");
  expect(std::bit_cast<std::uint32_t>(rac_vertical_tangent_factor_v1(
             RacProjectionSelectorV1::other)) == 0x3f466666U,
         "the other vertical tangent factor differs from source bits");

  const auto pal_input =
      rac_gameplay_projection_defaults_v1(RacProjectionSelectorV1::pal,
                                          256.0F, 224.0F);
  expect(std::bit_cast<std::uint32_t>(pal_input.horizontal_tangent) ==
                 0x3f2147aeU &&
             std::bit_cast<std::uint32_t>(pal_input.near_distance) ==
                 0x42000000U &&
             std::bit_cast<std::uint32_t>(pal_input.far_distance) ==
                 0x49360000U,
         "the 1f7bc8 gameplay projection scalars differ from source bits");

  const auto pal = rac_gameplay_projection_v1(pal_input);
  const auto other = rac_gameplay_projection_v1(
      rac_gameplay_projection_defaults_v1(RacProjectionSelectorV1::other,
                                          256.0F, 224.0F));
  constexpr double kTangent = 0.63;
  constexpr double kNear = 32.0;
  constexpr double kFar = 745472.0;
  constexpr double kDepthScale = -8388080.0;
  expect(pal.horizontal_tangent == pal_input.horizontal_tangent &&
             near_relative(pal.vertical_tangent, kTangent * 0.756, 1.0e-6) &&
             near_relative(other.vertical_tangent, kTangent * 0.775, 1.0e-6),
         "the vertical tangent is not horizontal tangent times the factor");
  expect(near_relative(pal.scale_x, 256.0 / (kTangent * kNear), 1.0e-6) &&
             near_relative(pal.scale_y, 224.0 / (kTangent * 0.756 * kNear),
                           1.0e-6) &&
             near_relative(other.scale_y,
                           224.0 / (kTangent * 0.775 * kNear), 1.0e-6) &&
             pal.scale_x == other.scale_x,
         "the viewport scales do not divide by tangent times near");
  expect(near_relative(pal.depth_z,
                       (kFar + kNear) / (kNear * (kFar - kNear)) * kDepthScale,
                       1.0e-5) &&
             near_relative(pal.depth_w,
                           -2.0 * kNear * kFar / (kNear * (kFar - kNear)) *
                               kDepthScale,
                           1.0e-5),
         "the projection depth row differs from the 1f7d00 formula");

  auto invalid = pal_input;
  invalid.far_distance = invalid.near_distance;
  expect_camera_error(
      [&] { static_cast<void>(rac_gameplay_projection_v1(invalid)); },
      "a projection without positive depth span was accepted");
  invalid = pal_input;
  invalid.viewport_half_width = 0.0F;
  expect_camera_error(
      [&] { static_cast<void>(rac_gameplay_projection_v1(invalid)); },
      "a projection with an empty viewport was accepted");
}

void test_original_focus_follow_sequence() {
  using namespace openrc::game;

  const auto profile = rac_gameplay_camera_profile_v1();
  expect(std::bit_cast<std::uint32_t>(profile.focus_spring.stiffness) ==
                 0x3c75c28fU &&
             std::bit_cast<std::uint32_t>(profile.focus_spring.damping) ==
                 0x3e4ccccdU &&
             profile.focus_spring.maximum_step == 0.0F,
         "the focus spring differs from the 2e72e8 source constants");

  const RacVector3fV1 player{1.0F, -2.0F, 0.5F};
  RacGameplayCameraV1 subject(profile,
                              rac_gameplay_camera_initial_state_v1({}));
  constexpr std::array kExpectedUnitFocus{0.015, 0.041775, 0.077568375};
  for (const auto expected : kExpectedUnitFocus) {
    subject.step_pal_frame(player);
    const auto focus = subject.state().focus;
    expect(near(focus.x, expected, 1.0e-6) &&
               near(focus.y, -2.0 * expected, 1.0e-6) &&
               near(focus.z, 0.5 * expected, 1.0e-6),
           "the focus follow differs from its frame-by-frame recurrence");
  }

  double reference = kExpectedUnitFocus.back();
  double velocity = 0.035793375;
  for (int frame = 3; frame < 400; ++frame) {
    subject.step_pal_frame(player);
    const auto delta = 1.0 - reference;
    velocity = velocity + (0.015 * delta - 0.2 * velocity);
    velocity = std::clamp(velocity, -std::abs(delta), std::abs(delta));
    reference += velocity;
    expect(near(subject.state().focus.x, reference, 1.0e-4),
           "the focus follow drifted from its reference recurrence");
  }
  expect(near(subject.state().focus.x, 1.0, 1.0e-4) &&
             near(subject.state().focus.y, -2.0, 2.0e-4) &&
             near(subject.state().focus.z, 0.5, 1.0e-4),
         "the focus follow did not settle on the player");

  RacGameplayCameraV1 overshoot(
      profile, {{0.0F, 0.0F, 0.0F}, {5.0F, 0.0F, 0.0F}});
  overshoot.step_pal_frame({1.0F, 0.0F, 0.0F});
  expect(overshoot.state().focus.x == 1.0F &&
             overshoot.state().focus_velocity.x == 1.0F,
         "the spring step was not clamped to the remaining distance");

  float spring_velocity = 0.0F;
  const auto limited =
      rac_camera_spring_step_v1(0.0F, 1.0F, spring_velocity, {1.0F, 1.0F, 0.02F});
  expect(limited == 0.02F && spring_velocity == 0.02F,
         "the optional spring maximum step was not applied");

  const auto before = subject.state();
  expect_camera_error(
      [&] {
        subject.step_pal_frame(
            {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
      },
      "a non-finite camera target was accepted");
  expect(subject.state() == before,
         "a rejected camera step partially changed its state");
}

void test_original_pitch_response() {
  using namespace openrc::game;

  expect(rac_gameplay_camera_pitch_response_v1(0.0F) == 0.0F &&
             rac_gameplay_camera_pitch_response_v1(0.29F) == 0.0F &&
             rac_gameplay_camera_pitch_response_v1(
                 std::bit_cast<float>(0x3e99999aU)) == 0.0F &&
             rac_gameplay_camera_pitch_response_v1(
                 -std::bit_cast<float>(0x3e99999aU)) == 0.0F,
         "the pitch dead zone is not strict at 0.3");
  expect(near(rac_gameplay_camera_pitch_response_v1(1.0F), 1.0, 1.0e-6) &&
             near(rac_gameplay_camera_pitch_response_v1(-0.65F), -0.5, 1.0e-6),
         "the pitch response does not rescale the remaining travel");
  expect_camera_error(
      [] { static_cast<void>(rac_gameplay_camera_pitch_response_v1(1.5F)); },
      "a pitch axis outside the pad domain was accepted");
}

} // namespace

int main() {
  try {
    test_camera_relative_movement_at_cardinal_yaws();
    test_pitch_distance_clamps_and_elapsed_time();
    test_neutral_input_has_no_drift();
    test_view_is_finite_and_orthonormal();
    test_invalid_profiles_states_and_updates_are_rejected();
    test_original_projection_uses_source_factor_bits();
    test_original_focus_follow_sequence();
    test_original_pitch_response();
    std::cout << "third_person_camera_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "third_person_camera_tests: " << error.what() << '\n';
    return 1;
  }
}
