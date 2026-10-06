#pragma once

#include "openrc/rac_moby_spatial_bounds.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace openrc {

using RacMobyPostVectorV1 = std::array<std::uint32_t, 4U>;
using RacMobyPostMatrixV1 = std::array<RacMobyPostVectorV1, 3U>;

// Current named compiler-side fields for ONE actor, not a source byte image
// or another entity world. Every input bit has an explicit current owner;
// default member values are not proof of original initialization.
struct RacMobyPostActorV1 {
  std::uint8_t state_byte = 0U;              // +20, signed gate
  std::uint16_t flags = 0U;                  // +34
  std::uint32_t model_reference = 0U;        // +24, opaque compiler reference
  std::uint32_t scale_bits = 0U;             // +2c
  std::uint8_t previous_sequence = 0U;       // +52
  std::uint8_t current_sequence = 0U;        // +53
  std::uint8_t previous_frame = 0U;          // +50, snapshot-vector index
  std::uint8_t header_cache_key = 0U;        // +71
  std::uint32_t phase_bits = 0U;             // +54
  RacMobyPostVectorV1 rotation_bits{};       // +40
  RacMobyPostVectorV1 position_bits{};       // +10, never changed here
  RacMobyPostVectorV1 cached_header_bits{};  // +f0
  RacMobyPostMatrixV1 matrix_bits{};         // +c0/+d0/+e0
  RacMobyPostVectorV1 derived_vector_bits{}; // +00
  std::uint32_t counter_word_bits = 0U;      // +a8: only low16 incremented
  std::uint32_t spatial_reference = 0U;      // +94, opaque non-null gate
  std::uint32_t packed_bounds_bits = 0U;     // +a0
  std::uint32_t live_index_bits = 0U;        // +ac, consumed by spatial tail
  [[nodiscard]] bool operator==(const RacMobyPostActorV1 &) const = default;
};

struct RacMobyPostSequenceHeaderV1 {
  std::uint8_t sequence_index = 0U;
  std::uint32_t header_reference = 0U;
  RacMobyPostVectorV1 header_bits{};
};

struct RacMobyPostSnapshotVectorV1 {
  std::uint8_t snapshot_index = 0U;
  RacMobyPostVectorV1 vector_bits{};
};

struct RacMobyPostBindingsV1 {
  // Compiler-only ownership proof, never a runtime actor address field.
  // The entire aligned256-byte source owner must be non-wrapping. Reached
  // model-table/header/snapshot read intervals may not alias this owner.
  std::uint32_t source_actor_address_bits = 0U;
  // Unique ascending unsigned byte keys; each supplied value is an actual
  // current resolved read. Missing reached entries fail, not zero-fill.
  // Reached model-table words and header/snapshot vectors may share bytes
  // only when their values agree; these are not temporal observations of
  // independently mutable memory. Unreached bindings are not dereferenced.
  std::span<const RacMobyPostSequenceHeaderV1> sequence_headers;
  // This is VECTOR table197080+index*16, not frame snapshots18f040+index*800.
  std::span<const RacMobyPostSnapshotVectorV1> snapshot_vectors;
};

struct RacMobyPostLimitsV1 {
  std::uint32_t max_sequence_headers = 0U;
  std::uint32_t max_snapshot_vectors = 0U;
  std::uint32_t max_recorded_writes = 0U;
};

enum class RacMobyPostFieldV1 : std::uint8_t {
  header_cache_key,
  cached_header,
  matrix0,
  matrix1,
  matrix2,
  derived_vector,
  counter_low16,
};

struct RacMobyPostWriteV1 {
  RacMobyPostFieldV1 field = RacMobyPostFieldV1::header_cache_key;
  // Vector fields have4 words; byte/halfword fields use only low bits of[0].
  // These are completed NAMED staged writes in source order, not VM opcodes.
  RacMobyPostVectorV1 value_bits{};
  [[nodiscard]] bool operator==(const RacMobyPostWriteV1 &) const = default;
};

enum class RacMobyPostReturnReasonV1 : std::uint8_t {
  negative_state,
  null_spatial_reference,
  unchanged_bounds,
  rejected_minimum,
};

struct RacMobyPostReturnedV1 {
  RacMobyPostReturnReasonV1 reason = RacMobyPostReturnReasonV1::negative_state;
  [[nodiscard]] bool operator==(const RacMobyPostReturnedV1 &) const = default;
};

struct RacMobyPostRotationRequestV1 {
  RacMobyPostVectorV1 rotation_bits{};
  // Original resident microprocedure d18, including its FMOR zero gates,
  // coefficient/I issue order and scalar MULA/MADDA/MADD composition.
  [[nodiscard]] bool
  operator==(const RacMobyPostRotationRequestV1 &) const = default;
};
struct RacMobyPostRotationResultV1 {
  // Source d18's initialized/composed W lanes have zero magnitude. Either
  // zero sign is accepted until the full ACC signed-zero behavior is proved.
  // This invariant does not constrain the separate cached-matrix path.
  RacMobyPostMatrixV1 columns{};
};

struct RacMobyPostBlendRequestV1 {
  RacMobyPostVectorV1 previous_header_bits{};
  RacMobyPostVectorV1 current_header_bits{};
  std::uint32_t phase_bits = 0U;
  // All4 lanes: MULA(new,phase); MADDA(old,VF0.w=1); MSUB(old,phase).
  // This is NOT a host lerp, reassociation or fused native expression.
  [[nodiscard]] bool
  operator==(const RacMobyPostBlendRequestV1 &) const = default;
};
struct RacMobyPostBlendResultV1 {
  RacMobyPostVectorV1 header_bits{};
};

struct RacMobyPostScaleRequestV1 {
  RacMobyPostVectorV1 header_bits{};
  std::uint32_t scale_bits = 0U;
  RacMobyPostVectorV1 position_bits{};
  // VMULx.xyzw header*scale, then VMULx.xyz position*44800000. W of
  // position is neither multiplied nor consumed by the later XYZ chain.
  [[nodiscard]] bool
  operator==(const RacMobyPostScaleRequestV1 &) const = default;
};
struct RacMobyPostScaleResultV1 {
  RacMobyPostVectorV1 scaled_header_bits{};
  std::array<std::uint32_t, 3U> scaled_position_bits{};
};

struct RacMobyPostDerivedRequestV1 {
  RacMobyPostMatrixV1 columns{};
  RacMobyPostVectorV1 scaled_header_bits{};
  std::array<std::uint32_t, 3U> scaled_position_bits{};
  // XYZ: MULA(col0,Bx); MADDA(col1,By); MADDA(col2,Bz);
  // MADD(scaled_position,VF0.w=1). Destination W retains scaled_header.w.
  [[nodiscard]] bool
  operator==(const RacMobyPostDerivedRequestV1 &) const = default;
};
struct RacMobyPostDerivedResultV1 {
  std::array<std::uint32_t, 3U> xyz_bits{};
};

struct RacMobyPostSpatialRequestV1 {
  std::uint32_t old_packed_bits = 0U;
  std::uint32_t new_packed_bits = 0U;
  std::uint32_t live_index_bits = 0U;
  // Actual tail251b58 still MUST run using the existing ordered spatial
  // owner and a proved source-live-token binding. New argument is zero-
  // extended64; the old LW comparison was sign-extended64. No actor bound
  // store or spatial membership update has been performed by this request.
  [[nodiscard]] bool
  operator==(const RacMobyPostSpatialRequestV1 &) const = default;
};

using RacMobyPostContinuationV1 =
    std::variant<RacMobyPostReturnedV1, RacMobyPostRotationRequestV1,
                 RacMobyPostBlendRequestV1, RacMobyPostScaleRequestV1,
                 RacMobyPostDerivedRequestV1, RacMobyPostSpatialRequestV1>;

class RacMobyPostError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Finite, value-owned compiler continuation for scalar/header/cache/control
// of entry251e30 (NOT alternate251fd0). It never executes unqualified ACC
// or rotation code, nor a supplied plan/placeholder as a successful call.
// Each typed response explicitly binds results from an EXTERNAL numeric
// owner; that owner's arithmetic, VU state and evidence remain its obligation.
// No response validates or certifies numeric correctness. There are no default
// successful responses. A wrong-stage/repeated response fails atomically.
//
// Existing reference-qualified VU SUB supplies optional row21.xyz negation;
// existing FTOI0 supplies value conversions. Their qualifications are not
// promoted into complete VU timing/MAC/ACC or physical-console equivalence.
// evaluate_scale_reference uses the separately qualified ordered integer
// MUL reference, not host arithmetic or physical-console certification.
// Original actor write order is retained, not claimed asynchronous VU timing.
// Discard this entire staged value on failure; do not publish partial actor
// state while a numeric or spatial request remains pending. There is NO fake
// spatial completion method: integrate the real OrderedSpatialIndex owner.
class RacMobyPostV1 final {
public:
  static constexpr bool executes_unresolved_numeric_operations = false;
  static constexpr bool executes_spatial_tail = false;

  RacMobyPostV1(const RacMobyPostActorV1 &actor,
                const RacMobyPostBindingsV1 &bindings,
                RacMobyPostLimitsV1 limits);

  [[nodiscard]] const RacMobyPostActorV1 &staged_actor() const noexcept;
  [[nodiscard]] const RacMobyPostContinuationV1 &continuation() const noexcept;
  [[nodiscard]] std::span<const RacMobyPostWriteV1> writes() const noexcept;

  void resume(const RacMobyPostRotationResultV1 &external_result);
  void resume(const RacMobyPostBlendResultV1 &external_result);
  void resume(const RacMobyPostScaleResultV1 &external_result);
  void resume(const RacMobyPostDerivedResultV1 &external_result);

  // Execute the reached seven scalar MUL value operations in source order,
  // using dvp_vu_mul_bits_v1, then the already qualified FTOI0.w conversion.
  // This consumes ScaleRequest exactly once and leaves DerivedRequest pending.
  // It does not publish VU flags, timing, or ACC history.
  void evaluate_scale_reference();

private:
  void select_header();
  void store_matrix_and_request_scale(const RacMobyPostVectorV1 &header);
  void record(RacMobyPostFieldV1 field, const RacMobyPostVectorV1 &bits);
  [[nodiscard]] std::uint32_t
  validate_table_address(std::uint8_t sequence) const;
  [[nodiscard]] RacMobyPostVectorV1 sequence_header(std::uint8_t sequence);
  [[nodiscard]] RacMobyPostVectorV1 snapshot_vector(std::uint8_t index);
  void validate_read_owner(std::uint32_t address, std::uint32_t bytes,
                           std::uint32_t alignment) const;
  void observe_read(std::uint32_t address, std::uint32_t bytes,
                    const RacMobyPostVectorV1 &bits);

  struct ReadObservation {
    std::uint32_t address = 0U;
    std::uint32_t bytes = 0U;
    RacMobyPostVectorV1 bits{};
  };

  RacMobyPostActorV1 actor_;
  std::uint32_t source_actor_address_bits_ = 0U;
  std::vector<RacMobyPostSequenceHeaderV1> sequence_headers_;
  std::vector<RacMobyPostSnapshotVectorV1> snapshot_vectors_;
  RacMobyPostLimitsV1 limits_;
  RacMobyPostMatrixV1 working_matrix_{};
  std::uint32_t converted_radius_bits_ = 0U;
  // At most two reached table/header pairs, or one snapshot plus a pair.
  std::array<ReadObservation, 4U> read_observations_{};
  std::uint8_t read_observation_count_ = 0U;
  std::vector<RacMobyPostWriteV1> writes_;
  RacMobyPostContinuationV1 continuation_;
};

} // namespace openrc
