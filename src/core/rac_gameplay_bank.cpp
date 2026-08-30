#include "openrc/rac_gameplay_bank.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace openrc {
namespace {

constexpr std::uint32_t kDirectoryPadOffset = 0x90U;
constexpr std::uint32_t kRacLevelSettingsBytes = 0x50U;
constexpr std::uint32_t kMobyBlockHeaderBytes = 0x10U;

struct BlockDescriptionV1 {
    RacGameplayBlockKindV1 kind;
    std::uint32_t pointer_offset;
    std::string_view name;
};

// This is the serialization order used by the original RAC1 gameplay bank.
// Header slots are intentionally not ordered this way.
constexpr std::array<BlockDescriptionV1, kRacGameplayBlockCountV1>
    kBlockDescriptions{{
        {RacGameplayBlockKindV1::environment_sample_points,
         0x88U,
         "environment sample points"},
        {RacGameplayBlockKindV1::level_settings, 0x00U, "level settings"},
        {RacGameplayBlockKindV1::help_us_english,
         0x10U,
         "US English help messages"},
        {RacGameplayBlockKindV1::help_uk_english,
         0x14U,
         "UK English help messages"},
        {RacGameplayBlockKindV1::help_french, 0x18U, "French help messages"},
        {RacGameplayBlockKindV1::help_german, 0x1cU, "German help messages"},
        {RacGameplayBlockKindV1::help_spanish, 0x20U, "Spanish help messages"},
        {RacGameplayBlockKindV1::help_italian, 0x24U, "Italian help messages"},
        {RacGameplayBlockKindV1::help_japanese,
         0x28U,
         "Japanese help messages"},
        {RacGameplayBlockKindV1::help_korean, 0x2cU, "Korean help messages"},
        {RacGameplayBlockKindV1::directional_lights,
         0x04U,
         "directional lights"},
        {RacGameplayBlockKindV1::environment_transitions,
         0x80U,
         "environment transitions"},
        {RacGameplayBlockKindV1::cameras, 0x08U, "cameras"},
        {RacGameplayBlockKindV1::sound_instances, 0x0cU, "sound instances"},
        {RacGameplayBlockKindV1::moby_classes, 0x40U, "moby classes"},
        {RacGameplayBlockKindV1::moby_instances, 0x44U, "moby instances"},
        {RacGameplayBlockKindV1::pvar_table, 0x54U, "pvar table"},
        {RacGameplayBlockKindV1::pvar_data, 0x58U, "pvar data"},
        {RacGameplayBlockKindV1::pvar_moby_links,
         0x50U,
         "moby-link pvar fixups"},
        {RacGameplayBlockKindV1::pvar_relative_pointers,
         0x5cU,
         "relative pvar pointers"},
        {RacGameplayBlockKindV1::moby_groups, 0x48U, "moby groups"},
        {RacGameplayBlockKindV1::shared_data, 0x4cU, "shared data"},
        {RacGameplayBlockKindV1::tie_classes, 0x30U, "tie classes"},
        {RacGameplayBlockKindV1::tie_instances, 0x34U, "tie instances"},
        {RacGameplayBlockKindV1::shrub_classes, 0x38U, "shrub classes"},
        {RacGameplayBlockKindV1::shrub_instances, 0x3cU, "shrub instances"},
        {RacGameplayBlockKindV1::paths, 0x70U, "paths"},
        {RacGameplayBlockKindV1::cuboids, 0x60U, "cuboids"},
        {RacGameplayBlockKindV1::spheres, 0x64U, "spheres"},
        {RacGameplayBlockKindV1::cylinders, 0x68U, "cylinders"},
        {RacGameplayBlockKindV1::pills, 0x6cU, "pills"},
        {RacGameplayBlockKindV1::camera_collision_grid,
         0x84U,
         "camera collision grid"},
        {RacGameplayBlockKindV1::point_lights, 0x7cU, "point lights"},
        {RacGameplayBlockKindV1::point_light_grid, 0x78U, "point light grid"},
        {RacGameplayBlockKindV1::grind_paths, 0x74U, "grind paths"},
        {RacGameplayBlockKindV1::occlusion_mappings,
         0x8cU,
         "occlusion mappings"},
    }};

[[noreturn]] void fail(const std::string& message) {
    throw RacGameplayBankError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_le32(const std::span<const std::byte> bytes,
                                      const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
           (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
           (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
           (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] bool is_zero(const std::span<const std::byte> bytes,
                           const std::size_t begin,
                           const std::size_t end) {
    for (auto offset = begin; offset < end; ++offset) {
        if (bytes[offset] != std::byte{0}) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const RacGameplayBlockV1&
require_block(const RacGameplayBankV1& bank,
              const RacGameplayBlockKindV1 kind,
              const char* const description) {
    const auto* const block = find_rac_gameplay_block_v1(bank, kind);
    if (block == nullptr) {
        fail(std::string("RacGameplayBankV1 is missing its ") + description);
    }
    return *block;
}

void validate_zero_tail(const std::span<const std::byte> bytes,
                        const RacGameplayBlockV1& block,
                        const std::uint64_t logical_bytes,
                        const char* const description) {
    if (logical_bytes > block.range.size) {
        fail(std::string("RacGameplayBankV1 has a truncated ") + description);
    }
    const auto tail_begin =
        static_cast<std::size_t>(block.range.offset + logical_bytes);
    const auto tail_end =
        static_cast<std::size_t>(block.range.offset + block.range.size);
    if (!is_zero(bytes, tail_begin, tail_end)) {
        fail(std::string("RacGameplayBankV1 has non-zero padding after ") +
             description);
    }
}

[[nodiscard]] std::uint64_t
checked_table_bytes(const std::uint32_t count,
                    const std::uint32_t header_bytes,
                    const std::uint32_t record_bytes,
                    const char* const description) {
    const auto records = static_cast<std::uint64_t>(count) * record_bytes;
    if (records > std::numeric_limits<std::uint64_t>::max() - header_bytes) {
        fail(std::string("Integer overflow while sizing ") + description);
    }
    return header_bytes + records;
}

void validate_semantic_anchors(const std::span<const std::byte> bytes,
                               RacGameplayBankV1& result) {
    const auto& level_settings = require_block(
        result, RacGameplayBlockKindV1::level_settings, "level-settings block");
    if (level_settings.range.size < kRacLevelSettingsBytes) {
        fail("RacGameplayBankV1 has truncated RAC1 level settings");
    }
    const auto settings_offset =
        static_cast<std::size_t>(level_settings.range.offset);
    if (read_le32(bytes, settings_offset + 0x48U) != 0U ||
        read_le32(bytes, settings_offset + 0x4cU) != 0U) {
        fail("RacGameplayBankV1 has non-zero level-settings padding");
    }
    validate_zero_tail(
        bytes, level_settings, kRacLevelSettingsBytes, "level settings");

    const auto& classes = require_block(
        result, RacGameplayBlockKindV1::moby_classes, "moby-class block");
    if (classes.range.size < sizeof(std::uint32_t)) {
        fail("RacGameplayBankV1 has a truncated moby-class block");
    }
    const auto classes_offset = static_cast<std::size_t>(classes.range.offset);
    const auto raw_class_count = read_le32(bytes, classes_offset);
    if (raw_class_count == 0U ||
        raw_class_count > static_cast<std::uint32_t>(
                              std::numeric_limits<std::int32_t>::max())) {
        fail("RacGameplayBankV1 has an invalid moby-class count");
    }
    const auto class_bytes = checked_table_bytes(raw_class_count,
                                                 sizeof(std::uint32_t),
                                                 sizeof(std::uint32_t),
                                                 "the moby-class list");
    validate_zero_tail(bytes, classes, class_bytes, "the moby-class list");
    result.moby_class_count = raw_class_count;

    const auto& mobies = require_block(
        result, RacGameplayBlockKindV1::moby_instances, "moby-instance block");
    if (mobies.range.size < kMobyBlockHeaderBytes) {
        fail("RacGameplayBankV1 has a truncated moby-instance header");
    }
    const auto moby_offset = static_cast<std::size_t>(mobies.range.offset);
    const auto raw_static_count = read_le32(bytes, moby_offset);
    const auto raw_spawnable_count = read_le32(bytes, moby_offset + 4U);
    if (raw_static_count == 0U ||
        raw_static_count > static_cast<std::uint32_t>(
                               std::numeric_limits<std::int32_t>::max()) ||
        raw_spawnable_count > static_cast<std::uint32_t>(
                                  std::numeric_limits<std::int32_t>::max()) ||
        read_le32(bytes, moby_offset + 8U) != 0U ||
        read_le32(bytes, moby_offset + 12U) != 0U) {
        fail("RacGameplayBankV1 has an invalid moby-instance header");
    }
    const auto moby_bytes = checked_table_bytes(raw_static_count,
                                                kMobyBlockHeaderBytes,
                                                kRacGameplayMobyRecordBytesV1,
                                                "the moby-instance list");
    if (moby_bytes > mobies.range.size) {
        fail("RacGameplayBankV1 has a truncated moby-instance list");
    }
    for (std::uint32_t index = 0U; index < raw_static_count; ++index) {
        const auto record_offset =
            moby_offset + kMobyBlockHeaderBytes +
            static_cast<std::size_t>(index) * kRacGameplayMobyRecordBytesV1;
        if (read_le32(bytes, record_offset) != kRacGameplayMobyRecordBytesV1) {
            fail("RacGameplayBankV1 has an invalid 0x78-byte moby record");
        }
    }
    validate_zero_tail(bytes, mobies, moby_bytes, "the moby-instance list");
    result.static_moby_count = raw_static_count;
    result.spawnable_moby_count = raw_spawnable_count;
}

} // namespace

std::string_view
rac_gameplay_block_name_v1(const RacGameplayBlockKindV1 kind) noexcept {
    for (const auto& description : kBlockDescriptions) {
        if (description.kind == kind) {
            return description.name;
        }
    }
    return "unknown";
}

const RacGameplayBlockV1*
find_rac_gameplay_block_v1(const RacGameplayBankV1& bank,
                           const RacGameplayBlockKindV1 kind) noexcept {
    const auto found = std::find_if(
        bank.blocks.begin(),
        bank.blocks.end(),
        [kind](const RacGameplayBlockV1& block) { return block.kind == kind; });
    return found == bank.blocks.end() ? nullptr : &*found;
}

RacGameplayBankV1
parse_rac_gameplay_bank_v1(const std::span<const std::byte> bytes,
                           const RacGameplayBankLimitsV1 limits) {
    if (limits.max_input_bytes == 0U) {
        fail("RacGameplayBankV1 caller limits must be non-zero");
    }
    if (bytes.size() > limits.max_input_bytes) {
        fail("RacGameplayBankV1 exceeds the caller's input-byte limit");
    }
    if (bytes.size() < kRacGameplayFirstBlockOffsetV1 ||
        bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
        (bytes.size() & (kRacGameplayBlockAlignmentV1 - 1U)) != 0U) {
        fail("RacGameplayBankV1 has an invalid input envelope");
    }

    RacGameplayBankV1 result;
    result.input_bytes = bytes.size();
    result.header_range = RacGameplayRangeV1{0U, kRacGameplayHeaderBytesV1};
    for (std::size_t slot = 0U; slot < result.block_offsets.size(); ++slot) {
        result.block_offsets[slot] =
            read_le32(bytes, slot * sizeof(std::uint32_t));
    }
    if (result.block_offsets[kDirectoryPadOffset / sizeof(std::uint32_t)] !=
        0U) {
        fail("RacGameplayBankV1 has a non-zero reserved directory slot");
    }

    std::uint32_t previous_offset = 0U;
    result.blocks.reserve(kRacGameplayBlockCountV1);
    for (const auto& description : kBlockDescriptions) {
        const auto block_offset =
            result.block_offsets[description.pointer_offset /
                                 sizeof(std::uint32_t)];
        if (block_offset == 0U) {
            fail("RacGameplayBankV1 has a missing block pointer");
        }
        if (block_offset < kRacGameplayFirstBlockOffsetV1 ||
            block_offset >= bytes.size() ||
            (block_offset & (kRacGameplayBlockAlignmentV1 - 1U)) != 0U) {
            fail("RacGameplayBankV1 has an invalid block pointer");
        }
        if (previous_offset != 0U && block_offset <= previous_offset) {
            fail("RacGameplayBankV1 block pointers violate physical order");
        }
        previous_offset = block_offset;
        result.blocks.push_back(
            RacGameplayBlockV1{description.kind,
                               description.pointer_offset,
                               RacGameplayRangeV1{block_offset, 0U}});
    }
    if (result.blocks.size() != kRacGameplayBlockCountV1 ||
        result.blocks.front().range.offset != kRacGameplayFirstBlockOffsetV1) {
        fail("RacGameplayBankV1 does not begin at its canonical block offset");
    }
    if (!is_zero(
            bytes, kRacGameplayHeaderBytesV1, kRacGameplayFirstBlockOffsetV1)) {
        fail("RacGameplayBankV1 has non-zero header alignment padding");
    }
    result.header_padding_range = RacGameplayRangeV1{
        kRacGameplayHeaderBytesV1,
        kRacGameplayFirstBlockOffsetV1 - kRacGameplayHeaderBytesV1};

    for (std::size_t index = 0U; index < result.blocks.size(); ++index) {
        const auto end = index + 1U < result.blocks.size()
                             ? result.blocks[index + 1U].range.offset
                             : bytes.size();
        result.blocks[index].range.size =
            end - result.blocks[index].range.offset;
    }

    validate_semantic_anchors(bytes, result);
    return result;
}

} // namespace openrc
