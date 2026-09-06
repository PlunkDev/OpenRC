#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc::game {

// Neutral inclusive cell coordinates. Conversion from world coordinates and
// source packed bounds is a separate, source-qualified numerical contract.
struct SpatialCellRectangleV1 {
  std::uint16_t min_x = 0U;
  std::uint16_t min_y = 0U;
  std::uint16_t max_x = 0U;
  std::uint16_t max_y = 0U;
  [[nodiscard]] bool operator==(const SpatialCellRectangleV1 &) const = default;
};

struct OrderedSpatialIndexLayoutV1 {
  std::uint32_t columns = 0U;
  std::uint32_t rows = 0U;
  // One allocation bit per block of sixteen u16 members. Multiple of 32.
  std::uint32_t pool_blocks = 0U;
  [[nodiscard]] bool
  operator==(const OrderedSpatialIndexLayoutV1 &) const = default;
};

struct OrderedSpatialCellV1 {
  std::uint16_t first_block = 0U;
  std::uint8_t member_count = 0U;
  std::uint8_t capacity_blocks = 0U;
  [[nodiscard]] bool operator==(const OrderedSpatialCellV1 &) const = default;
};

struct OrderedSpatialMembershipV1 {
  // A source-compiled live-order token, NOT EntityIdV1::slot or authored ID.
  // The level owner must bind it explicitly to the corresponding live entity.
  std::uint16_t member = 0U;
  std::optional<SpatialCellRectangleV1> rectangle;
  [[nodiscard]] bool
  operator==(const OrderedSpatialMembershipV1 &) const = default;
};

struct OrderedSpatialIndexSnapshotV1 {
  OrderedSpatialIndexLayoutV1 layout;
  std::uint64_t revision = 0U;
  // Row-major cells. Pool includes stale/inactive bytes: freeing never clears
  // them, and relocation copies whole blocks, not only active members.
  std::vector<OrderedSpatialCellV1> cells;
  std::vector<std::uint16_t> member_pool;
  std::vector<std::uint32_t> allocation_words;
  // Strict member-token order, independent of the order within cell lists.
  std::vector<OrderedSpatialMembershipV1> memberships;
  [[nodiscard]] bool
  operator==(const OrderedSpatialIndexSnapshotV1 &) const = default;
};

struct OrderedSpatialIndexLimitsV1 {
  std::uint32_t max_cells = 0U;
  std::uint32_t max_pool_blocks = 0U;
  std::uint32_t max_memberships = 0U;
};

class OrderedSpatialIndexError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Explicit cleared initial image, never a claim about live source memory.
// A compiler must establish the original clear/extent before using it.
[[nodiscard]] OrderedSpatialIndexSnapshotV1
make_cleared_ordered_spatial_index_v1(OrderedSpatialIndexLayoutV1 layout,
                                      OrderedSpatialIndexLimitsV1 limits);

class OrderedSpatialIndexV1 final {
public:
  // Validate all extents, ownership, active members and bounds before copying.
  // Set allocation bits not owned by a cell are preserved as opaque reserved
  // blocks. They are never silently made available by restoration.
  explicit OrderedSpatialIndexV1(const OrderedSpatialIndexSnapshotV1 &initial,
                                 OrderedSpatialIndexLimitsV1 limits);

  [[nodiscard]] OrderedSpatialIndexSnapshotV1 snapshot() const;
  [[nodiscard]] std::uint64_t revision() const noexcept;
  [[nodiscard]] std::span<const std::uint16_t>
  members_at(std::uint32_t x, std::uint32_t y) const;
  // Remove old-only cells (X inner, Y outer), then append to new-only cells.
  // Removal replaces by the last member. Growth allocates before freeing;
  // shrink frees before allocating and halves exactly once. An absent new
  // rectangle removes membership, but keeps the token's lifetime record.
  // A successful call advances the revision once, including unchanged bounds.
  // Any malformed state, exhausted pool or unsupported domain leaves all
  // storage unchanged. No alternate allocator or wrapped count is substituted.
  void set_membership(std::uint16_t member,
                      std::optional<SpatialCellRectangleV1> rectangle,
                      std::uint64_t expected_revision);

private:
  OrderedSpatialIndexSnapshotV1 state_;
  OrderedSpatialIndexLimitsV1 limits_;
};

} // namespace openrc::game
