#pragma once

#include "openrc/session_state.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

namespace openrc {

inline constexpr std::uint32_t kPlacementAdmissionSchemaVersionV1 = 1U;

// Compiler-resolved neutral element, never a source address or signed index.
struct PlacementStateElementV1 {
  std::string view_key;
  std::uint64_t element_index = 0U;
  [[nodiscard]] bool
  operator==(const PlacementStateElementV1 &) const = default;
};

// Missing element means unavailable input, not zero. Only reached accesses
// require it. The containing operation itself may independently be absent.
struct PlacementByteReadV1 {
  std::optional<PlacementStateElementV1> element;
  [[nodiscard]] bool operator==(const PlacementByteReadV1 &) const = default;
};

struct PlacementBitReadV1 {
  std::optional<PlacementStateElementV1> word;
  std::uint8_t bit_index = 0U;
  [[nodiscard]] bool operator==(const PlacementBitReadV1 &) const = default;
};

struct PlacementReverseRegistrationV1 {
  // A u16 view; row.element_index is the first slot. Scan count-1 down to 0.
  std::optional<PlacementStateElementV1> row;
  std::uint32_t slot_count = 0U;
  // No exact match still permits the first zero-valued slot.
  std::optional<std::uint16_t> exact_match;
  std::uint16_t inserted_value = 0U;
  [[nodiscard]] bool
  operator==(const PlacementReverseRegistrationV1 &) const = default;
};

struct PlacementCountAdjustmentV1 {
  std::uint32_t maximum_bits = 0U;
  // On a set bit: t = maximum+1, a = t+(t>>31), result = arithmetic a>>1,
  // with explicit wrapping u32 operations. Otherwise current = maximum.
  PlacementBitReadV1 adjust_when_set;
  [[nodiscard]] bool
  operator==(const PlacementCountAdjustmentV1 &) const = default;
};

struct PlacementCountSelectionV1 {
  PlacementByteReadV1 selector;
  std::uint8_t equal_value = 0U;
  // An absent arm rejects without replacing the initial count values.
  std::optional<PlacementCountAdjustmentV1> when_equal;
  std::optional<PlacementCountAdjustmentV1> otherwise;
  [[nodiscard]] bool
  operator==(const PlacementCountSelectionV1 &) const = default;
};

struct PlacementRequireBitClearV1 {
  PlacementBitReadV1 bit;
  [[nodiscard]] bool
  operator==(const PlacementRequireBitClearV1 &) const = default;
};

// Finite source-compiled data contract: no bytecode, RAC flags, source class
// IDs, model dependencies, implicit new-game initialization or constructors.
struct PlacementAdmissionPlanV1 {
  std::uint32_t schema_version = kPlacementAdmissionSchemaVersionV1;
  PreparedContentDigestV1 state_schema_sha256{};
  std::uint32_t initial_count_bits = 0U;
  std::optional<PlacementReverseRegistrationV1> registration;
  std::optional<PlacementByteReadV1> reject_when_nonzero;
  std::variant<std::monostate, PlacementRequireBitClearV1,
               PlacementCountSelectionV1>
      condition;
  [[nodiscard]] bool
  operator==(const PlacementAdmissionPlanV1 &) const = default;
};

struct PlacementAdmissionLimitsV1 {
  std::uint32_t max_key_bytes = 0U;
  std::uint32_t max_registration_slots = 0U;
};

class PlacementAdmissionError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Validates plan shape and bounds, not existence/values of unexecuted reads.
void validate_placement_admission_plan_v1(const PlacementAdmissionPlanV1 &plan,
                                          PlacementAdmissionLimitsV1 limits);

namespace game {

struct PlacementRegistrationWriteV1 {
  std::uint32_t slot_index = 0U;
  std::uint16_t previous_value = 0U;
  std::uint16_t stored_value = 0U;
  [[nodiscard]] bool
  operator==(const PlacementRegistrationWriteV1 &) const = default;
};

struct PlacementAdmissionResultV1 {
  bool admitted = true;
  std::uint32_t maximum_bits = 0U;
  std::uint32_t current_bits = 0U;
  std::optional<PlacementRegistrationWriteV1> registration_write;
  [[nodiscard]] bool
  operator==(const PlacementAdmissionResultV1 &) const = default;
};

// One transactional step. Check schema/revision even for a read-only plan.
// Early registration writes are visible to later aliased reads. Ordinary
// rejection commits them; any error leaves the input state unchanged. Caller
// must execute accepted construction before the next placement where required.
[[nodiscard]] PlacementAdmissionResultV1 execute_placement_admission_v1(
    const PlacementAdmissionPlanV1 &plan, SessionStateV1 &state,
    std::uint64_t expected_revision, PlacementAdmissionLimitsV1 limits);

} // namespace game
} // namespace openrc
