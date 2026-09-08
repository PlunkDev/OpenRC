#include "openrc/two_fip.hpp"
#include "openrc/ps2_palette.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace openrc {
namespace {

constexpr std::size_t kTgaHeaderSize = 18;

[[noreturn]] void fail(const std::string& message) {
    throw TwoFipError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

void write_le16(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

[[nodiscard]] std::uint64_t checked_pixel_count(
    const std::uint32_t width,
    const std::uint32_t height) {
    if (width == 0U || height == 0U) {
        fail("The 2FIP dimensions must be non-zero");
    }
    return static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height);
}

[[nodiscard]] std::uint64_t validate_image(const TwoFipImage& image) {
    const auto pixel_count = checked_pixel_count(image.width, image.height);
    if (image.pixel_storage_format != kTwoFipPsmT8Format ||
        image.trailing_header_fields != std::array<std::uint32_t, 3>{0U, 0U, 1U}) {
        fail("The in-memory 2FIP header is inconsistent");
    }
    if (pixel_count != image.indices.size()) {
        fail("The in-memory 2FIP index count does not match its dimensions");
    }
    return pixel_count;
}

} // namespace

TwoFipImage parse_two_fip(
    const std::span<const std::byte> bytes,
    const std::uint64_t max_pixel_count) {
    if (bytes.size() < kTwoFipPixelDataOffset) {
        fail("The 2FIP input is too small to contain its header and palette");
    }
    for (std::size_t index = 0; index < kTwoFipMagic.size(); ++index) {
        if (byte_value(bytes[index]) !=
            static_cast<std::uint8_t>(kTwoFipMagic[index])) {
            fail("The input does not have a 2FIP signature");
        }
    }

    TwoFipImage image;
    image.opaque_identifier = read_le32(bytes, 0x04);
    image.width = read_le32(bytes, 0x08);
    image.height = read_le32(bytes, 0x0c);
    image.pixel_storage_format = read_le32(bytes, 0x10);
    image.trailing_header_fields = {
        read_le32(bytes, 0x14),
        read_le32(bytes, 0x18),
        read_le32(bytes, 0x1c)};

    if (image.pixel_storage_format != kTwoFipPsmT8Format) {
        fail("The 2FIP pixel storage format is not the supported PS2 PSMT8 value");
    }
    if (image.trailing_header_fields !=
        std::array<std::uint32_t, 3>{0U, 0U, 1U}) {
        fail("The 2FIP trailing header constants are invalid");
    }

    const auto pixel_count = checked_pixel_count(image.width, image.height);
    if (pixel_count > max_pixel_count) {
        fail("The 2FIP pixel count exceeds the caller-provided limit");
    }
    if (pixel_count >
        std::numeric_limits<std::uint64_t>::max() - kTwoFipPixelDataOffset) {
        fail("Integer overflow while calculating the 2FIP logical size");
    }
    image.logical_bytes = kTwoFipPixelDataOffset + pixel_count;
    if (image.logical_bytes > bytes.size()) {
        fail("The 2FIP input ends before its indexed pixel data");
    }
    if (image.logical_bytes > std::numeric_limits<std::size_t>::max()) {
        fail("The 2FIP logical size exceeds the host container limit");
    }
    const auto logical_size = static_cast<std::size_t>(image.logical_bytes);
    const auto padding = bytes.subspan(logical_size);
    if (std::any_of(
            padding.begin(),
            padding.end(),
            [](const std::byte value) { return value != std::byte{0}; })) {
        fail("The 2FIP storage padding contains non-zero bytes");
    }
    image.padding_bytes =
        static_cast<std::uint64_t>(bytes.size() - logical_size);

    for (std::size_t logical_index = 0;
         logical_index < image.palette.size();
         ++logical_index) {
        const auto source_index = psmt8_clut_storage_index_v1(
            static_cast<std::uint8_t>(logical_index));
        const auto offset = kTwoFipHeaderSize + source_index * 4U;
        auto& color = image.palette[logical_index];
        color.red = byte_value(bytes[offset]);
        color.green = byte_value(bytes[offset + 1U]);
        color.blue = byte_value(bytes[offset + 2U]);
        color.ps2_alpha = byte_value(bytes[offset + 3U]);
    }

    const auto pixel_bytes =
        bytes.subspan(kTwoFipPixelDataOffset, static_cast<std::size_t>(pixel_count));
    image.indices.reserve(pixel_bytes.size());
    std::transform(
        pixel_bytes.begin(),
        pixel_bytes.end(),
        std::back_inserter(image.indices),
        [](const std::byte value) { return byte_value(value); });
    return image;
}

std::vector<std::byte> expand_two_fip_rgba(const TwoFipImage& image) {
    const auto pixel_count = validate_image(image);
    if (pixel_count > std::numeric_limits<std::size_t>::max() / 4U) {
        fail("The expanded 2FIP image exceeds the host container limit");
    }

    std::vector<std::byte> result;
    result.reserve(static_cast<std::size_t>(pixel_count) * 4U);
    for (const auto index : image.indices) {
        const auto& color = image.palette[index];
        result.push_back(static_cast<std::byte>(color.red));
        result.push_back(static_cast<std::byte>(color.green));
        result.push_back(static_cast<std::byte>(color.blue));
        result.push_back(
            static_cast<std::byte>(ps2_alpha_to_rgba8_v1(color.ps2_alpha)));
    }
    return result;
}

std::vector<std::byte> encode_two_fip_tga(const TwoFipImage& image) {
    (void)validate_image(image);
    if (image.width > std::numeric_limits<std::uint16_t>::max() ||
        image.height > std::numeric_limits<std::uint16_t>::max()) {
        fail("The 2FIP image cannot be represented as a TGA file");
    }
    const auto rgba = expand_two_fip_rgba(image);
    if (rgba.size() > std::numeric_limits<std::size_t>::max() - kTgaHeaderSize) {
        fail("The 2FIP image cannot be represented as a TGA file");
    }

    std::vector<std::byte> result(kTgaHeaderSize + rgba.size(), std::byte{0});
    result[2] = std::byte{2}; // uncompressed true-color image
    write_le16(result, 12, static_cast<std::uint16_t>(image.width));
    write_le16(result, 14, static_cast<std::uint16_t>(image.height));
    result[16] = std::byte{32};
    result[17] = std::byte{0x28}; // 8 alpha bits, top-left origin

    auto destination = result.begin() + static_cast<std::ptrdiff_t>(kTgaHeaderSize);
    for (std::size_t offset = 0; offset < rgba.size(); offset += 4U) {
        *destination++ = rgba[offset + 2U];
        *destination++ = rgba[offset + 1U];
        *destination++ = rgba[offset];
        *destination++ = rgba[offset + 3U];
    }
    return result;
}

} // namespace openrc
