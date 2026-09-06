#include "openrc/runtime_actor_schedule.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace openrc::game;

constexpr ActorUpdateScheduleLimitsV1 kLimits{256U, 112U, 256U, 4096U, 224U,
                                            256U};

void expect(const bool condition, const char *const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_error(const std::function<void()> &action,
                  const std::string_view fragment) {
  try {
    action();
  } catch (const ActorUpdateScheduleError &error) {
    if (std::string_view(error.what()).find(fragment) == std::string_view::npos) {
      throw std::runtime_error("Unexpected schedule diagnostic: " +
                               std::string(error.what()));
    }
    return;
  }
  throw std::runtime_error("Invalid actor schedule was accepted");
}

void test_source_group_permutation_differs_from_initialization() {
  // Structural placement/group facts recovered from the Veldin bank. This
  // tests the conditional all-live/all-in-range case, not a claim about the
  // initial camera, save flags or source-to-live slot mapping.
  std::vector<ActorUpdateCandidateV1> candidates;
  std::vector<ActorUpdateRangeEligibilityV1> ranges;
  std::vector<std::uint32_t> ascending;
  for (std::uint32_t id = 143U; id <= 158U; ++id) {
    candidates.push_back({id, 3U, {}, true, false, false, false});
    ranges.push_back({id, true});
    ascending.push_back(id);
  }
  candidates[0U].activation_group_id = 0U;
  candidates[1U].activation_group_id = 0U;
  candidates[2U].activation_group_id = 1U;
  candidates[3U].activation_group_id = 1U;
  candidates[4U].activation_group_id = 1U;
  candidates[6U].activation_group_id = 2U;
  candidates[7U].activation_group_id = 2U;
  const std::vector<ActorActivationGroupV1> groups{
      {0U, {143U, 144U}}, {1U, {145U, 146U, 147U}}, {2U, {149U, 150U}}};
  expect(build_actor_initialization_schedule_v1(candidates, kLimits) == ascending,
         "initialization applied normal-update group ordering");
  const auto normal =
      build_grouped_actor_update_schedule_v1(candidates, groups, ranges, kLimits);
  expect(normal.ordered_authored_ids ==
             std::vector<std::uint32_t>{148U, 151U, 152U, 153U, 154U, 155U,
                                        156U, 157U, 158U, 143U, 144U, 145U,
                                        146U, 147U, 149U, 150U} &&
             normal.auxiliary_authored_ids.empty(),
         "normal scheduling lost the recovered Veldin group permutation");

  // A live-slot remap can change scan order without changing authored IDs.
  std::reverse(candidates.begin(), candidates.end());
  std::reverse(ascending.begin(), ascending.end());
  expect(build_actor_initialization_schedule_v1(candidates, kLimits) == ascending,
         "initialization sorted source live slots by authored identity");
  const auto remapped =
      build_grouped_actor_update_schedule_v1(candidates, groups, ranges, kLimits);
  expect(remapped.ordered_authored_ids ==
             std::vector<std::uint32_t>{158U, 157U, 156U, 155U, 154U, 153U,
                                        152U, 151U, 148U, 143U, 144U, 145U,
                                        146U, 147U, 149U, 150U},
         "group list order changed when the direct slot order changed");
}

void test_groups_awaken_live_siblings_and_auxiliary_order_is_distinct() {
  const std::vector<ActorUpdateCandidateV1> candidates{
      {10U, 2U, {}, true, false, true, true},
      {20U, 0U, {}, true, false, false, true},
      {30U, 2U, 5U, true, false, false, false},
      {40U, 1U, 2U, true, false, false, true},
      {50U, 1U, 5U, true, true, false, true},
      {51U, 1U, 5U, true, false, false, false},
      {60U, 1U, 2U, false, false, false, true},
  };
  const std::vector<ActorActivationGroupV1> groups{
      {2U, {40U, 60U}}, {5U, {50U, 51U, 30U}}};
  const std::vector<ActorUpdateRangeEligibilityV1> ranges{
      {10U, false}, {20U, true}, {30U, true}, {40U, true}, {51U, false}};
  const auto schedule =
      build_grouped_actor_update_schedule_v1(candidates, groups, ranges, kLimits);
  expect(schedule.ordered_authored_ids ==
             std::vector<std::uint32_t>{20U, 40U, 50U, 51U, 10U, 30U},
         "groups failed to awaken skipped/out-of-range live siblings or "
         "scheduled an inactive member");
  expect(schedule.auxiliary_authored_ids ==
             std::vector<std::uint32_t>{10U, 20U, 40U, 50U},
         "auxiliary pass inherited callback bucket order");
  expect(build_actor_initialization_schedule_v1(candidates, kLimits) ==
             std::vector<std::uint32_t>{10U, 20U, 30U, 40U, 51U},
         "initialization used group activation or update distance");
}

void test_range_results_are_explicit_and_source_scan_does_not_shortcut() {
  std::vector<ActorUpdateCandidateV1> candidates{
      {1U, 0U, 0U, true, false, false, false},
      {2U, 0U, 0U, true, false, false, false}};
  const std::vector<ActorActivationGroupV1> groups{{0U, {1U, 2U}}};
  std::vector<ActorUpdateRangeEligibilityV1> ranges{{1U, true}};
  const auto candidates_before = candidates;
  const auto groups_before = groups;
  expect_error(
      [&] {
        static_cast<void>(build_grouped_actor_update_schedule_v1(
            candidates, groups, ranges, kLimits));
      },
      "missing a required range result");
  expect(candidates == candidates_before && groups == groups_before,
         "failed selection changed its source inputs");

  ranges.push_back({2U, false});
  expect(build_grouped_actor_update_schedule_v1(candidates, groups, ranges,
                                                kLimits)
                 .ordered_authored_ids ==
             std::vector<std::uint32_t>{1U, 2U},
         "an out-of-range sibling was not awakened by its group");
  ranges[0U].eligible = false;
  expect(build_grouped_actor_update_schedule_v1(candidates, groups, ranges,
                                                kLimits)
             .ordered_authored_ids.empty(),
         "an entirely out-of-range group was activated");
  candidates[0U].bypass_update_range = true;
  candidates[0U].skip_direct_update = true;
  candidates[1U].live = false;
  expect(build_grouped_actor_update_schedule_v1(candidates, groups, {}, kLimits)
             .ordered_authored_ids.empty(),
         "range bypass overrode source skip or live-state guards");
  candidates[0U].skip_direct_update = false;
  expect(build_grouped_actor_update_schedule_v1(candidates, groups, {}, kLimits)
                 .ordered_authored_ids == std::vector<std::uint32_t>{1U},
         "range bypass required an unused distance result");
}

void test_malformed_ids_lists_and_missing_references_fail() {
  const std::vector<ActorUpdateCandidateV1> candidates{
      {1U, 0U, 0U, true, false, true, false},
      {2U, 0U, {}, true, false, true, false}};
  const std::vector<ActorActivationGroupV1> groups{{0U, {1U}}};
  auto duplicate = candidates;
  duplicate[1U].authored_id = 1U;
  expect_error(
      [&] {
        static_cast<void>(build_actor_initialization_schedule_v1(duplicate,
                                                                 kLimits));
      },
      "repeats a candidate identity");
  expect_error(
      [&] {
        static_cast<void>(build_grouped_actor_update_schedule_v1(
            candidates, {}, {}, kLimits));
      },
      "missing activation group");
  const auto check_groups = [&](const std::vector<ActorActivationGroupV1> &value,
                                const std::string_view message) {
    expect_error(
        [&] {
          static_cast<void>(build_grouped_actor_update_schedule_v1(
              candidates, value, {}, kLimits));
        },
        message);
  };
  check_groups({{1U, {}}, {0U, {1U}}}, "ascending ID order");
  check_groups({{0U, {1U}}, {0U, {2U}}}, "ascending ID order");
  check_groups({{0U, {999U}}}, "missing candidate");
  check_groups({{0U, {1U, 1U}}}, "more than once");
  check_groups({{0U, {1U, 2U}}}, "more than once");
  for (const auto &value :
       std::vector<std::vector<ActorUpdateRangeEligibilityV1>>{
           {{1U, true}, {1U, false}}, {{999U, true}}}) {
    expect_error(
        [&] {
          static_cast<void>(build_grouped_actor_update_schedule_v1(
              candidates, groups, value, kLimits));
        },
        value.size() == 2U ? "repeat a candidate" : "missing candidate");
  }

  auto bad_bucket = candidates;
  bad_bucket[1U].live = false;
  bad_bucket[1U].order_bucket = kLimits.max_order_buckets;
  expect_error(
      [&] {
        static_cast<void>(build_grouped_actor_update_schedule_v1(
            bad_bucket, groups, {}, kLimits));
      },
      "order-bucket limit");
  // The initializer does not read bucket or activation-group metadata.
  bad_bucket[0U].order_bucket = std::numeric_limits<std::uint32_t>::max();
  bad_bucket[0U].activation_group_id = 999U;
  expect(build_actor_initialization_schedule_v1(bad_bucket, kLimits) ==
             std::vector<std::uint32_t>{1U},
         "initialization inspected metadata unused by its source pass");
}

void test_allocation_and_output_limits() {
  const std::vector<ActorUpdateCandidateV1> candidates{
      {1U, 2U, 0U, true, false, true, true},
      {2U, 2U, 0U, true, false, true, true}};
  const std::vector<ActorActivationGroupV1> groups{{0U, {1U, 2U}}, {1U, {}}};
  const auto check = [&](const ActorUpdateScheduleLimitsV1 limits,
                         const std::string_view message) {
    expect_error(
        [&] {
          static_cast<void>(build_grouped_actor_update_schedule_v1(
              candidates, groups, {}, limits));
        },
        message);
  };
  auto limits = kLimits;
  limits.max_candidates = 0U;
  check(limits, "explicit positive limits");
  limits = kLimits;
  limits.max_candidates = 1U;
  check(limits, "candidate limit");
  limits = kLimits;
  limits.max_groups = 1U;
  check(limits, "group or range-result limit");
  limits = kLimits;
  limits.max_members_per_group = 1U;
  check(limits, "group-member limits");
  limits = kLimits;
  limits.max_total_group_members = 1U;
  check(limits, "group-member limits");
  limits = kLimits;
  limits.max_order_buckets = 2U;
  check(limits, "order-bucket limit");
  limits = kLimits;
  limits.max_auxiliary_entries = 1U;
  check(limits, "auxiliary-pass limit");
  expect(build_grouped_actor_update_schedule_v1({}, {}, {}, kLimits) ==
             ActorUpdateScheduleV1{} &&
             build_actor_initialization_schedule_v1({}, kLimits).empty(),
         "an empty scene created an actor update");
}

} // namespace

int main() {
  try {
    test_source_group_permutation_differs_from_initialization();
    test_groups_awaken_live_siblings_and_auxiliary_order_is_distinct();
    test_range_results_are_explicit_and_source_scan_does_not_shortcut();
    test_malformed_ids_lists_and_missing_references_fail();
    test_allocation_and_output_limits();
    std::cout << "runtime actor-schedule tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime actor-schedule tests failed: " << error.what() << '\n';
    return 1;
  }
}
