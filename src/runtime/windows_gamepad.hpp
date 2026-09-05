#pragma once

#include "openrc/game_input.hpp"

#include <cstdint>
#include <memory>

namespace openrc::runtime {

struct WindowsGamepadPollV1 {
  game::GameInputSampleV1 sample;
  std::uint32_t user_index = 0U;
  bool connected = false;

  [[nodiscard]] bool operator==(const WindowsGamepadPollV1 &) const = default;
};

// Uses XInput through runtime symbol lookup so the executable does not gain a
// hard dependency on a particular xinput*.dll. Stick values cross this adapter
// without a guessed dead zone or response curve.
class WindowsGamepadV1 final {
public:
  WindowsGamepadV1();
  ~WindowsGamepadV1();

  WindowsGamepadV1(const WindowsGamepadV1 &) = delete;
  WindowsGamepadV1 &operator=(const WindowsGamepadV1 &) = delete;
  WindowsGamepadV1(WindowsGamepadV1 &&) noexcept;
  WindowsGamepadV1 &operator=(WindowsGamepadV1 &&) noexcept;

  [[nodiscard]] WindowsGamepadPollV1 poll() const noexcept;

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace openrc::runtime
