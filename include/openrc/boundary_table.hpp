#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace openrc {

inline constexpr std::size_t kBoundaryTableBoundaryCount = 8;
inline constexpr std::size_t kBoundaryTableRegionCount =
    kBoundaryTableBoundaryCount - 1U;
inline constexpr std::uint32_t kBoundaryTableHeaderSize = 0x20;
inline constexpr std::uint32_t kBoundaryTableAlignment = 0x10;

struct BoundaryTableLimits {
    std::uint64_t max_input_bytes = 0;
    std::uint64_t max_region_bytes = 0;
};

struct BoundaryTableRegion {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct BoundaryTable {
    std::uint32_t input_size = 0;
    std::array<std::uint32_t, kBoundaryTableBoundaryCount> boundaries{};
    std::array<BoundaryTableRegion, kBoundaryTableRegionCount> regions{};
};

class BoundaryTableError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The input must contain exactly one complete boundary-table payload.
// The returned report owns all of its data and does not refer back to input.
[[nodiscard]] BoundaryTable parse_boundary_table(
    std::span<const std::byte> bytes,
    BoundaryTableLimits limits);

} // namespace openrc
