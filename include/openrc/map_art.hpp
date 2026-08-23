#pragma once

#include "openrc/boundary_table.hpp"
#include "openrc/two_fip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::size_t kMapArtOwnedRegionCount = 4;
inline constexpr std::size_t kMapArtFirstImageRegion = 4;
inline constexpr std::size_t kMapArtImageCount = 3;
inline constexpr std::size_t kMapArtRegion0RecordCount = 256;
inline constexpr std::uint32_t kMapArtRegion0SecondHeaderWord = 32;
inline constexpr std::uint16_t kMapArtRegion0RelativeRecordBase = 0x200;
inline constexpr std::uint32_t kMapArtImageWidth = 128;
inline constexpr std::uint32_t kMapArtImageHeight = 128;

struct MapArtLimits {
    BoundaryTableLimits boundary_table;
    std::uint64_t max_pixels_per_image = 0;
};

struct MapArtRecordRange {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct MapArtV1 {
    BoundaryTable boundary_table;
    std::array<std::vector<std::byte>, kMapArtOwnedRegionCount> owned_regions;
    std::array<std::uint32_t, 2> region0_header_words{};
    std::array<std::uint16_t, kMapArtRegion0RecordCount> region0_end_offsets{};
    std::array<MapArtRecordRange, kMapArtRegion0RecordCount> region0_records{};
    std::uint32_t region0_logical_bytes = 0;
    std::uint32_t region0_padding_bytes = 0;
    std::array<TwoFipImage, kMapArtImageCount> images;
};

class MapArtError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses one complete seven-region MapArtV1 payload. Regions 0-3 are copied
// into the returned report. Region 0 additionally exposes its bounded record
// ranges, while regions 4-6 are parsed as exact 128x128 2FIP images.
[[nodiscard]] MapArtV1 parse_map_art_v1(
    std::span<const std::byte> bytes,
    MapArtLimits limits);

// Places image regions 4, 5, and 6 side by side in that order and returns an
// uncompressed, top-left-origin 384x128 32-bit TGA image.
[[nodiscard]] std::vector<std::byte> encode_map_art_tga(
    const MapArtV1& map_art);

} // namespace openrc
