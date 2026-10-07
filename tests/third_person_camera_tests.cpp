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

  RacGameplayCameraStateV1 focus_only;
  focus_only.focus_velocity.x = 5.0F;
  RacGameplayCameraV1 overshoot(profile, focus_only);
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

float source_float(const std::uint32_t bits) { return std::bit_cast<float>(bits); }

void test_original_yaw_selectors_and_inversions() {
  using namespace openrc::game;
  constexpr std::array<std::uint32_t, 3> rates{0x3c8efa35U, 0x3cb9dedeU, 0x3ce4c388U};
  for (std::uint32_t selector = 0; selector < rates.size(); ++selector) {
    RacGameplayCameraV1 camera(rac_gameplay_camera_profile_v1(),
                                rac_gameplay_camera_initial_state_v1({}));
    RacGameplayCameraInputV1 input;
    input.right_x = 1.0F;
    input.yaw_selector = selector;
    camera.step_pal_frame({}, input);
    expect(std::bit_cast<std::uint32_t>(camera.state().applied_yaw_step) == rates[selector] &&
               camera.state().yaw_response == 1.0F,
           "yaw is not immediate k=1,d=1 response times selected source rate");
    // 260c80 stores q*v*conj(q) (1ffe18 = a (x) b): a positive source angle
    // is a right-handed turn, so the eye offset (-4.64,0,0) moves to -Y.
    expect(near(std::atan2(camera.state().desired_offset.y,
                           -camera.state().desired_offset.x),
                -source_float(rates[selector]), 1.0e-6),
           "260c80 source yaw rotation has the wrong neutral sign");
    input.yaw_option_nonzero = false;
    camera.step_pal_frame({}, input);
    expect(camera.state().applied_yaw_step == -source_float(rates[selector]) &&
               near(camera.state().desired_offset.y, 0.0, 1.0e-6),
           "15eee0 inversion did not undo yaw");
  }
  expect_camera_error([] { static_cast<void>(rac_gameplay_camera_yaw_step_v1(3U)); },
                       "out-of-table yaw selector was accepted");
}

void test_original_pitch_smoothing_and_feedback() {
  using namespace openrc::game;
  const auto profile = rac_gameplay_camera_profile_v1();
  RacGameplayCameraV1 dead(profile, rac_gameplay_camera_initial_state_v1({}));
  RacGameplayCameraInputV1 input;
  input.right_y = 0.3F;
  dead.step_pal_frame({}, input);
  expect(dead.state().pitch_response_velocity == 0.0F &&
             near(dead.state().applied_pitch_step, 0.0F, 1.0e-6) &&
             dead.state().desired_offset.z == 0.0F,
         "the second pitch dead zone changed the offset at its boundary");
  input.right_y = -1.0F;
  RacGameplayCameraV1 rising(profile, rac_gameplay_camera_initial_state_v1({}));
  rising.step_pal_frame({}, input);
  expect(rising.state().pitch_response_velocity == source_float(0x3ca3d70aU) &&
             near(rising.state().applied_pitch_step,
                  source_float(0x3ca3d70aU) * source_float(0x3f32b8c2U), 2.0e-6) &&
             rising.state().desired_offset.z > 0.0F,
         "increasing pitch did not use the 0.02 step and 40-degree target");
  // Disable the source feedback condition using a pre-tilted desired offset.
  auto initial = rac_gameplay_camera_initial_state_v1({});
  initial.desired_offset = {-4.0F, 0.0F, 2.0F};
  // +0 = pivot (player + 2*up) + desired offset.
  initial.unfiltered_eye = {-4.0F, 0.0F, 4.0F};
  initial.pitch_response = 0.8F;
  RacGameplayCameraV1 decreasing(profile, initial);
  input.right_y = -0.65F; // response +0.5, same sign and smaller magnitude
  decreasing.step_pal_frame({}, input);
  expect(near(decreasing.state().pitch_response_velocity, -0.01, 1.0e-6),
         "same-sign decreasing pitch did not use the 0.01 maximum");
  RacGameplayCameraV1 neutral(profile, initial);
  neutral.step_pal_frame({});
  expect(near(neutral.state().pitch_response_velocity, -0.01, 1.0e-6),
         "neutral pitch did not take the branch-delay-slot 0.01 maximum");
  RacGameplayCameraV1 reversed(profile, rac_gameplay_camera_initial_state_v1({}));
  input = {};
  input.right_y = -1.0F;
  input.pitch_option_nonzero = false;
  reversed.step_pal_frame({}, input);
  expect(reversed.state().desired_offset.z < 0.0F &&
             reversed.state().pitch_response_velocity == -source_float(0x3ca3d70aU),
         "15eedc did not invert pitch before its response");
}

void test_original_offset_and_view_pitch_limits() {
  using namespace openrc::game;
  const float limit = source_float(0x40947ae1U);
  const auto limited = rac_gameplay_camera_limit_offset_v1({30.0F, 40.0F, 0.0F});
  expect(near(limited.x, 0.6 * limit, 1.0e-6) &&
             near(limited.y, 0.8 * limit, 1.0e-6) && limited.z == 0.0F,
         "the 4.64 radial offset limit changed the direction");
  expect(rac_gameplay_camera_limit_offset_v1({1.0F, 2.0F, 2.0F}) ==
             RacVector3fV1{1.0F, 2.0F, 2.0F},
         "a shorter standalone offset was changed");
  const float pitch_limit = source_float(0x3f9c61aaU);
  for (const float pitch : {-2.0F, 2.0F}) {
    auto initial = rac_gameplay_camera_initial_state_v1({});
    initial.look_pitch = pitch;
    initial.desired_offset = {-30.0F, 0.0F, 0.0F};
    RacGameplayCameraV1 camera(rac_gameplay_camera_profile_v1(), initial);
    camera.step_pal_frame({});
    const auto offset = camera.state().desired_offset;
    expect(near(std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z),
                limit, 1.0e-6), "2ea3f0 did not restore the limited desired offset");
    expect(camera.state().view_pitch_difference == (pitch < 0.0F ? pitch_limit : -pitch_limit),
           "2ea4c0 did not clamp its composed view pitch to +/-70 degrees");
    const auto view = camera.view();
    const auto forward_z = view.target.z - view.eye.z;
    expect(near(std::abs(forward_z), std::sin(pitch_limit), 1.0e-6),
           "the clamped pitch was not applied to the renderer view");
  }
  expect(rac_gameplay_camera_probe_height_v1(0U) == source_float(0x3f000000U) &&
             rac_gameplay_camera_probe_height_v1(1U) == source_float(0x3ecccccdU) &&
             rac_gameplay_camera_probe_height_v1(2U) == source_float(0x40400000U) &&
             rac_gameplay_camera_probe_height_v1(255U) == source_float(0x3f000000U),
         "2e6fb0 byte selection has the wrong source height bits");
}

void test_original_angular_spring_boundary() {
  using namespace openrc::game;
  const float pi = source_float(0x40490fdbU);
  float velocity = 0.0F;
  auto result = rac_camera_angular_spring_step_v1(pi - 0.01F, -pi + 0.01F,
                                                 velocity, {1.0F, 1.0F, 0.0F});
  expect(near(result, -pi + 0.01F, 1.0e-6) && near(velocity, 0.02F, 1.0e-6),
         "1eb6a8 took the long arc over +pi");
  velocity = 0.0F;
  result = rac_camera_angular_spring_step_v1(-pi + 0.01F, pi - 0.01F,
                                             velocity, {1.0F, 1.0F, 0.0F});
  expect(near(result, pi - 0.01F, 1.0e-6) && near(velocity, -0.02F, 1.0e-6),
         "1eb6a8 took the long arc over -pi");
  velocity = 0.0F;
  expect(rac_camera_angular_spring_step_v1(0.0F, pi, velocity, {1.0F, 1.0F, 0.0F}) == -pi &&
             velocity == -pi,
         "source equality at +pi did not map to -pi");
  velocity = 0.0F;
  expect(rac_camera_angular_spring_step_v1(-pi, -pi, velocity, {1.0F, 1.0F, 0.0F}) == -pi,
         "source equality at -pi was wrapped unnecessarily");
  velocity = 0.0F;
  result = rac_camera_angular_spring_step_v1(pi - 0.01F, -pi + 0.01F,
      velocity, {source_float(0x3c75c28fU), source_float(0x3e4ccccdU), 0.0F});
  expect(near(velocity, 0.0003, 1.0e-7) && result < pi && result > pi - 0.01F,
         "the original angular spring did not preserve the small wrapped delta");
}

void test_veldin_entry_and_deterministic_eye_sequence() {
  using namespace openrc::game;
  // gameplay-705.bin SHA256 f085b471...c6422, only class 0 at record 0.
  // Conditional no-hit entry: 205278 and 2e5770 probe results are not claimed.
  const RacVector3fV1 spawn{source_float(0x4304170aU), source_float(0x42e6f5c3U),
                           source_float(0x41fb70a4U)};
  const float yaw = source_float(0x3f29a6ceU);
  const auto initial = rac_gameplay_camera_initial_state_v1(spawn, yaw);
  const auto entered = rac_gameplay_camera_level_enter_state_v1(spawn, yaw);
  expect(initial.focus == spawn && initial.focus_velocity == RacVector3fV1{} &&
             initial.eye_height == 2.0F && initial.look_height == 1.5F &&
             entered.focus == spawn && entered.eye_initialized &&
             !entered.collision_modeled && !entered.entry_probes_modeled && !entered.ee_bit_exact,
         "conditional Veldin entry lost source initialization or qualification flags");
  // Geometric oracle from the placed transform, independent of the springs:
  // eye = P - 4.64*(cos(yaw),sin(yaw),0) + 2*up. Source asin polynomial
  // residual introduces a small first-update offset in the host model.
  expect(near(entered.eye.x, 128.4320702, 5.0e-4) &&
             near(entered.eye.y, 112.6250287, 5.0e-4) &&
             near(entered.eye.z, 33.4300003, 5.0e-4),
         "the first camera frame was not seeded behind the actual Veldin yaw");
  RacGameplayCameraV1 manual(rac_gameplay_camera_profile_v1(), initial);
  manual.step_pal_frame(spawn);
  expect(manual.state() == entered, "level enter did not perform exactly one update");
  // 2ea6e0..2ea750: V = horizontal look + up*(dot(+320,up) + (+40 - +36)),
  // so the first view looks down at the 1.5 reference from 2.0 above the
  // focus: forward.z = -0.5/sqrt(4.64^2 + 0.5^2) up to the asin residual.
  const auto entered_view = manual.view();
  expect(near(entered_view.target.z - entered_view.eye.z,
              -0.5 / std::sqrt(4.64 * 4.64 + 0.25), 3.0e-4),
         "2ea4c0 did not aim at the 1.5 look reference");
  expect(entered.offset_shortfall >= 0.0F && entered.offset_shortfall < 1.0e-5F &&
             near(std::sqrt(entered.desired_offset.x * entered.desired_offset.x +
                            entered.desired_offset.y * entered.desired_offset.y +
                            entered.desired_offset.z * entered.desired_offset.z),
                  4.64, 1.0e-5),
         "2ea3f0 did not keep the entry offset at the 4.64 target");
  expect(rac_gameplay_camera_initial_state_v1(spawn, -yaw).eye.y > spawn.y,
         "entry ignored the player's yaw sign");
  RacGameplayCameraV1 first(rac_gameplay_camera_profile_v1(), entered);
  RacGameplayCameraV1 second(rac_gameplay_camera_profile_v1(), entered);
  for (int frame = 0; frame < 1000; ++frame) {
    RacGameplayCameraInputV1 input;
    input.right_x = frame % 120 < 60 ? 0.6F : -0.6F;
    input.right_y = frame % 160 < 80 ? -0.9F : 0.9F;
    input.yaw_selector = static_cast<std::uint32_t>(frame % 3);
    const RacVector3fV1 player{spawn.x + static_cast<float>(frame) * 0.01F,
                               spawn.y, spawn.z};
    first.step_pal_frame(player, input);
    second.step_pal_frame(player, input);
    expect(first.state() == second.state() && first.view() == second.view(),
           "host camera replay differs for identical PAL input");
    const auto view = first.view();
    expect(finite(view.eye) && finite(view.target) && finite(view.up),
           "camera replay produced a non-finite renderer view");
  }
  const auto before = first.state();
  RacGameplayCameraInputV1 unsupported;
  unsupported.special_override_active = true;
  expect_camera_error([&] { first.step_pal_frame(spawn, unsupported); },
                       "a special-state camera override was silently modeled");
  unsupported = {};
  unsupported.right_x = std::numeric_limits<float>::quiet_NaN();
  expect_camera_error([&] { first.step_pal_frame(spawn, unsupported); },
                       "a non-finite source pad axis was accepted");
  expect(first.state() == before, "a rejected original camera frame changed state");
}

void test_original_moving_distance_and_previous_pivot() {
  using namespace openrc::game;
  const auto profile = rac_gameplay_camera_profile_v1();
  const float limit = source_float(0x40947ae1U);
  RacGameplayCameraV1 lateral(profile, rac_gameplay_camera_initial_state_v1({}));
  lateral.step_pal_frame({0.0F, 0.1F, 0.0F});
  expect(lateral.state().desired_offset.y < 0.0F &&
             !lateral.state().movement_distance_active,
         "2ea3f0 lost previous-eye minus current-pivot translation");
  RacGameplayCameraV1 backward(profile, rac_gameplay_camera_initial_state_v1({}));
  backward.step_pal_frame({-0.14F, 0.0F, 0.0F});
  const auto first = backward.state();
  expect(first.movement_distance_active && first.movement_distance_maximum == 6.0F &&
             near(first.target_distance, limit + (6.0F-limit)*source_float(0x3b449ba6U), 1.0e-6) &&
             near(std::sqrt(first.smoothed_offset.x*first.smoothed_offset.x +
                            first.smoothed_offset.y*first.smoothed_offset.y +
                            first.smoothed_offset.z*first.smoothed_offset.z), limit, 1.0e-6),
         "2ea9c8 did not defer its moving-player radius target until the next update");
  backward.step_pal_frame({-0.28F, 0.0F, 0.0F});
  const auto second = backward.state();
  expect(second.target_distance > first.target_distance &&
             near(second.eye_distance_velocity,
                  (first.target_distance-limit)*source_float(0x3d23d70aU), 1.0e-6),
         "backward motion did not interpolate radial stiffness to 0.04");
  RacGameplayCameraInputV1 input;
  input.player_yaw_radians = source_float(0x40490fdbU);
  backward.step_pal_frame({-0.28F, 0.0F, 0.0F}, input);
  expect(backward.state().movement_distance_active &&
             backward.state().movement_distance_maximum == 6.0F,
         "stationary 2ea9c8 continuation ignored player Moby forward");
  input.player_yaw_radians = 0.0F;
  backward.step_pal_frame({-0.28F, 0.0F, 0.0F}, input);
  expect(!backward.state().movement_distance_active &&
             backward.state().movement_distance_maximum == limit,
         "2ea9c8 did not clear its remembered distance when the guard failed");
}

void test_original_pole_correction_precedes_eye() {
  using namespace openrc::game;
  auto initial = rac_gameplay_camera_initial_state_v1({});
  // +320 12.5 degrees from up: the elevation spring alone stays inside the
  // 15-degree cone, so 2eae68..2eaee8 must rotate it before 2eaef8.
  initial.smoothed_offset = {-1.0F, 0.0F, 4.5F};
  RacGameplayCameraV1 camera(rac_gameplay_camera_profile_v1(), initial);
  camera.step_pal_frame({});
  const auto &state = camera.state();
  const auto offset = state.smoothed_offset;
  const double offset_length =
      std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
  const double from_up = std::acos(offset.z / offset_length);
  expect(near(from_up, source_float(0x3e860a92U), 2.0e-4) &&
             state.eye_elevation_velocity == 0.0F &&
             state.eye_distance_velocity == 0.0F &&
             state.eye_azimuth_velocity == 0.0F,
         "the 15-degree pole correction did not rotate +320 and clear velocities");
  const RacVector3fV1 anchor{state.focus.x + 0.0F, state.focus.y + 0.0F,
                             2.0F + state.focus.z};
  expect(state.eye == RacVector3fV1{anchor.x + offset.x, anchor.y + offset.y,
                                    anchor.z + offset.z},
         "the eye was not composed from the pole-corrected +320");
}

void test_original_renderer_adapter_matrices() {
  using namespace openrc::game;
  const auto pal = rac_gameplay_pal_projection_defaults_v1();
  expect(pal.viewport_half_width == 256.0F && pal.viewport_half_height == 224.0F &&
             rac_projection_selector_v1(0U) == RacProjectionSelectorV1::other &&
             rac_projection_selector_v1(1U) == RacProjectionSelectorV1::pal &&
             rac_projection_selector_v1(7U) == RacProjectionSelectorV1::pal,
         "the runtime display/selector adapter has the wrong source domain");
  RacGameplayCameraV1 camera(rac_gameplay_camera_profile_v1(),
                             rac_gameplay_camera_level_enter_state_v1({10.0F, 20.0F, 30.0F}, 0.0F));
  const auto view = camera.view();
  expect(view.near_plane_distance == 0.03125 && view.far_plane_distance == 728.0 &&
             near(std::tan(view.vertical_field_of_view_radians / 2.0),
                  static_cast<double>(pal.horizontal_tangent * source_float(0x3f418937U)), 1.0e-12),
         "the renderer adapter lost source world scale or tangent FOV");
  expect(near(std::tan(view.vertical_field_of_view_radians / 2.0) * view.aspect_ratio,
              pal.horizontal_tangent, 1.0e-12) && view.aspect_ratio != 512.0 / 448.0,
         "the renderer adapter incorrectly substituted the PAL pixel aspect");
  using Vec = std::array<double, 3>;
  const auto dot = [](Vec a, Vec b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; };
  const auto normalized = [&](Vec a) {
    const double norm = std::sqrt(dot(a, a));
    return Vec{a[0]/norm, a[1]/norm, a[2]/norm};
  };
  const Vec eye{view.eye.x, view.eye.y, view.eye.z};
  const auto forward = normalized({view.target.x-view.eye.x, view.target.y-view.eye.y,
                                   view.target.z-view.eye.z});
  const Vec up{view.up.x, view.up.y, view.up.z};
  const auto right = normalized({forward[1]*up[2]-forward[2]*up[1],
                                 forward[2]*up[0]-forward[0]*up[2],
                                 forward[0]*up[1]-forward[1]*up[0]});
  expect(near(right[0], 0.0, 1.0e-6) && near(right[1], -1.0, 1.0e-6) &&
             near(dot(forward, up), 0.0, 1.0e-6),
         "the yaw-zero neutral view basis has the wrong handedness/up");
  // Column-vector matrices equivalent to the renderer's dot-product path.
  const std::array<Vec, 3> view_rows{right, up, forward};
  const double n = view.near_plane_distance, f = view.far_plane_distance;
  const double sx = 1.0 / (std::tan(view.vertical_field_of_view_radians/2.0)*view.aspect_ratio);
  const double sy = 1.0 / std::tan(view.vertical_field_of_view_radians/2.0);
  const std::array<std::array<double, 4>, 4> projection{{
      {sx,0.0,0.0,0.0}, {0.0,sy,0.0,0.0},
      {0.0,0.0,f/(f-n),-n*f/(f-n)}, {0.0,0.0,1.0,0.0}}};
  for (const double distance : {n, f}) {
    Vec point{eye[0]+forward[0]*distance, eye[1]+forward[1]*distance,
              eye[2]+forward[2]*distance};
    std::array<double,4> camera_point{0.0,0.0,0.0,1.0};
    for (std::size_t i=0; i<3; ++i) {
      camera_point[i] = dot(view_rows[i],point)-dot(view_rows[i],eye);
    }
    std::array<double,4> clip{};
    for (std::size_t row=0; row<4; ++row) {
      for (std::size_t col=0; col<4; ++col) {
        clip[row] += projection[row][col]*camera_point[col];
      }
    }
    expect(near(clip[0]/clip[3],0.0,1.0e-5) && near(clip[1]/clip[3],0.0,1.0e-5) &&
               near(clip[2]/clip[3],distance==n ? 0.0 : 1.0,1.0e-9),
           "the renderer view/projection does not map neutral near/far to D3D depth");
  }
  auto invalid = camera.state();
  invalid.eye_initialized = false;
  expect_camera_error([&] { static_cast<void>(rac_gameplay_camera_renderer_view_v1(
      invalid,pal,1024.0F)); }, "a focus-only state produced a renderer view");
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
    test_original_yaw_selectors_and_inversions();
    test_original_pitch_smoothing_and_feedback();
    test_original_offset_and_view_pitch_limits();
    test_original_angular_spring_boundary();
    test_veldin_entry_and_deterministic_eye_sequence();
    test_original_moving_distance_and_previous_pivot();
    test_original_pole_correction_precedes_eye();
    test_original_renderer_adapter_matrices();
    std::cout << "third_person_camera_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "third_person_camera_tests: " << error.what() << '\n';
    return 1;
  }
}
