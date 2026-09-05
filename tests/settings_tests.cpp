#include "openrc/paths.hpp"
#include "openrc/settings.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_text(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << bytes;
    if (!output) {
        throw std::runtime_error("failed to write settings fixture");
    }
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

#ifdef _WIN32
void set_environment(const wchar_t* name, const std::filesystem::path& value) {
    if (_wputenv_s(name, value.c_str()) != 0) {
        throw std::runtime_error("failed to set a test environment variable");
    }
}
#else
void set_environment(const char* name, const std::filesystem::path& value) {
    const auto encoded = value.string();
    if (::setenv(name, encoded.c_str(), 1) != 0) {
        throw std::runtime_error("failed to set a test environment variable");
    }
}
#endif

void test_local_settings_migration(const std::filesystem::path& directory) {
    const auto roaming_root = directory / "roaming";
    const auto local_root = directory / "local";
#ifdef _WIN32
    set_environment(L"APPDATA", roaming_root);
    set_environment(L"LOCALAPPDATA", local_root);
#else
    set_environment("HOME", directory / "home");
    set_environment("XDG_CONFIG_HOME", roaming_root);
    set_environment("XDG_DATA_HOME", local_root);
    set_environment("XDG_CACHE_HOME", directory / "cache");
#endif

    const auto paths = openrc::application_paths();
    expect(
        paths.roaming_config == roaming_root / "PlunkDev" / "OpenRC",
        "roaming configuration does not use the PlunkDev/OpenRC convention");
    expect(
        paths.local_data == local_root / "PlunkDev" / "OpenRC",
        "local data does not use the PlunkDev/OpenRC convention");

    const auto legacy_path = paths.roaming_config / "launcher.ini";
    const auto current_path = paths.local_data / "launcher.ini";
    std::filesystem::create_directories(paths.roaming_config);

    const auto legacy_iso = directory / "legacy.iso";
    const auto legacy_bytes =
        "iso_path=" + openrc::path_to_utf8(legacy_iso) + "\n";
    write_text(legacy_path, legacy_bytes);

    const auto migrated = openrc::load_launcher_settings();
    expect(migrated.iso_path == legacy_iso, "legacy launcher selection was not loaded");
    expect(
        std::filesystem::is_regular_file(current_path),
        "legacy launcher settings were not copied to local data");
    expect(
        read_text(current_path) ==
            "format_version=2\n" + legacy_bytes,
        "migrated launcher settings were not atomically upgraded to version 2");
    expect(read_text(legacy_path) == legacy_bytes, "migration modified legacy settings");

    const auto local_iso = directory / "local.iso";
    write_text(
        current_path,
        "iso_path=" + openrc::path_to_utf8(local_iso) + "\n");
    expect(
        openrc::load_launcher_settings().iso_path == local_iso,
        "local launcher settings did not take precedence after migration");

    const auto saved_iso = directory / "saved.iso";
    const auto prepared_root =
        std::filesystem::absolute(directory / "prepared native game");
    const std::string manifest_digest(64U, 'a');
    openrc::save_launcher_settings(openrc::LauncherSettings{
        saved_iso, prepared_root, manifest_digest});
    const auto saved = openrc::load_launcher_settings();
    expect(
        saved.iso_path == saved_iso,
        "saved local launcher selection was not reloaded");
    expect(
        saved.prepared_game_root == prepared_root &&
            saved.prepared_game_manifest_sha256 == manifest_digest,
        "saved prepared game identity was not reloaded");
    expect(
        read_text(current_path).starts_with("format_version=2\n"),
        "new launcher settings did not declare format version 2");
    expect(read_text(legacy_path) == legacy_bytes, "saving modified legacy settings");

    const auto previous_bytes = read_text(current_path);
    bool invalid_save_rejected = false;
    try {
        openrc::save_launcher_settings(openrc::LauncherSettings{
            saved_iso, prepared_root, "NOT-A-DIGEST"});
    } catch (const std::runtime_error&) {
        invalid_save_rejected = true;
    }
    expect(invalid_save_rejected, "invalid prepared identity was saved");
    expect(
        read_text(current_path) == previous_bytes,
        "failed settings save modified the previous file");

    write_text(
        current_path,
        "format_version=999\niso_path=" + openrc::path_to_utf8(saved_iso) +
            "\n");
    bool newer_format_rejected = false;
    try {
        static_cast<void>(openrc::load_launcher_settings());
    } catch (const std::runtime_error&) {
        newer_format_rejected = true;
    }
    expect(newer_format_rejected, "newer launcher settings were accepted");
}

} // namespace

int main() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("OpenRC-settings-tests-" + std::to_string(suffix));
    try {
        std::filesystem::create_directories(directory);
        test_local_settings_migration(directory);
        std::filesystem::remove_all(directory);
        std::cout << "OpenRC settings migration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::cerr << "OpenRC settings migration tests failed: " << error.what() << '\n';
        return 1;
    }
}
