#include "windows_gamepad.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>

#include <array>
#include <cstdint>
#include <utility>

namespace openrc::runtime {
namespace {

using XInputGetStateFunction = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);

[[nodiscard]] game::GameInputSampleV1
make_sample(const XINPUT_GAMEPAD &gamepad) noexcept {
  game::GameInputSampleV1 result;
  result.axes.move_x = game::canonical_game_input_axis_v1(
      static_cast<std::int32_t>(gamepad.sThumbLX));
  result.axes.move_y = game::canonical_game_input_axis_v1(
      static_cast<std::int32_t>(gamepad.sThumbLY));
  result.axes.look_x = game::canonical_game_input_axis_v1(
      static_cast<std::int32_t>(gamepad.sThumbRX));
  result.axes.look_y = game::canonical_game_input_axis_v1(
      static_cast<std::int32_t>(gamepad.sThumbRY));

  // Cross/A and Square/X are the two controls already represented by the
  // current gameplay boundary. Remaining source-game button meanings will
  // be connected only after their PS2 dispatch has been recovered.
  if ((gamepad.wButtons & XINPUT_GAMEPAD_A) != 0U) {
    result.held_buttons |= game::game_button_mask_v1(game::GameButtonV1::jump);
  }
  if ((gamepad.wButtons & XINPUT_GAMEPAD_X) != 0U) {
    result.held_buttons |=
        game::game_button_mask_v1(game::GameButtonV1::primary_action);
  }
  if ((gamepad.wButtons & XINPUT_GAMEPAD_START) != 0U)
    result.held_buttons |= game::game_button_mask_v1(game::GameButtonV1::pause);
  if ((gamepad.wButtons & XINPUT_GAMEPAD_B) != 0U)
    result.held_buttons |= game::game_button_mask_v1(game::GameButtonV1::menu_back);
  return result;
}

} // namespace

class WindowsGamepadV1::Implementation final {
public:
  Implementation() noexcept {
    constexpr std::array module_names{
        L"xinput1_4.dll",
        L"xinput1_3.dll",
        L"xinput9_1_0.dll",
    };
    for (const auto *const module_name : module_names) {
      auto *const candidate =
          LoadLibraryExW(module_name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
      if (candidate == nullptr) {
        continue;
      }
      const auto function = reinterpret_cast<XInputGetStateFunction>(
          GetProcAddress(candidate, "XInputGetState"));
      if (function != nullptr) {
        module_ = candidate;
        get_state_ = function;
        break;
      }
      FreeLibrary(candidate);
    }
  }

  ~Implementation() {
    if (module_ != nullptr) {
      FreeLibrary(module_);
    }
  }

  Implementation(const Implementation &) = delete;
  Implementation &operator=(const Implementation &) = delete;

  [[nodiscard]] WindowsGamepadPollV1 poll() const noexcept {
    if (get_state_ == nullptr) {
      return {};
    }
    for (DWORD user_index = 0U; user_index < XUSER_MAX_COUNT; ++user_index) {
      XINPUT_STATE state{};
      if (get_state_(user_index, &state) == ERROR_SUCCESS) {
        return WindowsGamepadPollV1{
            make_sample(state.Gamepad),
            static_cast<std::uint32_t>(user_index),
            true,
        };
      }
    }
    return {};
  }

private:
  HMODULE module_ = nullptr;
  XInputGetStateFunction get_state_ = nullptr;
};

WindowsGamepadV1::WindowsGamepadV1()
    : implementation_(std::make_unique<Implementation>()) {}

WindowsGamepadV1::~WindowsGamepadV1() = default;
WindowsGamepadV1::WindowsGamepadV1(WindowsGamepadV1 &&) noexcept = default;
WindowsGamepadV1 &
WindowsGamepadV1::operator=(WindowsGamepadV1 &&) noexcept = default;

WindowsGamepadPollV1 WindowsGamepadV1::poll() const noexcept {
  return implementation_ == nullptr ? WindowsGamepadPollV1{}
                                    : implementation_->poll();
}

} // namespace openrc::runtime
