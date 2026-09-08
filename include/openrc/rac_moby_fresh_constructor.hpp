#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace openrc {

// Compiler-side resolved sequence0 inputs. The source leaf reads frame0 by
// logical table index, never by sorting physical frame extents. References
// here are opaque source/compiler values, not dereferenceable runtime handles.
struct RacMobyFreshSequenceV1 {
  std::uint32_t frame0_reference = 0U;
  std::uint8_t frame_count = 0U;
  std::uint8_t sound_byte = 0U;
  std::uint8_t trigger_byte = 0U;
  [[nodiscard]] bool operator==(const RacMobyFreshSequenceV1 &) const = default;
};

struct RacMobyFreshModelV1 {
  // Presence denotes a genuinely bound non-null model. Its reference must
  // be nonzero; unresolved lookup must not be represented as null-model.
  std::uint32_t reference = 0U;
  std::uint32_t spatial_reference = 0U;
  std::uint32_t auxiliary_reference = 0U;
  std::uint32_t scale_bits = 0U;
  // Current shared model flags, including earlier accepted placement ORs.
  // This function reads them; the later placement step owns their mutation.
  std::uint16_t flags = 0U;
  std::uint8_t byte_06 = 0U;
  std::uint8_t byte_0c = 0U;
  std::uint8_t byte_0e = 0U;
  std::uint8_t byte_0f = 0U;
  // Absence means the actual resolved sequence0 pointer is null, not that
  // parsing failed. External sequence tables must be resolved beforehand.
  std::optional<RacMobyFreshSequenceV1> sequence0;
  [[nodiscard]] bool operator==(const RacMobyFreshModelV1 &) const = default;
};

struct RacMobyFreshConstructorInputV1 {
  std::uint32_t class_id_bits = 0U;
  std::uint8_t class_table_index = 0U;
  // Explicit result of the source's slot-base subtraction/arithmetic shift.
  // This is NOT EntityIdV1::slot or an authored record index.
  std::uint32_t live_index_bits = 0U;
  std::uint32_t callback_reference = 0U;
  std::optional<RacMobyFreshModelV1> model;
  [[nodiscard]] bool
  operator==(const RacMobyFreshConstructorInputV1 &) const = default;
};

// Complete set of nonzero-capable fields written by the fresh constructor.
// All other bytes of its initially cleared 0x100-byte source slot are zero:
// in particular state, frames/sequences, phase, positions/rotations, derived
// vector, matrix/header caches, PVar and placement/admission fields. This is
// a fresh value, not a patch to merge into an existing actor or a serialized
// source-memory image. Fields with unproven meaning retain qualified names.
struct RacMobyFreshConstructorStateV1 {
  std::uint8_t group_byte = 0xffU;     // +21
  std::uint8_t class_table_index = 0U; // +22
  std::uint8_t byte_23 = 0x80U;
  std::uint32_t model_reference = 0U;                // +24
  std::uint32_t scale_bits = 0U;                     // +2c
  std::uint16_t flags = 0U;                          // +34
  std::uint16_t occlusion_bits = 0x7f80U;            // +36
  std::uint64_t packed_bits = 0x0040404000000000ULL; // +38
  std::uint32_t animation_speed_bits = 0U;           // +58
  std::uint32_t animation_rate_bits = 0U;            // +5c
  std::uint32_t previous_frame_reference = 0U;       // +68
  std::uint32_t current_frame_reference = 0U;        // +6c
  std::uint8_t header_cache_key = 0xffU;             // +71
  std::uint8_t model_byte_0e = 0xffU;                // +72
  std::uint8_t byte_73 = 0U;
  std::uint32_t callback_reference = 0U; // +74; never invoked here
  std::uint8_t sound_byte = 0xffU;       // +7c
  std::uint8_t byte_7d = 0xffU;
  std::uint8_t trigger_byte = 0U; // +7e
  std::uint8_t byte_7f = 0U;
  std::uint32_t auxiliary_reference = 0U;         // +90
  std::uint32_t spatial_reference = 0U;           // +94
  std::uint32_t packed_bounds_bits = 0x80807f7fU; // +a0
  std::uint8_t byte_a4 = 0xffU;
  std::uint16_t class_id_low16 = 0U;          // +a6
  std::uint32_t live_index_counter_bits = 0U; // +a8: low16 counter remains0
  std::uint32_t live_index_bits = 0U;         // +ac
  [[nodiscard]] bool
  operator==(const RacMobyFreshConstructorStateV1 &) const = default;
};

class RacMobyFreshConstructorError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Source-qualified fresh constructor and its frame-pointer leaf. No float
// arithmetic, callbacks, admission, shared model writes or post-step occurs.
// Scale values are copied as raw bits; this is BEFORE authored scale MUL.S.
// Every accepted bound model still requires the complete subsequent source
// post-step, for nonzero rotations as well as zero, before next admission.
// Never serialize these source references into neutral runtime packages.
[[nodiscard]] RacMobyFreshConstructorStateV1
construct_rac_moby_fresh_v1(const RacMobyFreshConstructorInputV1 &input);

} // namespace openrc
