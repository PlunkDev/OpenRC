#include "openrc/rac_level_moby_texture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kTextureBase = 0x20U;
constexpr openrc::RacLevelMobyTextureLimitsV1 kLimits{
    0x40U,
    0x40U,
    0x800U,
    4U,
    8U,
    8U,
    64U,
    128U,
    512U,
};

struct Fixture {
    std::vector<std::byte> table;
    std::vector<std::byte> core;
    std::vector<std::byte> gs_ram;
};

void expect(const bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void expect_texture_error(Function &&function, const std::string &message) {
    bool rejected = false;
    try {
        function();
    } catch (const openrc::RacLevelMobyTextureError &) {
        rejected = true;
    }
    expect(rejected, message);
}

[[nodiscard]] std::uint8_t value(const std::byte byte) noexcept {
    return std::to_integer<std::uint8_t>(byte);
}

void write_le16(std::vector<std::byte> &bytes,
                const std::size_t offset,
                const std::uint16_t input) {
    bytes.at(offset) = static_cast<std::byte>(input & 0xffU);
    bytes.at(offset + 1U) =
        static_cast<std::byte>((input >> 8U) & 0xffU);
}

void write_le32(std::vector<std::byte> &bytes,
                const std::size_t offset,
                const std::uint32_t input) {
    bytes.at(offset) = static_cast<std::byte>(input & 0xffU);
    bytes.at(offset + 1U) =
        static_cast<std::byte>((input >> 8U) & 0xffU);
    bytes.at(offset + 2U) =
        static_cast<std::byte>((input >> 16U) & 0xffU);
    bytes.at(offset + 3U) =
        static_cast<std::byte>((input >> 24U) & 0xffU);
}

void write_entry(std::vector<std::byte> &table,
                 const std::size_t index,
                 const std::int32_t data_offset,
                 const std::int16_t width,
                 const std::int16_t height,
                 const std::int16_t type,
                 const std::int16_t palette_block,
                 const std::int16_t mipmap_block,
                 const std::int16_t padding) {
    const auto offset =
        index * openrc::kRacLevelMobyTextureEntryBytesV1;
    write_le32(table, offset, static_cast<std::uint32_t>(data_offset));
    write_le16(table, offset + 0x04U, static_cast<std::uint16_t>(width));
    write_le16(table, offset + 0x06U, static_cast<std::uint16_t>(height));
    write_le16(table, offset + 0x08U, static_cast<std::uint16_t>(type));
    write_le16(
        table, offset + 0x0aU, static_cast<std::uint16_t>(palette_block));
    write_le16(
        table, offset + 0x0cU, static_cast<std::uint16_t>(mipmap_block));
    write_le16(table, offset + 0x0eU, static_cast<std::uint16_t>(padding));
}

void write_raw_palette_color(std::vector<std::byte> &gs_ram,
                             const std::size_t palette_offset,
                             const std::uint8_t raw_index,
                             const std::array<std::uint8_t, 4U> rgba) {
    const auto offset = palette_offset + static_cast<std::size_t>(raw_index) * 4U;
    for (std::size_t channel = 0U; channel < rgba.size(); ++channel) {
        gs_ram.at(offset + channel) = static_cast<std::byte>(rgba[channel]);
    }
}

[[nodiscard]] Fixture make_fixture() {
    Fixture fixture;
    fixture.table.resize(
        2U * openrc::kRacLevelMobyTextureEntryBytesV1, std::byte{0});
    fixture.core.resize(0x40U, std::byte{0});
    fixture.gs_ram.resize(0x800U, std::byte{0});

    write_entry(fixture.table, 0U, 4, 2, 2, 3, 0, -1, -1);
    // An arbitrary type is retained rather than interpreted by this RAC1
    // base-image decoder.
    write_entry(fixture.table, 1U, 8, 1, 2, 7, 4, 5, 6);

    fixture.core[kTextureBase + 4U] = std::byte{8U};
    fixture.core[kTextureBase + 5U] = std::byte{16U};
    fixture.core[kTextureBase + 6U] = std::byte{24U};
    fixture.core[kTextureBase + 7U] = std::byte{1U};
    fixture.core[kTextureBase + 8U] = std::byte{0U};
    fixture.core[kTextureBase + 9U] = std::byte{255U};

    // Logical CLUT 8 reads raw 16 and logical 16 reads raw 8.
    write_raw_palette_color(
        fixture.gs_ram, 0U, 16U, {10U, 20U, 30U, 0x40U});
    write_raw_palette_color(
        fixture.gs_ram, 0U, 8U, {40U, 50U, 60U, 0x80U});
    write_raw_palette_color(
        fixture.gs_ram, 0U, 24U, {70U, 80U, 90U, 0x90U});
    write_raw_palette_color(
        fixture.gs_ram, 0U, 1U, {100U, 110U, 120U, 0x7fU});

    constexpr std::size_t second_palette = 4U * 0x100U;
    write_raw_palette_color(
        fixture.gs_ram, second_palette, 0U, {1U, 2U, 3U, 4U});
    write_raw_palette_color(
        fixture.gs_ram, second_palette, 255U, {5U, 6U, 7U, 0xffU});
    return fixture;
}

[[nodiscard]] openrc::RacLevelMobyTextureBankV1 decode(
    const Fixture &fixture,
    const openrc::RacLevelMobyTextureLimitsV1 limits = kLimits,
    const std::uint64_t texture_base = kTextureBase) {
    return openrc::decode_rac_level_moby_texture_bank_v1(
        fixture.table, fixture.core, fixture.gs_ram, texture_base, limits);
}

void expect_rgba(const std::span<const std::byte> bytes,
                 const std::size_t pixel,
                 const std::array<std::uint8_t, 4U> expected,
                 const std::string &description) {
    const auto offset = pixel * 4U;
    for (std::size_t channel = 0U; channel < expected.size(); ++channel) {
        expect(value(bytes[offset + channel]) == expected[channel],
               description + " channel differs");
    }
}

void test_bank_decode_and_metadata() {
    const auto fixture = make_fixture();
    const auto bank = decode(fixture);

    expect(bank.texture_table_input_bytes == 0x20U,
           "texture-table input size is wrong");
    expect(bank.decoded_core_input_bytes == 0x40U,
           "decoded-core input size is wrong");
    expect(bank.gs_ram_input_bytes == 0x800U,
           "GS RAM input size is wrong");
    expect(bank.textures_base_offset == kTextureBase,
           "texture base was not retained");
    expect(bank.total_pixel_count == 6U, "total pixel count is wrong");
    expect(bank.total_rgba_bytes == 24U, "total RGBA size is wrong");
    expect(bank.textures.size() == 2U, "texture count is wrong");

    const auto &first = bank.textures[0U];
    expect(first.global_index == 0U, "first global index is wrong");
    expect(first.entry.table_entry_range ==
               openrc::RacLevelMobyTextureRangeV1{0U, 0x10U},
           "first table-entry range is wrong");
    expect(first.entry.data_offset == 4 && first.entry.width == 2 &&
               first.entry.height == 2 && first.entry.type == 3 &&
               first.entry.palette_block == 0 &&
               first.entry.mipmap_block == -1 &&
               first.entry.trailing_block == -1,
           "first TextureEntry metadata is wrong");
    expect(first.entry.pixel_range ==
               openrc::RacLevelMobyTextureRangeV1{kTextureBase + 4U, 4U},
           "first pixel range is wrong");
    expect(first.entry.palette_range ==
               openrc::RacLevelMobyTextureRangeV1{0U, 0x400U},
           "first palette range is wrong");
    expect(first.indices ==
               std::vector<std::byte>{
                   std::byte{8U}, std::byte{16U}, std::byte{24U},
                   std::byte{1U}},
           "RAC1 pixel indices were reordered");

    expect_rgba(first.palette_rgba, 8U, {10U, 20U, 30U, 0x80U},
                "logical CLUT entry 8");
    expect_rgba(first.palette_rgba, 16U, {40U, 50U, 60U, 0xffU},
                "logical CLUT entry 16");
    expect_rgba(first.palette_rgba, 24U, {70U, 80U, 90U, 0xffU},
                "logical CLUT entry 24");
    expect_rgba(first.palette_rgba, 1U, {100U, 110U, 120U, 0xfeU},
                "logical CLUT entry 1");
    expect_rgba(first.palette_raw_rgba, 8U, {10U, 20U, 30U, 0x40U},
                "raw logical CLUT entry 8 retains half alpha");
    expect_rgba(first.palette_raw_rgba, 16U, {40U, 50U, 60U, 0x80U},
                "raw logical CLUT entry 16 retains unit alpha");
    expect_rgba(first.palette_raw_rgba, 1U, {100U, 110U, 120U, 0x7fU},
                "raw logical CLUT entry 1 retains alpha below unit");
    expect_rgba(first.rgba, 0U, {10U, 20U, 30U, 0x80U},
                "first expanded pixel");
    expect_rgba(first.rgba, 1U, {40U, 50U, 60U, 0xffU},
                "second expanded pixel");
    expect_rgba(first.rgba, 2U, {70U, 80U, 90U, 0xffU},
                "third expanded pixel");
    expect_rgba(first.rgba, 3U, {100U, 110U, 120U, 0xfeU},
                "fourth expanded pixel");

    const auto &second = bank.textures[1U];
    expect_rgba(second.palette_raw_rgba, 255U, {5U, 6U, 7U, 0xffU},
                "raw logical CLUT retains alpha beyond unit");
    expect(second.global_index == 1U, "second global index is wrong");
    expect(second.entry.table_entry_range ==
               openrc::RacLevelMobyTextureRangeV1{0x10U, 0x10U},
           "second table-entry range is wrong");
    expect(second.entry.type == 7 && second.entry.mipmap_block == 5 &&
               second.entry.trailing_block == 6,
           "opaque TextureEntry fields were not retained");
    expect(second.entry.pixel_range ==
               openrc::RacLevelMobyTextureRangeV1{kTextureBase + 8U, 2U},
           "second pixel range is wrong");
    expect(second.entry.palette_range ==
               openrc::RacLevelMobyTextureRangeV1{0x400U, 0x400U},
           "second palette range is wrong");
    expect_rgba(second.rgba, 0U, {1U, 2U, 3U, 8U},
                "second texture first pixel");
    expect_rgba(second.rgba, 1U, {5U, 6U, 7U, 0xffU},
                "second texture last pixel");
}

void test_tga_export() {
    const auto bank = decode(make_fixture());
    const auto tga = openrc::encode_rac_level_moby_texture_tga_v1(
        bank.textures[0U], 34U);

    expect(tga.global_index == 0U && tga.width == 2U && tga.height == 2U,
           "TGA metadata is wrong");
    expect(tga.bytes.size() == 34U, "TGA size is wrong");
    expect(value(tga.bytes[0x00U]) == 0U &&
               value(tga.bytes[0x01U]) == 0U &&
               value(tga.bytes[0x02U]) == 2U,
           "TGA image type is wrong");
    expect(value(tga.bytes[0x0cU]) == 2U &&
               value(tga.bytes[0x0dU]) == 0U &&
               value(tga.bytes[0x0eU]) == 2U &&
               value(tga.bytes[0x0fU]) == 0U,
           "TGA dimensions are wrong");
    expect(value(tga.bytes[0x10U]) == 32U &&
               value(tga.bytes[0x11U]) == 0x28U,
           "TGA depth/origin descriptor is wrong");

    const std::span<const std::byte> pixels(tga.bytes.data() + 18U, 16U);
    expect_rgba(pixels, 0U, {30U, 20U, 10U, 0x80U},
                "first TGA BGRA pixel");
    expect_rgba(pixels, 1U, {60U, 50U, 40U, 0xffU},
                "second TGA BGRA pixel");
    expect_rgba(pixels, 2U, {90U, 80U, 70U, 0xffU},
                "third TGA BGRA pixel");
    expect_rgba(pixels, 3U, {120U, 110U, 100U, 0xfeU},
                "fourth TGA BGRA pixel");
}

void test_input_and_limit_rejections() {
    const auto valid = make_fixture();

    auto zero_limits = kLimits;
    zero_limits.max_total_pixels = 0U;
    expect_texture_error([&] { (void)decode(valid, zero_limits); },
                         "zero limits were accepted");

    auto broken_table = valid;
    broken_table.table.push_back(std::byte{0});
    expect_texture_error([&] { (void)decode(broken_table); },
                         "a partial TextureEntry was accepted");

    auto limits = kLimits;
    limits.max_texture_table_bytes = valid.table.size() - 1U;
    expect_texture_error([&] { (void)decode(valid, limits); },
                         "the table byte limit was ignored");
    limits = kLimits;
    limits.max_decoded_core_bytes = valid.core.size() - 1U;
    expect_texture_error([&] { (void)decode(valid, limits); },
                         "the decoded-core byte limit was ignored");
    limits = kLimits;
    limits.max_gs_ram_bytes = valid.gs_ram.size() - 1U;
    expect_texture_error([&] { (void)decode(valid, limits); },
                         "the GS RAM byte limit was ignored");
    limits = kLimits;
    limits.max_textures = 1U;
    expect_texture_error([&] { (void)decode(valid, limits); },
                         "the texture count limit was ignored");
    limits = kLimits;
    limits.max_width = 1U;
    expect_texture_error([&] { (void)decode(valid, limits); },
                         "the texture width limit was ignored");
    limits = kLimits;
    limits.max_height = 1U;
    expect_texture_error([&] { (void)decode(valid, limits); },
                         "the texture height limit was ignored");
    limits = kLimits;
    limits.max_pixels_per_texture = 3U;
    expect_texture_error([&] { (void)decode(valid, limits); },
                         "the per-texture pixel limit was ignored");
    limits = kLimits;
    limits.max_total_pixels = 5U;
    expect_texture_error([&] { (void)decode(valid, limits); },
                         "the total pixel limit was ignored");
    limits = kLimits;
    limits.max_total_rgba_bytes = 23U;
    expect_texture_error([&] { (void)decode(valid, limits); },
                         "the total RGBA byte limit was ignored");

    expect_texture_error([&] { (void)decode(valid, kLimits, 0x41U); },
                         "an out-of-range texture base was accepted");
}

void test_entry_range_rejections() {
    auto fixture = make_fixture();
    write_le32(fixture.table, 0U, 0xffffffffU);
    expect_texture_error([&] { (void)decode(fixture); },
                         "a negative data offset was accepted");

    fixture = make_fixture();
    write_le16(fixture.table, 0x04U, 0U);
    expect_texture_error([&] { (void)decode(fixture); },
                         "a zero width was accepted");

    fixture = make_fixture();
    write_le16(fixture.table, 0x06U, 0xffffU);
    expect_texture_error([&] { (void)decode(fixture); },
                         "a negative height was accepted");

    fixture = make_fixture();
    write_le16(fixture.table, 0x0aU, 0xffffU);
    expect_texture_error([&] { (void)decode(fixture); },
                         "a negative palette block was accepted");

    fixture = make_fixture();
    write_le32(fixture.table, 0U, 0x20U);
    expect_texture_error([&] { (void)decode(fixture); },
                         "an out-of-range pixel envelope was accepted");

    fixture = make_fixture();
    write_le16(fixture.table, 0x0aU, 5U);
    expect_texture_error([&] { (void)decode(fixture); },
                         "an out-of-range palette envelope was accepted");
}

void test_tga_rejections() {
    auto texture = decode(make_fixture()).textures[0U];
    expect_texture_error(
        [&] {
            (void)openrc::encode_rac_level_moby_texture_tga_v1(texture, 0U);
        },
        "a zero TGA byte limit was accepted");
    expect_texture_error(
        [&] {
            (void)openrc::encode_rac_level_moby_texture_tga_v1(texture, 33U);
        },
        "a too-small TGA byte limit was accepted");

    auto broken = texture;
    broken.rgba.pop_back();
    expect_texture_error(
        [&] {
            (void)openrc::encode_rac_level_moby_texture_tga_v1(broken, 34U);
        },
        "an inconsistent TGA RGBA buffer was accepted");

    broken = texture;
    broken.indices.pop_back();
    expect_texture_error(
        [&] {
            (void)openrc::encode_rac_level_moby_texture_tga_v1(broken, 34U);
        },
        "an inconsistent TGA index buffer was accepted");

    broken = texture;
    broken.entry.width = 0;
    expect_texture_error(
        [&] {
            (void)openrc::encode_rac_level_moby_texture_tga_v1(broken, 34U);
        },
        "invalid TGA dimensions were accepted");
}

} // namespace

int main() {
    try {
        test_bank_decode_and_metadata();
        test_tga_export();
        test_input_and_limit_rejections();
        test_entry_range_rejections();
        test_tga_rejections();
        std::cout << "RAC1 level Moby texture tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "RAC1 level Moby texture tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
