#include "openrc/two_fip.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kWidth = 2;
constexpr std::uint32_t kHeight = 2;
constexpr std::uint64_t kPixelCount =
    static_cast<std::uint64_t>(kWidth) * kHeight;

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::uint8_t value(const std::byte byte) {
    return std::to_integer<std::uint8_t>(byte);
}

void write_le32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void write_color(
    std::vector<std::byte>& bytes,
    const std::size_t stored_index,
    const openrc::TwoFipColor color) {
    const auto offset = openrc::kTwoFipHeaderSize + stored_index * 4U;
    bytes[offset] = static_cast<std::byte>(color.red);
    bytes[offset + 1U] = static_cast<std::byte>(color.green);
    bytes[offset + 2U] = static_cast<std::byte>(color.blue);
    bytes[offset + 3U] = static_cast<std::byte>(color.ps2_alpha);
}

std::vector<std::byte> make_image(const std::size_t padding = 0) {
    std::vector<std::byte> bytes(
        openrc::kTwoFipPixelDataOffset +
            static_cast<std::size_t>(kPixelCount) + padding,
        std::byte{0});
    for (std::size_t index = 0; index < openrc::kTwoFipMagic.size(); ++index) {
        bytes[index] = static_cast<std::byte>(openrc::kTwoFipMagic[index]);
    }
    write_le32(bytes, 0x04, 0x12345678U);
    write_le32(bytes, 0x08, kWidth);
    write_le32(bytes, 0x0c, kHeight);
    write_le32(bytes, 0x10, openrc::kTwoFipPsmT8Format);
    write_le32(bytes, 0x1c, 1U);

    write_color(bytes, 0, openrc::TwoFipColor{10, 20, 30, 0});
    // Logical index 8 is stored at 16, and logical 16 is stored at 8.
    write_color(bytes, 16, openrc::TwoFipColor{40, 50, 60, 0x80});
    write_color(bytes, 8, openrc::TwoFipColor{70, 80, 90, 0x40});
    write_color(bytes, 24, openrc::TwoFipColor{100, 110, 120, 0xff});
    bytes[openrc::kTwoFipPixelDataOffset] = std::byte{0};
    bytes[openrc::kTwoFipPixelDataOffset + 1U] = std::byte{8};
    bytes[openrc::kTwoFipPixelDataOffset + 2U] = std::byte{16};
    bytes[openrc::kTwoFipPixelDataOffset + 3U] = std::byte{24};
    return bytes;
}

template <typename Callback>
void expect_rejected(Callback&& callback, const std::string& message) {
    bool rejected = false;
    try {
        callback();
    } catch (const openrc::TwoFipError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void test_valid_parse_and_render() {
    const auto source = make_image(37);
    const auto image = openrc::parse_two_fip(source, kPixelCount);
    expect(image.opaque_identifier == 0x12345678U, "opaque identifier changed");
    expect(image.width == kWidth && image.height == kHeight, "dimensions changed");
    expect(
        image.pixel_storage_format == openrc::kTwoFipPsmT8Format,
        "pixel storage format changed");
    expect(
        image.trailing_header_fields ==
            std::array<std::uint32_t, 3>{0U, 0U, 1U},
        "trailing header fields changed");
    expect(
        image.logical_bytes ==
            openrc::kTwoFipPixelDataOffset + kPixelCount,
        "logical size is incorrect");
    expect(image.padding_bytes == 37, "zero padding was not reported");
    expect(
        image.palette[8] == openrc::TwoFipColor{40, 50, 60, 0x80},
        "PSMT8 palette permutation was not normalized");
    expect(
        image.palette[16] == openrc::TwoFipColor{70, 80, 90, 0x40},
        "inverse PSMT8 palette group was not normalized");
    expect(
        image.indices[0] == 0 && image.indices[1] == 8 &&
            image.indices[2] == 16 && image.indices[3] == 24,
        "pixel indices changed");

    const auto rgba = openrc::expand_two_fip_rgba(image);
    expect(rgba.size() == kPixelCount * 4U, "RGBA output size is incorrect");
    expect(
        value(rgba[0]) == 10 && value(rgba[1]) == 20 &&
            value(rgba[2]) == 30 && value(rgba[3]) == 0,
        "transparent RGBA pixel is incorrect");
    expect(
        value(rgba[4]) == 40 && value(rgba[5]) == 50 &&
            value(rgba[6]) == 60 && value(rgba[7]) == 255,
        "opaque RGBA pixel is incorrect");
    expect(
        value(rgba[8]) == 70 && value(rgba[9]) == 80 &&
            value(rgba[10]) == 90 && value(rgba[11]) == 128,
        "intermediate-alpha RGBA pixel is incorrect");
    expect(value(rgba[15]) == 255, "PS2 alpha saturation is incorrect");

    const auto tga = openrc::encode_two_fip_tga(image);
    expect(tga.size() == 18U + kPixelCount * 4U, "TGA size is incorrect");
    expect(value(tga[2]) == 2, "TGA image type is incorrect");
    expect(
        value(tga[12]) == 2 && value(tga[13]) == 0 &&
            value(tga[14]) == 2 && value(tga[15]) == 0,
        "TGA dimensions are incorrect");
    expect(
        value(tga[16]) == 32 && value(tga[17]) == 0x28,
        "TGA pixel format/origin is incorrect");
    expect(
        value(tga[18]) == 30 && value(tga[19]) == 20 &&
            value(tga[20]) == 10 && value(tga[21]) == 0 &&
            value(tga[22]) == 60 && value(tga[23]) == 50 &&
            value(tga[24]) == 40 && value(tga[25]) == 255 &&
            value(tga[26]) == 90 && value(tga[27]) == 80 &&
            value(tga[28]) == 70 && value(tga[29]) == 128 &&
            value(tga[30]) == 120 && value(tga[31]) == 110 &&
            value(tga[32]) == 100 && value(tga[33]) == 255,
        "TGA BGRA channel order is incorrect");
}

void test_parse_rejections() {
    std::vector<std::byte> short_input(
        openrc::kTwoFipPixelDataOffset - 1U,
        std::byte{0});
    expect_rejected(
        [&] { (void)openrc::parse_two_fip(short_input, kPixelCount); },
        "input shorter than the fixed header and palette was accepted");

    auto source = make_image();
    source[0] = std::byte{'X'};
    expect_rejected(
        [&] { (void)openrc::parse_two_fip(source, kPixelCount); },
        "bad magic was accepted");

    source = make_image();
    write_le32(source, 0x10, 0x14U);
    expect_rejected(
        [&] { (void)openrc::parse_two_fip(source, kPixelCount); },
        "unsupported pixel storage format was accepted");

    source = make_image();
    write_le32(source, 0x14, 1U);
    expect_rejected(
        [&] { (void)openrc::parse_two_fip(source, kPixelCount); },
        "bad trailing header constants were accepted");

    source = make_image();
    write_le32(source, 0x18, 1U);
    expect_rejected(
        [&] { (void)openrc::parse_two_fip(source, kPixelCount); },
        "bad second trailing header constant was accepted");

    source = make_image();
    write_le32(source, 0x1c, 0U);
    expect_rejected(
        [&] { (void)openrc::parse_two_fip(source, kPixelCount); },
        "bad final header constant was accepted");

    source = make_image();
    write_le32(source, 0x08, 0U);
    expect_rejected(
        [&] { (void)openrc::parse_two_fip(source, kPixelCount); },
        "zero dimensions were accepted");

    source = make_image();
    expect_rejected(
        [&] { (void)openrc::parse_two_fip(source, kPixelCount - 1U); },
        "caller pixel cap was ignored");

    source = make_image();
    source.pop_back();
    expect_rejected(
        [&] { (void)openrc::parse_two_fip(source, kPixelCount); },
        "truncated pixel data was accepted");

    source = make_image(1);
    source.back() = std::byte{1};
    expect_rejected(
        [&] { (void)openrc::parse_two_fip(source, kPixelCount); },
        "non-zero storage padding was accepted");

    source = make_image();
    write_le32(source, 0x08, std::numeric_limits<std::uint32_t>::max());
    write_le32(source, 0x0c, std::numeric_limits<std::uint32_t>::max());
    expect_rejected(
        [&] {
            (void)openrc::parse_two_fip(
                source,
                std::numeric_limits<std::uint64_t>::max());
        },
        "impossibly large dimensions were accepted");
}

void test_in_memory_rejections() {
    auto image = openrc::parse_two_fip(make_image(), kPixelCount);
    image.indices.pop_back();
    expect_rejected(
        [&] { (void)openrc::expand_two_fip_rgba(image); },
        "inconsistent in-memory index count was accepted");

    image = openrc::parse_two_fip(make_image(), kPixelCount);
    image.trailing_header_fields[0] = 1;
    expect_rejected(
        [&] { (void)openrc::expand_two_fip_rgba(image); },
        "inconsistent in-memory header was accepted");

    openrc::TwoFipImage too_wide;
    too_wide.width =
        static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max()) + 1U;
    too_wide.height = 1;
    too_wide.pixel_storage_format = openrc::kTwoFipPsmT8Format;
    too_wide.trailing_header_fields = {0U, 0U, 1U};
    too_wide.indices.resize(too_wide.width);
    expect_rejected(
        [&] { (void)openrc::encode_two_fip_tga(too_wide); },
        "TGA width limit was ignored");
}

} // namespace

int main() {
    try {
        test_valid_parse_and_render();
        test_parse_rejections();
        test_in_memory_rejections();
        std::cout << "OpenRC 2FIP tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC 2FIP tests failed: " << error.what() << '\n';
        return 1;
    }
}
