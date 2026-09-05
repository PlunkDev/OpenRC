#include "openrc/rac_level_core.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint32_t kWadV1MinimumLogicalBytes = 0x10U;
constexpr std::uint32_t kRatchetSequenceTableBytesV1 =
    kRacLevelCoreRatchetSequenceCountV1 * sizeof(std::uint32_t);

// This is not all-zero padding. These bytes are identical in all 19 original
// PAL level-core indices and are deliberately kept separate from the 0xbc
// header understood by Wrench.
constexpr std::array<std::uint8_t,
                     kRacLevelCoreMobyTableOffsetV1 -
                         kRacLevelCoreHeaderBytesV1>
    kRac1HeaderTrailer{
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x5cU,
        0x00U, 0x00U, 0x00U, 0x58U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    };

[[noreturn]] void fail(const std::string& message) {
    throw RacLevelCoreError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::int32_t read_le_i32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return std::bit_cast<std::int32_t>(read_le32(bytes, offset));
}

[[nodiscard]] std::int16_t read_le_i16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    const std::uint16_t value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(byte_value(bytes[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(byte_value(bytes[offset + 1U])) << 8U));
    return std::bit_cast<std::int16_t>(value);
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left * right;
}

[[nodiscard]] std::uint64_t align_up_asset(
    const std::uint64_t value,
    const char* const description) {
    constexpr auto alignment =
        static_cast<std::uint64_t>(kRacLevelCoreAssetAlignmentV1);
    constexpr auto mask = alignment - 1U;
    static_assert((alignment & mask) == 0U);
    if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
        fail(std::string("Integer overflow while aligning ") + description);
    }
    return (value + mask) & ~mask;
}

[[nodiscard]] bool is_aligned(
    const std::uint64_t value,
    const std::uint64_t alignment) noexcept {
    return value % alignment == 0U;
}

void require_range(
    const std::uint64_t offset,
    const std::uint64_t size,
    const std::size_t input_size,
    const char* const description) {
    const auto available = static_cast<std::uint64_t>(input_size);
    if (offset > available || size > available - offset) {
        fail(std::string(description) + " exceeds its input span");
    }
}

[[nodiscard]] bool is_zero_range(
    const std::span<const std::byte> bytes,
    const RacLevelCoreRangeV1 range) {
    require_range(range.offset, range.size, bytes.size(), "A zero range");
    for (std::uint64_t index = 0U; index < range.size; ++index) {
        if (bytes[static_cast<std::size_t>(range.offset + index)] !=
            std::byte{0}) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool has_wad_magic(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return byte_value(bytes[offset]) == static_cast<std::uint8_t>('W') &&
        byte_value(bytes[offset + 1U]) == static_cast<std::uint8_t>('A') &&
        byte_value(bytes[offset + 2U]) == static_cast<std::uint8_t>('D');
}

[[nodiscard]] RacLevelCoreArrayRangeV1 read_array_range(
    const std::span<const std::byte> bytes,
    const std::size_t header_offset,
    const std::uint32_t record_size,
    const char* const description) {
    RacLevelCoreArrayRangeV1 result;
    result.count = read_le32(bytes, header_offset);
    result.offset = read_le32(bytes, header_offset + sizeof(std::uint32_t));
    result.byte_range = {
        result.offset,
        checked_multiply(result.count, record_size, description),
    };
    return result;
}

void validate_index_table(
    const RacLevelCoreArrayRangeV1& table,
    const std::size_t input_size,
    const char* const description) {
    if (!is_aligned(table.offset, kRacLevelCoreIndexAlignmentV1)) {
        fail(std::string(description) + " offset is not 0x10-aligned");
    }
    require_range(
        table.byte_range.offset,
        table.byte_range.size,
        input_size,
        description);
}

[[nodiscard]] std::uint64_t range_end(
    const RacLevelCoreRangeV1 range,
    const char* const description) {
    return checked_add(range.offset, range.size, description);
}

void require_adjacent(
    const RacLevelCoreArrayRangeV1& left,
    const RacLevelCoreArrayRangeV1& right,
    const char* const description) {
    if (range_end(left.byte_range, description) != right.byte_range.offset) {
        fail(std::string(description) + " tables are not contiguous");
    }
}

void add_aligned_asset_boundary(
    std::vector<std::uint32_t>& boundaries,
    const std::uint32_t offset,
    const std::size_t decoded_size,
    const std::uint32_t alignment,
    const char* const description) {
    if (offset == 0U) {
        return;
    }
    if (!is_aligned(offset, alignment) || offset >= decoded_size) {
        fail(std::string(description) +
             " is not a valid decoded level-core offset");
    }
    boundaries.push_back(offset);
}

} // namespace

RacLevelCoreIndexV1 parse_rac_level_core_index_v1(
    const std::span<const std::byte> index_bytes,
    const std::span<const std::byte> encoded_asset_bytes,
    const std::span<const std::byte> decoded_asset_bytes,
    const RacLevelCoreLimitsV1 limits) {
    if (limits.max_index_bytes == 0U ||
        limits.max_encoded_asset_bytes == 0U ||
        limits.max_decoded_asset_bytes == 0U ||
        limits.max_moby_classes == 0U ||
        limits.max_moby_textures == 0U || limits.max_gadgets == 0U ||
        limits.max_tie_classes == 0U ||
        limits.max_shrub_classes == 0U ||
        limits.max_tie_textures == 0U ||
        limits.max_shrub_textures == 0U) {
        fail("RacLevelCoreIndexV1 caller limits must all be non-zero");
    }
    if (index_bytes.size() > limits.max_index_bytes) {
        fail("RacLevelCoreIndexV1 exceeds the caller's index-byte limit");
    }
    if (encoded_asset_bytes.size() > limits.max_encoded_asset_bytes) {
        fail("RacLevelCoreIndexV1 exceeds the caller's encoded-byte limit");
    }
    if (decoded_asset_bytes.size() > limits.max_decoded_asset_bytes) {
        fail("RacLevelCoreIndexV1 exceeds the caller's decoded-byte limit");
    }
    if (index_bytes.size() < kRacLevelCoreMobyTableOffsetV1 ||
        !is_aligned(index_bytes.size(), kRacLevelCoreIndexAlignmentV1) ||
        index_bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail("RacLevelCoreIndexV1 has an invalid index envelope");
    }
    if (encoded_asset_bytes.size() < kWadV1MinimumLogicalBytes ||
        encoded_asset_bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail("RacLevelCoreIndexV1 has an invalid encoded asset envelope");
    }
    if (decoded_asset_bytes.empty() ||
        decoded_asset_bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
        !is_aligned(
            decoded_asset_bytes.size(), kRacLevelCoreAssetAlignmentV1)) {
        fail("RacLevelCoreIndexV1 has an invalid decoded asset envelope");
    }

    RacLevelCoreIndexV1 result;
    result.index_input_bytes = index_bytes.size();
    result.encoded_asset_input_bytes = encoded_asset_bytes.size();
    result.decoded_asset_input_bytes = decoded_asset_bytes.size();
    result.header_range = {0U, kRacLevelCoreHeaderBytesV1};
    result.rac1_header_trailer_range = {
        kRacLevelCoreHeaderBytesV1,
        kRacLevelCoreMobyTableOffsetV1 - kRacLevelCoreHeaderBytesV1,
    };

    auto& header = result.header;
    for (std::size_t index = 0U; index < header.raw_words.size(); ++index) {
        header.raw_words[index] = read_le32(index_bytes, index * 4U);
    }
    header.gs_ram = read_array_range(
        index_bytes, 0x00U, kRacLevelCoreTextureEntryBytesV1, "the GS table size");
    header.tfrags_offset = header.raw_words[0x08U / 4U];
    header.occlusion_offset = header.raw_words[0x0cU / 4U];
    header.sky_offset = header.raw_words[0x10U / 4U];
    header.collision_offset = header.raw_words[0x14U / 4U];
    header.moby_classes = read_array_range(
        index_bytes,
        0x18U,
        kRacLevelCoreMobyClassEntryBytesV1,
        "the Moby-class table size");
    header.tie_classes = read_array_range(
        index_bytes,
        0x20U,
        kRacLevelCoreTieClassEntryBytesV1,
        "the tie-class table size");
    header.shrub_classes = read_array_range(
        index_bytes,
        0x28U,
        kRacLevelCoreShrubClassEntryBytesV1,
        "the shrub-class table size");
    header.tfrag_textures = read_array_range(
        index_bytes,
        0x30U,
        kRacLevelCoreTextureEntryBytesV1,
        "the tfrag-texture table size");
    header.moby_textures = read_array_range(
        index_bytes,
        0x38U,
        kRacLevelCoreTextureEntryBytesV1,
        "the Moby-texture table size");
    header.tie_textures = read_array_range(
        index_bytes,
        0x40U,
        kRacLevelCoreTextureEntryBytesV1,
        "the tie-texture table size");
    header.shrub_textures = read_array_range(
        index_bytes,
        0x48U,
        kRacLevelCoreTextureEntryBytesV1,
        "the shrub-texture table size");
    header.particle_textures = read_array_range(
        index_bytes,
        0x50U,
        kRacLevelCoreTextureEntryBytesV1,
        "the particle-texture table size");
    header.fx_textures = read_array_range(
        index_bytes,
        0x58U,
        kRacLevelCoreTextureEntryBytesV1,
        "the FX-texture table size");
    header.textures_base_offset = header.raw_words[0x60U / 4U];
    header.particle_bank_offset = header.raw_words[0x64U / 4U];
    header.fx_bank_offset = header.raw_words[0x68U / 4U];
    header.particle_definitions_offset = header.raw_words[0x6cU / 4U];
    header.sound_remap_offset = header.raw_words[0x70U / 4U];
    header.opaque_74 = header.raw_words[0x74U / 4U];
    header.ratchet_sequences_offset = header.raw_words[0x78U / 4U];
    header.scene_view_size = header.raw_words[0x7cU / 4U];
    header.gadget_count = header.raw_words[0x80U / 4U];
    header.gadget_offset = header.raw_words[0x84U / 4U];
    header.assets_encoded_size = header.raw_words[0x88U / 4U];
    header.assets_decoded_size = header.raw_words[0x8cU / 4U];
    header.chrome_map_texture = header.raw_words[0x90U / 4U];
    header.chrome_map_palette = header.raw_words[0x94U / 4U];
    header.glass_map_texture = header.raw_words[0x98U / 4U];
    header.glass_map_palette = header.raw_words[0x9cU / 4U];
    header.opaque_a0 = header.raw_words[0xa0U / 4U];
    header.heightmap_offset = header.raw_words[0xa4U / 4U];
    header.occlusion_octree_offset = header.raw_words[0xa8U / 4U];
    for (std::size_t index = 0U; index < header.rac1_tail_words.size(); ++index) {
        header.rac1_tail_words[index] =
            header.raw_words[0xacU / 4U + index];
    }

    for (std::size_t index = 0U; index < kRac1HeaderTrailer.size(); ++index) {
        if (byte_value(index_bytes[kRacLevelCoreHeaderBytesV1 + index]) !=
            kRac1HeaderTrailer[index]) {
            fail("RacLevelCoreIndexV1 has an invalid RAC1 header trailer");
        }
    }
    if (header.tfrags_offset != 0U) {
        fail("RacLevelCoreIndexV1 does not begin its tfrag data at offset zero");
    }
    if (header.moby_classes.count == 0U ||
        header.moby_classes.count > limits.max_moby_classes) {
        fail("RacLevelCoreIndexV1 has an invalid Moby-class count");
    }
    if (header.moby_classes.offset != kRacLevelCoreMobyTableOffsetV1) {
        fail("RacLevelCoreIndexV1 Moby classes do not begin at offset 0xe0");
    }
    if (header.moby_textures.count == 0U ||
        header.moby_textures.count > limits.max_moby_textures ||
        header.moby_textures.count > kRacLevelCoreUnusedTextureSlotV1) {
        fail("RacLevelCoreIndexV1 has an invalid Moby-texture count");
    }
    if (header.tie_classes.count > limits.max_tie_classes) {
        fail("RacLevelCoreIndexV1 has an invalid tie-class count");
    }
    if (header.shrub_classes.count > limits.max_shrub_classes) {
        fail("RacLevelCoreIndexV1 has an invalid shrub-class count");
    }
    if (header.tie_textures.count > limits.max_tie_textures ||
        header.tie_textures.count > kRacLevelCoreUnusedTextureSlotV1) {
        fail("RacLevelCoreIndexV1 has an invalid tie-texture count");
    }
    if (header.shrub_textures.count > limits.max_shrub_textures ||
        header.shrub_textures.count > kRacLevelCoreUnusedTextureSlotV1) {
        fail("RacLevelCoreIndexV1 has an invalid shrub-texture count");
    }
    if (header.gadget_count == 0U ||
        header.gadget_count > limits.max_gadgets) {
        fail("RacLevelCoreIndexV1 has an invalid gadget count");
    }

    validate_index_table(header.gs_ram, index_bytes.size(), "The GS table");
    validate_index_table(
        header.moby_classes, index_bytes.size(), "The Moby-class table");
    validate_index_table(
        header.tie_classes, index_bytes.size(), "The tie-class table");
    validate_index_table(
        header.shrub_classes, index_bytes.size(), "The shrub-class table");
    validate_index_table(
        header.tfrag_textures, index_bytes.size(), "The tfrag-texture table");
    validate_index_table(
        header.moby_textures, index_bytes.size(), "The Moby-texture table");
    validate_index_table(
        header.tie_textures, index_bytes.size(), "The tie-texture table");
    validate_index_table(
        header.shrub_textures, index_bytes.size(), "The shrub-texture table");
    validate_index_table(
        header.particle_textures,
        index_bytes.size(),
        "The particle-texture table");
    validate_index_table(
        header.fx_textures, index_bytes.size(), "The FX-texture table");

    require_adjacent(
        header.moby_classes, header.tie_classes, "The class-directory");
    require_adjacent(
        header.tie_classes, header.shrub_classes, "The class-directory");
    require_adjacent(
        header.shrub_classes, header.tfrag_textures, "The class-directory");
    require_adjacent(
        header.tfrag_textures,
        header.moby_textures,
        "The texture-directory");
    require_adjacent(
        header.moby_textures, header.tie_textures, "The texture-directory");
    require_adjacent(
        header.tie_textures, header.shrub_textures, "The texture-directory");
    require_adjacent(
        header.shrub_textures,
        header.particle_textures,
        "The texture-directory");
    require_adjacent(
        header.particle_textures,
        header.fx_textures,
        "The texture-directory");
    if (range_end(header.fx_textures.byte_range, "the FX-texture table end") !=
        header.gs_ram.offset) {
        fail("The texture directory does not end at the GS table");
    }
    if (range_end(header.gs_ram.byte_range, "the GS table end") !=
        header.particle_definitions_offset) {
        fail("The GS table does not end at the particle definitions");
    }

    if (!is_aligned(
            header.particle_definitions_offset,
            kRacLevelCoreIndexAlignmentV1) ||
        !is_aligned(
            header.sound_remap_offset, kRacLevelCoreIndexAlignmentV1) ||
        !is_aligned(
            header.ratchet_sequences_offset,
            kRacLevelCoreIndexAlignmentV1) ||
        !is_aligned(header.gadget_offset, kRacLevelCoreIndexAlignmentV1) ||
        header.particle_definitions_offset > header.sound_remap_offset ||
        header.sound_remap_offset > header.ratchet_sequences_offset) {
        fail("RacLevelCoreIndexV1 has an invalid index-tail order");
    }
    require_range(
        header.particle_definitions_offset,
        0U,
        index_bytes.size(),
        "The particle-definition offset");
    require_range(
        header.sound_remap_offset,
        0U,
        index_bytes.size(),
        "The sound-remap offset");

    result.ratchet_sequence_table_range = {
        header.ratchet_sequences_offset,
        kRatchetSequenceTableBytesV1,
    };
    require_range(
        result.ratchet_sequence_table_range.offset,
        result.ratchet_sequence_table_range.size,
        index_bytes.size(),
        "The Ratchet-sequence table");
    if (range_end(
            result.ratchet_sequence_table_range,
            "the Ratchet-sequence table end") != header.gadget_offset) {
        fail("The Ratchet-sequence table does not end at the gadget table");
    }

    const auto gadget_table_bytes = checked_multiply(
        header.gadget_count,
        kRacLevelCoreGadgetEntryBytesV1,
        "the gadget-table size");
    result.gadget_table_range = {header.gadget_offset, gadget_table_bytes};
    require_range(
        result.gadget_table_range.offset,
        result.gadget_table_range.size,
        index_bytes.size(),
        "The gadget table");
    if (range_end(result.gadget_table_range, "the gadget-table end") !=
        index_bytes.size()) {
        fail("The gadget table does not end exactly at index EOF");
    }

    result.moby_class_table_range = header.moby_classes.byte_range;
    result.tie_class_table_range = header.tie_classes.byte_range;
    result.shrub_class_table_range = header.shrub_classes.byte_range;
    result.tfrag_texture_table_range = header.tfrag_textures.byte_range;
    result.moby_texture_table_range = header.moby_textures.byte_range;
    result.tie_texture_table_range = header.tie_textures.byte_range;
    result.shrub_texture_table_range = header.shrub_textures.byte_range;

    if (header.assets_encoded_size != encoded_asset_bytes.size() ||
        header.assets_decoded_size != decoded_asset_bytes.size()) {
        fail("RacLevelCoreIndexV1 asset sizes do not match the caller spans");
    }
    if (!has_wad_magic(encoded_asset_bytes, 0U) ||
        read_le32(encoded_asset_bytes, 3U) != encoded_asset_bytes.size()) {
        fail("The encoded level-core asset is not one complete WadV1 record");
    }

    std::unordered_map<std::int32_t, std::size_t> class_indices;
    std::unordered_set<std::uint32_t> class_asset_offsets;
    if (static_cast<std::uint64_t>(header.moby_classes.count) >
            static_cast<std::uint64_t>(result.moby_classes.max_size()) ||
        static_cast<std::uint64_t>(header.moby_classes.count) >
            static_cast<std::uint64_t>(class_indices.max_size()) ||
        static_cast<std::uint64_t>(header.moby_classes.count) >
            static_cast<std::uint64_t>(class_asset_offsets.max_size())) {
        fail("RacLevelCoreIndexV1 class metadata exceeds a host container limit");
    }
    result.moby_classes.reserve(header.moby_classes.count);
    class_indices.reserve(header.moby_classes.count);
    class_asset_offsets.reserve(header.moby_classes.count);
    std::uint32_t previous_class_asset_offset = 0U;
    for (std::uint32_t index = 0U; index < header.moby_classes.count; ++index) {
        const auto record_offset = checked_add(
            header.moby_classes.offset,
            checked_multiply(
                index,
                kRacLevelCoreMobyClassEntryBytesV1,
                "a Moby-class record offset"),
            "a Moby-class record offset");
        const auto host_offset = static_cast<std::size_t>(record_offset);

        RacLevelCoreMobyClassEntryV1 entry;
        entry.table_entry_range = {
            record_offset,
            kRacLevelCoreMobyClassEntryBytesV1,
        };
        entry.asset_offset = read_le32(index_bytes, host_offset);
        entry.class_id = read_le_i32(index_bytes, host_offset + 0x04U);
        entry.reserved_words[0U] = read_le32(index_bytes, host_offset + 0x08U);
        entry.reserved_words[1U] = read_le32(index_bytes, host_offset + 0x0cU);
        if (entry.class_id < 0 || entry.reserved_words[0U] != 0U ||
            entry.reserved_words[1U] != 0U) {
            fail("A Moby-class entry has invalid ID or reserved words");
        }
        if (!class_indices.emplace(entry.class_id, result.moby_classes.size())
                 .second) {
            fail("RacLevelCoreIndexV1 contains a duplicate Moby class ID");
        }

        bool saw_unused_texture = false;
        for (std::size_t slot = 0U; slot < entry.texture_slots.size(); ++slot) {
            const auto texture = byte_value(
                index_bytes[host_offset + 0x10U + slot]);
            entry.texture_slots[slot] = texture;
            if (texture == kRacLevelCoreUnusedTextureSlotV1) {
                saw_unused_texture = true;
                continue;
            }
            if (saw_unused_texture) {
                fail("A Moby-class texture appears after the 0xff sentinel");
            }
            if (texture >= header.moby_textures.count) {
                fail("A Moby-class texture index exceeds the Moby-texture table");
            }
            ++entry.used_texture_slot_count;
        }

        if (entry.asset_offset != 0U) {
            if (!is_aligned(
                    entry.asset_offset, kRacLevelCoreAssetAlignmentV1) ||
                entry.asset_offset >= decoded_asset_bytes.size()) {
                fail("A Moby-class asset offset is outside the decoded core");
            }
            if (previous_class_asset_offset != 0U &&
                entry.asset_offset <= previous_class_asset_offset) {
                fail("Moby-class asset offsets are not strictly increasing");
            }
            if (!class_asset_offsets.insert(entry.asset_offset).second) {
                fail("RacLevelCoreIndexV1 contains duplicate Moby asset offsets");
            }
            previous_class_asset_offset = entry.asset_offset;
        }
        result.moby_classes.push_back(entry);
    }

    std::unordered_set<std::int32_t> tie_class_ids;
    std::unordered_set<std::uint32_t> tie_asset_offsets;
    if (static_cast<std::uint64_t>(header.tie_classes.count) >
            static_cast<std::uint64_t>(result.tie_classes.max_size()) ||
        static_cast<std::uint64_t>(header.tie_classes.count) >
            static_cast<std::uint64_t>(tie_class_ids.max_size()) ||
        static_cast<std::uint64_t>(header.tie_classes.count) >
            static_cast<std::uint64_t>(tie_asset_offsets.max_size())) {
        fail("RacLevelCoreIndexV1 tie metadata exceeds a host container limit");
    }
    result.tie_classes.reserve(header.tie_classes.count);
    tie_class_ids.reserve(header.tie_classes.count);
    tie_asset_offsets.reserve(header.tie_classes.count);
    for (std::uint32_t index = 0U; index < header.tie_classes.count; ++index) {
        const auto record_offset = checked_add(
            header.tie_classes.offset,
            checked_multiply(
                index,
                kRacLevelCoreTieClassEntryBytesV1,
                "a tie-class record offset"),
            "a tie-class record offset");
        const auto host_offset = static_cast<std::size_t>(record_offset);

        RacLevelCoreTieClassEntryV1 entry;
        entry.table_entry_range = {
            record_offset,
            kRacLevelCoreTieClassEntryBytesV1,
        };
        entry.asset_offset = read_le32(index_bytes, host_offset);
        entry.class_id = read_le_i32(index_bytes, host_offset + 0x04U);
        entry.reserved_words[0U] = read_le32(index_bytes, host_offset + 0x08U);
        entry.reserved_words[1U] = read_le32(index_bytes, host_offset + 0x0cU);
        if (entry.class_id < 0 || entry.reserved_words[0U] != 0U ||
            entry.reserved_words[1U] != 0U ||
            !tie_class_ids.insert(entry.class_id).second) {
            fail("A tie-class entry has an invalid ID or reserved words");
        }

        bool saw_unused_texture = false;
        for (std::size_t slot = 0U; slot < entry.texture_slots.size(); ++slot) {
            const auto texture =
                byte_value(index_bytes[host_offset + 0x10U + slot]);
            entry.texture_slots[slot] = texture;
            if (texture == kRacLevelCoreUnusedTextureSlotV1) {
                saw_unused_texture = true;
                continue;
            }
            if (saw_unused_texture) {
                fail("A tie-class texture appears after the 0xff sentinel");
            }
            if (texture >= header.tie_textures.count) {
                fail("A tie-class texture index exceeds the tie-texture table");
            }
            ++entry.used_texture_slot_count;
        }

        if (entry.asset_offset != 0U) {
            if (!is_aligned(entry.asset_offset, kRacLevelCoreAssetAlignmentV1) ||
                entry.asset_offset >= decoded_asset_bytes.size()) {
                fail("A tie-class asset offset is outside the decoded core");
            }
            if (!tie_asset_offsets.insert(entry.asset_offset).second) {
                fail("RacLevelCoreIndexV1 contains duplicate tie asset offsets");
            }
        }
        result.tie_classes.push_back(entry);
    }

    std::unordered_set<std::int32_t> shrub_class_ids;
    std::unordered_set<std::uint32_t> shrub_asset_offsets;
    if (static_cast<std::uint64_t>(header.shrub_classes.count) >
            static_cast<std::uint64_t>(result.shrub_classes.max_size()) ||
        static_cast<std::uint64_t>(header.shrub_classes.count) >
            static_cast<std::uint64_t>(shrub_class_ids.max_size()) ||
        static_cast<std::uint64_t>(header.shrub_classes.count) >
            static_cast<std::uint64_t>(shrub_asset_offsets.max_size())) {
        fail("RacLevelCoreIndexV1 shrub metadata exceeds a host container limit");
    }
    result.shrub_classes.reserve(header.shrub_classes.count);
    shrub_class_ids.reserve(header.shrub_classes.count);
    shrub_asset_offsets.reserve(header.shrub_classes.count);
    for (std::uint32_t index = 0U; index < header.shrub_classes.count;
         ++index) {
        const auto record_offset = checked_add(
            header.shrub_classes.offset,
            checked_multiply(
                index,
                kRacLevelCoreShrubClassEntryBytesV1,
                "a shrub-class record offset"),
            "a shrub-class record offset");
        const auto host_offset = static_cast<std::size_t>(record_offset);

        RacLevelCoreShrubClassEntryV1 entry;
        entry.table_entry_range = {
            record_offset,
            kRacLevelCoreShrubClassEntryBytesV1,
        };
        entry.asset_offset = read_le32(index_bytes, host_offset);
        entry.class_id = read_le_i32(index_bytes, host_offset + 0x04U);
        entry.reserved_words[0U] = read_le32(index_bytes, host_offset + 0x08U);
        entry.reserved_words[1U] = read_le32(index_bytes, host_offset + 0x0cU);
        if (entry.class_id < 0 || entry.reserved_words[0U] != 0U ||
            entry.reserved_words[1U] != 0U ||
            !shrub_class_ids.insert(entry.class_id).second) {
            fail("A shrub-class entry has an invalid ID or reserved words");
        }

        bool saw_unused_texture = false;
        for (std::size_t slot = 0U; slot < entry.texture_slots.size(); ++slot) {
            const auto texture =
                byte_value(index_bytes[host_offset + 0x10U + slot]);
            entry.texture_slots[slot] = texture;
            if (texture == kRacLevelCoreUnusedTextureSlotV1) {
                saw_unused_texture = true;
                continue;
            }
            if (saw_unused_texture) {
                fail("A shrub-class texture appears after the 0xff sentinel");
            }
            if (texture >= header.shrub_textures.count) {
                fail(
                    "A shrub-class texture index exceeds the shrub-texture table");
            }
            ++entry.used_texture_slot_count;
        }
        entry.billboard.texture_width = read_le_i16(index_bytes, host_offset + 0x20U);
        entry.billboard.texture_height = read_le_i16(index_bytes, host_offset + 0x22U);
        entry.billboard.maximum_mipmap_level =
            read_le_i16(index_bytes, host_offset + 0x24U);
        entry.billboard.palette_offset =
            read_le_i16(index_bytes, host_offset + 0x26U);
        entry.billboard.texture_offset =
            read_le_i16(index_bytes, host_offset + 0x28U);
        for (std::size_t mip = 0U; mip < entry.billboard.mipmap_offsets.size();
             ++mip) {
            entry.billboard.mipmap_offsets[mip] = read_le_i16(
                index_bytes, host_offset + 0x2aU + mip * sizeof(std::int16_t));
        }

        if (entry.asset_offset != 0U) {
            if (!is_aligned(entry.asset_offset, kRacLevelCoreAssetAlignmentV1) ||
                entry.asset_offset >= decoded_asset_bytes.size()) {
                fail("A shrub-class asset offset is outside the decoded core");
            }
            if (!shrub_asset_offsets.insert(entry.asset_offset).second) {
                fail("RacLevelCoreIndexV1 contains duplicate shrub asset offsets");
            }
        }
        result.shrub_classes.push_back(entry);
    }

    std::vector<std::uint32_t> asset_boundaries;
    auto asset_boundary_capacity = checked_add(
        header.moby_classes.count,
        header.tie_classes.count,
        "the level-core asset-boundary capacity");
    asset_boundary_capacity = checked_add(
        asset_boundary_capacity,
        header.shrub_classes.count,
        "the level-core asset-boundary capacity");
    asset_boundary_capacity = checked_add(
        asset_boundary_capacity,
        kRacLevelCoreRatchetSequenceCountV1,
        "the level-core asset-boundary capacity");
    asset_boundary_capacity = checked_add(
        asset_boundary_capacity,
        header.gadget_count,
        "the level-core asset-boundary capacity");
    asset_boundary_capacity = checked_add(
        asset_boundary_capacity, 8U, "the level-core asset-boundary capacity");
    if (asset_boundary_capacity >
        static_cast<std::uint64_t>(asset_boundaries.max_size())) {
        fail("RacLevelCoreIndexV1 asset boundaries exceed a host container limit");
    }
    asset_boundaries.reserve(static_cast<std::size_t>(asset_boundary_capacity));
    asset_boundaries.push_back(0U);
    asset_boundaries.push_back(
        static_cast<std::uint32_t>(decoded_asset_bytes.size()));
    add_aligned_asset_boundary(
        asset_boundaries,
        header.occlusion_offset,
        decoded_asset_bytes.size(),
        kRacLevelCoreAssetAlignmentV1,
        "The occlusion block");
    add_aligned_asset_boundary(
        asset_boundaries,
        header.sky_offset,
        decoded_asset_bytes.size(),
        kRacLevelCoreAssetAlignmentV1,
        "The sky block");
    add_aligned_asset_boundary(
        asset_boundaries,
        header.collision_offset,
        decoded_asset_bytes.size(),
        kRacLevelCoreAssetAlignmentV1,
        "The collision block");
    add_aligned_asset_boundary(
        asset_boundaries,
        header.textures_base_offset,
        decoded_asset_bytes.size(),
        kRacLevelCoreAssetAlignmentV1,
        "The shared-texture block");
    add_aligned_asset_boundary(
        asset_boundaries,
        header.rac1_tail_words[2U],
        decoded_asset_bytes.size(),
        kRacLevelCoreIndexAlignmentV1,
        "The RAC1 tail boundary");
    for (const auto& entry : result.moby_classes) {
        if (entry.asset_offset != 0U) {
            asset_boundaries.push_back(entry.asset_offset);
        }
    }
    for (const auto& entry : result.tie_classes) {
        add_aligned_asset_boundary(
            asset_boundaries,
            entry.asset_offset,
            decoded_asset_bytes.size(),
            kRacLevelCoreAssetAlignmentV1,
            "A tie-class asset");
    }
    for (const auto& entry : result.shrub_classes) {
        add_aligned_asset_boundary(
            asset_boundaries,
            entry.asset_offset,
            decoded_asset_bytes.size(),
            kRacLevelCoreAssetAlignmentV1,
            "A shrub-class asset");
    }
    for (std::uint32_t index = 0U;
         index < kRacLevelCoreRatchetSequenceCountV1;
         ++index) {
        const auto offset = read_le32(
            index_bytes,
            static_cast<std::size_t>(header.ratchet_sequences_offset) +
                static_cast<std::size_t>(index) * sizeof(std::uint32_t));
        result.ratchet_sequence_offsets[index] = offset;
        add_aligned_asset_boundary(
            asset_boundaries,
            offset,
            decoded_asset_bytes.size(),
            kRacLevelCoreIndexAlignmentV1,
            "A Ratchet-sequence asset");
    }

    std::unordered_set<std::int32_t> gadget_class_ids;
    if (static_cast<std::uint64_t>(header.gadget_count) >
            static_cast<std::uint64_t>(result.gadgets.max_size()) ||
        static_cast<std::uint64_t>(header.gadget_count) >
            static_cast<std::uint64_t>(gadget_class_ids.max_size())) {
        fail("RacLevelCoreIndexV1 gadget metadata exceeds a host container limit");
    }
    result.gadgets.reserve(header.gadget_count);
    gadget_class_ids.reserve(header.gadget_count);
    for (std::uint32_t index = 0U; index < header.gadget_count; ++index) {
        const auto record_offset = checked_add(
            header.gadget_offset,
            checked_multiply(
                index,
                kRacLevelCoreGadgetEntryBytesV1,
                "a gadget record offset"),
            "a gadget record offset");
        const auto host_offset = static_cast<std::size_t>(record_offset);

        RacLevelCoreGadgetEntryV1 entry;
        entry.table_entry_range = {
            record_offset,
            kRacLevelCoreGadgetEntryBytesV1,
        };
        entry.asset_offset = read_le32(index_bytes, host_offset);
        entry.class_id = read_le_i32(index_bytes, host_offset + 0x04U);
        entry.encoded_size = read_le32(index_bytes, host_offset + 0x08U);
        entry.reserved_word = read_le32(index_bytes, host_offset + 0x0cU);
        entry.encoded_range = {entry.asset_offset, entry.encoded_size};

        if (entry.class_id < 0 || entry.reserved_word != 0U ||
            entry.encoded_size < kWadV1MinimumLogicalBytes ||
            !is_aligned(
                entry.asset_offset, kRacLevelCoreIndexAlignmentV1)) {
            fail("A gadget entry has invalid fixed fields");
        }
        if (!gadget_class_ids.insert(entry.class_id).second) {
            fail("RacLevelCoreIndexV1 contains a duplicate gadget class ID");
        }
        const auto class_match = class_indices.find(entry.class_id);
        if (class_match == class_indices.end() ||
            result.moby_classes[class_match->second].asset_offset != 0U) {
            fail("A gadget ID lacks its zero-offset Moby-class entry");
        }
        require_range(
            entry.encoded_range.offset,
            entry.encoded_range.size,
            decoded_asset_bytes.size(),
            "A gadget WadV1 range");
        const auto gadget_offset = static_cast<std::size_t>(entry.asset_offset);
        if (!has_wad_magic(decoded_asset_bytes, gadget_offset) ||
            read_le32(decoded_asset_bytes, gadget_offset + 3U) !=
                entry.encoded_size) {
            fail("A gadget range is not one complete WadV1 record");
        }

        const auto logical_end = range_end(
            entry.encoded_range, "a gadget WadV1 logical end");
        const auto padded_end =
            align_up_asset(logical_end, "a gadget WadV1 logical end");
        const auto expected_next = index + 1U < header.gadget_count
            ? static_cast<std::uint64_t>(read_le32(
                  index_bytes,
                  host_offset + kRacLevelCoreGadgetEntryBytesV1))
            : static_cast<std::uint64_t>(decoded_asset_bytes.size());
        if (padded_end != expected_next) {
            fail("Gadget WadV1 records do not form the terminal 0x40-aligned chain");
        }
        entry.padding_after_range = {logical_end, padded_end - logical_end};
        if (!is_zero_range(decoded_asset_bytes, entry.padding_after_range)) {
            fail("A gadget WadV1 has non-zero alignment padding");
        }
        result.total_gadget_encoded_bytes = checked_add(
            result.total_gadget_encoded_bytes,
            entry.encoded_size,
            "the total gadget WadV1 bytes");
        result.total_gadget_padding_bytes = checked_add(
            result.total_gadget_padding_bytes,
            entry.padding_after_range.size,
            "the total gadget padding bytes");
        asset_boundaries.push_back(entry.asset_offset);
        result.gadgets.push_back(entry);
    }

    result.gadget_asset_prefix_range = {
        0U,
        result.gadgets.front().asset_offset,
    };
    result.gadget_asset_chain_range = {
        result.gadgets.front().asset_offset,
        decoded_asset_bytes.size() - result.gadgets.front().asset_offset,
    };

    std::sort(asset_boundaries.begin(), asset_boundaries.end());
    asset_boundaries.erase(
        std::unique(asset_boundaries.begin(), asset_boundaries.end()),
        asset_boundaries.end());

    std::vector<std::uint32_t> unique_ratchet_sequence_offsets;
    if (static_cast<std::uint64_t>(kRacLevelCoreRatchetSequenceCountV1) >
            static_cast<std::uint64_t>(
                unique_ratchet_sequence_offsets.max_size()) ||
        static_cast<std::uint64_t>(kRacLevelCoreRatchetSequenceCountV1) >
            static_cast<std::uint64_t>(result.ratchet_sequences.max_size())) {
        fail("RacLevelCoreIndexV1 Ratchet-sequence metadata exceeds a host "
             "container limit");
    }
    unique_ratchet_sequence_offsets.reserve(
        kRacLevelCoreRatchetSequenceCountV1);
    for (const auto offset : result.ratchet_sequence_offsets) {
        if (offset != 0U) {
            unique_ratchet_sequence_offsets.push_back(offset);
        }
    }
    std::sort(
        unique_ratchet_sequence_offsets.begin(),
        unique_ratchet_sequence_offsets.end());
    unique_ratchet_sequence_offsets.erase(
        std::unique(
            unique_ratchet_sequence_offsets.begin(),
            unique_ratchet_sequence_offsets.end()),
        unique_ratchet_sequence_offsets.end());
    result.ratchet_sequences.reserve(unique_ratchet_sequence_offsets.size());
    for (const auto offset : unique_ratchet_sequence_offsets) {
        const auto next = std::upper_bound(
            asset_boundaries.begin(), asset_boundaries.end(), offset);
        if (next == asset_boundaries.end() || *next <= offset) {
            fail("A Ratchet-sequence asset has no greater proven block "
                 "boundary");
        }
        result.ratchet_sequences.push_back(RacLevelCoreRatchetSequenceV1{
            offset,
            RacLevelCoreRangeV1{
                offset,
                static_cast<std::uint64_t>(*next) - offset,
            },
        });
    }

    if (header.collision_offset != 0U) {
        const auto next = std::upper_bound(
            asset_boundaries.begin(), asset_boundaries.end(),
            header.collision_offset);
        if (next == asset_boundaries.end() ||
            *next <= header.collision_offset) {
            fail("The collision asset has no greater proven block boundary");
        }
        result.collision_asset_range = {
            header.collision_offset,
            static_cast<std::uint64_t>(*next) - header.collision_offset,
        };
    }
    for (auto& entry : result.moby_classes) {
        if (entry.asset_offset == 0U) {
            continue;
        }
        const auto next = std::upper_bound(
            asset_boundaries.begin(),
            asset_boundaries.end(),
            entry.asset_offset);
        if (next == asset_boundaries.end() || *next <= entry.asset_offset) {
            fail("A Moby-class asset has no greater proven block boundary");
        }
        entry.asset_range = {
            entry.asset_offset,
            static_cast<std::uint64_t>(*next) - entry.asset_offset,
        };
    }
    for (auto& entry : result.tie_classes) {
        if (entry.asset_offset == 0U) {
            continue;
        }
        const auto next = std::upper_bound(
            asset_boundaries.begin(), asset_boundaries.end(), entry.asset_offset);
        if (next == asset_boundaries.end() || *next <= entry.asset_offset) {
            fail("A tie-class asset has no greater proven block boundary");
        }
        entry.asset_range = {
            entry.asset_offset,
            static_cast<std::uint64_t>(*next) - entry.asset_offset,
        };
    }
    for (auto& entry : result.shrub_classes) {
        if (entry.asset_offset == 0U) {
            continue;
        }
        const auto next = std::upper_bound(
            asset_boundaries.begin(), asset_boundaries.end(), entry.asset_offset);
        if (next == asset_boundaries.end() || *next <= entry.asset_offset) {
            fail("A shrub-class asset has no greater proven block boundary");
        }
        entry.asset_range = {
            entry.asset_offset,
            static_cast<std::uint64_t>(*next) - entry.asset_offset,
        };
    }

    return result;
}

} // namespace openrc
