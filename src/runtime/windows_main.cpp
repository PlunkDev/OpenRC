#include "d3d11_renderer.hpp"
#include "windows_gamepad.hpp"
#include "windows_media_decoder.hpp"
#include "windows_media_audio.hpp"
#include "windows_audio_bank.hpp"
#include "windows_audio_program_bank.hpp"

#include "openrc/prepared_game_v2_fs.hpp"
#include "openrc/runtime_gameplay.hpp"
#include "openrc/runtime_level_content.hpp"
#include "openrc/runtime_player_actor.hpp"
#include "openrc/runtime_player_animation.hpp"
#include "openrc/runtime_world_actor.hpp"
#include "openrc/third_person_camera.hpp"
#include "openrc/scene_timeline.hpp"
#include "openrc/image_presentation.hpp"
#include "openrc/screen_overlay.hpp"
#include "openrc/actor_animation_player.hpp"
#include "openrc/frontend_menu.hpp"
#include "openrc/frontend_sequence.hpp"
#include "openrc/session_state_io.hpp"
#include "openrc/state_installation.hpp"
#include "openrc/loading_presentation.hpp"
#include "openrc/paths.hpp"
#include "openrc/audio_program_cues.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"PlunkDev.OpenRC.Runtime.Window";
constexpr wchar_t kApplicationName[] = L"OpenRC runtime";

struct RuntimeArguments {
    std::filesystem::path prepared_root;
    std::uint32_t level_id = 0U;
    bool smoke_test = false;
    bool explicit_level = false;
    std::filesystem::path smoke_capture;
    std::wstring smoke_stage=L"intro";
    bool show_help = false;
};

struct GameplayKeyboardState {
    bool move_left = false;
    bool move_right = false;
    bool move_forward = false;
    bool move_backward = false;
    bool look_left = false;
    bool look_right = false;
    bool look_up = false;
    bool look_down = false;
    bool jump = false;
    bool reset = false;
    bool primary_action_key = false;
    bool primary_action_pointer = false;
};

enum class GameplaySmokeTargetKindV1 {
    collectible,
    destructible,
};

struct GameplaySmokeTargetV1 {
    GameplaySmokeTargetKindV1 kind =
        GameplaySmokeTargetKindV1::collectible;
    std::uint32_t authored_id = 0U;
    std::uint32_t render_instance_id = 0U;
    openrc::CollisionVectorV1 world_target;
    std::string item_key;
    std::uint32_t amount = 0U;
};

class FrontendCardBackend;
class PreparedLevelLoad;

struct WindowState {
    // Level assets outlive every presentation object that may borrow rigs or
    // animation clips. Pending I/O remains a separate, single-use owner below.
    std::optional<openrc::game::RuntimeLevelContentV1> admitted_level;
    std::unique_ptr<openrc::runtime::D3d11Renderer> renderer;
    std::unique_ptr<openrc::game::RuntimeGameplaySessionV1> gameplay;
    std::unique_ptr<openrc::game::RuntimePlayerAnimationV1>
        gameplay_animation;
    std::optional<openrc::game::ThirdPersonCameraV1> gameplay_camera;
    GameplayKeyboardState gameplay_keyboard;
    openrc::runtime::WindowsGamepadV1 gameplay_gamepad;
    openrc::game::GameInputSampleV1 gameplay_gamepad_sample;
    std::uint32_t gameplay_pending_pressed_buttons = 0U;
    std::uint32_t gameplay_pending_released_buttons = 0U;
    std::uint32_t gameplay_submitted_buttons = 0U;
    std::vector<openrc::EntityRenderBindingV1> gameplay_render_bindings;
    std::vector<std::uint32_t> gameplay_collectible_ids;
    std::vector<std::uint32_t> gameplay_destructible_ids;
    std::vector<std::uint32_t> gameplay_world_actor_ids;
    std::uint64_t gameplay_collected_count = 0U;
    std::uint64_t gameplay_destroyed_count = 0U;
    std::optional<GameplaySmokeTargetV1> gameplay_smoke_target;
    std::chrono::steady_clock::time_point previous_gameplay_frame{};
    std::optional<std::string> fatal_error;
    std::wstring base_title;
    bool gameplay_actor = false;
    bool gameplay_input_active = true;
    bool frontend_input_active = false;
    std::uint32_t frontend_keys = 0U, frontend_pending_pressed = 0U;
    std::uint32_t frontend_previous_buttons = 0U;
    std::unique_ptr<openrc::game::GameSessionV1> frontend_session;
    bool frontend_requested_new_game = false;
    bool frontend_platform_active = false;
    bool frontend_intro_retired = false;
    std::uint32_t frontend_display_width=0,frontend_display_height=0;
    std::unique_ptr<openrc::runtime::WindowsAudioBankV1> frontend_menu_audio;
    std::unique_ptr<openrc::runtime::WindowsAudioProgramBankV1> frontend_ambient_audio;
    std::unique_ptr<FrontendCardBackend> frontend_cards;
    std::unique_ptr<PreparedLevelLoad> prepared_level;
};

bool update_frontend_key(WindowState& state,WPARAM key,bool held) {
    const std::uint32_t bit=key==VK_RETURN||key==VK_SPACE?0x40U:
        key==VK_ESCAPE?0x10U:key==VK_TAB?0x800U:0U;
    if(!bit)return false;
    if(held){state.frontend_pending_pressed|=bit&~state.frontend_keys;state.frontend_keys|=bit;}
    else state.frontend_keys&=~bit;
    return true;
}

constexpr std::uint64_t kMaximumRuntimeRenderScenePayloadBytes =
    UINT64_C(512) * 1024U * 1024U;
constexpr std::uint64_t kMaximumRuntimeLevelPackageBytes =
    UINT64_C(768) * 1024U * 1024U;
constexpr std::uint64_t kMaximumRuntimePreparedGameBytes =
    UINT64_C(16) * 1024U * 1024U * 1024U;

[[nodiscard]] constexpr openrc::LevelPackageV1Limits
make_runtime_level_package_limits() {
    return openrc::LevelPackageV1Limits{
        kMaximumRuntimeLevelPackageBytes,
        64U,
        32U,
        512U,
        1024U,
        kMaximumRuntimeRenderScenePayloadBytes,
        kMaximumRuntimeLevelPackageBytes - openrc::kLevelPackageHeaderBytesV1,
        64U,
    };
}

[[nodiscard]] constexpr openrc::PreparedGameV2FilesystemLimitsV1
make_runtime_prepared_game_limits() {
    return openrc::PreparedGameV2FilesystemLimitsV1{
        openrc::PreparedGameV2Limits{
            1024U * 1024U,
            256U,
            128U,
            1024U,
            kMaximumRuntimePreparedGameBytes,
            UINT64_C(64) * 1024U * 1024U,
        },
        make_runtime_level_package_limits(),
        kMaximumRuntimePreparedGameBytes,
    };
}

[[nodiscard]] constexpr openrc::game::RuntimeLevelContentLimitsV1
make_runtime_level_content_limits() {
    return openrc::game::make_runtime_level_content_limits_v1();
}

[[nodiscard]] openrc::game::RuntimePlayerAnimationProfileV1
make_runtime_player_animation_profile() {
    return openrc::game::RuntimePlayerAnimationProfileV1{
        "actors/ratchet/source-sequence/000",
        "actors/ratchet/source-sequence/003",
        "actors/ratchet/source-sequence/004",
        0.25,
        2.0,
        60U,
    };
}

[[nodiscard]] constexpr openrc::ActorAnimationPlaybackLimitsV1
make_runtime_actor_animation_playback_limits() {
    return openrc::ActorAnimationPlaybackLimitsV1{
        8U,
        8U,
        openrc::game::kRuntimePlayerActorMaximumJointsV1,
        1.0e-8,
        1'000'000.0F,
    };
}

void apply_runtime_world_actor_initial_poses(
    openrc::runtime::D3d11Renderer& renderer,
    const openrc::game::RuntimeLevelContentV1& content,
    const std::span<const openrc::game::RuntimeWorldActorResolutionV1>
        actors) {
    if (actors.empty()) {
        return;
    }
    if (!content.actor_library) {
        throw std::runtime_error(
            "World actor presentation requires the mounted actor library");
    }
    const auto& library = *content.actor_library;
    std::vector<std::optional<openrc::ActorPosePaletteV1>> model_poses(
        library.models.size());
    std::vector<bool> model_pose_resolved(library.models.size(), false);
    for (const auto& actor : actors) {
        if (actor.actor_model_index >= library.models.size() ||
            actor.actor_rig_index >= library.rigs.size()) {
            throw std::runtime_error(
                "A resolved world actor exceeds the mounted actor library");
        }
        auto& pose = model_poses[actor.actor_model_index];
        if (!model_pose_resolved[actor.actor_model_index]) {
            model_pose_resolved[actor.actor_model_index] = true;
            const auto* const clip =
                openrc::game::find_runtime_world_actor_initial_animation_v1(
                    content, actor);
            if (clip == nullptr) {
                continue;
            }
            const auto playback =
                openrc::start_actor_animation_playback_v1(*clip);
            const auto& rig = library.rigs[actor.actor_rig_index];
            pose = openrc::sample_actor_animation_pose_v1(
                *clip,
                playback,
                rig.semantic_key,
                rig.rig,
                make_runtime_actor_animation_playback_limits());
        }
        if (pose) {
            renderer.set_world_actor_pose(actor.authored_id, *pose);
        }
    }
}

[[nodiscard]] bool has_complete_runtime_player_animation(
    const openrc::ActorAnimationBankV1& bank,
    const openrc::game::RuntimePlayerAnimationProfileV1& profile) {
    const auto has_idle = openrc::find_actor_animation_clip_v1(
                              bank, profile.idle_clip_key) != nullptr;
    const auto has_walk = openrc::find_actor_animation_clip_v1(
                              bank, profile.walk_clip_key) != nullptr;
    const auto has_run = openrc::find_actor_animation_clip_v1(
                             bank, profile.run_clip_key) != nullptr;
    const auto present_count = static_cast<unsigned>(has_idle) +
                               static_cast<unsigned>(has_walk) +
                               static_cast<unsigned>(has_run);
    if (present_count == 0U) {
        return false;
    }
    if (present_count != 3U) {
        throw std::runtime_error(
            "The runtime player animation bank contains an incomplete "
            "locomotion set; idle, walk, and run must be provided together");
    }
    return true;
}

[[nodiscard]] std::wstring utf8_to_wide(const std::string_view value) {
    if (value.empty()) {
        return {};
    }
    if (value.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return L"An error message was too long to display.";
    }
    const auto required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return L"OpenRC encountered an error whose text could not be decoded.";
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required);
    return result;
}

[[nodiscard]] std::optional<std::uint64_t>
parse_unsigned_decimal(const std::wstring_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0U;
    for (const auto character : text) {
        if (character < L'0' || character > L'9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint64_t>(character - L'0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) /
                        10U) {
            return std::nullopt;
        }
        value = value * 10U + digit;
    }
    return value;
}

[[nodiscard]] RuntimeArguments parse_arguments() {
    int argument_count = 0;
    auto* const raw_arguments =
        CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (raw_arguments == nullptr) {
        throw std::runtime_error("CommandLineToArgvW failed");
    }
    struct ArgumentArrayDeleter {
        void operator()(wchar_t** value) const noexcept {
            LocalFree(value);
        }
    };
    const std::unique_ptr<wchar_t*, ArgumentArrayDeleter> arguments(
        raw_arguments);

    RuntimeArguments result;
    bool saw_prepared_root = false;
    bool saw_level = false;

    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view name(raw_arguments[index]);
        if (name == L"--help" || name == L"-h" || name == L"/?") {
            result.show_help = true;
            continue;
        }
        if (name == L"--smoke-test") {
            result.smoke_test = true;
            continue;
        }
        if (name == L"--smoke-stage") {
            if (++index>=argument_count) throw std::runtime_error("--smoke-stage needs a stage name");
            result.smoke_stage=raw_arguments[index];
            if (result.smoke_stage!=L"intro" && result.smoke_stage!=L"post-intro" && result.smoke_stage!=L"frontend-background" &&
                result.smoke_stage!=L"frontend-menu" && result.smoke_stage!=L"frontend-dialog" && result.smoke_stage!=L"new-game-request" &&
                result.smoke_stage!=L"media-library" && result.smoke_stage!=L"frontend-exit" &&
                result.smoke_stage!=L"new-game-sequence" && result.smoke_stage!=L"frontend-audio-retirement")
                throw std::runtime_error("Unsupported startup smoke stage");
            continue;
        }
        if (name == L"--smoke-capture") {
            if (index+1>=argument_count || !result.smoke_capture.empty())
                throw std::runtime_error("--smoke-capture requires one output path");
            result.smoke_capture=std::filesystem::path(raw_arguments[++index]);
            continue;
        }
        if (name == L"--prepared-root") {
            if (index + 1 >= argument_count) {
                throw std::runtime_error(
                    "--prepared-root is missing its value");
            }
            const std::wstring_view value(raw_arguments[++index]);
            if (saw_prepared_root || value.empty()) {
                throw std::runtime_error(
                    "--prepared-root must be supplied exactly once");
            }
            result.prepared_root = std::filesystem::path(value);
            saw_prepared_root = true;
        } else if (name == L"--level") {
            if (index + 1 >= argument_count) {
                throw std::runtime_error("--level is missing its value");
            }
            const std::wstring_view value(raw_arguments[++index]);
            const auto parsed = parse_unsigned_decimal(value);
            if (saw_level || !parsed ||
                *parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error(
                    "--level must be one unsigned decimal value");
            }
            result.level_id = static_cast<std::uint32_t>(*parsed);
            result.explicit_level = true;
            saw_level = true;
        } else {
            throw std::runtime_error("The runtime received an unknown argument");
        }
    }

    if (!result.show_help && !saw_prepared_root) {
        throw std::runtime_error(
            "--prepared-root is required");
    }
    if (!result.smoke_capture.empty() && (!result.smoke_test || result.explicit_level ||
        !result.smoke_capture.is_absolute()))
        throw std::runtime_error("--smoke-capture needs an absolute path and an intro smoke");
    if(result.smoke_stage!=L"intro" && (!result.smoke_test||result.explicit_level))
        throw std::runtime_error("--smoke-stage requires startup --smoke-test");
    return result;
}

void refresh_window_title(const HWND window, const WindowState& state) {
    if (!state.renderer) {
        return;
    }
    auto suffix = state.gameplay_actor
                      ? std::wstring(
                            L" - playable prototype (prepared player model)")
                      : std::wstring(
                            L" - playable prototype (debug player marker)");
    if (!state.gameplay_collectible_ids.empty()) {
        suffix += L" - collectibles " +
                  std::to_wstring(state.gameplay_collected_count) + L"/" +
                  std::to_wstring(state.gameplay_collectible_ids.size());
    }
    if (!state.gameplay_destructible_ids.empty()) {
        suffix += L" - destructibles " +
                  std::to_wstring(state.gameplay_destroyed_count) + L"/" +
                  std::to_wstring(state.gameplay_destructible_ids.size());
    }
    SetWindowTextW(window, (state.base_title + suffix).c_str());
}

void remember_window_error(
    const HWND window,
    WindowState& state,
    const std::string& message) {
    if (!state.fatal_error) {
        state.fatal_error = message;
    }
    PostMessageW(window, WM_CLOSE, 0U, 0);
}

[[nodiscard]] constexpr std::int16_t gameplay_axis(
    const bool positive,
    const bool negative) noexcept {
    if (positive == negative) {
        return 0;
    }
    return positive ? openrc::game::kGameInputAxisMagnitudeV1
                    : static_cast<std::int16_t>(
                          -openrc::game::kGameInputAxisMagnitudeV1);
}

[[nodiscard]] openrc::game::GameInputSampleV1
make_gameplay_keyboard_sample(const GameplayKeyboardState& keyboard) noexcept {
    openrc::game::GameInputSampleV1 sample;
    sample.axes.move_x = gameplay_axis(
        keyboard.move_right, keyboard.move_left);
    sample.axes.move_y = gameplay_axis(
        keyboard.move_forward, keyboard.move_backward);
    sample.axes.look_x = gameplay_axis(
        keyboard.look_right, keyboard.look_left);
    sample.axes.look_y = gameplay_axis(
        keyboard.look_up, keyboard.look_down);
    if (keyboard.jump) {
        sample.held_buttons |= openrc::game::game_button_mask_v1(
            openrc::game::GameButtonV1::jump);
    }
    if (keyboard.reset) {
        sample.held_buttons |= openrc::game::game_button_mask_v1(
            openrc::game::GameButtonV1::reset_checkpoint);
    }
    if (keyboard.primary_action_key || keyboard.primary_action_pointer) {
        sample.held_buttons |= openrc::game::game_button_mask_v1(
            openrc::game::GameButtonV1::primary_action);
    }
    return sample;
}

[[nodiscard]] constexpr std::int16_t select_gameplay_axis(
    const std::int16_t keyboard,
    const std::int16_t gamepad) noexcept {
    return keyboard == 0 ? gamepad : keyboard;
}

[[nodiscard]] openrc::game::GameInputSampleV1 make_gameplay_input_sample(
    const WindowState& state) noexcept {
    const auto keyboard =
        make_gameplay_keyboard_sample(state.gameplay_keyboard);
    openrc::game::GameInputSampleV1 result;
    result.axes.move_x = select_gameplay_axis(
        keyboard.axes.move_x, state.gameplay_gamepad_sample.axes.move_x);
    result.axes.move_y = select_gameplay_axis(
        keyboard.axes.move_y, state.gameplay_gamepad_sample.axes.move_y);
    result.axes.look_x = select_gameplay_axis(
        keyboard.axes.look_x, state.gameplay_gamepad_sample.axes.look_x);
    result.axes.look_y = select_gameplay_axis(
        keyboard.axes.look_y, state.gameplay_gamepad_sample.axes.look_y);
    result.held_buttons = keyboard.held_buttons |
                          state.gameplay_gamepad_sample.held_buttons;
    return result;
}

void remember_gameplay_button_transition(
    WindowState& state,
    const std::uint32_t buttons_before,
    const std::uint32_t buttons_after) noexcept {
    state.gameplay_pending_pressed_buttons |=
        buttons_after & ~buttons_before;
    state.gameplay_pending_released_buttons |=
        buttons_before & ~buttons_after;
}

[[nodiscard]] bool set_gameplay_key(
    GameplayKeyboardState& keyboard,
    const WPARAM key,
    const bool held) noexcept {
    switch (key) {
    case 'A':
        keyboard.move_left = held;
        return true;
    case 'D':
        keyboard.move_right = held;
        return true;
    case 'W':
        keyboard.move_forward = held;
        return true;
    case 'S':
        keyboard.move_backward = held;
        return true;
    case VK_LEFT:
        keyboard.look_left = held;
        return true;
    case VK_RIGHT:
        keyboard.look_right = held;
        return true;
    case VK_UP:
        keyboard.look_up = held;
        return true;
    case VK_DOWN:
        keyboard.look_down = held;
        return true;
    case VK_SPACE:
        keyboard.jump = held;
        return true;
    case 'R':
        keyboard.reset = held;
        return true;
    case 'F':
        keyboard.primary_action_key = held;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool update_gameplay_key(
    WindowState& state,
    const WPARAM key,
    const bool held) noexcept {
    const auto buttons_before = make_gameplay_input_sample(state).held_buttons;
    if (!set_gameplay_key(state.gameplay_keyboard, key, held)) {
        return false;
    }
    const auto buttons_after = make_gameplay_input_sample(state).held_buttons;
    remember_gameplay_button_transition(
        state, buttons_before, buttons_after);
    return true;
}

void update_gameplay_primary_pointer(
    WindowState& state,
    const bool held) noexcept {
    const auto buttons_before = make_gameplay_input_sample(state).held_buttons;
    state.gameplay_keyboard.primary_action_pointer = held;
    const auto buttons_after = make_gameplay_input_sample(state).held_buttons;
    remember_gameplay_button_transition(
        state, buttons_before, buttons_after);
}

void poll_gameplay_gamepad(WindowState& state) noexcept {
    if (!state.gameplay_input_active) {
        return;
    }
    const auto buttons_before = make_gameplay_input_sample(state).held_buttons;
    const auto poll = state.gameplay_gamepad.poll();
    state.gameplay_gamepad_sample = poll.connected
                                        ? poll.sample
                                        : openrc::game::GameInputSampleV1{};
    const auto buttons_after = make_gameplay_input_sample(state).held_buttons;
    remember_gameplay_button_transition(
        state, buttons_before, buttons_after);
}

void suspend_gameplay_input(WindowState& state) noexcept {
    state.gameplay_input_active = false;
    state.gameplay_keyboard = {};
    state.gameplay_gamepad_sample = {};
    state.gameplay_pending_pressed_buttons = 0U;
    state.gameplay_pending_released_buttons =
        state.gameplay_submitted_buttons;
}

void submit_pending_gameplay_input(WindowState& state) {
    if (!state.gameplay) {
        return;
    }
    const auto final_sample = make_gameplay_input_sample(state);
    auto working_buttons = state.gameplay_submitted_buttons;
    const auto submit_buttons =
        [&state, &final_sample, &working_buttons](
            const std::uint32_t buttons) {
            auto sample = final_sample;
            sample.held_buttons = buttons;
            state.gameplay->submit_input_sample(sample);
            working_buttons = buttons;
        };

    for (std::uint8_t index = 0U;
         index < static_cast<std::uint8_t>(
                     openrc::game::GameButtonV1::count);
         ++index) {
        const auto bit = UINT32_C(1) << index;
        const auto pressed =
            (state.gameplay_pending_pressed_buttons & bit) != 0U;
        const auto released =
            (state.gameplay_pending_released_buttons & bit) != 0U;
        if (pressed && released) {
            if ((working_buttons & bit) != 0U) {
                submit_buttons(working_buttons & ~bit);
                submit_buttons(working_buttons | bit);
            } else {
                submit_buttons(working_buttons | bit);
                submit_buttons(working_buttons & ~bit);
            }
        } else if (pressed && (working_buttons & bit) == 0U) {
            submit_buttons(working_buttons | bit);
        } else if (released && (working_buttons & bit) != 0U) {
            submit_buttons(working_buttons & ~bit);
        }
    }
    if (working_buttons != final_sample.held_buttons) {
        submit_buttons(final_sample.held_buttons);
    } else {
        // Axes are sampled at the same fixed-tick boundary even when no
        // button transition occurred.
        state.gameplay->submit_input_sample(final_sample);
    }
}

[[nodiscard]] double gameplay_aspect_ratio(
    const std::uint32_t width,
    const std::uint32_t height) noexcept {
    if (width == 0U || height == 0U) {
        return 16.0 / 9.0;
    }
    return static_cast<double>(width) / static_cast<double>(height);
}

[[nodiscard]] openrc::game::ThirdPersonCameraProfileV1
make_gameplay_camera_profile(const double aspect_ratio) {
    constexpr auto kPi = std::numbers::pi_v<double>;
    return openrc::game::ThirdPersonCameraProfileV1{
        2.5,
        12.0,
        1.10,
        -10.0 * kPi / 180.0,
        70.0 * kPi / 180.0,
        2.4,
        1.8,
        5.0,
        60.0 * kPi / 180.0,
        aspect_ratio,
        0.05,
        4096.0,
    };
}

[[nodiscard]] double canonical_gameplay_yaw(const double yaw) {
    constexpr auto kTau = 2.0 * std::numbers::pi_v<double>;
    const auto result = std::remainder(yaw, kTau);
    if (!std::isfinite(result)) {
        throw std::runtime_error(
            "The prepared player spawn has an invalid facing direction");
    }
    return result == 0.0 ? 0.0 : result;
}

[[nodiscard]] GameplaySmokeTargetV1 make_gameplay_smoke_target(
    const openrc::EntitySceneV1& entities,
    const openrc::GameplayCollectibleV1& collectible) {
    const auto transform = std::lower_bound(
        entities.transforms.begin(),
        entities.transforms.end(),
        collectible.authored_id,
        [](const openrc::EntityTransformComponentV1& candidate,
           const std::uint32_t authored_id) {
            return candidate.authored_id < authored_id;
        });
    const auto binding = std::lower_bound(
        entities.render_bindings.begin(),
        entities.render_bindings.end(),
        collectible.authored_id,
        [](const openrc::EntityRenderBindingV1& candidate,
           const std::uint32_t authored_id) {
            return candidate.authored_id < authored_id;
        });
    if (transform == entities.transforms.end() ||
        transform->authored_id != collectible.authored_id ||
        binding == entities.render_bindings.end() ||
        binding->authored_id != collectible.authored_id) {
        throw std::runtime_error(
            "The graphical collectible smoke target is not render-bound");
    }
    return GameplaySmokeTargetV1{
        GameplaySmokeTargetKindV1::collectible,
        collectible.authored_id,
        binding->render_instance_id,
        openrc::game::world_collectible_center_v1(
            transform->transform, collectible),
        collectible.item_key,
        collectible.amount,
    };
}

[[nodiscard]] GameplaySmokeTargetV1 make_gameplay_smoke_target(
    const openrc::EntitySceneV1& entities,
    const openrc::DestructibleDefinitionV1& destructible) {
    const auto transform = std::lower_bound(
        entities.transforms.begin(),
        entities.transforms.end(),
        destructible.authored_id,
        [](const openrc::EntityTransformComponentV1& candidate,
           const std::uint32_t authored_id) {
            return candidate.authored_id < authored_id;
        });
    const auto binding = std::lower_bound(
        entities.render_bindings.begin(),
        entities.render_bindings.end(),
        destructible.authored_id,
        [](const openrc::EntityRenderBindingV1& candidate,
           const std::uint32_t authored_id) {
            return candidate.authored_id < authored_id;
        });
    if (transform == entities.transforms.end() ||
        transform->authored_id != destructible.authored_id ||
        binding == entities.render_bindings.end() ||
        binding->authored_id != destructible.authored_id) {
        throw std::runtime_error(
            "The graphical destructible smoke target is not render-bound");
    }
    if (destructible.drops.empty()) {
        throw std::runtime_error(
            "The graphical destructible smoke target has no drop to verify");
    }
    return GameplaySmokeTargetV1{
        GameplaySmokeTargetKindV1::destructible,
        destructible.authored_id,
        binding->render_instance_id,
        openrc::game::world_destructible_center_v1(
            transform->transform, destructible),
        destructible.drops.front().item_key,
        destructible.drops.front().amount,
    };
}

void update_gameplay_player_presentation(
    WindowState& state,
    const openrc::game::PlayerSimulationSnapshotV1& player) {
    if (!state.renderer || !state.gameplay || !state.gameplay_camera) {
        return;
    }
    const auto& character = player.character;
    const auto& profile = state.gameplay->profile().character;
    state.renderer->set_gameplay_presentation(
        state.gameplay_camera->view(character.feet_position),
        character.feet_position,
        player.facing_yaw_radians,
        profile.capsule_radius,
        profile.capsule_height);
}

void synchronize_gameplay_entity_presentation(WindowState& state) {
    if (!state.renderer || !state.gameplay) {
        return;
    }
    const auto* const entity_gameplay = state.gameplay->entity_gameplay();
    if (entity_gameplay == nullptr) {
        return;
    }
    for (const auto& binding : state.gameplay_render_bindings) {
        state.renderer->set_render_instance_enabled(
            binding.render_instance_id,
            entity_gameplay->enabled(binding.authored_id));
    }
    std::uint64_t collected_count = 0U;
    for (const auto authored_id : state.gameplay_collectible_ids) {
        if (entity_gameplay->collected(authored_id)) {
            ++collected_count;
        }
    }
    state.gameplay_collected_count = collected_count;

    std::uint64_t destroyed_count = 0U;
    for (const auto authored_id : state.gameplay_destructible_ids) {
        if (entity_gameplay->destroyed(authored_id)) {
            ++destroyed_count;
        }
    }
    state.gameplay_destroyed_count = destroyed_count;
}

void set_gameplay_entity_render_enabled(
    WindowState& state,
    const std::uint32_t authored_id,
    const bool enabled) {
    const auto binding = std::lower_bound(
        state.gameplay_render_bindings.begin(),
        state.gameplay_render_bindings.end(),
        authored_id,
        [](const openrc::EntityRenderBindingV1& candidate,
           const std::uint32_t id) {
            return candidate.authored_id < id;
        });
    if (binding != state.gameplay_render_bindings.end() &&
        binding->authored_id == authored_id) {
        state.renderer->set_render_instance_enabled(
            binding->render_instance_id, enabled);
    }
}

void apply_gameplay_events(
    WindowState& state,
    const std::span<const openrc::game::RuntimeGameplayTickV1> ticks) {
    if (!state.renderer) {
        throw std::logic_error(
            "Gameplay events cannot update presentation without a renderer");
    }
    for (const auto& tick : ticks) {
        for (const auto& event : tick.gameplay_events) {
            switch (event.kind) {
            case openrc::game::EntityGameplayEventKindV1::item_collected:
                if (!std::binary_search(
                        state.gameplay_collectible_ids.begin(),
                        state.gameplay_collectible_ids.end(),
                        event.authored_id)) {
                    throw std::logic_error(
                        "Gameplay collected an entity outside the mounted "
                        "scene");
                }
                set_gameplay_entity_render_enabled(
                    state, event.authored_id, false);
                if (state.gameplay_collected_count >=
                    static_cast<std::uint64_t>(
                        state.gameplay_collectible_ids.size())) {
                    throw std::logic_error(
                        "Gameplay collected an entity more than once");
                }
                ++state.gameplay_collected_count;
                break;
            case openrc::game::EntityGameplayEventKindV1::entity_damaged:
                if (!std::binary_search(
                        state.gameplay_destructible_ids.begin(),
                        state.gameplay_destructible_ids.end(),
                        event.authored_id)) {
                    throw std::logic_error(
                        "Gameplay damaged an entity outside the mounted "
                        "destructible scene");
                }
                // Damage alone deliberately leaves the render instance live.
                break;
            case openrc::game::EntityGameplayEventKindV1::entity_destroyed:
                if (!std::binary_search(
                        state.gameplay_destructible_ids.begin(),
                        state.gameplay_destructible_ids.end(),
                        event.authored_id)) {
                    throw std::logic_error(
                        "Gameplay destroyed an entity outside the mounted "
                        "destructible scene");
                }
                set_gameplay_entity_render_enabled(
                    state, event.authored_id, false);
                if (state.gameplay_destroyed_count >=
                    static_cast<std::uint64_t>(
                        state.gameplay_destructible_ids.size())) {
                    throw std::logic_error(
                        "Gameplay destroyed an entity more than once");
                }
                ++state.gameplay_destroyed_count;
                break;
            case openrc::game::EntityGameplayEventKindV1::item_granted:
                if (!std::binary_search(
                        state.gameplay_destructible_ids.begin(),
                        state.gameplay_destructible_ids.end(),
                        event.authored_id)) {
                    throw std::logic_error(
                        "Gameplay granted an item outside the mounted "
                        "destructible scene");
                }
                // The deterministic gameplay runtime has already committed
                // the persistent item total before emitting this event.
                break;
            default:
                throw std::logic_error(
                    "Gameplay emitted an unknown presentation event");
            }
        }
    }
}

void resize_gameplay_camera(
    WindowState& state,
    const std::uint32_t width,
    const std::uint32_t height) {
    if (!state.gameplay || !state.gameplay_camera || width == 0U ||
        height == 0U) {
        return;
    }
    auto profile = state.gameplay_camera->profile();
    profile.aspect_ratio = gameplay_aspect_ratio(width, height);
    const auto camera_state = state.gameplay_camera->state();
    state.gameplay_camera.emplace(profile, camera_state);
    update_gameplay_player_presentation(
        state, state.gameplay->player().snapshot());
}

[[nodiscard]] bool gameplay_frame_will_emit_tick(
    const WindowState& state,
    const std::uint64_t elapsed_nanoseconds) {
    if (!state.gameplay) {
        return false;
    }
    const auto& fixed_step = state.gameplay->profile().fixed_step;
    const auto accepted_elapsed = std::min(
        elapsed_nanoseconds, fixed_step.max_elapsed_nanoseconds);
    const auto scaled_total = state.gameplay->interpolation_numerator() +
        accepted_elapsed * fixed_step.ticks_per_second;
    return scaled_total >= openrc::game::kFixedStepTimeDenominatorV1;
}

void advance_gameplay_frame(
    const HWND window,
    WindowState& state,
    const std::chrono::steady_clock::time_point now) {
    if (!state.gameplay || !state.gameplay_camera) {
        return;
    }
    auto elapsed_nanoseconds = UINT64_C(0);
    if (state.previous_gameplay_frame !=
        std::chrono::steady_clock::time_point{}) {
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::nanoseconds>(now - state.previous_gameplay_frame);
        if (elapsed.count() > 0) {
            elapsed_nanoseconds =
                static_cast<std::uint64_t>(elapsed.count());
        }
    }
    state.previous_gameplay_frame = now;

    const auto will_emit_tick =
        gameplay_frame_will_emit_tick(state, elapsed_nanoseconds);
    if (will_emit_tick) {
        submit_pending_gameplay_input(state);
    }

    auto next_camera = *state.gameplay_camera;
    const openrc::game::RuntimeMovementMapperV1 movement_mapper =
        [&next_camera](const openrc::game::GameInputCommandV1& input,
                       const openrc::game::RuntimeMovementAxesV1 movement,
                       const double fixed_delta_seconds) {
            next_camera.fixed_update(
                {input.axes.look_x, input.axes.look_y, 0},
                fixed_delta_seconds);
            const auto world_movement = next_camera.map_unit_movement(
                movement.move_x, movement.move_y);
            return openrc::game::RuntimeMovementAxesV1{
                world_movement.move_x,
                world_movement.move_y,
            };
        };
    const auto frame = state.gameplay->advance_frame(
        elapsed_nanoseconds, movement_mapper);
    if (will_emit_tick != (frame.fixed_step.step_count != 0U)) {
        throw std::logic_error(
            "The platform input boundary disagreed with fixed-step scheduling");
    }
    if (will_emit_tick) {
        state.gameplay_submitted_buttons =
            make_gameplay_input_sample(state).held_buttons;
        state.gameplay_pending_pressed_buttons = 0U;
        state.gameplay_pending_released_buttons = 0U;
    }
    state.gameplay_camera = std::move(next_camera);
    const auto previous_collected_count = state.gameplay_collected_count;
    const auto previous_destroyed_count = state.gameplay_destroyed_count;
    if (state.gameplay_animation && !frame.ticks.empty()) {
        for (const auto& tick : frame.ticks) {
            state.gameplay_animation->fixed_update(tick.player_snapshot);
        }
        state.renderer->set_gameplay_actor_pose(
            state.gameplay_animation->palette());
    }
    update_gameplay_player_presentation(state, frame.snapshot.player);
    apply_gameplay_events(state, frame.ticks);
    if (state.gameplay_collected_count != previous_collected_count ||
        state.gameplay_destroyed_count != previous_destroyed_count) {
        refresh_window_title(window, state);
    }
}

LRESULT CALLBACK window_procedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param) {
    auto* state = reinterpret_cast<WindowState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create =
            reinterpret_cast<const CREATESTRUCTW*>(l_param);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (state != nullptr && state->renderer) {
            try {
                state->renderer->resize(
                    static_cast<std::uint32_t>(LOWORD(l_param)),
                    static_cast<std::uint32_t>(HIWORD(l_param)));
                resize_gameplay_camera(
                    *state,
                    static_cast<std::uint32_t>(LOWORD(l_param)),
                    static_cast<std::uint32_t>(HIWORD(l_param)));
                InvalidateRect(window, nullptr, FALSE);
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
            }
        }
        return 0;
    case WM_KEYDOWN:
        if(state&&state->frontend_input_active&&update_frontend_key(*state,w_param,true))return 0;
        if (state != nullptr && state->gameplay) {
            try {
                if (update_gameplay_key(*state, w_param, true)) {
                    return 0;
                }
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
                return 0;
            }
        }
        return DefWindowProcW(window, message, w_param, l_param);
    case WM_KEYUP:
        if(state&&state->frontend_input_active&&update_frontend_key(*state,w_param,false))return 0;
        if (state != nullptr && state->gameplay) {
            try {
                if (update_gameplay_key(*state, w_param, false)) {
                    return 0;
                }
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
                return 0;
            }
        }
        return DefWindowProcW(window, message, w_param, l_param);
    case WM_KILLFOCUS:
        if(state&&state->frontend_input_active){state->frontend_keys=0;state->frontend_pending_pressed=0;}
        if (state != nullptr && state->gameplay) {
            suspend_gameplay_input(*state);
            return 0;
        }
        break;
    case WM_SETFOCUS:
        if (state != nullptr && state->gameplay) {
            state->gameplay_input_active = true;
            return 0;
        }
        break;
    case WM_ACTIVATEAPP:
        if (state != nullptr && state->gameplay) {
            if (w_param == FALSE) {
                suspend_gameplay_input(*state);
            } else {
                state->gameplay_input_active = true;
            }
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        if (state != nullptr && state->gameplay) {
            update_gameplay_primary_pointer(*state, true);
            SetCapture(window);
            return 0;
        }
        return DefWindowProcW(window, message, w_param, l_param);
    case WM_LBUTTONUP:
        if (state != nullptr && state->gameplay) {
            update_gameplay_primary_pointer(*state, false);
            if (GetCapture() == window) {
                ReleaseCapture();
            }
            return 0;
        }
        return DefWindowProcW(window, message, w_param, l_param);
    case WM_CAPTURECHANGED:
        if (state != nullptr && state->gameplay) {
            update_gameplay_primary_pointer(*state, false);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        if (state != nullptr && state->renderer) {
            try {
                state->renderer->render();
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
            }
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

[[nodiscard]] std::wstring
make_runtime_window_title(const openrc::game::RuntimeLevelContentV1& content) {
    std::uint64_t triangle_count = 0U;
    for (const auto& mesh : content.render_scene.meshes) {
        triangle_count += mesh.triangle_indices.size() / 3U;
    }
    return std::wstring(L"OpenRC - level ") +
        std::to_wstring(content.foundation.level_id) + L" - " +
        std::to_wstring(content.render_scene.instances.size()) +
        L" instances, " + std::to_wstring(triangle_count) + L" triangles";
}

[[nodiscard]] HWND create_runtime_window(
    const HINSTANCE instance,
    WindowState& state,
    const std::wstring& title) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&window_class) == 0U) {
        throw std::runtime_error("RegisterClassExW failed");
    }

    constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
    RECT rectangle{0, 0, 1280, 720};
    if (AdjustWindowRectEx(&rectangle, kWindowStyle, FALSE, 0U) == FALSE) {
        throw std::runtime_error("AdjustWindowRectEx failed");
    }
    const auto window = CreateWindowExW(
        0U,
        kWindowClassName,
        title.c_str(),
        kWindowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top,
        nullptr,
        nullptr,
        instance,
        &state);
    if (window == nullptr) {
        throw std::runtime_error("CreateWindowExW failed");
    }
    return window;
}

void verify_media_capture(const openrc::runtime::MediaVideoFrameV1 &source,
                          std::span<const std::byte> capture,
                          std::uint32_t width, std::uint32_t height,
                          const openrc::MediaClipV1 &clip) {
    // Compare actual GPU output to the decoder's display-encoded pixels.
    // Sampling inside the rectangle also detects double gamma conversion,
    // wrong channel order, missing draws and a stale/black back buffer.
    const auto aspect=static_cast<double>(clip.display_aspect_numerator)/clip.display_aspect_denominator;
    const auto draw_width=std::min<double>(width,height*aspect);
    const auto draw_height=draw_width/aspect;
    const auto left=(width-draw_width)*0.5, top=(height-draw_height)*0.5;
    unsigned max_error=0, source_peak=0;
    for (unsigned gy=1;gy<32;++gy) for (unsigned gx=1;gx<32;++gx) {
        const auto x=static_cast<std::uint32_t>(left+draw_width*gx/32.0);
        const auto y=static_cast<std::uint32_t>(top+draw_height*gy/32.0);
        const auto sx=std::clamp((x+0.5-left)/draw_width*source.width-0.5,0.0,double(source.width-1U));
        const auto sy=std::clamp((y+0.5-top)/draw_height*source.height-0.5,0.0,double(source.height-1U));
        const auto x0=static_cast<std::uint32_t>(sx), y0=static_cast<std::uint32_t>(sy);
        const auto x1=std::min(x0+1U,source.width-1U), y1=std::min(y0+1U,source.height-1U);
        const auto fx=sx-x0, fy=sy-y0;
        for (unsigned channel=0;channel<3;++channel) {
            const auto pixel=[&](std::uint32_t px,std::uint32_t py) {
                return std::to_integer<unsigned>(source.rgba[(std::size_t(py)*source.width+px)*4U+channel]);
            };
            const auto expected=static_cast<int>(std::lround(
                (pixel(x0,y0)*(1-fx)+pixel(x1,y0)*fx)*(1-fy)+
                (pixel(x0,y1)*(1-fx)+pixel(x1,y1)*fx)*fy));
            const auto actual=std::to_integer<int>(capture[(std::size_t(y)*width+x)*4U+channel]);
            source_peak=std::max(source_peak,static_cast<unsigned>(expected));
            max_error=std::max(max_error,static_cast<unsigned>(std::abs(expected-actual)));
        }
    }
    if (source_peak<8U || max_error>3U)
        throw std::runtime_error("Intro framebuffer differs from decoded video: max channel error="+
                                 std::to_string(max_error)+", source peak="+std::to_string(source_peak));
    std::cout << "OpenRC intro framebuffer: 961 samples, max_channel_error=" << max_error << '\n';
}

// The only installation profile this runtime starts from. Older installations
// lack startup resources, so they are rejected before any presentation begins.
constexpr std::string_view kRequiredInstallationProfileSuffix="-native-eight-resource-v14-level-installation";

std::string& startup_installation_compiler_version() {
    static std::string value;
    return value;
}

std::string installation_profile_hint(std::string_view found) {
    return "Installation profile (compiler_version): "+(found.empty()?std::string("unknown"):std::string(found))+
        ", required suffix: "+std::string(kRequiredInstallationProfileSuffix)+
        ". Przygotuj grę ponownie w launcherze (Inspect disc → Prepare game).";
}

// Startup path only: the prepared installation must be the current profile.
void require_current_installation_profile(const openrc::PreparedGameV2RootV1& prepared) {
    const auto& identity=prepared.manifest.provenance;
    startup_installation_compiler_version()=identity.compiler_version;
    if(!(identity.game_id=="openrc-rac-2002"&&identity.build_id=="SCES-50916-PAL-v2.00"&&
         identity.compiler_id=="openrc-asset-compiler"&&
         identity.compiler_version.ends_with(kRequiredInstallationProfileSuffix)&&
         prepared.manifest.overlays.empty()&&prepared.manifest.shared_package.has_value()))
        throw std::runtime_error("The prepared installation does not match the current startup profile. "+
            installation_profile_hint(identity.compiler_version));
}

const openrc::LevelPackageResourceV1& startup_resource(
    const openrc::LevelPackageV1& package,std::string_view id,std::string_view type) {
    const auto entry=std::find_if(package.resources.begin(),package.resources.end(),
        [&](const auto& r){return r.resource_id==id;});
    if(entry==package.resources.end()||entry->type_id!=type||entry->schema_version!=1U)
        throw std::runtime_error("The prepared startup is missing resource "+std::string(id)+
            ". The installation profile is the probable cause. "+
            installation_profile_hint(startup_installation_compiler_version()));
    return *entry;
}

bool pump_startup_messages(WindowState& state) {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
        if(message.message==WM_QUIT) return false;
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    if(state.fatal_error) throw std::runtime_error(*state.fatal_error);
    return true;
}

bool wait_startup_until(WindowState& state,std::chrono::steady_clock::time_point deadline) {
    while(std::chrono::steady_clock::now()<deadline) {
        if(!pump_startup_messages(state)) return false;
        if(MsgWaitForMultipleObjectsEx(0,nullptr,2,QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED)
            throw std::runtime_error("Waiting for startup presentation failed");
    }
    return pump_startup_messages(state);
}

bool drain_startup_graphics(WindowState& state,bool retire_completion_owner=false) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(true) {
        const auto token=state.renderer->begin_submission_drain();
        while(!state.renderer->submission_drain_completed(token)) {
            if(!pump_startup_messages(state))return false;
            if(std::chrono::steady_clock::now()>=deadline)
                throw std::runtime_error("GPU presentation submissions did not complete");
            if(MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED)
                throw std::runtime_error("Waiting for GPU presentation completion failed");
        }
        // Window paint/resize can submit work while the completion wait pumps
        // messages. A fresh event must cover that work before returning.
        const auto completed=retire_completion_owner?
            state.renderer->try_retire_submission_drain(token):
            state.renderer->submission_drain_covers_current_work(token);
        if(completed)return true;
        if(std::chrono::steady_clock::now()>=deadline)
            throw std::runtime_error("New GPU presentation work did not settle");
    }
}

void save_startup_capture(const std::filesystem::path& path,
    std::span<const std::byte> rgba,std::uint32_t width,std::uint32_t height) {
    if(rgba.size()!=std::uint64_t(width)*height*4U)
        throw std::runtime_error("Startup capture dimensions differ from its framebuffer");
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out<<"P6\n"<<width<<' '<<height<<"\n255\n";
    for(std::size_t at=0;at<rgba.size();at+=4) out.write(reinterpret_cast<const char*>(rgba.data()+at),3);
    out.close();if(!out) throw std::runtime_error("Writing startup capture failed");
}

struct StartupSceneContent {
    openrc::RenderSceneV1 geometry;
    openrc::ActorLibraryV1 actors;
    openrc::ActorAnimationBankV1 animation;
    openrc::SceneTimelineV1 timeline;
    openrc::ScreenOverlayV1 title;
    openrc::ActorLibraryV1 menu_actors;
    openrc::ActorAnimationBankV1 menu_animation;
    openrc::SceneTimelineV1 menu_timeline;
    std::vector<openrc::ScreenOverlayV1> overlays;
    openrc::SessionStateInitialV1 session_initial;
    openrc::FrontendNoSavePlanV1 no_save;
    openrc::FrontendSequenceV1 new_game;
    std::vector<openrc::runtime::WindowsAudioBankClipV1> menu_audio;
    std::optional<openrc::AudioProgramCuesV1> ambient_cues;
};

bool has_startup_ambient(const openrc::LevelPackageV1& package) {
    const auto has=[&](std::string_view id){return std::ranges::any_of(package.resources,
        [&](const auto& resource){return resource.resource_id==id;});};
    const auto bank=has("frontend/audio/ambient-bank"),cues=has("frontend/audio/ambient-cues");
    if(bank!=cues)throw std::runtime_error("Startup ambient bank and cue resources must be admitted together");
    return bank;
}
bool admit_startup_audio(WindowState& state,const openrc::LevelPackageV1& package) {
    if(!has_startup_ambient(package))return true;
    openrc::runtime::WindowsAudioProgramBankContentV1 content;
    content.voices=openrc::decode_audio_voice_bank_v1(
        startup_resource(package,"frontend/audio/ambient-bank","openrc.audio-voice-bank").payload);
    content.program_resource_id=content.voices.program_resource_id;
    content.programs=openrc::decode_audio_program_bank_v1(
        startup_resource(package,content.program_resource_id,"openrc.audio-program").payload);
    for(const auto& id:content.voices.stream_resource_ids)
        content.streams.push_back({id,openrc::decode_audio_stream_v1(startup_resource(package,id,"openrc.audio-stream").payload)});
    for(const auto& id:content.voices.gain_resource_ids)
        content.gains.push_back({id,openrc::decode_audio_gain_table_v1(startup_resource(package,id,"openrc.audio-gain-table").payload)});
    state.frontend_ambient_audio=std::make_unique<openrc::runtime::WindowsAudioProgramBankV1>(std::move(content));
    // Start the single persistent timer/mixer owner before intro. Construction
    // and resource validation precede its first observation. Menu admission
    // never resets this clock or its idle, unchanged random state.
    state.frontend_ambient_audio->start();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!state.frontend_ambient_audio->stats().worker.submitted_frames) {
        static_cast<void>(state.frontend_ambient_audio->take_events());
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Persistent audio worker did not begin PCM delivery");
        if(!wait_startup_until(state,std::chrono::steady_clock::now()+std::chrono::milliseconds(1)))return false;
    }
    std::cout<<"OpenRC ambient audio: persistent_worker_started=1 first_pcm_submitted=1 before_intro=1\n";
    return true;
}
StartupSceneContent load_startup_scene(const openrc::LevelPackageV1& package) {
    const auto& actors=startup_resource(package,"frontend/background/actors","openrc.actor-library");
    const auto& animation=startup_resource(package,"frontend/background/animation","openrc.actor-animation-bank");
    const auto& timeline=startup_resource(package,"frontend/background/timeline","openrc.scene-timeline");
    const auto limits=openrc::game::make_runtime_level_content_limits_v1();
    StartupSceneContent content;
    content.geometry=openrc::decode_render_scene_v1(
        startup_resource(package,"frontend/background/geometry","openrc.render-scene").payload,limits.render_scene);
    content.timeline=openrc::decode_scene_timeline_v1(timeline.payload);
    if(has_startup_ambient(package)) {
        content.ambient_cues=openrc::decode_audio_program_cues_v1(
            startup_resource(package,"frontend/audio/ambient-cues","openrc.audio-program-cues").payload);
        if(content.ambient_cues->voice_bank_resource_id!="frontend/audio/ambient-bank"||
            content.ambient_cues->timeline_resource_id!=timeline.resource_id||
            content.ambient_cues->updates_per_second!=content.timeline.updates_per_second)
            throw std::runtime_error("Ambient cues refer to a different bank or scene clock");
        const auto bank=openrc::decode_audio_voice_bank_v1(
            startup_resource(package,content.ambient_cues->voice_bank_resource_id,"openrc.audio-voice-bank").payload);
        const auto programs=openrc::decode_audio_program_bank_v1(
            startup_resource(package,bank.program_resource_id,"openrc.audio-program").payload);
        for(const auto& cue:content.ambient_cues->cues) {
            // An authored window may cover the entire repeating scene and
            // extend beyond its final sample. Its first sample must be reachable;
            // the neutral cue validator already requires an ordered interval.
            if(cue.first_scene_sample>=content.timeline.samples.size()||
                !std::ranges::any_of(programs.programs,[&](const auto& program){return program.key==cue.program_key;}))
                throw std::runtime_error("Ambient cue leaves its admitted scene or program bank");
        }
    }
    content.title=openrc::decode_screen_overlay_v1(startup_resource(package,"frontend/title","openrc.screen-overlay").payload);
    if(content.title.updates_per_second!=content.timeline.updates_per_second)
        throw std::runtime_error("Startup title and scene clocks disagree");
    if(content.timeline.actor_library_sha256!=actors.payload_sha256||
       content.timeline.actor_animation_sha256!=animation.payload_sha256)
        throw std::runtime_error("Startup timeline refers to different actor or animation bytes");
    content.actors=openrc::decode_actor_library_v1(actors.payload,limits.actor_library);
    content.animation=openrc::decode_actor_animation_bank_v1(animation.payload,limits.actor_animation);
    openrc::validate_scene_timeline_bindings_v1(content.timeline,content.actors,content.animation);
    const auto& menu_actors=startup_resource(package,"frontend/menu/actors","openrc.actor-library");
    const auto& menu_animation=startup_resource(package,"frontend/menu/animation","openrc.actor-animation-bank");
    content.menu_actors=openrc::decode_actor_library_v1(menu_actors.payload,limits.actor_library);
    content.menu_animation=openrc::decode_actor_animation_bank_v1(menu_animation.payload,limits.actor_animation);
    content.menu_timeline=openrc::decode_scene_timeline_v1(
        startup_resource(package,"frontend/menu/timeline","openrc.scene-timeline").payload);
    if(content.menu_timeline.actor_library_sha256!=menu_actors.payload_sha256||
        content.menu_timeline.actor_animation_sha256!=menu_animation.payload_sha256||
        content.menu_timeline.updates_per_second!=content.timeline.updates_per_second)
        throw std::runtime_error("Frontend menu refers to different resources or cadence");
    openrc::validate_scene_timeline_bindings_v1(content.menu_timeline,content.menu_actors,content.menu_animation);
    content.session_initial=openrc::decode_session_state_initial_v1(
        startup_resource(package,"frontend/session-state","openrc.session-state").payload,
        {4U*1024U*1024U,openrc::frontend_session_state_limits_v1()});
    content.no_save=openrc::decode_frontend_no_save_plan_v1(
        startup_resource(package,"frontend/no-save-input","openrc.frontend-no-save-input").payload);
    content.new_game=openrc::decode_frontend_sequence_v1(
        startup_resource(package,"frontend/new-game-sequence","openrc.frontend-sequence").payload);
    if(std::ranges::any_of(content.new_game.resources,[](const auto& resource){
        return resource.resource_type=="openrc.frame-color-transfers";})) {
        for(unsigned variant=0;variant<5U;++variant)
            content.menu_audio.push_back({variant,openrc::decode_audio_clip_v1(startup_resource(
                package,"frontend/audio/variant-"+std::to_string(variant),"openrc.audio-clip").payload)});
    }
    openrc::ScreenOverlayLimitsV1 overlay_limits;overlay_limits.max_images=4096;
    // Source order: main lists, logo, prompt, then dialog backdrop/body/prompts.
    content.overlays.push_back(openrc::decode_screen_overlay_v1(
        startup_resource(package,"frontend/menu/lists","openrc.screen-overlay").payload,overlay_limits));
    for(const auto range:{std::pair{0U,64U},std::pair{64U,192U}}) {
        openrc::ScreenOverlayV1 layer;
        layer.canvas_width=content.title.canvas_width;layer.canvas_height=content.title.canvas_height;
        layer.updates_per_second=content.title.updates_per_second;layer.coverage_denominator=content.title.coverage_denominator;
        layer.frames.emplace_back();
        for(auto i=range.first;i<range.second;++i) {
            layer.images.push_back(content.title.images.at(i));
            layer.frames.push_back({{{i-range.first,range.first?160:236,
                range.first?static_cast<std::int32_t>(content.title.canvas_height)-80:16}}});
        }
        content.overlays.push_back(std::move(layer));
    }
    for(const auto id:{"frontend/dialog/backdrop","frontend/dialog/body/empty","frontend/dialog/body/absent",
            "frontend/dialog/body/without-game","frontend/dialog/body/unavailable","frontend/dialog/prompts/new",
            "frontend/dialog/prompts/existing","frontend/dialog/prompts/without-game","frontend/dialog/prompts/unavailable"})
        content.overlays.push_back(openrc::decode_screen_overlay_v1(startup_resource(package,id,"openrc.screen-overlay").payload,overlay_limits));
    return content;
}

std::uint32_t frontend_word(const openrc::game::SessionStateV1& state,const std::string& key,unsigned bytes=4U) {
    std::uint32_t value=0;
    for(unsigned i=0;i<bytes;++i)value|=std::uint32_t(state.read_u8(key+"/bytes",i))<<(8U*i);
    return value;
}

void write_frontend_input(openrc::game::GameSessionV1& session,std::uint32_t pressed) {
    std::vector<openrc::game::SessionStateWriteV1> writes;
    for(const auto key:{"input/pressed/bytes","input/global-pressed/bytes","input/repeated/bytes"})
        for(unsigned i=0;i<4U;++i)writes.push_back({key,i,openrc::SessionStateValueTypeV1::u8,(pressed>>(8U*i))&255U});
    session.apply_persistent_state_writes(writes,session.persistent_state()->revision());
}

openrc::FrontendMenuEvaluationV1 commit_frontend_evaluation(
    openrc::game::GameSessionV1& session,openrc::FrontendMenuEvaluationV1 evaluation) {
    if(evaluation.unsupported) {
        const auto& state=*session.persistent_state();
        throw std::runtime_error("Frontend reached an unimplemented original menu branch: mode="+
            std::to_string(frontend_word(state,"frontend/mode"))+" phase="+
            std::to_string(frontend_word(state,"frontend/root-phase"))+" card_mode="+
            std::to_string(frontend_word(state,"frontend/card-mode"))+" card_status="+
            std::to_string(frontend_word(state,"frontend/card-status"))+" card_index="+
            std::to_string(frontend_word(state,"frontend/card/index")));
    }
    session.apply_persistent_state_writes(evaluation.writes,evaluation.expected_revision);
    return evaluation;
}

// No card devices are mounted in this native session. A query creates a real
// completed backend operation; polling consumes that result once. The card
// owner receives completion from this queue, never from an elapsed-time gate.
class FrontendCardBackend {
public:
    openrc::FrontendCardSignalV1 execute(openrc::FrontendCardCommandV1 command) {
        openrc::FrontendCardSignalV1 signal;
        if(command==openrc::FrontendCardCommandV1::request_status) {
            signal.request_accepted=!completed_;
            if(*signal.request_accepted)completed_=true;
        } else if(command==openrc::FrontendCardCommandV1::poll) {
            signal.poll=completed_?openrc::FrontendCardPollStatusV1::completed_absent:openrc::FrontendCardPollStatusV1::no_request;
            if(completed_) {
                signal.completed_command_token=1U;
                signal.completed_result=-11;
                completed_=false;
            }
        }
        return signal;
    }
private:
    bool completed_=false;
};

void pump_frontend_card(WindowState& state) {
    if(!state.frontend_session||!state.frontend_cards)
        throw std::runtime_error("Frontend card update has no admitted session/backend");
    auto& session=*state.frontend_session;
    const auto& persistent=*session.persistent_state();
    commit_frontend_evaluation(session,openrc::evaluate_frontend_absent_card_v1(persistent,persistent.revision(),
        state.frontend_cards->execute(openrc::frontend_card_command_v1(persistent))));
}

bool frontend_loading_permitted(const openrc::game::SessionStateV1& state) {
    return std::bit_cast<std::int32_t>(frontend_word(state,"frontend/card-status"))<3&&
        std::bit_cast<std::int32_t>(frontend_word(state,"frontend/card-result"))<0;
}

bool run_startup_scene(WindowState& state,HWND window,
    const StartupSceneContent& content,const RuntimeArguments& arguments) {
    const auto& timeline=content.timeline;
    const auto limits=make_runtime_level_content_limits();
    const std::array libraries{content.actors,content.menu_actors};
    const auto combined=openrc::compose_actor_libraries_v1(libraries,limits.actor_library.library);
    std::vector<openrc::game::RuntimeWorldActorResolutionV1> bindings;
    const auto append_bindings=[&](const openrc::ActorLibraryV1& library,const openrc::SceneTimelineV1& scene,bool visible) {
        for(std::size_t i=0;i<scene.actors.size();++i) {
            const auto& actor=scene.actors[i];const auto& sample=scene.samples.front().actors[i];
            const auto rig=std::ranges::find(combined.rigs,library.rigs[actor.rig_index].semantic_key,&openrc::ActorRigAssetV1::semantic_key);
            const auto model=std::ranges::find(combined.models,library.models[actor.model_index].semantic_key,&openrc::ActorModelV1::semantic_key);
            if(rig==combined.rigs.end()||model==combined.models.end())throw std::runtime_error("Frontend actor composition lost a binding");
            bindings.push_back({static_cast<std::uint32_t>(bindings.size()),rig->id,model->id,
                actor.model_to_entity,sample.transform,visible&&sample.enabled});
        }
    };
    append_bindings(content.actors,timeline,true);
    const auto menu_base=static_cast<std::uint32_t>(bindings.size());
    append_bindings(content.menu_actors,content.menu_timeline,false);
    state.renderer->set_scene_geometry(content.geometry);
    state.renderer->set_scene_actors(combined,bindings);
    openrc::ScreenOverlayLimitsV1 overlay_limits;overlay_limits.max_images=32768;overlay_limits.max_bytes=192U*1024U*1024U;
    state.renderer->set_screen_overlay_layers(content.overlays,overlay_limits);
    state.frontend_session=std::make_unique<openrc::game::GameSessionV1>(0U,content.session_initial,openrc::frontend_session_state_limits_v1());
    auto& session=*state.frontend_session;
    const auto persistent=[&]() -> const openrc::game::SessionStateV1& {return *session.persistent_state();};
    const auto evaluate=[&](openrc::FrontendMenuEvaluationV1 result) {
        result=commit_frontend_evaluation(session,std::move(result));
        if(result.sound_requested&&state.frontend_menu_audio)
            static_cast<void>(state.frontend_menu_audio->queue(result.sound_variant,result.sound_object_token));
        return result;
    };
    const auto operation=[&](openrc::FrontendMenuOperationV1 op) {
        return evaluate(openrc::evaluate_frontend_menu_v1(op,content.no_save,persistent(),persistent().revision()));
    };
    struct AmbientCueOwner {std::uint64_t owner=0,stop_owner=0;bool queued=false;};
    std::vector<AmbientCueOwner> ambient_owners(content.ambient_cues?content.ambient_cues->cues.size():0);
    if(content.ambient_cues&&!state.frontend_ambient_audio)
        throw std::runtime_error("Scene ambient cues have no persistent audio owner");
    const auto acknowledge_ambient=[&]() {
        if(!state.frontend_ambient_audio)return;
        for(const auto& event:state.frontend_ambient_audio->take_events()) {
            if(event.kind==openrc::runtime::WindowsAudioProgramEventKindV1::admitted)continue;
            for(auto& owner:ambient_owners)if(owner.owner==event.owner)owner.owner=0;
        }
    };
    const auto submit_ambient=[&]() {
        if(!content.ambient_cues)return;
        for(std::size_t i=0;i<ambient_owners.size();++i) {
            auto& owner=ambient_owners[i];
            if(!owner.queued&&!owner.stop_owner)continue;
            // A render already in flight owns its samples. Retry a late
            // admission at the actual next command boundary, never backdate it.
            for(unsigned attempt=0;;++attempt) {
                const auto frame=state.frontend_ambient_audio->stats().worker.earliest_command_frame;
                openrc::runtime::WindowsPcmWorkerAdmissionKindV1 admitted;
                if(owner.stop_owner) {
                    const auto result=state.frontend_ambient_audio->stop(owner.stop_owner,frame);admitted=result.kind;
                    if(admitted==openrc::runtime::WindowsPcmWorkerAdmissionKindV1::accepted)owner.stop_owner=0;
                }else {
                    const auto result=state.frontend_ambient_audio->queue(content.ambient_cues->cues[i].program_key,frame);
                    admitted=result.kind;
                    if(admitted==openrc::runtime::WindowsPcmWorkerAdmissionKindV1::accepted) {
                        owner.owner=result.owner;owner.queued=false;
                        std::cout<<"OpenRC ambient cue: key="<<content.ambient_cues->cues[i].key
                            <<" program="<<content.ambient_cues->cues[i].program_key<<" owner="<<owner.owner
                            <<" accepted_frame="<<frame<<'\n';
                    }
                }
                if(admitted==openrc::runtime::WindowsPcmWorkerAdmissionKindV1::accepted)break;
                if(admitted==openrc::runtime::WindowsPcmWorkerAdmissionKindV1::full||attempt>=8U)
                    throw std::runtime_error("Frontend ambient admission did not reach its bounded audio queue");
            }
        }
    };
    const auto scan_ambient=[&](std::size_t sample) {
        if(!content.ambient_cues)return;
        for(std::size_t i=0;i<ambient_owners.size();++i) {
            auto& owner=ambient_owners[i];
            const auto action=openrc::audio_program_cue_action_v1(content.ambient_cues->cues[i],sample,owner.owner!=0);
            if(action==openrc::AudioProgramCueActionV1::admit)owner.queued=true;
            else if(action==openrc::AudioProgramCueActionV1::stop) {
                owner.stop_owner=owner.owner;owner.owner=0;owner.queued=false;
            }
        }
    };
    const auto pump_audio=[&]() {
        // The original pump delivers earlier callbacks, admits queued requests,
        // then polls again. Cue scanning itself does not observe worker state.
        acknowledge_ambient();submit_ambient();acknowledge_ambient();
        if(!state.frontend_menu_audio)return;
        state.frontend_menu_audio->pump();
        for(const auto& event:state.frontend_menu_audio->take_events())
            if(event.kind==openrc::runtime::WindowsAudioVoiceEventKindV1::started)
                std::cout<<"OpenRC menu audio: started variant="<<event.clip_key<<" owner="<<event.owner<<" voice="<<event.token<<'\n';
    };
    openrc::validate_frontend_no_save_plan_v1(content.no_save,persistent());
    if(content.new_game.state_schema_sha256!=persistent().schema_sha256())throw std::runtime_error("Frontend sequence state schema differs");
    operation(openrc::FrontendMenuOperationV1::bootstrap);
    if(!content.menu_audio.empty()) {
        if(frontend_word(persistent(),"progress/primary/field-36")!=1024U||
            frontend_word(persistent(),"progress/primary/field-34")!=1U)
            throw std::runtime_error("Prepared menu audio does not support the current sound settings");
        state.frontend_menu_audio=std::make_unique<openrc::runtime::WindowsAudioBankV1>(content.menu_audio);
        commit_frontend_evaluation(session,openrc::evaluate_frontend_platform_bootstrap_v1(persistent(),persistent().revision()));
        state.frontend_platform_active=true;
    }
    state.frontend_cards=std::make_unique<FrontendCardBackend>();
    state.frontend_display_width=timeline.display_aspect_numerator;
    state.frontend_display_height=timeline.display_aspect_denominator;
    state.frontend_input_active=true;
    bool main_admitted=false;
    std::uint64_t menu_age=0;
    const auto begin=std::chrono::steady_clock::now();
    std::uint64_t updates=0,submitted=0,menu_submitted=0;
    bool captured=false,menu_captured=false,dialog_captured=false;
    const bool menu_smoke=arguments.smoke_test&&arguments.smoke_stage!=L"frontend-background";
    bool smoke_title_pressed=false,smoke_menu_pressed=false,smoke_dialog_pressed=false;
    const auto smoke_press=[&]() {
        if(!PostMessageW(window,WM_KEYDOWN,VK_RETURN,0)||!PostMessageW(window,WM_KEYUP,VK_RETURN,0))
            throw std::runtime_error("Cannot submit frontend smoke controller input");
    };
    const auto capture=[&](const wchar_t* suffix) {
        if(arguments.smoke_capture.empty())return;
        RECT client{};if(!GetClientRect(window,&client))throw std::runtime_error("Cannot read frontend framebuffer dimensions");
        auto path=arguments.smoke_capture;path+=suffix;
        save_startup_capture(path,state.renderer->capture_frame_rgba(),static_cast<std::uint32_t>(client.right),static_cast<std::uint32_t>(client.bottom));
    };
    while(true) {
        if(!pump_startup_messages(state)) return false;
        if(menu_smoke&&!smoke_title_pressed&&updates>=120U){smoke_press();smoke_title_pressed=true;}
        if(!pump_startup_messages(state))return false;
        // Prepared sample n stores the visual post-update clock. Its INDEX n
        // names the source cue scan before that update, including scene wrap.
        scan_ambient(openrc::scene_timeline_sample_index_v1(timeline,updates));
        pump_frontend_card(state);
        const auto pad=state.gameplay_gamepad.poll();
        auto held=state.frontend_keys;
        if(pad.connected&&!arguments.smoke_test&&GetForegroundWindow()==window) {
            if(pad.sample.held_buttons&openrc::game::game_button_mask_v1(openrc::game::GameButtonV1::jump))held|=0x40U;
            if(pad.sample.held_buttons&openrc::game::game_button_mask_v1(openrc::game::GameButtonV1::pause))held|=0x800U;
            if(pad.sample.held_buttons&openrc::game::game_button_mask_v1(openrc::game::GameButtonV1::menu_back))held|=0x10U;
        }
        const auto pressed=state.frontend_pending_pressed|(held&~state.frontend_previous_buttons);
        state.frontend_pending_pressed=0;state.frontend_previous_buttons=held;
        write_frontend_input(session,pressed);
        const auto mode=frontend_word(persistent(),"frontend/mode");
        bool requested_new_game=false;
        if(mode==0U||mode==3U)
            commit_frontend_evaluation(session,openrc::evaluate_frontend_title_v1(content.title,persistent(),persistent().revision()));
        if(mode==0U)operation(openrc::FrontendMenuOperationV1::title_input);
        else if(mode==3U) {
            operation(openrc::FrontendMenuOperationV1::menu_counters);
            std::optional<openrc::FrontendMainResourcesReadyV1> ready;
            if(!main_admitted) {
                ready.emplace();
                if(content.menu_timeline.actors.size()!=ready->actor_tokens.size())throw std::runtime_error("Main menu actor count differs");
                for(std::size_t i=0;i<ready->actor_tokens.size();++i) {
                    ready->actor_tokens[i]=menu_base+static_cast<std::uint32_t>(i)+1U;
                    state.renderer->set_world_actor_camera(menu_base+static_cast<std::uint32_t>(i),&content.menu_timeline.samples[0].camera);
                }
            }
            const auto entry=evaluate(openrc::evaluate_frontend_main_entry_v1(persistent(),persistent().revision(),ready));
            if(entry.began_main_entry){main_admitted=true;menu_age=0;}
            else if(main_admitted)++menu_age;
            if(frontend_word(persistent(),"frontend/main/focused",1U))operation(openrc::FrontendMenuOperationV1::action4_input);
        } else if(mode==4U) {
            // Dialog update has its own leading sound pump, followed by the
            // outer dispatch tail's second pump, even when it requests exit.
            pump_audio();
            requested_new_game=operation(openrc::FrontendMenuOperationV1::dialog3_update).requested_new_game;
        }
        else throw std::runtime_error("Frontend entered an unsupported presentation mode");
        pump_audio();
        const auto index=openrc::scene_timeline_sample_index_v1(timeline,updates);
        const auto& sample=timeline.samples[index];
        state.renderer->set_scene_camera(sample.camera,timeline.display_aspect_numerator,
            timeline.display_aspect_denominator,timeline.clear_color);
        const auto draw_actors=[&](const openrc::ActorLibraryV1& library,const openrc::ActorAnimationBankV1& animation,
            const openrc::SceneTimelineV1& scene,std::uint32_t frame,std::uint32_t base,bool enabled,bool own_camera) {
            const auto& current=scene.samples.at(frame);
            for(std::size_t i=0;i<current.actors.size();++i) {
                const auto& actor=current.actors[i];const auto id=base+static_cast<std::uint32_t>(i);
                state.renderer->set_world_actor_enabled(id,enabled&&actor.enabled);
                if(!enabled||!actor.enabled)continue;
                const auto& rig=library.rigs[scene.actors[i].rig_index];const auto& clip=animation.clips[actor.clip_index];
                auto playback=openrc::start_actor_animation_playback_v1(clip);
                playback.frame_index=actor.frame_index;playback.phase=actor.phase;
                state.renderer->set_world_actor_pose(id,openrc::sample_actor_animation_pose_v1(clip,playback,rig.semantic_key,rig.rig,
                    {1024U,1024U,1024U,1.e-8,1.e6F}));
                state.renderer->set_world_actor_transform(id,actor.transform);
                if(own_camera)state.renderer->set_world_actor_camera(id,&current.camera);
            }
        };
        const auto final_mode=frontend_word(persistent(),"frontend/mode");
        draw_actors(content.actors,content.animation,timeline,index,0U,true,false);
        draw_actors(content.menu_actors,content.menu_animation,content.menu_timeline,
            static_cast<std::uint32_t>(std::min(menu_age,UINT64_C(12))),menu_base,main_admitted&&final_mode==3U,true);
        std::vector<std::optional<std::uint32_t>> frames(content.overlays.size());
        if(main_admitted&&final_mode==3U)frames[0]=openrc::screen_overlay_frame_index_v1(content.overlays[0],menu_age);
        for(const auto& draw:openrc::frontend_title_presentation_v1(persistent()).draws) {
            if(draw.image_id<64U)frames[1]=draw.image_id+1U;
            else frames[2]=draw.image_id-63U;
        }
        const auto dialog=openrc::evaluate_frontend_dialog_presentation_v1(persistent());
        if(dialog.unsupported)throw std::runtime_error("Frontend dialog reached an unprepared original presentation");
        if(dialog.backdrop)frames[3]=0U;
        if(dialog.panel) {
            std::uint32_t body=4U,prompts=UINT32_MAX;
            switch(dialog.message) {
                case openrc::FrontendDialogMessageV1::empty:break;
                case openrc::FrontendDialogMessageV1::absent_fresh:body=5U;prompts=8U;break;
                case openrc::FrontendDialogMessageV1::absent_existing:body=5U;prompts=9U;break;
                case openrc::FrontendDialogMessageV1::absent_without_game:body=6U;prompts=10U;break;
                case openrc::FrontendDialogMessageV1::unavailable:body=7U;prompts=11U;break;
            }
            frames[body]=dialog.body_remaining;
            if(prompts!=UINT32_MAX)frames[prompts]=dialog.prompt_remaining;
        }
        state.renderer->set_screen_overlay_layer_frames(frames);
        static_cast<void>(state.renderer->render());
        for(std::size_t i=0;i<sample.actors.size();++i)
            if(state.renderer->last_frame_world_actor_submitted(static_cast<std::uint32_t>(i))) ++submitted;
        for(std::size_t i=0;i<content.menu_timeline.actors.size();++i)
            if(state.renderer->last_frame_world_actor_submitted(menu_base+static_cast<std::uint32_t>(i)))++menu_submitted;
        if(menu_smoke&&main_admitted&&final_mode==3U&&menu_age>=20U&&!menu_captured) {
            if(!menu_submitted)throw std::runtime_error("Main menu did not submit its original actors");
            capture(L".menu.ppm");menu_captured=true;
            std::cout<<"OpenRC main menu: update="<<updates<<" entry_updates=12 actor_submissions="<<menu_submitted<<'\n';
            if(arguments.smoke_stage==L"frontend-menu")return true;
        }
        if(menu_smoke&&menu_captured&&!smoke_menu_pressed) {smoke_press();smoke_menu_pressed=true;}
        if(menu_smoke&&dialog.panel&&dialog.prompt_remaining==0U&&!dialog_captured) {
            capture(L".dialog.ppm");dialog_captured=true;
            std::cout<<"OpenRC absent-card dialog: update="<<updates<<" panel=1 prompts=1\n";
            if(arguments.smoke_stage==L"frontend-dialog")return true;
        }
        if(menu_smoke&&dialog_captured&&!smoke_dialog_pressed) {smoke_press();smoke_dialog_pressed=true;}
        if(!captured&&!arguments.smoke_capture.empty()&&updates>=100) {
            const auto rgba=state.renderer->capture_frame_rgba();RECT client{};
            if(!GetClientRect(window,&client)) throw std::runtime_error("Cannot read scene capture dimensions");
            auto output=arguments.smoke_capture;output+=L".frontend.ppm";
            save_startup_capture(output,rgba,static_cast<std::uint32_t>(client.right),static_cast<std::uint32_t>(client.bottom));
            std::uint64_t colored=0;
            for(std::size_t i=0;i<rgba.size();i+=4)
                if(rgba[i]!=std::byte{0}||rgba[i+1]!=std::byte{0}||rgba[i+2]!=std::byte{0}) ++colored;
            if(colored<256) throw std::runtime_error("Startup scene capture contains no visible geometry");
            std::cout<<"OpenRC frontend framebuffer: update="<<updates<<" nonblack_pixels="<<colored<<'\n';
            captured=true;
        }
        if(requested_new_game) {
            state.frontend_input_active=false;
            state.frontend_requested_new_game=true;
            std::cout<<"OpenRC New Game request: committed=1 revision="<<persistent().revision()
                <<" target_level="<<frontend_word(persistent(),"session/target-level")<<" final_frame_submitted=1\n";
            if(state.frontend_menu_audio) {
                const auto audio=state.frontend_menu_audio->stats();
                std::cout<<"OpenRC finite menu audio: admitted_clips="<<audio.clips<<" started="<<audio.started
                    <<" naturally_completed="<<audio.naturally_completed<<'\n';
                if(arguments.smoke_test&&audio.started!=2U)
                    throw std::runtime_error("Menu smoke did not start its two original finite sound requests");
            }
            if(state.frontend_ambient_audio) {
                const auto audio=state.frontend_ambient_audio->stats();
                std::cout<<"OpenRC ambient menu: program_tick="<<audio.playback.program_tick
                    <<" started="<<audio.playback.started<<" physical="<<audio.playback.physical_voices
                    <<" logical="<<audio.playback.instances<<" played_frames="<<audio.worker.played_frames<<'\n';
                if(arguments.smoke_test&&!audio.playback.started)
                    throw std::runtime_error("Menu smoke did not start its prepared ambient voices");
            }
            if(arguments.smoke_test&&arguments.smoke_stage==L"new-game-request"&&(!menu_captured||!dialog_captured))
                throw std::runtime_error("New Game request skipped its visible menu/dialog proof");
            // The caller retains this same canonical GameSession after the
            // source update's final draw for the prepared continuation.
            return true;
        }
        ++updates;
        if(!wait_startup_until(state,begin+std::chrono::nanoseconds(updates*1'000'000'000U/timeline.updates_per_second))) return false;
        if(arguments.smoke_test&&arguments.smoke_stage==L"frontend-background"&&updates>=timeline.samples.size()) {
            if(!submitted||(!arguments.smoke_capture.empty()&&!captured))
                throw std::runtime_error("Startup scene smoke did not submit its actors or capture");
            std::cout<<"OpenRC frontend background: updates="<<updates<<" actor_submissions="<<submitted<<'\n';
            return true;
        }
        if(menu_smoke&&updates>2000U)throw std::runtime_error("Frontend smoke did not reach its requested menu stage");
    }
}

bool present_startup_frame(WindowState& state) {
    while(!state.renderer->render()) {
        if(!wait_startup_until(state,std::chrono::steady_clock::now()+std::chrono::milliseconds(10)))return false;
    }
    return true;
}

bool run_frame_color_transfers(WindowState& state,HWND window,
    const openrc::FrameColorTransferSequenceV1& sequence,bool verify_frames,
    const std::function<void(std::uint32_t)>& completed_update={}) {
    openrc::validate_frame_color_transfer_sequence_v1(sequence);
    RECT client{};
    while(true) {
        if(!GetClientRect(window,&client))throw std::runtime_error("Cannot read feedback framebuffer dimensions");
        if(client.right>client.left&&client.bottom>client.top)break;
        if(!wait_startup_until(state,std::chrono::steady_clock::now()+std::chrono::milliseconds(10)))return false;
    }
    const auto width=static_cast<std::uint32_t>(client.right-client.left);
    const auto height=static_cast<std::uint32_t>(client.bottom-client.top);
    auto previous=state.renderer->capture_frame_rgba();
    const auto begin=std::chrono::steady_clock::now();
    std::uint64_t tick=sequence.lead_updates;
    const auto deadline=[&](std::uint64_t update){
        return begin+std::chrono::nanoseconds(update*1'000'000'000U/sequence.updates_per_second);
    };
    if(!wait_startup_until(state,deadline(tick)))return false;
    for(std::uint32_t index=0;index<sequence.transfers.size();++index) {
        openrc::apply_image_color_transfer_v1(previous,sequence.transfers[index]);
        state.renderer->set_media_frame(width,height,previous,width,height);
        if(!present_startup_frame(state))return false;
        if(verify_frames) {
            const auto actual=state.renderer->capture_frame_rgba();
            if(actual.size()!=previous.size())throw std::runtime_error("Feedback framebuffer resized during smoke verification");
            for(std::size_t i=0;i<actual.size();++i) {
                if(i%4U==3U)continue;
                const auto difference=std::to_integer<int>(actual[i])-std::to_integer<int>(previous[i]);
                if(difference<-1||difference>1)throw std::runtime_error("Feedback framebuffer differs from its prepared transfer");
            }
        }
        if(!wait_startup_until(state,deadline(++tick)))return false;
        // The final cue acknowledgment also owns the source's trailing wait.
        if(index+1U==sequence.transfers.size()&&
            !wait_startup_until(state,deadline(tick+sequence.tail_updates)))return false;
        if(completed_update)completed_update(index);
    }
    if(verify_frames)std::cout<<"OpenRC feedback framebuffer: verified_updates="<<sequence.transfers.size()
        <<" lead="<<sequence.lead_updates<<" tail="<<sequence.tail_updates<<'\n';
    return true;
}

class PreparedLevelLoad {
public:
    PreparedLevelLoad(const openrc::PreparedGameV2RootV1& prepared,std::uint32_t level_id):
        pending_(std::async(std::launch::async,[prepared,level_id] {
            const auto resolved=openrc::load_resolved_prepared_game_level_package_v1(prepared,level_id,
                std::span<const openrc::ExplicitLevelPackageOverlayBytesV1>{},make_runtime_prepared_game_limits());
            return openrc::game::load_runtime_level_content_v1(resolved,make_runtime_level_content_limits());
        })) {}
    bool poll() {
        if(content_)return true;
        if(!pending_.valid())throw std::runtime_error("Prepared level completion has no owned load");
        if(pending_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
        content_=pending_.get();
        std::cout<<"OpenRC prepared level load: completed=1 decoded_content=1\n";
        return true;
    }
    openrc::game::RuntimeLevelContentV1 take() {
        if(!content_)throw std::runtime_error("Prepared level admission has no completed content");
        auto result=std::move(*content_);content_.reset();return result;
    }
private:
    // A still-active async future joins when its owner is destroyed, including
    // window close/error. No detached producer can retain package references.
    std::future<openrc::game::RuntimeLevelContentV1> pending_;
    std::optional<openrc::game::RuntimeLevelContentV1> content_;
};

struct PreparedLoadingCard {
    std::uint32_t resource_index=UINT32_MAX;
    openrc::LoadingPresentationV1 presentation;
    std::vector<std::byte> rgba;
    bool began=false;
    std::chrono::steady_clock::time_point begin{};

    PreparedLoadingCard(std::uint32_t index,std::span<const std::byte> payload):
        resource_index(index),presentation(openrc::decode_loading_presentation_v1(payload)),
        rgba(std::size_t(presentation.library.canvas_width)*presentation.library.canvas_height*4U) {}

    void draw(std::uint32_t frame,std::uint32_t duration) {
        std::fill(rgba.begin(),rgba.end(),std::byte{0});
        for(std::size_t i=3;i<rgba.size();i+=4U)rgba[i]=std::byte{255};
        const auto overlay=openrc::materialize_loading_presentation_v1(presentation,frame,duration);
        openrc::composite_screen_overlay_frame_v1(rgba,overlay,0U);
    }
};

bool present_loading_card(WindowState& state,const PreparedLoadingCard& card) {
    if(!state.frontend_display_width||!state.frontend_display_height)
        throw std::runtime_error("Loading presentation has no admitted display profile");
    state.renderer->set_media_frame(card.presentation.library.canvas_width,
        card.presentation.library.canvas_height,card.rgba,
        state.frontend_display_width,state.frontend_display_height);
    return present_startup_frame(state);
}

struct PreparedMovieOwner {
    const openrc::MediaClipV1& clip;
    std::unique_ptr<openrc::runtime::WindowsMediaDecoderV1> decoder;
    std::optional<openrc::runtime::MediaVideoFrameV1> next;
    std::unique_ptr<openrc::runtime::WindowsMediaAudioV1> audio;
    bool begun=false,retired=false;
    explicit PreparedMovieOwner(const openrc::MediaClipV1& value):clip(value),
        decoder(std::make_unique<openrc::runtime::WindowsMediaDecoderV1>(clip)),next(decoder->next_frame()) {
        if(!next)throw std::runtime_error("Prepared movie has no decoded video frames");
        if(!clip.audio.empty())audio=std::make_unique<openrc::runtime::WindowsMediaAudioV1>(clip);
    }
};

bool run_prepared_movie_owner(WindowState&,HWND,PreparedMovieOwner&,std::string_view,const RuntimeArguments&,
    const std::function<bool(std::uint64_t)>& = {},std::uint64_t = 200U,const std::function<void()>& = {});

void request_frontend_audio_flags(WindowState& state,std::uint32_t mask) {
    auto& session=*state.frontend_session;const auto& persistent=*session.persistent_state();
    const auto flags=frontend_word(persistent,"audio/reverb-flags",1U)|mask;
    const std::array writes{openrc::game::SessionStateWriteV1{
        "audio/reverb-flags/bytes",0U,openrc::SessionStateValueTypeV1::u8,flags}};
    session.apply_persistent_state_writes(writes,persistent.revision());
}

struct FrontendAudioRetirementResult {
    openrc::runtime::WindowsAudioBankStatsV1 finite_before,finite_after;
    openrc::runtime::WindowsAudioProgramBankStatsV1 ambient_before,ambient_after;
    std::size_t finite_terminal_events=0,ambient_terminal_events=0;
};

// This component retires only the two admitted native sound-bank owners.
// It does not establish effect history or the absence of original disc streams,
// and therefore cannot by itself acknowledge transition/prepare.
FrontendAudioRetirementResult retire_frontend_audio(WindowState& state) {
    if(state.frontend_input_active||!state.frontend_menu_audio||!state.frontend_ambient_audio)
        throw std::runtime_error("Frontend audio retirement requires stopped input and both admitted banks");
    FrontendAudioRetirementResult result;
    auto& finite=*state.frontend_menu_audio;
    auto& ambient=*state.frontend_ambient_audio;
    result.finite_before=finite.stats();result.ambient_before=ambient.stats();
    const auto finite_owners=finite.voices();
    if(!result.finite_before.loaded)
        throw std::runtime_error("Frontend audio retirement has already consumed the finite bank");

    // Device reset/unprepare/close and the program-worker stop command are
    // checked barriers. An exception retains the owners for checked cleanup;
    // no destructor outcome is used as a successful stop acknowledgement.
    // A retry may already have the worker's source stop acknowledgement while
    // still owning a native device whose earlier retirement failed.
    finite.stop_all_and_wait();
    ambient.stop_all_and_wait();
    const auto finite_events=finite.take_events();
    const auto ambient_events=ambient.take_events();
    for(const auto& owner:finite_owners) {
        const auto terminal=std::ranges::count_if(finite_events,[&](const auto& event) {
            return event.token==owner.token&&event.kind!=openrc::runtime::WindowsAudioVoiceEventKindV1::started;
        });
        if(terminal!=1)throw std::runtime_error("Finite sound owner has no unique terminal acknowledgement");
    }
    result.finite_terminal_events=std::ranges::count_if(finite_events,[](const auto& event) {
        return event.kind!=openrc::runtime::WindowsAudioVoiceEventKindV1::started;
    });
    std::vector<std::uint64_t> terminal_owners;
    for(const auto& event:ambient_events) {
        if(event.kind==openrc::runtime::WindowsAudioProgramEventKindV1::admitted)continue;
        if(!event.owner||std::ranges::find(terminal_owners,event.owner)!=terminal_owners.end())
            throw std::runtime_error("Ambient sound owner has an invalid or duplicate terminal acknowledgement");
        terminal_owners.push_back(event.owner);
    }
    result.ambient_terminal_events=terminal_owners.size();
    result.finite_after=finite.stats();result.ambient_after=ambient.stats();
    const auto& f=result.finite_after;const auto& a=result.ambient_after;
    if(f.queued||f.prepared||f.active||f.retiring||f.pending_events||f.stopping||
        f.accepted!=f.naturally_completed+f.stopped+f.cancelled)
        throw std::runtime_error("Finite frontend sound retirement retained an owner");
    // Natural completion can race the pre-stop snapshot; those terminal events
    // are still queued for this control thread, so every earlier accepted owner
    // must be accounted for even when it completed before the stop command.
    if(result.ambient_terminal_events<result.ambient_before.accepted_owners||
        !a.stop_acknowledged||!a.playback.stopped||a.accepted_owners||a.pending_events||
        a.playback.instances||a.playback.pending_starts||a.playback.physical_voices||a.playback.attached_voices||
        !a.worker.device_retired||!a.worker.worker_exited||!a.worker.joined||
        a.worker.queued_commands||a.worker.pending_command_results||a.worker.failed||a.worker.retirement_failed)
        throw std::runtime_error("Ambient frontend sound retirement retained an owner");

    finite.unload();result.finite_after=finite.stats();
    if(result.finite_after.loaded||result.finite_after.clips||result.finite_after.pcm_bytes)
        throw std::runtime_error("Finite frontend bank retained its PCM storage");
    // The joined worker already destroyed its renderer and owned program bank.
    // Retain only value snapshots above; no sound may borrow either bank now.
    state.frontend_menu_audio.reset();state.frontend_ambient_audio.reset();
    return result;
}

// These bounded profiles have resident dry tones and a separately retired intro
// stream. Source history through preparation establishes zero wet effect state
// and no ordinary disc-stream admission; an empty native list alone does not.
// Read only neutral publication metadata here, never source images or modules.
void validate_frontend_prepare_profile(const openrc::LevelPackageV1& package,
    const openrc::PreparedGameV2RootV1& prepared) {
    const auto require=[](bool value) {
        if(!value)throw std::runtime_error("Frontend preparation requires the qualified PAL audio profile");
    };
    const auto& identity=prepared.manifest.provenance;
    require(identity.game_id=="openrc-rac-2002"&&identity.build_id=="SCES-50916-PAL-v2.00"&&
        identity.compiler_id=="openrc-asset-compiler"&&
        (identity.compiler_version.ends_with("-native-eight-resource-v13-frontend-ambient")||
         identity.compiler_version.ends_with(kRequiredInstallationProfileSuffix))&&
        identity.source_image_bytes&&!openrc::is_zero_prepared_digest_v1(identity.source_image_sha256)&&
        package.build_id==identity.build_id&&prepared.manifest.overlays.empty());
    using Kind=openrc::LevelPackageProvenanceKindV1;
    const auto source=[&](const openrc::LevelPackageResourceV1& resource,Kind kind,std::string_view locator)
        ->const openrc::LevelPackageProvenanceV1& {
        const openrc::LevelPackageProvenanceV1* found=nullptr;
        for(const auto& value:resource.provenance)if(value.kind==kind&&value.source_locator==locator) {
            require(!found);found=&value;
        }
        require(found!=nullptr);return *found;
    };
    const auto same=[](const auto& a,const auto& b) {
        return a.kind==b.kind&&a.source_locator==b.source_locator&&a.source_offset==b.source_offset&&
            a.source_bytes==b.source_bytes&&a.source_sha256==b.source_sha256;
    };
    const auto& first=startup_resource(package,"frontend/audio/variant-0","openrc.audio-clip");
    const auto& bank_source=source(first,Kind::iso_range,"rac1/frontend-sound-bank");
    const auto& modules=source(first,Kind::iso_range,"rac1/iop-module-bundle");
    const auto& boot=source(first,Kind::prepared_resource,"rac1/boot-executable");
    for(const auto* value:{&bank_source,&modules})
        require(value->source_bytes&&value->source_offset<=identity.source_image_bytes&&
            value->source_bytes<=identity.source_image_bytes-value->source_offset&&
            !openrc::is_zero_prepared_digest_v1(value->source_sha256));
    require(boot.source_bytes&&!openrc::is_zero_prepared_digest_v1(boot.source_sha256));
    std::vector<std::string> audio_ids;
    const auto checked=[&](const std::string& id,const char* type,const char* pass)
        ->const openrc::LevelPackageResourceV1& {
        const auto& resource=startup_resource(package,id,type);
        const auto& image=source(resource,Kind::iso_range,"rac1/disc-image");
        const auto& generated=source(resource,Kind::generated,pass);
        require(resource.schema_version==1U&&image.source_offset==0U&&
            image.source_bytes==identity.source_image_bytes&&image.source_sha256==identity.source_image_sha256&&
            same(source(resource,Kind::prepared_resource,"rac1/boot-executable"),boot)&&
            same(source(resource,Kind::iso_range,"rac1/frontend-sound-bank"),bank_source)&&
            same(source(resource,Kind::iso_range,"rac1/iop-module-bundle"),modules)&&
            generated.source_offset==0U&&generated.source_bytes==0U&&
            openrc::is_zero_prepared_digest_v1(generated.source_sha256));
        audio_ids.push_back(id);return resource;
    };
    constexpr auto finite_pass="compiler/rac-frontend-sound-resources-v1-effects1024-stereo";
    constexpr auto ambient_pass="compiler/rac-frontend-ambient-resources-v1-effects1024-stereo";
    for(unsigned i=0;i<5U;++i)
        static_cast<void>(checked("frontend/audio/variant-"+std::to_string(i),"openrc.audio-clip",finite_pass));
    const auto& program_resource=checked("frontend/audio/ambient-program","openrc.audio-program",ambient_pass);
    const auto& bank_resource=checked("frontend/audio/ambient-bank","openrc.audio-voice-bank",ambient_pass);
    const auto& cue_resource=checked("frontend/audio/ambient-cues","openrc.audio-program-cues",ambient_pass);
    const auto bank=openrc::decode_audio_voice_bank_v1(bank_resource.payload);
    const auto programs=openrc::decode_audio_program_bank_v1(program_resource.payload);
    const auto cues=openrc::decode_audio_program_cues_v1(cue_resource.payload);
    require(bank.program_resource_id==program_resource.resource_id&&bank.stream_resource_ids.size()==13U&&
        bank.gain_resource_ids.size()==4U&&bank.bindings.size()==26U&&bank.observation.frame_stride==200U&&
        programs.ticks_per_second==240U&&programs.programs.size()==5U&&
        cues.voice_bank_resource_id==bank_resource.resource_id&&cues.timeline_resource_id=="frontend/background/timeline"&&
        cues.updates_per_second==50U&&cues.cues.size()==5U);
    const auto reference=[&](const openrc::LevelPackageResourceV1& owner,const openrc::LevelPackageResourceV1& target) {
        const auto& value=source(owner,Kind::prepared_resource,target.resource_id);
        require(value.source_offset==0U&&value.source_bytes==target.payload.size()&&value.source_sha256==target.payload_sha256);
    };
    reference(bank_resource,program_resource);reference(cue_resource,bank_resource);
    reference(cue_resource,startup_resource(package,cues.timeline_resource_id,"openrc.scene-timeline"));
    constexpr std::array<unsigned,5> keys{2,3,8,4,9};
    for(unsigned i=0;i<keys.size();++i)
        require(programs.programs[i].key==keys[i]&&cues.cues[i].program_key==keys[i]);
    for(unsigned i=0;i<bank.stream_resource_ids.size();++i) {
        require(bank.stream_resource_ids[i]=="frontend/audio/ambient-stream-"+std::to_string(i));
        reference(bank_resource,checked(bank.stream_resource_ids[i],"openrc.audio-stream",ambient_pass));
    }
    for(unsigned i=0;i<bank.gain_resource_ids.size();++i) {
        require(bank.gain_resource_ids[i]=="frontend/audio/ambient-gain-"+std::to_string(i));
        reference(bank_resource,checked(bank.gain_resource_ids[i],"openrc.audio-gain-table",ambient_pass));
    }
    for(const auto& resource:package.resources)if(resource.resource_id.starts_with("frontend/audio/"))
        require(std::ranges::find(audio_ids,resource.resource_id)!=audio_ids.end());
    const auto& intro=startup_resource(package,"startup/intro","openrc.media-clip");
    const auto& intro_image=source(intro,Kind::iso_range,"rac1/disc-image");
    require(intro_image.source_offset==0U&&intro_image.source_bytes==identity.source_image_bytes&&
        intro_image.source_sha256==identity.source_image_sha256&&
        same(source(intro,Kind::prepared_resource,"rac1/boot-executable"),boot));
    static_cast<void>(source(intro,Kind::generated,"openrc.rac-startup-media-compile.v1"));
    for(const auto& entry:std::array<std::pair<const char*,const char*>,3>{{
        {"frontend/session-state","openrc.session-state"},
        {"frontend/no-save-input","openrc.frontend-no-save-input"},
        {"frontend/new-game-sequence","openrc.frontend-sequence"}}}) {
        const auto& resource=startup_resource(package,entry.first,entry.second);
        const auto& image=source(resource,Kind::iso_range,"rac1/disc-image");
        require(image.source_offset==0U&&image.source_bytes==identity.source_image_bytes&&
            image.source_sha256==identity.source_image_sha256&&
            same(source(resource,Kind::prepared_resource,"rac1/boot-executable"),boot));
        static_cast<void>(source(resource,Kind::generated,"openrc.rac-frontend-state-compile.v1"));
    }
    const auto& session=startup_resource(package,"frontend/session-state","openrc.session-state");
    reference(startup_resource(package,"frontend/no-save-input","openrc.frontend-no-save-input"),session);
    reference(startup_resource(package,"frontend/new-game-sequence","openrc.frontend-sequence"),session);
    if(std::ranges::any_of(package.resources,[](const auto& resource) {
        return resource.resource_id=="new-game/level-installation";
    })) {
        const auto& installation=startup_resource(package,"new-game/level-installation","openrc.state-installation");
        const auto& image=source(installation,Kind::iso_range,"rac1/disc-image");
        const auto& overlay=source(installation,Kind::iso_range,"rac1/initial-level-overlay");
        const auto& generated=source(installation,Kind::generated,"compiler/rac-initial-level-installation-v1");
        require(identity.compiler_version.ends_with(kRequiredInstallationProfileSuffix)&&
            openrc::prepared_content_sha256_v1(installation.payload)==installation.payload_sha256&&
            image.source_offset==0U&&image.source_bytes==identity.source_image_bytes&&
            image.source_sha256==identity.source_image_sha256&&
            same(source(installation,Kind::prepared_resource,"rac1/boot-executable"),boot)&&
            overlay.source_bytes&&overlay.source_offset<=identity.source_image_bytes&&
            overlay.source_bytes<=identity.source_image_bytes-overlay.source_offset&&
            !openrc::is_zero_prepared_digest_v1(overlay.source_sha256)&&
            generated.source_offset==0U&&generated.source_bytes==0U&&
            openrc::is_zero_prepared_digest_v1(generated.source_sha256));
        reference(installation,session);
    }
}

bool run_new_game_sequence(WindowState& state,HWND window,
    const openrc::LevelPackageV1& package,const RuntimeArguments& arguments,
    std::optional<StartupSceneContent>& frontend_content,const openrc::PreparedGameV2RootV1& prepared) {
    auto program=openrc::decode_frontend_sequence_v1(
        startup_resource(package,"frontend/new-game-sequence","openrc.frontend-sequence").payload);
    std::vector<openrc::FrontendSequenceResourceV1> admitted;
    for(const auto& reference:program.resources) {
        const auto& resource=startup_resource(package,reference.resource_id,reference.resource_type);
        if(openrc::prepared_content_sha256_v1(resource.payload)!=reference.payload_sha256)
            throw std::runtime_error("New Game sequence references different prepared resource bytes");
        if(resource.type_id=="openrc.media-clip")static_cast<void>(openrc::decode_media_clip_v1(resource.payload));
        else if(resource.type_id=="openrc.loading-presentation")static_cast<void>(openrc::decode_loading_presentation_v1(resource.payload));
        else if(resource.type_id=="openrc.frame-color-transfers")static_cast<void>(openrc::decode_frame_color_transfer_sequence_v1(resource.payload));
        else throw std::runtime_error("New Game sequence refers to an unsupported presentation resource");
        admitted.push_back({resource.resource_id,resource.type_id,resource.payload_sha256});
    }
    if(!state.frontend_session||!state.frontend_requested_new_game||
        state.frontend_session->persistent_state()->schema_sha256()!=program.state_schema_sha256)
        throw std::runtime_error("New Game continuation has no committed canonical frontend session");
    std::optional<openrc::StateInstallationV1> level_installation;
    if(std::ranges::any_of(package.resources,[](const auto& resource) {
        return resource.resource_id=="new-game/level-installation";
    })) {
        const auto& resource=startup_resource(package,"new-game/level-installation","openrc.state-installation");
        level_installation=openrc::decode_state_installation_v1(resource.payload);
        if(!prepared.manifest.provenance.compiler_version.ends_with(kRequiredInstallationProfileSuffix)||
            level_installation->level_id!=0U||level_installation->state_schema_sha256!=program.state_schema_sha256)
            throw std::runtime_error("New Game level installation has a different profile, level or canonical schema");
    }
    const bool audio_retirement_diagnostic=arguments.smoke_test&&arguments.smoke_stage==L"frontend-audio-retirement";
    std::vector<std::string> consumers;
    if(state.frontend_platform_active)consumers.push_back("frontend/exit-and-video-restore");
    if(state.frontend_platform_active&&!audio_retirement_diagnostic) {
        validate_frontend_prepare_profile(package,prepared);
        consumers.push_back("transition/prepare");
        if(level_installation)consumers.push_back("level/admit-prepared-sections");
    }
    const bool prepared_fades=std::ranges::all_of(program.cues,[&](const auto& cue) {
        return cue.kind!=openrc::FrontendSequenceCueKindV1::fade||cue.resource_index<program.resources.size();
    });
    if(prepared_fades)consumers.push_back("presentation/fade");
    if(state.frontend_cards&&state.frontend_display_width&&state.frontend_display_height) {
        consumers.push_back("loading/prepare");
        consumers.push_back("loading/begin-presentation");
        consumers.push_back("presentation/loading-overlay");
    }
    consumers.push_back("level/start-load");consumers.push_back("level/await-load");
    if(state.frontend_platform_active) {
        for(const auto key:{"media/prepare","presentation/media","media/cleanup-before-fade","media/cleanup-after-fade"})
            consumers.emplace_back(key);
        consumers.emplace_back("transition/cleanup");
    }
    openrc::FrontendSequencePlayerV1 player(std::move(program),admitted,consumers);
    std::optional<PreparedLoadingCard> loading_card;
    std::optional<openrc::MediaClipV1> movie_clip;
    std::unique_ptr<PreparedMovieOwner> movie_owner;
    std::uint32_t movie_resource=UINT32_MAX;
    bool movie_before_fade_completed=false;
    bool transition_prepared=false,transition_cleaned=false;
    bool level_state_installed=false;
    unsigned completed_movies=0,completed_fades=0,presented_cards=0;
    const auto poll_level_load=[&]() {
        if(!state.prepared_level)throw std::runtime_error("Prepared level completion has no started load");
        return state.prepared_level->poll();
    };
    while(player.command().phase!=openrc::FrontendSequencePhaseV1::complete) {
        const auto& cue=player.program().cues.at(player.command().cue_index);
        if(player.command().phase==openrc::FrontendSequencePhaseV1::incomplete&&
            cue.consumer_key=="transition/prepare"&&arguments.smoke_test&&
            arguments.smoke_stage==L"frontend-audio-retirement") {
            if(frontend_content||!state.frontend_menu_audio||!state.frontend_ambient_audio)
                throw std::runtime_error("Frontend audio diagnostic did not reach the real New Game scene exit");
            const auto before=state.frontend_ambient_audio->stats();
            if(!before.playback.physical_voices||!before.playback.instances||!before.accepted_owners||
                state.frontend_menu_audio->stats().started!=2U)
                throw std::runtime_error("Frontend audio diagnostic did not exercise live menu sound owners");
            const auto revision=state.frontend_session->persistent_state()->revision();
            const auto frozen=state.renderer->capture_frame_rgba();
            const auto retired=retire_frontend_audio(state);
            if(state.frontend_session->persistent_state()->revision()!=revision||
                state.renderer->capture_frame_rgba()!=frozen||
                player.command().phase!=openrc::FrontendSequencePhaseV1::incomplete)
                throw std::runtime_error("Frontend audio diagnostic changed unrelated transition state");
            std::cout<<"OpenRC frontend audio retirement diagnostic: finite_started="<<retired.finite_before.started
                <<" finite_natural="<<retired.finite_after.naturally_completed
                <<" finite_stopped="<<retired.finite_after.stopped
                <<" finite_terminal_events="<<retired.finite_terminal_events
                <<" ambient_live_before="<<retired.ambient_before.playback.physical_voices
                <<" ambient_logical_before="<<retired.ambient_before.playback.instances
                <<" ambient_terminal_events="<<retired.ambient_terminal_events
                <<" ambient_started="<<retired.ambient_after.playback.started
                <<" ambient_retired="<<retired.ambient_after.playback.retired
                <<" stop_acknowledged=1 devices_retired=1 worker_joined=1 banks_unloaded=1"
                <<" frozen_frame_preserved=1 session_unchanged=1 normal_sequence_barriers_executed=0\n";
            return true;
        }
        if(player.command().phase==openrc::FrontendSequencePhaseV1::incomplete&&
            cue.consumer_key=="level/enter"&&arguments.smoke_test&&
            arguments.smoke_stage==L"new-game-sequence") {
            if(!transition_prepared||!transition_cleaned||completed_movies!=3U||completed_fades!=7U||
                presented_cards!=3U||!level_state_installed||!state.admitted_level||state.prepared_level||state.gameplay)
                throw std::runtime_error("Normal New Game presentation did not complete its native owners");
            std::cout<<"OpenRC normal New Game sequence: prepare_completed=1 cards="<<presented_cards
                <<" movies="<<completed_movies<<" fades="<<completed_fades
                <<" level_load_completed=1 transition_cleanup_completed=1 normal_sequence_barriers_executed=1"
                <<" level_state_installed=1 next_owner=level/enter level_admitted=0 world_entered=0 level_playable=0\n";
            return true;
        }
        if(player.command().phase==openrc::FrontendSequencePhaseV1::incomplete)
            throw std::runtime_error("The original New Game request was committed; its next unimplemented runtime owner is "+cue.consumer_key);
        if(cue.kind==openrc::FrontendSequenceCueKindV1::session_writes) {
            player.apply_session_writes(*state.frontend_session);continue;
        }
        if(cue.consumer_key=="level/admit-prepared-sections") {
            if(!transition_cleaned||!level_installation||level_state_installed||state.admitted_level||
                !state.prepared_level||state.gameplay||state.gameplay_animation||state.frontend_input_active||
                state.frontend_platform_active||frontend_content||loading_card||movie_owner||movie_clip||
                movie_before_fade_completed||state.frontend_menu_audio||state.frontend_ambient_audio)
                throw std::runtime_error("Level installation precedes completed frontend owner retirement");
            if(!poll_level_load())throw std::runtime_error("Level installation has no completed prepared level");
            auto& session=*state.frontend_session;
            const auto& persistent=*session.persistent_state();
            const auto level=frontend_word(persistent,"session/target-level");
            if(level_installation->level_id!=level||level_installation->state_schema_sha256!=persistent.schema_sha256()||
                session.active_level_id()||session.pending_level_request()||session.next_tick_index())
                throw std::runtime_error("Level installation disagrees with the current canonical frontend session");
            const auto revision=persistent.revision();
            const auto frozen=state.renderer->capture_frame_rgba();
            // Consume completed I/O once. Keep its neutral assets alive even if
            // installation fails; later animation owners may borrow this storage.
            state.admitted_level.emplace(state.prepared_level->take());
            state.prepared_level.reset();
            if(state.admitted_level->foundation.level_id!=level)
                throw std::runtime_error("Completed level content differs from the requested installation");
            session.apply_state_installation(*level_installation,level,revision);
            if(session.persistent_state()->revision()!=revision+1U||
                state.renderer->capture_frame_rgba()!=frozen)
                throw std::runtime_error("Level installation lost its revision or changed the frozen presentation");
            level_state_installed=true;
            openrc::FrontendSequenceSignalV1 completed;completed.consumer_completed=true;player.advance(completed);
            std::cout<<"OpenRC level state installation: level="<<level
                <<" writes="<<level_installation->writes.size()<<" revision_before="<<revision
                <<" revision_after="<<session.persistent_state()->revision()
                <<" completed_content_taken=1 canonical_session_retained=1 frozen_frame_preserved=1"
                <<" level_state_installed=1 level_admitted=0 world_entered=0 level_playable=0\n";
            continue;
        }
        if(cue.consumer_key=="transition/prepare") {
            auto& session=*state.frontend_session;
            const auto& persistent=*session.persistent_state();
            if(transition_prepared||frontend_content||!state.frontend_intro_retired||
                state.frontend_input_active||!state.frontend_platform_active||state.prepared_level||
                loading_card||movie_owner||movie_clip||movie_before_fade_completed||
                !state.frontend_cards||player.program().updates_per_second!=50U||
                frontend_word(persistent,"session/target-level")!=0U||
                frontend_word(persistent,"progress/level/0/field-3001",1U)!=0U||
                frontend_word(persistent,"loading/selector-first-unlocked",1U)!=0U||
                frontend_word(persistent,"loading/selector-second-unlocked",1U)!=0U||
                frontend_word(persistent,"display/video-selector")!=1U||
                frontend_word(persistent,"display/saved-video-selector",1U)!=1U||
                frontend_word(persistent,"audio/group5-volume")!=1024U||
                frontend_word(persistent,"audio/reverb-depth")!=0U||
                frontend_word(persistent,"audio/reverb-mode",1U)!=0U||
                frontend_word(persistent,"audio/reverb-delay",1U)!=0U||
                frontend_word(persistent,"audio/reverb-feedback",1U)!=0U||
                (frontend_word(persistent,"audio/reverb-flags",1U)&~0x18U)!=0U)
                throw std::runtime_error("Frontend preparation is outside the admitted fresh dry-audio continuation");
            const auto frozen=state.renderer->capture_frame_rgba();
            commit_frontend_evaluation(session,openrc::evaluate_frontend_transition_prefix_v1(persistent,persistent.revision()));
            const auto retired=retire_frontend_audio(state);
            // The intro owner already stopped its stream and retired its decoder,
            // audio and GPU work. This profile admits no later disc-stream owner;
            // all resident tone storage is now released by the joined bank worker.
            // Media mode retains the exit image and clears black on each draw.
            // Source fog inputs have no effect on these FGE=0 cards or decoded
            // movies. Do not rebuild a camera or erase the image before its fade.
            if(!drain_startup_graphics(state))return false;
            if(state.renderer->capture_frame_rgba()!=frozen)
                throw std::runtime_error("Frontend preparation changed the retained fade framebuffer");
            transition_prepared=true;
            openrc::FrontendSequenceSignalV1 completed;completed.consumer_completed=true;player.advance(completed);
            std::cout<<"OpenRC transition prepare: canonical_prefix_committed=1 dry_profile_verified=1 intro_retired=1"
                <<" finite_terminal_events="<<retired.finite_terminal_events
                <<" ambient_terminal_events="<<retired.ambient_terminal_events
                <<" worker_joined=1 banks_unloaded=1 gpu_drained=1 frozen_frame_preserved=1\n";
            continue;
        }
        if(cue.consumer_key=="transition/cleanup") {
            if(frontend_content||loading_card||movie_owner||movie_clip||movie_before_fade_completed||
                state.frontend_input_active)
                throw std::runtime_error("Transition cleanup still owns an active presentation");
            if(state.frontend_menu_audio) {
                const auto audio=state.frontend_menu_audio->stats();
                if(audio.loaded||audio.queued||audio.prepared||audio.active||audio.retiring)
                    throw std::runtime_error("Transition cleanup precedes frontend sound bank retirement");
            }
            if(state.frontend_ambient_audio) {
                const auto audio=state.frontend_ambient_audio->stats();
                if(!audio.stop_acknowledged||!audio.worker.device_retired||!audio.worker.joined||
                    audio.worker.failed||audio.worker.retirement_failed||audio.accepted_owners||
                    audio.playback.instances||audio.playback.pending_starts||audio.playback.physical_voices)
                    throw std::runtime_error("Transition cleanup precedes ambient owner retirement");
            }
            if(!poll_level_load())
                throw std::runtime_error("Transition cleanup precedes prepared level completion");
            // Final transfer completion and removal of its native event owner.
            // The last image, renderer and canonical session remain live for
            // the later level admission. This does not acknowledge preparation.
            if(!drain_startup_graphics(state,true))return false;
            state.frontend_platform_active=false;
            transition_cleaned=true;
            openrc::FrontendSequenceSignalV1 completed;completed.consumer_completed=true;player.advance(completed);
            std::cout<<"OpenRC transition cleanup: gpu_drained=1 completion_owner_retired=1 level_load_completed=1\n";
            continue;
        }
        if(cue.consumer_key=="media/prepare") {
            if(movie_owner||movie_before_fade_completed)
                throw std::runtime_error("New Game movie preparation overlaps an active movie owner");
            if(frontend_word(*state.frontend_session->persistent_state(),"audio/group5-volume")!=1024U)
                throw std::runtime_error("Prepared movie requires an unimplemented restored group gain");
            const auto& reference=player.program().resources.at(cue.resource_index);
            movie_clip=openrc::decode_media_clip_v1(startup_resource(package,reference.resource_id,"openrc.media-clip").payload);
            // The original writes pending reverb bits; it does not process
            // that separate audio-command owner at this store.
            request_frontend_audio_flags(state,8U);
            movie_owner=std::make_unique<PreparedMovieOwner>(*movie_clip);
            movie_resource=cue.resource_index;
            loading_card.reset();
            openrc::FrontendSequenceSignalV1 completed;completed.consumer_completed=true;player.advance(completed);
            continue;
        }
        if(cue.kind==openrc::FrontendSequenceCueKindV1::media) {
            if(!movie_owner||movie_resource!=cue.resource_index||movie_owner->begun)
                throw std::runtime_error("New Game movie has no matching prepared decoder/audio owner");
            const auto skip=cue.skip;
            state.frontend_input_active=true;
            const auto stop_requested=[&](std::uint64_t) {
                if(arguments.smoke_test)return false;
                auto held=state.frontend_keys;
                const auto pad=state.gameplay_gamepad.poll();
                if(pad.connected&&GetForegroundWindow()==window&&
                    (pad.sample.held_buttons&openrc::game::game_button_mask_v1(openrc::game::GameButtonV1::pause)))held|=0x800U;
                const auto pressed=state.frontend_pending_pressed|(held&~state.frontend_previous_buttons);
                state.frontend_pending_pressed=0;state.frontend_previous_buttons=held;
                write_frontend_input(*state.frontend_session,pressed);
                openrc::FrontendSequenceSignalV1 input;input.pressed_word=pressed;
                player.advance(input);
                return (pressed&skip.pressed_any_mask)!=0U;
            };
            auto movie_arguments=arguments;
            if(!movie_arguments.smoke_capture.empty())movie_arguments.smoke_capture+=
                L".sequence-movie-"+std::to_wstring(movie_resource)+L".ppm";
            const auto& reference=player.program().resources.at(movie_resource);
            if(!run_prepared_movie_owner(state,window,*movie_owner,reference.resource_id,movie_arguments,
                stop_requested,200U,[&] {
                    openrc::FrontendSequenceSignalV1 started;started.media_started=true;player.advance(started);
                }))return false;
            state.frontend_input_active=false;
            if(!movie_owner->retired)throw std::runtime_error("Movie completed without retiring its actual owners");
            if(player.command().phase==openrc::FrontendSequencePhaseV1::media_feed) {
                openrc::FrontendSequenceSignalV1 ended;ended.media_input_available=false;player.advance(ended);
            }
            openrc::FrontendSequenceSignalV1 drained;drained.media_decoder_drained=true;player.advance(drained);
            drained={};drained.media_presentation_drained=true;player.advance(drained);
            ++completed_movies;
            continue;
        }
        if(cue.consumer_key=="media/cleanup-before-fade") {
            if(!movie_owner||!movie_owner->retired||movie_before_fade_completed)
                throw std::runtime_error("Movie cleanup precedes actual movie retirement");
            if(!wait_startup_until(state,std::chrono::steady_clock::now()+
                std::chrono::nanoseconds(1'000'000'000U/player.program().updates_per_second)))return false;
            if(!drain_startup_graphics(state))return false;
            movie_owner.reset();movie_clip.reset();movie_resource=UINT32_MAX;
            movie_before_fade_completed=true;
            openrc::FrontendSequenceSignalV1 completed;completed.consumer_completed=true;player.advance(completed);
            continue;
        }
        if(cue.consumer_key=="media/cleanup-after-fade") {
            if(!movie_before_fade_completed||movie_owner)
                throw std::runtime_error("Movie post-fade cleanup is out of order");
            request_frontend_audio_flags(state,16U);movie_before_fade_completed=false;
            openrc::FrontendSequenceSignalV1 completed;completed.consumer_completed=true;player.advance(completed);
            continue;
        }
        if(cue.kind==openrc::FrontendSequenceCueKindV1::start_level_load) {
            if(state.prepared_level||
                cue.level_id!=frontend_word(*state.frontend_session->persistent_state(),"session/target-level"))
                throw std::runtime_error("Prepared level load disagrees with its committed request");
            // The source starts real level I/O before the third card. Loading
            // parses neutral packages in its own joined task, never source data.
            state.prepared_level=std::make_unique<PreparedLevelLoad>(prepared,cue.level_id);
            std::cout<<"OpenRC prepared level load: started=1 level="<<cue.level_id<<'\n';
            openrc::FrontendSequenceSignalV1 completed;completed.consumer_completed=true;player.advance(completed);
            continue;
        }
        if(cue.kind==openrc::FrontendSequenceCueKindV1::await_level_load) {
            while(!poll_level_load())
                if(!wait_startup_until(state,std::chrono::steady_clock::now()+std::chrono::milliseconds(1)))return false;
            openrc::FrontendSequenceSignalV1 completed;completed.level_load_completed=true;player.advance(completed);
            continue;
        }
        if(cue.consumer_key=="loading/prepare") {
            const auto& reference=player.program().resources.at(cue.resource_index);
            if(reference.resource_type!="openrc.loading-presentation")
                throw std::runtime_error("Loading preparation references the wrong resource type");
            loading_card.emplace(cue.resource_index,
                startup_resource(package,reference.resource_id,reference.resource_type).payload);
            if(loading_card->presentation.library.updates_per_second!=player.program().updates_per_second)
                throw std::runtime_error("Loading presentation has a different prepared update clock");
            openrc::FrontendSequenceSignalV1 completed;completed.consumer_completed=true;player.advance(completed);
            continue;
        }
        if(cue.consumer_key=="loading/begin-presentation") {
            if(!loading_card||loading_card->resource_index!=cue.resource_index||loading_card->began)
                throw std::runtime_error("Loading presentation has no matching prepared owner");
            if(!wait_startup_until(state,std::chrono::steady_clock::now()+
                std::chrono::nanoseconds(1'000'000'000U/player.program().updates_per_second)))return false;
            if(!drain_startup_graphics(state))return false;
            loading_card->began=true;loading_card->begin=std::chrono::steady_clock::now();
            openrc::FrontendSequenceSignalV1 completed;completed.consumer_completed=true;player.advance(completed);
            continue;
        }
        if(cue.kind==openrc::FrontendSequenceCueKindV1::loading_overlay) {
            if(!loading_card||loading_card->resource_index!=cue.resource_index||!loading_card->began)
                throw std::runtime_error("Loading frame has no active presentation owner");
            openrc::FrontendSequenceSignalV1 presented;
            presented.loading_presentation_permitted=frontend_loading_permitted(*state.frontend_session->persistent_state());
            if(player.command().phase==openrc::FrontendSequencePhaseV1::loading_gate) {
                player.advance(presented);continue;
            }
            const auto command=player.command();
            if(command.phase!=openrc::FrontendSequencePhaseV1::loading_frame)
                throw std::runtime_error("Loading presentation is in an unexpected phase");
            loading_card->draw(command.frame_index,command.duration);
            // Source frame tail polls the same card/UI owner before Present;
            // real level-load completion belongs after that presentation.
            pump_frontend_card(state);
            if(!present_loading_card(state,*loading_card))return false;
            if(command.frame_index==0U)++presented_cards;
            if(!wait_startup_until(state,loading_card->begin+std::chrono::nanoseconds(
                (std::uint64_t(command.frame_index)+1U)*1'000'000'000U/player.program().updates_per_second)))return false;
            presented.frame_presented=true;
            presented.loading_presentation_permitted=frontend_loading_permitted(*state.frontend_session->persistent_state());
            if(command.poll_level_load)presented.level_load_completed=poll_level_load();
            player.advance(presented);continue;
        }
        if(cue.kind==openrc::FrontendSequenceCueKindV1::fade) {
            const auto& reference=player.program().resources.at(cue.resource_index);
            const auto fade=openrc::decode_frame_color_transfer_sequence_v1(
                startup_resource(package,reference.resource_id,reference.resource_type).payload);
            if(fade.transfers.size()!=cue.updates||fade.updates_per_second!=player.program().updates_per_second)
                throw std::runtime_error("New Game fade timing differs from its admitted sequence");
            if(!run_frame_color_transfers(state,window,fade,arguments.smoke_test,[&](std::uint32_t index) {
                if(player.command().phase!=openrc::FrontendSequencePhaseV1::fade_frame||player.command().frame_index!=index)
                    throw std::runtime_error("New Game fade acknowledgment is out of order");
                openrc::FrontendSequenceSignalV1 presented;presented.frame_presented=true;player.advance(presented);
            }))return false;
            ++completed_fades;
            continue;
        }
        if(cue.consumer_key!="frontend/exit-and-video-restore")
            throw std::runtime_error("New Game reached an unregistered native operation");
        auto& session=*state.frontend_session;
        const auto& persistent=*session.persistent_state();
        auto exit=openrc::evaluate_frontend_exit_v1(persistent,persistent.revision());
        if(exit.unsupported||!exit.frontend_exit_reached)
            throw std::runtime_error("Frontend exit has no supported committed transition request");
        if(exit.restore_display_selector)
            throw std::runtime_error("Frontend exit requires an unimplemented display profile change");
        RECT client{};
        if(!GetClientRect(window,&client))throw std::runtime_error("Cannot read frontend exit dimensions");
        const auto width=static_cast<std::uint32_t>(client.right-client.left);
        const auto height=static_cast<std::uint32_t>(client.bottom-client.top);
        const auto previous=state.renderer->capture_frame_rgba();
        state.renderer->set_media_frame(width,height,previous,width,height);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(true) {
            const auto drain=state.renderer->begin_submission_drain();
            while(!state.renderer->submission_drain_completed(drain)) {
                if(!wait_startup_until(state,std::chrono::steady_clock::now()+std::chrono::milliseconds(1)))return false;
                if(std::chrono::steady_clock::now()>=deadline)
                    throw std::runtime_error("Frontend exit GPU work did not complete");
            }
            if(state.renderer->try_retire_scene_for_media(drain))break;
            if(std::chrono::steady_clock::now()>=deadline)
                throw std::runtime_error("Frontend exit display work did not settle");
        }
        frontend_content.reset();
        commit_frontend_evaluation(session,std::move(exit));
        openrc::FrontendSequenceSignalV1 completed;completed.consumer_completed=true;player.advance(completed);
        std::cout<<"OpenRC frontend exit: gpu_drained=1 scene_retired=1 frozen_frame_preserved=1\n";
        if(arguments.smoke_test&&arguments.smoke_stage==L"frontend-exit")return true;
    }
    return true;
}

bool run_post_intro(WindowState& state,HWND window,
    const openrc::LevelPackageV1& package,const RuntimeArguments& arguments,
    const openrc::PreparedGameV2RootV1& prepared) {
    const auto image=openrc::decode_image_presentation_v1(
        startup_resource(package,"startup/post-intro","openrc.image-presentation").payload);
    const openrc::FrameColorTransferSequenceV1 feedback{image.updates_per_second,
        image.transfer_lead_updates,image.transfer_tail_updates,image.color_transfers};
    if(!run_frame_color_transfers(state,window,feedback,arguments.smoke_test))return false;
    state.renderer->set_media_frame(image.width,image.height,image.rgba,
        image.display_aspect_numerator,image.display_aspect_denominator);
    if(!present_startup_frame(state))return false;
    // The loading clock begins at asset initialization, after bitmap display.
    const auto initialization_begin=std::chrono::steady_clock::now();
    std::optional<StartupSceneContent> scene;
    const bool has_timeline=std::ranges::any_of(package.resources,[](const auto& resource){
        return resource.resource_id=="frontend/background/timeline";});
    if(has_timeline) scene=load_startup_scene(package);
    const auto initialization_end=std::chrono::steady_clock::now();
    const auto work_ns=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(initialization_end-initialization_begin).count());
    const auto remaining=openrc::image_initialization_remaining_updates_v1(image,work_ns);
    if(!wait_startup_until(state,initialization_end+std::chrono::nanoseconds(std::uint64_t(remaining)*1'000'000'000U/image.updates_per_second))) return false;
    if(!arguments.smoke_capture.empty()) {
        const auto rgba=state.renderer->capture_frame_rgba();
        RECT client{};
        if(!GetClientRect(window,&client)) throw std::runtime_error("Cannot read post-intro capture dimensions");
        openrc::runtime::MediaVideoFrameV1 reference;reference.width=image.width;reference.height=image.height;reference.rgba=image.rgba;
        openrc::MediaClipV1 geometry;geometry.display_aspect_numerator=image.display_aspect_numerator;geometry.display_aspect_denominator=image.display_aspect_denominator;
        verify_media_capture(reference,rgba,static_cast<std::uint32_t>(client.right),static_cast<std::uint32_t>(client.bottom),geometry);
        auto output=arguments.smoke_capture;output+=L".post-intro.ppm";
        save_startup_capture(output,rgba,static_cast<std::uint32_t>(client.right),static_cast<std::uint32_t>(client.bottom));
    }
    std::cout<<"OpenRC post-intro: fade_updates="<<image.color_transfers.size()<<" initialization_work_ns="<<work_ns
        <<" remaining_updates="<<remaining<<" image="<<image.width<<'x'<<image.height<<'\n';
    if(arguments.smoke_test&&arguments.smoke_stage==L"post-intro") return true;
    if(!scene) throw std::runtime_error("The original post-intro image finished. Prepared frontend scene presentation is not available yet.");
    if(!run_startup_scene(state,window,*scene,arguments))return false;
    if(state.frontend_requested_new_game&&!(arguments.smoke_test&&arguments.smoke_stage==L"new-game-request"))
        return run_new_game_sequence(state,window,package,arguments,scene,prepared);
    return true;
}

// The same movie owner serves startup and prepared sequence media. Its stop
// callback comes from the caller's admitted input policy; it never selects a
// replacement movie or advances an unimplemented sequence consumer.
bool run_prepared_movie_owner(WindowState& state,HWND window,PreparedMovieOwner& owner,
    std::string_view label,const RuntimeArguments& arguments,
    const std::function<bool(std::uint64_t)>& stop_requested,
    std::uint64_t capture_frame,const std::function<void()>& started) {
    if(owner.begun||owner.retired||!owner.decoder||!owner.next)
        throw std::runtime_error("Prepared movie cannot begin from its current owner state");
    const auto& clip=owner.clip;auto& decoder=owner.decoder;auto& next=owner.next;auto& audio=owner.audio;
    const auto audio_time=clip.audio.empty()?next->presentation_time_100ns:clip.audio_start_time*1000/9;
    const auto origin=std::min(audio_time,next->presentation_time_100ns);
    const auto audio_offset=audio_time-origin;
    const auto submitted_begin=state.renderer->media_frames_submitted();
    if(next->presentation_time_100ns==origin)
        state.renderer->set_media_frame(next->width,next->height,next->rgba,
            clip.display_aspect_numerator,clip.display_aspect_denominator);
    const auto begin=std::chrono::steady_clock::now();
    owner.begun=true;if(started)started();
    bool audio_started=false,captured=false,stopped=false;
    std::uint64_t decoded_frames=0;
    std::int64_t last_frame_end=0;
    std::optional<openrc::runtime::MediaVideoFrameV1> capture_source;
    std::optional<std::chrono::steady_clock::time_point> audio_finished_at;
    while(true) {
        if(!pump_startup_messages(state)) {
            if(arguments.smoke_test)throw std::runtime_error("Movie smoke closed before completion");
            return false;
        }
        if(!stopped&&stop_requested&&stop_requested(decoded_frames)) {
            decoder->stop_input_and_drain();
            stopped=true;
        }
        const auto now=std::chrono::steady_clock::now();
        auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(now-begin).count()/100;
        if(audio&&!audio_started&&elapsed>=audio_offset){audio->start();audio_started=true;}
        if(audio_started) {
            elapsed=audio_offset+static_cast<std::int64_t>(audio->played_samples()*10'000'000U/clip.audio_sample_rate);
            if(audio->finished()) {
                if(!audio_finished_at)audio_finished_at=now;
                elapsed=audio_offset+static_cast<std::int64_t>(clip.audio.size()/clip.audio_channels*10'000'000U/clip.audio_sample_rate)
                    +std::chrono::duration_cast<std::chrono::nanoseconds>(now-*audio_finished_at).count()/100;
            }
        }
        while(next&&next->presentation_time_100ns-origin<=elapsed) {
            state.renderer->set_media_frame(next->width,next->height,next->rgba,
                clip.display_aspect_numerator,clip.display_aspect_denominator);
            last_frame_end=next->presentation_time_100ns-origin+next->duration_100ns;
            if(!present_startup_frame(state))return false;
            ++decoded_frames;
            if(!captured&&!arguments.smoke_capture.empty())capture_source=std::move(next);
            next=decoder->next_frame();
        }
        if(!captured&&decoded_frames>=capture_frame&&!arguments.smoke_capture.empty()) {
            const auto rgba=state.renderer->capture_media_frame_rgba();
            RECT client{};
            if(!GetClientRect(window,&client))throw std::runtime_error("Cannot read movie capture dimensions");
            const auto width=static_cast<std::uint32_t>(client.right-client.left);
            const auto height=static_cast<std::uint32_t>(client.bottom-client.top);
            if(rgba.size()!=std::uint64_t(width)*height*4U)
                throw std::runtime_error("Movie framebuffer capture size differs");
            verify_media_capture(*capture_source,rgba,width,height,clip);
            save_startup_capture(arguments.smoke_capture,rgba,width,height);
            std::cout<<"OpenRC "<<label<<" capture: frame_index="<<decoded_frames-1U<<'\n';
            captured=true;capture_source.reset();
        }
        // End of input is not presentation completion. Every decoder output
        // is presented through its duration before the actual stream stop.
        if(!next&&elapsed>=last_frame_end)break;
        if(MsgWaitForMultipleObjectsEx(0,nullptr,2,QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED)
            throw std::runtime_error("Waiting for prepared movie failed");
        if(arguments.smoke_test&&now-begin>std::chrono::seconds(180))
            throw std::runtime_error("Movie smoke did not complete video presentation");
    }
    if(!decoder->drained()||decoder->input_available()||!decoded_frames||
        state.renderer->media_frames_submitted()==submitted_begin)
        throw std::runtime_error("Movie did not drain its decoder and submit its video");
    if(!arguments.smoke_capture.empty()&&!captured)
        throw std::runtime_error("Movie ended before its requested framebuffer capture");
    // The original movie tail synchronously stops its stream. Reset-marked
    // buffers must not be reported as having played naturally to completion.
    if(audio)audio->stop_and_retire();
    if(audio&&!audio->retired())throw std::runtime_error("Movie audio owner did not retire");
    std::cout<<"OpenRC prepared "<<label<<": decoded_frames="<<decoded_frames
        <<" submitted_frames="<<state.renderer->media_frames_submitted()-submitted_begin
        <<" audio_samples="<<clip.audio.size()/std::max(1U,clip.audio_channels)
        <<" played_audio_samples="<<(audio?audio->played_samples():0U)
        <<" audio_drained="<<(!audio||audio->finished())
        <<" audio_retired="<<(!audio||audio->retired())
        <<" decoder_drained=1 stopped="<<stopped<<'\n';
    audio.reset();decoder.reset();
    if(!drain_startup_graphics(state)) {
        if(arguments.smoke_test)throw std::runtime_error("Movie smoke closed during GPU completion");
        return false;
    }
    std::cout<<"OpenRC "<<label<<" GPU submissions: drained=1\n";
    owner.retired=true;
    return true;
}

bool run_prepared_movie(WindowState& state,HWND window,const openrc::MediaClipV1& clip,
    std::string_view label,const RuntimeArguments& arguments,
    const std::function<bool(std::uint64_t)>& stop_requested={},std::uint64_t capture_frame=200U) {
    PreparedMovieOwner owner(clip);
    return run_prepared_movie_owner(state,window,owner,label,arguments,stop_requested,capture_frame);
}

int run_prepared_startup(const HINSTANCE instance,const int show_command,
    const RuntimeArguments& arguments,const openrc::PreparedGameV2RootV1& prepared) {
    const auto shared=openrc::load_prepared_game_shared_package_v1(prepared,make_runtime_prepared_game_limits());
    const auto intro=openrc::decode_media_clip_v1(startup_resource(shared,"startup/intro","openrc.media-clip").payload);
    WindowState state;state.base_title=L"OpenRC";
    const auto window=create_runtime_window(instance,state,state.base_title);
    try {
        state.renderer=std::make_unique<openrc::runtime::D3d11Renderer>(window);
        if(!admit_startup_audio(state,shared)) {
            if(IsWindow(window))DestroyWindow(window);return 0;
        }
        if(!arguments.smoke_test){ShowWindow(window,show_command==0?SW_SHOWNORMAL:show_command);UpdateWindow(window);}
        if(!run_prepared_movie(state,window,intro,"intro",arguments)) {
            if(IsWindow(window))DestroyWindow(window);
            return 0;
        }
        state.frontend_intro_retired=true;
        if(arguments.smoke_test&&arguments.smoke_stage==L"media-library") {
            // Explicit diagnostic: exercise admitted presentation resources on
            // this device. It does not execute or acknowledge the normal New
            // Game state/audio/I/O barriers, nor claim that complete flow works.
            const auto timeline=openrc::decode_scene_timeline_v1(
                startup_resource(shared,"frontend/background/timeline","openrc.scene-timeline").payload);
            state.frontend_display_width=timeline.display_aspect_numerator;
            state.frontend_display_height=timeline.display_aspect_denominator;
            const auto program=openrc::decode_frontend_sequence_v1(
                startup_resource(shared,"frontend/new-game-sequence","openrc.frontend-sequence").payload);
            unsigned movies=0,cards=0,fades=0;
            bool presentation_cleanup=false;
            for(const auto& cue:program.cues) {
                if(cue.consumer_key=="transition/cleanup") {
                    if(!state.prepared_level||!state.prepared_level->poll())
                        throw std::runtime_error("Diagnostic presentation cleanup precedes completed level I/O");
                    const auto frozen=state.renderer->capture_frame_rgba();
                    if(!drain_startup_graphics(state,true))
                        throw std::runtime_error("Diagnostic closed during presentation cleanup");
                    if(state.renderer->capture_frame_rgba()!=frozen)
                        throw std::runtime_error("Presentation cleanup changed the final framebuffer");
                    presentation_cleanup=true;
                    std::cout<<"OpenRC presentation cleanup diagnostic: completion_owner_retired=1 frozen_frame_preserved=1\n";
                    continue;
                }
                if(cue.kind==openrc::FrontendSequenceCueKindV1::start_level_load) {
                    if(state.prepared_level)throw std::runtime_error("Diagnostic repeated its prepared level load");
                    state.prepared_level=std::make_unique<PreparedLevelLoad>(prepared,cue.level_id);
                    std::cout<<"OpenRC diagnostic level load: started=1 before_card="<<cards<<" level="<<cue.level_id<<'\n';
                    continue;
                }
                if(cue.kind==openrc::FrontendSequenceCueKindV1::await_level_load) {
                    if(!state.prepared_level)throw std::runtime_error("Diagnostic level wait has no started load");
                    while(!state.prepared_level->poll())
                        if(!wait_startup_until(state,std::chrono::steady_clock::now()+std::chrono::milliseconds(1)))
                            throw std::runtime_error("Diagnostic closed during final level I/O wait");
                    continue;
                }
                if(cue.kind!=openrc::FrontendSequenceCueKindV1::loading_overlay&&
                    cue.kind!=openrc::FrontendSequenceCueKindV1::media&&
                    cue.kind!=openrc::FrontendSequenceCueKindV1::fade)continue;
                if(cue.resource_index==UINT32_MAX)continue; // Earlier profile has no independent fade resource.
                const auto& reference=program.resources.at(cue.resource_index);
                const auto& resource=startup_resource(shared,reference.resource_id,reference.resource_type);
                if(openrc::prepared_content_sha256_v1(resource.payload)!=reference.payload_sha256)
                    throw std::runtime_error("Presentation diagnostic resource digest differs");
                if(cue.kind==openrc::FrontendSequenceCueKindV1::loading_overlay) {
                    PreparedLoadingCard card(cue.resource_index,resource.payload);
                    const auto begin=std::chrono::steady_clock::now();
                    auto duration=cue.updates;
                    bool load_pending=bool(state.prepared_level);
                    for(std::uint32_t frame=0;frame<duration;++frame) {
                        card.draw(frame,duration);
                        if(!present_loading_card(state,card))throw std::runtime_error("Loading diagnostic closed before completion");
                        if(frame==67U) {
                            const auto rgba=state.renderer->capture_frame_rgba();RECT client{};
                            if(!GetClientRect(window,&client))throw std::runtime_error("Cannot read loading capture dimensions");
                            openrc::runtime::MediaVideoFrameV1 expected;
                            expected.width=card.presentation.library.canvas_width;expected.height=card.presentation.library.canvas_height;
                            expected.rgba=card.rgba;
                            openrc::MediaClipV1 geometry;geometry.display_aspect_numerator=state.frontend_display_width;
                            geometry.display_aspect_denominator=state.frontend_display_height;
                            verify_media_capture(expected,rgba,static_cast<std::uint32_t>(client.right),static_cast<std::uint32_t>(client.bottom),geometry);
                            if(!arguments.smoke_capture.empty()) {
                                auto output=arguments.smoke_capture;output+=L".card-"+std::to_wstring(cards)+L".ppm";
                                save_startup_capture(output,rgba,static_cast<std::uint32_t>(client.right),static_cast<std::uint32_t>(client.bottom));
                            }
                        }
                        if(!wait_startup_until(state,begin+std::chrono::nanoseconds(
                            (std::uint64_t(frame)+1U)*1'000'000'000U/program.updates_per_second)))
                            throw std::runtime_error("Loading diagnostic closed before its last presentation wait");
                        if(load_pending) {
                            load_pending=!state.prepared_level->poll();
                            if(load_pending) {
                                const auto extended=std::uint64_t(frame)+cue.pending_load_extension;
                                if(extended>program.max_loading_updates)throw std::runtime_error("Diagnostic loading exceeded its update bound");
                                duration=std::max(duration,static_cast<std::uint32_t>(extended));
                            }
                        }
                    }
                    std::cout<<"OpenRC loading card diagnostic: card="<<cards++<<" presented_updates="<<duration<<'\n';
                } else if(cue.kind==openrc::FrontendSequenceCueKindV1::fade) {
                    const auto fade=openrc::decode_frame_color_transfer_sequence_v1(resource.payload);
                    if(fade.transfers.size()!=cue.updates||fade.updates_per_second!=program.updates_per_second)
                        throw std::runtime_error("Diagnostic fade timing differs");
                    if(!run_frame_color_transfers(state,window,fade,true))throw std::runtime_error("Feedback diagnostic closed before completion");
                    ++fades;
                } else {
                    const auto clip=openrc::decode_media_clip_v1(resource.payload);
                    auto movie_arguments=arguments;
                    if(!movie_arguments.smoke_capture.empty())movie_arguments.smoke_capture+=L".movie-"+std::to_wstring(movies)+L".ppm";
                    if(!run_prepared_movie(state,window,clip,reference.resource_id,movie_arguments))
                        throw std::runtime_error("Movie library diagnostic ended before completion");
                    ++movies;
                }
            }
            const auto clip=openrc::decode_media_clip_v1(startup_resource(shared,"new-game/movie-0","openrc.media-clip").payload);
            auto stopped_arguments=arguments;
            if(!stopped_arguments.smoke_capture.empty())stopped_arguments.smoke_capture+=L".stopped.ppm";
            if(!run_prepared_movie(state,window,clip,"new-game/movie-0-stop",stopped_arguments,
                [](std::uint64_t frames){return frames>=100U;},50U))
                throw std::runtime_error("Interrupted movie diagnostic ended before retirement");
            if(movies!=3U||cards!=3U||!presentation_cleanup)
                throw std::runtime_error("Presentation diagnostic did not reach three cards/movies and final cleanup");
            std::cout<<"OpenRC media library diagnostic: three_complete_movies=1 cards="<<cards
                <<" fades="<<fades<<" interrupted_retirement=1 presentation_cleanup=1 normal_sequence_barriers_executed=0\n";
            DestroyWindow(window);return 0;
        }
        if(arguments.smoke_test&&arguments.smoke_stage==L"intro"){DestroyWindow(window);return 0;}
        const auto completed=run_post_intro(state,window,shared,arguments,prepared);
        if(IsWindow(window))DestroyWindow(window);
        if(arguments.smoke_test&&!completed)throw std::runtime_error("Startup smoke closed before its selected stage completed");
        return 0;
    } catch(...) {
        if(IsWindow(window))DestroyWindow(window);
        throw;
    }
}

constexpr wchar_t kUsageText[] =
    L"OpenRC runtime\n\n"
    L"Required prepared-game inputs:\n"
    L"  --prepared-root <prepared game directory>\n"
    L"  With no level argument, begin the prepared original startup.\n"
    L"  The startup requires the current installation profile (re-run Prepare game in the launcher).\n\n"
    L"Optional:\n"
    L"  --level <level ID> - direct developer level load\n"
    L"  --smoke-test - run hidden graphical/gameplay smoke and exit\n"
    L"  --smoke-stage intro|post-intro|frontend-background|frontend-menu|frontend-dialog|new-game-request - startup smoke extent\n"
    L"  --smoke-stage media-library - separate movie playback/retirement diagnostic\n"
    L"  --smoke-stage frontend-exit - verify New Game frontend resource retirement\n"
    L"  --smoke-stage new-game-sequence - present New Game and install level state, stopping before world entry\n"
    L"  --smoke-stage frontend-audio-retirement - verify admitted New Game sound owners without acknowledging preparation\n"
    L"  --help - show this help\n\n"
    L"Controls:\n"
    L"  W/A/S/D - move relative to the camera\n"
    L"  arrow keys - rotate/pitch camera\n"
    L"  Space - jump\n"
    L"  F or left mouse button - primary action\n"
    L"  R - reset to checkpoint\n"
    L"  XInput left/right sticks - analog move/camera\n"
    L"  XInput A/X - jump/primary action\n";

} // namespace

int WINAPI wWinMain(
    const HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    const int show_command) {
    bool suppress_error_ui = false;
    try {
        std::cout << std::unitbuf;
        std::clog << std::unitbuf;
        const auto arguments = parse_arguments();
        suppress_error_ui = arguments.smoke_test;
        if (arguments.show_help) {
            MessageBoxW(
                nullptr,
                kUsageText,
                kApplicationName,
                MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        const auto filesystem_limits = make_runtime_prepared_game_limits();
        const auto prepared = openrc::load_prepared_game_v2_root_v1(
            arguments.prepared_root, filesystem_limits);
        if (!arguments.explicit_level) {
            require_current_installation_profile(prepared);
            return run_prepared_startup(instance,show_command,arguments,prepared);
        }
        const auto resolved =
            openrc::load_resolved_prepared_game_level_package_v1(
                prepared,
                arguments.level_id,
                std::span<
                    const openrc::ExplicitLevelPackageOverlayBytesV1>{},
                filesystem_limits);
        auto level_content = openrc::game::load_runtime_level_content_v1(
            resolved, make_runtime_level_content_limits());

        WindowState state;
        state.base_title = make_runtime_window_title(level_content);
        const auto window = create_runtime_window(
            instance,
            state,
            state.base_title);
        try {
            const auto player_actor =
                openrc::game::resolve_runtime_player_actor_v1(
                    level_content, 0U);
            const auto world_actors =
                openrc::game::resolve_runtime_world_actors_v1(level_content);
            if (player_actor) {
                state.gameplay_actor = true;
                const auto& library = *level_content.actor_library;
                const auto& player_rig =
                    library.rigs[player_actor->actor_rig_index];
                state.renderer =
                    std::make_unique<openrc::runtime::D3d11Renderer>(
                        window,
                        level_content.render_scene,
                        library,
                        *player_actor,
                        world_actors);
                state.gameplay_world_actor_ids.reserve(world_actors.size());
                for (const auto& actor : world_actors) {
                    state.gameplay_world_actor_ids.push_back(
                        actor.authored_id);
                }
                apply_runtime_world_actor_initial_poses(
                    *state.renderer, level_content, world_actors);
                const auto animation_profile =
                    make_runtime_player_animation_profile();
                if (level_content.actor_animation_bank &&
                    has_complete_runtime_player_animation(
                        *level_content.actor_animation_bank,
                        animation_profile)) {
                    state.gameplay_animation = std::make_unique<
                        openrc::game::RuntimePlayerAnimationV1>(
                            *level_content.actor_animation_bank,
                            player_rig,
                            animation_profile,
                            make_runtime_actor_animation_playback_limits());
                    state.renderer->set_gameplay_actor_pose(
                        state.gameplay_animation->palette());
                }
            } else {
                state.renderer =
                    std::make_unique<openrc::runtime::D3d11Renderer>(
                        window, level_content.render_scene);
            }
            openrc::game::RuntimeGameplaySessionOptionsV1 gameplay_options;
            if (level_content.entity_scene &&
                (level_content.gameplay_scene ||
                 level_content.destructible_scene)) {
                state.gameplay_render_bindings =
                    level_content.entity_scene->render_bindings;
                if (level_content.gameplay_scene) {
                    state.gameplay_collectible_ids.reserve(
                        level_content.gameplay_scene->collectibles.size());
                    for (const auto& collectible :
                         level_content.gameplay_scene->collectibles) {
                        state.gameplay_collectible_ids.push_back(
                            collectible.authored_id);
                    }
                }
                if (level_content.destructible_scene) {
                    state.gameplay_destructible_ids.reserve(
                        level_content.destructible_scene->destructibles.size());
                    for (const auto& destructible :
                         level_content.destructible_scene->destructibles) {
                        state.gameplay_destructible_ids.push_back(
                            destructible.authored_id);
                    }
                }
                if (arguments.smoke_test &&
                    level_content.destructible_scene &&
                    !level_content.destructible_scene->destructibles.empty()) {
                    state.gameplay_smoke_target = make_gameplay_smoke_target(
                        *level_content.entity_scene,
                        level_content.destructible_scene
                            ->destructibles.front());
                } else if (arguments.smoke_test &&
                           level_content.gameplay_scene &&
                           !level_content.gameplay_scene
                                ->collectibles.empty()) {
                    state.gameplay_smoke_target = make_gameplay_smoke_target(
                        *level_content.entity_scene,
                        level_content.gameplay_scene->collectibles.front());
                }
                openrc::GameplaySceneV1 gameplay_scene;
                gameplay_scene.level_id =
                    level_content.entity_scene->level_id;
                if (level_content.gameplay_scene) {
                    gameplay_scene =
                        std::move(*level_content.gameplay_scene);
                }
                gameplay_options.entity_gameplay =
                    openrc::game::RuntimeGameplayEntityContentV1{
                        std::move(*level_content.entity_scene),
                        std::move(gameplay_scene),
                        openrc::game::
                            make_runtime_entity_gameplay_limits_v1(),
                        std::move(level_content.destructible_scene),
                    };
            }
            state.gameplay = std::make_unique<
                openrc::game::RuntimeGameplaySessionV1>(
                    std::move(level_content.foundation),
                    std::move(gameplay_options));
            RECT client_rectangle{};
            if (GetClientRect(window, &client_rectangle) == FALSE) {
                throw std::runtime_error(
                    "GetClientRect failed while initializing gameplay");
            }
            const auto client_width = static_cast<std::uint32_t>(
                std::max<LONG>(
                    0,
                    client_rectangle.right - client_rectangle.left));
            const auto client_height = static_cast<std::uint32_t>(
                std::max<LONG>(
                    0,
                    client_rectangle.bottom - client_rectangle.top));
            const auto player = state.gameplay->snapshot().player;
            state.gameplay_camera.emplace(
                make_gameplay_camera_profile(
                    gameplay_aspect_ratio(client_width, client_height)),
                openrc::game::ThirdPersonCameraStateV1{
                    canonical_gameplay_yaw(player.facing_yaw_radians),
                    22.0 * std::numbers::pi_v<double> / 180.0,
                    6.0,
                });
            update_gameplay_player_presentation(state, player);
            synchronize_gameplay_entity_presentation(state);
            refresh_window_title(window, state);
        } catch (...) {
            DestroyWindow(window);
            throw;
        }

        if (arguments.smoke_test) {
            if (state.gameplay) {
                auto initial_item_total = UINT64_C(0);
                if (state.gameplay_smoke_target) {
                    auto checkpoint =
                        state.gameplay->player().snapshot().checkpoint;
                    checkpoint.checkpoint_id =
                        state.gameplay_smoke_target->authored_id;
                    checkpoint.facing_yaw_radians = 0.0;
                    checkpoint.feet_position =
                        state.gameplay_smoke_target->world_target;
                    if (state.gameplay_smoke_target->kind ==
                        GameplaySmokeTargetKindV1::destructible) {
                        const auto& combat = state.gameplay->profile().combat;
                        checkpoint.feet_position.x -=
                            (combat.forward_start + combat.forward_end) * 0.5;
                        checkpoint.feet_position.z -= combat.vertical_offset;
                        initial_item_total = state.gameplay->item_total(
                            state.gameplay_smoke_target->item_key);
                    }
                    state.gameplay->set_checkpoint(checkpoint, true);
                    update_gameplay_player_presentation(
                        state, state.gameplay->player().snapshot());
                    synchronize_gameplay_entity_presentation(state);

                    const auto render_instance_id =
                        state.gameplay_smoke_target->render_instance_id;
                    if (!state.renderer->render_instance_enabled(
                            render_instance_id)) {
                        throw std::runtime_error(
                            "The graphical smoke target was hidden before "
                            "gameplay could exercise it");
                    }
                    static_cast<void>(state.renderer->render());
                    if (!state.renderer
                             ->last_frame_render_instance_submitted(
                                 render_instance_id)) {
                        throw std::runtime_error(
                            "The graphical smoke did not submit its visible "
                            "real mounted target to D3D11");
                    }

                    if (state.gameplay_smoke_target->kind ==
                            GameplaySmokeTargetKindV1::destructible &&
                        !update_gameplay_key(state, 'F', true)) {
                        throw std::logic_error(
                            "The graphical smoke could not submit primary "
                            "action");
                    }
                }
                const auto smoke_frame_time =
                    std::chrono::steady_clock::now();
                state.previous_gameplay_frame =
                    smoke_frame_time - std::chrono::milliseconds(
                        state.gameplay_smoke_target &&
                                state.gameplay_smoke_target->kind ==
                                    GameplaySmokeTargetKindV1::destructible
                            ? 100
                            : 20);
                advance_gameplay_frame(window, state, smoke_frame_time);
                if (state.gameplay_smoke_target) {
                    const auto* const entity_gameplay =
                        state.gameplay->entity_gameplay();
                    const auto final_item_total = state.gameplay->item_total(
                        state.gameplay_smoke_target->item_key);
                    if (state.gameplay_smoke_target->kind ==
                        GameplaySmokeTargetKindV1::destructible) {
                        static_cast<void>(
                            update_gameplay_key(state, 'F', false));
                        if (entity_gameplay == nullptr ||
                            !entity_gameplay->destroyed(
                                state.gameplay_smoke_target->authored_id) ||
                            final_item_total < initial_item_total ||
                            final_item_total - initial_item_total <
                                state.gameplay_smoke_target->amount ||
                            state.renderer->render_instance_enabled(
                                state.gameplay_smoke_target
                                    ->render_instance_id)) {
                            throw std::runtime_error(
                                "The graphical destructible smoke did not "
                                "destroy, credit, and hide its real mounted "
                                "target");
                        }
                    } else if (entity_gameplay == nullptr ||
                               !entity_gameplay->collected(
                                   state.gameplay_smoke_target->authored_id) ||
                               final_item_total <
                                   state.gameplay_smoke_target->amount ||
                               state.renderer->render_instance_enabled(
                                   state.gameplay_smoke_target
                                       ->render_instance_id)) {
                        throw std::runtime_error(
                            "The graphical collectible smoke did not collect, "
                            "credit, and hide its real mounted target");
                    }
                }
            }
            static_cast<void>(state.renderer->render());
            if (state.gameplay_smoke_target &&
                state.renderer->last_frame_render_instance_submitted(
                    state.gameplay_smoke_target->render_instance_id)) {
                throw std::runtime_error(
                    "The graphical smoke resubmitted its hidden real mounted "
                    "target after gameplay removed it");
            }
            for (const auto authored_id : state.gameplay_world_actor_ids) {
                if (state.renderer->world_actor_enabled(authored_id) &&
                    !state.renderer->last_frame_world_actor_submitted(
                        authored_id)) {
                    throw std::runtime_error(
                        "The graphical smoke did not submit an enabled mounted world actor");
                }
            }
            DestroyWindow(window);
            return 0;
        }

        ShowWindow(window, show_command == 0 ? SW_SHOWNORMAL : show_command);
        UpdateWindow(window);

        MSG message{};
        if (state.gameplay) {
            state.previous_gameplay_frame =
                std::chrono::steady_clock::now();
            bool running = true;
            while (running) {
                while (PeekMessageW(
                           &message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
                    if (message.message == WM_QUIT) {
                        running = false;
                        break;
                    }
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                if (!running) {
                    break;
                }
                if (IsIconic(window) != FALSE) {
                    if (WaitMessage() == FALSE) {
                        throw std::runtime_error("WaitMessage failed");
                    }
                    state.previous_gameplay_frame =
                        std::chrono::steady_clock::now();
                    continue;
                }
                poll_gameplay_gamepad(state);
                advance_gameplay_frame(
                    window, state, std::chrono::steady_clock::now());
                if (!state.renderer->render()) {
                    constexpr DWORD kOccludedWaitMilliseconds = 16U;
                    const auto wait_result = MsgWaitForMultipleObjectsEx(
                        0U,
                        nullptr,
                        kOccludedWaitMilliseconds,
                        QS_ALLINPUT,
                        MWMO_INPUTAVAILABLE);
                    if (wait_result == WAIT_FAILED) {
                        throw std::runtime_error(
                            "Waiting for an occluded runtime window failed");
                    }
                }
            }
        }
        if (state.fatal_error) {
            throw std::runtime_error(*state.fatal_error);
        }
        return static_cast<int>(message.wParam);
    } catch (const std::exception& error) {
        // A GUI process may have no usable inherited standard-error handle.
        // Preserve the failure independently of the launching shell or UI.
        try {
            const auto logs = openrc::application_paths().logs;
            std::filesystem::create_directories(logs);
            std::ofstream out(logs / L"runtime-last-error.log", std::ios::trunc);
            out << "pid=" << GetCurrentProcessId() << '\n' << error.what() << '\n';
        } catch (...) {
            // Logging failure must not replace the original runtime error.
        }
        if (suppress_error_ui) {
            std::cerr << "OpenRC graphical smoke failed: " << error.what()
                      << '\n';
            OutputDebugStringW(utf8_to_wide(error.what()).c_str());
            return 1;
        }
        MessageBoxW(
            nullptr,
            utf8_to_wide(error.what()).c_str(),
            kApplicationName,
            MB_OK | MB_ICONERROR);
        return 1;
    }
}
