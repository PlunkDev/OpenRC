#include "d3d11_renderer.hpp"

#include "openrc/prepared_game_v2_fs.hpp"
#include "openrc/runtime_gameplay.hpp"
#include "openrc/runtime_level_content.hpp"
#include "openrc/runtime_player_actor.hpp"
#include "openrc/runtime_player_animation.hpp"
#include "openrc/third_person_camera.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
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

struct WindowState {
    std::unique_ptr<openrc::runtime::D3d11Renderer> renderer;
    std::unique_ptr<openrc::game::RuntimeGameplaySessionV1> gameplay;
    std::unique_ptr<openrc::game::RuntimePlayerAnimationV1>
        gameplay_animation;
    std::optional<openrc::game::ThirdPersonCameraV1> gameplay_camera;
    GameplayKeyboardState gameplay_keyboard;
    std::uint32_t gameplay_pending_pressed_buttons = 0U;
    std::uint32_t gameplay_pending_released_buttons = 0U;
    std::uint32_t gameplay_submitted_buttons = 0U;
    std::vector<openrc::EntityRenderBindingV1> gameplay_render_bindings;
    std::vector<std::uint32_t> gameplay_collectible_ids;
    std::vector<std::uint32_t> gameplay_destructible_ids;
    std::uint64_t gameplay_collected_count = 0U;
    std::uint64_t gameplay_destroyed_count = 0U;
    std::optional<GameplaySmokeTargetV1> gameplay_smoke_target;
    std::chrono::steady_clock::time_point previous_gameplay_frame{};
    std::optional<std::string> fatal_error;
    std::wstring base_title;
    bool gameplay_actor = false;
};

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
        32U,
        16U,
        256U,
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
        "actors/ratchet/idle",
        "actors/ratchet/walk",
        "actors/ratchet/run",
        0.25,
        2.0,
        60U,
    };
}

[[nodiscard]] constexpr openrc::ActorAnimationPlaybackLimitsV1
make_runtime_player_animation_playback_limits() {
    return openrc::ActorAnimationPlaybackLimitsV1{
        8U,
        8U,
        openrc::game::kRuntimePlayerActorMaximumJointsV1,
        1.0e-8,
        1'000'000.0F,
    };
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
            saw_level = true;
        } else {
            throw std::runtime_error("The runtime received an unknown argument");
        }
    }

    if (!result.show_help && (!saw_prepared_root || !saw_level)) {
        throw std::runtime_error(
            "Both --prepared-root and --level are required");
    }
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
make_gameplay_input_sample(const GameplayKeyboardState& keyboard) noexcept {
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
    const auto buttons_before =
        make_gameplay_input_sample(state.gameplay_keyboard).held_buttons;
    if (!set_gameplay_key(state.gameplay_keyboard, key, held)) {
        return false;
    }
    const auto buttons_after =
        make_gameplay_input_sample(state.gameplay_keyboard).held_buttons;
    state.gameplay_pending_pressed_buttons |=
        buttons_after & ~buttons_before;
    state.gameplay_pending_released_buttons |=
        buttons_before & ~buttons_after;
    return true;
}

void update_gameplay_primary_pointer(
    WindowState& state,
    const bool held) noexcept {
    const auto buttons_before =
        make_gameplay_input_sample(state.gameplay_keyboard).held_buttons;
    state.gameplay_keyboard.primary_action_pointer = held;
    const auto buttons_after =
        make_gameplay_input_sample(state.gameplay_keyboard).held_buttons;
    state.gameplay_pending_pressed_buttons |=
        buttons_after & ~buttons_before;
    state.gameplay_pending_released_buttons |=
        buttons_before & ~buttons_after;
}

void release_gameplay_input(WindowState& state) noexcept {
    state.gameplay_keyboard = {};
    state.gameplay_pending_pressed_buttons = 0U;
    state.gameplay_pending_released_buttons =
        state.gameplay_submitted_buttons;
}

void submit_pending_gameplay_input(WindowState& state) {
    if (!state.gameplay) {
        return;
    }
    const auto final_sample =
        make_gameplay_input_sample(state.gameplay_keyboard);
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

[[nodiscard]] std::int16_t quantize_gameplay_axis(const double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(
            "The gameplay camera produced a non-finite movement axis");
    }
    const auto scaled = std::clamp(value, -1.0, 1.0) *
                        openrc::game::kGameInputAxisMagnitudeV1;
    const auto rounded = std::llround(scaled);
    return static_cast<std::int16_t>(std::clamp<long long>(
        rounded,
        -openrc::game::kGameInputAxisMagnitudeV1,
        openrc::game::kGameInputAxisMagnitudeV1));
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
                       const double fixed_delta_seconds) {
            next_camera.fixed_update(
                {input.axes.look_x, input.axes.look_y, 0},
                fixed_delta_seconds);
            const auto movement = next_camera.map_movement(
                input.axes.move_x, input.axes.move_y);
            return openrc::game::RuntimeMovementAxesV1{
                quantize_gameplay_axis(movement.move_x),
                quantize_gameplay_axis(movement.move_y),
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
            make_gameplay_input_sample(
                state.gameplay_keyboard).held_buttons;
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
        if (state != nullptr && state->gameplay) {
            release_gameplay_input(*state);
            return 0;
        }
        break;
    case WM_ACTIVATEAPP:
        if (state != nullptr && state->gameplay && w_param == FALSE) {
            release_gameplay_input(*state);
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

constexpr wchar_t kUsageText[] =
    L"OpenRC runtime\n\n"
    L"Required prepared-game inputs:\n"
    L"  --prepared-root <prepared game directory>\n"
    L"  --level <level ID>\n\n"
    L"Optional:\n"
    L"  --smoke-test - run hidden graphical/gameplay smoke and exit\n"
    L"  --help - show this help\n\n"
    L"Controls:\n"
    L"  W/A/S/D - move relative to the camera\n"
    L"  arrow keys - rotate/pitch camera\n"
    L"  Space - jump\n"
    L"  F or left mouse button - primary action\n"
    L"  R - reset to checkpoint\n";

} // namespace

int WINAPI wWinMain(
    const HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    const int show_command) {
    bool suppress_error_ui = false;
    try {
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
            if (player_actor) {
                state.gameplay_actor = true;
                const auto& library = *level_content.actor_library;
                const auto& player_rig =
                    library.rigs[player_actor->actor_rig_index];
                state.renderer =
                    std::make_unique<openrc::runtime::D3d11Renderer>(
                        window,
                        level_content.render_scene,
                        player_rig.rig,
                        library.models[player_actor->actor_model_index],
                        player_actor->model_to_entity);
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
                            make_runtime_player_animation_playback_limits());
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
        if (suppress_error_ui) {
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
