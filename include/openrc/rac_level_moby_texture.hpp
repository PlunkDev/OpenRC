#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kRacLevelMobyTextureEntryBytesV1 = 0x10U;
inline constexpr std::uint32_t kRacLevelMobyTexturePaletteColorCountV1 =
    256U;
inline constexpr std::uint32_t kRacLevelMobyTexturePaletteBytesV1 =
    kRacLevelMobyTexturePaletteColorCountV1 * 4U;
inline constexpr std::uint32_t kRacLevelMobyTextureTgaHeaderBytesV1 = 18U;

struct RacLevelMobyTextureLimitsV1 {
    std::uint64_t max_texture_table_bytes = 0U;
    std::uint64_t max_decoded_core_bytes = 0U;
    std::uint64_t max_gs_ram_bytes = 0U;
    std::uint64_t max_textures = 0U;
    std::uint64_t max_width = 0U;
    std::uint64_t max_height = 0U;
    std::uint64_t max_pixels_per_texture = 0U;
    std::uint64_t max_total_pixels = 0U;
    std::uint64_t max_total_rgba_bytes = 0U;
};

struct RacLevelMobyTextureRangeV1 {
    std::uint64_t offset = 0U;
    std::uint64_t size = 0U;

    [[nodiscard]] bool
    operator==(const RacLevelMobyTextureRangeV1 &) const = default;
};

struct RacLevelMobyTextureEntryV1 {
    // Relative to the supplied raw texture-table span.
    RacLevelMobyTextureRangeV1 table_entry_range;

    // These retain the exact signed little-endian fields from the RAC1
    // 0x10-byte TextureEntry record. Valid decoded images have positive width
    // and height, a non-negative data offset, and a non-negative palette block.
    std::int32_t data_offset = 0;
    std::int16_t width = 0;
    std::int16_t height = 0;
    std::int16_t type = 0;
    std::int16_t palette_block = 0;
    std::int16_t mipmap_block = 0;
    // The final field is another GS-page reference in the observed type-4
    // records and -1 otherwise. Its exact runtime role remains deliberately
    // neutral in this base-image decoder.
    std::int16_t trailing_block = 0;

    // Pixel range is relative to decoded_core_data. Palette range is relative
    // to raw_gs_ram. RAC1 Moby base pixels are linear PSMT8 indices and the
    // palette is a 256-entry RGBA32 CLUT.
    RacLevelMobyTextureRangeV1 pixel_range;
    RacLevelMobyTextureRangeV1 palette_range;
};

struct RacLevelMobyTextureV1 {
    std::uint32_t global_index = 0U;
    RacLevelMobyTextureEntryV1 entry;

    // Logical CLUT order after swapping GS CLUT address bits 3 and 4 and
    // expanding PS2 alpha [0, 0x80] to host alpha [0, 0xff]. Channels are RGBA.
    std::array<std::byte, kRacLevelMobyTexturePaletteBytesV1> palette_rgba{};

    // Linear row-major PSMT8 indices followed by their expanded RGBA pixels.
    // No RAC1 pixel swizzle or vertical flip is applied.
    std::vector<std::byte> indices;
    std::vector<std::byte> rgba;
};

struct RacLevelMobyTextureBankV1 {
    std::uint64_t texture_table_input_bytes = 0U;
    std::uint64_t decoded_core_input_bytes = 0U;
    std::uint64_t gs_ram_input_bytes = 0U;
    std::uint64_t textures_base_offset = 0U;
    std::uint64_t total_pixel_count = 0U;
    std::uint64_t total_rgba_bytes = 0U;
    std::vector<RacLevelMobyTextureV1> textures;
};

struct RacLevelMobyTextureTgaV1 {
    std::uint32_t global_index = 0U;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
    std::vector<std::byte> bytes;
};

class RacLevelMobyTextureError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Decodes the already-sliced RAC1 Moby TextureEntry table. The core span is
// the complete decoded primary-extent-0/subrange-10 payload and raw_gs_ram is
// primary-extent-0/subrange-3. TextureEntry::data_offset is relative to
// textures_base_offset; TextureEntry::palette_block is measured in 0x100-byte
// GS RAM blocks.
[[nodiscard]] RacLevelMobyTextureBankV1
decode_rac_level_moby_texture_bank_v1(
    std::span<const std::byte> texture_table_bytes,
    std::span<const std::byte> decoded_core_data,
    std::span<const std::byte> raw_gs_ram,
    std::uint64_t textures_base_offset,
    RacLevelMobyTextureLimitsV1 limits);

// Encodes one decoded image as an uncompressed 32-bit, top-left-origin TGA.
// TGA stores each output pixel in BGRA order; no file is written.
[[nodiscard]] RacLevelMobyTextureTgaV1
encode_rac_level_moby_texture_tga_v1(
    const RacLevelMobyTextureV1 &texture,
    std::uint64_t max_output_bytes);

} // namespace openrc
