#include "openrc/rac_level_moby_texture.hpp"
#include "openrc/ps2_palette.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
    throw RacLevelMobyTextureError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char *const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char *const description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left * right;
}

void require_range(const std::uint64_t offset,
                   const std::uint64_t size,
                   const std::uint64_t input_size,
                   const char *const description) {
    if (offset > input_size || size > input_size - offset) {
        fail(std::string(description) + " exceeds its input span");
    }
}

[[nodiscard]] std::uint16_t read_le16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(byte_value(bytes[offset])) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(byte_value(bytes[offset + 1U]))
               << 8U);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
           (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U]))
            << 8U) |
           (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U]))
            << 16U) |
           (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U]))
            << 24U);
}

[[nodiscard]] std::int16_t read_le_s16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return std::bit_cast<std::int16_t>(read_le16(bytes, offset));
}

[[nodiscard]] std::int32_t read_le_s32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return std::bit_cast<std::int32_t>(read_le32(bytes, offset));
}

void validate_limits(const RacLevelMobyTextureLimitsV1 limits) {
    if (limits.max_texture_table_bytes == 0U ||
        limits.max_decoded_core_bytes == 0U ||
        limits.max_gs_ram_bytes == 0U || limits.max_textures == 0U ||
        limits.max_width == 0U || limits.max_height == 0U ||
        limits.max_pixels_per_texture == 0U ||
        limits.max_total_pixels == 0U ||
        limits.max_total_rgba_bytes == 0U) {
        fail("RAC1 Moby texture limits must all be non-zero");
    }
}

void require_host_size(const std::uint64_t size,
                       const std::size_t host_limit,
                       const char *const description) {
    if (size > static_cast<std::uint64_t>(host_limit)) {
        fail(std::string(description) + " exceeds its host container");
    }
}

void write_le16(std::vector<std::byte> &bytes,
                const std::size_t offset,
                const std::uint16_t value) noexcept {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

} // namespace

RacLevelMobyTextureBankV1 decode_rac_level_moby_texture_bank_v1(
    const std::span<const std::byte> texture_table_bytes,
    const std::span<const std::byte> decoded_core_data,
    const std::span<const std::byte> raw_gs_ram,
    const std::uint64_t textures_base_offset,
    const RacLevelMobyTextureLimitsV1 limits) {
    validate_limits(limits);

    const auto table_size =
        static_cast<std::uint64_t>(texture_table_bytes.size());
    const auto core_size = static_cast<std::uint64_t>(decoded_core_data.size());
    const auto gs_size = static_cast<std::uint64_t>(raw_gs_ram.size());
    if (table_size > limits.max_texture_table_bytes) {
        fail("The RAC1 Moby texture table exceeds the caller's byte limit");
    }
    if (core_size > limits.max_decoded_core_bytes) {
        fail("The decoded RAC1 level core exceeds the caller's byte limit");
    }
    if (gs_size > limits.max_gs_ram_bytes) {
        fail("The RAC1 GS RAM image exceeds the caller's byte limit");
    }
    if (texture_table_bytes.size() % kRacLevelMobyTextureEntryBytesV1 != 0U) {
        fail("The RAC1 Moby texture table is not a sequence of 0x10-byte records");
    }
    if (textures_base_offset > core_size) {
        fail("The RAC1 shared-texture base exceeds the decoded level core");
    }

    const auto texture_count =
        table_size / kRacLevelMobyTextureEntryBytesV1;
    if (texture_count > limits.max_textures) {
        fail("The RAC1 Moby texture count exceeds the caller's limit");
    }
    if (texture_count > std::numeric_limits<std::uint32_t>::max()) {
        fail("The RAC1 Moby texture count exceeds its 32-bit index domain");
    }

    RacLevelMobyTextureBankV1 result;
    result.texture_table_input_bytes = table_size;
    result.decoded_core_input_bytes = core_size;
    result.gs_ram_input_bytes = gs_size;
    result.textures_base_offset = textures_base_offset;
    require_host_size(texture_count, result.textures.max_size(),
                      "The RAC1 Moby texture count");
    result.textures.reserve(static_cast<std::size_t>(texture_count));

    for (std::uint64_t global_index = 0U; global_index < texture_count;
         ++global_index) {
        const auto entry_offset = checked_multiply(
            global_index, kRacLevelMobyTextureEntryBytesV1,
            "a RAC1 Moby TextureEntry offset");
        require_range(entry_offset, kRacLevelMobyTextureEntryBytesV1,
                      table_size, "A RAC1 Moby TextureEntry");
        const auto host_offset = static_cast<std::size_t>(entry_offset);

        RacLevelMobyTextureV1 texture;
        texture.global_index = static_cast<std::uint32_t>(global_index);
        auto &entry = texture.entry;
        entry.table_entry_range = {
            entry_offset, kRacLevelMobyTextureEntryBytesV1};
        entry.data_offset = read_le_s32(texture_table_bytes, host_offset);
        entry.width = read_le_s16(texture_table_bytes, host_offset + 0x04U);
        entry.height = read_le_s16(texture_table_bytes, host_offset + 0x06U);
        entry.type = read_le_s16(texture_table_bytes, host_offset + 0x08U);
        entry.palette_block =
            read_le_s16(texture_table_bytes, host_offset + 0x0aU);
        entry.mipmap_block =
            read_le_s16(texture_table_bytes, host_offset + 0x0cU);
        entry.trailing_block =
            read_le_s16(texture_table_bytes, host_offset + 0x0eU);

        if (entry.data_offset < 0) {
            fail("A RAC1 Moby texture has a negative data offset");
        }
        if (entry.width <= 0 || entry.height <= 0) {
            fail("A RAC1 Moby texture has non-positive dimensions");
        }
        if (entry.palette_block < 0) {
            fail("A RAC1 Moby texture has a negative palette block");
        }

        const auto width = static_cast<std::uint64_t>(entry.width);
        const auto height = static_cast<std::uint64_t>(entry.height);
        if (width > limits.max_width || height > limits.max_height) {
            fail("A RAC1 Moby texture exceeds the caller's dimension limit");
        }
        const auto pixel_count = checked_multiply(
            width, height, "a RAC1 Moby texture pixel count");
        if (pixel_count > limits.max_pixels_per_texture) {
            fail("A RAC1 Moby texture exceeds the caller's per-image pixel limit");
        }
        const auto rgba_bytes = checked_multiply(
            pixel_count, 4U, "a RAC1 Moby texture RGBA byte count");
        const auto next_total_pixels = checked_add(
            result.total_pixel_count, pixel_count,
            "the RAC1 Moby texture-bank pixel count");
        const auto next_total_rgba = checked_add(
            result.total_rgba_bytes, rgba_bytes,
            "the RAC1 Moby texture-bank RGBA byte count");
        if (next_total_pixels > limits.max_total_pixels) {
            fail("The RAC1 Moby texture bank exceeds the caller's total pixel limit");
        }
        if (next_total_rgba > limits.max_total_rgba_bytes) {
            fail("The RAC1 Moby texture bank exceeds the caller's RGBA byte limit");
        }

        const auto pixel_offset = checked_add(
            textures_base_offset,
            static_cast<std::uint64_t>(entry.data_offset),
            "a RAC1 Moby texture pixel offset");
        require_range(pixel_offset, pixel_count, core_size,
                      "A RAC1 Moby texture pixel range");
        entry.pixel_range = {pixel_offset, pixel_count};

        const auto palette_offset = checked_multiply(
            static_cast<std::uint64_t>(entry.palette_block), 0x100U,
            "a RAC1 Moby texture palette offset");
        require_range(palette_offset, kRacLevelMobyTexturePaletteBytesV1,
                      gs_size, "A RAC1 Moby texture palette range");
        entry.palette_range = {
            palette_offset, kRacLevelMobyTexturePaletteBytesV1};

        require_host_size(pixel_count, texture.indices.max_size(),
                          "A RAC1 Moby texture index buffer");
        require_host_size(rgba_bytes, texture.rgba.max_size(),
                          "A RAC1 Moby texture RGBA buffer");
        const auto pixel_subspan = decoded_core_data.subspan(
            static_cast<std::size_t>(pixel_offset),
            static_cast<std::size_t>(pixel_count));
        texture.indices.assign(pixel_subspan.begin(), pixel_subspan.end());

        for (std::uint32_t logical_index = 0U;
             logical_index < kRacLevelMobyTexturePaletteColorCountV1;
             ++logical_index) {
            const auto raw_index = psmt8_clut_storage_index_v1(
                static_cast<std::uint8_t>(logical_index));
            const auto raw_offset =
                static_cast<std::size_t>(palette_offset) +
                static_cast<std::size_t>(raw_index) * 4U;
            const auto output_offset =
                static_cast<std::size_t>(logical_index) * 4U;
            texture.palette_rgba[output_offset] = raw_gs_ram[raw_offset];
            texture.palette_rgba[output_offset + 1U] =
                raw_gs_ram[raw_offset + 1U];
            texture.palette_rgba[output_offset + 2U] =
                raw_gs_ram[raw_offset + 2U];
            texture.palette_rgba[output_offset + 3U] =
                static_cast<std::byte>(ps2_alpha_to_rgba8_v1(
                    byte_value(raw_gs_ram[raw_offset + 3U])));
        }

        texture.rgba.resize(static_cast<std::size_t>(rgba_bytes));
        for (std::size_t pixel = 0U; pixel < texture.indices.size(); ++pixel) {
            const auto palette_index =
                static_cast<std::size_t>(byte_value(texture.indices[pixel]));
            const auto palette_rgba_offset = palette_index * 4U;
            const auto output_offset = pixel * 4U;
            texture.rgba[output_offset] =
                texture.palette_rgba[palette_rgba_offset];
            texture.rgba[output_offset + 1U] =
                texture.palette_rgba[palette_rgba_offset + 1U];
            texture.rgba[output_offset + 2U] =
                texture.palette_rgba[palette_rgba_offset + 2U];
            texture.rgba[output_offset + 3U] =
                texture.palette_rgba[palette_rgba_offset + 3U];
        }

        result.total_pixel_count = next_total_pixels;
        result.total_rgba_bytes = next_total_rgba;
        result.textures.push_back(std::move(texture));
    }

    return result;
}

RacLevelMobyTextureTgaV1 encode_rac_level_moby_texture_tga_v1(
    const RacLevelMobyTextureV1 &texture,
    const std::uint64_t max_output_bytes) {
    if (max_output_bytes == 0U) {
        fail("The RAC1 Moby TGA output limit must be non-zero");
    }
    if (texture.entry.width <= 0 || texture.entry.height <= 0) {
        fail("The decoded RAC1 Moby texture has invalid TGA dimensions");
    }

    const auto width = static_cast<std::uint64_t>(texture.entry.width);
    const auto height = static_cast<std::uint64_t>(texture.entry.height);
    const auto pixel_count = checked_multiply(
        width, height, "a RAC1 Moby TGA pixel count");
    const auto rgba_bytes = checked_multiply(
        pixel_count, 4U, "a RAC1 Moby TGA pixel byte count");
    if (texture.indices.size() != pixel_count ||
        texture.rgba.size() != rgba_bytes) {
        fail("The decoded RAC1 Moby texture has inconsistent TGA buffers");
    }
    const auto output_bytes = checked_add(
        kRacLevelMobyTextureTgaHeaderBytesV1, rgba_bytes,
        "a RAC1 Moby TGA output size");
    if (output_bytes > max_output_bytes) {
        fail("The RAC1 Moby TGA exceeds the caller's output byte limit");
    }

    RacLevelMobyTextureTgaV1 result;
    result.global_index = texture.global_index;
    result.width = static_cast<std::uint16_t>(width);
    result.height = static_cast<std::uint16_t>(height);
    require_host_size(output_bytes, result.bytes.max_size(),
                      "The RAC1 Moby TGA output");
    result.bytes.resize(static_cast<std::size_t>(output_bytes), std::byte{0});
    result.bytes[0x02U] = std::byte{2U}; // Uncompressed true-colour image.
    write_le16(result.bytes, 0x0cU, result.width);
    write_le16(result.bytes, 0x0eU, result.height);
    result.bytes[0x10U] = std::byte{32U};
    result.bytes[0x11U] = std::byte{0x28U}; // Top-left origin, 8 alpha bits.

    const auto header_size =
        static_cast<std::size_t>(kRacLevelMobyTextureTgaHeaderBytesV1);
    for (std::size_t pixel = 0U; pixel < texture.indices.size(); ++pixel) {
        const auto rgba_offset = pixel * 4U;
        const auto tga_offset = header_size + rgba_offset;
        result.bytes[tga_offset] = texture.rgba[rgba_offset + 2U];
        result.bytes[tga_offset + 1U] = texture.rgba[rgba_offset + 1U];
        result.bytes[tga_offset + 2U] = texture.rgba[rgba_offset];
        result.bytes[tga_offset + 3U] = texture.rgba[rgba_offset + 3U];
    }
    return result;
}

} // namespace openrc
