#include "openrc/ordered_spatial_index.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace openrc::game {
namespace {

constexpr std::uint32_t kMembersPerBlock = 16U;

[[noreturn]] void fail(const std::string_view message) {
  throw OrderedSpatialIndexError("Ordered spatial index " +
                                 std::string(message));
}

void validate_layout(const OrderedSpatialIndexLayoutV1 layout,
                     const OrderedSpatialIndexLimitsV1 limits) {
  if (limits.max_cells == 0U || limits.max_pool_blocks == 0U ||
      limits.max_memberships == 0U) {
    fail("requires explicit positive limits");
  }
  // This is the established non-wrapping coordinate and signed-block domain,
  // not permission to extend a source allocation past its verified owner.
  if (layout.columns == 0U || layout.columns > 64U || layout.rows == 0U ||
      layout.rows > 64U || layout.columns * layout.rows > limits.max_cells) {
    fail("grid dimensions exceed the supported bounded domain");
  }
  if (layout.pool_blocks == 0U || layout.pool_blocks % 32U != 0U ||
      layout.pool_blocks > 32768U ||
      layout.pool_blocks > limits.max_pool_blocks) {
    fail("pool must contain a bounded whole number of allocation words");
  }
}

void validate_rectangle(const SpatialCellRectangleV1 &rectangle,
                        const OrderedSpatialIndexLayoutV1 layout) {
  if (rectangle.min_x > rectangle.max_x || rectangle.min_y > rectangle.max_y ||
      rectangle.max_x >= layout.columns || rectangle.max_y >= layout.rows) {
    fail("rectangle is inverted or outside the grid");
  }
}

bool contains(const std::optional<SpatialCellRectangleV1> &rectangle,
              const std::uint32_t x, const std::uint32_t y) noexcept {
  return rectangle && x >= rectangle->min_x && x <= rectangle->max_x &&
         y >= rectangle->min_y && y <= rectangle->max_y;
}

bool valid_capacity(const std::uint32_t capacity) noexcept {
  return capacity != 0U && capacity <= 16U &&
         (capacity & (capacity - 1U)) == 0U;
}

bool allocated(const OrderedSpatialIndexSnapshotV1 &state,
               const std::uint32_t block) noexcept {
  return (state.allocation_words[block / 32U] & (1U << (block % 32U))) != 0U;
}

auto find_member(const auto &memberships, const std::uint16_t member) {
  return std::lower_bound(
      memberships.begin(), memberships.end(), member,
      [](const OrderedSpatialMembershipV1 &entry, const std::uint16_t value) {
        return entry.member < value;
      });
}

void validate_snapshot(const OrderedSpatialIndexSnapshotV1 &state,
                       const OrderedSpatialIndexLimitsV1 limits) {
  validate_layout(state.layout, limits);
  const auto cell_count = state.layout.columns * state.layout.rows;
  if (state.cells.size() != cell_count ||
      state.member_pool.size() != state.layout.pool_blocks * kMembersPerBlock ||
      state.allocation_words.size() != state.layout.pool_blocks / 32U ||
      state.memberships.size() > limits.max_memberships ||
      state.memberships.size() > 65536U) {
    fail("snapshot storage does not match its bounded layout");
  }
  for (std::size_t i = 0U; i < state.memberships.size(); ++i) {
    const auto &entry = state.memberships[i];
    if (i != 0U && state.memberships[i - 1U].member >= entry.member) {
      fail("membership tokens must be strictly ordered and unique");
    }
    if (entry.rectangle) {
      validate_rectangle(*entry.rectangle, state.layout);
    }
  }

  std::vector<bool> owned(state.layout.pool_blocks, false);
  std::vector<std::uint32_t> occurrences(state.memberships.size(), 0U);
  for (std::size_t i = 0U; i < state.cells.size(); ++i) {
    const auto &cell = state.cells[i];
    if (cell.capacity_blocks == 0U) {
      if (cell.member_count != 0U || cell.first_block != 0U) {
        fail("unallocated cell has members or a nonzero block index");
      }
      continue;
    }
    if (!valid_capacity(cell.capacity_blocks) ||
        cell.member_count > kMembersPerBlock * cell.capacity_blocks ||
        cell.first_block % cell.capacity_blocks != 0U ||
        static_cast<std::uint32_t>(cell.first_block) + cell.capacity_blocks >
            state.layout.pool_blocks) {
      fail("cell allocation or count exceeds its supported domain");
    }
    for (std::uint32_t j = 0U; j < cell.capacity_blocks; ++j) {
      const auto block = cell.first_block + j;
      if (owned[block] || !allocated(state, block)) {
        fail("cell allocations overlap or reference a free block");
      }
      owned[block] = true;
    }
    const auto first =
        state.member_pool.begin() + cell.first_block * kMembersPerBlock;
    for (std::uint32_t j = 0U; j < cell.member_count; ++j) {
      const auto member = first[j];
      const auto entry = find_member(state.memberships, member);
      if (entry == state.memberships.end() || entry->member != member ||
          !contains(entry->rectangle,
                    static_cast<std::uint32_t>(i) % state.layout.columns,
                    static_cast<std::uint32_t>(i) / state.layout.columns) ||
          std::find(first, first + j, member) != first + j) {
        fail("cell has an unknown, misplaced or duplicate active member");
      }
      ++occurrences[static_cast<std::size_t>(entry -
                                             state.memberships.begin())];
    }
  }
  for (std::size_t i = 0U; i < state.memberships.size(); ++i) {
    const auto &rectangle = state.memberships[i].rectangle;
    const auto required =
        rectangle ? (static_cast<std::uint32_t>(rectangle->max_x) -
                     rectangle->min_x + 1U) *
                        (static_cast<std::uint32_t>(rectangle->max_y) -
                         rectangle->min_y + 1U)
                  : 0U;
    if (occurrences[i] != required) {
      fail("membership rectangle does not match its active cell entries");
    }
  }
}

std::uint16_t allocate(OrderedSpatialIndexSnapshotV1 &state,
                       const std::uint32_t blocks) {
  if (!valid_capacity(blocks)) {
    fail("allocation request exceeds the supported aligned group domain");
  }
  const auto base_mask = (1U << blocks) - 1U;
  for (std::size_t word = 0U; word < state.allocation_words.size(); ++word) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += blocks) {
      const auto mask = base_mask << shift;
      if ((state.allocation_words[word] & mask) == 0U) {
        state.allocation_words[word] |= mask;
        return static_cast<std::uint16_t>(word * 32U + shift);
      }
    }
  }
  fail("allocation would leave the explicitly supplied pool extent");
}

void release(OrderedSpatialIndexSnapshotV1 &state, const std::uint16_t first,
             const std::uint32_t blocks) {
  if (!valid_capacity(blocks) ||
      static_cast<std::uint32_t>(first) + blocks > state.layout.pool_blocks) {
    fail("release exceeds its supported allocation");
  }
  for (std::uint32_t i = 0U; i < blocks; ++i) {
    const auto block = first + i;
    const auto mask = 1U << (block % 32U);
    auto &word = state.allocation_words[block / 32U];
    if ((word & mask) == 0U) {
      fail("release reached an unallocated block");
    }
    word &= ~mask;
  }
}

void copy_blocks(OrderedSpatialIndexSnapshotV1 &state, const std::uint16_t from,
                 const std::uint16_t to, const std::uint32_t blocks) {
  // The source reads both halves before writing each 32-byte block. Use a
  // temporary block even for self-copy or overlapping old/new allocations.
  for (std::uint32_t i = 0U; i < blocks; ++i) {
    std::array<std::uint16_t, kMembersPerBlock> block;
    const auto source =
        state.member_pool.begin() + (from + i) * kMembersPerBlock;
    const auto target = state.member_pool.begin() + (to + i) * kMembersPerBlock;
    std::copy_n(source, block.size(), block.begin());
    std::copy(block.begin(), block.end(), target);
  }
}

void remove_member(OrderedSpatialIndexSnapshotV1 &state,
                   OrderedSpatialCellV1 &cell, const std::uint16_t member) {
  const auto first =
      state.member_pool.begin() + cell.first_block * kMembersPerBlock;
  const auto end = first + cell.member_count;
  const auto found = std::find(first, end, member);
  if (found == end) {
    fail("removal could not find the expected previous membership");
  }
  *found = *(end - 1);
  *(end - 1) = 0U;
  --cell.member_count;
  const auto capacity = cell.capacity_blocks;
  if (cell.member_count == 0U ||
      (capacity != 1U && cell.member_count < 8U * capacity - 4U)) {
    const auto old_block = cell.first_block;
    release(state, old_block, capacity);
    cell.first_block = 0U;
    cell.capacity_blocks = static_cast<std::uint8_t>(capacity / 2U);
    if (cell.capacity_blocks != 0U) {
      cell.first_block = allocate(state, cell.capacity_blocks);
      copy_blocks(state, old_block, cell.first_block, cell.capacity_blocks);
    }
  }
}

void append_member(OrderedSpatialIndexSnapshotV1 &state,
                   OrderedSpatialCellV1 &cell, const std::uint16_t member) {
  if (cell.member_count == 255U) {
    fail("insertion would wrap the bounded member count");
  }
  if (cell.capacity_blocks == 0U ||
      cell.member_count == kMembersPerBlock * cell.capacity_blocks) {
    const auto old_block = cell.first_block;
    const auto old_capacity = cell.capacity_blocks;
    const auto new_capacity = std::max(1U, 2U * old_capacity);
    const auto new_block = allocate(state, new_capacity);
    cell.first_block = new_block;
    cell.capacity_blocks = static_cast<std::uint8_t>(new_capacity);
    if (old_capacity != 0U) {
      release(state, old_block, old_capacity);
      copy_blocks(state, old_block, new_block, old_capacity);
    }
  }
  state.member_pool[cell.first_block * kMembersPerBlock + cell.member_count] =
      member;
  ++cell.member_count;
}

template <typename Function>
void for_each_cell(const SpatialCellRectangleV1 &rectangle, Function function) {
  for (std::uint32_t y = rectangle.min_y; y <= rectangle.max_y; ++y) {
    for (std::uint32_t x = rectangle.min_x; x <= rectangle.max_x; ++x) {
      function(x, y);
    }
  }
}

} // namespace

OrderedSpatialIndexSnapshotV1 make_cleared_ordered_spatial_index_v1(
    const OrderedSpatialIndexLayoutV1 layout,
    const OrderedSpatialIndexLimitsV1 limits) {
  validate_layout(layout, limits);
  OrderedSpatialIndexSnapshotV1 result;
  result.layout = layout;
  result.cells.resize(layout.columns * layout.rows);
  result.member_pool.resize(layout.pool_blocks * kMembersPerBlock);
  result.allocation_words.resize(layout.pool_blocks / 32U);
  return result;
}

OrderedSpatialIndexV1::OrderedSpatialIndexV1(
    const OrderedSpatialIndexSnapshotV1 &initial,
    const OrderedSpatialIndexLimitsV1 limits)
    : limits_(limits) {
  validate_snapshot(initial, limits);
  state_ = initial;
}

OrderedSpatialIndexSnapshotV1 OrderedSpatialIndexV1::snapshot() const {
  return state_;
}

std::uint64_t OrderedSpatialIndexV1::revision() const noexcept {
  return state_.revision;
}

std::span<const std::uint16_t>
OrderedSpatialIndexV1::members_at(const std::uint32_t x,
                                  const std::uint32_t y) const {
  if (x >= state_.layout.columns || y >= state_.layout.rows) {
    fail("query cell is outside the grid");
  }
  const auto &cell = state_.cells[y * state_.layout.columns + x];
  return std::span<const std::uint16_t>(state_.member_pool)
      .subspan(cell.first_block * kMembersPerBlock, cell.member_count);
}

void OrderedSpatialIndexV1::set_membership(
    const std::uint16_t member,
    const std::optional<SpatialCellRectangleV1> rectangle,
    const std::uint64_t expected_revision) {
  if (expected_revision != state_.revision || state_.revision == UINT64_MAX) {
    fail("update has a stale or exhausted revision");
  }
  if (rectangle) {
    validate_rectangle(*rectangle, state_.layout);
  }
  const auto existing = find_member(state_.memberships, member);
  const bool present =
      existing != state_.memberships.end() && existing->member == member;
  if (!present && state_.memberships.size() >= limits_.max_memberships) {
    fail("update exceeds the membership limit");
  }
  const auto previous = present ? existing->rectangle : std::nullopt;
  auto staged = state_;
  if (previous) {
    for_each_cell(*previous, [&](const std::uint32_t x, const std::uint32_t y) {
      if (!contains(rectangle, x, y)) {
        remove_member(staged, staged.cells[y * staged.layout.columns + x],
                      member);
      }
    });
  }
  if (rectangle) {
    for_each_cell(
        *rectangle, [&](const std::uint32_t x, const std::uint32_t y) {
          if (!contains(previous, x, y)) {
            append_member(staged, staged.cells[y * staged.layout.columns + x],
                          member);
          }
        });
  }
  const auto index =
      static_cast<std::size_t>(existing - state_.memberships.begin());
  if (present) {
    staged.memberships[index].rectangle = rectangle;
  } else {
    staged.memberships.insert(staged.memberships.begin() + index,
                              OrderedSpatialMembershipV1{member, rectangle});
  }
  ++staged.revision;
  static_assert(
      std::is_nothrow_move_assignable_v<OrderedSpatialIndexSnapshotV1>);
  state_ = std::move(staged);
}

} // namespace openrc::game
