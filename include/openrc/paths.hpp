#pragma once

#include <filesystem>
#include <string>

namespace openrc {

struct ApplicationPaths {
    std::filesystem::path roaming_config;
    std::filesystem::path local_data;
    std::filesystem::path cache;
    std::filesystem::path logs;
};

[[nodiscard]] ApplicationPaths application_paths();
void ensure_application_directories(const ApplicationPaths& paths);

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path path_from_utf8(const std::string& value);

} // namespace openrc
