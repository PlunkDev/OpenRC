#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::array<char, 4> kTwoFipMagic{'2', 'F', 'I', 'P'};
inline constexpr std::size_t kTwoFipHeaderSize = 0x20;
inline constexpr std::size_t kTwoFipPaletteEntryCount = 256;
inline constexpr std::size_t kTwoFipPaletteSize =
    kTwoFipPaletteEntryCount * 4U;
inline constexpr std::size_t kTwoFipPixelDataOffset =
    kTwoFipHeaderSize + kTwoFipPaletteSize;
inline constexpr std::uint32_t kTwoFipPsmT8Format = 0x13;

struct TwoFipColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t ps2_alpha = 0;

    [[nodiscard]] bool operator==(const TwoFipColor&) const = default;
};

struct TwoFipImage {
    std::uint32_t opaque_identifier = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pixel_storage_format = 0;
    std::array<std::uint32_t, 3> trailing_header_fields{};
    std::uint64_t logical_bytes = 0;
    std::uint64_t padding_bytes = 0;

    // The source CLUT permutation is normalized here. An index from `indices`
    // can be used directly with this palette.
    std::array<TwoFipColor, kTwoFipPaletteEntryCount> palette{};
    std::vector<std::uint8_t> indices;
};

class TwoFipError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses one complete 2FIP logical image. The input may additionally contain
// zero-filled storage padding, which is validated and reported.
[[nodiscard]] TwoFipImage parse_two_fip(
    std::span<const std::byte> bytes,
    std::uint64_t max_pixel_count);

// Expands indexed pixels in top-to-bottom, left-to-right order to RGBA8.
[[nodiscard]] std::vector<std::byte> expand_two_fip_rgba(
    const TwoFipImage& image);

// Creates an uncompressed 32-bit, top-left-origin TGA image.
[[nodiscard]] std::vector<std::byte> encode_two_fip_tga(
    const TwoFipImage& image);

} // namespace openrc
