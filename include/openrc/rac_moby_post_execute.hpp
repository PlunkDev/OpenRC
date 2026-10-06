#pragma once

#include "openrc/ordered_spatial_index.hpp"
#include "openrc/rac_moby_rotation.hpp"

#include <optional>

namespace openrc {

// Explicit compiler-side binding to the level's existing index. A member is
// the original live-order token, never an inferred entity slot/authored ID.
// The bounded source domain requires the entire live-index word to fit u16;
// silently truncating a larger signed-LW value would change later removals.
struct RacMobyPostSpatialOwnerV1 {
  game::OrderedSpatialIndexV1 &index;
  std::uint64_t expected_revision;
  std::uint32_t source_actor_address_bits;
  std::uint32_t source_live_index_bits;
  std::uint16_t member_token;
};

struct RacMobyPostPackedBoundsWriteV1 {
  // Original tail's actor+a0 SW, including inactive and same-word stores.
  std::uint32_t packed_bits = 0U;
  [[nodiscard]] bool operator==(const RacMobyPostPackedBoundsWriteV1 &) const = default;
};

using RacMobyPostExecutedWriteV1 =
    std::variant<RacMobyPostWriteV1, RacMobyPostPackedBoundsWriteV1>;

struct RacMobyPostSpatialResultV1 {
  std::uint16_t member_token = 0U;
  std::uint32_t old_packed_bits = 0U;
  std::uint32_t new_packed_bits = 0U;
  std::optional<game::SpatialCellRectangleV1> old_rectangle;
  std::optional<game::SpatialCellRectangleV1> new_rectangle;
  std::uint64_t revision_before = 0U;
  std::uint64_t revision_after = 0U;
};

struct RacMobyPostExecutionResultV1 {
  RacMobyPostActorV1 actor;
  // Only actor stores, in original order. The complete index delta and final
  // actor word are published at one bounded success boundary; this vector
  // does not expose the source's intervening removal/store/add memory timing.
  std::vector<RacMobyPostExecutedWriteV1> writes;
  // Exactly one completion: a genuine earlier return, or completed251b58.
  std::optional<RacMobyPostReturnReasonV1> return_reason;
  std::optional<RacMobyPostSpatialResultV1> spatial;
  std::uint32_t rotation_calls = 0U;
  std::uint32_t blend_calls = 0U;
  std::uint32_t scale_calls = 0U;
  std::uint32_t derived_calls = 0U;
  std::uint64_t rotation_instruction_pairs = 0U;
  std::vector<DvpVuExecutionWarningV1> warnings;
  static constexpr bool physical_console_qualified = false;
};

// Complete the finite source251e30 value path through its actual typed stages.
// Numeric values use the existing ordered MUL/ACC reference qualification;
// there is no retained VU-state/timing or physical-console claim.
// A reached rotation requires the qualified source owner. A reached spatial
// tail requires an explicit binding, matching old membership, original64x64
// coordinates and a real mutation of that existing OrderedSpatialIndexV1.
// Neither owner is inspected when its stage is not reached.
//
// Actor and index work remain staged until every reached operation succeeds.
// Failure leaves the supplied actor and actual index unchanged. Success
// returns the complete actor and commits the index (if reached) using a
// nonthrowing move; callers publish that actor in their existing actor owner.
// max_recorded_writes includes the additional packed-bounds store. No fake
// spatial resume or callback-supplied successful numeric results are accepted.
[[nodiscard]] RacMobyPostExecutionResultV1 execute_rac_moby_post_v1(
    const RacMobyPostActorV1 &actor, const RacMobyPostBindingsV1 &bindings,
    const RacMobyRotationSourceV1 *rotation,
    const RacMobyPostSpatialOwnerV1 *spatial,
    RacMobyPostLimitsV1 limits);

} // namespace openrc
