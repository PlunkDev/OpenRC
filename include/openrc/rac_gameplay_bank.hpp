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

struct RacGameplayBankV1 {
    std::uint64_t input_bytes = 0U;
    RacGameplayRangeV1 header_range;
    RacGameplayRangeV1 header_padding_range;
    std::array<std::uint32_t, kRacGameplayDirectorySlotCountV1> block_offsets{};
    // All 36 blocks are returned in their physical serialization order.
    std::vector<RacGameplayBlockV1> blocks;

    std::uint32_t moby_class_count = 0U;
    std::uint32_t static_moby_count = 0U;
    std::uint32_t spawnable_moby_count = 0U;
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
// every section remains a zero-copy range. The class list and 0x78-byte moby
// instance records provide a strict semantic anchor for format probing.
[[nodiscard]] RacGameplayBankV1
parse_rac_gameplay_bank_v1(std::span<const std::byte> bytes,
                           RacGameplayBankLimitsV1 limits);

} // namespace openrc
