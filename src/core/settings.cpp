#include "openrc/settings.hpp"

#include "openrc/paths.hpp"

#include <fstream>
#include <string>

namespace openrc {
namespace {

constexpr const char* kIsoPathKey = "iso_path=";

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

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.starts_with(kIsoPathKey)) {
            result.iso_path = path_from_utf8(
                line.substr(std::char_traits<char>::length(kIsoPathKey)));
        }
    }
    return result;
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
        filesystem_error.clear();
        const auto copied = std::filesystem::copy_file(
                legacy_path,
                current_path,
                std::filesystem::copy_options::none,
                filesystem_error);
        if (copied || std::filesystem::exists(current_path)) {
            return read_settings(current_path);
        }
    } catch (const std::filesystem::filesystem_error&) {
        // Loading the legacy selection still succeeds if best-effort migration fails.
    }
    return legacy_settings;
}

void save_launcher_settings(const LauncherSettings& settings) {
    const auto paths = application_paths();
    ensure_application_directories(paths);

    std::ofstream output(settings_path(paths), std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot write launcher settings");
    }
    output << kIsoPathKey << path_to_utf8(settings.iso_path) << '\n';
}

} // namespace openrc
