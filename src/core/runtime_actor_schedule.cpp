#include "openrc/runtime_actor_schedule.hpp"

#include <algorithm>
#include <cstddef>
#include <unordered_map>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const char *const message) {
  throw ActorUpdateScheduleError(message);
}

void validate_limits(const ActorUpdateScheduleLimitsV1 limits) {
  if (limits.max_candidates == 0U || limits.max_groups == 0U ||
      limits.max_members_per_group == 0U ||
      limits.max_total_group_members == 0U ||
      limits.max_order_buckets == 0U || limits.max_auxiliary_entries == 0U) {
    fail("Actor update schedule requires explicit positive limits");
  }
}

using CandidateIndex = std::unordered_map<std::uint32_t, std::size_t>;

[[nodiscard]] CandidateIndex index_candidates(
    const std::span<const ActorUpdateCandidateV1> candidates,
    const ActorUpdateScheduleLimitsV1 limits) {
  validate_limits(limits);
  if (candidates.size() > limits.max_candidates) {
    fail("Actor update schedule exceeds its candidate limit");
  }
  CandidateIndex result;
  result.reserve(candidates.size());
  for (std::size_t index = 0U; index < candidates.size(); ++index) {
    if (!result.emplace(candidates[index].authored_id, index).second) {
      fail("Actor update schedule repeats a candidate identity");
    }
  }
  return result;
}

[[nodiscard]] std::size_t find_group(
    const std::span<const ActorActivationGroupV1> groups,
    const std::uint32_t id) {
  const auto found = std::lower_bound(
      groups.begin(), groups.end(), id,
      [](const ActorActivationGroupV1 &group, const std::uint32_t key) {
        return group.id < key;
      });
  if (found == groups.end() || found->id != id) {
    fail("Actor update candidate references a missing activation group");
  }
  return static_cast<std::size_t>(found - groups.begin());
}

} // namespace

std::vector<std::uint32_t> build_actor_initialization_schedule_v1(
    const std::span<const ActorUpdateCandidateV1> candidates,
    const ActorUpdateScheduleLimitsV1 limits) {
  static_cast<void>(index_candidates(candidates, limits));
  std::vector<std::uint32_t> result;
  result.reserve(candidates.size());
  for (const auto &candidate : candidates) {
    if (candidate.live && !candidate.skip_direct_update) {
      result.push_back(candidate.authored_id);
    }
  }
  return result;
}

ActorUpdateScheduleV1 build_grouped_actor_update_schedule_v1(
    const std::span<const ActorUpdateCandidateV1> candidates,
    const std::span<const ActorActivationGroupV1> groups,
    const std::span<const ActorUpdateRangeEligibilityV1> range_eligibility,
    const ActorUpdateScheduleLimitsV1 limits) {
  const auto indices = index_candidates(candidates, limits);
  if (groups.size() > limits.max_groups ||
      range_eligibility.size() > candidates.size()) {
    fail("Actor update schedule exceeds its group or range-result limit");
  }
  std::uint64_t total_members = 0U;
  for (std::size_t index = 0U; index < groups.size(); ++index) {
    const auto &group = groups[index];
    if (index != 0U && groups[index - 1U].id >= group.id) {
      fail("Actor activation groups are not in unique ascending ID order");
    }
    if (group.member_authored_ids.size() > limits.max_members_per_group ||
        group.member_authored_ids.size() >
            limits.max_total_group_members - total_members) {
      fail("Actor update schedule exceeds its group-member limits");
    }
    total_members += group.member_authored_ids.size();
    for (const auto authored_id : group.member_authored_ids) {
      if (!indices.contains(authored_id)) {
        fail("Actor activation group references a missing candidate");
      }
    }
  }

  std::vector<std::optional<bool>> ranges(candidates.size());
  for (const auto &entry : range_eligibility) {
    const auto found = indices.find(entry.authored_id);
    if (found == indices.end()) {
      fail("Actor range result references a missing candidate");
    }
    auto &value = ranges[found->second];
    if (value.has_value()) {
      fail("Actor range results repeat a candidate identity");
    }
    value = entry.eligible;
  }

  // Validate relationships independently of eligibility so invalid content
  // cannot become accepted merely because the reference actor is far away.
  std::vector<std::optional<std::size_t>> candidate_groups(candidates.size());
  for (std::size_t index = 0U; index < candidates.size(); ++index) {
    const auto &candidate = candidates[index];
    if (candidate.order_bucket >= limits.max_order_buckets) {
      fail("Actor update candidate exceeds the order-bucket limit");
    }
    if (candidate.activation_group_id) {
      candidate_groups[index] = find_group(groups, *candidate.activation_group_id);
    }
  }

  ActorUpdateScheduleV1 result;
  result.auxiliary_authored_ids.reserve(std::min<std::size_t>(
      candidates.size(), limits.max_auxiliary_entries));
  std::vector<std::size_t> selected;
  selected.reserve(candidates.size());
  std::vector<bool> already_selected(candidates.size(), false);
  std::vector<bool> activated(groups.size(), false);
  const auto append = [&](const std::size_t index) {
    if (already_selected[index]) {
      fail("Actor update schedule selects a candidate more than once");
    }
    const auto &candidate = candidates[index];
    already_selected[index] = true;
    selected.push_back(index);
    if (candidate.auxiliary_pass) {
      if (result.auxiliary_authored_ids.size() >= limits.max_auxiliary_entries) {
        fail("Actor update schedule exceeds its auxiliary-pass limit");
      }
      result.auxiliary_authored_ids.push_back(candidate.authored_id);
    }
  };

  for (std::size_t index = 0U; index < candidates.size(); ++index) {
    const auto &candidate = candidates[index];
    if (!candidate.live || candidate.skip_direct_update) {
      continue;
    }
    if (!candidate.bypass_update_range) {
      if (!ranges[index].has_value()) {
        fail("Actor update schedule is missing a required range result");
      }
      if (!*ranges[index]) {
        continue;
      }
    }
    if (candidate_groups[index]) {
      activated[*candidate_groups[index]] = true;
    } else {
      append(index);
    }
  }
  for (std::size_t index = 0U; index < groups.size(); ++index) {
    if (!activated[index]) {
      continue;
    }
    for (const auto authored_id : groups[index].member_authored_ids) {
      const auto member = indices.at(authored_id);
      if (candidates[member].live) {
        append(member);
      }
    }
  }

  std::stable_sort(selected.begin(), selected.end(),
                   [&](const std::size_t left, const std::size_t right) {
                     return candidates[left].order_bucket <
                            candidates[right].order_bucket;
                   });
  result.ordered_authored_ids.reserve(selected.size());
  for (const auto index : selected) {
    result.ordered_authored_ids.push_back(candidates[index].authored_id);
  }
  return result;
}

} // namespace openrc::game
