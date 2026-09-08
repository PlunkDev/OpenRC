#include "openrc/rac_moby_sequence_set.hpp"

#include <algorithm>
#include <bit>
#include <string>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RacMobySequenceSetError("RAC Moby sequence setter " + message);
}

void validate_bank(const RacMobySequenceBankV1 &bank,
                   const RacMobySequenceSetLimitsV1 limits) {
  if (limits.max_sequence_bindings == 0U ||
      limits.max_total_frame_bindings == 0U ||
      limits.max_first_word_bindings == 0U) {
    fail("limits must be nonzero");
  }
  if (bank.sequences.size() > limits.max_sequence_bindings ||
      bank.frame_first_words.size() > limits.max_first_word_bindings) {
    fail("bank exceeds its explicit binding limit");
  }
  std::uint64_t total_frames = 0U;
  std::uint32_t previous_sequence = 0U;
  bool have_sequence = false;
  for (const auto &sequence : bank.sequences) {
    if (sequence.table_word_index > 0x3fffffffU ||
        (have_sequence && sequence.table_word_index <= previous_sequence)) {
      fail("sequence table bindings must have unique ascending30-bit keys");
    }
    if (sequence.frames.size() >
        limits.max_total_frame_bindings - total_frames) {
      fail("aggregate frame bindings exceed their explicit limit");
    }
    total_frames += sequence.frames.size();
    std::uint8_t previous_frame = 0U;
    bool have_frame = false;
    for (const auto &frame : sequence.frames) {
      if (have_frame && frame.logical_frame_index <= previous_frame) {
        fail("frame table words must have unique ascending logical byte "
             "indices");
      }
      previous_frame = frame.logical_frame_index;
      have_frame = true;
    }
    previous_sequence = sequence.table_word_index;
    have_sequence = true;
  }
  std::uint32_t previous_word = 0U;
  bool have_word = false;
  for (const auto &word : bank.frame_first_words) {
    if ((word.frame_reference & 3U) != 0U ||
        (have_word && word.frame_reference <= previous_word)) {
      fail("first-word bindings must have unique ascending aligned references");
    }
    previous_word = word.frame_reference;
    have_word = true;
  }
}

const RacMobySequenceBindingV1 &sequence_at(const RacMobySequenceBankV1 &bank,
                                            const std::uint32_t key) {
  const auto found = std::lower_bound(
      bank.sequences.begin(), bank.sequences.end(), key,
      [](const RacMobySequenceBindingV1 &candidate, std::uint32_t value) {
        return candidate.table_word_index < value;
      });
  if (found == bank.sequences.end() || found->table_word_index != key) {
    fail("reached sequence-table word has no current resolved owner");
  }
  return *found;
}

std::uint32_t frame_at(const RacMobySequenceBindingV1 &sequence,
                       const std::uint8_t index) {
  const auto found = std::lower_bound(
      sequence.frames.begin(), sequence.frames.end(), index,
      [](const RacMobySequenceFrameBindingV1 &candidate, std::uint8_t value) {
        return candidate.logical_frame_index < value;
      });
  if (found == sequence.frames.end() || found->logical_frame_index != index) {
    fail("reached logical frame-table word has no resolved owner");
  }
  return found->frame_reference;
}

RacMobyFrameReferencesV1 resolve_frames(const RacMobyFrameIndicesV1 &indices,
                                        const RacMobySequenceBankV1 &bank) {
  RacMobyFrameReferencesV1 result;
  if (indices.previous_sequence == 0xffU) {
    result.previous_frame_reference =
        0x18f040U + (std::uint32_t{indices.previous_frame} << 11U);
    result.sound_byte = 0xffU;
    result.trigger_byte = 0U;
  } else {
    const auto &previous = sequence_at(bank, indices.previous_sequence);
    result.previous_frame_reference =
        frame_at(previous, indices.previous_frame);
    result.trigger_byte = previous.trigger_byte;
    result.sound_byte = previous.sound_byte;
  }
  const auto &current = sequence_at(bank, indices.current_sequence);
  result.current_frame_reference = frame_at(current, indices.current_frame);
  return result;
}

std::uint32_t first_word(const RacMobySequenceBankV1 &bank,
                         const std::uint32_t reference) {
  const auto found = std::lower_bound(
      bank.frame_first_words.begin(), bank.frame_first_words.end(), reference,
      [](const RacMobyFrameFirstWordBindingV1 &candidate, std::uint32_t value) {
        return candidate.frame_reference < value;
      });
  if (found == bank.frame_first_words.end() ||
      found->frame_reference != reference) {
    fail("reached previous-frame first word is missing; no snapshot/default "
         "fallback");
  }
  return found->first_word_bits;
}

} // namespace

RacMobyFrameReferencesV1
resolve_rac_moby_frame_references_v1(const RacMobyFrameIndicesV1 &indices,
                                     const RacMobySequenceBankV1 &bank,
                                     const RacMobySequenceSetLimitsV1 limits) {
  validate_bank(bank, limits);
  return resolve_frames(indices, bank);
}

RacMobySequenceSetResultV1
set_rac_moby_sequence_v1(const std::uint64_t sequence_argument_bits,
                         const std::uint64_t frame_argument_bits,
                         const std::uint8_t current_sequence_flags,
                         const RacMobySequenceBankV1 &bank,
                         const RacMobySequenceSetLimitsV1 limits) {
  validate_bank(bank, limits);
  const auto initial_key =
      static_cast<std::uint32_t>(sequence_argument_bits) & 0x3fffffffU;
  const auto &initial = sequence_at(bank, initial_key);
  // LBU count is positive64. SLT/MOVN retain full signed64 input until
  // following ADDIU/SB instructions truncate the selected frame word/byte.
  const bool input_less = std::bit_cast<std::int64_t>(frame_argument_bits) <
                          static_cast<std::int64_t>(initial.frame_count);
  const std::uint32_t selected_word =
      input_less ? static_cast<std::uint32_t>(frame_argument_bits)
                 : std::uint32_t{initial.frame_count} - 1U;
  RacMobySequenceSetResultV1 result;
  result.indices.previous_sequence =
      static_cast<std::uint8_t>(sequence_argument_bits);
  result.indices.previous_frame = static_cast<std::uint8_t>(selected_word);
  result.indices.current_frame = static_cast<std::uint8_t>(selected_word + 1U);
  // Actual source re-reads the same sequence/count; the explicit resolved
  // source owner is disjoint from these actor byte writes and unchanged.
  const std::int32_t last_frame =
      static_cast<std::int32_t>(initial.frame_count) - 1;
  if (last_frame < static_cast<std::int32_t>(result.indices.current_frame)) {
    result.indices.current_frame = static_cast<std::uint8_t>(last_frame);
  }
  result.indices.current_sequence =
      static_cast<std::uint8_t>(sequence_argument_bits);
  if (result.indices.current_frame >= initial.frame_count) {
    result.indices.current_frame = 0U;
  }
  result.references = resolve_frames(result.indices, bank);
  result.animation_rate_bits =
      first_word(bank, result.references.previous_frame_reference);
  result.sequence_flags =
      static_cast<std::uint8_t>(current_sequence_flags & 0xfdU);
  return result;
}

} // namespace openrc
