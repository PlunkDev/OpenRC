#include "openrc/boundary_table.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace openrc {
namespace {

static_assert(
    kBoundaryTableHeaderSize ==
        kBoundaryTableBoundaryCount * sizeof(std::uint32_t),
    "The boundary table header must contain exactly eight LE32 boundaries");

[[noreturn]] void fail(const std::string& message) {
    throw BoundaryTableError(message);
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

} // namespace

BoundaryTable parse_boundary_table(
    const std::span<const std::byte> bytes,
    const BoundaryTableLimits limits) {
    const auto input_size = static_cast<std::uint64_t>(bytes.size());
    if (input_size > limits.max_input_bytes) {
        fail("The boundary-table payload exceeds the caller's input-size limit");
    }
    if (input_size > std::numeric_limits<std::uint32_t>::max()) {
        fail("The boundary-table payload exceeds its 32-bit address space");
    }
    if (bytes.size() < kBoundaryTableHeaderSize) {
        fail("The payload is too small to contain eight boundary-table entries");
    }

    BoundaryTable result;
    result.input_size = static_cast<std::uint32_t>(bytes.size());
    for (std::size_t index = 0; index < result.boundaries.size(); ++index) {
        result.boundaries[index] = read_le32(bytes, index * sizeof(std::uint32_t));
    }

    if (result.boundaries.front() != kBoundaryTableHeaderSize) {
        fail("The first boundary-table entry is not the end of the 0x20-byte header");
    }

    for (std::size_t index = 0; index < result.boundaries.size(); ++index) {
        const auto boundary = result.boundaries[index];
        if (boundary % kBoundaryTableAlignment != 0U) {
            fail(
                "Boundary-table entry " + std::to_string(index) +
                " is not aligned to 0x10 bytes");
        }
        if (boundary > result.input_size) {
            fail(
                "Boundary-table entry " + std::to_string(index) +
                " points outside the payload");
        }
        if (index != 0U && boundary <= result.boundaries[index - 1U]) {
            fail("Boundary-table entries are not strictly ascending");
        }
    }

    if (result.boundaries.back() != result.input_size) {
        fail("The final boundary-table entry does not equal the payload size");
    }

    for (std::size_t index = 0; index < result.regions.size(); ++index) {
        const auto offset = result.boundaries[index];
        const auto size = result.boundaries[index + 1U] - offset;
        if (static_cast<std::uint64_t>(size) > limits.max_region_bytes) {
            fail(
                "Boundary-table region " + std::to_string(index) +
                " exceeds the caller's region-size limit");
        }
        result.regions[index] = BoundaryTableRegion{offset, size};
    }

    return result;
}

} // namespace openrc
