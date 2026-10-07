#include "openrc/game_input.hpp"
#include "openrc/rac_pad_input.hpp"
#include "openrc/rac_player_locomotion.hpp"

#include <bit>
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

[[nodiscard]] std::uint32_t bits(const float value) {
  return std::bit_cast<std::uint32_t>(value);
}

// Expected bit patterns below were evaluated independently from the
// disassembly in binary32 (local/forensics/player-ground/expected_sequences.py).

void test_recovered_ground_transition_thresholds() {
  using namespace openrc::game;

  expect(!rac_player_idle_requests_ground_move_v1(
             kRacPlayerGroundMoveEnterMagnitudeV1) &&
             rac_player_idle_requests_ground_move_v1(
                 std::nextafter(kRacPlayerGroundMoveEnterMagnitudeV1, 1.0F)),
         "state 0 did not require a strictly greater previous magnitude");
  expect(!rac_player_ground_move_opens_stop_path_v1(
             kRacPlayerGroundStopMagnitudeV1) &&
             rac_player_ground_move_opens_stop_path_v1(
                 std::nextafter(kRacPlayerGroundStopMagnitudeV1, 0.0F)),
         "state 2 did not require a strictly smaller previous magnitude");

  const RacPadDirectionBitsV1 digital{true, true, false, false};
  expect(gate_rac_player_stick_sample_v1(0.25F, 0.0F, digital) ==
                 RacPlayerStickSampleV1{0.25F, 0.0F} &&
             gate_rac_player_stick_sample_v1(
                 std::nextafter(0.25F, 0.0F), 0.0F, digital) ==
                 RacPlayerStickSampleV1{1.0F, -1.0F} &&
             gate_rac_player_stick_sample_v1(0.1F, 0.1F, {}) ==
                 RacPlayerStickSampleV1{},
         "the 0.25 analog/digital source gate is wrong");
  expect(rac_player_previous_stick_magnitude_v1({1.0F, 1.0F}) == 1.0F &&
             rac_player_previous_stick_magnitude_v1({0.0F, 0.6F}) == 0.6F,
         "the previous-sample magnitude was not clamped to one");

  // +0x229c is measured before the new sample replaces +0x1d20, so every
  // transition sees the stick one PAL frame late.
  const RacPlayerFrameTimingV1 timing;
  RacPlayerGroundFrameStateV1 state;
  const RacPlayerGroundFrameInputV1 pushed{{1.0F, 0.0F}, 0.5F};
  const RacPlayerGroundFrameInputV1 released{{0.0F, 0.0F}, 0.5F};
  const auto first = step_rac_player_ground_frame_v1(state, pushed, timing);
  expect(first.previous_magnitude == 0.0F &&
             !first.idle_requests_ground_move &&
             first.state.stored_sample == pushed.sample &&
             first.state.actual_pace == 0.0F && first.target_pace == 0.0F,
         "state 0 reacted to the current sample instead of the previous one");
  const auto second =
      step_rac_player_ground_frame_v1(first.state, pushed, timing);
  expect(second.previous_magnitude == 1.0F &&
             second.idle_requests_ground_move &&
             !second.ground_move_opens_stop_path,
         "state 0 did not request state 2 one frame after the push");

  state = enter_rac_player_ground_move_v1(second.state, 0.5F, timing);
  expect(state.state == RacPlayerGroundStateV1::ground_move &&
             state.substate == 0U && bits(state.actual_pace) == 0x3e0f5c2aU &&
             enter_rac_player_ground_move_v1(second.state, 0.1F, timing)
                     .actual_pace == 0.1F,
         "state-2 entry did not limit the measured pace to 7 * dt");

  const auto moving = step_rac_player_ground_frame_v1(state, pushed, timing);
  expect(moving.previous_magnitude == 1.0F &&
             bits(moving.target_pace) == 0x3de978d6U &&
             bits(moving.state.actual_pace) == 0x3e0be0e0U &&
             bits(moving.state.turn.facing) == 0x3c602214U &&
             bits(moving.state.turn.angular_velocity) == 0x3c602214U &&
             bits(moving.state.remaining_turn) == 0x3ef8feefU &&
             !moving.ground_move_opens_stop_path,
         "the first state-2 frame differs from the source arithmetic");
  const auto lagging =
      step_rac_player_ground_frame_v1(moving.state, released, timing);
  expect(lagging.previous_magnitude == 1.0F && lagging.target_pace == 0.0F &&
             bits(lagging.state.actual_pace) == 0x3e086596U &&
             lagging.state.turn.facing == moving.state.turn.facing &&
             lagging.state.turn.angular_velocity == 0.0F &&
             lagging.state.remaining_turn == 0.0F &&
             !lagging.ground_move_opens_stop_path,
         "state 2 opened its stop path on the release frame itself");
  const auto stopping =
      step_rac_player_ground_frame_v1(lagging.state, released, timing);
  expect(stopping.previous_magnitude == 0.0F &&
             bits(stopping.state.actual_pace) == 0x3e04ea4cU &&
             stopping.ground_move_opens_stop_path &&
             stopping.state.state == RacPlayerGroundStateV1::ground_move,
         "state 2 did not open its stop path one frame after the release");
}

void test_recovered_ground_pace_smoothing_sequence() {
  using namespace openrc::game;

  const RacPlayerFrameTimingV1 timing;
  expect(bits(timing.frame_step) == 0x3ca3d70bU &&
             bits(timing.frame_step_squared) == 0x39d1b718U &&
             bits(timing.time_scale_squared) == 0x3fb851ecU,
         "the PAL frame-time block is not the 0x2623d0 50 Hz branch");
  const auto fast =
      rac_player_standard_target_pace_v1({1.0F, 0.0F}, timing);
  const auto slow =
      rac_player_standard_target_pace_v1({0.5F, 0.0F}, timing);
  expect(bits(fast) == 0x3de978d6U && bits(slow) == 0x3c9374bdU &&
             rac_player_standard_target_pace_v1({}, timing) == 0.0F,
         "the per-frame target pace is not coefficient * dt");

  // Acceleration 7.5 * dt^2 per frame; the last frame adds target - pace.
  const std::uint32_t accelerating[] = {
      0x3b449ba6U, 0x3bc49ba6U, 0x3c1374bcU};
  auto pace = 0.0F;
  auto frames = 0;
  while (pace != fast && frames < 100) {
    pace = step_rac_player_ground_pace_v1(pace, fast, 1.0F, timing);
    ++frames;
    if (frames <= 3) {
      expect(bits(pace) == accelerating[frames - 1],
             "an early acceleration frame differs from the source");
    }
    if (frames == 37) {
      expect(bits(pace) == 0x3de353f4U, "acceleration frame 37 differs");
    }
    if (frames == 38) {
      expect(bits(pace) == 0x3de978d1U, "acceleration frame 38 differs");
    }
  }
  expect(frames == 39 && bits(pace) == 0x3de978d6U,
         "acceleration did not converge on frame 39");

  // Deceleration 8.5 * dt^2 per frame.
  frames = 0;
  while (pace != 0.0F && frames < 100) {
    pace = step_rac_player_ground_pace_v1(pace, 0.0F, 1.0F, timing);
    ++frames;
    if (frames == 1) {
      expect(bits(pace) == 0x3de28242U, "deceleration frame 1 differs");
    }
    if (frames == 33) {
      expect(bits(pace) == 0x3aebeec8U, "deceleration frame 33 differs");
    }
  }
  expect(frames == 34 && bits(pace) == 0U,
         "deceleration did not reach zero on frame 34");

  // Idle decay 12.6 * dt^2 per frame.
  pace = fast;
  frames = 0;
  while (pace != 0.0F && frames < 100) {
    pace = step_rac_player_idle_pace_v1(pace, timing);
    ++frames;
    if (frames == 1) {
      expect(bits(pace) == 0x3ddf266dU, "idle decay frame 1 differs");
    }
    if (frames == 22) {
      expect(bits(pace) == 0x3b4c794cU, "idle decay frame 22 differs");
    }
  }
  expect(frames == 23 && bits(pace) == 0U,
         "idle decay did not reach zero on frame 23");

  // The acceleration gate compares the signed previous remaining turn.
  expect(rac_player_ground_acceleration_scale_v1(0U, 0.5F, 0.6F) == 0.0F &&
             rac_player_ground_acceleration_scale_v1(0U, 0.5F, -0.6F) ==
                 1.0F &&
             rac_player_ground_acceleration_scale_v1(
                 0U, 0.5F, kRacPlayerAccelerationTurnAngleV1) == 1.0F &&
             rac_player_ground_acceleration_scale_v1(
                 0U, kRacPlayerAccelerationTurnMagnitudeV1, 0.6F) == 1.0F &&
             rac_player_ground_acceleration_scale_v1(1U, 0.5F, 0.6F) == 1.0F,
         "the turning acceleration gate is wrong");
  expect(step_rac_player_ground_pace_v1(0.01F, fast, 0.0F, timing) == 0.01F &&
             bits(step_rac_player_ground_pace_v1(0.05F, 0.0F, 0.0F,
                                                 timing)) ==
                 bits(0.05F - std::bit_cast<float>(0x3b5ed28aU)),
         "a suppressed acceleration also suppressed deceleration");

  auto value = 1.0F;
  expect(approach_rac_value_v1(value, 1.25F, 0.25F) == 0.0F &&
             value == 1.25F,
         "an exact approach step did not reach its target");
}

void test_recovered_ground_turn_sequence() {
  using namespace openrc::game;

  const RacPlayerFrameTimingV1 timing;
  const auto full = rac_player_ground_turn_parameters_v1(1U, 1.0F, timing);
  const auto fast = rac_player_ground_turn_parameters_v1(0U, 1.0F, timing);
  const auto slow = rac_player_ground_turn_parameters_v1(0U, 0.5F, timing);
  const auto edge = rac_player_ground_turn_parameters_v1(
      0U, kRacPlayerStandardFastPaceThresholdV1, timing);
  expect(bits(full.acceleration) == 0x3c3cbe63U &&
             bits(full.damping) == 0x3e5d2f1cU &&
             bits(full.maximum_rate) == 0x3e4bbe26U,
         "the substate-1 turn coefficients differ from the source");
  expect(bits(fast.acceleration) == 0x3ce02214U &&
             bits(fast.damping) == 0x3e1374bdU &&
             bits(fast.maximum_rate) == 0x3e4bbe26U,
         "the fast-stick turn coefficients differ from the source");
  expect(bits(slow.acceleration) == 0x3bc88a49U &&
             bits(slow.damping) == 0x3e1374bdU &&
             bits(slow.maximum_rate) == 0x3da4110aU &&
             bits(edge.acceleration) == 0x3c0a04d1U &&
             bits(edge.maximum_rate) == 0x3de1d531U,
         "the slow-stick turn coefficients or the strict 0.82 split differ");

  expect(rac_wrap_angle_sum_v1(kRacPiV1, 0.0F) == -kRacPiV1 &&
             rac_wrap_angle_difference_v1(-3.0F, 3.0F) ==
                 (-6.0F + kRacPiV1) + kRacPiV1 &&
             rac_wrap_angle_sum_v1(-3.0F, -0.5F) ==
                 (-3.5F + kRacPiV1) + kRacPiV1,
         "the single source angle wrap is wrong");

  struct TurnFrame {
    int frame;
    std::uint32_t facing;
    std::uint32_t angular_velocity;
    std::uint32_t remaining;
  };
  const TurnFrame quarter_turn[] = {
      {1, 0x3d3008a3U, 0x3d3008a3U, 0x3fc38f96U},
      {2, 0x3df8f7d2U, 0x3da0f380U, 0x3fb9805eU},
      {5, 0x3effde35U, 0x3e126ea8U, 0x3f89184eU},
      {12, 0x3fbede9bU, 0x3debc327U, 0x3da31400U},
      {13, 0x3fc90fdbU, 0U, 0U},
  };
  RacPlayerTurnStateV1 turn;
  const auto quarter = std::bit_cast<float>(0x3fc90fdbU);
  auto checked = 0U;
  for (auto frame = 1; frame <= 13; ++frame) {
    const auto remaining = step_rac_player_turn_v1(turn, quarter, fast);
    for (const auto &expected : quarter_turn) {
      if (expected.frame == frame) {
        expect(bits(turn.facing) == expected.facing &&
                   bits(turn.angular_velocity) == expected.angular_velocity &&
                   bits(remaining) == expected.remaining,
               "quarter-turn frame " + std::to_string(frame) +
                   " differs from the source arithmetic");
        ++checked;
      }
    }
  }
  expect(checked == 5U, "the quarter-turn sequence was not fully checked");

  // The shortest path from 3.0 to -3.0 crosses +pi and wraps the facing.
  turn = {3.0F, 0.0F};
  for (auto frame = 1; frame <= 7; ++frame) {
    const auto remaining = step_rac_player_turn_v1(turn, -3.0F, fast);
    if (frame == 6) {
      expect(bits(turn.facing) == 0x40478438U &&
                 bits(turn.angular_velocity) == 0x3ce00f07U &&
                 bits(remaining) == 0x3e29b7e0U,
             "the turn before crossing pi differs from the source");
    }
    if (frame == 7) {
      expect(bits(turn.facing) == 0xc048d19cU &&
                 bits(turn.angular_velocity) == 0x3ce4f133U &&
                 bits(remaining) == 0x3e0d19c0U,
             "the turn did not wrap across pi like the source");
    }
  }

  // The rate is clamped first to the maximum, then to the remaining angle;
  // a zero maximum disables both its clamp and the final snap.
  turn = {};
  const auto clamped = step_rac_player_turn_v1(turn, 1.0F, {1.0F, 0.0F, 0.1F});
  expect(turn.facing == 0.1F && turn.angular_velocity == 0.1F &&
             clamped == 1.0F - 0.1F,
         "the maximum turn rate clamp is wrong");
  turn = {};
  const auto exact = step_rac_player_turn_v1(turn, 0.5F, {2.0F, 0.0F, 0.0F});
  expect(turn.facing == 0.5F && turn.angular_velocity == 0.5F && exact == 0.0F,
         "the remaining-angle clamp or the zero-rate snap rule is wrong");
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
    test_recovered_ground_transition_thresholds();
    test_recovered_ground_pace_smoothing_sequence();
    test_recovered_ground_turn_sequence();
    std::cout << "game_input_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "game_input_tests: " << error.what() << '\n';
    return 1;
  }
}
