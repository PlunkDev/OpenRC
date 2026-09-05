#include "openrc/game_input.hpp"

#include <array>
#include <limits>

namespace openrc::game {
namespace {

void validate_button_bits(const std::uint32_t buttons) {
  if ((buttons & ~kAllGameButtonsV1) != 0U) {
    throw GameInputError("A game-input value contains unknown button bits");
  }
}

void validate_axes(const GameInputAxesV1 &axes) {
  const std::array values{axes.move_x, axes.move_y, axes.look_x, axes.look_y};
  for (const auto value : values) {
    if (value < -kGameInputAxisMagnitudeV1 ||
        value > kGameInputAxisMagnitudeV1) {
      throw GameInputError(
          "A game-input axis is outside the symmetric quantized domain");
    }
  }
}

} // namespace

void validate_game_input_sample_v1(const GameInputSampleV1 &sample) {
  validate_axes(sample.axes);
  validate_button_bits(sample.held_buttons);
}

void validate_game_input_command_v1(const GameInputCommandV1 &command) {
  validate_axes(command.axes);
  validate_button_bits(command.held_buttons);
  validate_button_bits(command.pressed_buttons);
  validate_button_bits(command.released_buttons);
}

GameInputStateV1::GameInputStateV1(const std::uint64_t next_tick_index) noexcept
    : next_tick_index_(next_tick_index) {}

void GameInputStateV1::submit_sample(const GameInputSampleV1 &sample) {
  validate_game_input_sample_v1(sample);
  const auto changed_buttons =
      current_sample_.held_buttons ^ sample.held_buttons;
  pending_pressed_buttons_ |= changed_buttons & sample.held_buttons;
  pending_released_buttons_ |= changed_buttons & current_sample_.held_buttons;
  current_sample_ = sample;
}

void GameInputStateV1::set_axes(const GameInputAxesV1 axes) {
  auto sample = current_sample_;
  sample.axes = axes;
  submit_sample(sample);
}

void GameInputStateV1::set_button(const GameButtonV1 button, const bool held) {
  const auto mask = game_button_mask_v1(button);
  if (mask == 0U) {
    throw GameInputError("A game-input button is outside the known domain");
  }

  auto sample = current_sample_;
  if (held) {
    sample.held_buttons |= mask;
  } else {
    sample.held_buttons &= ~mask;
  }
  submit_sample(sample);
}

void GameInputStateV1::release_all() noexcept {
  pending_released_buttons_ |= current_sample_.held_buttons;
  current_sample_ = {};
}

GameInputCommandV1
GameInputStateV1::consume_for_tick(const std::uint64_t tick_index) {
  if (tick_index != next_tick_index_) {
    throw GameInputError(
        "Game-input commands must be consumed in exact tick order");
  }
  if (next_tick_index_ == std::numeric_limits<std::uint64_t>::max()) {
    throw GameInputError("The game-input tick sequence is exhausted");
  }

  GameInputCommandV1 result{
      tick_index,
      current_sample_.axes,
      current_sample_.held_buttons,
      pending_pressed_buttons_,
      pending_released_buttons_,
  };
  validate_game_input_command_v1(result);
  pending_pressed_buttons_ = 0U;
  pending_released_buttons_ = 0U;
  ++next_tick_index_;
  return result;
}

void GameInputStateV1::reset(const std::uint64_t next_tick_index) noexcept {
  current_sample_ = {};
  pending_pressed_buttons_ = 0U;
  pending_released_buttons_ = 0U;
  next_tick_index_ = next_tick_index;
}

const GameInputSampleV1 &GameInputStateV1::current_sample() const noexcept {
  return current_sample_;
}

std::uint32_t GameInputStateV1::pending_pressed_buttons() const noexcept {
  return pending_pressed_buttons_;
}

std::uint32_t GameInputStateV1::pending_released_buttons() const noexcept {
  return pending_released_buttons_;
}

std::uint64_t GameInputStateV1::next_tick_index() const noexcept {
  return next_tick_index_;
}

} // namespace openrc::game
