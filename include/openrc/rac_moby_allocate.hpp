#pragma once

#include "openrc/placement_admission.hpp"
#include "openrc/rac_moby_fresh_constructor.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace openrc {

// Compiler-only CURRENT source cursor, resolved anew by the staged loader
// owner. These numbers never become pointers or fields in a runtime package.
// The end is exclusive for selection but names the final physical guard slot.
struct RacMobyAllocateCursorV1 {
  std::uint32_t dynamic_begin_bits = 0U;
  std::uint32_t exclusive_end_bits = 0U;
};

struct RacMobyAllocateBindingsV1 {
  PreparedContentDigestV1 state_schema_sha256{};
  RacMobyAllocateCursorV1 cursor;
  // Separate canonical arrays, not a raw source actor image. Each covers
  // ALL physical slots including the final guard: one u8 status and two
  // contiguous u32 words (low, high) of the +38 union per physical slot.
  std::optional<PlacementStateElementV1> first_status;
  std::optional<PlacementStateElementV1> first_packed_word;
  std::optional<PlacementStateElementV1> current_timer_word;
  std::optional<PlacementStateElementV1> current_count_word;
  // Required only after selection; complete 32-u32 PVar blocks for every
  // physical slot, guard included. Values must be current, never implicit0.
  std::optional<PlacementStateElementV1> first_pvar_word;
  std::optional<std::uint32_t> all_actor_base_bits;
  std::optional<std::uint32_t> pvar_base_bits;
};

// Current resolved class/model/sequence reads for this actual call. Missing
// input means unresolved, not a null model. The fresh constructor's live
// index is computed here from the distinct all-actor base, not caller guessed.
// Source dependencies must belong to disjoint class/model/sequence owners;
// raw references remain compiler-only and callbacks are not invoked here.
struct RacMobyAllocateClassV1 {
  std::uint32_t class_id_bits = 0U;
  std::uint8_t class_table_index = 0U;
  std::uint32_t callback_reference = 0U;
  std::optional<RacMobyFreshModelV1> model;
};

struct RacMobyAllocateLimitsV1 {
  std::uint32_t max_key_bytes = 0U;
  std::uint32_t max_physical_slots = 0U;
};

struct RacMobyAllocatedFreshV1 {
  std::uint32_t physical_slot_index = 0U;
  std::uint8_t previous_status = 0U;
  bool advanced_ff_marker = false;
  RacMobyFreshConstructorStateV1 constructor;
  // Neutral binding for the selected, exactly128-byte cleared PVar block.
  // Never the source +78 pointer and never an EntityId slot/lifetime claim.
  PlacementStateElementV1 first_pvar_word;
  std::uint32_t previous_count_bits = 0U;
  std::uint32_t remaining_count_bits = 0U;
  [[nodiscard]] bool
  operator==(const RacMobyAllocatedFreshV1 &) const = default;
};

struct RacMobyAllocateResultV1 {
  std::uint32_t examined_slots = 0U;
  // One LWU per status>=fe candidate; exhaustion adds the diagnostic LW.
  std::uint32_t timer_reads = 0U;
  std::optional<RacMobyAllocatedFreshV1> fresh;
  [[nodiscard]] bool
  operator==(const RacMobyAllocateResultV1 &) const = default;
};

class RacMobyAllocateError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Complete scalar allocator graph as a COMPILER-SIDE STAGED adapter, not a
// second entity world or a runtime allocation/publication API. Caller owns
// this staged SessionState and must install the complete returned fresh value
// and neutral PVar binding before publishing ANY resulting world/session.
// Fresh source references still require proper lowering by that owner.
//
// In source order: scan status; compare zeroextended timer32 with full64 +38;
// statusff writes next statusff; execute the real fresh constructor; bind +78;
// clear exactly32 PVar words; read current count and decrement only if nonzero.
// Source empty/exhausted pool returns no actor; its actual diagnostic callee
// is a stack-only stub and causes no invented logging/global state effects.
//
// Native writes to the supplied staged canonical metadata/PVar commit as one
// batch after all reached work succeeds; this atomic error policy is separate
// from original instruction timing. Unreached optional inputs need not exist.
// Whole reached ranges (including the guard), type/stride, schema/revision,
// independent ownership (also source pools versus fixed scalar globals) and
// address-domain bounds are checked. Unsupported
// wrapping owners or source signed-ADDI clear traps fail, never allocate via
// a normalized index. Missing constructor bindings never mean success.
[[nodiscard]] RacMobyAllocateResultV1 execute_rac_moby_allocate_v1(
    const RacMobyAllocateBindingsV1 &bindings,
    game::SessionStateV1 &staged_state, std::uint64_t expected_revision,
    const std::optional<RacMobyAllocateClassV1> &resolved_class,
    RacMobyAllocateLimitsV1 limits);

} // namespace openrc
