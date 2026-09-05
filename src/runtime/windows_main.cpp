#include "d3d11_renderer.hpp"
#include "level_scene_recovery.hpp"

#include "openrc/prepared_game_v2_fs.hpp"
#include "openrc/runtime_level_content.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shellapi.h>
#include <windows.h>
#include <windowsx.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"PlunkDev.OpenRC.Runtime.Window";
constexpr wchar_t kApplicationName[] = L"OpenRC native level viewer";

struct RuntimeArguments {
    std::optional<std::filesystem::path> prepared_root;
    std::filesystem::path disc_image;
    std::filesystem::path boot_executable;
    std::uint32_t level_id = 0U;
    std::uint64_t record_index = 0U;
    std::uint16_t entrypoint = 16U;
    bool all_records = false;
    bool smoke_test = false;
    bool show_help = false;
};

struct WindowState {
    std::unique_ptr<openrc::runtime::D3d11Renderer> renderer;
    std::optional<std::string> fatal_error;
    std::wstring base_title;
    POINT previous_pointer{};
    bool orbit_drag_active = false;
    bool native_content = false;
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
    bool saw_disc_image = false;
    bool saw_boot_executable = false;
    bool saw_prepared_root = false;
    bool saw_level = false;
    bool saw_record = false;
    bool saw_entrypoint = false;

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
        if (index + 1 >= argument_count) {
            throw std::runtime_error(
                "A named runtime argument is missing its value");
        }
        const std::wstring_view value(raw_arguments[++index]);
        if (name == L"--prepared-root") {
            if (saw_prepared_root || value.empty()) {
                throw std::runtime_error(
                    "--prepared-root must be supplied exactly once");
            }
            result.prepared_root = std::filesystem::path(value);
            saw_prepared_root = true;
        } else if (name == L"--disc-image") {
            if (saw_disc_image || value.empty()) {
                throw std::runtime_error(
                    "--disc-image must be supplied exactly once");
            }
            result.disc_image = std::filesystem::path(value);
            saw_disc_image = true;
        } else if (name == L"--boot-executable") {
            if (saw_boot_executable || value.empty()) {
                throw std::runtime_error(
                    "--boot-executable must be supplied exactly once");
            }
            result.boot_executable = std::filesystem::path(value);
            saw_boot_executable = true;
        } else if (name == L"--level") {
            const auto parsed = parse_unsigned_decimal(value);
            if (saw_level || !parsed ||
                *parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error(
                    "--level must be one unsigned decimal value");
            }
            result.level_id = static_cast<std::uint32_t>(*parsed);
            saw_level = true;
        } else if (name == L"--record") {
            if (saw_record) {
                throw std::runtime_error(
                    "--record must be supplied at most once");
            }
            if (value == L"all") {
                result.all_records = true;
            } else {
                const auto parsed = parse_unsigned_decimal(value);
                if (!parsed) {
                    throw std::runtime_error(
                        "--record must be one unsigned decimal value or all");
                }
                result.record_index = *parsed;
            }
            saw_record = true;
        } else if (name == L"--entry-pair") {
            const auto parsed = parse_unsigned_decimal(value);
            if (saw_entrypoint || !parsed ||
                *parsed > std::numeric_limits<std::uint16_t>::max()) {
                throw std::runtime_error(
                    "--entry-pair must be one unsigned decimal value");
            }
            result.entrypoint = static_cast<std::uint16_t>(*parsed);
            saw_entrypoint = true;
        } else {
            throw std::runtime_error("The runtime received an unknown argument");
        }
    }

    if (!result.show_help) {
        if (saw_prepared_root) {
            if (saw_disc_image || saw_boot_executable) {
                throw std::runtime_error(
                    "--prepared-root cannot be combined with disc/ELF inputs");
            }
            if (saw_record || saw_entrypoint) {
                throw std::runtime_error(
                    "record and entry-pair controls are only available in "
                    "the legacy recovery viewer");
            }
        } else if (!saw_disc_image || !saw_boot_executable) {
        throw std::runtime_error(
                "Use --prepared-root, or supply both --disc-image and "
                "--boot-executable for the legacy recovery viewer");
        }
    }
    return result;
}

void refresh_window_title(const HWND window, const WindowState& state) {
    if (!state.renderer) {
        return;
    }
    const auto suffix = state.native_content
                            ? L" - native package"
                            : (state.renderer->is_3d_view()
        ? L" - recovered level 3D (debug orbit)"
        : L" - decoded GS output (2D)");
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
                InvalidateRect(window, nullptr, FALSE);
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (state != nullptr && state->renderer) {
            try {
                constexpr float kOrbitStep = 0.0872664626F;
                switch (w_param) {
                case VK_LEFT:
                    state->renderer->orbit(-kOrbitStep, 0.0F);
                    break;
                case VK_RIGHT:
                    state->renderer->orbit(kOrbitStep, 0.0F);
                    break;
                case VK_UP:
                    state->renderer->orbit(0.0F, kOrbitStep);
                    break;
                case VK_DOWN:
                    state->renderer->orbit(0.0F, -kOrbitStep);
                    break;
                case VK_ADD:
                case VK_OEM_PLUS:
                    state->renderer->zoom(1.0F);
                    break;
                case VK_SUBTRACT:
                case VK_OEM_MINUS:
                    state->renderer->zoom(-1.0F);
                    break;
                case 'R':
                    state->renderer->reset_camera();
                    break;
                case VK_TAB:
                    state->renderer->toggle_view_mode();
                    refresh_window_title(window, *state);
                    break;
                default:
                    return DefWindowProcW(window, message, w_param, l_param);
                }
                InvalidateRect(window, nullptr, FALSE);
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state != nullptr && state->renderer) {
            try {
                const auto delta = static_cast<float>(
                    GET_WHEEL_DELTA_WPARAM(w_param));
                state->renderer->zoom(delta / WHEEL_DELTA);
                InvalidateRect(window, nullptr, FALSE);
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (state != nullptr && state->renderer &&
            state->renderer->is_3d_view()) {
            state->previous_pointer = {
                static_cast<LONG>(GET_X_LPARAM(l_param)),
                static_cast<LONG>(GET_Y_LPARAM(l_param)),
            };
            state->orbit_drag_active = true;
            SetCapture(window);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state != nullptr && state->renderer &&
            state->orbit_drag_active && (w_param & MK_LBUTTON) != 0U) {
            const POINT current{
                static_cast<LONG>(GET_X_LPARAM(l_param)),
                static_cast<LONG>(GET_Y_LPARAM(l_param)),
            };
            const auto delta_x = current.x - state->previous_pointer.x;
            const auto delta_y = current.y - state->previous_pointer.y;
            state->previous_pointer = current;
            try {
                state->renderer->orbit(
                    static_cast<float>(delta_x) * 0.008F,
                    static_cast<float>(-delta_y) * 0.008F);
                InvalidateRect(window, nullptr, FALSE);
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (state != nullptr && state->orbit_drag_active) {
            state->orbit_drag_active = false;
            if (GetCapture() == window) {
                ReleaseCapture();
            }
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (state != nullptr) {
            state->orbit_drag_active = false;
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

[[nodiscard]] std::wstring make_window_title(
    const RuntimeArguments& arguments,
    const openrc::runtime::LevelSceneRecoveryResultV1 & geometry) {
    if (arguments.all_records) {
        if (arguments.entrypoint !=
            openrc::kSceneBlockSourceGeometryEntrypointV1) {
            return std::wstring(L"OpenRC - Level ") +
                std::to_wstring(arguments.level_id) +
                L", all records - GS " +
                std::to_wstring(geometry.raster_record_count) + L"/" +
                std::to_wstring(geometry.total_record_count) + L", " +
                std::to_wstring(geometry.raster.emitted_triangle_count) +
                L" triangles";
        }
        const auto non_drawing_record_count =
            geometry.total_record_count - geometry.raster_record_count;
        const auto source_triangle_count = geometry.terrain_triangle_count;
        return std::wstring(L"OpenRC - Level ") +
            std::to_wstring(arguments.level_id) +
            L", all records - source " +
            std::to_wstring(geometry.source_record_count) + L", GS-only " +
            std::to_wstring(geometry.unavailable_source_record_count) +
            L", non-drawing " +
            std::to_wstring(non_drawing_record_count) + L", triangles " +
            std::to_wstring(source_triangle_count) + L" source + " +
            std::to_wstring(geometry.moby_triangle_count) +
            L" static-Moby + " +
            std::to_wstring(geometry.tie_triangle_count) +
            L" TIE, terrain-textured " +
            std::to_wstring(geometry.terrain_textured_triangle_count) + L"/" +
            std::to_wstring(source_triangle_count) + L", placements " +
            std::to_wstring(geometry.moby_rendered_placement_count) + L"/" +
            std::to_wstring(geometry.moby_placement_count) + L" Moby + " +
            std::to_wstring(geometry.tie_rendered_placement_count) + L"/" +
            std::to_wstring(geometry.tie_placement_count) + L" TIE, " +
            std::to_wstring(geometry.raster.emitted_triangle_count) + L" GS";
    }
    return std::wstring(L"OpenRC - Level ") +
        std::to_wstring(arguments.level_id) + L", record " +
        std::to_wstring(arguments.record_index) + L", entry " +
        std::to_wstring(arguments.entrypoint) + L" - " +
        std::to_wstring(geometry.raster.emitted_triangle_count) +
        L" triangles, terrain-textured " +
        std::to_wstring(geometry.terrain_textured_triangle_count) + L"/" +
        std::to_wstring(geometry.source
                            ? geometry.source->emitted_triangle_count
                            : 0U);
}

[[nodiscard]] std::wstring
make_native_window_title(const openrc::game::RuntimeLevelContentV1 &content) {
    std::uint64_t triangle_count = 0U;
    for (const auto &mesh : content.render_scene.meshes) {
        triangle_count += mesh.triangle_indices.size() / 3U;
    }
    return std::wstring(L"OpenRC - native level ") +
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
    L"OpenRC native level viewer\n\n"
    L"Prepared game (normal runtime):\n"
    L"  --prepared-root <prepared game directory>\n"
    L"  --level <level ID>\n\n"
    L"Legacy recovery viewer:\n"
    L"  --disc-image <disc.iso>\n"
    L"  --boot-executable <prepared ELF>\n\n"
    L"Optional:\n"
    L"  --level <0..18>\n"
    L"  --record <index|all>\n"
    L"  --entry-pair <6,8,10,14,16,20>\n\n"
    L"Developer verification:\n"
    L"  --smoke-test - render one hidden frame and exit\n\n"
    L"Using --record all independently executes and merges every supported "
    L"record in the selected level.\n\n"
    L"Recovered 3D debug view (entry 16):\n"
    L"  drag or arrow keys - orbit\n"
    L"  mouse wheel or +/- - zoom\n"
    L"  R - reset camera\n"
    L"  Tab - compare decoded GS output\n";

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

        std::optional<openrc::runtime::LevelSceneRecoveryResultV1>
            recovered_geometry;
        std::optional<openrc::game::RuntimeLevelContentV1> native_content;
        if (arguments.prepared_root) {
            const auto filesystem_limits = make_runtime_prepared_game_limits();
            const auto prepared = openrc::load_prepared_game_v2_root_v1(
                *arguments.prepared_root, filesystem_limits);
            const auto resolved =
                openrc::load_resolved_prepared_game_level_package_v1(
                    prepared, arguments.level_id,
                    std::span<
                        const openrc::ExplicitLevelPackageOverlayBytesV1>{},
                    filesystem_limits);
            native_content = openrc::game::load_runtime_level_content_v1(
                resolved, make_runtime_level_content_limits());
        } else {
            const openrc::runtime::LevelSceneRecoveryRequestV1 recovery_request{
                arguments.disc_image,
                arguments.boot_executable,
                arguments.level_id,
                arguments.all_records
                    ? openrc::runtime::LevelSceneRecordSelectionV1::all_records
                    : openrc::runtime::LevelSceneRecordSelectionV1::
                          single_record,
                arguments.record_index,
                arguments.entrypoint,
            };
            recovered_geometry = openrc::runtime::recover_level_scene_v1(
                recovery_request,
                openrc::runtime::make_level_scene_recovery_limits_v1(),
                openrc::runtime::make_level_scene_recovery_profile_v1());
        }

        WindowState state;
        state.native_content = native_content.has_value();
        state.base_title =
            native_content ? make_native_window_title(*native_content)
                           : make_window_title(arguments, *recovered_geometry);
        const auto window = create_runtime_window(
            instance,
            state,
            state.base_title);
        try {
            if (native_content) {
                state.renderer =
                    std::make_unique<openrc::runtime::D3d11Renderer>(
                        window, native_content->render_scene);
            } else {
                auto &geometry = *recovered_geometry;
                if (geometry.source) {
                if (geometry.tfrag_texture_bank &&
                    !geometry.terrain_material_batches.empty()) {
                    std::vector<
                        openrc::runtime::D3d11ObjectTextureSourceV1>
                        object_texture_sources;
                    object_texture_sources.reserve(2U);
                    if (geometry.moby_texture_bank &&
                        geometry.moby_first_triangle) {
                        object_texture_sources.push_back({
                            *geometry.moby_first_triangle,
                            geometry.moby_triangle_count,
                            geometry.moby_material_batches,
                            geometry.moby_texture_bank->textures,
                        });
                    }
                    if (geometry.tie_texture_bank &&
                        geometry.tie_first_triangle) {
                        object_texture_sources.push_back({
                            *geometry.tie_first_triangle,
                            geometry.tie_triangle_count,
                            geometry.tie_material_batches,
                            geometry.tie_texture_bank->textures,
                        });
                    }
                    openrc::runtime::D3d11SourceTextureSourcesV1 textures{
                        {
                            geometry.terrain_triangle_count,
                            geometry.terrain_material_batches,
                            geometry.tfrag_texture_bank->textures,
                        },
                        object_texture_sources,
                    };
                    state.renderer =
                        std::make_unique<openrc::runtime::D3d11Renderer>(
                            window,
                            geometry.raster,
                            *geometry.source,
                            textures);
                } else {
                    state.renderer =
                        std::make_unique<openrc::runtime::D3d11Renderer>(
                            window, geometry.raster, *geometry.source);
                }
            } else {
                state.renderer =
                    std::make_unique<openrc::runtime::D3d11Renderer>(
                        window, geometry.raster);
                }
            }
            refresh_window_title(window, state);
        } catch (...) {
            DestroyWindow(window);
            throw;
        }

        if (arguments.smoke_test) {
            state.renderer->render();
            DestroyWindow(window);
            return 0;
        }

        ShowWindow(window, show_command == 0 ? SW_SHOWNORMAL : show_command);
        UpdateWindow(window);

        MSG message{};
        for (;;) {
            const auto result = GetMessageW(&message, nullptr, 0U, 0U);
            if (result == -1) {
                throw std::runtime_error("GetMessageW failed");
            }
            if (result == 0) {
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
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
