#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace openrc::iso9660 {

inline constexpr std::uint32_t kLogicalBlockSize = 2048;

class Error final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Extent {
    std::uint32_t logical_block{};
    std::uint32_t byte_length{};
    std::uint8_t extended_attribute_blocks{};
};

struct File {
    // Absolute ISO path, preserving the on-disc identifier (including ;version).
    std::string iso_path;
    // Relative, portable extraction path using '/' separators.
    std::string output_path;
    std::uint64_t size{};
    // One entry for an ordinary file, or the ordered records of a multi-extent file.
    std::vector<Extent> extents;
};

struct Metadata {
    std::string system_identifier;
    std::string volume_identifier;
    std::uint32_t volume_blocks{};
    std::uint16_t logical_block_size{};
    std::uint64_t declared_volume_bytes{};
    std::uint64_t image_bytes{};
    std::uint32_t primary_descriptor_sector{};
};

class Image final {
public:
    using ChunkCallback = std::function<void(std::span<const std::byte>)>;

    [[nodiscard]] static Image open(const std::filesystem::path& source);

    [[nodiscard]] const Metadata& metadata() const noexcept { return metadata_; }
    [[nodiscard]] const std::vector<File>& files() const noexcept { return files_; }

    // Performs a case-sensitive lookup of the absolute raw ISO path.
    [[nodiscard]] const File* find_exact(std::string_view iso_path) const noexcept;

    // Reads the complete file, failing before allocation when it exceeds max_bytes.
    [[nodiscard]] std::vector<std::byte> read_small_file(
        const File& file,
        std::uint64_t max_bytes) const;

    // Streams the complete logical file in bounded chunks, in extent order.
    void stream_file(const File& file, const ChunkCallback& callback) const;

private:
    std::filesystem::path source_;
    Metadata metadata_;
    std::vector<File> files_;
};

}  // namespace openrc::iso9660
