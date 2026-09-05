#include "openrc/disc.hpp"
#include "openrc/disc_toc.hpp"
#include "openrc/content_api.hpp"
#include "openrc/hash.hpp"
#include "openrc/launcher_plan.hpp"
#include "openrc/native_game_prepare.hpp"
#include "openrc/paths.hpp"
#include "openrc/preparation.hpp"
#include "openrc/prepared_game_v2_fs.hpp"
#include "openrc/runtime_level_content.hpp"
#include "openrc/settings.hpp"
#include "portable_executable.hpp"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"OpenRCLauncherWindow";
constexpr wchar_t kWindowTitle[] = L"OpenRC Launcher";

constexpr int kIsoEdit = 1001;
constexpr int kBrowseButton = 1002;
constexpr int kInspectButton = 1003;
constexpr int kExtractButton = 1004;
constexpr int kPlayButton = 1005;
constexpr int kOpenDataButton = 1006;
constexpr int kStatusLabel = 1007;
constexpr int kReportEdit = 1008;

constexpr UINT kPreparationProgressMessage = WM_APP + 1;
constexpr UINT kPreparationDoneMessage = WM_APP + 2;
constexpr UINT_PTR kPreparationPollTimer = 1;

enum class PreparationOutcome {
    running,
    succeeded,
    cancelled,
    failed,
};

struct PreparationOperation {
    PreparationOperation(
        const std::uint32_t operation_id,
        std::filesystem::path source_image_path,
        const DWORD owner_ui_thread_id)
        : id(operation_id), image_path(std::move(source_image_path)),
          ui_thread_id(owner_ui_thread_id) {}

    const std::uint32_t id;
    const std::filesystem::path image_path;
    const DWORD ui_thread_id;
    std::mutex mutex;
    std::wstring latest_progress;
    bool has_progress = false;
    bool progress_message_pending = false;
    bool cancellation_requested = false;
    PreparationOutcome outcome = PreparationOutcome::running;
    std::optional<openrc::NativeGamePreparationResultV1> result;
    std::string error_message;
};

struct ReadyGame {
    std::filesystem::path prepared_root;
    openrc::PreparedContentDigestV1 manifest_sha256{};
};

HWND g_iso_edit = nullptr;
HWND g_extract_button = nullptr;
HWND g_play_button = nullptr;
HWND g_status_label = nullptr;
HWND g_report_edit = nullptr;

std::optional<std::filesystem::path> g_inspected_iso;
std::optional<ReadyGame> g_ready_game;
std::shared_ptr<PreparationOperation> g_preparation_operation;
std::jthread g_preparation_worker;
std::uint32_t g_next_operation_id = 1;
DWORD g_ui_thread_id = 0;
bool g_close_requested = false;

[[nodiscard]] std::wstring to_wide(const std::string_view value) {
    if (value.empty()) {
        return {};
    }

    const auto required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return L"<invalid UTF-8>";
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

[[nodiscard]] std::wstring control_text(const HWND control) {
    const auto length = GetWindowTextLengthW(control);
    std::wstring result(static_cast<std::size_t>(length) + 1U, L'\0');
    GetWindowTextW(control, result.data(), static_cast<int>(result.size()));
    result.resize(static_cast<std::size_t>(length));
    return result;
}

void set_status(const wchar_t* text) {
    SetWindowTextW(g_status_label, text);
}

void set_report(const std::wstring& text) {
    SetWindowTextW(g_report_edit, text.c_str());
}

[[nodiscard]] const wchar_t* preparation_phase_name(const openrc::PreparationPhase phase) {
    switch (phase) {
    case openrc::PreparationPhase::scanning:
        return L"Scanning disc";
    case openrc::PreparationPhase::hashing_image:
        return L"Hashing disc image";
    case openrc::PreparationPhase::verifying_files:
        return L"Verifying prepared files";
    case openrc::PreparationPhase::extracting_files:
        return L"Extracting game files";
    case openrc::PreparationPhase::writing_manifest:
        return L"Writing manifest";
    case openrc::PreparationPhase::finalizing:
        return L"Finalizing";
    }
    return L"Preparing game files";
}

[[nodiscard]] std::wstring format_preparation_progress(
    const openrc::PreparationProgress& progress) {
    std::wostringstream output;
    output << preparation_phase_name(progress.phase);

    if (progress.total_bytes != 0) {
        const auto percentage = static_cast<unsigned int>(std::min<long double>(
            100.0L,
            (static_cast<long double>(progress.bytes_processed) * 100.0L) /
                static_cast<long double>(progress.total_bytes)));
        output << L" - " << percentage << L"%";
    }
    if (progress.file_count != 0) {
        output << L" - file " << progress.file_index << L"/" << progress.file_count;
    }
    if (!progress.current_path.empty()) {
        output << L" - " << to_wide(progress.current_path);
    }
    return output.str();
}

[[nodiscard]] const wchar_t* native_preparation_phase_name(
    const openrc::NativeGamePreparationPhaseV1 phase) {
    using Phase = openrc::NativeGamePreparationPhaseV1;
    switch (phase) {
    case Phase::validating_inputs:
        return L"Validating source files";
    case Phase::hashing_inputs:
        return L"Hashing source files";
    case Phase::checking_existing_publication:
        return L"Checking the existing native installation";
    case Phase::loading_level_assets:
        return L"Loading level assets";
    case Phase::compiling_level_foundation:
        return L"Compiling collision and gameplay data";
    case Phase::recovering_level_scene:
        return L"Recovering the complete level scene";
    case Phase::compiling_render_scene:
        return L"Compiling native rendering data";
    case Phase::encoding_level_package:
        return L"Encoding the native level package";
    case Phase::verifying_inputs:
        return L"Verifying unchanged source files";
    case Phase::publishing:
        return L"Publishing the native installation";
    case Phase::publishing_staged:
        return L"Verifying the staged native installation";
    case Phase::publishing_commit:
        return L"Activating the native installation";
    case Phase::reusing_existing_publication:
        return L"Reusing the verified native installation";
    case Phase::compiling_player_actor:
        return L"Compiling the reusable player actor";
    case Phase::compiling_player_animation:
        return L"Compiling the player locomotion animations";
    case Phase::compiling_entity_scene:
        return L"Compiling neutral player entity bindings";
    }
    return L"Preparing the native game";
}

[[nodiscard]] std::wstring format_native_preparation_progress(
    const openrc::NativeGamePreparationProgressV1& progress) {
    std::wostringstream output;
    output << native_preparation_phase_name(progress.phase);
    if (progress.level_id != openrc::kNativeGamePreparationNoLevelV1) {
        output << L" - level " << (progress.level_id + 1U) << L"/"
               << progress.total_levels;
    } else if (progress.total_levels != 0U && progress.completed_levels != 0U) {
        output << L" - " << progress.completed_levels << L"/"
               << progress.total_levels;
    }
    return output.str();
}

void invalidate_inspected_iso() {
    g_inspected_iso.reset();
    EnableWindow(g_extract_button, FALSE);
    EnableWindow(g_play_button, g_ready_game ? TRUE : FALSE);
}

void set_preparation_controls(const HWND window, const bool preparing) {
    EnableWindow(g_iso_edit, preparing ? FALSE : TRUE);
    EnableWindow(GetDlgItem(window, kBrowseButton), preparing ? FALSE : TRUE);
    EnableWindow(GetDlgItem(window, kInspectButton), preparing ? FALSE : TRUE);
    EnableWindow(
        g_play_button,
        !preparing && g_ready_game ? TRUE : FALSE);

    SetWindowTextW(
        g_extract_button,
        preparing ? L"Cancel" : L"Prepare native game");
    EnableWindow(
        g_extract_button,
        preparing || g_inspected_iso.has_value() ? TRUE : FALSE);
}

[[nodiscard]] std::uint32_t next_operation_id() {
    const auto result = g_next_operation_id++;
    if (g_next_operation_id == 0) {
        ++g_next_operation_id;
    }
    return result;
}

[[nodiscard]] bool publish_preparation_progress(
    PreparationOperation& operation,
    std::wstring progress,
    const DWORD ui_thread_id) {
    bool should_post = false;
    {
        const std::lock_guard lock(operation.mutex);
        if (operation.cancellation_requested) {
            return false;
        }
        operation.latest_progress = std::move(progress);
        operation.has_progress = true;
        if (!operation.progress_message_pending) {
            operation.progress_message_pending = true;
            should_post = true;
        }
    }

    if (should_post && PostThreadMessageW(
            ui_thread_id,
            kPreparationProgressMessage,
            static_cast<WPARAM>(operation.id),
            0) == FALSE) {
        const std::lock_guard lock(operation.mutex);
        operation.progress_message_pending = false;
    }
    return true;
}

[[nodiscard]] bool publish_native_preparation_progress(
    const openrc::NativeGamePreparationProgressV1& progress,
    void* const context) noexcept {
    auto* const operation = static_cast<PreparationOperation*>(context);
    if (operation == nullptr) {
        return false;
    }
    try {
        return publish_preparation_progress(
            *operation,
            format_native_preparation_progress(progress),
            operation->ui_thread_id);
    } catch (...) {
        return false;
    }
}

void start_preparation(const HWND window) {
    if (g_preparation_operation || !g_inspected_iso) {
        return;
    }

    openrc::ApplicationPaths application_paths;
    std::filesystem::path games_directory;
    try {
        application_paths = openrc::application_paths();
        games_directory =
            openrc::launcher::legacy_games_root_v1(application_paths);
    } catch (const std::exception& error) {
        set_status(L"Could not determine the game data directory.");
        set_report(to_wide(std::string("Error: ") + error.what()));
        return;
    }

    const auto image_path = *g_inspected_iso;
    auto operation = std::make_shared<PreparationOperation>(
        next_operation_id(),
        image_path,
        g_ui_thread_id);
    g_preparation_operation = operation;
    set_preparation_controls(window, true);
    set_status(L"Preparing the native game...");
    set_report(
        L"OpenRC is building a reusable native installation from your disc "
        L"image. This compiles all 19 planets once; the launcher remains "
        L"responsive.");

    try {
        g_preparation_worker = std::jthread(
            [operation, image_path, application_paths, games_directory](
                const std::stop_token stop_token) {
                try {
                    const auto extracted = openrc::prepare_game_files(
                        image_path,
                        games_directory,
                        [operation, stop_token](
                            const openrc::PreparationProgress& progress) {
                            if (stop_token.stop_requested()) {
                                return false;
                            }
                            return publish_preparation_progress(
                                *operation,
                                format_preparation_progress(progress),
                                operation->ui_thread_id) &&
                                !stop_token.stop_requested();
                        });

                    if (stop_token.stop_requested()) {
                        throw openrc::PreparationCancelled();
                    }

                    const auto native_root =
                        openrc::launcher::prepared_game_v2_root_v1(
                            application_paths, extracted.image_sha256);
                    std::error_code filesystem_error;
                    std::filesystem::create_directories(
                        native_root.parent_path(), filesystem_error);
                    if (filesystem_error) {
                        throw std::runtime_error(
                            "Cannot create the native installation parent: " +
                            filesystem_error.message());
                    }

                    auto result = openrc::prepare_native_game_v1(
                        openrc::NativeGamePreparationRequestV1{
                            image_path,
                            extracted.boot_executable_path,
                            native_root,
                            openrc::launcher::parse_lowercase_sha256_hex_v1(
                                extracted.image_sha256)},
                        openrc::NativeGamePreparationControlV1{
                            &publish_native_preparation_progress,
                            operation.get()});

                    const std::lock_guard lock(operation->mutex);
                    operation->result = std::move(result);
                    operation->outcome = PreparationOutcome::succeeded;
                } catch (const openrc::PreparationCancelled&) {
                    const std::lock_guard lock(operation->mutex);
                    operation->outcome = PreparationOutcome::cancelled;
                } catch (const openrc::NativeGamePreparationCancelledV1&) {
                    const std::lock_guard lock(operation->mutex);
                    operation->outcome = PreparationOutcome::cancelled;
                } catch (const std::exception& error) {
                    const std::lock_guard lock(operation->mutex);
                    operation->error_message = error.what();
                    operation->outcome = PreparationOutcome::failed;
                } catch (...) {
                    const std::lock_guard lock(operation->mutex);
                    operation->error_message = "Unknown preparation error";
                    operation->outcome = PreparationOutcome::failed;
                }

                PostThreadMessageW(
                    operation->ui_thread_id,
                    kPreparationDoneMessage,
                    static_cast<WPARAM>(operation->id),
                    0);
            });
        SetTimer(window, kPreparationPollTimer, 250, nullptr);
    } catch (const std::exception& error) {
        g_preparation_operation.reset();
        set_preparation_controls(window, false);
        set_status(L"Could not start game-file preparation.");
        set_report(to_wide(std::string("Error: ") + error.what()));
    }
}

void cancel_preparation() {
    if (!g_preparation_operation) {
        return;
    }
    g_preparation_worker.request_stop();
    {
        const std::lock_guard lock(g_preparation_operation->mutex);
        g_preparation_operation->cancellation_requested = true;
        g_preparation_operation->progress_message_pending = false;
    }
    SetWindowTextW(g_extract_button, L"Cancelling...");
    EnableWindow(g_extract_button, FALSE);
    set_status(L"Cancelling game-file preparation...");
}

void display_latest_preparation_progress(const std::uint32_t operation_id) {
    const auto operation = g_preparation_operation;
    if (!operation || operation->id != operation_id) {
        return;
    }

    std::wstring progress;
    {
        const std::lock_guard lock(operation->mutex);
        if (!operation->has_progress || !operation->progress_message_pending) {
            return;
        }
        progress = operation->latest_progress;
        operation->progress_message_pending = false;
    }

    set_status(progress.c_str());
}

void finish_preparation(const HWND window, const std::uint32_t operation_id) {
    const auto operation = g_preparation_operation;
    if (!operation || operation->id != operation_id) {
        return;
    }

    PreparationOutcome outcome = PreparationOutcome::running;
    std::optional<openrc::NativeGamePreparationResultV1> result;
    std::string error_message;
    {
        const std::lock_guard lock(operation->mutex);
        outcome = operation->outcome;
        if (outcome == PreparationOutcome::running) {
            return;
        }
        result = operation->result;
        error_message = operation->error_message;
    }

    if (g_preparation_worker.joinable()) {
        g_preparation_worker.join();
    }
    KillTimer(window, kPreparationPollTimer);
    g_preparation_operation.reset();

    if (g_close_requested) {
        DestroyWindow(window);
        return;
    }

    set_preparation_controls(window, false);
    if (outcome == PreparationOutcome::succeeded && result) {
        g_ready_game = ReadyGame{
            result->publication.root,
            result->publication.manifest_sha256};
        std::string settings_warning;
        try {
            openrc::save_launcher_settings(openrc::LauncherSettings{
                operation->image_path,
                result->publication.root,
                openrc::hex_digest(result->publication.manifest_sha256)});
        } catch (const std::exception& error) {
            settings_warning =
                std::string("Could not remember the native installation: ") +
                error.what();
        }
        std::wostringstream report;
        report << (result->already_prepared
                       ? L"The verified native installation was reused."
                       : L"The complete native game was prepared successfully.")
               << L"\r\n\r\nDestination: "
               << result->publication.root.wstring()
               << L"\r\nPlanets:     " << result->publication.level_count
               << L"\r\nPackages:    " << result->publication.package_bytes
               << L" bytes\r\nManifest:    "
               << to_wide(openrc::hex_digest(
                      result->publication.manifest_sha256));
        if (!settings_warning.empty()) {
            report << L"\r\n\r\nWarning: " << to_wide(settings_warning);
        }
        set_report(report.str());
        set_status(result->already_prepared
            ? L"The native game is ready."
            : L"Native game prepared successfully.");
        EnableWindow(g_play_button, TRUE);
    } else if (outcome == PreparationOutcome::cancelled) {
        set_status(L"Native game preparation cancelled.");
        set_report(L"Preparation was cancelled. No partial installation was activated.");
        EnableWindow(g_play_button, g_ready_game ? TRUE : FALSE);
    } else {
        set_status(L"Native game preparation failed.");
        set_report(to_wide(std::string("Error: ") + error_message));
        EnableWindow(g_play_button, g_ready_game ? TRUE : FALSE);
    }
}

void poll_preparation(const HWND window) {
    const auto operation = g_preparation_operation;
    if (!operation) {
        return;
    }
    display_latest_preparation_progress(operation->id);
    finish_preparation(window, operation->id);
}

void save_selected_iso() {
    openrc::LauncherSettings settings;
    settings.iso_path = control_text(g_iso_edit);
    if (g_ready_game) {
        settings.prepared_game_root = g_ready_game->prepared_root;
        settings.prepared_game_manifest_sha256 =
            openrc::hex_digest(g_ready_game->manifest_sha256);
    }
    openrc::save_launcher_settings(settings);
}

void choose_iso(const HWND owner) {
    std::wstring file_buffer(32768, L'\0');
    const auto current = control_text(g_iso_edit);
    if (!current.empty() && current.size() + 1U < file_buffer.size()) {
        std::copy(current.begin(), current.end(), file_buffer.begin());
    }

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"PlayStation 2 disc images (*.iso)\0*.iso\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = file_buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(file_buffer.size());
    dialog.lpstrTitle = L"Select your legally dumped Ratchet & Clank disc image";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    dialog.lpstrDefExt = L"iso";

    if (GetOpenFileNameW(&dialog) != FALSE) {
        SetWindowTextW(g_iso_edit, file_buffer.c_str());
        set_status(L"Image selected. Click Inspect disc.");
        try {
            save_selected_iso();
        } catch (const std::exception& error) {
            set_report(to_wide(std::string("Could not save launcher settings: ") + error.what()));
        }
    }
}

void inspect_selected_iso() {
    const auto path_text = control_text(g_iso_edit);
    if (path_text.empty()) {
        set_status(L"Select an ISO image first.");
        set_report(L"No disc image selected.");
        return;
    }

    set_status(L"Inspecting disc image...");
    invalidate_inspected_iso();

    std::error_code filesystem_error;
    auto image_path = std::filesystem::absolute(
        std::filesystem::path(path_text),
        filesystem_error);
    if (filesystem_error) {
        set_status(L"Disc inspection failed.");
        set_report(to_wide(
            std::string("Error: Cannot make the ISO path absolute: ") +
            filesystem_error.message()));
        return;
    }
    image_path = image_path.lexically_normal();

    std::string settings_warning;
    try {
        openrc::LauncherSettings settings;
        settings.iso_path = image_path;
        if (g_ready_game) {
            settings.prepared_game_root = g_ready_game->prepared_root;
            settings.prepared_game_manifest_sha256 =
                openrc::hex_digest(g_ready_game->manifest_sha256);
        }
        openrc::save_launcher_settings(settings);
    } catch (const std::exception& error) {
        settings_warning = std::string("Could not save launcher settings: ") + error.what();
    }

    try {
        const auto report = openrc::inspect_disc(image_path);
        auto report_text = to_wide(openrc::format_disc_report(report));
        for (std::size_t position = 0; (position = report_text.find(L'\n', position)) != std::wstring::npos;) {
            report_text.replace(position, 1, L"\r\n");
            position += 2;
        }
        if (!settings_warning.empty()) {
            report_text += L"\r\nWarning: " + to_wide(settings_warning);
        }
        set_report(report_text);

        if (report.supported_build) {
            g_inspected_iso = image_path;
            EnableWindow(g_extract_button, TRUE);
            set_status(
                L"Supported Ratchet & Clank disc detected. Click Prepare "
                L"native game.");
        } else {
            set_status(L"Disc parsed, but this build is not supported yet.");
        }
    } catch (const std::exception& error) {
        set_status(L"Disc inspection failed.");
        auto error_text = std::string("Error: ") + error.what();
        if (!settings_warning.empty()) {
            error_text += "\n" + settings_warning;
        }
        set_report(to_wide(error_text));
    }
}

[[nodiscard]] std::wstring windows_error_text(const DWORD error_code) {
    wchar_t* message_buffer = nullptr;
    const auto character_count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        0,
        reinterpret_cast<wchar_t*>(&message_buffer),
        0,
        nullptr);

    std::wstring result;
    if (character_count != 0 && message_buffer != nullptr) {
        result.assign(message_buffer, character_count);
        LocalFree(message_buffer);
        while (!result.empty() &&
               (result.back() == L'\r' || result.back() == L'\n' ||
                result.back() == L' ' || result.back() == L'\t')) {
            result.pop_back();
        }
    }
    if (result.empty()) {
        result = L"Windows error " + std::to_wstring(error_code);
    }
    return result;
}

[[nodiscard]] std::filesystem::path launcher_executable_path() {
    std::vector<wchar_t> buffer(512U, L'\0');
    for (;;) {
        const auto character_count = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (character_count == 0) {
            const auto error_code = GetLastError();
            throw std::runtime_error(
                "Cannot determine the launcher executable path (Windows error " +
                std::to_string(error_code) + ")");
        }
        if (character_count < buffer.size()) {
            return std::filesystem::path(
                std::wstring(buffer.data(), character_count));
        }
        if (buffer.size() >= 32768U) {
            throw std::runtime_error("The launcher executable path is too long");
        }
        buffer.resize(std::min<std::size_t>(buffer.size() * 2U, 32768U));
    }
}

[[nodiscard]] std::wstring quote_windows_argument(const std::wstring_view argument) {
    std::wstring result;
    result.push_back(L'"');

    std::size_t backslash_count = 0;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslash_count;
            continue;
        }
        if (character == L'"') {
            result.append(backslash_count * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslash_count = 0;
            continue;
        }
        result.append(backslash_count, L'\\');
        backslash_count = 0;
        result.push_back(character);
    }

    result.append(backslash_count * 2U, L'\\');
    result.push_back(L'"');
    return result;
}

[[nodiscard]] std::wstring make_runtime_command_line(
    const std::filesystem::path& runtime_path,
    const ReadyGame& ready_game) {
    const auto plan = openrc::launcher::make_runtime_launch_plan_v1(
        runtime_path, ready_game.prepared_root, 0U);
    const auto utf8_arguments = plan.argv_utf8_v1();

    std::wstring command_line;
    for (const auto& utf8_argument : utf8_arguments) {
        if (!command_line.empty()) {
            command_line.push_back(L' ');
        }
        command_line += quote_windows_argument(to_wide(utf8_argument));
    }
    return command_line;
}

[[nodiscard]] openrc::PreparedGameV2RootV1 load_ready_native_game(
    const ReadyGame& ready_game) {
    const auto prepared = openrc::load_prepared_game_v2_root_v1(
        ready_game.prepared_root,
        openrc::make_native_game_prepared_game_limits_v1());
    if (prepared.manifest_sha256 != ready_game.manifest_sha256) {
        throw std::runtime_error(
            "The prepared-game manifest no longer matches the installation "
            "remembered by the launcher");
    }
    openrc::validate_current_native_game_publication_v1(prepared);
    return prepared;
}

void launch_runtime() {
    if (!g_ready_game) {
        set_status(L"Prepare the game files before starting OpenRC.");
        set_report(L"No verified prepared game is currently selected.");
        return;
    }

    try {
        try {
            static_cast<void>(load_ready_native_game(*g_ready_game));
        } catch (const std::exception& error) {
            set_status(L"The native game installation is no longer valid.");
            set_report(to_wide(
                std::string("Prepare the game again before starting OpenRC.\n\n") +
                error.what()));
            g_ready_game.reset();
            EnableWindow(g_play_button, FALSE);
            return;
        }

        std::error_code filesystem_error;
        const auto runtime_path =
            openrc::launcher::sibling_runtime_executable_v1(
                launcher_executable_path());
        if (!std::filesystem::is_regular_file(runtime_path, filesystem_error) ||
            filesystem_error) {
            set_status(L"OpenRC runtime was not found.");
            set_report(
                L"Expected runtime executable:\r\n" + runtime_path.wstring());
            return;
        }

        const auto executable_check =
            openrc::launcher::check_runtime_executable(runtime_path);
        if (!executable_check.accepted) {
            set_status(L"OpenRC runtime is not a compatible portable build.");
            set_report(
                L"The adjacent runtime was rejected before Windows tried to "
                L"start it:\r\n\r\n" +
                to_wide(executable_check.detail) +
                L"\r\n\r\nBuild and start the verified package with:\r\n"
                L"scripts\\build-portable.ps1\r\n"
                L"build-portable\\openrc-launcher.exe");
            return;
        }

        auto command_line = make_runtime_command_line(runtime_path, *g_ready_game);
        if (command_line.size() >= 32767U) {
            set_status(L"Could not start OpenRC runtime.");
            set_report(L"The runtime command line exceeds the Windows length limit.");
            return;
        }
        std::vector<wchar_t> mutable_command_line(
            command_line.begin(),
            command_line.end());
        mutable_command_line.push_back(L'\0');

        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        PROCESS_INFORMATION process_info{};
        const auto runtime_directory = runtime_path.parent_path();
        if (CreateProcessW(
                runtime_path.c_str(),
                mutable_command_line.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                runtime_directory.c_str(),
                &startup_info,
                &process_info) == FALSE) {
            const auto error_code = GetLastError();
            set_status(L"Could not start OpenRC runtime.");
            set_report(
                L"Windows could not start:\r\n" + runtime_path.wstring() +
                L"\r\n\r\nError: " + windows_error_text(error_code));
            return;
        }

        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        set_status(L"OpenRC runtime started.");
        set_report(
            L"The native runtime is loading Veldin from the prepared game "
            L"package. The original ISO and PS2 executable are not used "
            L"during play.");
    } catch (const std::exception& error) {
        set_status(L"Could not start OpenRC runtime.");
        set_report(to_wide(std::string("Error: ") + error.what()));
    }
}

void open_data_directory(const HWND owner) {
    try {
        const auto paths = openrc::application_paths();
        openrc::ensure_application_directories(paths);
        const auto result = ShellExecuteW(
            owner,
            L"open",
            paths.local_data.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            set_status(L"Windows could not open the local data directory.");
        }
    } catch (const std::exception& error) {
        set_report(to_wide(std::string("Error: ") + error.what()));
    }
}

void apply_default_font(const HWND window) {
    const auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    for (auto child = GetWindow(window, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT)) {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void create_controls(const HWND window) {
    CreateWindowExW(
        0,
        L"STATIC",
        L"OpenRC",
        WS_CHILD | WS_VISIBLE,
        24,
        18,
        180,
        28,
        window,
        nullptr,
        nullptr,
        nullptr);

    CreateWindowExW(
        0,
        L"STATIC",
        L"Native Ratchet & Clank reimplementation",
        WS_CHILD | WS_VISIBLE,
        24,
        45,
        500,
        20,
        window,
        nullptr,
        nullptr,
        nullptr);

    g_iso_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        24,
        78,
        610,
        27,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIsoEdit)),
        nullptr,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Browse...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        646,
        77,
        106,
        29,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBrowseButton)),
        nullptr,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Inspect disc",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        24,
        119,
        132,
        32,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInspectButton)),
        nullptr,
        nullptr);

    g_extract_button = CreateWindowExW(
        0,
        L"BUTTON",
        L"Prepare native game",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | WS_DISABLED,
        166,
        119,
        148,
        32,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExtractButton)),
        nullptr,
        nullptr);

    g_play_button = CreateWindowExW(
        0,
        L"BUTTON",
        L"Play",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | WS_DISABLED,
        324,
        119,
        100,
        32,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPlayButton)),
        nullptr,
        nullptr);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"Open data folder",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        604,
        119,
        148,
        32,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenDataButton)),
        nullptr,
        nullptr);

    g_status_label = CreateWindowExW(
        0,
        L"STATIC",
        L"Select your legally dumped ISO image to begin.",
        WS_CHILD | WS_VISIBLE,
        24,
        169,
        728,
        22,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusLabel)),
        nullptr,
        nullptr);

    g_report_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"The launcher is ready.\r\n\r\nGame assets are never included with OpenRC.",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        24,
        198,
        728,
        264,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReportEdit)),
        nullptr,
        nullptr);

    apply_default_font(window);

    try {
        const auto settings = openrc::load_launcher_settings();
        if (!settings.iso_path.empty()) {
            SetWindowTextW(g_iso_edit, settings.iso_path.c_str());
            set_status(L"Previous disc image restored. Click Inspect disc.");
        }
        if (!settings.prepared_game_root.empty()) {
            const auto prepared = openrc::load_prepared_game_v2_root_v1(
                settings.prepared_game_root,
                openrc::make_native_game_prepared_game_limits_v1());
            if (openrc::hex_digest(prepared.manifest_sha256) !=
                settings.prepared_game_manifest_sha256) {
                throw std::runtime_error(
                    "The saved native installation identity has changed");
            }
            ReadyGame restored{
                settings.prepared_game_root,
                prepared.manifest_sha256};
            static_cast<void>(load_ready_native_game(restored));
            g_ready_game = std::move(restored);
            EnableWindow(g_play_button, TRUE);
            set_status(L"Native game installation restored. Click Play.");
            set_report(
                L"Verified native installation:\r\n" +
                settings.prepared_game_root.wstring() +
                L"\r\n\r\nThe original ISO is not required to play.");
        }
    } catch (const std::exception& error) {
        g_ready_game.reset();
        EnableWindow(g_play_button, FALSE);
        set_status(L"The saved native installation needs to be prepared again.");
        set_report(to_wide(
            std::string("The saved game data is old, incomplete, or damaged. "
                        "Keep your ISO selected, click Inspect disc, then "
                        "Prepare game. OpenRC will rebuild the same local "
                        "installation; no second client is created.\n\n") +
            error.what()));
    }
}

LRESULT CALLBACK window_procedure(
    const HWND window,
    const UINT message,
    const WPARAM word_parameter,
    const LPARAM long_parameter) {
    switch (message) {
    case WM_CREATE:
        create_controls(window);
        return 0;

    case WM_COMMAND:
        if (LOWORD(word_parameter) == kIsoEdit &&
            HIWORD(word_parameter) == EN_CHANGE &&
            !g_preparation_operation) {
            invalidate_inspected_iso();
            return 0;
        }
        if (HIWORD(word_parameter) != BN_CLICKED) {
            break;
        }
        switch (LOWORD(word_parameter)) {
        case kBrowseButton:
            choose_iso(window);
            return 0;
        case kInspectButton:
            inspect_selected_iso();
            return 0;
        case kExtractButton:
            if (g_preparation_operation) {
                cancel_preparation();
            } else {
                start_preparation(window);
            }
            return 0;
        case kPlayButton:
            launch_runtime();
            return 0;
        case kOpenDataButton:
            open_data_directory(window);
            return 0;
        default:
            break;
        }
        break;

    case WM_TIMER:
        if (word_parameter == kPreparationPollTimer) {
            poll_preparation(window);
            return 0;
        }
        break;

    case WM_CLOSE:
        if (g_preparation_operation) {
            g_close_requested = true;
            cancel_preparation();
            return 0;
        }
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        KillTimer(window, kPreparationPollTimer);
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(window, message, word_parameter, long_parameter);
}

} // namespace

int WINAPI wWinMain(
    const HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int show_command) {
    g_ui_thread_id = GetCurrentThreadId();
    SetProcessDPIAware();

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClass;
    window_class.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

    if (RegisterClassExW(&window_class) == 0) {
        return 1;
    }

    const auto screen_width = GetSystemMetrics(SM_CXSCREEN);
    const auto screen_height = GetSystemMetrics(SM_CYSCREEN);
    constexpr int width = 800;
    constexpr int height = 530;

    const auto window = CreateWindowExW(
        0,
        kWindowClass,
        kWindowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (screen_width - width) / 2,
        (screen_height - height) / 2,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (window == nullptr) {
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    int exit_code = 0;
    for (;;) {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            exit_code = 1;
            break;
        }
        if (result == 0) {
            exit_code = static_cast<int>(message.wParam);
            break;
        }

        if (message.hwnd == nullptr && message.message == kPreparationProgressMessage) {
            display_latest_preparation_progress(
                static_cast<std::uint32_t>(message.wParam));
            continue;
        }
        if (message.hwnd == nullptr && message.message == kPreparationDoneMessage) {
            finish_preparation(
                window,
                static_cast<std::uint32_t>(message.wParam));
            continue;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_preparation_worker.joinable()) {
        g_preparation_worker.request_stop();
        g_preparation_worker.join();
    }
    return exit_code;
}
