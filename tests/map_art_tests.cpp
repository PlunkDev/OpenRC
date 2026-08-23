#include "openrc/map_art.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kRegion0Size = 0x310;
constexpr std::uint32_t kImageSize = 0x4420;
constexpr std::uint64_t kImagePixels =
    static_cast<std::uint64_t>(openrc::kMapArtImageWidth) *
    openrc::kMapArtImageHeight;
constexpr std::array<std::uint32_t, openrc::kBoundaryTableBoundaryCount>
    kBoundaries{
        0x20U,
        0x330U,
        0x340U,
        0x350U,
        0x360U,
        0x4780U,
        0x8ba0U,
        0xcfc0U,
    };
constexpr openrc::MapArtLimits kLimits{
    openrc::BoundaryTableLimits{kBoundaries.back(), kImageSize},
    kImagePixels,
};

[[nodiscard]] std::uint8_t byte_value(const std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

void write_le16(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_le32(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

[[nodiscard]] std::vector<std::byte> make_two_fip(
    const std::uint8_t pixel_index) {
    std::vector<std::byte> bytes(kImageSize, std::byte{0});
    for (std::size_t index = 0; index < openrc::kTwoFipMagic.size(); ++index) {
        bytes[index] = static_cast<std::byte>(openrc::kTwoFipMagic[index]);
    }
    write_le32(bytes, 4U, 0x12345678U);
    write_le32(bytes, 8U, openrc::kMapArtImageWidth);
    write_le32(bytes, 12U, openrc::kMapArtImageHeight);
    write_le32(bytes, 16U, openrc::kTwoFipPsmT8Format);
    write_le32(bytes, 28U, 1U);

    const std::array<std::array<std::uint8_t, 4>, 3> colors{{
        {{10U, 20U, 30U, 0x80U}},
        {{40U, 50U, 60U, 0x80U}},
        {{70U, 80U, 90U, 0x80U}},
    }};
    for (std::size_t index = 0; index < colors.size(); ++index) {
        const auto offset = openrc::kTwoFipHeaderSize + index * 4U;
        for (std::size_t channel = 0; channel < colors[index].size(); ++channel) {
            bytes[offset + channel] = static_cast<std::byte>(colors[index][channel]);
        }
    }

    std::fill(
        bytes.begin() + static_cast<std::ptrdiff_t>(openrc::kTwoFipPixelDataOffset),
        bytes.end(),
        static_cast<std::byte>(pixel_index));
    return bytes;
}

[[nodiscard]] std::vector<std::byte> make_payload() {
    std::vector<std::byte> bytes(kBoundaries.back(), std::byte{0});
    for (std::size_t index = 0; index < kBoundaries.size(); ++index) {
        write_le32(bytes, index * sizeof(std::uint32_t), kBoundaries[index]);
    }

    const auto region0 = std::span<std::byte>(bytes).subspan(
        kBoundaries[0],
        kRegion0Size);
    write_le32(region0, 0, openrc::kMapArtRegion0RecordCount);
    write_le32(region0, 4, openrc::kMapArtRegion0SecondHeaderWord);
    for (std::size_t index = 0;
         index < openrc::kMapArtRegion0RecordCount;
         ++index) {
        const auto end = static_cast<std::uint16_t>(
            openrc::kMapArtRegion0RelativeRecordBase + index + 1U);
        write_le16(region0, 8U + index * sizeof(std::uint16_t), end);
        region0[0x208U + index] = static_cast<std::byte>(index & 0xffU);
    }

    for (std::size_t region_index = 1; region_index < 4; ++region_index) {
        std::fill(
            bytes.begin() + kBoundaries[region_index],
            bytes.begin() + kBoundaries[region_index + 1U],
            static_cast<std::byte>(0x40U + region_index));
    }
    for (std::size_t image_index = 0;
         image_index < openrc::kMapArtImageCount;
         ++image_index) {
        const auto image = make_two_fip(static_cast<std::uint8_t>(image_index));
        const auto destination = kBoundaries[
            openrc::kMapArtFirstImageRegion + image_index];
        std::copy(image.begin(), image.end(), bytes.begin() + destination);
    }
    return bytes;
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_parse_rejected(
    const std::vector<std::byte>& bytes,
    const openrc::MapArtLimits limits,
    const std::string& message) {
    bool rejected = false;
    try {
        (void)openrc::parse_map_art_v1(bytes, limits);
    } catch (const openrc::MapArtError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void test_valid_map_art_and_owned_report() {
    auto source = make_payload();
    const auto original = source;
    const auto map_art = openrc::parse_map_art_v1(source, kLimits);

    expect(map_art.boundary_table.boundaries == kBoundaries, "boundaries changed");
    expect(
        map_art.region0_header_words == std::array<std::uint32_t, 2>{256U, 32U},
        "region 0 header changed");
    expect(map_art.region0_logical_bytes == 0x308U, "region 0 logical end is wrong");
    expect(map_art.region0_padding_bytes == 8U, "region 0 padding size is wrong");
    expect(
        map_art.region0_records.front().offset == 0x208U &&
            map_art.region0_records.front().size == 1U,
        "first region 0 record range is wrong");
    expect(
        map_art.region0_records.back().offset == 0x307U &&
            map_art.region0_records.back().size == 1U,
        "last region 0 record range is wrong");

    for (std::size_t region_index = 0;
         region_index < map_art.owned_regions.size();
         ++region_index) {
        const auto begin = original.begin() + kBoundaries[region_index];
        const auto end = original.begin() + kBoundaries[region_index + 1U];
        expect(
            std::equal(
                map_art.owned_regions[region_index].begin(),
                map_art.owned_regions[region_index].end(),
                begin,
                end),
            "owned region bytes changed");
    }
    std::fill(source.begin(), source.end(), std::byte{0});
    expect(
        map_art.owned_regions[1].front() == std::byte{0x41},
        "report retained a borrowed source span");

    for (std::size_t image_index = 0;
         image_index < map_art.images.size();
         ++image_index) {
        const auto& image = map_art.images[image_index];
        expect(
            image.width == openrc::kMapArtImageWidth &&
                image.height == openrc::kMapArtImageHeight,
            "map-art image dimensions are wrong");
        expect(image.padding_bytes == 0U, "exact image reported padding");
        expect(
            image.indices.front() == static_cast<std::uint8_t>(image_index),
            "image region order changed");
    }
}

void test_atlas_ordering() {
    auto map_art = openrc::parse_map_art_v1(make_payload(), kLimits);
    const auto tga = openrc::encode_map_art_tga(map_art);
    constexpr std::size_t kTgaHeaderSize = 18;
    constexpr std::size_t kAtlasWidth =
        openrc::kMapArtImageWidth * openrc::kMapArtImageCount;
    constexpr std::size_t kAtlasPixelBytes =
        kAtlasWidth * openrc::kMapArtImageHeight * 4U;
    expect(tga.size() == kTgaHeaderSize + kAtlasPixelBytes, "atlas size is wrong");
    expect(byte_value(tga[2]) == 2U, "atlas is not an uncompressed true-color TGA");
    expect(
        byte_value(tga[12]) == 0x80U && byte_value(tga[13]) == 0x01U,
        "atlas width is not 384");
    expect(
        byte_value(tga[14]) == 0x80U && byte_value(tga[15]) == 0U,
        "atlas height is not 128");
    expect(byte_value(tga[16]) == 32U, "atlas is not 32-bit");
    expect(byte_value(tga[17]) == 0x28U, "atlas origin or alpha depth is wrong");

    const std::array<std::array<std::uint8_t, 4>, 3> expected_bgra{{
        {{30U, 20U, 10U, 255U}},
        {{60U, 50U, 40U, 255U}},
        {{90U, 80U, 70U, 255U}},
    }};
    for (std::size_t image_index = 0;
         image_index < expected_bgra.size();
         ++image_index) {
        const auto pixel_offset = kTgaHeaderSize +
            image_index * openrc::kMapArtImageWidth * 4U;
        for (std::size_t channel = 0; channel < 4; ++channel) {
            expect(
                byte_value(tga[pixel_offset + channel]) ==
                    expected_bgra[image_index][channel],
                "atlas panel ordering or BGRA conversion is wrong");
        }
    }

    map_art.images[1].indices.pop_back();
    bool rejected = false;
    try {
        (void)openrc::encode_map_art_tga(map_art);
    } catch (const openrc::MapArtError&) {
        rejected = true;
    }
    expect(rejected, "atlas accepted an inconsistent in-memory index vector");
}

void test_boundary_and_region0_rejections() {
    auto bytes = make_payload();
    write_le32(bytes, 0, 0x10U);
    expect_parse_rejected(bytes, kLimits, "invalid boundary header was accepted");

    bytes = make_payload();
    write_le32(bytes, kBoundaries[0], 255U);
    expect_parse_rejected(bytes, kLimits, "wrong region 0 record count was accepted");

    bytes = make_payload();
    write_le32(bytes, kBoundaries[0] + 4U, 31U);
    expect_parse_rejected(bytes, kLimits, "wrong region 0 second word was accepted");

    bytes = make_payload();
    write_le16(bytes, kBoundaries[0] + 8U + sizeof(std::uint16_t), 0x201U);
    expect_parse_rejected(bytes, kLimits, "duplicate region 0 end offset was accepted");

    bytes = make_payload();
    write_le16(
        bytes,
        kBoundaries[0] + 8U +
            (openrc::kMapArtRegion0RecordCount - 1U) * sizeof(std::uint16_t),
        0x400U);
    expect_parse_rejected(bytes, kLimits, "out-of-bounds region 0 end was accepted");

    bytes = make_payload();
    bytes[kBoundaries[1] - 1U] = std::byte{1};
    expect_parse_rejected(bytes, kLimits, "non-zero region 0 padding was accepted");

    bytes = make_payload();
    bytes.insert(bytes.begin() + kBoundaries[1], 0x10U, std::byte{0});
    for (std::size_t boundary_index = 1;
         boundary_index < kBoundaries.size();
         ++boundary_index) {
        write_le32(
            bytes,
            boundary_index * sizeof(std::uint32_t),
            kBoundaries[boundary_index] + 0x10U);
    }
    expect_parse_rejected(
        bytes,
        openrc::MapArtLimits{
            openrc::BoundaryTableLimits{kBoundaries.back() + 0x10U, kImageSize},
            kImagePixels,
        },
        "a non-minimal aligned region 0 envelope was accepted");
}

void test_image_and_cap_rejections() {
    auto bytes = make_payload();
    bytes[kBoundaries[4]] = std::byte{'X'};
    expect_parse_rejected(bytes, kLimits, "invalid 2FIP signature was accepted");

    bytes = make_payload();
    write_le32(bytes, kBoundaries[4] + 8U, 64U);
    write_le32(bytes, kBoundaries[4] + 12U, 256U);
    expect_parse_rejected(bytes, kLimits, "non-128x128 image was accepted");

    bytes = make_payload();
    write_le32(bytes, kBoundaries[4] + 8U, 127U);
    expect_parse_rejected(bytes, kLimits, "2FIP storage padding was accepted");

    bytes = make_payload();
    write_le32(bytes, kBoundaries[5] + 4U, 0x87654321U);
    expect_parse_rejected(bytes, kLimits, "mismatched image header was accepted");

    bytes = make_payload();
    bytes[kBoundaries[6] + openrc::kTwoFipHeaderSize] = std::byte{0xee};
    expect_parse_rejected(bytes, kLimits, "mismatched image palette was accepted");

    expect_parse_rejected(
        make_payload(),
        openrc::MapArtLimits{
            openrc::BoundaryTableLimits{kBoundaries.back() - 1U, kImageSize},
            kImagePixels,
        },
        "caller input cap was ignored");
    expect_parse_rejected(
        make_payload(),
        openrc::MapArtLimits{
            openrc::BoundaryTableLimits{kBoundaries.back(), kImageSize - 1U},
            kImagePixels,
        },
        "caller region cap was ignored");
    expect_parse_rejected(
        make_payload(),
        openrc::MapArtLimits{
            openrc::BoundaryTableLimits{kBoundaries.back(), kImageSize},
            kImagePixels - 1U,
        },
        "caller pixel cap was ignored");
}

} // namespace

int main() {
    try {
        test_valid_map_art_and_owned_report();
        test_atlas_ordering();
        test_boundary_and_region0_rejections();
        test_image_and_cap_rejections();
        std::cout << "OpenRC MapArtV1 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC MapArtV1 tests failed: " << error.what() << '\n';
        return 1;
    }
}
