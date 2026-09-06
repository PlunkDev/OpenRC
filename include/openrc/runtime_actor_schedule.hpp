#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc::game {

// Source-compiled identities in live-slot scan order. This is deliberately
// separate from canonical authored-ID order in package tables and snapshots.
// Mutable eligibility is supplied for one source update, not cached at mount.
// The caller supplies only slots before the source scan terminator; live=false
// represents an inactive slot, not an end-of-array sentinel.
struct ActorUpdateCandidateV1 {
  std::uint32_t authored_id = 0U;
  std::uint32_t order_bucket = 0U;
  std::optional<std::uint32_t> activation_group_id;
  bool live = true;
  bool skip_direct_update = false;
  bool bypass_update_range = false;
  bool auxiliary_pass = false;

  [[nodiscard]] bool operator==(const ActorUpdateCandidateV1 &) const = default;
};

struct ActorActivationGroupV1 {
  std::uint32_t id = 0U;
  // Source group-list order, which need not match live-slot or authored order.
  std::vector<std::uint32_t> member_authored_ids;

  [[nodiscard]] bool operator==(const ActorActivationGroupV1 &) const = default;
};

// Supplied by the recovered world/range evaluation for this source update.
// There is no implicit proximity calculation or all-in-range fallback here.
struct ActorUpdateRangeEligibilityV1 {
  std::uint32_t authored_id = 0U;
  bool eligible = false;

  [[nodiscard]] bool
  operator==(const ActorUpdateRangeEligibilityV1 &) const = default;
};

struct ActorUpdateScheduleLimitsV1 {
  std::uint32_t max_candidates = 0U;
  std::uint32_t max_groups = 0U;
  std::uint32_t max_members_per_group = 0U;
  std::uint64_t max_total_group_members = 0U;
  std::uint32_t max_order_buckets = 0U;
  std::uint32_t max_auxiliary_entries = 0U;
};

struct ActorUpdateScheduleV1 {
  // Bucket order, then direct scan order, then activated-group/member order.
  std::vector<std::uint32_t> ordered_authored_ids;
  // Direct scan followed by group/member order, before bucket concatenation.
  // Kept separate because the source auxiliary pass uses a different order.
  std::vector<std::uint32_t> auxiliary_authored_ids;

  [[nodiscard]] bool operator==(const ActorUpdateScheduleV1 &) const = default;
};

class ActorUpdateScheduleError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Selection only. Initialization ignores groups, order buckets, distance and
// the auxiliary flag: every live, non-skipped slot is selected in scan order.
// It does not execute behavior or the source animation pre/post steps.
[[nodiscard]] std::vector<std::uint32_t>
build_actor_initialization_schedule_v1(
    std::span<const ActorUpdateCandidateV1> candidates,
    ActorUpdateScheduleLimitsV1 limits);

// Grouped source policy: eligible direct candidates activate their group or
// enter their bucket. Activated groups are processed in ascending group ID.
// Every live member is then included even if that member fails the direct
// skip/range tests. All append operations precede stable bucket ordering.
// Groups must be in ascending ID order. Candidate and range-result IDs must
// be unique; group references must resolve, and duplicate scheduled actors
// fail rather than producing a cyclic source list. Missing required range
// results fail. Inputs are never mutated and no callbacks are invoked.
//
// The execution owner must still recheck live state before each actor and
// examine current pre/post flags around its callback, as earlier callbacks
// may change them. This list is not a precomputed animation/behavior journal.
[[nodiscard]] ActorUpdateScheduleV1 build_grouped_actor_update_schedule_v1(
    std::span<const ActorUpdateCandidateV1> candidates,
    std::span<const ActorActivationGroupV1> groups,
    std::span<const ActorUpdateRangeEligibilityV1> range_eligibility,
    ActorUpdateScheduleLimitsV1 limits);

} // namespace openrc::game
