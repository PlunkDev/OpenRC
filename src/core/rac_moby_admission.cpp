#include "openrc/rac_moby_admission.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string_view table,
                       const std::string_view reason) {
  throw RacMobyAdmissionError("RAC Moby admission " + std::string(table) + " " +
                              std::string(reason));
}

[[nodiscard]] std::size_t checked_index(const std::int32_t index,
                                        const std::size_t size,
                                        const std::uint64_t maximum,
                                        const std::string_view table) {
  if (index < 0) {
    fail(table, "has an unsupported reached negative source index");
  }
  if (maximum == 0U || size > maximum) {
    fail(table, "exceeds the reached input limit");
  }
  const auto bounded = static_cast<std::uint32_t>(index);
  if (bounded >= size) {
    fail(table, "is missing a reached source element");
  }
  return static_cast<std::size_t>(bounded);
}

[[nodiscard]] std::uint8_t read_byte(const RacMobyAdmissionByteWindowV1 &window,
                                     const std::int32_t index,
                                     const std::uint64_t maximum,
                                     const std::string_view table) {
  if (maximum == 0U || window.bytes.size() > maximum) {
    fail(table, "exceeds the reached input limit");
  }
  const auto available_indices = static_cast<std::uint64_t>(
      static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) -
      static_cast<std::int64_t>(window.first_index) + 1);
  if (window.bytes.size() > available_indices) {
    fail(table, "window extends beyond the signed source-index domain");
  }
  const auto relative = static_cast<std::int64_t>(index) -
                        static_cast<std::int64_t>(window.first_index);
  if (relative < 0 ||
      static_cast<std::uint64_t>(relative) >= window.bytes.size()) {
    fail(table, "is missing a reached source element in its explicit window");
  }
  return window.bytes[static_cast<std::size_t>(relative)];
}

[[nodiscard]] bool read_bit(const std::vector<std::uint32_t> &words,
                            const std::int32_t key, const std::uint64_t maximum,
                            const std::string_view table) {
  if (key < 0 || key >= 2048 || words.size() > 64U) {
    fail(table,
         "has an unsupported source domain (keys 0..2047, at most 64 words)");
  }
  const auto raw_key = static_cast<std::uint32_t>(key);
  const auto index = checked_index(static_cast<std::int32_t>(raw_key >> 5U),
                                   words.size(), maximum, table);
  // Source SRAV followed by AND 1 has this same raw-bit result. The
  // unsupported negative-address domain is rejected above, not guessed.
  return ((words[index] >> (raw_key & 31U)) & 1U) != 0U;
}

void stage_registration(const RacGameplayMobyAdmissionV1 &admission,
                        const RacMobyAdmissionStateV1 &state,
                        RacMobyAdmissionResultV1 &result) {
  result.auxiliary_slot_bits = 0xffU;
  if (admission.key_index < 0) {
    return;
  }
  if (!state.registration_slots.has_value()) {
    fail("registration row", "is missing reached source input");
  }
  const auto wanted_bits = static_cast<std::uint32_t>(admission.key_index) + 1U;
  const auto wanted = std::bit_cast<std::int32_t>(wanted_bits);
  const auto stored =
      std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(wanted_bits));
  const auto &slots = *state.registration_slots;
  for (std::size_t cursor = slots.size(); cursor > 0U; --cursor) {
    const auto index = cursor - 1U;
    const auto &slot = slots[index];
    if (static_cast<std::int32_t>(slot.key) == wanted || slot.key == 0) {
      const auto slot_index = static_cast<std::uint8_t>(index);
      result.auxiliary_slot_bits = slot_index;
      result.registration_write = RacMobyAdmissionRegistrationWriteV1{
          slot_index, slot.key, stored, slot.auxiliary_bits};
      return;
    }
  }
}

void apply_count_transform(RacMobyAdmissionResultV1 &result,
                           const std::uint32_t count_bits) {
  RacMobyAdmissionCountTransformV1 transform;
  transform.input_bits = count_bits;
  transform.incremented_bits = count_bits + 1U;
  transform.sign_adjustment = transform.incremented_bits >> 31U;
  transform.adjusted_bits =
      transform.incremented_bits + transform.sign_adjustment;
  transform.result_bits =
      (transform.adjusted_bits >> 1U) | (transform.adjusted_bits & 0x80000000U);
  result.count_transform = transform;
  result.current_bits = transform.result_bits;
}

[[nodiscard]] RacMobyAdmissionResultV1
finish(RacMobyAdmissionResultV1 result,
       const RacGameplayMobyAdmissionV1 &admission,
       RacMobyAdmissionStateV1 &state) noexcept {
  if (result.decision == RacMobyAdmissionDecisionV1::admitted) {
    result.constructor_fields = RacMobyAdmissionConstructorFieldsV1{
        static_cast<std::uint16_t>(admission.key_index),
        static_cast<std::uint16_t>(result.current_bits),
        static_cast<std::uint16_t>(result.maximum_bits),
        static_cast<std::uint8_t>(admission.selector_index),
        result.auxiliary_slot_bits};
  }
  // All reached source accesses have now succeeded. Normal source skip
  // retains this early write; an exception above never modifies the row.
  if (result.registration_write.has_value()) {
    const auto &write = *result.registration_write;
    (*state.registration_slots)[write.slot_index].key = write.stored_key;
  }
  return result;
}

} // namespace

RacMobyAdmissionResultV1
evaluate_rac_moby_admission_v1(const RacGameplayMobyAdmissionV1 &admission,
                               RacMobyAdmissionStateV1 &state,
                               const RacMobyAdmissionLimitsV1 &limits) {
  RacMobyAdmissionResultV1 result;
  result.maximum_bits = static_cast<std::uint32_t>(admission.primary_count);
  result.current_bits = result.maximum_bits;
  const auto flags = admission.policy_bits;
  if (flags == 0U) {
    return finish(result, admission, state);
  }

  result.auxiliary_slot_bits = 0xffU;
  if ((flags & 0x10U) != 0U) {
    stage_registration(admission, state, result);
  }

  if (read_byte(state.suppression_bytes, admission.key_index,
                limits.max_suppression_bytes, "suppression bytes") != 0U) {
    result.decision = RacMobyAdmissionDecisionV1::skipped;
    return finish(result, admission, state);
  }

  if ((flags & 3U) != 0U) {
    if (admission.selector_index < -1 || admission.selector_index > 15) {
      fail("selector bytes",
           "has an unsupported source domain (indices -1..15)");
    }
    const auto selector =
        read_byte(state.selector_bytes, admission.selector_index,
                  limits.max_selector_bytes, "selector bytes");
    if (selector == 0xffU) {
      if ((flags & 2U) == 0U) {
        result.decision = RacMobyAdmissionDecisionV1::skipped;
      } else {
        result.maximum_bits =
            static_cast<std::uint32_t>(admission.alternate_count);
        result.current_bits = result.maximum_bits;
        if (read_bit(state.alternate_bit_words, admission.key_index,
                     limits.max_bit_words, "alternate bit words")) {
          apply_count_transform(result, result.maximum_bits);
        }
      }
    } else if ((flags & 1U) == 0U) {
      result.decision = RacMobyAdmissionDecisionV1::skipped;
    } else if (read_bit(state.primary_bit_words, admission.key_index,
                        limits.max_bit_words, "primary bit words")) {
      apply_count_transform(result, result.maximum_bits);
    }
  } else if ((flags & 8U) != 0U) {
    if (read_bit(state.alternate_bit_words, admission.key_index,
                 limits.max_bit_words, "alternate bit words")) {
      result.decision = RacMobyAdmissionDecisionV1::skipped;
    }
  } else if ((flags & 0xcU) == 4U) {
    if (read_bit(state.primary_bit_words, admission.key_index,
                 limits.max_bit_words, "primary bit words")) {
      result.decision = RacMobyAdmissionDecisionV1::skipped;
    }
  }
  return finish(result, admission, state);
}

} // namespace openrc
