#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kRacRatchetSequenceHeaderBytesV1 = 0x1cU;
inline constexpr std::uint32_t kRacRatchetSequenceRegularFrameHeaderBytesV1 =
    0x10U;
inline constexpr std::uint32_t kRacRatchetSequenceOffsetBitsMaskV1 =
    0x0fffffffU;
inline constexpr std::uint32_t kRacRatchetSequenceOpaqueBitsMaskV1 =
    0xf0000000U;

struct RacRatchetSequenceRangeV1 {
  std::uint64_t offset = 0U;
  std::uint64_t size = 0U;

  [[nodiscard]] bool
  operator==(const RacRatchetSequenceRangeV1 &) const = default;
};

struct RacRatchetSequenceLimitsV1 {
  std::uint64_t max_input_bytes = 0U;
  std::uint64_t max_sequence_bytes = 0U;
  std::uint64_t max_frames = 0U;
  std::uint64_t max_trigger_words = 0U;
};

struct RacRatchetSequenceFrameV1 {
  // Exact table word is retained separately from the normalized
  // sequence-relative offset and its derived source location. Ratchet tables
  // encode a sequence-relative value; ordinary Moby class tables encode a
  // class-asset-relative value. V1 rejects any non-zero upper bits.
  std::uint32_t packed_offset_word = 0U;
  std::uint32_t relative_offset = 0U;
  std::uint64_t source_offset = 0U;

  // Ranges are relative to RacRatchetSequenceV1::encoded_bytes. A regular
  // frame has a 0x10-byte structural header and a qword-sized opaque payload.
  RacRatchetSequenceRangeV1 range;
  RacRatchetSequenceRangeV1 structural_header_range;
  RacRatchetSequenceRangeV1 opaque_payload_range;

  // +0 is the phase advance per source update when the sequence-level
  // override is zero. The remaining fields are direct little-endian copies
  // used to prove the exact regular-frame payload partition.
  float phase_rate = 0.0F;
  std::uint16_t opaque_half_4 = 0U;
  std::uint16_t regular_payload_qwords = 0U;
  std::uint16_t primary_byte_count = 0U;
  std::uint16_t supplemental_a_count = 0U;
  std::uint16_t supplemental_b_byte_offset = 0U;
  std::uint16_t supplemental_b_count = 0U;

  // Together these four ranges form an exact, non-overlapping partition of
  // opaque_payload_range. Supplemental records are structurally eight bytes
  // each. Trailing bytes are preserved and are not required to be zero.
  RacRatchetSequenceRangeV1 primary_payload_range;
  RacRatchetSequenceRangeV1 supplemental_a_payload_range;
  RacRatchetSequenceRangeV1 supplemental_b_payload_range;
  RacRatchetSequenceRangeV1 trailing_alignment_padding_range;
};

struct RacRatchetSequenceV1 {
  // Original location in the caller's source buffer. encoded_bytes is an
  // owned byte-for-byte copy of this bounded range, so later compiler stages
  // need neither the caller's storage nor raw pointers.
  RacRatchetSequenceRangeV1 source_range;
  std::vector<std::byte> encoded_bytes;

  RacRatchetSequenceRangeV1 header_range;
  RacRatchetSequenceRangeV1 frame_offset_table_range;
  RacRatchetSequenceRangeV1 trigger_list_range;
  RacRatchetSequenceRangeV1 pre_frame_data_range;

  std::array<std::uint32_t, 4U> opaque_prefix_words{};
  std::uint8_t frame_count = 0U;
  // 0xff means that the sequence has no continuous sound definition.
  std::uint8_t continuous_sound_id = 0xffU;
  std::uint8_t trigger_count = 0U;
  std::uint8_t opaque_control_13 = 0U;
  std::uint32_t opaque_word_14 = 0U;
  // A non-zero value overrides every frame's phase_rate. Zero selects the
  // per-frame value. Both are measured in phase units per source update.
  float sequence_phase_rate_override = 0.0F;

  std::vector<std::uint32_t> trigger_words;
  std::vector<RacRatchetSequenceFrameV1> frames;
};

class RacRatchetSequenceError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Parses one complete RAC1 Ratchet-sequence range within its decoded source
// buffer. This compiler-side parser proves only envelopes, tables, and
// non-overlapping regular frame ranges. Frame and pre-frame payloads remain
// opaque; no packed field is assigned pose or transform semantics here.
[[nodiscard]] RacRatchetSequenceV1
parse_rac_ratchet_sequence_v1(std::span<const std::byte> source,
                              RacRatchetSequenceRangeV1 sequence_range,
                              RacRatchetSequenceLimitsV1 limits);

// Parses the same regular RAC1 animation payload when it is embedded in an
// ordinary Moby class asset. Unlike Ratchet's level-core table, each packed
// frame offset is relative to the beginning of source (the class asset), not
// to sequence_range. Returned frame ranges are normalized back to the
// sequence-local domain, so pose decoding remains source-layout independent.
[[nodiscard]] RacRatchetSequenceV1
parse_rac_moby_sequence_v1(std::span<const std::byte> source,
                           RacRatchetSequenceRangeV1 sequence_range,
                           RacRatchetSequenceLimitsV1 limits);

} // namespace openrc
