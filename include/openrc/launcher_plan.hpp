#pragma once

#include "openrc/paths.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace openrc::launcher {

// Paths owned by the launcher. The legacy extraction directory remains
// separate from the immutable PreparedGameV2 installations.
struct LauncherInstallPathsV1 {
  std::filesystem::path legacy_games_root;
  std::filesystem::path prepared_game_root;
  std::filesystem::path runtime_executable;
};

[[nodiscard]] bool
is_lowercase_sha256_hex_v1(std::string_view digest) noexcept;

[[nodiscard]] PreparedContentDigestV1
parse_lowercase_sha256_hex_v1(std::string_view digest);

[[nodiscard]] std::filesystem::path
legacy_games_root_v1(const ApplicationPaths &application_paths);

[[nodiscard]] std::filesystem::path prepared_game_v2_root_v1(
    const ApplicationPaths &application_paths,
    std::string_view source_iso_sha256);

[[nodiscard]] std::filesystem::path sibling_runtime_executable_v1(
    const std::filesystem::path &launcher_executable);

[[nodiscard]] LauncherInstallPathsV1 make_launcher_install_paths_v1(
    const ApplicationPaths &application_paths,
    const std::filesystem::path &launcher_executable,
    std::string_view source_iso_sha256);

// A typed process contract. argv_utf8_v1() always emits exactly the package
// runtime interface; the original ISO and prepared PS2 executable never cross
// the play-time boundary.
struct RuntimeLaunchPlanV1 {
  std::filesystem::path executable;
  std::filesystem::path prepared_root;
  std::uint32_t level_id{};
  bool startup = false;

  [[nodiscard]] std::vector<std::string> argv_utf8_v1() const;
};

[[nodiscard]] RuntimeLaunchPlanV1 make_runtime_launch_plan_v1(
    const std::filesystem::path &runtime_executable,
    const std::filesystem::path &prepared_root, std::uint32_t level_id);

[[nodiscard]] RuntimeLaunchPlanV1 make_runtime_startup_launch_plan_v1(
    const std::filesystem::path &runtime_executable,
    const std::filesystem::path &prepared_root);

} // namespace openrc::launcher
