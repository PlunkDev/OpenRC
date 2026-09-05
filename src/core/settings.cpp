#include "openrc/settings.hpp"

#include "openrc/paths.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace openrc {
namespace {

constexpr const char* kIsoPathKey = "iso_path=";
constexpr const char* kFormatVersionKey = "format_version=";
constexpr const char* kPreparedGameRootKey = "prepared_game_root=";
constexpr const char* kPreparedGameManifestSha256Key =
    "prepared_game_manifest_sha256=";
constexpr std::string_view kCurrentFormatVersion = "2";

[[nodiscard]] bool is_lower_hex_digest(const std::string_view value) noexcept {
    if (value.size() != 64U) {
        return false;
    }
    for (const auto character : value) {
        if ((character < '0' || character > '9') &&
            (character < 'a' || character > 'f')) {
            return false;
        }
    }
    return true;
}

void require_single_line(const std::string_view value, const char* description) {
    if (value.find('\n') != std::string_view::npos ||
        value.find('\r') != std::string_view::npos) {
        throw std::runtime_error(
            std::string(description) + " cannot contain a line break");
    }
}

[[nodiscard]] std::filesystem::path settings_path(const ApplicationPaths& paths) {
    return paths.local_data / "launcher.ini";
}

[[nodiscard]] std::filesystem::path legacy_settings_path(const ApplicationPaths& paths) {
    return paths.roaming_config / "launcher.ini";
}

[[nodiscard]] LauncherSettings read_settings(const std::filesystem::path& path) {
    LauncherSettings result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return result;
    }

    std::optional<std::string> format_version;
    bool saw_iso_path = false;
    bool saw_prepared_root = false;
    bool saw_prepared_digest = false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            continue;
        }
        if (line.starts_with(kFormatVersionKey)) {
            if (format_version) {
                throw std::runtime_error(
                    "Launcher settings repeat format_version");
            }
            format_version = line.substr(
                std::char_traits<char>::length(kFormatVersionKey));
        } else if (line.starts_with(kIsoPathKey)) {
            if (saw_iso_path) {
                throw std::runtime_error("Launcher settings repeat iso_path");
            }
            saw_iso_path = true;
            result.iso_path = path_from_utf8(
                line.substr(std::char_traits<char>::length(kIsoPathKey)));
        } else if (line.starts_with(kPreparedGameRootKey)) {
            if (saw_prepared_root) {
                throw std::runtime_error(
                    "Launcher settings repeat prepared_game_root");
            }
            saw_prepared_root = true;
            result.prepared_game_root = path_from_utf8(line.substr(
                std::char_traits<char>::length(kPreparedGameRootKey)));
        } else if (line.starts_with(kPreparedGameManifestSha256Key)) {
            if (saw_prepared_digest) {
                throw std::runtime_error(
                    "Launcher settings repeat prepared_game_manifest_sha256");
            }
            saw_prepared_digest = true;
            result.prepared_game_manifest_sha256 = line.substr(
                std::char_traits<char>::length(
                    kPreparedGameManifestSha256Key));
        } else {
            throw std::runtime_error(
                "Launcher settings contain an unknown field");
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("Cannot read launcher settings");
    }
    if (format_version && *format_version != kCurrentFormatVersion) {
        throw std::runtime_error(
            "Launcher settings use an unsupported format version");
    }
    if (!format_version && (saw_prepared_root || saw_prepared_digest)) {
        throw std::runtime_error(
            "Prepared-game settings require format version 2");
    }
    if (saw_prepared_root != saw_prepared_digest) {
        throw std::runtime_error(
            "Launcher settings contain an incomplete prepared game identity");
    }
    if (saw_prepared_root &&
        (result.prepared_game_root.empty() ||
         !result.prepared_game_root.is_absolute() ||
         !is_lower_hex_digest(result.prepared_game_manifest_sha256))) {
        throw std::runtime_error(
            "Launcher settings contain an invalid prepared game identity");
    }
    return result;
}

[[nodiscard]] std::filesystem::path temporary_settings_path(
    const std::filesystem::path& destination) {
    static std::atomic<std::uint64_t> sequence{0U};
    const auto timestamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#ifdef _WIN32
    const auto process_id = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const auto process_id = static_cast<std::uint64_t>(::getpid());
#endif
    return destination.parent_path() /
        (destination.filename().string() + ".tmp-" +
         std::to_string(process_id) + "-" +
         std::to_string(timestamp) + "-" +
         std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed)));
}

// Returns false only when another process already created this exact
// candidate. A successfully created temporary belongs exclusively to this
// call and is removed before any write/flush error is reported.
[[nodiscard]] bool write_new_settings_file(
    const std::filesystem::path& path,
    const std::string_view contents) {
#ifdef _WIN32
    const auto handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
            return false;
        }
        throw std::runtime_error(
            "Cannot create temporary launcher settings (Windows error " +
            std::to_string(error) + ")");
    }

    DWORD failure = ERROR_SUCCESS;
    std::size_t offset = 0U;
    while (offset < contents.size()) {
        const auto remaining = contents.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining, std::numeric_limits<DWORD>::max()));
        DWORD written = 0U;
        if (WriteFile(
                handle, contents.data() + offset, chunk, &written, nullptr) ==
                FALSE ||
            written == 0U) {
            failure = GetLastError();
            if (failure == ERROR_SUCCESS) {
                failure = ERROR_WRITE_FAULT;
            }
            break;
        }
        offset += written;
    }
    if (failure == ERROR_SUCCESS && FlushFileBuffers(handle) == FALSE) {
        failure = GetLastError();
    }
    if (CloseHandle(handle) == FALSE && failure == ERROR_SUCCESS) {
        failure = GetLastError();
    }
    if (failure != ERROR_SUCCESS) {
        DeleteFileW(path.c_str());
        throw std::runtime_error(
            "Cannot write temporary launcher settings (Windows error " +
            std::to_string(failure) + ")");
    }
    return true;
#else
    const auto native = path.native();
    const auto descriptor = ::open(
        native.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        if (errno == EEXIST) {
            return false;
        }
        throw std::runtime_error(
            "Cannot create temporary launcher settings: " +
            std::string(std::strerror(errno)));
    }

    int failure = 0;
    std::size_t offset = 0U;
    while (offset < contents.size()) {
        const auto written =
            ::write(descriptor, contents.data() + offset, contents.size() - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            failure = written == 0 ? EIO : errno;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (failure == 0 && ::fsync(descriptor) != 0) {
        failure = errno;
    }
    if (::close(descriptor) != 0 && failure == 0) {
        failure = errno;
    }
    if (failure != 0) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        throw std::runtime_error(
            "Cannot write temporary launcher settings: " +
            std::string(std::strerror(failure)));
    }
    return true;
#endif
}

[[nodiscard]] std::string
encode_settings_v2(const LauncherSettings& settings) {
    const auto iso_path = path_to_utf8(settings.iso_path);
    const auto prepared_root = path_to_utf8(settings.prepared_game_root);
    require_single_line(iso_path, "The selected ISO path");
    require_single_line(prepared_root, "The prepared game root");
    if (settings.prepared_game_root.empty() !=
        settings.prepared_game_manifest_sha256.empty()) {
        throw std::runtime_error(
            "A prepared game root and manifest digest must be saved together");
    }
    if (!settings.prepared_game_root.empty() &&
        (!settings.prepared_game_root.is_absolute() ||
         !is_lower_hex_digest(settings.prepared_game_manifest_sha256))) {
        throw std::runtime_error(
            "Cannot save an invalid prepared game identity");
    }

    std::ostringstream encoded;
    encoded << kFormatVersionKey << kCurrentFormatVersion << '\n'
            << kIsoPathKey << iso_path << '\n';
    if (!settings.prepared_game_root.empty()) {
        encoded << kPreparedGameRootKey << prepared_root << '\n'
                << kPreparedGameManifestSha256Key
                << settings.prepared_game_manifest_sha256 << '\n';
    }
    if (!encoded) {
        throw std::runtime_error("Cannot encode launcher settings");
    }
    return encoded.str();
}

[[nodiscard]] std::filesystem::path write_unique_settings_temporary(
    const std::filesystem::path& destination,
    const std::string_view contents) {
    constexpr std::uint32_t kMaximumTemporaryAttempts = 64U;
    for (std::uint32_t attempt = 0U;
         attempt < kMaximumTemporaryAttempts; ++attempt) {
        auto candidate = temporary_settings_path(destination);
        if (write_new_settings_file(candidate, contents)) {
            return candidate;
        }
    }
    throw std::runtime_error(
        "Cannot reserve a unique temporary launcher-settings file");
}

[[nodiscard]] bool activate_settings_file_if_absent(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) {
#ifdef _WIN32
    if (MoveFileExW(
            temporary.c_str(), destination.c_str(),
            MOVEFILE_WRITE_THROUGH) != FALSE) {
        return true;
    }
    const auto error = GetLastError();
    if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
        return false;
    }
    throw std::runtime_error(
        "Cannot migrate launcher settings (Windows error " +
        std::to_string(error) + ")");
#else
    const auto temporary_bytes = temporary.native();
    const auto destination_bytes = destination.native();
    if (::link(temporary_bytes.c_str(), destination_bytes.c_str()) == 0) {
        return true;
    }
    if (errno == EEXIST) {
        return false;
    }
    throw std::runtime_error(
        "Cannot migrate launcher settings: " +
        std::string(std::strerror(errno)));
#endif
}

void replace_settings_file(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) {
#ifdef _WIN32
    if (MoveFileExW(
            temporary.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        throw std::runtime_error(
            "Cannot activate launcher settings (Windows error " +
            std::to_string(GetLastError()) + ")");
    }
#else
    const auto temporary_bytes = temporary.native();
    const auto destination_bytes = destination.native();
    if (std::rename(temporary_bytes.c_str(), destination_bytes.c_str()) != 0) {
        throw std::runtime_error(
            "Cannot activate launcher settings: " +
            std::string(std::strerror(errno)));
    }
#endif
}

} // namespace

LauncherSettings load_launcher_settings() {
    const auto paths = application_paths();
    const auto current_path = settings_path(paths);
    std::error_code filesystem_error;
    if (std::filesystem::exists(current_path, filesystem_error) && !filesystem_error) {
        return read_settings(current_path);
    }

    const auto legacy_path = legacy_settings_path(paths);
    filesystem_error.clear();
    if (!std::filesystem::exists(legacy_path, filesystem_error) || filesystem_error) {
        return {};
    }

    const auto legacy_settings = read_settings(legacy_path);
    try {
        ensure_application_directories(paths);
        const auto encoded = encode_settings_v2(legacy_settings);
        const auto temporary =
            write_unique_settings_temporary(current_path, encoded);
        try {
            static_cast<void>(activate_settings_file_if_absent(
                temporary, current_path));
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        filesystem_error.clear();
        if (std::filesystem::exists(current_path, filesystem_error) &&
            !filesystem_error) {
            return read_settings(current_path);
        }
    } catch (const std::exception&) {
        // Loading the legacy selection still succeeds if best-effort migration fails.
    }
    return legacy_settings;
}

void save_launcher_settings(const LauncherSettings& settings) {
    const auto paths = application_paths();
    ensure_application_directories(paths);

    const auto destination = settings_path(paths);
    const auto encoded = encode_settings_v2(settings);
    const auto temporary =
        write_unique_settings_temporary(destination, encoded);
    try {
        replace_settings_file(temporary, destination);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

} // namespace openrc
