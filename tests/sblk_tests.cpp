#include "openrc/sblk.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kDescriptorCount = 1;
constexpr std::size_t kDescriptorTableEnd =
    openrc::kSBlkV1HeaderSize +
    kDescriptorCount * openrc::kSBlkV1DescriptorSize;
constexpr std::size_t kSBlkRecordSize =
    kDescriptorTableEnd + openrc::kSBlkV1ItemSize;
constexpr std::size_t kSBlkRecordOffset = openrc::kSBlkBundleV3HeaderSize;
constexpr std::size_t kSecondaryOffset = kSBlkRecordOffset + kSBlkRecordSize;
constexpr std::size_t kSecondarySize = 0x10;
constexpr std::size_t kBundleSize = kSecondaryOffset + kSecondarySize;
constexpr openrc::SBlkLimits kValidLimits{
    kBundleSize,
    kDescriptorCount,
    kDescriptorCount,
    kSecondarySize,
};

void write_le32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

std::vector<std::byte> make_valid_bundle(const std::size_t descriptor_count = 1U) {
    const auto descriptor_table_end =
        static_cast<std::size_t>(openrc::kSBlkV1HeaderSize) +
        descriptor_count * openrc::kSBlkV1DescriptorSize;
    const auto sblk_size =
        descriptor_table_end + descriptor_count * openrc::kSBlkV1ItemSize;
    const auto secondary_offset =
        static_cast<std::size_t>(openrc::kSBlkBundleV3HeaderSize) + sblk_size;
    const auto bundle_size = secondary_offset + kSecondarySize;
    std::vector<std::byte> bytes(bundle_size, std::byte{0});

    write_le32(bytes, 0U, openrc::kSBlkBundleV3Version);
    write_le32(bytes, 4U, openrc::kSBlkBundleV3RecordCount);
    write_le32(bytes, 8U, openrc::kSBlkBundleV3HeaderSize);
    write_le32(bytes, 12U, static_cast<std::uint32_t>(sblk_size));
    write_le32(bytes, 16U, static_cast<std::uint32_t>(secondary_offset));
    write_le32(bytes, 20U, static_cast<std::uint32_t>(kSecondarySize));

    const auto sblk = static_cast<std::size_t>(openrc::kSBlkBundleV3HeaderSize);
    bytes[sblk] = std::byte{'S'};
    bytes[sblk + 1U] = std::byte{'B'};
    bytes[sblk + 2U] = std::byte{'l'};
    bytes[sblk + 3U] = std::byte{'k'};
    write_le32(bytes, sblk + 4U, 1U);
    write_le32(bytes, sblk + 8U, 4U);
    write_le32(bytes, sblk + 12U, 0x00574144U);
    write_le32(bytes, sblk + 20U, 0x11223344U);
    write_le32(bytes, sblk + 24U, 0x55667788U);
    write_le32(bytes, sblk + 28U, openrc::kSBlkV1HeaderSize);
    write_le32(
        bytes,
        sblk + 32U,
        static_cast<std::uint32_t>(descriptor_table_end));
    write_le32(bytes, sblk + 40U, static_cast<std::uint32_t>(kSecondarySize));
    write_le32(bytes, sblk + 44U, static_cast<std::uint32_t>(kSecondarySize));

    for (std::size_t index = 0; index < descriptor_count; ++index) {
        const auto descriptor =
            sblk + openrc::kSBlkV1HeaderSize +
            index * openrc::kSBlkV1DescriptorSize;
        write_le32(
            bytes,
            descriptor,
            static_cast<std::uint32_t>(0x89abcdefU + index));
        const auto flags = index == 0U ? 0xbeef0000U : 0xcafe0000U;
        write_le32(bytes, descriptor + 4U, flags | 1U);
        write_le32(
            bytes,
            descriptor + 8U,
            static_cast<std::uint32_t>(index * openrc::kSBlkV1ItemSize));
    }

    const auto item_data = sblk + descriptor_table_end;
    for (std::size_t index = 0;
         index < descriptor_count * openrc::kSBlkV1ItemSize;
         ++index) {
        bytes[item_data + index] =
            static_cast<std::byte>(0x40U + (index & 0x3fU));
    }
    for (std::size_t index = 0; index < kSecondarySize; ++index) {
        bytes[secondary_offset + index] =
            static_cast<std::byte>(0xa0U + index);
    }
    return bytes;
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_rejected(
    const std::vector<std::byte>& bytes,
    const openrc::SBlkLimits limits,
    const std::string& message) {
    bool rejected = false;
    try {
        (void)openrc::parse_sblk_bundle_v3(bytes, limits);
    } catch (const openrc::SBlkError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void test_valid_bundle_and_ownership() {
    auto bytes = make_valid_bundle();
    const auto report = openrc::parse_sblk_bundle_v3(bytes, kValidLimits);

    expect(report.input_size == kBundleSize, "input size was not reported");
    expect(report.sblk_record.offset == kSBlkRecordOffset, "SBlk offset is wrong");
    expect(report.sblk_record.size == kSBlkRecordSize, "SBlk size is wrong");
    expect(
        report.secondary_record.offset == kSecondaryOffset,
        "secondary offset is wrong");
    expect(
        report.secondary_record.size == kSecondarySize,
        "secondary size is wrong");
    expect(report.opaque_a == 0x11223344U, "opaque field A changed");
    expect(report.opaque_b == 0x55667788U, "opaque field B changed");
    expect(
        report.descriptor_table_offset == openrc::kSBlkV1HeaderSize,
        "descriptor-table offset is wrong");
    expect(
        report.descriptor_table_end == kDescriptorTableEnd,
        "descriptor-table end is wrong");
    expect(report.item_data_offset == kDescriptorTableEnd, "item-data offset is wrong");
    expect(report.descriptors.size() == 1U, "descriptor count is wrong");

    const auto& descriptor = report.descriptors.front();
    expect(descriptor.type == 0x89abcdefU, "unknown descriptor type was not preserved");
    expect(descriptor.packed_count == 0xbeef0001U, "packed count changed");
    expect(descriptor.item_count == 1U, "low item count is wrong");
    expect(descriptor.flags == 0xbeef0000U, "high flags were not preserved");
    expect(descriptor.data_offset == 0U, "first data offset is wrong");

    expect(
        report.item_bytes.size() == openrc::kSBlkV1ItemSize,
        "owned item-data size is wrong");
    expect(report.item_bytes.front() == std::byte{0x40}, "owned item data changed");
    expect(report.item_bytes.back() == std::byte{0x67}, "owned item tail changed");
    expect(report.secondary_bytes.size() == kSecondarySize, "owned secondary size is wrong");
    expect(report.secondary_bytes.front() == std::byte{0xa0}, "secondary data changed");
    expect(report.secondary_bytes.back() == std::byte{0xaf}, "secondary tail changed");

    std::fill(bytes.begin(), bytes.end(), std::byte{0});
    expect(report.item_bytes.front() == std::byte{0x40}, "item data borrowed its input");
    expect(
        report.secondary_bytes.front() == std::byte{0xa0},
        "secondary data borrowed its input");
}

void test_wrapper_rejections() {
    std::vector<std::byte> short_prefix(7U, std::byte{0});
    expect_rejected(short_prefix, kValidLimits, "a truncated wrapper prefix was accepted");

    auto bytes = make_valid_bundle();
    bytes.resize(openrc::kSBlkBundleV3HeaderSize - 1U);
    expect_rejected(bytes, kValidLimits, "a truncated record table was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, 0U, 2U);
    expect_rejected(bytes, kValidLimits, "an unsupported wrapper version was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, 4U, 1U);
    expect_rejected(bytes, kValidLimits, "a wrapper record count other than two was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, 8U, openrc::kSBlkBundleV3HeaderSize + 1U);
    expect_rejected(bytes, kValidLimits, "a gap before the SBlk record was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, 16U, static_cast<std::uint32_t>(kSecondaryOffset + 1U));
    expect_rejected(bytes, kValidLimits, "a gap before the secondary record was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, 16U, static_cast<std::uint32_t>(kSecondaryOffset - 1U));
    expect_rejected(bytes, kValidLimits, "overlapping records were accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, 20U, 0x100U);
    expect_rejected(bytes, kValidLimits, "an out-of-bounds secondary record was accepted");

    bytes = make_valid_bundle();
    bytes.push_back(std::byte{0});
    const openrc::SBlkLimits trailing_limits{
        kBundleSize + 1U,
        kDescriptorCount,
        kDescriptorCount,
        kSecondarySize,
    };
    expect_rejected(bytes, trailing_limits, "bytes after the secondary record were accepted");
}

void test_sblk_header_rejections() {
    auto bytes = make_valid_bundle();
    constexpr auto short_sblk_size = openrc::kSBlkV1HeaderSize - 1U;
    constexpr auto short_secondary_offset =
        openrc::kSBlkBundleV3HeaderSize + short_sblk_size;
    bytes.resize(short_secondary_offset + kSecondarySize);
    write_le32(bytes, 12U, short_sblk_size);
    write_le32(bytes, 16U, short_secondary_offset);
    write_le32(bytes, 20U, kSecondarySize);
    expect_rejected(bytes, kValidLimits, "an SBlk record shorter than its header was accepted");

    bytes = make_valid_bundle();
    bytes[kSBlkRecordOffset] = std::byte{'X'};
    expect_rejected(bytes, kValidLimits, "bad SBlk magic was accepted");

    for (const auto offset : {4U, 8U, 12U}) {
        bytes = make_valid_bundle();
        write_le32(bytes, kSBlkRecordOffset + offset, 0U);
        expect_rejected(bytes, kValidLimits, "a bad fixed SBlk field was accepted");
    }

    for (const auto offset : {16U, 36U, 48U, 52U, 56U}) {
        bytes = make_valid_bundle();
        write_le32(bytes, kSBlkRecordOffset + offset, 1U);
        expect_rejected(bytes, kValidLimits, "a non-zero reserved SBlk field was accepted");
    }

    bytes = make_valid_bundle();
    write_le32(bytes, kSBlkRecordOffset + 40U, kSecondarySize - 1U);
    expect_rejected(bytes, kValidLimits, "the first wrong secondary-size copy was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, kSBlkRecordOffset + 44U, kSecondarySize - 1U);
    expect_rejected(bytes, kValidLimits, "the second wrong secondary-size copy was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, kSBlkRecordOffset + 28U, openrc::kSBlkV1HeaderSize + 4U);
    expect_rejected(bytes, kValidLimits, "a descriptor table not starting at 0x3c was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, kSBlkRecordOffset + 32U, openrc::kSBlkV1HeaderSize - 1U);
    expect_rejected(bytes, kValidLimits, "a descending descriptor boundary was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, kSBlkRecordOffset + 32U, kDescriptorTableEnd + 1U);
    expect_rejected(bytes, kValidLimits, "a non-divisible descriptor table was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, kSBlkRecordOffset + 32U, kSBlkRecordSize + 1U);
    expect_rejected(bytes, kValidLimits, "an out-of-bounds descriptor table was accepted");
}

void test_descriptor_rejections() {
    constexpr auto descriptor =
        kSBlkRecordOffset + static_cast<std::size_t>(openrc::kSBlkV1HeaderSize);

    auto bytes = make_valid_bundle();
    write_le32(bytes, descriptor + 4U, 0xbeef0000U);
    expect_rejected(bytes, kValidLimits, "a zero descriptor item count was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, descriptor + 8U, 1U);
    expect_rejected(bytes, kValidLimits, "a first item offset other than zero was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, descriptor + 4U, 0xbeef0002U);
    expect_rejected(bytes, kValidLimits, "an item range beyond the record was accepted");

    bytes = make_valid_bundle(2U);
    const auto second_descriptor =
        kSBlkRecordOffset + static_cast<std::size_t>(openrc::kSBlkV1HeaderSize) +
        openrc::kSBlkV1DescriptorSize;
    const openrc::SBlkLimits two_limits{
        bytes.size(),
        2U,
        2U,
        kSecondarySize,
    };
    write_le32(bytes, second_descriptor + 8U, openrc::kSBlkV1ItemSize + 1U);
    expect_rejected(bytes, two_limits, "a gap between descriptor ranges was accepted");

    bytes = make_valid_bundle(2U);
    write_le32(bytes, second_descriptor + 8U, openrc::kSBlkV1ItemSize - 1U);
    expect_rejected(bytes, two_limits, "overlapping descriptor ranges were accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, kSBlkRecordOffset + 32U, openrc::kSBlkV1HeaderSize);
    expect_rejected(bytes, kValidLimits, "unclaimed bytes after an empty table were accepted");
}

void test_caller_limits() {
    const auto bytes = make_valid_bundle();
    expect_rejected(
        bytes,
        openrc::SBlkLimits{
            kBundleSize - 1U,
            kDescriptorCount,
            kDescriptorCount,
            kSecondarySize,
        },
        "an input larger than the caller's cap was accepted");
    expect_rejected(
        bytes,
        openrc::SBlkLimits{kBundleSize, 0U, kDescriptorCount, kSecondarySize},
        "too many descriptors were accepted");
    expect_rejected(
        bytes,
        openrc::SBlkLimits{kBundleSize, kDescriptorCount, 0U, kSecondarySize},
        "too many items were accepted");
    expect_rejected(
        bytes,
        openrc::SBlkLimits{
            kBundleSize,
            kDescriptorCount,
            kDescriptorCount,
            kSecondarySize - 1U,
        },
        "secondary data larger than the caller's cap was accepted");
}

} // namespace

int main() {
    try {
        test_valid_bundle_and_ownership();
        test_wrapper_rejections();
        test_sblk_header_rejections();
        test_descriptor_rejections();
        test_caller_limits();
        std::cout << "OpenRC SBlk tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC SBlk tests failed: " << error.what() << '\n';
        return 1;
    }
}
