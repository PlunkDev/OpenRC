#pragma once

#include "openrc/rac_gameplay_bank.hpp"
#include "openrc/runtime_actor_schedule.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

struct RacActorScheduleCompileLimitsV1 {
  std::uint64_t max_source_instances = 0U;
  std::uint64_t max_groups = 0U;
  std::uint64_t max_members_per_group = 0U;
  std::uint64_t max_total_group_members = 0U;
};

class RacActorScheduleCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compiler-only remap/compaction of source activation groups. Mapping entries
// correspond to the complete authored static-Moby table in source order.
// Absence means source loader admission omitted that placement; present values
// are caller-assigned neutral entity IDs and must be unique across the map.
// The caller must recover admission policy and provide this mapping explicitly:
// there is no default authored-ordinal/live-slot/entity-ID equivalence.
//
// Source group IDs and surviving member order are preserved, including absent
// and all-removed empty group slots. Source repeated membership is retained;
// relationships to candidates and schedule conflicts are checked by the neutral
// scheduler. This adapter does not execute save/progress guards or behavior.
// Overlapping source member-list byte ranges are rejected: the original
// loader's in-place writes can change later aliased lists, which requires a
// separate source-memory mutation contract rather than independent remapping.
[[nodiscard]] std::vector<game::ActorActivationGroupV1>
compile_rac_actor_activation_groups_v1(
    const RacGameplayBankV1 &source,
    std::span<const std::optional<std::uint32_t>> source_to_entity,
    RacActorScheduleCompileLimitsV1 limits);

} // namespace openrc
