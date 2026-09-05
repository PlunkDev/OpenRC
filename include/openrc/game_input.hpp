#pragma once

#include <cstdint>
#include <stdexcept>

namespace openrc::game {

inline constexpr std::int16_t kGameInputAxisMagnitudeV1 = 32'767;

// Platform adapters quantize physical controls before they reach simulation.
// Recorded commands therefore contain no device-specific or wall-clock state.
struct GameInputAxesV1 {
  std::int16_t move_x = 0;
  std::int16_t move_y = 0;
  std::int16_t look_x = 0;
  std::int16_t look_y = 0;

  [[nodiscard]] bool operator==(const GameInputAxesV1 &) const = default;
};

enum class GameButtonV1 : std::uint8_t {
  jump = 0U,
  primary_action,
  secondary_action,
  interact,
  crouch,
  pause,
  reset_checkpoint,
  menu_back,
  weapon_next,
  weapon_previous,
  count,
};

inline constexpr std::uint32_t kAllGameButtonsV1 =
    (1U << static_cast<std::uint8_t>(GameButtonV1::count)) - 1U;

[[nodiscard]] constexpr std::uint32_t
game_button_mask_v1(const GameButtonV1 button) noexcept {
  const auto index = static_cast<std::uint8_t>(button);
  return index < static_cast<std::uint8_t>(GameButtonV1::count) ? 1U << index
                                                                : 0U;
}

struct GameInputSampleV1 {
  GameInputAxesV1 axes;
  std::uint32_t held_buttons = 0U;

  [[nodiscard]] bool operator==(const GameInputSampleV1 &) const = default;
};

// This is the replay boundary. One command is consumed by exactly one fixed
// simulation tick. A press and release may both be set when a complete tap
// occurred between two ticks.
struct GameInputCommandV1 {
  std::uint64_t tick_index = 0U;
  GameInputAxesV1 axes;
  std::uint32_t held_buttons = 0U;
  std::uint32_t pressed_buttons = 0U;
  std::uint32_t released_buttons = 0U;

  [[nodiscard]] bool operator==(const GameInputCommandV1 &) const = default;
};

class GameInputError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void validate_game_input_sample_v1(const GameInputSampleV1 &sample);
void validate_game_input_command_v1(const GameInputCommandV1 &command);

// Collects asynchronous platform samples and emits a strictly ordered stream
// of deterministic, quantized commands. Button edges survive even when a full
// press/release occurs before the next simulation tick.
class GameInputStateV1 final {
public:
  explicit GameInputStateV1(std::uint64_t next_tick_index = 0U) noexcept;

  void submit_sample(const GameInputSampleV1 &sample);
  void set_axes(GameInputAxesV1 axes);
  void set_button(GameButtonV1 button, bool held);
  void release_all() noexcept;

  [[nodiscard]] GameInputCommandV1 consume_for_tick(std::uint64_t tick_index);

  void reset(std::uint64_t next_tick_index = 0U) noexcept;

  [[nodiscard]] const GameInputSampleV1 &current_sample() const noexcept;
  [[nodiscard]] std::uint64_t next_tick_index() const noexcept;

private:
  GameInputSampleV1 current_sample_;
  std::uint32_t pending_pressed_buttons_ = 0U;
  std::uint32_t pending_released_buttons_ = 0U;
  std::uint64_t next_tick_index_ = 0U;
};

} // namespace openrc::game
