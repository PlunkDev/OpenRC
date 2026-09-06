#pragma once

#include "openrc/placement_admission.hpp"
#include "openrc/rac_moby_admission.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace openrc {

// Compiler-only mapping of an explicitly supplied source-index window to a
// neutral view. Does not supply values or infer a new-game/reload lifecycle.
struct RacAdmissionStateWindowV1 {
  std::string view_key;
  std::uint64_t first_element = 0U;
  std::uint64_t element_count = 0U;
  std::int32_t first_source_index = 0;
};

struct RacMobyAdmissionStateBindingsV1 {
  PreparedContentDigestV1 state_schema_sha256{};
  std::optional<RacAdmissionStateWindowV1> selector_bytes;
  std::optional<RacAdmissionStateWindowV1> suppression_bytes;
  // Bit-word and registration rows begin at source index zero.
  std::optional<RacAdmissionStateWindowV1> primary_bit_words;
  std::optional<RacAdmissionStateWindowV1> alternate_bit_words;
  std::optional<RacAdmissionStateWindowV1> registration_keys;
};

struct RacMobyAdmissionCompilationV1 {
  PlacementAdmissionPlanV1 plan;
  // Existing source constructor scalars, not inputs to neutral execution.
  std::uint16_t constructor_key_bits = 0U;
  std::uint8_t constructor_selector_bits = 0U;
  std::uint8_t unregistered_slot_bits = 0xfeU;
};

// Translates branch precedence, source signed-index resolution and full-s32
// registration matching once on the compiler side. Unsupported/missing reached
// operands remain explicitly unavailable in the plan; they do not cause eager
// reads or invented zero values. Native execution sees no raw policy word.
[[nodiscard]] RacMobyAdmissionCompilationV1
compile_rac_moby_admission_v1(const RacGameplayMobyAdmissionV1 &source,
                              const RacMobyAdmissionStateBindingsV1 &bindings,
                              PlacementAdmissionLimitsV1 plan_limits,
                              RacMobyAdmissionLimitsV1 source_limits = {});

} // namespace openrc
