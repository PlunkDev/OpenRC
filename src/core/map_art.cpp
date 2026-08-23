#include "openrc/map_art.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace openrc {
namespace {

constexpr std::uint32_t kRegion0HeaderSize = 8;
constexpr std::uint32_t kRegion0EndOffsetTableSize =
    kMapArtRegion0RecordCount * sizeof(std::uint16_t);
constexpr std::uint32_t kRegion0RecordDataOffset =
    kRegion0HeaderSize + kRegion0EndOffsetTableSize;
constexpr std::uint32_t kMapArtImageLogicalBytes =
    kTwoFipPixelDataOffset + kMapArtImageWidth * kMapArtImageHeight;

static_assert(
    kRegion0RecordDataOffset ==
        kRegion0HeaderSize + kMapArtRegion0RelativeRecordBase,
    "MapArtV1 region 0 records must start immediately after the end-offset table");

[[noreturn]] void fail(const std::string& message) {
    throw MapArtError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) {
    return static_cast<std::uint16_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint16_t>(byte_value(bytes[offset + 1U])) << 8U);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint32_t align_to_boundary_table(const std::uint32_t value) {
    constexpr auto mask = kBoundaryTableAlignment - 1U;
    static_assert((kBoundaryTableAlignment & mask) == 0U);
    return (value + mask) & ~mask;
}

void parse_region0(
    const std::span<const std::byte> region,
    MapArtV1& result) {
    if (region.size() < kRegion0RecordDataOffset) {
        fail("MapArtV1 region 0 is too small for its header and end-offset table");
    }

    result.region0_header_words = {read_le32(region, 0), read_le32(region, 4)};
    if (result.region0_header_words[0] != kMapArtRegion0RecordCount ||
        result.region0_header_words[1] != kMapArtRegion0SecondHeaderWord) {
        fail("MapArtV1 region 0 does not have the expected {256, 32} header");
    }

    std::uint16_t previous_end = kMapArtRegion0RelativeRecordBase;
    for (std::size_t index = 0; index < kMapArtRegion0RecordCount; ++index) {
        const auto end = read_le16(
            region,
            kRegion0HeaderSize + index * sizeof(std::uint16_t));
        if (end <= previous_end) {
            fail("MapArtV1 region 0 end offsets are not strictly ascending");
        }

        const auto absolute_begin =
            kRegion0HeaderSize + static_cast<std::uint32_t>(previous_end);
        const auto absolute_end =
            kRegion0HeaderSize + static_cast<std::uint32_t>(end);
        if (absolute_end > region.size()) {
            fail("MapArtV1 region 0 record points outside its region");
        }

        result.region0_end_offsets[index] = end;
        result.region0_records[index] = MapArtRecordRange{
            absolute_begin,
            absolute_end - absolute_begin,
        };
        previous_end = end;
    }

    result.region0_logical_bytes =
        kRegion0HeaderSize + static_cast<std::uint32_t>(previous_end);
    const auto expected_envelope = align_to_boundary_table(result.region0_logical_bytes);
    if (expected_envelope != region.size()) {
        fail("MapArtV1 region 0 is not the minimum 0x10-aligned record envelope");
    }

    const auto padding = region.subspan(result.region0_logical_bytes);
    if (std::any_of(
            padding.begin(),
            padding.end(),
            [](const std::byte value) { return value != std::byte{0}; })) {
        fail("MapArtV1 region 0 has non-zero alignment padding");
    }
    result.region0_padding_bytes =
        static_cast<std::uint32_t>(padding.size());
}

[[nodiscard]] bool matching_image_metadata(
    const TwoFipImage& left,
    const TwoFipImage& right) {
    return left.opaque_identifier == right.opaque_identifier &&
        left.width == right.width &&
        left.height == right.height &&
        left.pixel_storage_format == right.pixel_storage_format &&
        left.trailing_header_fields == right.trailing_header_fields &&
        left.palette == right.palette;
}

void validate_map_art_image(
    const TwoFipImage& image,
    const std::size_t region_index) {
    if (image.width != kMapArtImageWidth ||
        image.height != kMapArtImageHeight) {
        fail(
            "MapArtV1 image region " + std::to_string(region_index) +
            " is not exactly 128x128");
    }
    if (image.logical_bytes != kMapArtImageLogicalBytes ||
        image.padding_bytes != 0) {
        fail(
            "MapArtV1 image region " + std::to_string(region_index) +
            " is not one exact 2FIP logical image");
    }
}

} // namespace

MapArtV1 parse_map_art_v1(
    const std::span<const std::byte> bytes,
    const MapArtLimits limits) {
    MapArtV1 result;
    try {
        result.boundary_table = parse_boundary_table(bytes, limits.boundary_table);
    } catch (const BoundaryTableError& error) {
        fail("Invalid MapArtV1 boundary table: " + std::string(error.what()));
    }

    const auto& region0_description = result.boundary_table.regions[0];
    const auto region0 = bytes.subspan(
        region0_description.offset,
        region0_description.size);
    parse_region0(region0, result);

    for (std::size_t index = 0; index < result.owned_regions.size(); ++index) {
        const auto& region = result.boundary_table.regions[index];
        const auto source = bytes.subspan(region.offset, region.size);
        result.owned_regions[index].assign(source.begin(), source.end());
    }

    std::span<const std::byte> first_image_prefix;
    for (std::size_t image_index = 0;
         image_index < result.images.size();
         ++image_index) {
        const auto region_index = kMapArtFirstImageRegion + image_index;
        const auto& region = result.boundary_table.regions[region_index];
        const auto source = bytes.subspan(region.offset, region.size);
        try {
            result.images[image_index] = parse_two_fip(
                source,
                limits.max_pixels_per_image);
        } catch (const TwoFipError& error) {
            fail(
                "Invalid 2FIP image in MapArtV1 region " +
                std::to_string(region_index) + ": " + error.what());
        }
        validate_map_art_image(result.images[image_index], region_index);

        const auto prefix = source.first(kTwoFipPixelDataOffset);
        if (image_index == 0) {
            first_image_prefix = prefix;
        } else if (!std::equal(
                       first_image_prefix.begin(),
                       first_image_prefix.end(),
                       prefix.begin(),
                       prefix.end())) {
            fail("MapArtV1 image regions do not share an identical header and palette");
        }
    }

    return result;
}

std::vector<std::byte> encode_map_art_tga(const MapArtV1& map_art) {
    const auto& first = map_art.images.front();
    constexpr auto pixels_per_image =
        static_cast<std::size_t>(kMapArtImageWidth) * kMapArtImageHeight;

    for (std::size_t image_index = 0;
         image_index < map_art.images.size();
         ++image_index) {
        const auto& image = map_art.images[image_index];
        validate_map_art_image(image, kMapArtFirstImageRegion + image_index);
        if (!matching_image_metadata(first, image) ||
            image.indices.size() != pixels_per_image) {
            fail("MapArtV1 in-memory image metadata or indexed pixels are inconsistent");
        }
    }

    TwoFipImage atlas = first;
    atlas.width = kMapArtImageWidth * kMapArtImageCount;
    atlas.height = kMapArtImageHeight;
    atlas.logical_bytes = kTwoFipPixelDataOffset +
        static_cast<std::uint64_t>(atlas.width) * atlas.height;
    atlas.padding_bytes = 0;
    atlas.indices.clear();
    atlas.indices.reserve(pixels_per_image * kMapArtImageCount);

    for (std::uint32_t row = 0; row < kMapArtImageHeight; ++row) {
        const auto row_offset =
            static_cast<std::size_t>(row) * kMapArtImageWidth;
        for (const auto& image : map_art.images) {
            const auto source = std::span<const std::uint8_t>(image.indices).subspan(
                row_offset,
                kMapArtImageWidth);
            atlas.indices.insert(atlas.indices.end(), source.begin(), source.end());
        }
    }

    try {
        return encode_two_fip_tga(atlas);
    } catch (const TwoFipError& error) {
        fail("Cannot encode the MapArtV1 TGA atlas: " + std::string(error.what()));
    }
}

} // namespace openrc
