#pragma once

#include <filesystem>
#include <string>

namespace openrc {

struct LauncherSettings {
    std::filesystem::path iso_path;
    std::filesystem::path prepared_game_root;
    std::string prepared_game_manifest_sha256;
};

[[nodiscard]] LauncherSettings load_launcher_settings();
void save_launcher_settings(const LauncherSettings& settings);

} // namespace openrc
