#pragma once

#include <filesystem>

namespace openrc {

struct LauncherSettings {
    std::filesystem::path iso_path;
};

[[nodiscard]] LauncherSettings load_launcher_settings();
void save_launcher_settings(const LauncherSettings& settings);

} // namespace openrc
