#include "openrc/wad_bundle.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kInitialOffset = 0xc0;
constexpr std::size_t kInitialSize = 0x10;
constexpr std::size_t kNestedSlot = 0;
constexpr std::size_t kEmptySlot = 1;
constexpr std::size_t kElfSlot = 2;
constexpr std::size_t kNestedOffset = 0x100;
constexpr std::size_t kNestedSize = 0x10;
constexpr std::size_t kElfOffset = 0x140;
constexpr std::size_t kElfSize = 52;
constexpr std::size_t kBundleSize = kElfOffset + kElfSize;

void write_le16(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_le32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void write_empty_wad(std::vector<std::byte>& bytes, const std::size_t offset) {
    bytes[offset] = std::byte{'W'};
    bytes[offset + 1U] = std::byte{'A'};
    bytes[offset + 2U] = std::byte{'D'};
    write_le32(bytes, offset + 3U, static_cast<std::uint32_t>(kNestedSize));
}

void write_minimal_elf(std::vector<std::byte>& bytes, const std::size_t offset) {
    bytes[offset] = std::byte{0x7f};
    bytes[offset + 1U] = std::byte{'E'};
    bytes[offset + 2U] = std::byte{'L'};
    bytes[offset + 3U] = std::byte{'F'};
    bytes[offset + 4U] = std::byte{1};
    bytes[offset + 5U] = std::byte{1};
    bytes[offset + 6U] = std::byte{1};
    write_le16(bytes, offset + 16U, 0xff80U);
    write_le16(bytes, offset + 18U, 8U);
    write_le32(bytes, offset + 20U, 1U);
    write_le16(bytes, offset + 40U, static_cast<std::uint16_t>(kElfSize));
    write_le16(bytes, offset + 42U, 32U);
    write_le16(bytes, offset + 46U, 40U);
}

void write_slot(
    std::vector<std::byte>& bytes,
    const std::size_t slot,
    const std::uint32_t offset,
    const std::uint32_t size) {
    const auto pair_offset = 8U + slot * 8U;
    write_le32(bytes, pair_offset, offset);
    write_le32(bytes, pair_offset + 4U, size);
}

std::vector<std::byte> make_valid_bundle() {
    std::vector<std::byte> bytes(kBundleSize, std::byte{0});
    write_le32(bytes, 0U, openrc::kWadBundleV1HeaderSize);
    write_le32(bytes, 4U, static_cast<std::uint32_t>(kInitialSize));
    write_slot(
        bytes,
        kNestedSlot,
        static_cast<std::uint32_t>(kNestedOffset),
        static_cast<std::uint32_t>(kNestedSize));
    write_slot(
        bytes,
        kElfSlot,
        static_cast<std::uint32_t>(kElfOffset),
        static_cast<std::uint32_t>(kElfSize));
    write_empty_wad(bytes, kInitialOffset);
    write_empty_wad(bytes, kNestedOffset);
    write_minimal_elf(bytes, kElfOffset);
    return bytes;
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_rejected(const std::vector<std::byte>& bytes, const std::string& message) {
    bool rejected = false;
    try {
        (void)openrc::parse_wad_bundle_v1(bytes);
    } catch (const openrc::WadBundleError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void test_valid_bundle() {
    const auto report = openrc::parse_wad_bundle_v1(make_valid_bundle());
    expect(report.decoded_size == kBundleSize, "decoded bundle size was not reported");
    expect(
        report.header_size == openrc::kWadBundleV1HeaderSize,
        "bundle header size was not reported");
    expect(report.initial_size == kInitialSize, "initial record size was not reported");
    expect(
        report.initial_record.kind == openrc::WadBundleRecordKind::nested_wad,
        "implicit record was not classified as a nested WAD");
    expect(report.initial_record.offset == kInitialOffset, "implicit record offset is wrong");
    expect(report.initial_record.size == kInitialSize, "implicit record size is wrong");
    expect(
        report.slots[kNestedSlot].kind == openrc::WadBundleRecordKind::nested_wad,
        "explicit nested WAD was not classified");
    expect(report.slots[kNestedSlot].offset == kNestedOffset, "nested WAD offset is wrong");
    expect(report.slots[kNestedSlot].size == kNestedSize, "nested WAD size is wrong");
    expect(
        report.slots[kEmptySlot].kind == openrc::WadBundleRecordKind::empty,
        "zero pair was not classified as an empty slot");
    expect(report.slots[kEmptySlot].offset == 0, "empty slot has a non-zero offset");
    expect(report.slots[kEmptySlot].size == 0, "empty slot has a non-zero size");
    expect(
        report.slots[kElfSlot].kind == openrc::WadBundleRecordKind::elf,
        "ELF record was not classified");
    expect(report.slots[kElfSlot].offset == kElfOffset, "ELF record offset is wrong");
    expect(report.slots[kElfSlot].size == kElfSize, "ELF record size is wrong");
    expect(report.slots.size() == openrc::kWadBundleV1SlotCount, "slot count is wrong");
}

void test_header_and_bounds_rejections() {
    auto bytes = make_valid_bundle();
    bytes.resize(openrc::kWadBundleV1HeaderSize - 1U);
    expect_rejected(bytes, "truncated WadBundleV1 header was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, 0U, openrc::kWadBundleV1HeaderSize - 8U);
    expect_rejected(bytes, "unsupported WadBundleV1 header size was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, 4U, static_cast<std::uint32_t>(bytes.size()));
    expect_rejected(bytes, "out-of-bounds implicit record was accepted");

    bytes = make_valid_bundle();
    write_slot(bytes, kElfSlot, static_cast<std::uint32_t>(kElfOffset), 0x100U);
    expect_rejected(bytes, "out-of-bounds explicit record was accepted");

    bytes = make_valid_bundle();
    write_slot(bytes, kEmptySlot, 0U, 1U);
    expect_rejected(bytes, "partially empty offset/size pair was accepted");
}

void test_layout_rejections() {
    auto bytes = make_valid_bundle();
    write_slot(
        bytes,
        kNestedSlot,
        static_cast<std::uint32_t>(kNestedOffset + 1U),
        static_cast<std::uint32_t>(kNestedSize));
    expect_rejected(bytes, "unaligned record was accepted");

    bytes = make_valid_bundle();
    write_slot(
        bytes,
        kElfSlot,
        static_cast<std::uint32_t>(kNestedOffset),
        static_cast<std::uint32_t>(kElfSize));
    expect_rejected(bytes, "overlapping or out-of-order record was accepted");

    bytes = make_valid_bundle();
    bytes[kInitialOffset + kInitialSize] = std::byte{1};
    expect_rejected(bytes, "non-zero alignment gap was accepted");

    bytes = make_valid_bundle();
    bytes.push_back(std::byte{0});
    expect_rejected(bytes, "bytes after the final active record were accepted");
}

void test_record_rejections() {
    auto bytes = make_valid_bundle();
    bytes[kInitialOffset] = std::byte{'B'};
    expect_rejected(bytes, "bad implicit WadV1 magic was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, kNestedOffset + 3U, static_cast<std::uint32_t>(kNestedSize - 1U));
    expect_rejected(bytes, "nested WadV1 size mismatch was accepted");

    bytes = make_valid_bundle();
    write_slot(
        bytes,
        kElfSlot,
        static_cast<std::uint32_t>(kElfOffset),
        static_cast<std::uint32_t>(kElfSize - 1U));
    bytes.resize(kBundleSize - 1U);
    expect_rejected(bytes, "truncated ELF record was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, kElfOffset + 28U, static_cast<std::uint32_t>(kElfSize - 1U));
    write_le16(bytes, kElfOffset + 44U, 1U);
    expect_rejected(bytes, "out-of-bounds ELF program table was accepted");
}

} // namespace

int main() {
    try {
        test_valid_bundle();
        test_header_and_bounds_rejections();
        test_layout_rejections();
        test_record_rejections();
        std::cout << "OpenRC WadBundleV1 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC WadBundleV1 tests failed: " << error.what() << '\n';
        return 1;
    }
}
