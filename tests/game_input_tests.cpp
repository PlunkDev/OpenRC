#include "openrc/game_input.hpp"
#include "openrc/rac_pad_input.hpp"
#include "openrc/rac_player_locomotion.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_input_error(Callback &&callback, const std::string &message) {
  try {
    callback();
  } catch (const openrc::game::GameInputError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_quantized_state_and_edges() {
  using namespace openrc::game;

  GameInputStateV1 input(40U);
  input.set_axes({1234, -2345, 3000, -4000});
  input.set_button(GameButtonV1::jump, true);
  input.set_button(GameButtonV1::primary_action, true);

  const auto first = input.consume_for_tick(40U);
  const auto jump = game_button_mask_v1(GameButtonV1::jump);
  const auto primary = game_button_mask_v1(GameButtonV1::primary_action);
  expect(first.tick_index == 40U &&
             first.axes == GameInputAxesV1{1234, -2345, 3000, -4000} &&
             first.held_buttons == (jump | primary) &&
             first.pressed_buttons == (jump | primary) &&
             first.released_buttons == 0U,
         "the first quantized input command is wrong");

  const auto held = input.consume_for_tick(41U);
  expect(held.held_buttons == (jump | primary) && held.pressed_buttons == 0U &&
             held.released_buttons == 0U,
         "held buttons emitted duplicate edges");

  input.set_button(GameButtonV1::jump, false);
  const auto released = input.consume_for_tick(42U);
  expect(released.held_buttons == primary && released.released_buttons == jump,
         "a released button was not retained until the next tick");
}

void test_complete_tap_between_ticks() {
  using namespace openrc::game;

  GameInputStateV1 input;
  input.set_button(GameButtonV1::interact, true);
  input.set_button(GameButtonV1::interact, false);
  const auto command = input.consume_for_tick(0U);
  const auto interact = game_button_mask_v1(GameButtonV1::interact);
  expect(command.held_buttons == 0U && command.pressed_buttons == interact &&
             command.released_buttons == interact,
         "a complete between-tick tap lost one of its edges");
}

void test_release_all_and_reset() {
  using namespace openrc::game;

  GameInputStateV1 input(7U);
  input.set_axes({1, 2, 3, 4});
  input.set_button(GameButtonV1::pause, true);
  static_cast<void>(input.consume_for_tick(7U));
  input.release_all();
  const auto released = input.consume_for_tick(8U);
  expect(released.axes == GameInputAxesV1{} && released.held_buttons == 0U &&
             released.released_buttons ==
                 game_button_mask_v1(GameButtonV1::pause),
         "release_all did not produce a neutral sample and release edge");

  input.set_button(GameButtonV1::jump, true);
  input.reset(100U);
  expect(input.current_sample() == GameInputSampleV1{} &&
             input.next_tick_index() == 100U,
         "reset did not clear input state or restore its tick sequence");
  expect(input.consume_for_tick(100U) ==
             GameInputCommandV1{100U, {}, 0U, 0U, 0U},
         "reset left a pending input edge behind");
}

void test_validation_and_tick_order() {
  using namespace openrc::game;

  GameInputStateV1 input(3U);
  expect_input_error(
      [&] { input.submit_sample({{}, kAllGameButtonsV1 | 0x80000000U}); },
      "an unknown input bit was accepted");
  expect_input_error(
      [&] {
        input.set_axes({std::numeric_limits<std::int16_t>::min(), 0, 0, 0});
      },
      "an asymmetric minimum input axis was accepted");
  expect_input_error([&] { input.set_button(GameButtonV1::count, true); },
                     "the button sentinel was accepted as a real button");
  expect_input_error([&] { static_cast<void>(input.consume_for_tick(4U)); },
                     "an out-of-order input tick was accepted");

  GameInputCommandV1 invalid;
  invalid.pressed_buttons = 0x80000000U;
  expect_input_error([&] { validate_game_input_command_v1(invalid); },
                     "an invalid replay command was accepted");
}

void test_platform_axis_canonicalization_preserves_analog_magnitude() {
  using namespace openrc::game;

  expect(canonical_game_input_axis_v1(0) == 0 &&
             canonical_game_input_axis_v1(1'234) == 1'234 &&
             canonical_game_input_axis_v1(-23'456) == -23'456,
         "platform axis canonicalization changed an in-range magnitude");
  expect(
      canonical_game_input_axis_v1(32'767) == 32'767 &&
          canonical_game_input_axis_v1(-32'768) == -32'767,
      "platform axis canonicalization did not make signed endpoints symmetric");
  expect(canonical_game_input_axis_v1(100'000) == 32'767 &&
             canonical_game_input_axis_v1(-100'000) == -32'767,
         "platform axis canonicalization did not clamp a wider source domain");
}

void test_recovered_rac_pad_byte_response() {
  using namespace openrc::game;

  expect(decode_rac_pad_axis_v1(127U) == 0.0F &&
             !std::signbit(decode_rac_pad_axis_v1(127U)) &&
             decode_rac_pad_axis_v1(175U) == 0.0F &&
             decode_rac_pad_axis_v1(128U) == 0.0F,
         "the recovered RAC pad center/dead-zone response is wrong");
  expect(decode_rac_pad_axis_v1(79U) == 0.0F &&
             std::signbit(decode_rac_pad_axis_v1(79U)),
         "the recovered RAC pad response lost its negative zero boundary");
  expect(decode_rac_pad_axis_v1(78U) == -1.0F / 76.0F &&
             decode_rac_pad_axis_v1(176U) == 1.0F / 76.0F,
         "the recovered RAC pad response has the wrong first nonzero step");
  expect(decode_rac_pad_axis_v1(0U) == -1.0F &&
             decode_rac_pad_axis_v1(255U) == 1.0F,
         "the recovered RAC pad response did not clamp its endpoints");
}

void test_signed_controller_bridge_and_four_axis_response() {
  using namespace openrc::game;

  expect(quantize_game_input_axis_to_rac_pad_v1(0) == 127U &&
             quantize_game_input_axis_to_rac_pad_v1(32'767) == 255U &&
             quantize_game_input_axis_to_rac_pad_v1(-32'767) == 0U,
         "the signed controller bridge missed the DualShock byte endpoints");
  const auto filtered =
      apply_rac_pad_axes_response_v1({12'000, 16'384, -32'767, 32'767});
  const auto exact =
      decode_rac_pad_axes_response_v1({12'000, 16'384, -32'767, 32'767});
  expect(filtered.move_x == 0 && filtered.move_y > 0 &&
             filtered.move_y < kGameInputAxisMagnitudeV1 &&
             filtered.look_x == -kGameInputAxisMagnitudeV1 &&
             filtered.look_y == kGameInputAxisMagnitudeV1,
         "the recovered RAC pad response did not preserve partial/full axes");
  expect(exact.move_x == 0.0F && exact.move_y > 0.0F &&
             exact.move_y < 1.0F && exact.look_x == -1.0F &&
             exact.look_y == 1.0F,
         "the floating RAC pad response lost source-domain magnitudes");
}

void test_recovered_standard_ground_pace_selection() {
  using namespace openrc::game;

  const auto stationary =
      map_rac_player_standard_ground_movement_v1(0, 0);
  expect(stationary == RacPlayerGroundMovementV1{},
         "neutral source input did not remain stationary");

  // The source comparison is strict: equality belongs to the fast side.
  const auto slow = map_rac_player_standard_ground_movement_v1(
      std::nextafter(kRacPlayerStandardFastPaceThresholdV1, 0.0F), 0.0F);
  const auto fast = map_rac_player_standard_ground_movement_v1(
      kRacPlayerStandardFastPaceThresholdV1, 0.0F);
  expect(slow.pace == RacPlayerGroundPaceV1::slow &&
             slow.target_ground_speed ==
                 kRacPlayerStandardSlowGroundSpeedV1 &&
             slow.source_input_magnitude <
                 kRacPlayerStandardFastPaceThresholdV1 &&
             slow.direction_x == 1.0 && slow.direction_y == 0.0,
         "a filtered stick magnitude below 0.82 did not select source slow "
         "movement");
  expect(fast.pace == RacPlayerGroundPaceV1::fast &&
             fast.target_ground_speed ==
                 kRacPlayerStandardFastGroundSpeedV1 &&
             fast.source_input_magnitude >=
                 kRacPlayerStandardFastPaceThresholdV1 &&
             fast.direction_x == 1.0 && fast.direction_y == 0.0,
         "the exact 0.82 magnitude did not select source fast movement");

  const auto diagonal = map_rac_player_standard_ground_movement_v1(
      1.0F, 1.0F);
  const auto diagonal_length =
      std::hypot(diagonal.direction_x, diagonal.direction_y);
  expect(diagonal.pace == RacPlayerGroundPaceV1::fast &&
             diagonal.source_input_magnitude == 1.0F &&
             std::abs(diagonal_length - 1.0) < 1.0e-6,
         "source fast movement did not retain diagonal direction in the "
         "unit circle");

  const auto rejected = map_rac_player_standard_ground_movement_v1(
      std::numeric_limits<float>::quiet_NaN(), 0.5F);
  expect(rejected == RacPlayerGroundMovementV1{},
         "a non-finite source movement vector did not fail closed");
}

} // namespace

int main() {
  try {
    test_quantized_state_and_edges();
    test_complete_tap_between_ticks();
    test_release_all_and_reset();
    test_validation_and_tick_order();
    test_platform_axis_canonicalization_preserves_analog_magnitude();
    test_recovered_rac_pad_byte_response();
    test_signed_controller_bridge_and_four_axis_response();
    test_recovered_standard_ground_pace_selection();
    std::cout << "game_input_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "game_input_tests: " << error.what() << '\n';
    return 1;
  }
}
