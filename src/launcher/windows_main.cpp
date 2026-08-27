#include "openrc/disc.hpp"
#include "openrc/paths.hpp"
#include "openrc/preparation.hpp"
#include "openrc/settings.hpp"

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
        std::filesystem::path source_image_path)
        : id(operation_id), image_path(std::move(source_image_path)) {}

    const std::uint32_t id;
    const std::filesystem::path image_path;
    std::mutex mutex;
    openrc::PreparationProgress latest_progress;
    bool has_progress = false;
    bool progress_message_pending = false;
    bool cancellation_requested = false;
    PreparationOutcome outcome = PreparationOutcome::running;
    std::optional<openrc::PreparationResult> result;
    std::string error_message;
};

struct ReadyGame {
    std::filesystem::path image_path;
    openrc::PreparationResult preparation;
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

void invalidate_inspected_iso() {
    g_inspected_iso.reset();
    g_ready_game.reset();
    EnableWindow(g_extract_button, FALSE);
    EnableWindow(g_play_button, FALSE);
}

void set_preparation_controls(const HWND window, const bool preparing) {
    EnableWindow(g_iso_edit, preparing ? FALSE : TRUE);
    EnableWindow(GetDlgItem(window, kBrowseButton), preparing ? FALSE : TRUE);
    EnableWindow(GetDlgItem(window, kInspectButton), preparing ? FALSE : TRUE);
    EnableWindow(g_play_button, FALSE);

    SetWindowTextW(
        g_extract_button,
        preparing ? L"Cancel" : L"Prepare game files");
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

void publish_preparation_progress(
    const std::shared_ptr<PreparationOperation>& operation,
    const openrc::PreparationProgress& progress,
    const DWORD ui_thread_id) {
    bool should_post = false;
    {
        const std::lock_guard lock(operation->mutex);
        if (operation->cancellation_requested) {
            return;
        }
        operation->latest_progress = progress;
        operation->has_progress = true;
        if (!operation->progress_message_pending) {
            operation->progress_message_pending = true;
            should_post = true;
        }
    }

    if (should_post && PostThreadMessageW(
            ui_thread_id,
            kPreparationProgressMessage,
            static_cast<WPARAM>(operation->id),
            0) == FALSE) {
        const std::lock_guard lock(operation->mutex);
        operation->progress_message_pending = false;
    }
}

void start_preparation(const HWND window) {
    if (g_preparation_operation || !g_inspected_iso) {
        return;
    }

    g_ready_game.reset();

    std::filesystem::path games_directory;
    try {
        games_directory = openrc::application_paths().local_data / L"games";
    } catch (const std::exception& error) {
        set_status(L"Could not determine the game data directory.");
        set_report(to_wide(std::string("Error: ") + error.what()));
        return;
    }

    const auto image_path = *g_inspected_iso;
    auto operation = std::make_shared<PreparationOperation>(
        next_operation_id(),
        image_path);
    g_preparation_operation = operation;
    set_preparation_controls(window, true);
    set_status(L"Preparing game files...");
    set_report(L"OpenRC is reading your disc image. The launcher remains responsive.");

    try {
        g_preparation_worker = std::jthread(
            [operation, image_path, games_directory, ui_thread_id = g_ui_thread_id](
                const std::stop_token stop_token) {
                try {
                    auto result = openrc::prepare_game_files(
                        image_path,
                        games_directory,
                        [operation, stop_token, ui_thread_id](
                            const openrc::PreparationProgress& progress) {
                            if (stop_token.stop_requested()) {
                                return false;
                            }
                            publish_preparation_progress(operation, progress, ui_thread_id);
                            return !stop_token.stop_requested();
                        });

                    const std::lock_guard lock(operation->mutex);
                    operation->result = std::move(result);
                    operation->outcome = PreparationOutcome::succeeded;
                } catch (const openrc::PreparationCancelled&) {
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
                    ui_thread_id,
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

    openrc::PreparationProgress progress;
    {
        const std::lock_guard lock(operation->mutex);
        if (!operation->has_progress || !operation->progress_message_pending) {
            return;
        }
        progress = operation->latest_progress;
        operation->progress_message_pending = false;
    }

    const auto text = format_preparation_progress(progress);
    set_status(text.c_str());
}

void finish_preparation(const HWND window, const std::uint32_t operation_id) {
    const auto operation = g_preparation_operation;
    if (!operation || operation->id != operation_id) {
        return;
    }

    PreparationOutcome outcome = PreparationOutcome::running;
    std::optional<openrc::PreparationResult> result;
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
        g_ready_game = ReadyGame{operation->image_path, *result};
        std::wostringstream report;
        report << (result->already_prepared
                       ? L"Game files were already prepared."
                       : L"Game files prepared successfully.")
               << L"\r\n\r\nDestination: " << result->destination.wstring()
               << L"\r\nManifest:    " << result->manifest_path.wstring()
               << L"\r\nFiles:       " << result->file_count
               << L"\r\nImage hash:  " << to_wide(result->image_sha256);
        set_report(report.str());
        set_status(result->already_prepared
            ? L"Game files are ready."
            : L"Game files prepared successfully.");
        EnableWindow(g_play_button, TRUE);
    } else if (outcome == PreparationOutcome::cancelled) {
        set_status(L"Game-file preparation cancelled.");
        set_report(L"Preparation was cancelled. No partial installation was activated.");
    } else {
        set_status(L"Game-file preparation failed.");
        set_report(to_wide(std::string("Error: ") + error_message));
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
        openrc::save_launcher_settings(openrc::LauncherSettings{image_path});
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
            set_status(L"Supported Ratchet & Clank executable detected. Click Prepare game files.");
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
    const std::vector<std::wstring> arguments{
        runtime_path.wstring(),
        L"--disc-image",
        ready_game.image_path.wstring(),
        L"--boot-executable",
        ready_game.preparation.boot_executable_path.wstring(),
        L"--level",
        L"0",
        L"--record",
        L"all",
        L"--entry-pair",
        L"16",
    };

    std::wstring command_line;
    for (const auto& argument : arguments) {
        if (!command_line.empty()) {
            command_line.push_back(L' ');
        }
        command_line += quote_windows_argument(argument);
    }
    return command_line;
}

void launch_runtime() {
    if (!g_ready_game) {
        set_status(L"Prepare the game files before starting OpenRC.");
        set_report(L"No verified prepared game is currently selected.");
        return;
    }

    try {
        std::error_code filesystem_error;
        if (!std::filesystem::is_regular_file(
                g_ready_game->image_path,
                filesystem_error) ||
            filesystem_error) {
            set_status(L"The selected disc image is no longer available.");
            set_report(L"Select and inspect the disc image again before starting OpenRC.");
            invalidate_inspected_iso();
            return;
        }

        filesystem_error.clear();
        if (!std::filesystem::is_regular_file(
                g_ready_game->preparation.boot_executable_path,
                filesystem_error) ||
            filesystem_error) {
            set_status(L"The prepared game files are no longer available.");
            set_report(L"Prepare the game files again before starting OpenRC.");
            g_ready_game.reset();
            EnableWindow(g_play_button, FALSE);
            return;
        }

        const auto runtime_path =
            launcher_executable_path().parent_path() / L"openrc-runtime.exe";
        filesystem_error.clear();
        if (!std::filesystem::is_regular_file(runtime_path, filesystem_error) ||
            filesystem_error) {
            set_status(L"OpenRC runtime was not found.");
            set_report(
                L"Expected runtime executable:\r\n" + runtime_path.wstring());
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
            L"The native runtime is loading all supported Veldin records in "
            L"a separate process. The first window can take a few seconds.");
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
        L"Native Ratchet & Clank reimplementation - Stage 1",
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
        L"Prepare game files",
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
    } catch (const std::exception& error) {
        set_report(to_wide(std::string("Could not load launcher settings: ") + error.what()));
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
