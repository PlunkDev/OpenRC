#include "openrc/paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {
namespace {

#ifdef _WIN32
[[nodiscard]] std::filesystem::path environment_path(const wchar_t* name) {
    std::size_t required = 0;
    if (_wgetenv_s(&required, nullptr, 0, name) != 0 || required == 0) {
        return {};
    }

    std::vector<wchar_t> buffer(required);
    if (_wgetenv_s(&required, buffer.data(), buffer.size(), name) != 0) {
        return {};
    }
    return std::filesystem::path(buffer.data());
}
#else
[[nodiscard]] std::filesystem::path environment_path(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::filesystem::path(value) : std::filesystem::path{};
}
#endif

[[nodiscard]] std::filesystem::path require_environment_path(
    const std::filesystem::path& path,
    const char* variable_name) {
    if (path.empty()) {
        throw std::runtime_error(std::string("Required environment variable is missing: ") + variable_name);
    }
    return path;
}

} // namespace

ApplicationPaths application_paths() {
#ifdef _WIN32
    const auto roaming_root = require_environment_path(environment_path(L"APPDATA"), "APPDATA");
    const auto local_root = require_environment_path(environment_path(L"LOCALAPPDATA"), "LOCALAPPDATA");

    const auto roaming = roaming_root / L"PlunkDev" / L"OpenRC";
    const auto local = local_root / L"PlunkDev" / L"OpenRC";
#else
    const auto home = require_environment_path(environment_path("HOME"), "HOME");
    const auto config_root = environment_path("XDG_CONFIG_HOME").empty()
        ? home / ".config"
        : environment_path("XDG_CONFIG_HOME");
    const auto data_root = environment_path("XDG_DATA_HOME").empty()
        ? home / ".local" / "share"
        : environment_path("XDG_DATA_HOME");
    const auto cache_root = environment_path("XDG_CACHE_HOME").empty()
        ? home / ".cache"
        : environment_path("XDG_CACHE_HOME");

    const auto roaming = config_root / "PlunkDev" / "OpenRC";
    const auto local = data_root / "PlunkDev" / "OpenRC";
#endif

    ApplicationPaths result;
    result.roaming_config = roaming;
    result.local_data = local;
#ifdef _WIN32
    result.cache = local / L"cache";
    result.logs = local / L"logs";
#else
    result.cache = cache_root / "PlunkDev" / "OpenRC";
    result.logs = local / "logs";
#endif
    return result;
}

void ensure_application_directories(const ApplicationPaths& paths) {
    std::filesystem::create_directories(paths.roaming_config);
    std::filesystem::create_directories(paths.local_data);
    std::filesystem::create_directories(paths.cache);
    std::filesystem::create_directories(paths.logs);
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::filesystem::path path_from_utf8(const std::string& value) {
    std::u8string encoded;
    encoded.resize(value.size());
    std::transform(value.begin(), value.end(), encoded.begin(), [](const char character) {
        return static_cast<char8_t>(static_cast<unsigned char>(character));
    });
    return std::filesystem::path(encoded);
}

} // namespace openrc
