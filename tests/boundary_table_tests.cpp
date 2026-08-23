#include "openrc/boundary_table.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::array<std::uint32_t, openrc::kBoundaryTableBoundaryCount>
    kValidBoundaries{0x20U, 0x30U, 0x50U, 0x80U, 0x90U, 0xc0U, 0xe0U, 0x100U};
constexpr openrc::BoundaryTableLimits kValidLimits{0x100U, 0x30U};

void write_le32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

std::vector<std::byte> make_valid_table() {
    std::vector<std::byte> bytes(kValidBoundaries.back(), std::byte{0});
    for (std::size_t index = 0; index < kValidBoundaries.size(); ++index) {
        write_le32(bytes, index * sizeof(std::uint32_t), kValidBoundaries[index]);
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
    const openrc::BoundaryTableLimits limits,
    const std::string& message) {
    bool rejected = false;
    try {
        (void)openrc::parse_boundary_table(bytes, limits);
    } catch (const openrc::BoundaryTableError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void test_valid_table() {
    const auto report = openrc::parse_boundary_table(make_valid_table(), kValidLimits);
    expect(report.input_size == kValidBoundaries.back(), "input size was not reported");
    expect(report.boundaries == kValidBoundaries, "boundaries were not copied");
    expect(
        report.regions.size() == openrc::kBoundaryTableRegionCount,
        "region count is wrong");

    for (std::size_t index = 0; index < report.regions.size(); ++index) {
        const auto expected_offset = kValidBoundaries[index];
        const auto expected_size =
            kValidBoundaries[index + 1U] - kValidBoundaries[index];
        expect(report.regions[index].offset == expected_offset, "region offset is wrong");
        expect(report.regions[index].size == expected_size, "region size is wrong");
    }
}

void test_header_rejections() {
    auto bytes = make_valid_table();
    bytes.resize(openrc::kBoundaryTableHeaderSize - 1U);
    expect_rejected(bytes, kValidLimits, "a short boundary-table header was accepted");

    bytes = make_valid_table();
    write_le32(bytes, 0U, 0x10U);
    expect_rejected(bytes, kValidLimits, "a first boundary other than 0x20 was accepted");
}

void test_boundary_rejections() {
    auto bytes = make_valid_table();
    write_le32(bytes, 2U * sizeof(std::uint32_t), 0x51U);
    expect_rejected(bytes, kValidLimits, "a non-aligned boundary was accepted");

    bytes = make_valid_table();
    write_le32(bytes, 3U * sizeof(std::uint32_t), kValidBoundaries[2]);
    expect_rejected(bytes, kValidLimits, "a duplicate boundary was accepted");

    bytes = make_valid_table();
    write_le32(bytes, 3U * sizeof(std::uint32_t), 0x40U);
    expect_rejected(bytes, kValidLimits, "descending boundaries were accepted");

    bytes = make_valid_table();
    write_le32(bytes, 6U * sizeof(std::uint32_t), 0x110U);
    expect_rejected(bytes, kValidLimits, "an out-of-bounds boundary was accepted");

    bytes = make_valid_table();
    write_le32(bytes, 7U * sizeof(std::uint32_t), 0xf0U);
    expect_rejected(bytes, kValidLimits, "a final boundary different from input size was accepted");
}

void test_caller_limits() {
    const auto bytes = make_valid_table();
    expect_rejected(
        bytes,
        openrc::BoundaryTableLimits{0xffU, 0x30U},
        "an input larger than the caller's cap was accepted");
    expect_rejected(
        bytes,
        openrc::BoundaryTableLimits{0x100U, 0x2fU},
        "a region larger than the caller's cap was accepted");
}

} // namespace

int main() {
    try {
        test_valid_table();
        test_header_rejections();
        test_boundary_rejections();
        test_caller_limits();
        std::cout << "OpenRC boundary-table tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC boundary-table tests failed: " << error.what() << '\n';
        return 1;
    }
}
