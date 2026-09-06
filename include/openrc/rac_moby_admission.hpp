#pragma once

#include "openrc/rac_gameplay_bank.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace openrc {

class RacMobyAdmissionError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct RacMobyAdmissionRegistrationV1 {
  std::int16_t key = 0;
  std::uint16_t auxiliary_bits = 0U;
  [[nodiscard]] bool
  operator==(const RacMobyAdmissionRegistrationV1 &) const = default;
};

// An explicitly supplied contiguous window of source-relative byte indices.
// A prefix such as selector index -1 must come from its actual source bytes;
// it must not be synthesized from zero or from a different persistent row.
// An explicit window does not widen the evaluator's supported selector or
// bit-table domains: this API is not an arbitrary source-memory emulator.
struct RacMobyAdmissionByteWindowV1 {
  std::int32_t first_index = 0;
  std::vector<std::uint8_t> bytes;
  [[nodiscard]] bool
  operator==(const RacMobyAdmissionByteWindowV1 &) const = default;
};

// Compiler-side source state for an explicitly selected level row. Empty
// windows/vectors and a disengaged registration row are unavailable input, not
// zero-filled new-game defaults. Only reached source accesses are required.
struct RacMobyAdmissionStateV1 {
  RacMobyAdmissionByteWindowV1 selector_bytes;
  RacMobyAdmissionByteWindowV1 suppression_bytes;
  std::vector<std::uint32_t> primary_bit_words;
  std::vector<std::uint32_t> alternate_bit_words;
  std::optional<std::array<RacMobyAdmissionRegistrationV1, 64U>>
      registration_slots;
};

struct RacMobyAdmissionLimitsV1 {
  std::uint64_t max_selector_bytes = 256U;
  std::uint64_t max_suppression_bytes = 1U << 20U;
  std::uint64_t max_bit_words = 64U;
};

enum class RacMobyAdmissionDecisionV1 : std::uint8_t {
  admitted,
  skipped,
};

struct RacMobyAdmissionCountTransformV1 {
  std::uint32_t input_bits = 0U;
  std::uint32_t incremented_bits = 0U;
  std::uint32_t sign_adjustment = 0U;
  std::uint32_t adjusted_bits = 0U;
  std::uint32_t result_bits = 0U;
};

struct RacMobyAdmissionRegistrationWriteV1 {
  std::uint8_t slot_index = 0U;
  std::int16_t previous_key = 0;
  std::int16_t stored_key = 0;
  // The source writes even when the key already matches, and does not
  // touch this adjacent halfword. Record it without clearing/replacing it.
  std::uint16_t preserved_auxiliary_bits = 0U;
};

struct RacMobyAdmissionConstructorFieldsV1 {
  std::uint16_t key_bits = 0U;
  std::uint16_t current_count_bits = 0U;
  std::uint16_t maximum_count_bits = 0U;
  std::uint8_t selector_bits = 0U;
  std::uint8_t auxiliary_slot_bits = 0xfeU;
};

struct RacMobyAdmissionResultV1 {
  RacMobyAdmissionDecisionV1 decision = RacMobyAdmissionDecisionV1::admitted;
  std::uint32_t maximum_bits = 0U;
  std::uint32_t current_bits = 0U;
  std::uint8_t auxiliary_slot_bits = 0xfeU;
  std::optional<RacMobyAdmissionCountTransformV1> count_transform;
  // Present only for admitted records. Prepared scalar values only: the
  // original constructor is not run, and no live actor is written here.
  std::optional<RacMobyAdmissionConstructorFieldsV1> constructor_fields;
  std::optional<RacMobyAdmissionRegistrationWriteV1> registration_write;
};

// Evaluate ONE source step from an explicitly supplied, source-correct current
// snapshot. The caller must refresh read views affected by preceding source
// operations, including any aliases of registration data and interleaved
// constructor effects. Only registration_slots is mutated here. Repeated calls
// with stale independent views are not a complete source-loader simulation.
// Ordinary source rejection preserves an earlier registration write.
// Missing/malformed reached input throws without mutating state. This is
// not a placement constructor, source initializer or runtime integration.
// Reached selector accesses support only the proven cache indices -1..15;
// reached bit gates support keys 0..2047 and at most 64 supplied words.
// Caller limits cannot widen these hard source-profile bounds. Out-of-profile
// addresses could alias staged registration writes and must not be evaluated
// as independent immutable rows.
[[nodiscard]] RacMobyAdmissionResultV1
evaluate_rac_moby_admission_v1(const RacGameplayMobyAdmissionV1 &admission,
                               RacMobyAdmissionStateV1 &state,
                               const RacMobyAdmissionLimitsV1 &limits = {});

} // namespace openrc
