#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kSBlkBundleV3Version = 3;
inline constexpr std::uint32_t kSBlkBundleV3RecordCount = 2;
inline constexpr std::uint32_t kSBlkBundleV3HeaderSize = 0x18;
inline constexpr std::uint32_t kSBlkV1HeaderSize = 0x3c;
inline constexpr std::uint32_t kSBlkV1DescriptorSize = 0x0c;
inline constexpr std::uint32_t kSBlkV1ItemSize = 0x28;

struct SBlkLimits {
    std::uint64_t max_input_bytes = 0;
    std::uint64_t max_descriptors = 0;
    std::uint64_t max_items = 0;
    std::uint64_t max_secondary_bytes = 0;
};

struct SBlkRecord {
    // Relative to the beginning of the complete wrapper input.
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct SBlkDescriptor {
    std::uint32_t type = 0;
    std::uint32_t packed_count = 0;
    std::uint32_t item_count = 0;
    std::uint32_t flags = 0;
    // Relative to item_bytes and to item_data_offset in the original SBlk record.
    std::uint32_t data_offset = 0;
};

struct SBlkBundleV3 {
    std::uint32_t input_size = 0;
    SBlkRecord sblk_record;
    SBlkRecord secondary_record;
    std::uint32_t opaque_a = 0;
    std::uint32_t opaque_b = 0;
    std::uint32_t descriptor_table_offset = 0;
    std::uint32_t descriptor_table_end = 0;
    // Relative to the beginning of the original SBlk record.
    std::uint32_t item_data_offset = 0;
    std::vector<SBlkDescriptor> descriptors;
    std::vector<std::byte> item_bytes;
    std::vector<std::byte> secondary_bytes;
};

class SBlkError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The input must contain exactly one complete V3 two-record bundle.
// The returned report owns all copied item and secondary data.
[[nodiscard]] SBlkBundleV3 parse_sblk_bundle_v3(
    std::span<const std::byte> bytes,
    SBlkLimits limits);

} // namespace openrc
