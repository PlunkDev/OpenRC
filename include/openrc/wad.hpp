#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::string_view kWadV1Magic = "WAD";
inline constexpr std::uint32_t kWadV1HeaderSize = 0x10;
inline constexpr std::size_t kWadV1AuxiliarySize = 9;
inline constexpr std::uint64_t kWadSectorSize = 2048;

struct WadReport {
    std::filesystem::path image_path;
    std::uint64_t logical_block = 0;
    std::uint64_t sector_count = 0;
    std::uint64_t extent_bytes = 0;
    std::uint32_t total_bytes = 0;
    std::uint32_t compressed_payload_bytes = 0;
    std::uint64_t padding_bytes = 0;
    std::array<std::uint8_t, kWadV1AuxiliarySize> auxiliary_bytes{};
    std::string sha256;
};

struct DecodedWad {
    WadReport source;
    std::vector<std::byte> bytes;
    std::string sha256;
};

struct DecodedWadBytes {
    std::vector<std::byte> bytes;
    std::string sha256;
};

class WadError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] WadReport inspect_wad(
    const std::filesystem::path& image_path,
    std::uint64_t logical_block,
    std::uint64_t sector_count);

[[nodiscard]] DecodedWad decode_wad(
    const std::filesystem::path& image_path,
    std::uint64_t logical_block,
    std::uint64_t sector_count,
    std::uint64_t max_decoded_bytes);

[[nodiscard]] DecodedWadBytes decode_wad_bytes(
    std::span<const std::byte> logical_wad,
    std::uint64_t max_decoded_bytes);

} // namespace openrc
