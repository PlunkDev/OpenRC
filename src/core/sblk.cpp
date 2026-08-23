#include "openrc/sblk.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace openrc {
namespace {

constexpr std::uint32_t kSBlkV1Version = 1;
constexpr std::uint32_t kSBlkV1Field08 = 4;
constexpr std::uint32_t kSBlkV1Tag = 0x00574144;

[[noreturn]] void fail(const std::string& message) {
    throw SBlkError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) {
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

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* description) {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left * right;
}

[[nodiscard]] bool has_sblk_magic(const std::span<const std::byte> bytes) {
    return bytes.size() >= 4U &&
        byte_value(bytes[0]) == static_cast<std::uint8_t>('S') &&
        byte_value(bytes[1]) == static_cast<std::uint8_t>('B') &&
        byte_value(bytes[2]) == static_cast<std::uint8_t>('l') &&
        byte_value(bytes[3]) == static_cast<std::uint8_t>('k');
}

} // namespace

SBlkBundleV3 parse_sblk_bundle_v3(
    const std::span<const std::byte> bytes,
    const SBlkLimits limits) {
    const auto input_size = static_cast<std::uint64_t>(bytes.size());
    if (input_size > limits.max_input_bytes) {
        fail("The SBlk bundle exceeds the caller's input-size limit");
    }
    if (input_size > std::numeric_limits<std::uint32_t>::max()) {
        fail("The SBlk bundle exceeds its 32-bit address space");
    }
    if (bytes.size() < 8U) {
        fail("The input is too small to contain an SBlk bundle prefix");
    }
    if (read_le32(bytes, 0U) != kSBlkBundleV3Version) {
        fail("The SBlk bundle has an unsupported wrapper version");
    }
    if (read_le32(bytes, 4U) != kSBlkBundleV3RecordCount) {
        fail("The SBlk bundle does not contain exactly two records");
    }
    if (bytes.size() < kSBlkBundleV3HeaderSize) {
        fail("The input is too small to contain the SBlk bundle record table");
    }

    const SBlkRecord sblk_record{
        read_le32(bytes, 8U),
        read_le32(bytes, 12U),
    };
    const SBlkRecord secondary_record{
        read_le32(bytes, 16U),
        read_le32(bytes, 20U),
    };

    if (sblk_record.offset != kSBlkBundleV3HeaderSize) {
        fail("The SBlk record does not begin immediately after the wrapper header");
    }
    const auto sblk_end = checked_add(
        sblk_record.offset,
        sblk_record.size,
        "the end of the SBlk record");
    if (sblk_end > input_size) {
        fail("The SBlk record exceeds the bundle boundary");
    }
    if (secondary_record.offset != sblk_end) {
        fail("The secondary record is not contiguous with the SBlk record");
    }
    const auto secondary_end = checked_add(
        secondary_record.offset,
        secondary_record.size,
        "the end of the SBlk secondary record");
    if (secondary_end != input_size) {
        fail("The secondary record does not end exactly at the bundle boundary");
    }
    if (static_cast<std::uint64_t>(secondary_record.size) >
        limits.max_secondary_bytes) {
        fail("The SBlk secondary record exceeds the caller's size limit");
    }

    const auto sblk = bytes.subspan(sblk_record.offset, sblk_record.size);
    if (sblk.size() < kSBlkV1HeaderSize) {
        fail("The SBlk record is smaller than its fixed header");
    }
    if (!has_sblk_magic(sblk)) {
        fail("The first bundle record does not have an SBlk signature");
    }
    if (read_le32(sblk, 4U) != kSBlkV1Version ||
        read_le32(sblk, 8U) != kSBlkV1Field08 ||
        read_le32(sblk, 12U) != kSBlkV1Tag) {
        fail("The SBlk record has unsupported fixed header fields");
    }
    if (read_le32(sblk, 16U) != 0U || read_le32(sblk, 36U) != 0U ||
        read_le32(sblk, 48U) != 0U || read_le32(sblk, 52U) != 0U ||
        read_le32(sblk, 56U) != 0U) {
        fail("The SBlk record has non-zero reserved header fields");
    }

    const auto descriptor_table_offset = read_le32(sblk, 28U);
    const auto descriptor_table_end = read_le32(sblk, 32U);
    if (descriptor_table_offset != kSBlkV1HeaderSize) {
        fail("The SBlk descriptor table does not begin after the fixed header");
    }
    if (descriptor_table_end < descriptor_table_offset ||
        descriptor_table_end > sblk.size()) {
        fail("The SBlk descriptor table boundary lies outside the record");
    }
    const auto descriptor_bytes = static_cast<std::uint64_t>(
        descriptor_table_end - descriptor_table_offset);
    if (descriptor_bytes % kSBlkV1DescriptorSize != 0U) {
        fail("The SBlk descriptor table size is not divisible by 12");
    }
    const auto descriptor_count = descriptor_bytes / kSBlkV1DescriptorSize;
    if (descriptor_count > limits.max_descriptors) {
        fail("The SBlk descriptor table exceeds the caller's count limit");
    }

    if (read_le32(sblk, 40U) != secondary_record.size ||
        read_le32(sblk, 44U) != secondary_record.size) {
        fail("The SBlk header copies do not match the secondary record size");
    }

    SBlkBundleV3 result;
    result.input_size = static_cast<std::uint32_t>(input_size);
    result.sblk_record = sblk_record;
    result.secondary_record = secondary_record;
    result.opaque_a = read_le32(sblk, 20U);
    result.opaque_b = read_le32(sblk, 24U);
    result.descriptor_table_offset = descriptor_table_offset;
    result.descriptor_table_end = descriptor_table_end;
    result.item_data_offset = descriptor_table_end;

    if (descriptor_count > result.descriptors.max_size()) {
        fail("The SBlk descriptor count exceeds the host container limit");
    }
    result.descriptors.reserve(static_cast<std::size_t>(descriptor_count));

    const auto item_region_bytes =
        static_cast<std::uint64_t>(sblk.size() - descriptor_table_end);
    std::uint64_t expected_data_offset = 0;
    std::uint64_t total_items = 0;
    for (std::uint64_t index = 0; index < descriptor_count; ++index) {
        const auto relative_entry_offset = checked_multiply(
            index,
            kSBlkV1DescriptorSize,
            "an SBlk descriptor offset");
        const auto entry_offset = checked_add(
            descriptor_table_offset,
            relative_entry_offset,
            "an SBlk descriptor address");
        const auto host_entry_offset = static_cast<std::size_t>(entry_offset);
        const auto type = read_le32(sblk, host_entry_offset);
        const auto packed_count = read_le32(sblk, host_entry_offset + 4U);
        const auto item_count = packed_count & 0xffffU;
        const auto flags = packed_count & 0xffff0000U;
        const auto data_offset = read_le32(sblk, host_entry_offset + 8U);

        if (item_count == 0U) {
            fail("An SBlk descriptor has a zero item count");
        }
        if (static_cast<std::uint64_t>(data_offset) != expected_data_offset) {
            fail("The SBlk descriptor item ranges are not exactly contiguous");
        }
        const auto descriptor_item_bytes = checked_multiply(
            item_count,
            kSBlkV1ItemSize,
            "an SBlk descriptor item range");
        expected_data_offset = checked_add(
            expected_data_offset,
            descriptor_item_bytes,
            "the SBlk item-data end");
        if (expected_data_offset > item_region_bytes) {
            fail("An SBlk descriptor item range exceeds the SBlk record");
        }
        total_items = checked_add(
            total_items,
            item_count,
            "the total SBlk item count");
        if (total_items > limits.max_items) {
            fail("The SBlk item count exceeds the caller's limit");
        }

        result.descriptors.push_back(SBlkDescriptor{
            type,
            packed_count,
            item_count,
            flags,
            data_offset,
        });
    }

    if (expected_data_offset != item_region_bytes) {
        fail("The SBlk descriptor item ranges do not exactly fill the record");
    }

    if (item_region_bytes > result.item_bytes.max_size()) {
        fail("The SBlk item data exceeds the host container limit");
    }
    const auto item_bytes = sblk.subspan(
        descriptor_table_end,
        static_cast<std::size_t>(item_region_bytes));
    result.item_bytes.assign(item_bytes.begin(), item_bytes.end());

    if (static_cast<std::uint64_t>(secondary_record.size) >
        result.secondary_bytes.max_size()) {
        fail("The SBlk secondary data exceeds the host container limit");
    }
    const auto secondary_bytes =
        bytes.subspan(secondary_record.offset, secondary_record.size);
    result.secondary_bytes.assign(secondary_bytes.begin(), secondary_bytes.end());
    return result;
}

} // namespace openrc
