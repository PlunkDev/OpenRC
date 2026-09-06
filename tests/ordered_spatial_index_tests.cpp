#include "openrc/ordered_spatial_index.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

namespace {

using namespace openrc::game;
using Snapshot = OrderedSpatialIndexSnapshotV1;
using Index = OrderedSpatialIndexV1;
using Rectangle = SpatialCellRectangleV1;
constexpr OrderedSpatialIndexLimitsV1 kLimits{4096U, 32768U, 65536U};
constexpr Rectangle kOneCell{0U, 0U, 0U, 0U};

void expect(const bool value, const std::string &message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}

template <class Operation>
void rejected(Operation operation, const std::string &description) {
  try {
    operation();
  } catch (const OrderedSpatialIndexError &) {
    return;
  }
  throw std::runtime_error("Accepted " + description);
}

void expect_members(const Index &index, const std::uint32_t x,
                    const std::uint32_t y,
                    const std::initializer_list<std::uint16_t> expected,
                    const std::string &description) {
  const auto actual = index.members_at(x, y);
  expect(actual.size() == expected.size() &&
             std::equal(actual.begin(), actual.end(), expected.begin()),
         description);
}

void update(Index &index, const std::uint16_t member,
            const std::optional<Rectangle> rectangle) {
  index.set_membership(member, rectangle, index.revision());
}

void occupy(Snapshot &snapshot, const std::uint32_t first,
            const std::uint32_t count) {
  for (std::uint32_t block = first; block < first + count; ++block) {
    snapshot.allocation_words.at(block / 32U) |= std::uint32_t{1U}
                                                 << (block % 32U);
  }
}

Snapshot cleared(const std::uint32_t columns = 1U,
                 const std::uint32_t rows = 1U,
                 const std::uint32_t blocks = 64U) {
  return make_cleared_ordered_spatial_index_v1({columns, rows, blocks},
                                               kLimits);
}

// Explicit synthetic storage, including meaningful inactive entries. These
// fixtures do not predict cells from any source world-coordinate arithmetic.
Snapshot populated_cell(const std::uint8_t capacity, const std::uint8_t count,
                        const std::uint16_t first = 0U,
                        const std::uint32_t blocks = 64U) {
  auto result = cleared(1U, 1U, blocks);
  for (std::size_t i = 0U; i < result.member_pool.size(); ++i) {
    result.member_pool[i] = static_cast<std::uint16_t>(0xa000U + (i % 0x1000U));
  }
  result.cells[0] = {first, count, capacity};
  occupy(result, first, capacity);
  for (std::uint32_t i = 0U; i < count; ++i) {
    const auto member = static_cast<std::uint16_t>(i + 1U);
    result.member_pool[static_cast<std::size_t>(first) * 16U + i] = member;
    result.memberships.push_back({member, kOneCell});
  }
  return result;
}

void test_clear_restore_and_storage_ownership() {
  const auto clear = cleared(3U, 2U, 32U);
  expect(clear.layout == OrderedSpatialIndexLayoutV1{3U, 2U, 32U} &&
             clear.revision == 0U && clear.cells.size() == 6U &&
             clear.member_pool.size() == 512U &&
             clear.allocation_words == std::vector<std::uint32_t>{0U} &&
             clear.memberships.empty(),
         "Explicit clear has incorrect extents or metadata");
  expect(std::all_of(
             clear.cells.begin(), clear.cells.end(),
             [](const auto &cell) { return cell == OrderedSpatialCellV1{}; }) &&
             std::all_of(clear.member_pool.begin(), clear.member_pool.end(),
                         [](const auto value) { return value == 0U; }),
         "Explicit clear did not initialize its complete storage");
  auto supplied = populated_cell(1U, 2U, 3U);
  supplied.revision = 29U;
  const auto saved = supplied;
  Index index(supplied, kLimits);
  supplied.member_pool[3U * 16U] = 999U;
  supplied.cells[0].member_count = 0U;
  supplied.memberships.clear();
  expect(index.snapshot() == saved && index.revision() == 29U,
         "Restoration aliases caller storage or resets revision");
  auto exported = index.snapshot();
  exported.member_pool[3U * 16U] = 777U;
  exported.allocation_words[0] = 0U;
  expect(index.snapshot() == saved, "Returned snapshot aliases live storage");
  Index restored(index.snapshot(), kLimits);
  expect(restored.snapshot() == saved,
         "Snapshot round trip changed stale bytes");
  update(index, 3U, kOneCell);
  update(restored, 3U, kOneCell);
  expect(index.snapshot() == restored.snapshot(),
         "Restored continuation diverged");
}

void test_last_member_replacement_and_explicit_last_zero() {
  Index index(cleared(), kLimits);
  for (const auto member : {10U, 20U, 30U, 40U}) {
    update(index, static_cast<std::uint16_t>(member), kOneCell);
  }
  update(index, 20U, std::nullopt);
  expect_members(index, 0U, 0U, {10U, 40U, 30U},
                 "Removal did not replace the found member by the last");
  auto state = index.snapshot();
  expect(state.member_pool[3] == 0U && state.member_pool[1] == 40U &&
             state.cells[0] == OrderedSpatialCellV1{0U, 3U, 1U},
         "Removal did not clear only the old final active entry");
  update(index, 30U, std::nullopt);
  expect_members(index, 0U, 0U, {10U, 40U},
                 "Last-member self-copy changed order");
  expect(index.snapshot().member_pool[2] == 0U,
         "Last-member self-copy skipped the explicit final zero write");
  update(index, 10U, std::nullopt);
  expect_members(index, 0U, 0U, {40U}, "First-member replacement is incorrect");
  update(index, 40U, std::nullopt);
  state = index.snapshot();
  expect(state.cells[0] == OrderedSpatialCellV1{} &&
             state.allocation_words[0] == 0U && state.member_pool[0] == 0U,
         "Removing the last member of a one-block list did not release it");
  expect(state.memberships.size() == 4U &&
             std::all_of(state.memberships.begin(), state.memberships.end(),
                         [](const auto &entry) { return !entry.rectangle; }),
         "Removal discarded lifetime records");
}

void test_rectangle_overlap_and_x_inner_order() {
  Index index(cleared(3U, 3U, 32U), kLimits);
  const Rectangle old_bounds{0U, 0U, 1U, 1U};
  for (const auto member : {10U, 20U, 30U}) {
    update(index, static_cast<std::uint16_t>(member), old_bounds);
  }
  auto state = index.snapshot();
  expect(state.cells[0].first_block == 0U && state.cells[1].first_block == 1U &&
             state.cells[3].first_block == 2U &&
             state.cells[4].first_block == 3U,
         "Initial rectangle did not visit X inner, Y outer");
  update(index, 20U, Rectangle{1U, 1U, 2U, 2U});
  expect_members(index, 0U, 0U, {10U, 30U},
                 "Old-only cell did not remove member");
  expect_members(index, 1U, 0U, {10U, 30U},
                 "Old-only second cell is incorrect");
  expect_members(index, 0U, 1U, {10U, 30U}, "Old-only next row is incorrect");
  expect_members(index, 1U, 1U, {10U, 20U, 30U},
                 "Overlap cell was removed/reappended and reordered");
  expect_members(index, 2U, 1U, {20U}, "New-only first cell was not appended");
  expect_members(index, 1U, 2U, {20U}, "New-only second row is incorrect");
  expect_members(index, 2U, 2U, {20U}, "Inclusive maximum cell is missing");
  expect_members(index, 2U, 0U, {}, "Untouched cell gained membership");
  state = index.snapshot();
  expect(state.cells[5].first_block == 4U && state.cells[7].first_block == 5U &&
             state.cells[8].first_block == 6U &&
             state.allocation_words[0] == 0x7fU,
         "New-only rectangle allocation order changed");
  const auto before_same = state;
  update(index, 20U, Rectangle{1U, 1U, 2U, 2U});
  auto expected = before_same;
  ++expected.revision;
  expect(index.snapshot() == expected,
         "Same bounds must advance revision without touching list order or "
         "storage");
  Index restored(index.snapshot(), kLimits);
  expect(restored.snapshot() == index.snapshot(),
         "Overlap snapshot is inconsistent");
}

void test_removal_iteration_order_is_observable_in_relocation() {
  auto initial = cleared(2U, 2U, 32U);
  for (std::size_t i = 0U; i < initial.member_pool.size(); ++i) {
    initial.member_pool[i] = static_cast<std::uint16_t>(0xb000U + i);
  }
  initial.cells = {{4U, 1U, 2U}, {2U, 1U, 2U}, {0U, 1U, 2U}, {6U, 1U, 2U}};
  initial.allocation_words[0] = 0xffffffffU;
  initial.memberships = {{42U, Rectangle{0U, 0U, 1U, 1U}}};
  for (const auto &cell : initial.cells) {
    initial.member_pool[static_cast<std::size_t>(cell.first_block) * 16U] = 42U;
  }
  Index index(initial, kLimits);
  update(index, 42U, std::nullopt);
  const auto state = index.snapshot();
  const std::array<std::uint16_t, 4U> expected_blocks{4U, 2U, 0U, 1U};
  for (std::size_t cell = 0U; cell < 4U; ++cell) {
    expect(state.cells[cell] ==
               OrderedSpatialCellV1{expected_blocks[cell], 0U, 1U},
           "Removal/shrink did not visit X inner, Y outer");
    const auto source =
        static_cast<std::size_t>(initial.cells[cell].first_block) * 16U;
    const auto destination =
        static_cast<std::size_t>(expected_blocks[cell]) * 16U;
    for (std::size_t i = 0U; i < 16U; ++i) {
      expect(state.member_pool[destination + i] ==
                 (i == 0U ? 0U : initial.member_pool[source + i]),
             "Ordered removal relocation lost inactive block entries");
    }
  }
  expect(state.allocation_words[0] == 0xffffff17U,
         "Ordered shrink changed opaque bits or selected the wrong freed hole");
}

void test_growth_alignment_order_and_full_block_copy() {
  for (const auto capacity : {1U, 2U, 4U, 8U}) {
    const auto initial =
        populated_cell(static_cast<std::uint8_t>(capacity),
                       static_cast<std::uint8_t>(16U * capacity));
    Index index(initial, kLimits);
    const auto appended = static_cast<std::uint16_t>(16U * capacity + 1U);
    update(index, appended, kOneCell);
    const auto state = index.snapshot();
    const auto new_capacity = 2U * capacity;
    const auto new_first = new_capacity;
    expect(state.cells[0] ==
               OrderedSpatialCellV1{static_cast<std::uint16_t>(new_first),
                                    static_cast<std::uint8_t>(appended),
                                    static_cast<std::uint8_t>(new_capacity)},
           "Growth must choose the first aligned group while old blocks are "
           "occupied");
    for (std::uint32_t i = 0U; i < 16U * capacity; ++i) {
      expect(state.member_pool[new_first * 16U + i] == initial.member_pool[i] &&
                 state.member_pool[i] == initial.member_pool[i],
             "Growth did not copy every old block or cleared freed old bytes");
    }
    expect(state.member_pool[new_first * 16U + 16U * capacity] == appended,
           "Growth appended before restoring the previous list");
    for (std::uint32_t i = 16U * capacity + 1U; i < 16U * new_capacity; ++i) {
      expect(state.member_pool[new_first * 16U + i] ==
                 initial.member_pool[new_first * 16U + i],
             "Growth cleared inactive destination bytes");
    }
    auto expected_words = std::vector<std::uint32_t>(2U, 0U);
    for (std::uint32_t block = new_first; block < new_first + new_capacity;
         ++block) {
      expected_words[block / 32U] |= std::uint32_t{1U} << (block % 32U);
    }
    expect(state.allocation_words == expected_words,
           "Growth did not free exactly the old bitmap group");
  }
  // Freeing old block0 first would incorrectly make the only pair available.
  auto exhausted = populated_cell(1U, 16U, 0U, 32U);
  exhausted.allocation_words[0] = 0xfffffffdU;
  Index index(exhausted, kLimits);
  rejected([&] { update(index, 17U, kOneCell); },
           "growth that succeeds only by freeing old storage too early");
  expect(index.snapshot() == exhausted, "Failed growth changed storage");
}

void test_allocator_bitmap_word_order_and_reserved_bits() {
  auto initial = cleared(2U, 1U, 64U);
  initial.allocation_words[0] = 0x7fffffffU;
  Index single(initial, kLimits);
  update(single, 0U, Rectangle{0U, 0U, 1U, 0U});
  auto state = single.snapshot();
  expect(
      state.cells[0].first_block == 31U && state.cells[1].first_block == 32U &&
          state.allocation_words == std::vector<std::uint32_t>{0xffffffffU, 1U},
      "One-block allocation did not scan LSB-first across bitmap words");
  expect_members(single, 0U, 0U, {0U}, "Zero is a valid active member token");
  Index restored(state, kLimits);
  expect(restored.snapshot() == state,
         "Opaque reserved bitmap bits were rejected");

  initial = populated_cell(1U, 16U, 0U, 64U);
  initial.allocation_words = {0xffffffffU, 1U};
  Index groups(initial, kLimits);
  update(groups, 17U, kOneCell);
  state = groups.snapshot();
  expect(state.cells[0].first_block == 34U &&
             state.allocation_words ==
                 std::vector<std::uint32_t>{0xfffffffeU, 0x0dU},
         "Two-block allocation lost alignment, word order or reserved bits");

  initial = populated_cell(2U, 32U, 8U, 32U);
  occupy(initial, 0U, 1U);
  occupy(initial, 4U, 1U);
  Index aligned(initial, kLimits);
  update(aligned, 33U, kOneCell);
  state = aligned.snapshot();
  expect(state.cells[0].first_block == 12U &&
             state.cells[0].capacity_blocks == 4U &&
             state.allocation_words[0] == 0xf011U,
         "Four-block allocator used an unaligned hole or changed opaque "
         "reservations");
}

void test_shrink_threshold_single_halving_and_stale_copy() {
  for (const auto capacity : {2U, 4U, 8U, 16U}) {
    const auto threshold = 8U * capacity - 4U;
    auto initial =
        populated_cell(static_cast<std::uint8_t>(capacity),
                       static_cast<std::uint8_t>(threshold + 1U), 16U);
    Index at_threshold(initial, kLimits);
    update(at_threshold, static_cast<std::uint16_t>(threshold + 1U),
           std::nullopt);
    auto state = at_threshold.snapshot();
    expect(state.cells[0].capacity_blocks == capacity &&
               state.cells[0].first_block == 16U &&
               state.cells[0].member_count == threshold,
           "Shrink used <= instead of strict < at the threshold");

    initial = populated_cell(static_cast<std::uint8_t>(capacity),
                             static_cast<std::uint8_t>(threshold), 16U);
    Index below_threshold(initial, kLimits);
    update(below_threshold, static_cast<std::uint16_t>(threshold),
           std::nullopt);
    state = below_threshold.snapshot();
    expect(state.cells[0].capacity_blocks == capacity / 2U &&
               state.cells[0].first_block == 0U &&
               state.cells[0].member_count == threshold - 1U,
           "Shrink did not halve once below its threshold");
    for (std::uint32_t i = 0U; i < 16U * (capacity / 2U); ++i) {
      const auto expected =
          i == threshold - 1U ? 0U : initial.member_pool[256U + i];
      expect(state.member_pool[i] == expected,
             "Shrink did not copy all new-capacity blocks including inactive "
             "entries");
    }
    expect(state.member_pool[256U + threshold - 1U] == 0U,
           "Shrink copy occurred before the explicit old-final-member zero");
  }

  auto initial = populated_cell(4U, 1U, 8U);
  Index empty(initial, kLimits);
  update(empty, 1U, std::nullopt);
  auto state = empty.snapshot();
  expect(state.cells[0] == OrderedSpatialCellV1{0U, 0U, 2U} &&
             state.allocation_words[0] == 3U,
         "Empty list from a larger allocation was incorrectly fully freed");
  Index restored_empty(state, kLimits);
  expect(restored_empty.snapshot() == state,
         "Restoration rejected a retained zero-count allocation");
  for (std::size_t i = 0U; i < 32U; ++i) {
    expect(state.member_pool[i] ==
               (i == 0U ? 0U : initial.member_pool[128U + i]),
           "Zero-count shrink omitted stale full-block copy");
  }
  for (std::size_t i = 32U; i < 64U; ++i) {
    expect(state.member_pool[i] == initial.member_pool[i],
           "Shrink copied the old capacity instead of the new half-capacity");
  }
  const auto after_empty = state;
  update(empty, 1U, std::nullopt);
  auto same = after_empty;
  ++same.revision;
  expect(empty.snapshot() == same, "Unchanged absent bounds halved again");
  update(empty, 2U, kOneCell);
  expect(empty.snapshot().cells[0] == OrderedSpatialCellV1{0U, 1U, 2U},
         "Append did not reuse retained zero-count capacity");

  initial = populated_cell(1U, 2U, 5U);
  Index one_block(initial, kLimits);
  update(one_block, 2U, std::nullopt);
  expect(one_block.snapshot().cells[0] == OrderedSpatialCellV1{5U, 1U, 1U},
         "One-block nonempty list was shrunk");
  update(one_block, 1U, std::nullopt);
  state = one_block.snapshot();
  expect(state.cells[0] == OrderedSpatialCellV1{} &&
             state.allocation_words[0] == 0U && state.member_pool[80U] == 0U &&
             state.member_pool[82U] == initial.member_pool[82U],
         "One-block empty release cleared stale bytes or retained a nonzero "
         "pointer");
}

void test_shrink_free_before_allocate_and_self_copy() {
  auto initial = populated_cell(2U, 12U, 8U, 32U);
  initial.allocation_words[0] = 0xffffffffU;
  Index index(initial, kLimits);
  update(index, 12U, std::nullopt);
  auto expected = initial;
  expected.revision = 1U;
  expected.cells[0] = {8U, 11U, 1U};
  expected.member_pool[128U + 11U] = 0U;
  expected.allocation_words[0] = 0xfffffdffU;
  expected.memberships.back().rectangle.reset();
  expect(index.snapshot() == expected, "Shrink must free before allocating and "
                                       "preserve a same-address block copy");
}

void test_atomic_failure_revision_and_count_boundary() {
  auto move_state = cleared(2U, 1U, 32U);
  move_state.cells[0] = {0U, 1U, 1U};
  move_state.member_pool[0] = 7U;
  move_state.allocation_words[0] = 0xffffffffU;
  move_state.memberships = {{7U, kOneCell}};
  Index move(move_state, kLimits);
  update(move, 7U, Rectangle{1U, 0U, 1U, 0U});
  auto moved = move.snapshot();
  expect(moved.cells[0] == OrderedSpatialCellV1{} &&
             moved.cells[1] == OrderedSpatialCellV1{0U, 1U, 1U} &&
             moved.allocation_words[0] == 0xffffffffU,
         "Old-only removals must finish before new-only allocations");

  auto initial = cleared(2U, 1U, 32U);
  initial.allocation_words[0] = 0xfffffffeU;
  Index late(initial, kLimits);
  rejected([&] { update(late, 12U, Rectangle{0U, 0U, 1U, 0U}); },
           "second-cell allocation past the pool boundary");
  expect(late.snapshot() == initial,
         "Late pool exhaustion committed the first cell or lifetime record");

  initial = populated_cell(16U, 255U);
  Index full(initial, kLimits);
  rejected([&] { update(full, 256U, kOneCell); }, "unsupported count255 to256");
  expect(full.snapshot() == initial,
         "Count overflow wrapped or mutated storage");

  auto late_count = initial;
  late_count.layout.columns = 2U;
  late_count.cells.push_back({16U, 1U, 1U});
  late_count.member_pool[256U] = 256U;
  occupy(late_count, 16U, 1U);
  late_count.memberships.push_back({256U, Rectangle{1U, 0U, 1U, 0U}});
  Index rollback_remove(late_count, kLimits);
  rejected([&] { update(rollback_remove, 256U, kOneCell); },
           "count overflow after old-only removal");
  expect(rollback_remove.snapshot() == late_count,
         "Late count overflow committed removal, release or old-final zero");

  const auto before_same = full.snapshot();
  update(full, 128U, kOneCell);
  auto after_same = before_same;
  ++after_same.revision;
  expect(
      full.snapshot() == after_same,
      "Same rectangle incorrectly appended to an already full byte-count list");

  const auto before = full.snapshot();
  rejected([&] { full.set_membership(1U, std::nullopt, 0U); },
           "stale revision");
  expect(full.snapshot() == before, "Stale revision changed state");
  rejected([&] { update(full, 1U, Rectangle{1U, 0U, 0U, 0U}); },
           "inverted rectangle");
  rejected([&] { update(full, 1U, Rectangle{0U, 0U, 1U, 0U}); },
           "out-of-layout rectangle");
  rejected([&] { (void)full.members_at(1U, 0U); },
           "out-of-layout member query");
  rejected([&] { (void)full.members_at(0U, 1U); }, "out-of-layout row query");
  expect(full.snapshot() == before, "Invalid update or query changed state");
  initial.revision = std::numeric_limits<std::uint64_t>::max();
  Index exhausted_revision(initial, kLimits);
  rejected([&] { update(exhausted_revision, 1U, kOneCell); }, "revision wrap");
  expect(exhausted_revision.snapshot() == initial,
         "Revision overflow changed state");
}

void test_snapshot_membership_consistency_and_allocation_ownership() {
  const auto check = [](auto mutate, const std::string &why) {
    auto invalid = populated_cell(2U, 2U, 4U);
    mutate(invalid);
    rejected([&] { Index index(invalid, kLimits); }, why);
  };
  check([](Snapshot &s) { s.cells.pop_back(); }, "short cell table");
  check([](Snapshot &s) { s.cells.push_back({}); }, "extra cell");
  check([](Snapshot &s) { s.member_pool.pop_back(); }, "short member pool");
  check([](Snapshot &s) { s.member_pool.push_back(0U); }, "extra pool entry");
  check([](Snapshot &s) { s.allocation_words.pop_back(); }, "short bitmap");
  check([](Snapshot &s) { s.allocation_words.push_back(0U); },
        "extra bitmap word");
  check([](Snapshot &s) { s.cells[0].capacity_blocks = 3U; },
        "non-power-of-two capacity");
  check([](Snapshot &s) { s.cells[0].capacity_blocks = 32U; },
        "unsupported capacity32");
  check([](Snapshot &s) { s.cells[0].member_count = 33U; },
        "count beyond capacity");
  check([](Snapshot &s) { s.cells[0].capacity_blocks = 0U; },
        "active count with no allocation");
  check([](Snapshot &s) { s.cells[0].first_block = 5U; },
        "unaligned allocation");
  check([](Snapshot &s) { s.cells[0].first_block = 64U; },
        "allocation outside pool");
  check([](Snapshot &s) { s.allocation_words[0] &= ~(1U << 5U); },
        "partly unallocated cell range");
  check([](Snapshot &s) { std::swap(s.memberships[0], s.memberships[1]); },
        "unsorted membership tokens");
  check([](Snapshot &s) { s.memberships[1].member = 1U; },
        "duplicate membership token");
  check([](Snapshot &s) { s.memberships.pop_back(); },
        "active unregistered member");
  check([](Snapshot &s) { s.memberships[0].rectangle.reset(); },
        "active member with absent registered bounds");
  check(
      [](Snapshot &s) {
        s.memberships[0].rectangle = Rectangle{1U, 0U, 0U, 0U};
      },
      "inverted registered bounds");
  check(
      [](Snapshot &s) {
        s.memberships[0].rectangle = Rectangle{0U, 0U, 1U, 0U};
      },
      "registered bounds outside layout");
  check([](Snapshot &s) { s.member_pool[64U + 1U] = 1U; },
        "duplicate active token in a cell");
  check([](Snapshot &s) { s.member_pool[64U] = 999U; },
        "unknown active member token");

  auto empty_pointer = cleared();
  empty_pointer.cells[0].first_block = 1U;
  rejected([&] { Index index(empty_pointer, kLimits); },
           "nonzero first block at zero capacity");
  auto alias = cleared(2U, 1U, 32U);
  alias.cells = {{0U, 0U, 1U}, {0U, 0U, 1U}};
  alias.allocation_words[0] = 1U;
  rejected([&] { Index index(alias, kLimits); },
           "overlapping cell allocations");

  Index valid(cleared(2U, 1U, 32U), kLimits);
  update(valid, 7U, Rectangle{0U, 0U, 1U, 0U});
  auto missing = valid.snapshot();
  missing.cells[1].member_count = 0U;
  rejected([&] { Index index(missing, kLimits); },
           "rectangle whose member is missing from one required cell");
  auto outside = valid.snapshot();
  outside.memberships[0].rectangle = kOneCell;
  rejected([&] { Index index(outside, kLimits); },
           "active membership outside registered rectangle");
}

void test_layout_limits_lifetime_tokens_and_extreme_valid_shape() {
  for (const auto layout : {OrderedSpatialIndexLayoutV1{0U, 1U, 32U},
                            {1U, 0U, 32U},
                            {65U, 1U, 32U},
                            {1U, 65U, 32U},
                            {1U, 1U, 0U},
                            {1U, 1U, 31U},
                            {1U, 1U, 33U},
                            {1U, 1U, 32800U},
                            {0xffffffffU, 0xffffffffU, 32U}}) {
    rejected(
        [&] { (void)make_cleared_ordered_spatial_index_v1(layout, kLimits); },
        "unsupported cleared layout");
    auto invalid = cleared();
    invalid.layout = layout;
    rejected([&] { Index index(invalid, kLimits); },
             "unsupported restored layout");
  }
  const auto initial = populated_cell(1U, 2U);
  for (const auto limits : {OrderedSpatialIndexLimitsV1{},
                            {0U, 64U, 2U},
                            {1U, 0U, 2U},
                            {1U, 64U, 0U},
                            {1U, 32U, 2U},
                            {1U, 64U, 1U}}) {
    rejected([&] { Index index(initial, limits); },
             "restored count or mandatory limit");
  }
  rejected(
      [&] {
        (void)make_cleared_ordered_spatial_index_v1({2U, 2U, 32U},
                                                    {3U, 32U, 1U});
      },
      "clear exceeding aggregate cell limit");
  auto two_cells = cleared(2U, 1U, 32U);
  rejected([&] { Index index(two_cells, {1U, 32U, 1U}); },
           "restore exceeding aggregate cell limit");

  Index lifetime(cleared(), {1U, 64U, 2U});
  update(lifetime, 65535U, kOneCell);
  update(lifetime, 0U, kOneCell);
  auto state = lifetime.snapshot();
  expect(
      state.memberships[0].member == 0U &&
          state.memberships[1].member == 65535U,
      "Lifetime tokens were not canonicalized independently of append order");
  expect_members(
      lifetime, 0U, 0U, {65535U, 0U},
      "Canonical token registry incorrectly sorted active cell list");
  update(lifetime, 65535U, std::nullopt);
  const auto before = lifetime.snapshot();
  rejected([&] { update(lifetime, 1U, kOneCell); },
           "lifetime token count after removal");
  expect(lifetime.snapshot() == before, "Lifetime limit failure changed state");
  update(lifetime, 65535U, kOneCell);
  expect_members(lifetime, 0U, 0U, {0U, 65535U},
                 "Existing lifetime token was not reusable");

  // The supported maximum is a finite storage shape, not a live-source claim.
  auto maximum = cleared(64U, 64U, 32768U);
  std::fill(maximum.allocation_words.begin(), maximum.allocation_words.end(),
            0xffffffffU);
  maximum.allocation_words.back() = 0x7fffffffU;
  maximum.member_pool.back() = 0xabcdU;
  Index large(maximum, kLimits);
  update(large, 5U, Rectangle{63U, 63U, 63U, 63U});
  expect_members(large, 63U, 63U, {5U},
                 "Maximum supported cell coordinate failed");
  state = large.snapshot();
  expect(state.cells.back().first_block == 32767U &&
             state.member_pool.back() == 0xabcdU &&
             std::all_of(state.allocation_words.begin(),
                         state.allocation_words.end(),
                         [](const auto word) { return word == 0xffffffffU; }),
         "Maximum pool shape lost opaque bits or inactive final bytes");
}

} // namespace

int main() {
  try {
    test_clear_restore_and_storage_ownership();
    test_last_member_replacement_and_explicit_last_zero();
    test_rectangle_overlap_and_x_inner_order();
    test_removal_iteration_order_is_observable_in_relocation();
    test_growth_alignment_order_and_full_block_copy();
    test_allocator_bitmap_word_order_and_reserved_bits();
    test_shrink_threshold_single_halving_and_stale_copy();
    test_shrink_free_before_allocate_and_self_copy();
    test_atomic_failure_revision_and_count_boundary();
    test_snapshot_membership_consistency_and_allocation_ownership();
    test_layout_limits_lifetime_tokens_and_extreme_valid_shape();
    std::cout << "Ordered spatial index tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Ordered spatial index tests failed: " << error.what() << '\n';
    return 1;
  }
}
