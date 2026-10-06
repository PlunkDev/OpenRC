#include "openrc/launcher_plan.hpp"

#include <stdexcept>
#include <utility>

namespace openrc::launcher {
namespace {

[[nodiscard]] std::filesystem::path
require_absolute_file_like_path(const std::filesystem::path &path,
                                const char *description) {
  if (path.empty() || !path.is_absolute()) {
    throw std::invalid_argument(std::string(description) +
                                " must be an absolute path");
  }

  const auto normalized = path.lexically_normal();
  if (normalized.filename().empty()) {
    throw std::invalid_argument(std::string(description) +
                                " must name a file or directory below a "
                                "filesystem root");
  }
  return normalized;
}

[[nodiscard]] std::filesystem::path
require_absolute_local_data(const ApplicationPaths &application_paths) {
  return require_absolute_file_like_path(application_paths.local_data,
                                         "Application local-data directory");
}

} // namespace

bool is_lowercase_sha256_hex_v1(const std::string_view digest) noexcept {
  if (digest.size() != 64U) {
    return false;
  }

  for (const char character : digest) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

PreparedContentDigestV1
parse_lowercase_sha256_hex_v1(const std::string_view digest) {
  if (!is_lowercase_sha256_hex_v1(digest)) {
    throw std::invalid_argument(
        "SHA-256 must be exactly 64 lowercase hexadecimal digits");
  }
  const auto nibble = [](const char character) -> std::uint8_t {
    if (character >= '0' && character <= '9') {
      return static_cast<std::uint8_t>(character - '0');
    }
    return static_cast<std::uint8_t>(character - 'a' + 10);
  };

  PreparedContentDigestV1 result{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = static_cast<std::byte>(
        (nibble(digest[index * 2U]) << 4U) |
        nibble(digest[index * 2U + 1U]));
  }
  return result;
}

std::filesystem::path
legacy_games_root_v1(const ApplicationPaths &application_paths) {
  return require_absolute_local_data(application_paths) / "games";
}

std::filesystem::path prepared_game_v2_root_v1(
    const ApplicationPaths &application_paths,
    const std::string_view source_iso_sha256) {
  static_cast<void>(parse_lowercase_sha256_hex_v1(source_iso_sha256));

  return require_absolute_local_data(application_paths) / "prepared-v2" /
         "openrc-rac-2002" / std::string(source_iso_sha256);
}

std::filesystem::path sibling_runtime_executable_v1(
    const std::filesystem::path &launcher_executable) {
  const auto normalized =
      require_absolute_file_like_path(launcher_executable,
                                      "Launcher executable");
#ifdef _WIN32
  return normalized.parent_path() / "openrc-runtime.exe";
#else
  return normalized.parent_path() / "openrc-runtime";
#endif
}

LauncherInstallPathsV1 make_launcher_install_paths_v1(
    const ApplicationPaths &application_paths,
    const std::filesystem::path &launcher_executable,
    const std::string_view source_iso_sha256) {
  return LauncherInstallPathsV1{
      legacy_games_root_v1(application_paths),
      prepared_game_v2_root_v1(application_paths, source_iso_sha256),
      sibling_runtime_executable_v1(launcher_executable),
  };
}

std::vector<std::string> RuntimeLaunchPlanV1::argv_utf8_v1() const {
  const auto checked_executable =
      require_absolute_file_like_path(executable, "Runtime executable");
  const auto checked_prepared_root =
      require_absolute_file_like_path(prepared_root, "PreparedGameV2 root");

  if (startup) return {path_to_utf8(checked_executable), "--prepared-root",
                       path_to_utf8(checked_prepared_root)};

  return {
      path_to_utf8(checked_executable),
      "--prepared-root",
      path_to_utf8(checked_prepared_root),
      "--level",
      std::to_string(level_id),
  };
}

RuntimeLaunchPlanV1 make_runtime_launch_plan_v1(
    const std::filesystem::path &runtime_executable,
    const std::filesystem::path &prepared_root, const std::uint32_t level_id) {
  RuntimeLaunchPlanV1 result;
  result.executable = require_absolute_file_like_path(runtime_executable,
                                                      "Runtime executable");
  result.prepared_root = require_absolute_file_like_path(
      prepared_root, "PreparedGameV2 root");
  result.level_id = level_id;
  return result;
}

RuntimeLaunchPlanV1 make_runtime_startup_launch_plan_v1(
    const std::filesystem::path &runtime_executable,
    const std::filesystem::path &prepared_root) {
  auto result = make_runtime_launch_plan_v1(runtime_executable,prepared_root,0U);
  result.startup = true;
  return result;
}

} // namespace openrc::launcher
