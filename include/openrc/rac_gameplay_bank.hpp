#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kRacGameplayHeaderBytesV1 = 0x94U;
inline constexpr std::uint32_t kRacGameplayFirstBlockOffsetV1 = 0xa0U;
inline constexpr std::uint32_t kRacGameplayBlockAlignmentV1 = 0x10U;
inline constexpr std::uint32_t kRacGameplayDirectorySlotCountV1 = 37U;
inline constexpr std::uint32_t kRacGameplayBlockCountV1 = 36U;
inline constexpr std::uint32_t kRacGameplayMobyRecordBytesV1 = 0x78U;
inline constexpr std::uint32_t kRacGameplayTieRecordBytesV1 = 0xe0U;
inline constexpr std::uint32_t kRacGameplayShrubRecordBytesV1 = 0x70U;

// Values follow the on-disc header slots. The physical block order is
// deliberately different and is reconstructed by the parser.
enum class RacGameplayBlockKindV1 : std::uint8_t {
    level_settings = 0U,
    directional_lights,
    cameras,
    sound_instances,
    help_us_english,
    help_uk_english,
    help_french,
    help_german,
    help_spanish,
    help_italian,
    help_japanese,
    help_korean,
    tie_classes,
    tie_instances,
    shrub_classes,
    shrub_instances,
    moby_classes,
    moby_instances,
    moby_groups,
    shared_data,
    pvar_moby_links,
    pvar_table,
    pvar_data,
    pvar_relative_pointers,
    cuboids,
    spheres,
    cylinders,
    pills,
    paths,
    grind_paths,
    point_light_grid,
    point_lights,
    environment_transitions,
    camera_collision_grid,
    environment_sample_points,
    occlusion_mappings,
};

struct RacGameplayBankLimitsV1 {
    std::uint64_t max_input_bytes = 0U;
    std::uint64_t max_moby_classes = 65'536U;
    std::uint64_t max_static_mobies = 65'536U;
    std::uint64_t max_tie_classes = 65'536U;
    std::uint64_t max_tie_instances = 65'536U;
    std::uint64_t max_shrub_classes = 65'536U;
    std::uint64_t max_shrub_instances = 65'536U;
};

struct RacGameplayRangeV1 {
    std::uint64_t offset = 0U;
    std::uint64_t size = 0U;

    [[nodiscard]] bool operator==(const RacGameplayRangeV1&) const = default;
};

struct RacGameplayBlockV1 {
    RacGameplayBlockKindV1 kind = RacGameplayBlockKindV1::level_settings;
    std::uint32_t header_pointer_offset = 0U;
    RacGameplayRangeV1 range;
};

// The fixed 0x50-byte RAC1 level-settings record. Every source word remains
// available so package compilers can reproduce or audit the original data,
// while fields needed by the native runtime are decoded explicitly.
struct RacGameplayLevelSettingsV1 {
    RacGameplayRangeV1 record_range;
    std::array<std::uint32_t, 0x50U / sizeof(std::uint32_t)> raw_words{};
    std::array<std::int32_t, 3U> background_colour{};
    std::array<std::int32_t, 3U> fog_colour{};
    std::uint32_t fog_near_distance_bits = 0U;
    float fog_near_distance = 0.0F;
    std::uint32_t fog_far_distance_bits = 0U;
    float fog_far_distance = 0.0F;
    std::uint32_t fog_near_intensity_bits = 0U;
    float fog_near_intensity = 0.0F;
    std::uint32_t fog_far_intensity_bits = 0U;
    float fog_far_intensity = 0.0F;
    std::uint32_t death_height_bits = 0U;
    float death_height = 0.0F;
    std::array<std::uint32_t, 3U> ship_position_bits{};
    std::array<float, 3U> ship_position{};
    std::uint32_t ship_rotation_z_bits = 0U;
    float ship_rotation_z = 0.0F;
    std::int32_t ship_path = 0;
    std::int32_t ship_camera_cuboid_start = 0;
    std::int32_t ship_camera_cuboid_end = 0;
};

struct RacGameplayMobyInstanceV1 {
    RacGameplayRangeV1 record_range;
    std::uint32_t class_id = 0U;
    std::uint32_t scale_bits = 0U;
    float scale = 0.0F;
    // Wrench labels offset 0x20 as an f32 draw distance, but every PAL v2.00
    // record observed so far stores a small integer-like word (0..1023).
    // Preserve the value without inventing units until runtime use is proven.
    std::uint32_t draw_distance_raw = 0U;
    std::int32_t update_distance = 0;
    std::array<std::uint32_t, 3> position_bits{};
    std::array<float, 3> position{};
    std::array<std::uint32_t, 3> rotation_bits{};
    std::array<float, 3> rotation{};
    std::int32_t group_index = 0;
    std::int32_t rooted = 0;
    std::uint32_t rooted_distance_bits = 0U;
    float rooted_distance = 0.0F;
    std::int32_t pvar_index = 0;
    std::int32_t occlusion = 0;
    std::uint32_t mode_bits = 0U;
    std::int32_t light_index = 0;
};

// TIE and shrub placement records carry a complete 4x4 matrix at byte 0x10.
// The remaining words are retained verbatim until their runtime meaning is
// proven. This keeps the complete fixed-size record available without
// assigning semantics to lighting, occlusion, or visibility fields yet.
struct RacGameplayTieInstanceV1 {
    RacGameplayRangeV1 record_range;
    std::uint32_t class_id = 0U;
    std::array<std::uint32_t, 16U> matrix_bits{};
    std::array<float, 16U> matrix{};
    std::array<std::uint32_t, kRacGameplayTieRecordBytesV1 / 4U> raw_words{};
};

struct RacGameplayShrubInstanceV1 {
    RacGameplayRangeV1 record_range;
    std::uint32_t class_id = 0U;
    std::array<std::uint32_t, 16U> matrix_bits{};
    std::array<float, 16U> matrix{};
    std::array<std::uint32_t, kRacGameplayShrubRecordBytesV1 / 4U> raw_words{};
};

struct RacGameplayBankV1 {
    std::uint64_t input_bytes = 0U;
    RacGameplayRangeV1 header_range;
    RacGameplayRangeV1 header_padding_range;
    std::array<std::uint32_t, kRacGameplayDirectorySlotCountV1> block_offsets{};
    // All 36 blocks are returned in their physical serialization order.
    std::vector<RacGameplayBlockV1> blocks;

    RacGameplayLevelSettingsV1 level_settings;

    std::uint32_t moby_class_count = 0U;
    std::vector<std::uint32_t> moby_class_ids;
    std::uint32_t static_moby_count = 0U;
    std::uint32_t spawnable_moby_count = 0U;
    std::vector<RacGameplayMobyInstanceV1> static_mobies;

    std::uint32_t tie_class_count = 0U;
    std::vector<std::uint32_t> tie_class_ids;
    std::uint32_t tie_instance_count = 0U;
    std::vector<RacGameplayTieInstanceV1> tie_instances;

    std::uint32_t shrub_class_count = 0U;
    std::vector<std::uint32_t> shrub_class_ids;
    std::uint32_t shrub_instance_count = 0U;
    std::vector<RacGameplayShrubInstanceV1> shrub_instances;
};

class RacGameplayBankError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string_view
rac_gameplay_block_name_v1(RacGameplayBlockKindV1 kind) noexcept;

[[nodiscard]] const RacGameplayBlockV1*
find_rac_gameplay_block_v1(const RacGameplayBankV1& bank,
                           RacGameplayBlockKindV1 kind) noexcept;

// Parses one complete, decoded Ratchet & Clank (2002) level gameplay bank.
// The fixed pointer directory and its physical ordering are validated, while
// every section remains a zero-copy range. Moby, TIE, and shrub class lists
// and fixed-size instance records provide strict semantic anchors for format
// probing. Moby placement fields and complete TIE/shrub transform matrices
// are decoded while all TIE/shrub record words remain available verbatim.
[[nodiscard]] RacGameplayBankV1
parse_rac_gameplay_bank_v1(std::span<const std::byte> bytes,
                           RacGameplayBankLimitsV1 limits);

} // namespace openrc
