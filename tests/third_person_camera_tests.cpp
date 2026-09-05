#include "openrc/third_person_camera.hpp"

#include "openrc/game_input.hpp"

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
  expect(subject.state() == before,
         "a rejected camera operation partially changed its state");
}

} // namespace

int main() {
  try {
    test_camera_relative_movement_at_cardinal_yaws();
    test_pitch_distance_clamps_and_elapsed_time();
    test_neutral_input_has_no_drift();
    test_view_is_finite_and_orthonormal();
    test_invalid_profiles_states_and_updates_are_rejected();
    std::cout << "third_person_camera_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "third_person_camera_tests: " << error.what() << '\n';
    return 1;
  }
}
