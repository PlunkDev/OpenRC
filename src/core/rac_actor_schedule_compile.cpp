#include "openrc/rac_actor_schedule_compile.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RacActorScheduleCompileError(message);
}

void require_input_range(const RacGameplayRangeV1 range,
                         const std::uint64_t input_bytes) {
  if (range.offset > input_bytes || range.size > input_bytes - range.offset) {
    fail("RAC actor activation-group source range exceeds the input bank");
  }
}

void require_contained_range(const RacGameplayRangeV1 child,
                             const RacGameplayRangeV1 parent) {
  if (child.offset < parent.offset ||
      child.offset - parent.offset > parent.size ||
      child.size > parent.size - (child.offset - parent.offset)) {
    fail("RAC actor activation-group member range exceeds its copied data");
  }
}

void validate_source(
    const RacGameplayBankV1 &source,
    const std::span<const std::optional<std::uint32_t>> source_to_entity,
    const RacActorScheduleCompileLimitsV1 limits) {
  if (limits.max_source_instances == 0U || limits.max_groups == 0U ||
      limits.max_members_per_group == 0U ||
      limits.max_total_group_members == 0U) {
    fail("RAC actor activation-group compiler requires non-zero limits");
  }
  if (source.static_mobies.size() != source.static_moby_count ||
      source_to_entity.size() != source.static_moby_count) {
    fail("RAC actor activation-group source-to-entity map must cover the "
         "complete static table");
  }
  if (source.static_moby_count > limits.max_source_instances ||
      source.moby_groups.size() > limits.max_groups) {
    fail("RAC actor activation-group source tables exceed caller limits");
  }
  if (source.moby_groups.size() != source.moby_group_count) {
    fail("RAC actor activation-group count disagrees with its source table");
  }

  require_input_range(source.moby_group_header_range, source.input_bytes);
  require_input_range(source.moby_group_table_records_range,
                      source.input_bytes);
  require_input_range(source.moby_group_member_data_range, source.input_bytes);
  const auto expected_table_size =
      static_cast<std::uint64_t>(source.moby_group_count) *
      kRacGameplayMobyGroupTableRecordBytesV1;
  if (source.moby_group_header_range.size !=
          kRacGameplayMobyGroupHeaderBytesV1 ||
      source.moby_group_table_records_range.offset !=
          source.moby_group_header_range.offset +
              source.moby_group_header_range.size ||
      source.moby_group_table_records_range.size != expected_table_size ||
      source.moby_group_member_data_range.offset !=
          source.moby_group_table_records_range.offset + expected_table_size) {
    fail("RAC actor activation-group header, table, and member data are "
         "inconsistent");
  }

  std::uint64_t total_members = 0U;
  std::vector<RacGameplayRangeV1> member_ranges;
  member_ranges.reserve(source.moby_groups.size());
  for (std::size_t group_index = 0U; group_index < source.moby_groups.size();
       ++group_index) {
    const auto &group = source.moby_groups[group_index];
    const RacGameplayRangeV1 expected_record{
        source.moby_group_table_records_range.offset +
            static_cast<std::uint64_t>(group_index) *
                kRacGameplayMobyGroupTableRecordBytesV1,
        kRacGameplayMobyGroupTableRecordBytesV1};
    if (group.source_group_id != group_index ||
        group.table_record_range != expected_record) {
      fail("RAC actor activation-group IDs or table records are not in source "
           "order");
    }
    if (group.members.size() > limits.max_members_per_group ||
        group.members.size() > limits.max_total_group_members - total_members ||
        group.members.size() > std::numeric_limits<std::uint64_t>::max() /
                                   kRacGameplayMobyGroupMemberBytesV1) {
      fail("RAC actor activation-group source members exceed caller limits");
    }
    total_members += group.members.size();
    if (group.source_member_data_offset < 0) {
      if (!group.members.empty() ||
          group.member_records_range != RacGameplayRangeV1{}) {
        fail("RAC actor activation-group absent source slot contains members");
      }
      continue;
    }
    const auto relative_offset =
        static_cast<std::uint64_t>(group.source_member_data_offset);
    const auto expected_member_bytes =
        static_cast<std::uint64_t>(group.members.size()) *
        kRacGameplayMobyGroupMemberBytesV1;
    if ((relative_offset & 1U) != 0U || group.members.empty() ||
        relative_offset > source.moby_group_member_data_range.size ||
        expected_member_bytes >
            source.moby_group_member_data_range.size - relative_offset) {
      fail("RAC actor activation-group source member offset or length is "
           "invalid");
    }
    require_contained_range(group.member_records_range,
                            source.moby_group_member_data_range);
    if (group.member_records_range.offset !=
            source.moby_group_member_data_range.offset + relative_offset ||
        group.member_records_range.size != expected_member_bytes) {
      fail("RAC actor activation-group member records disagree with the source "
           "list");
    }
    member_ranges.push_back(group.member_records_range);
    for (std::size_t member_index = 0U; member_index < group.members.size();
         ++member_index) {
      const auto &member = group.members[member_index];
      const RacGameplayRangeV1 expected_member{
          group.member_records_range.offset +
              static_cast<std::uint64_t>(member_index) *
                  kRacGameplayMobyGroupMemberBytesV1,
          kRacGameplayMobyGroupMemberBytesV1};
      if (member.record_range != expected_member ||
          member.static_moby_index >= source.static_moby_count ||
          member.static_moby_index != (member.raw_value & 0x7fffU)) {
        fail("RAC actor activation-group references an invalid source member");
      }
      const bool is_final = member_index + 1U == group.members.size();
      if (((member.raw_value & 0x8000U) != 0U) != is_final) {
        fail(
            "RAC actor activation-group source terminator is missing or early");
      }
    }
  }

  // The original loader compacts the copied shared payload in place. An
  // overlapping later source list could observe already-remapped values or a
  // moved terminator; applying the map independently would silently diverge.
  std::sort(member_ranges.begin(), member_ranges.end(),
            [](const RacGameplayRangeV1 left, const RacGameplayRangeV1 right) {
              return left.offset < right.offset;
            });
  for (std::size_t index = 1U; index < member_ranges.size(); ++index) {
    const auto &previous = member_ranges[index - 1U];
    if (member_ranges[index].offset < previous.offset + previous.size) {
      fail("RAC actor activation-group overlapping source lists require "
           "in-place loader semantics");
    }
  }

  std::vector<std::uint32_t> present_ids;
  present_ids.reserve(source_to_entity.size());
  for (const auto &mapped : source_to_entity) {
    if (mapped) {
      present_ids.push_back(*mapped);
    }
  }
  std::sort(present_ids.begin(), present_ids.end());
  if (std::adjacent_find(present_ids.begin(), present_ids.end()) !=
      present_ids.end()) {
    fail("RAC actor activation-group source-to-entity map contains duplicate "
         "entity IDs");
  }
}

} // namespace

std::vector<game::ActorActivationGroupV1>
compile_rac_actor_activation_groups_v1(
    const RacGameplayBankV1 &source,
    const std::span<const std::optional<std::uint32_t>> source_to_entity,
    const RacActorScheduleCompileLimitsV1 limits) {
  validate_source(source, source_to_entity, limits);
  std::vector<game::ActorActivationGroupV1> groups;
  groups.reserve(source.moby_groups.size());
  for (const auto &source_group : source.moby_groups) {
    game::ActorActivationGroupV1 group;
    group.id = source_group.source_group_id;
    group.member_authored_ids.reserve(source_group.members.size());
    for (const auto &source_member : source_group.members) {
      const auto &mapped = source_to_entity[source_member.static_moby_index];
      if (mapped) {
        group.member_authored_ids.push_back(*mapped);
      }
    }
    groups.push_back(std::move(group));
  }
  return groups;
}

} // namespace openrc
