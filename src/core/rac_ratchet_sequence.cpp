#include "openrc/rac_ratchet_sequence.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint32_t kRegularFrameAlignment = 0x10U;

[[noreturn]] void fail(const std::string &message) {
  throw RacRatchetSequenceError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(const std::span<const std::byte> bytes,
                                      const std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(byte_value(bytes[offset])) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(byte_value(bytes[offset + 1U])) << 8U);
}

[[nodiscard]] std::uint32_t read_le32(const std::span<const std::byte> bytes,
                                      const std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] float read_f32(const std::span<const std::byte> bytes,
                             const std::size_t offset,
                             const char *const description) {
  const auto value = std::bit_cast<float>(read_le32(bytes, offset));
  if (!std::isfinite(value) || value < 0.0F) {
    fail(std::string("RacRatchetSequenceV1 has an invalid ") + description);
  }
  return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string("Integer overflow while calculating ") + description);
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(const std::uint64_t left,
                                             const std::uint64_t right,
                                             const char *const description) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string("Integer overflow while calculating ") + description);
  }
  return left * right;
}

[[nodiscard]] std::uint64_t align_up_qword(const std::uint64_t value,
                                           const char *const description) {
  const auto remainder = value & 0x0fU;
  return remainder == 0U ? value
                         : checked_add(value, 0x10U - remainder, description);
}

void require_range(const std::uint64_t offset, const std::uint64_t size,
                   const std::uint64_t limit, const char *const description) {
  if (offset > limit || size > limit - offset) {
    fail(std::string(description) + " exceeds its bounded input");
  }
}

[[nodiscard]] bool ranges_overlap(const RacRatchetSequenceRangeV1 left,
                                  const RacRatchetSequenceRangeV1 right) {
  if (left.size == 0U || right.size == 0U) {
    return false;
  }
  return left.offset < checked_add(right.offset, right.size, "a range end") &&
         right.offset < checked_add(left.offset, left.size, "a range end");
}

struct RawFrameOffset {
  std::uint32_t packed = 0U;
  std::uint32_t relative_offset = 0U;
  std::uint64_t source_offset = 0U;
};

enum class FrameOffsetBasis {
  sequence,
  source,
};

[[nodiscard]] RacRatchetSequenceV1 parse_regular_sequence(
    const std::span<const std::byte> source,
    const RacRatchetSequenceRangeV1 sequence_range,
    const RacRatchetSequenceLimitsV1 limits,
    const FrameOffsetBasis frame_offset_basis) {
  if (limits.max_input_bytes == 0U || limits.max_sequence_bytes == 0U ||
      limits.max_frames == 0U || limits.max_trigger_words == 0U) {
    fail("RacRatchetSequenceV1 caller limits must be non-zero");
  }
  if (source.size() > limits.max_input_bytes) {
    fail("RacRatchetSequenceV1 exceeds the caller's input-byte limit");
  }
  if (sequence_range.size > limits.max_sequence_bytes) {
    fail("RacRatchetSequenceV1 exceeds the caller's sequence-byte limit");
  }
  require_range(sequence_range.offset, sequence_range.size, source.size(),
                "The RacRatchetSequenceV1 range");
  if (sequence_range.size < kRacRatchetSequenceHeaderBytesV1 ||
      (sequence_range.offset & (kRegularFrameAlignment - 1U)) != 0U) {
    fail("RacRatchetSequenceV1 has an invalid bounded envelope");
  }

  const auto sequence_begin = static_cast<std::size_t>(sequence_range.offset);
  RacRatchetSequenceV1 result;
  result.source_range = sequence_range;
  result.header_range = {0U, kRacRatchetSequenceHeaderBytesV1};
  for (std::size_t index = 0U; index < result.opaque_prefix_words.size();
       ++index) {
    result.opaque_prefix_words[index] =
        read_le32(source, sequence_begin + index * sizeof(std::uint32_t));
  }
  result.frame_count = byte_value(source[sequence_begin + 0x10U]);
  result.continuous_sound_id = byte_value(source[sequence_begin + 0x11U]);
  result.trigger_count = byte_value(source[sequence_begin + 0x12U]);
  result.opaque_control_13 = byte_value(source[sequence_begin + 0x13U]);
  result.opaque_word_14 = read_le32(source, sequence_begin + 0x14U);
  result.sequence_phase_rate_override =
      read_f32(source, sequence_begin + 0x18U, "sequence phase-rate override");

  if (result.frame_count == 0U || result.frame_count > limits.max_frames) {
    fail("RacRatchetSequenceV1 has an invalid or caller-limited frame count");
  }
  if (result.trigger_count > limits.max_trigger_words) {
    fail("RacRatchetSequenceV1 exceeds the caller's trigger-word limit");
  }

  const auto frame_table_bytes = checked_multiply(
      result.frame_count, sizeof(std::uint32_t), "the frame-offset table size");
  const auto trigger_list_bytes = checked_multiply(
      result.trigger_count, sizeof(std::uint32_t), "the trigger-list size");
  const auto trigger_list_begin =
      checked_add(kRacRatchetSequenceHeaderBytesV1, frame_table_bytes,
                  "the trigger-list offset");
  const auto directory_end = checked_add(trigger_list_begin, trigger_list_bytes,
                                         "the sequence directory end");
  require_range(0U, directory_end, sequence_range.size,
                "The RacRatchetSequenceV1 directory");
  result.frame_offset_table_range = {kRacRatchetSequenceHeaderBytesV1,
                                     frame_table_bytes};
  result.trigger_list_range = {trigger_list_begin, trigger_list_bytes};

  std::vector<RawFrameOffset> raw_frames;
  raw_frames.reserve(result.frame_count);
  for (std::uint32_t index = 0U; index < result.frame_count; ++index) {
    const auto table_offset =
        sequence_begin + kRacRatchetSequenceHeaderBytesV1 +
        static_cast<std::size_t>(index) * sizeof(std::uint32_t);
    const auto packed = read_le32(source, table_offset);
    const auto opaque_bits = packed & kRacRatchetSequenceOpaqueBitsMaskV1;
    if (opaque_bits != 0U) {
      fail("RacRatchetSequenceV1 has unsupported opaque frame-offset bits");
    }

    const auto encoded_offset =
        packed & kRacRatchetSequenceOffsetBitsMaskV1;
    const auto source_offset =
        frame_offset_basis == FrameOffsetBasis::sequence
            ? checked_add(sequence_range.offset, encoded_offset,
                          "a frame source offset")
            : static_cast<std::uint64_t>(encoded_offset);
    const auto sequence_end =
        checked_add(sequence_range.offset, sequence_range.size,
                    "the bounded sequence end");
    if (source_offset < sequence_range.offset || source_offset >= sequence_end) {
      fail("A RacRatchetSequenceV1 frame offset leaves the bounded sequence");
    }
    const auto relative_offset_u64 = source_offset - sequence_range.offset;
    if (relative_offset_u64 > std::numeric_limits<std::uint32_t>::max()) {
      fail("A RacRatchetSequenceV1 normalized frame offset exceeds uint32_t");
    }
    const auto relative_offset =
        static_cast<std::uint32_t>(relative_offset_u64);
    if (relative_offset < directory_end ||
        (relative_offset & (kRegularFrameAlignment - 1U)) != 0U) {
      fail("RacRatchetSequenceV1 has an invalid frame offset");
    }
    raw_frames.push_back(
        RawFrameOffset{packed, relative_offset, source_offset});
  }

  std::vector<std::uint64_t> physical_offsets;
  physical_offsets.reserve(raw_frames.size());
  for (const auto &frame : raw_frames) {
    physical_offsets.push_back(frame.relative_offset);
  }
  std::sort(physical_offsets.begin(), physical_offsets.end());
  if (std::adjacent_find(physical_offsets.begin(), physical_offsets.end()) !=
      physical_offsets.end()) {
    fail("RacRatchetSequenceV1 aliases two frames to one offset");
  }
  const auto first_frame_offset = physical_offsets.front();
  result.pre_frame_data_range = {directory_end,
                                 first_frame_offset - directory_end};

  result.trigger_words.reserve(result.trigger_count);
  for (std::uint32_t index = 0U; index < result.trigger_count; ++index) {
    result.trigger_words.push_back(read_le32(
        source, sequence_begin + static_cast<std::size_t>(trigger_list_begin) +
                    static_cast<std::size_t>(index) * sizeof(std::uint32_t)));
  }

  result.frames.reserve(result.frame_count);
  for (const auto &raw : raw_frames) {
    RacRatchetSequenceFrameV1 frame;
    frame.packed_offset_word = raw.packed;
    frame.relative_offset = raw.relative_offset;
    frame.source_offset = raw.source_offset;
    require_range(
        raw.relative_offset, kRacRatchetSequenceRegularFrameHeaderBytesV1,
        sequence_range.size, "A regular RacRatchetSequenceV1 frame header");
    frame.phase_rate = read_f32(source,
                                sequence_begin + raw.relative_offset + 0x00U,
                                "frame phase rate");
    frame.opaque_half_4 =
        read_le16(source, sequence_begin + raw.relative_offset + 0x04U);
    frame.regular_payload_qwords =
        read_le16(source, sequence_begin + raw.relative_offset + 0x06U);
    frame.primary_byte_count =
        read_le16(source, sequence_begin + raw.relative_offset + 0x08U);
    frame.supplemental_a_count =
        read_le16(source, sequence_begin + raw.relative_offset + 0x0aU);
    frame.supplemental_b_byte_offset =
        read_le16(source, sequence_begin + raw.relative_offset + 0x0cU);
    frame.supplemental_b_count =
        read_le16(source, sequence_begin + raw.relative_offset + 0x0eU);
    const auto payload_bytes =
        checked_multiply(frame.regular_payload_qwords, 0x10U,
                         "a regular RacRatchetSequenceV1 payload size");
    const auto frame_bytes =
        checked_add(kRacRatchetSequenceRegularFrameHeaderBytesV1, payload_bytes,
                    "a regular RacRatchetSequenceV1 frame size");
    require_range(raw.relative_offset, frame_bytes, sequence_range.size,
                  "A regular RacRatchetSequenceV1 frame");
    frame.range = {raw.relative_offset, frame_bytes};
    frame.structural_header_range = {
        raw.relative_offset, kRacRatchetSequenceRegularFrameHeaderBytesV1};
    const auto payload_begin = checked_add(
        raw.relative_offset, kRacRatchetSequenceRegularFrameHeaderBytesV1,
        "a regular RacRatchetSequenceV1 payload offset");
    frame.opaque_payload_range = {payload_begin, payload_bytes};

    const auto supplemental_a_bytes =
        checked_multiply(frame.supplemental_a_count, 8U,
                         "a regular RacRatchetSequenceV1 supplemental-A size");
    const auto supplemental_b_bytes =
        checked_multiply(frame.supplemental_b_count, 8U,
                         "a regular RacRatchetSequenceV1 supplemental-B size");
    const auto declared_b_offset =
        checked_add(frame.primary_byte_count, supplemental_a_bytes,
                    "a regular RacRatchetSequenceV1 supplemental-B offset");
    if (declared_b_offset != frame.supplemental_b_byte_offset) {
      fail("A regular RacRatchetSequenceV1 supplemental-B offset disagrees "
           "with its preceding partitions");
    }
    const auto partitioned_bytes =
        checked_add(frame.supplemental_b_byte_offset, supplemental_b_bytes,
                    "a regular RacRatchetSequenceV1 payload partition");
    if (align_up_qword(partitioned_bytes,
                       "an aligned RacRatchetSequenceV1 payload partition") !=
        payload_bytes) {
      fail("A regular RacRatchetSequenceV1 payload partition disagrees with "
           "its declared qword envelope");
    }

    auto payload_cursor = frame.opaque_payload_range.offset;
    frame.primary_payload_range = {payload_cursor, frame.primary_byte_count};
    payload_cursor =
        checked_add(payload_cursor, frame.primary_payload_range.size,
                    "the regular primary-payload end");
    frame.supplemental_a_payload_range = {payload_cursor, supplemental_a_bytes};
    payload_cursor =
        checked_add(payload_cursor, frame.supplemental_a_payload_range.size,
                    "the regular supplemental-A end");
    const auto supplemental_b_begin = checked_add(
        frame.opaque_payload_range.offset, frame.supplemental_b_byte_offset,
        "the regular supplemental-B begin");
    frame.supplemental_b_payload_range = {supplemental_b_begin,
                                          supplemental_b_bytes};
    payload_cursor = frame.supplemental_b_payload_range.offset;
    payload_cursor =
        checked_add(payload_cursor, frame.supplemental_b_payload_range.size,
                    "the regular supplemental-B end");
    frame.trailing_alignment_padding_range = {
        payload_cursor, payload_bytes - partitioned_bytes};
    result.frames.push_back(frame);
  }

  std::vector<RacRatchetSequenceRangeV1> physical_ranges;
  physical_ranges.reserve(result.frames.size());
  for (const auto &frame : result.frames) {
    physical_ranges.push_back(frame.range);
  }
  std::sort(physical_ranges.begin(), physical_ranges.end(),
            [](const auto left, const auto right) {
              return left.offset < right.offset;
            });
  for (std::size_t index = 1U; index < physical_ranges.size(); ++index) {
    if (ranges_overlap(physical_ranges[index - 1U], physical_ranges[index])) {
      fail("RacRatchetSequenceV1 frame ranges overlap");
    }
  }
  const auto bounded = source.subspan(
      sequence_begin, static_cast<std::size_t>(sequence_range.size));
  result.encoded_bytes.assign(bounded.begin(), bounded.end());
  return result;
}

} // namespace

RacRatchetSequenceV1
parse_rac_ratchet_sequence_v1(const std::span<const std::byte> source,
                              const RacRatchetSequenceRangeV1 sequence_range,
                              const RacRatchetSequenceLimitsV1 limits) {
  return parse_regular_sequence(source, sequence_range, limits,
                                FrameOffsetBasis::sequence);
}

RacRatchetSequenceV1
parse_rac_moby_sequence_v1(const std::span<const std::byte> source,
                           const RacRatchetSequenceRangeV1 sequence_range,
                           const RacRatchetSequenceLimitsV1 limits) {
  return parse_regular_sequence(source, sequence_range, limits,
                                FrameOffsetBasis::source);
}

} // namespace openrc
