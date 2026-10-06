#include "openrc/launcher_plan.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kDigest =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Function>
void expect_invalid_argument(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const std::invalid_argument &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::ApplicationPaths
paths_below(const std::filesystem::path &local_data) {
  openrc::ApplicationPaths paths;
  paths.local_data = local_data;
  paths.roaming_config = local_data.parent_path() / "config";
  paths.cache = local_data / "cache";
  paths.logs = local_data / "logs";
  return paths;
}

[[nodiscard]] std::filesystem::path absolute_test_root() {
  return std::filesystem::current_path() /
         std::filesystem::path(u8"launcher plan użytkownik") /
         std::filesystem::path(u8"dane z grą");
}

void test_install_paths_are_stable_and_separate() {
  const auto local_data = absolute_test_root();
  const auto launcher = local_data.parent_path() /
                        std::filesystem::path(u8"Program Ω") /
                        "openrc-launcher.exe";
  const auto plan = openrc::launcher::make_launcher_install_paths_v1(
      paths_below(local_data), launcher, kDigest);

  expect(plan.legacy_games_root == local_data / "games",
         "Legacy games root changed unexpectedly");
  expect(plan.prepared_game_root == local_data / "prepared-v2" /
                                        "openrc-rac-2002" /
                                        std::string(kDigest),
         "PreparedGameV2 root is not source-addressed or stable");
  expect(plan.prepared_game_root.parent_path().parent_path().parent_path() ==
             local_data,
         "PreparedGameV2 escaped the application local-data directory");
  expect(plan.prepared_game_root != plan.legacy_games_root,
         "PreparedGameV2 and legacy extraction roots overlap");
#ifdef _WIN32
  expect(plan.runtime_executable == launcher.parent_path() /
                                        "openrc-runtime.exe",
         "Runtime is not adjacent to the launcher");
#else
  expect(plan.runtime_executable == launcher.parent_path() / "openrc-runtime",
         "Runtime is not adjacent to the launcher");
#endif
}

void test_digest_and_path_validation() {
  const auto paths = paths_below(absolute_test_root());
  expect(openrc::launcher::is_lowercase_sha256_hex_v1(kDigest),
         "A valid lowercase SHA-256 digest was rejected");
  const auto parsed = openrc::launcher::parse_lowercase_sha256_hex_v1(kDigest);
  expect(parsed.front() == std::byte{0x01U} &&
             parsed[1U] == std::byte{0x23U} &&
             parsed.back() == std::byte{0xefU},
         "A valid SHA-256 digest decoded to the wrong bytes");
  expect(!openrc::launcher::is_lowercase_sha256_hex_v1(
             "0123456789abcdef"),
         "A short SHA-256 digest was accepted");
  expect(!openrc::launcher::is_lowercase_sha256_hex_v1(
             "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeG"),
         "An uppercase/non-hex SHA-256 digest was accepted");

  expect_invalid_argument(
      [&] {
        static_cast<void>(openrc::launcher::parse_lowercase_sha256_hex_v1(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeF"));
      },
      "Digest parsing accepted uppercase hexadecimal");
  expect_invalid_argument(
      [&] {
        static_cast<void>(openrc::launcher::prepared_game_v2_root_v1(
            paths,
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeF"));
      },
      "PreparedGameV2 accepted an uppercase digest");
  expect_invalid_argument(
      [&] {
        static_cast<void>(openrc::launcher::legacy_games_root_v1(
            paths_below("relative/local-data")));
      },
      "Legacy games planning accepted a relative local-data root");
  expect_invalid_argument(
      [&] {
        static_cast<void>(openrc::launcher::sibling_runtime_executable_v1(
            "relative/openrc-launcher.exe"));
      },
      "Runtime planning accepted a relative launcher path");
}

void test_runtime_plan_has_exact_package_only_argv() {
  const auto root = absolute_test_root();
  const auto runtime = root.parent_path() /
                       std::filesystem::path(u8"Program Ω") /
                       "openrc-runtime.exe";
  const auto prepared = root / "prepared-v2" / "openrc-rac-2002" /
                        std::string(kDigest);
  const auto plan = openrc::launcher::make_runtime_launch_plan_v1(
      runtime, prepared, 18U);
  const auto argv = plan.argv_utf8_v1();
  const std::vector<std::string> expected{
      openrc::path_to_utf8(runtime.lexically_normal()),
      "--prepared-root",
      openrc::path_to_utf8(prepared.lexically_normal()),
      "--level",
      "18",
  };

  expect(argv == expected,
         "Runtime argv differs from the PreparedGameV2-only contract");
  const auto startup = openrc::launcher::make_runtime_startup_launch_plan_v1(
      runtime, prepared).argv_utf8_v1();
  expect(startup == std::vector<std::string>(expected.begin(), expected.begin()+3),
         "Normal startup must not request a developer level or source ISO");
  expect(std::ranges::find(argv, "--disc-image") == argv.end() &&
             std::ranges::find(argv, "--boot-executable") == argv.end() &&
             std::ranges::find(argv, "--record") == argv.end() &&
             std::ranges::find(argv, "--entry-pair") == argv.end(),
         "Runtime argv leaked a legacy ISO/ELF/recovery flag");

  expect_invalid_argument(
      [&] {
        static_cast<void>(openrc::launcher::make_runtime_launch_plan_v1(
            "relative/openrc-runtime.exe", prepared, 0U));
      },
      "Runtime plan accepted a relative executable");
  expect_invalid_argument(
      [&] {
        static_cast<void>(openrc::launcher::make_runtime_launch_plan_v1(
            runtime, "relative/prepared-root", 0U));
      },
      "Runtime plan accepted a relative PreparedGameV2 root");
  expect_invalid_argument(
      [&] {
        static_cast<void>(openrc::launcher::make_runtime_launch_plan_v1(
            runtime, prepared.root_path(), 0U));
      },
      "Runtime plan accepted a filesystem root as PreparedGameV2");
}

} // namespace

int main() {
  try {
    test_install_paths_are_stable_and_separate();
    test_digest_and_path_validation();
    test_runtime_plan_has_exact_package_only_argv();
    std::cout << "launcher plan tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "launcher plan tests failed: " << error.what() << '\n';
    return 1;
  }
}
