#include "openrc/rac_actor_schedule_compile.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr openrc::RacActorScheduleCompileLimitsV1 kLimits{16U, 8U, 8U, 32U};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_error(Callback &&callback, const std::string_view diagnostic) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::RacActorScheduleCompileError &error) {
    expect(std::string_view(error.what()).find(diagnostic) !=
               std::string_view::npos,
           "Unexpected compile diagnostic: " + std::string(error.what()));
    return;
  }
  throw std::runtime_error("Malformed source groups or remap were accepted");
}

void write_u32(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes.at(offset + index) =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void write_u16(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint16_t value) {
  bytes.at(offset) = static_cast<std::byte>(value & 0xffU);
  bytes.at(offset + 1U) = static_cast<std::byte>((value >> 8U) & 0xffU);
}

// Original synthetic source bytes, with no game assets. Four authored
// placements, a permuted three-member group, an absent slot, and a singleton.
[[nodiscard]] std::vector<std::byte> make_source_bytes() {
  constexpr std::array<std::uint32_t, openrc::kRacGameplayBlockCountV1> slots{
      0x88U, 0x00U, 0x10U, 0x14U, 0x18U, 0x1cU, 0x20U, 0x24U, 0x28U,
      0x2cU, 0x04U, 0x80U, 0x08U, 0x0cU, 0x40U, 0x44U, 0x54U, 0x58U,
      0x50U, 0x5cU, 0x48U, 0x4cU, 0x30U, 0x34U, 0x38U, 0x3cU, 0x70U,
      0x60U, 0x64U, 0x68U, 0x6cU, 0x84U, 0x7cU, 0x78U, 0x74U, 0x8cU};
  std::array<std::uint32_t, openrc::kRacGameplayDirectorySlotCountV1> offsets{};
  auto next_offset = openrc::kRacGameplayFirstBlockOffsetV1;
  for (const auto slot : slots) {
    offsets[slot / 4U] = next_offset;
    if (slot == 0U) {
      next_offset += 0x50U;
    } else if (slot == 0x44U) {
      next_offset += 0x10U + 4U * openrc::kRacGameplayMobyRecordBytesV1;
    } else if (slot == 0x48U) {
      next_offset += 0x30U;
    } else {
      next_offset += 0x10U;
    }
  }
  std::vector<std::byte> bytes(next_offset, std::byte{0U});
  for (const auto slot : slots) {
    write_u32(bytes, slot, offsets[slot / 4U]);
  }
  const auto classes = offsets[0x40U / 4U];
  write_u32(bytes, classes, 1U);
  write_u32(bytes, classes + 4U, 42U);
  const auto mobies = offsets[0x44U / 4U];
  write_u32(bytes, mobies, 4U);
  for (std::uint32_t index = 0U; index < 4U; ++index) {
    const auto record =
        mobies + 0x10U + index * openrc::kRacGameplayMobyRecordBytesV1;
    write_u32(bytes, record, openrc::kRacGameplayMobyRecordBytesV1);
    write_u32(bytes, record + 0x18U, 42U);
    write_u32(bytes, record + 0x1cU, std::bit_cast<std::uint32_t>(1.0F));
    write_u32(bytes, record + 0x48U, index == 3U ? 2U : 0U);
    write_u32(bytes, record + 0x58U, 0xffffffffU);
  }
  for (const auto slot : {0x50U, 0x5cU}) {
    write_u32(bytes, offsets[slot / 4U], 0xffffffffU);
    write_u32(bytes, offsets[slot / 4U] + 4U, 0xffffffffU);
  }
  const auto groups = offsets[0x48U / 4U];
  write_u32(bytes, groups, 3U);
  write_u32(bytes, groups + 4U, 8U);
  write_u32(bytes, groups + 0x10U, 0U);
  write_u32(bytes, groups + 0x14U, 0xfffffffeU);
  write_u32(bytes, groups + 0x18U, 6U);
  write_u16(bytes, groups + 0x1cU, 2U);
  write_u16(bytes, groups + 0x1eU, 0U);
  write_u16(bytes, groups + 0x20U, 0x8001U);
  write_u16(bytes, groups + 0x22U, 0x8003U);
  return bytes;
}

[[nodiscard]] openrc::RacGameplayBankV1 make_source() {
  return openrc::parse_rac_gameplay_bank_v1(make_source_bytes(), {0x10000U});
}

[[nodiscard]] std::vector<std::optional<std::uint32_t>> make_remap() {
  return {17U, 42U, 90U, 8U};
}

[[nodiscard]] std::vector<std::uint64_t>
source_group_image(const openrc::RacGameplayBankV1 &source) {
  std::vector<std::uint64_t> words{source.input_bytes,
                                   source.static_moby_count,
                                   source.static_mobies.size(),
                                   source.moby_group_count,
                                   source.moby_group_header_range.offset,
                                   source.moby_group_header_range.size,
                                   source.moby_group_table_records_range.offset,
                                   source.moby_group_table_records_range.size,
                                   source.moby_group_member_data_range.offset,
                                   source.moby_group_member_data_range.size};
  for (const auto &group : source.moby_groups) {
    words.insert(words.end(),
                 {group.source_group_id, group.table_record_range.offset,
                  group.table_record_range.size,
                  static_cast<std::uint32_t>(group.source_member_data_offset),
                  group.member_records_range.offset,
                  group.member_records_range.size, group.members.size()});
    for (const auto &member : group.members) {
      words.insert(words.end(),
                   {member.record_range.offset, member.record_range.size,
                    member.raw_value, member.static_moby_index});
    }
  }
  return words;
}

void test_source_parser_remap_and_runtime_scheduler_integration() {
  const auto bytes = make_source_bytes();
  const auto before_bytes = bytes;
  const auto source = openrc::parse_rac_gameplay_bank_v1(bytes, {0x10000U});
  auto remap = make_remap();
  remap[0U] = std::nullopt;
  const auto before_remap = remap;
  const auto before_source = source_group_image(source);
  const auto groups =
      openrc::compile_rac_actor_activation_groups_v1(source, remap, kLimits);
  expect(
      groups ==
          std::vector<openrc::game::ActorActivationGroupV1>{
              {0U, {90U, 42U}}, {1U, {}}, {2U, {8U}}},
      "Compiler changed source group slots, permutation, or omitted mapping");
  expect(bytes == before_bytes && remap == before_remap &&
             source_group_image(source) == before_source,
         "Group compilation changed its source bytes, metadata, or remap");

  // The source loader's surviving live-slot order is source1,source2,source3;
  // IDs are independently assigned by the explicit compiler mapping above.
  const std::array<openrc::game::ActorUpdateCandidateV1, 3U> candidates{{
      {42U, 0U, 0U, true, false, false, false},
      {90U, 0U, 0U, true, true, false, false},
      {8U, 0U, 2U, true, false, true, false},
  }};
  const std::array<openrc::game::ActorUpdateRangeEligibilityV1, 1U> ranges{{
      {42U, true},
  }};
  constexpr openrc::game::ActorUpdateScheduleLimitsV1 schedule_limits{
      16U, 8U, 8U, 32U, 8U, 16U};
  const auto scheduled = openrc::game::build_grouped_actor_update_schedule_v1(
      candidates, groups, ranges, schedule_limits);
  expect(scheduled.ordered_authored_ids ==
             std::vector<std::uint32_t>{90U, 42U, 8U},
         "Parsed/remapped source group did not awaken its skipped sibling in "
         "source order");
  expect(openrc::game::build_actor_initialization_schedule_v1(
             candidates, schedule_limits) ==
             std::vector<std::uint32_t>{42U, 8U},
         "Source initializer incorrectly reused normal group selection/order");
}

void test_omitted_first_middle_last_and_entire_group() {
  const auto source = make_source();
  // Source member order is2,0,1: dropping source2 removes first, etc.
  const std::array<std::uint32_t, 3U> omitted{2U, 0U, 1U};
  const std::array<std::vector<std::uint32_t>, 3U> expected{
      std::vector<std::uint32_t>{17U, 42U},
      std::vector<std::uint32_t>{90U, 42U},
      std::vector<std::uint32_t>{90U, 17U}};
  for (std::size_t index = 0U; index < omitted.size(); ++index) {
    auto remap = make_remap();
    remap[omitted[index]] = std::nullopt;
    const auto groups =
        openrc::compile_rac_actor_activation_groups_v1(source, remap, kLimits);
    expect(groups[0U].member_authored_ids == expected[index] &&
               groups[1U].id == 1U && groups[1U].member_authored_ids.empty() &&
               groups[2U].member_authored_ids == std::vector<std::uint32_t>{8U},
           "Dropping a source member broke survivor order or another group");
  }
  const std::vector<std::optional<std::uint32_t>> none(4U);
  const auto empty =
      openrc::compile_rac_actor_activation_groups_v1(source, none, kLimits);
  expect(empty == std::vector<openrc::game::ActorActivationGroupV1>{{0U, {}},
                                                                    {1U, {}},
                                                                    {2U, {}}},
         "All-removed groups lost their source slots");
  auto boundary_ids = make_remap();
  boundary_ids[0U] = 0U;
  boundary_ids[1U] = std::numeric_limits<std::uint32_t>::max();
  const auto boundary = openrc::compile_rac_actor_activation_groups_v1(
      source, boundary_ids, kLimits);
  expect(
      boundary[0U].member_authored_ids ==
          std::vector<std::uint32_t>{90U, 0U,
                                     std::numeric_limits<std::uint32_t>::max()},
      "Compiler confused a valid neutral ID with the optional omission marker");
}

void test_mapping_and_source_validation() {
  const auto source = make_source();
  auto mapping = make_remap();
  mapping.pop_back();
  expect_error(
      [&] {
        static_cast<void>(openrc::compile_rac_actor_activation_groups_v1(
            source, mapping, kLimits));
      },
      "complete static table");
  mapping = make_remap();
  mapping.push_back(123U);
  expect_error(
      [&] {
        static_cast<void>(openrc::compile_rac_actor_activation_groups_v1(
            source, mapping, kLimits));
      },
      "complete static table");
  mapping = make_remap();
  mapping[3U] = mapping[0U];
  expect_error(
      [&] {
        static_cast<void>(openrc::compile_rac_actor_activation_groups_v1(
            source, mapping, kLimits));
      },
      "duplicate entity IDs");
  mapping = make_remap();
  const std::vector<std::pair<std::function<void(openrc::RacGameplayBankV1 &)>,
                              std::string_view>>
      corruptions{
          {[](auto &bank) { ++bank.static_moby_count; },
           "complete static table"},
          {[](auto &bank) { ++bank.moby_group_count; }, "count disagrees"},
          {[](auto &bank) { bank.moby_groups[1U].source_group_id = 0U; },
           "source order"},
          {[](auto &bank) { bank.moby_groups[2U].source_group_id = 4U; },
           "source order"},
          {[](auto &bank) {
             bank.moby_groups[0U].members[0U].static_moby_index = 4U;
           },
           "invalid source member"},
          {[](auto &bank) { bank.moby_groups[0U].members[0U].raw_value = 0U; },
           "invalid source member"},
          {[](auto &bank) {
             bank.moby_groups[0U].members[0U].raw_value |= 0x8000U;
           },
           "terminator is missing or early"},
          {[](auto &bank) {
             bank.moby_groups[0U].members.back().raw_value &= 0x7fffU;
           },
           "terminator is missing or early"},
          {[](auto &bank) {
             bank.moby_groups[0U].source_member_data_offset = -1;
           },
           "absent source slot contains members"},
          {[](auto &bank) {
             bank.moby_groups[0U].source_member_data_offset = 1;
           },
           "source member offset or length"},
          {[](auto &bank) {
             bank.moby_groups[0U].source_member_data_offset = 8;
           },
           "source member offset or length"},
          {[](auto &bank) {
             bank.moby_groups[0U].member_records_range.size += 2U;
           },
           "member records disagree"},
          {[](auto &bank) {
             ++bank.moby_groups[0U].members[0U].record_range.offset;
           },
           "invalid source member"},
          {[](auto &bank) { --bank.moby_group_table_records_range.size; },
           "header, table, and member data"},
          {[](auto &bank) {
             bank.moby_group_member_data_range.offset =
                 std::numeric_limits<std::uint64_t>::max();
           },
           "range exceeds the input bank"},
      };
  for (const auto &[corrupt, diagnostic] : corruptions) {
    auto invalid = source;
    corrupt(invalid);
    const auto before = source_group_image(invalid);
    expect_error(
        [&] {
          static_cast<void>(openrc::compile_rac_actor_activation_groups_v1(
              invalid, mapping, kLimits));
        },
        diagnostic);
    expect(source_group_image(invalid) == before,
           "Rejected source validation mutated its input");
  }
}

void test_limits_count_source_members_before_omission() {
  const auto source = make_source();
  const std::vector<std::optional<std::uint32_t>> none(4U);
  const std::array<openrc::RacActorScheduleCompileLimitsV1, 4U> bounded{
      openrc::RacActorScheduleCompileLimitsV1{3U, 8U, 8U, 32U},
      openrc::RacActorScheduleCompileLimitsV1{16U, 2U, 8U, 32U},
      openrc::RacActorScheduleCompileLimitsV1{16U, 8U, 2U, 32U},
      openrc::RacActorScheduleCompileLimitsV1{16U, 8U, 8U, 3U}};
  for (const auto limit : bounded) {
    expect_error(
        [&] {
          static_cast<void>(openrc::compile_rac_actor_activation_groups_v1(
              source, none, limit));
        },
        "caller limits");
  }
  expect_error(
      [&] {
        static_cast<void>(
            openrc::compile_rac_actor_activation_groups_v1(source, none, {}));
      },
      "non-zero limits");
}

void test_source_multiplicity_is_preserved() {
  auto source = make_source();
  source.moby_groups[0U].members[1U].static_moby_index = 2U;
  source.moby_groups[0U].members[1U].raw_value = 2U;
  source.moby_groups[2U].members[0U].static_moby_index = 1U;
  source.moby_groups[2U].members[0U].raw_value = 0x8001U;
  const auto groups = openrc::compile_rac_actor_activation_groups_v1(
      source, make_remap(), kLimits);
  expect(groups[0U].member_authored_ids ==
                 std::vector<std::uint32_t>{90U, 90U, 42U} &&
             groups[2U].member_authored_ids == std::vector<std::uint32_t>{42U},
         "Compiler silently deduplicated source group membership");
}

void test_aliases_reject_unsupported_in_place_remapping() {
  const auto source = make_source();
  auto mapping = make_remap();
  mapping[1U] = std::nullopt;
  for (const bool partial_alias : {false, true}) {
    auto aliased = source;
    auto &destination = aliased.moby_groups[2U];
    const auto &original = aliased.moby_groups[0U];
    destination.source_member_data_offset = partial_alias ? 2 : 0;
    destination.member_records_range = original.member_records_range;
    destination.members = original.members;
    if (partial_alias) {
      destination.member_records_range.offset += 2U;
      destination.member_records_range.size -= 2U;
      destination.members.erase(destination.members.begin());
    }
    const auto before = source_group_image(aliased);
    for (const auto &selected_mapping :
         {mapping, std::vector<std::optional<std::uint32_t>>(4U)}) {
      expect_error(
          [&] {
            static_cast<void>(openrc::compile_rac_actor_activation_groups_v1(
                aliased, selected_mapping, kLimits));
          },
          "overlapping source lists require in-place loader semantics");
    }
    expect(source_group_image(aliased) == before,
           "Rejecting aliased source lists mutated their original metadata");
  }
}

} // namespace

int main() {
  try {
    test_source_parser_remap_and_runtime_scheduler_integration();
    test_omitted_first_middle_last_and_entire_group();
    test_mapping_and_source_validation();
    test_limits_count_source_members_before_omission();
    test_source_multiplicity_is_preserved();
    test_aliases_reject_unsupported_in_place_remapping();
    std::cout << "RAC actor schedule compile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC actor schedule compile tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
