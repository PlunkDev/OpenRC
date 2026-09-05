#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kRacLevelCoreHeaderBytesV1 = 0xbcU;
inline constexpr std::uint32_t kRacLevelCoreMobyTableOffsetV1 = 0xe0U;
inline constexpr std::uint32_t kRacLevelCoreMobyClassEntryBytesV1 = 0x20U;
inline constexpr std::uint32_t kRacLevelCoreTextureEntryBytesV1 = 0x10U;
inline constexpr std::uint32_t kRacLevelCoreTieClassEntryBytesV1 = 0x20U;
inline constexpr std::uint32_t kRacLevelCoreShrubClassEntryBytesV1 = 0x30U;
inline constexpr std::uint32_t kRacLevelCoreGadgetEntryBytesV1 = 0x10U;
inline constexpr std::uint32_t kRacLevelCoreIndexAlignmentV1 = 0x10U;
inline constexpr std::uint32_t kRacLevelCoreAssetAlignmentV1 = 0x40U;
inline constexpr std::uint32_t kRacLevelCoreRatchetSequenceCountV1 = 256U;
inline constexpr std::uint8_t kRacLevelCoreUnusedTextureSlotV1 = 0xffU;

struct RacLevelCoreLimitsV1 {
    std::uint64_t max_index_bytes = 0U;
    std::uint64_t max_encoded_asset_bytes = 0U;
    std::uint64_t max_decoded_asset_bytes = 0U;
    std::uint64_t max_moby_classes = 0U;
    std::uint64_t max_moby_textures = 0U;
    std::uint64_t max_gadgets = 0U;
    std::uint64_t max_tie_classes = 0U;
    std::uint64_t max_shrub_classes = 0U;
    std::uint64_t max_tie_textures = 0U;
    std::uint64_t max_shrub_textures = 0U;
};

struct RacLevelCoreRangeV1 {
    std::uint64_t offset = 0U;
    std::uint64_t size = 0U;

    [[nodiscard]] bool operator==(const RacLevelCoreRangeV1&) const = default;
};

struct RacLevelCoreArrayRangeV1 {
    std::uint32_t count = 0U;
    std::uint32_t offset = 0U;
    RacLevelCoreRangeV1 byte_range;
};

struct RacLevelCoreRatchetSequenceV1 {
    std::uint32_t asset_offset = 0U;
    RacLevelCoreRangeV1 asset_range;

    [[nodiscard]] bool
    operator==(const RacLevelCoreRatchetSequenceV1&) const = default;
};

struct RacLevelCoreHeaderV1 {
    std::array<std::uint32_t, kRacLevelCoreHeaderBytesV1 / 4U> raw_words{};

    RacLevelCoreArrayRangeV1 gs_ram;
    std::uint32_t tfrags_offset = 0U;
    std::uint32_t occlusion_offset = 0U;
    std::uint32_t sky_offset = 0U;
    std::uint32_t collision_offset = 0U;
    RacLevelCoreArrayRangeV1 moby_classes;
    RacLevelCoreArrayRangeV1 tie_classes;
    RacLevelCoreArrayRangeV1 shrub_classes;
    RacLevelCoreArrayRangeV1 tfrag_textures;
    RacLevelCoreArrayRangeV1 moby_textures;
    RacLevelCoreArrayRangeV1 tie_textures;
    RacLevelCoreArrayRangeV1 shrub_textures;
    RacLevelCoreArrayRangeV1 particle_textures;
    RacLevelCoreArrayRangeV1 fx_textures;
    std::uint32_t textures_base_offset = 0U;
    std::uint32_t particle_bank_offset = 0U;
    std::uint32_t fx_bank_offset = 0U;
    std::uint32_t particle_definitions_offset = 0U;
    std::uint32_t sound_remap_offset = 0U;
    std::uint32_t opaque_74 = 0U;
    std::uint32_t ratchet_sequences_offset = 0U;
    std::uint32_t scene_view_size = 0U;
    std::uint32_t gadget_count = 0U;
    std::uint32_t gadget_offset = 0U;
    std::uint32_t assets_encoded_size = 0U;
    std::uint32_t assets_decoded_size = 0U;
    std::uint32_t chrome_map_texture = 0U;
    std::uint32_t chrome_map_palette = 0U;
    std::uint32_t glass_map_texture = 0U;
    std::uint32_t glass_map_palette = 0U;
    std::uint32_t opaque_a0 = 0U;
    std::uint32_t heightmap_offset = 0U;
    std::uint32_t occlusion_octree_offset = 0U;
    std::array<std::uint32_t, 4> rac1_tail_words{};
};

struct RacLevelCoreMobyClassEntryV1 {
    RacLevelCoreRangeV1 table_entry_range;
    std::uint32_t asset_offset = 0U;
    std::int32_t class_id = 0;
    std::array<std::uint32_t, 2> reserved_words{};
    std::array<std::uint8_t, 16> texture_slots{};
    std::uint8_t used_texture_slot_count = 0U;

    // Empty when asset_offset is zero. Otherwise this is the exact block
    // envelope ending at the next greater proven level-core boundary.
    RacLevelCoreRangeV1 asset_range;
};

struct RacLevelCoreTieClassEntryV1 {
    RacLevelCoreRangeV1 table_entry_range;
    std::uint32_t asset_offset = 0U;
    std::int32_t class_id = 0;
    std::array<std::uint32_t, 2> reserved_words{};
    std::array<std::uint8_t, 16> texture_slots{};
    std::uint8_t used_texture_slot_count = 0U;
    RacLevelCoreRangeV1 asset_range;
};

struct RacLevelCoreShrubBillboardTextureV1 {
    std::int16_t texture_width = 0;
    std::int16_t texture_height = 0;
    std::int16_t maximum_mipmap_level = 0;
    std::int16_t palette_offset = 0;
    std::int16_t texture_offset = 0;
    std::array<std::int16_t, 3> mipmap_offsets{};
};

struct RacLevelCoreShrubClassEntryV1 {
    RacLevelCoreRangeV1 table_entry_range;
    std::uint32_t asset_offset = 0U;
    std::int32_t class_id = 0;
    std::array<std::uint32_t, 2> reserved_words{};
    std::array<std::uint8_t, 16> texture_slots{};
    std::uint8_t used_texture_slot_count = 0U;
    RacLevelCoreShrubBillboardTextureV1 billboard;
    RacLevelCoreRangeV1 asset_range;
};

struct RacLevelCoreGadgetEntryV1 {
    RacLevelCoreRangeV1 table_entry_range;
    std::uint32_t asset_offset = 0U;
    std::int32_t class_id = 0;
    std::uint32_t encoded_size = 0U;
    std::uint32_t reserved_word = 0U;
    RacLevelCoreRangeV1 encoded_range;
    RacLevelCoreRangeV1 padding_after_range;
};

struct RacLevelCoreIndexV1 {
    std::uint64_t index_input_bytes = 0U;
    std::uint64_t encoded_asset_input_bytes = 0U;
    std::uint64_t decoded_asset_input_bytes = 0U;
    RacLevelCoreRangeV1 header_range;

    // The original RAC1 image has a fixed 0x24-byte trailer between the
    // 0xbc-byte header and the class table. It is retained as a borrowed
    // range; the parser validates its complete byte signature.
    RacLevelCoreRangeV1 rac1_header_trailer_range;
    RacLevelCoreHeaderV1 header;
    RacLevelCoreRangeV1 moby_class_table_range;
    RacLevelCoreRangeV1 tie_class_table_range;
    RacLevelCoreRangeV1 shrub_class_table_range;
    RacLevelCoreRangeV1 tfrag_texture_table_range;
    RacLevelCoreRangeV1 moby_texture_table_range;
    RacLevelCoreRangeV1 tie_texture_table_range;
    RacLevelCoreRangeV1 shrub_texture_table_range;
    RacLevelCoreRangeV1 ratchet_sequence_table_range;
    RacLevelCoreRangeV1 gadget_table_range;
    // Exact decoded-core envelope for the level collision asset. Empty only
    // when the source header does not publish a collision offset.
    RacLevelCoreRangeV1 collision_asset_range;
    RacLevelCoreRangeV1 gadget_asset_prefix_range;
    RacLevelCoreRangeV1 gadget_asset_chain_range;
    std::uint64_t total_gadget_encoded_bytes = 0U;
    std::uint64_t total_gadget_padding_bytes = 0U;
    // Exact table slots, including zero entries and duplicate aliases.
    std::array<std::uint32_t, kRacLevelCoreRatchetSequenceCountV1>
        ratchet_sequence_offsets{};
    // One entry per distinct non-zero table offset, ordered by asset offset.
    // Each opaque range ends at the next greater proven decoded-core boundary;
    // duplicate table slots therefore share this single range description.
    std::vector<RacLevelCoreRatchetSequenceV1> ratchet_sequences;
    std::vector<RacLevelCoreMobyClassEntryV1> moby_classes;
    std::vector<RacLevelCoreTieClassEntryV1> tie_classes;
    std::vector<RacLevelCoreShrubClassEntryV1> shrub_classes;
    std::vector<RacLevelCoreGadgetEntryV1> gadgets;
};

class RacLevelCoreError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses the raw RAC1 primary-extent-0/subrange-2 level-core index against
// both forms of primary-extent-0/subrange-10. The encoded span is the complete
// outer WadV1 record; decoded_asset_bytes is its complete decoded payload.
// Returned ranges borrow from one of those caller-owned inputs as documented
// by their containing field.
[[nodiscard]] RacLevelCoreIndexV1 parse_rac_level_core_index_v1(
    std::span<const std::byte> index_bytes,
    std::span<const std::byte> encoded_asset_bytes,
    std::span<const std::byte> decoded_asset_bytes,
    RacLevelCoreLimitsV1 limits);

} // namespace openrc
