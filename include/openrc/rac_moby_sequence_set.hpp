#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>

namespace openrc {

// Compiler-only resolved logical frame-table word, not a runtime pointer.
// The source byte index may be outside frame_count; only actual ownership of
// the reached table word determines whether that read is available.
struct RacMobySequenceFrameBindingV1 {
  std::uint8_t logical_frame_index = 0U;
  std::uint32_t frame_reference = 0U;
};

struct RacMobySequenceBindingV1 {
  // Actual wrapping SLL2 table addressing aliases the upper two input bits.
  // This key is the resulting word index0..3fffffff, not a byte sequence ID.
  // The later leaf instead uses the stored low8 sequence byte.
  std::uint32_t table_word_index = 0U;
  std::uint8_t frame_count = 0U;
  std::uint8_t sound_byte = 0U;
  std::uint8_t trigger_byte = 0U;
  // Strict ascending logical index, unique, explicitly owned words. No
  // sorting by physical frame extent or implied count-based zero filling.
  std::span<const RacMobySequenceFrameBindingV1> frames;
};

struct RacMobyFrameFirstWordBindingV1 {
  // Exact aligned source-word reference, including an explicitly resolved
  // snapshot frame when reached. Never dereferenced by this native adapter.
  std::uint32_t frame_reference = 0U;
  std::uint32_t first_word_bits = 0U;
};

struct RacMobySequenceBankV1 {
  // Both arrays are strictly ascending by their unsigned keys, unique and
  // resolved from current source owners. The caller must prove that ALL
  // model/sequence/frame/snapshot read owners are disjoint from the actor
  // slot being patched: otherwise actor stores can change later re-reads.
  // This resolved adapter has no actor address and cannot infer that proof.
  // No image/pointer fallback or invented snapshot contents are supplied.
  std::span<const RacMobySequenceBindingV1> sequences;
  std::span<const RacMobyFrameFirstWordBindingV1> frame_first_words;
};

struct RacMobySequenceSetLimitsV1 {
  std::uint32_t max_sequence_bindings = 0U;
  std::uint32_t max_total_frame_bindings = 0U;
  std::uint32_t max_first_word_bindings = 0U;
};

struct RacMobyFrameIndicesV1 {
  std::uint8_t previous_frame = 0U;    // +50
  std::uint8_t current_frame = 0U;     // +51
  std::uint8_t previous_sequence = 0U; // +52
  std::uint8_t current_sequence = 0U;  // +53
  [[nodiscard]] bool operator==(const RacMobyFrameIndicesV1 &) const = default;
};

struct RacMobyFrameReferencesV1 {
  std::uint32_t previous_frame_reference = 0U; // +68
  std::uint32_t current_frame_reference = 0U;  // +6c
  std::uint8_t sound_byte = 0U;                // +7c
  std::uint8_t trigger_byte = 0U;              // +7e
  [[nodiscard]] bool
  operator==(const RacMobyFrameReferencesV1 &) const = default;
};

struct RacMobySequenceSetResultV1 {
  RacMobyFrameIndicesV1 indices;
  RacMobyFrameReferencesV1 references;
  std::uint8_t sequence_flags = 0U;       // +70, only bit1 cleared
  std::uint32_t animation_rate_bits = 0U; // +5c, raw first frame word
  [[nodiscard]] bool
  operator==(const RacMobySequenceSetResultV1 &) const = default;
};

class RacMobySequenceSetError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Complete source leaf24fbf8. Previous sequenceff selects the actual source
// snapshot reference18f040+(previous_frame<<11), soundff and trigger0. It does
// not read snapshot contents. CURRENT sequenceff still uses the ordinary
// sequence table and frame index, not that special branch. All other paths
// use exact logical table indices and copy raw frame references.
[[nodiscard]] RacMobyFrameReferencesV1
resolve_rac_moby_frame_references_v1(const RacMobyFrameIndicesV1 &indices,
                                     const RacMobySequenceBankV1 &bank,
                                     RacMobySequenceSetLimitsV1 limits);

// Complete source258800 setter plus the real general leaf above. Preserve
// the full incoming64-bit a1/a2 ABI until each original word/byte truncation:
// first sequence lookup uses SLL2(low32(a1)), SLT uses signed64(a2), stored
// indices use low8. Counts, low-byte clamps and zero-count paths are literal
// source operations, not modern animation normalization. Missing reached
// words fail even if an invented clamp/null frame would be convenient.
//
// Copies the resolved previous frame's raw first word into rate+5c and
// clears only bit1 of the supplied CURRENT +70 byte. No float arithmetic,
// phase reset, animation-speed change, actor callback, snapshot creation,
// PVar change or world/session publication occurs. All other actor fields
// remain untouched; install this complete named patch in the staged owner.
[[nodiscard]] RacMobySequenceSetResultV1 set_rac_moby_sequence_v1(
    std::uint64_t sequence_argument_bits, std::uint64_t frame_argument_bits,
    std::uint8_t current_sequence_flags, const RacMobySequenceBankV1 &bank,
    RacMobySequenceSetLimitsV1 limits);

} // namespace openrc
