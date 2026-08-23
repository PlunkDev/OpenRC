#pragma once

#include "openrc/disc.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

struct DiscExtent {
    std::uint32_t logical_block = 0;
    std::uint32_t byte_length = 0;
};

struct DiscFile {
    std::string iso_path;
    std::string output_path;
    std::uint64_t size = 0;
    std::vector<DiscExtent> extents;
};

struct DiscInventory {
    DiscReport disc;
    std::vector<DiscFile> files;
    std::uint64_t total_file_bytes = 0;
};

enum class PreparationPhase {
    scanning,
    hashing_image,
    verifying_files,
    extracting_files,
    writing_manifest,
    finalizing,
};

struct PreparationProgress {
    PreparationPhase phase = PreparationPhase::hashing_image;
    std::size_t file_index = 0;
    std::size_t file_count = 0;
    std::uint64_t bytes_processed = 0;
    std::uint64_t total_bytes = 0;
    std::string current_path;
};

using PreparationProgressCallback = std::function<bool(const PreparationProgress&)>;

struct PreparationResult {
    std::filesystem::path destination;
    std::filesystem::path manifest_path;
    std::filesystem::path boot_executable_path;
    std::string image_sha256;
    std::size_t file_count = 0;
    std::uint64_t total_file_bytes = 0;
    bool already_prepared = false;
};

class PreparationCancelled final : public std::runtime_error {
public:
    PreparationCancelled();
};

[[nodiscard]] DiscInventory inventory_disc(const std::filesystem::path& image_path);

[[nodiscard]] PreparationResult prepare_game_files(
    const std::filesystem::path& image_path,
    const std::filesystem::path& games_directory,
    const PreparationProgressCallback& progress = {});

} // namespace openrc
