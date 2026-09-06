#include "openrc/rac_moby_admission.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using openrc::evaluate_rac_moby_admission_v1;
using openrc::RacGameplayMobyAdmissionV1;
using openrc::RacMobyAdmissionDecisionV1;
using openrc::RacMobyAdmissionStateV1;

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_error(Callback &&callback, const std::string_view diagnostic) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::RacMobyAdmissionError &error) {
    expect(std::string_view(error.what()).find(diagnostic) !=
               std::string_view::npos,
           "Unexpected admission diagnostic: " + std::string(error.what()));
    return;
  }
  throw std::runtime_error("Missing/malformed reached source input accepted");
}

[[nodiscard]] bool same_state(const RacMobyAdmissionStateV1 &left,
                              const RacMobyAdmissionStateV1 &right) {
  return left.selector_bytes == right.selector_bytes &&
         left.suppression_bytes == right.suppression_bytes &&
         left.primary_bit_words == right.primary_bit_words &&
         left.alternate_bit_words == right.alternate_bit_words &&
         left.registration_slots == right.registration_slots;
}

[[nodiscard]] RacGameplayMobyAdmissionV1
source(const std::uint32_t flags = 1U, const std::int32_t key = 0,
       const std::int32_t selector = 0) {
  return {selector, flags, key, 7, 22};
}

[[nodiscard]] RacMobyAdmissionStateV1 state_with_explicit_inputs() {
  RacMobyAdmissionStateV1 state;
  state.selector_bytes.bytes = {0U, 0xffU, 1U, 0xfeU};
  state.suppression_bytes.bytes.assign(64U, 0U);
  state.primary_bit_words.assign(2U, 0U);
  state.alternate_bit_words.assign(2U, 0U);
  return state;
}

void supply_registration_row(RacMobyAdmissionStateV1 &state) {
  state.registration_slots.emplace();
  for (std::size_t index = 0U; index < state.registration_slots->size();
       ++index) {
    (*state.registration_slots)[index] = {
        0, static_cast<std::uint16_t>(0x8100U + index)};
  }
}

void test_all_policy_combinations_and_priorities() {
  for (std::uint32_t low_flags = 0U; low_flags < 64U; ++low_flags) {
    for (const auto higher : {0U, 0x80000000U}) {
      for (const auto selector : {0U, 0xffU, 1U, 0xfeU}) {
        for (const bool suppressed : {false, true}) {
          for (const bool primary : {false, true}) {
            for (const bool alternate : {false, true}) {
              const auto flags = low_flags | higher;
              const auto input = source(flags);
              const auto before_input = input;
              auto state = state_with_explicit_inputs();
              supply_registration_row(state);
              state.selector_bytes.bytes[0U] =
                  static_cast<std::uint8_t>(selector);
              state.suppression_bytes.bytes[0U] = suppressed ? 7U : 0U;
              state.primary_bit_words[0U] = primary ? 1U : 0U;
              state.alternate_bit_words[0U] = alternate ? 1U : 0U;
              const auto before = state;

              bool accepted = true;
              std::uint32_t maximum = 7U;
              std::uint32_t current = 7U;
              bool transformed = false;
              if (flags != 0U && suppressed) {
                accepted = false;
              } else if (flags != 0U && (flags & 3U) != 0U) {
                const bool alternate_branch = selector == 0xffU;
                accepted = (flags & (alternate_branch ? 2U : 1U)) != 0U;
                if (accepted) {
                  maximum = alternate_branch ? 22U : 7U;
                  transformed = alternate_branch ? alternate : primary;
                  current = transformed ? (maximum + 1U) / 2U : maximum;
                }
              } else if ((flags & 8U) != 0U) {
                accepted = !alternate;
              } else if ((flags & 4U) != 0U) {
                accepted = !primary;
              }

              const auto result = evaluate_rac_moby_admission_v1(input, state);
              const bool registers = (flags & 0x10U) != 0U;
              const auto auxiliary = flags == 0U ? 0xfeU
                                     : registers ? 63U
                                                 : 0xffU;
              expect((result.decision ==
                      RacMobyAdmissionDecisionV1::admitted) == accepted,
                     "Policy combination changed source admission priority");
              expect(result.maximum_bits == maximum &&
                         result.current_bits == current,
                     "Policy combination changed selected count locals");
              expect(result.count_transform.has_value() == transformed &&
                         result.auxiliary_slot_bits == auxiliary,
                     "Policy combination changed count/auxiliary trace");
              expect(result.constructor_fields.has_value() == accepted,
                     "Skipped source record exposed constructor writes");
              if (accepted) {
                const auto &fields = *result.constructor_fields;
                expect(fields.key_bits == 0U && fields.selector_bits == 0U &&
                           fields.current_count_bits == current &&
                           fields.maximum_count_bits == maximum &&
                           fields.auxiliary_slot_bits == auxiliary,
                       "Prepared constructor fields changed source truncation");
              }
              auto expected_state = before;
              if (registers) {
                (*expected_state.registration_slots)[63U].key = 1;
              }
              expect(same_state(state, expected_state),
                     "Policy evaluation changed unrelated source state");
              expect(result.registration_write.has_value() == registers,
                     "Registration trace omitted a source write before skip");
              expect(input == before_input,
                     "Admission mutated its source record");
            }
          }
        }
      }
    }
  }
}

void test_only_reached_inputs_are_required() {
  RacMobyAdmissionStateV1 empty;
  auto bypass = source(0U, -1, -2);
  bypass.primary_count = -65537;
  bypass.alternate_count = std::numeric_limits<std::int32_t>::min();
  const auto untouched = empty;
  const auto result =
      evaluate_rac_moby_admission_v1(bypass, empty, {0U, 0U, 0U});
  expect(
      result.constructor_fields.has_value() &&
          result.auxiliary_slot_bits == 0xfeU &&
          result.constructor_fields->key_bits == 0xffffU &&
          result.constructor_fields->selector_bits == 0xfeU &&
          result.constructor_fields->current_count_bits == 0xffffU &&
          result.maximum_bits == 0xfffeffffU && !result.count_transform,
      "F0 did not preserve raw scalar defaults and bypass unavailable state");
  expect(same_state(empty, untouched), "F0 mutated unavailable source state");

  auto simple = RacMobyAdmissionStateV1{};
  simple.suppression_bytes.bytes = {0U};
  const auto high_only =
      evaluate_rac_moby_admission_v1(source(0x20U, 0, -1), simple);
  expect(high_only.constructor_fields.has_value() &&
             high_only.auxiliary_slot_bits == 0xffU,
         "High source policy bit invented an extra selector/bit gate");
  simple.suppression_bytes.bytes[0U] = 0x80U;
  const auto early_skip =
      evaluate_rac_moby_admission_v1(source(0xfU, 0, -1), simple);
  expect(early_skip.decision == RacMobyAdmissionDecisionV1::skipped,
         "Suppression failed to bypass later unavailable inputs");

  auto state = state_with_explicit_inputs();
  state.primary_bit_words.clear();
  state.alternate_bit_words.clear();
  state.selector_bytes.bytes[0U] = 0xffU;
  expect(evaluate_rac_moby_admission_v1(source(1U), state).decision ==
             RacMobyAdmissionDecisionV1::skipped,
         "Missing selector mode bit read an unreachable count-bit word");
  state.selector_bytes.bytes[0U] = 0U;
  expect(evaluate_rac_moby_admission_v1(source(2U), state).decision ==
             RacMobyAdmissionDecisionV1::skipped,
         "Opposite missing selector mode bit read unreachable count data");
  state.primary_bit_words = {0U};
  expect(evaluate_rac_moby_admission_v1(source(3U), state)
             .constructor_fields.has_value(),
         "Primary selector branch required alternate words");
  state.primary_bit_words.clear();
  state.alternate_bit_words = {0U};
  state.selector_bytes.bytes[0U] = 0xffU;
  expect(evaluate_rac_moby_admission_v1(source(3U), state)
             .constructor_fields.has_value(),
         "Alternate selector branch required primary words");
  state.selector_bytes.bytes.clear();
  expect(evaluate_rac_moby_admission_v1(source(0xcU), state)
             .constructor_fields.has_value(),
         "F8 priority required unreachable F4/selector data");
}

void test_exact_count_word_arithmetic() {
  struct Case {
    std::int32_t input;
    std::uint32_t incremented;
    std::uint32_t adjusted;
    std::uint32_t result;
  };
  constexpr std::array cases{
      Case{0, 1U, 1U, 0U},
      Case{1, 2U, 2U, 1U},
      Case{2, 3U, 3U, 1U},
      Case{7, 8U, 8U, 4U},
      Case{22, 23U, 23U, 11U},
      Case{-1, 0U, 0U, 0U},
      Case{-2, 0xffffffffU, 0U, 0U},
      Case{-3, 0xfffffffeU, 0xffffffffU, 0xffffffffU},
      Case{2147483646, 0x7fffffffU, 0x7fffffffU, 0x3fffffffU},
      Case{2147483647, 0x80000000U, 0x80000001U, 0xc0000000U},
      Case{std::numeric_limits<std::int32_t>::min(), 0x80000001U, 0x80000002U,
           0xc0000001U},
  };
  for (const auto &item : cases) {
    for (const bool alternate : {false, true}) {
      auto state = state_with_explicit_inputs();
      state.selector_bytes.bytes[0U] = alternate ? 0xffU : 0U;
      state.primary_bit_words[0U] = 1U;
      state.alternate_bit_words[0U] = 1U;
      auto input = source(3U);
      input.primary_count = alternate ? 7 : item.input;
      input.alternate_count = alternate ? item.input : 22;
      const auto result = evaluate_rac_moby_admission_v1(input, state);
      expect(result.count_transform.has_value(),
             "Count bit did not emit arithmetic trace");
      const auto &trace = *result.count_transform;
      expect(trace.input_bits == static_cast<std::uint32_t>(item.input) &&
                 trace.incremented_bits == item.incremented &&
                 trace.sign_adjustment == (item.incremented >> 31U) &&
                 trace.adjusted_bits == item.adjusted &&
                 trace.result_bits == item.result &&
                 result.current_bits == item.result &&
                 result.maximum_bits == static_cast<std::uint32_t>(item.input),
             "Count division lost source word wrap or arithmetic right shift");
      expect(result.constructor_fields->current_count_bits ==
                     static_cast<std::uint16_t>(item.result) &&
                 result.constructor_fields->maximum_count_bits ==
                     static_cast<std::uint16_t>(item.input),
             "Count constructor fields were clamped or widened");

      state.primary_bit_words[0U] = 0U;
      state.alternate_bit_words[0U] = 0U;
      const auto untransformed = evaluate_rac_moby_admission_v1(input, state);
      expect(!untransformed.count_transform &&
                 untransformed.current_bits ==
                     static_cast<std::uint32_t>(item.input),
             "Clear bit applied or exposed a discarded delay-slot count "
             "calculation");
    }
  }
}

void test_explicit_signed_byte_windows() {
  auto state = state_with_explicit_inputs();
  state.selector_bytes = {-1, {0xffU, 0U}};
  const auto alternate =
      evaluate_rac_moby_admission_v1(source(2U, 0, -1), state);
  expect(alternate.maximum_bits == 22U && alternate.constructor_fields &&
             alternate.constructor_fields->selector_bits == 0xffU,
         "Explicit selector prefix was not indexed as source S=-1");
  expect(evaluate_rac_moby_admission_v1(source(1U, 0, -1), state).decision ==
             RacMobyAdmissionDecisionV1::skipped,
         "Selector prefix silently used zero instead of caller-supplied 0xff");
  state.selector_bytes.bytes[0U] = 0U;
  expect(evaluate_rac_moby_admission_v1(source(5U, 0, -1), state)
             .constructor_fields.has_value(),
         "Real Veldin F5/S-1 policy shape rejected an explicit cached prefix");

  state.suppression_bytes = {-2, {9U, 0U, 0U}};
  expect(evaluate_rac_moby_admission_v1(source(0x20U, -2), state).decision ==
             RacMobyAdmissionDecisionV1::skipped,
         "Explicit suppression prefix lost a negative source index");
  const auto negative_helper =
      evaluate_rac_moby_admission_v1(source(0x10U, -1), state);
  expect(negative_helper.constructor_fields &&
             !negative_helper.registration_write &&
             negative_helper.auxiliary_slot_bits == 0xffU &&
             !state.registration_slots,
         "Negative helper key did not bypass unavailable registration row");
  expect_error(
      [&] {
        static_cast<void>(
            evaluate_rac_moby_admission_v1(source(4U, -1), state));
      },
      "unsupported source domain");

  constexpr auto minimum = std::numeric_limits<std::int32_t>::min();
  constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
  state.suppression_bytes = {minimum, {0U}};
  const auto min_key =
      evaluate_rac_moby_admission_v1(source(0x10U, minimum), state);
  expect(min_key.constructor_fields &&
             min_key.constructor_fields->key_bits == 0U &&
             !min_key.registration_write,
         "INT_MIN byte window index overflowed or read the registration row");
  expect_error(
      [&] {
        static_cast<void>(
            evaluate_rac_moby_admission_v1(source(0x20U, maximum), state));
      },
      "suppression bytes");
  state.suppression_bytes = {maximum, {0U}};
  supply_registration_row(state);
  const auto before_wrap = state;
  const auto wrapped_key =
      evaluate_rac_moby_admission_v1(source(0x10U, maximum), state);
  expect(wrapped_key.auxiliary_slot_bits == 63U &&
             wrapped_key.registration_write &&
             wrapped_key.registration_write->stored_key == 0 &&
             wrapped_key.constructor_fields->key_bits == 0xffffU &&
             same_state(state, before_wrap),
         "INT_MAX helper key did not wrap K+1 before the low16 source write");
  const auto wrapped_repeat =
      evaluate_rac_moby_admission_v1(source(0x10U, maximum), state);
  expect(wrapped_repeat.auxiliary_slot_bits == 63U &&
             wrapped_repeat.registration_write,
         "Truncated zero registration incorrectly occupied its source slot");
  expect_error(
      [&] {
        static_cast<void>(
            evaluate_rac_moby_admission_v1(source(0x20U, minimum), state));
      },
      "suppression bytes");

  state.suppression_bytes = {0, {0U}};
  state.selector_bytes = {minimum, {0U}};
  expect_error(
      [&] {
        static_cast<void>(
            evaluate_rac_moby_admission_v1(source(1U, 0, minimum), state));
      },
      "unsupported source domain");
  state.selector_bytes = {maximum, {0xffU}};
  expect_error(
      [&] {
        static_cast<void>(
            evaluate_rac_moby_admission_v1(source(2U, 0, maximum), state));
      },
      "unsupported source domain");
  const auto before_failure = state;
  expect_error(
      [&] {
        static_cast<void>(
            evaluate_rac_moby_admission_v1(source(0x11U, 0, minimum), state));
      },
      "selector bytes");
  expect(same_state(state, before_failure),
         "Signed window failure committed pending registration");

  state.selector_bytes = {maximum, {0U, 0U}};
  const auto malformed_before = state;
  expect_error(
      [&] {
        static_cast<void>(
            evaluate_rac_moby_admission_v1(source(0x11U, 0, maximum), state));
      },
      "unsupported source domain");
  expect(same_state(state, malformed_before),
         "Malformed window committed pending registration");
  state.suppression_bytes = {maximum, {0U, 0U}};
  expect_error(
      [&] {
        static_cast<void>(
            evaluate_rac_moby_admission_v1(source(0x10U, maximum), state));
      },
      "signed source-index domain");
  expect(evaluate_rac_moby_admission_v1(source(0U, maximum, minimum), state)
             .constructor_fields.has_value(),
         "F0 validated unvisited malformed byte windows");
}

void test_source_profile_prevents_intrastep_aliases() {
  constexpr openrc::RacMobyAdmissionLimitsV1 wide_limits{100000U, 100000U,
                                                         32768U};
  auto bits_alias = state_with_explicit_inputs();
  supply_registration_row(bits_alias);
  bits_alias.suppression_bytes.bytes.resize(42977U, 0U);
  bits_alias.primary_bit_words.resize(1344U, 0U);
  const auto before_bits = bits_alias;
  // On the source L0 memory map, primary bit word1343 aliases slot63.
  // A staged slot write would change bit0 before this count gate reads it.
  expect_error(
      [&] {
        static_cast<void>(evaluate_rac_moby_admission_v1(
            source(0x11U, 42976), bits_alias, wide_limits));
      },
      "unsupported source domain");
  expect(same_state(bits_alias, before_bits),
         "Unsupported bit alias committed a staged write");

  auto selector_alias = state_with_explicit_inputs();
  supply_registration_row(selector_alias);
  selector_alias.suppression_bytes.bytes.resize(255U, 0U);
  selector_alias.primary_bit_words.resize(64U, 0U);
  selector_alias.alternate_bit_words.resize(64U, 0U);
  selector_alias.selector_bytes = {-75196, {0U}};
  const auto before_selector = selector_alias;
  // The source selector address for -75196 is slot63: storing key255
  // changes its byte to0xff, changing the selected count branch.
  expect_error(
      [&] {
        static_cast<void>(evaluate_rac_moby_admission_v1(
            source(0x13U, 254, -75196), selector_alias, wide_limits));
      },
      "unsupported source domain");
  expect(same_state(selector_alias, before_selector),
         "Unsupported selector alias committed a staged write");

  auto boundary = state_with_explicit_inputs();
  boundary.suppression_bytes.bytes.resize(2049U, 0U);
  boundary.primary_bit_words.assign(64U, 0U);
  boundary.primary_bit_words[63U] = 0x80000000U;
  boundary.selector_bytes = {15, {0U}};
  expect(evaluate_rac_moby_admission_v1(source(1U, 2047, 15), boundary)
                 .current_bits == 4U,
         "Hard source profile rejected its last valid selector/key bit");
  expect_error(
      [&] {
        static_cast<void>(evaluate_rac_moby_admission_v1(
            source(4U, 2048), boundary, wide_limits));
      },
      "unsupported source domain");
  boundary.primary_bit_words.resize(65U, 0U);
  expect_error(
      [&] {
        static_cast<void>(evaluate_rac_moby_admission_v1(
            source(4U, 0), boundary, wide_limits));
      },
      "unsupported source domain");
  boundary.selector_bytes = {16, {0U}};
  expect_error(
      [&] {
        static_cast<void>(evaluate_rac_moby_admission_v1(
            source(1U, 0, 16), boundary, wide_limits));
      },
      "unsupported source domain");
  boundary.selector_bytes = {-2, {0U}};
  expect_error(
      [&] {
        static_cast<void>(evaluate_rac_moby_admission_v1(
            source(1U, 0, -2), boundary, wide_limits));
      },
      "unsupported source domain");

  // An unreachable aliased domain still must not become an invented read.
  bits_alias.suppression_bytes.bytes[42976U] = 1U;
  const auto early_skip = evaluate_rac_moby_admission_v1(
      source(0x11U, 42976), bits_alias, wide_limits);
  expect(early_skip.decision == RacMobyAdmissionDecisionV1::skipped &&
             early_skip.registration_write,
         "Unreached out-of-profile bit gate blocked an actual earlier source "
         "skip");
}

void test_bit_word_boundaries() {
  for (const std::int32_t key : {0, 31, 32, 63}) {
    auto state = state_with_explicit_inputs();
    state.primary_bit_words[static_cast<std::uint32_t>(key) >> 5U] =
        1U << (static_cast<std::uint32_t>(key) & 31U);
    expect(evaluate_rac_moby_admission_v1(source(4U, key), state).decision ==
               RacMobyAdmissionDecisionV1::skipped,
           "Source bit lookup lost a word boundary or sign bit");
    state.alternate_bit_words = state.primary_bit_words;
    expect(evaluate_rac_moby_admission_v1(source(8U, key), state).decision ==
               RacMobyAdmissionDecisionV1::skipped,
           "Alternate source bit lookup changed extraction");
  }
}

void test_registration_scan_order_and_repeat_writes() {
  auto state = state_with_explicit_inputs();
  supply_registration_row(state);
  const auto first = evaluate_rac_moby_admission_v1(source(0x10U, 7), state);
  expect(
      first.auxiliary_slot_bits == 63U && first.registration_write &&
          first.registration_write->previous_key == 0 &&
          first.registration_write->stored_key == 8 &&
          first.registration_write->preserved_auxiliary_bits == 0x813fU,
      "First reservation did not scan from slot63 or preserve adjacent bits");
  const auto repeated = evaluate_rac_moby_admission_v1(source(0x10U, 7), state);
  expect(repeated.auxiliary_slot_bits == 63U && repeated.registration_write &&
             repeated.registration_write->previous_key == 8 &&
             repeated.registration_write->stored_key == 8,
         "Matching reservation failed to record the repeated source SH");
  const auto next = evaluate_rac_moby_admission_v1(source(0x10U, 8), state);
  expect(next.auxiliary_slot_bits == 62U,
         "Reservation did not descend past occupied slot");

  auto vacancy = state_with_explicit_inputs();
  supply_registration_row(vacancy);
  (*vacancy.registration_slots)[62U].key = 8;
  const auto duplicate =
      evaluate_rac_moby_admission_v1(source(0x10U, 7), vacancy);
  expect(duplicate.auxiliary_slot_bits == 63U &&
             (*vacancy.registration_slots)[62U].key == 8 &&
             (*vacancy.registration_slots)[63U].key == 8,
         "Reservation searched all existing keys before the first empty slot");

  auto full = state_with_explicit_inputs();
  supply_registration_row(full);
  for (auto &slot : *full.registration_slots) {
    slot.key = -1;
  }
  const auto full_before = full;
  const auto exhausted = evaluate_rac_moby_admission_v1(source(0x10U, 7), full);
  expect(
      exhausted.auxiliary_slot_bits == 0xffU && !exhausted.registration_write &&
          exhausted.constructor_fields.has_value() &&
          same_state(full, full_before),
      "Full reservation row incorrectly skipped admission or modified state");
  (*full.registration_slots)[0U].key = 8;
  const auto last = evaluate_rac_moby_admission_v1(source(0x10U, 7), full);
  expect(last.auxiliary_slot_bits == 0U && last.registration_write,
         "Reservation did not inspect the final source slot0");
}

void test_registration_signed_low16_asymmetry() {
  auto state = state_with_explicit_inputs();
  supply_registration_row(state);
  state.suppression_bytes.bytes.resize(65537U, 0U);
  const auto first =
      evaluate_rac_moby_admission_v1(source(0x10U, 32767), state);
  const auto second =
      evaluate_rac_moby_admission_v1(source(0x10U, 32767), state);
  expect(first.auxiliary_slot_bits == 63U &&
             second.auxiliary_slot_bits == 62U &&
             (*state.registration_slots)[63U].key == -32768 &&
             (*state.registration_slots)[62U].key == -32768,
         "Reservation compared truncated unsigned keys instead of signed "
         "LH/full wanted");
  const auto before_zero = state;
  const auto zero = evaluate_rac_moby_admission_v1(source(0x10U, 65535), state);
  expect(
      zero.auxiliary_slot_bits == 61U && zero.registration_write &&
          zero.registration_write->stored_key == 0 &&
          same_state(state, before_zero),
      "Truncated zero reservation failed to preserve the source store event");
  const auto widened =
      evaluate_rac_moby_admission_v1(source(0x10U, 65536), state);
  expect(
      widened.auxiliary_slot_bits == 61U &&
          widened.registration_write->stored_key == 1 &&
          widened.constructor_fields->key_bits == 0U,
      "Reservation lost separate wanted width and constructor key truncation");
}

void test_skip_commits_early_write_and_authored_order() {
  auto state = state_with_explicit_inputs();
  supply_registration_row(state);
  state.suppression_bytes.bytes[7U] = 1U;
  const auto skipped =
      evaluate_rac_moby_admission_v1(source(0x13U, 7, -1), state);
  expect(skipped.decision == RacMobyAdmissionDecisionV1::skipped &&
             !skipped.constructor_fields && skipped.registration_write &&
             (*state.registration_slots)[63U].key == 8,
         "Source rejection undid an early reservation or claimed construction");
  const auto later = evaluate_rac_moby_admission_v1(source(0x10U, 8), state);
  expect(later.auxiliary_slot_bits == 62U,
         "Later authored record did not observe skipped record reservation");

  auto reversed = state_with_explicit_inputs();
  supply_registration_row(reversed);
  reversed.suppression_bytes.bytes[7U] = 1U;
  const auto earlier =
      evaluate_rac_moby_admission_v1(source(0x10U, 8), reversed);
  const auto later_skip =
      evaluate_rac_moby_admission_v1(source(0x10U, 7), reversed);
  expect(earlier.auxiliary_slot_bits == 63U &&
             later_skip.auxiliary_slot_bits == 62U &&
             !same_state(reversed, state),
         "Per-record admission evaluation discarded authored allocation order");

  auto replay = state_with_explicit_inputs();
  supply_registration_row(replay);
  replay.suppression_bytes.bytes[7U] = 1U;
  static_cast<void>(
      evaluate_rac_moby_admission_v1(source(0x13U, 7, -1), replay));
  static_cast<void>(evaluate_rac_moby_admission_v1(source(0x10U, 8), replay));
  expect(same_state(replay, state),
         "Ordered admission state is not deterministic");
}

void test_caller_refreshes_cross_step_alias_snapshot() {
  auto state = state_with_explicit_inputs();
  supply_registration_row(state);
  const auto first = evaluate_rac_moby_admission_v1(source(0x10U, 0), state);
  expect(first.registration_write &&
             first.registration_write->slot_index == 63U,
         "Cross-step fixture did not obtain its source registration write");
  // For the original selected L0, suppression index -451208 aliases the
  // key low byte at registration slot63. The caller owns source memory and
  // must refresh that read view after the first operation; the evaluator
  // intentionally does not synchronize independent source-memory views.
  state.suppression_bytes = {
      -451208,
      {static_cast<std::uint8_t>(first.registration_write->stored_key)}};
  const auto second =
      evaluate_rac_moby_admission_v1(source(0x20U, -451208), state);
  expect(
      second.decision == RacMobyAdmissionDecisionV1::skipped &&
          !second.registration_write && !second.constructor_fields,
      "Caller-refreshed alias snapshot did not reflect preceding source write");
}

void test_malformed_reached_inputs_and_transaction_rollback() {
  auto state = state_with_explicit_inputs();
  supply_registration_row(state);
  const auto check_rollback =
      [](const RacGameplayMobyAdmissionV1 &input,
         RacMobyAdmissionStateV1 candidate, const std::string_view diagnostic,
         const openrc::RacMobyAdmissionLimitsV1 limits = {}) {
        const auto before = candidate;
        expect_error(
            [&] {
              static_cast<void>(
                  evaluate_rac_moby_admission_v1(input, candidate, limits));
            },
            diagnostic);
        expect(same_state(candidate, before),
               "Failed admission left a staged persistent write or changed "
               "unrelated data");
      };

  auto missing = state;
  missing.registration_slots.reset();
  check_rollback(source(0x10U), missing, "registration row");
  missing = state;
  missing.suppression_bytes.bytes.clear();
  check_rollback(source(0x10U), missing, "suppression bytes");
  missing = state;
  missing.selector_bytes.bytes.clear();
  check_rollback(source(0x11U), missing, "selector bytes");
  missing = state;
  missing.primary_bit_words.clear();
  check_rollback(source(0x11U), missing, "primary bit words");
  check_rollback(source(0x14U), missing, "primary bit words");
  missing = state;
  missing.selector_bytes.bytes[0U] = 0xffU;
  missing.alternate_bit_words.clear();
  check_rollback(source(0x12U), missing, "alternate bit words");
  check_rollback(source(0x18U), missing, "alternate bit words");

  check_rollback(source(0x11U, 0, -1), state, "selector bytes");
  check_rollback(source(0x11U, 0, 4), state, "selector bytes");
  check_rollback(source(0x10U, 64), state, "suppression bytes");
  // The source helper bypasses a negative key without requiring its row;
  // the subsequently reached pre-table suppression address is unsupported.
  missing = state;
  missing.registration_slots.reset();
  check_rollback(source(0x10U, -1), missing, "suppression bytes");
  check_rollback(source(0x10U, std::numeric_limits<std::int32_t>::max()), state,
                 "suppression bytes");

  missing = state;
  missing.suppression_bytes.bytes.resize(65U, 0U);
  check_rollback(source(0x14U, 64), missing, "primary bit words");
  check_rollback(source(0x18U, 64), missing, "alternate bit words");
  check_rollback(source(0x10U), state, "input limit", {256U, 63U, 32768U});
  check_rollback(source(0x11U), state, "input limit", {3U, 64U, 32768U});
  check_rollback(source(0x11U), state, "input limit", {4U, 64U, 1U});
  check_rollback(source(0x18U), state, "input limit", {4U, 64U, 1U});
  check_rollback(source(0x10U), state, "input limit", {0U, 0U, 0U});

  // A limit on an unvisited table must not invent a source read/failure.
  const auto unvisited =
      evaluate_rac_moby_admission_v1(source(0x20U), state, {0U, 64U, 0U});
  expect(unvisited.constructor_fields.has_value(),
         "Unused limits rejected a bounded reached path");
}

} // namespace

int main() {
  try {
    test_all_policy_combinations_and_priorities();
    test_only_reached_inputs_are_required();
    test_exact_count_word_arithmetic();
    test_explicit_signed_byte_windows();
    test_source_profile_prevents_intrastep_aliases();
    test_bit_word_boundaries();
    test_registration_scan_order_and_repeat_writes();
    test_registration_signed_low16_asymmetry();
    test_skip_commits_early_write_and_authored_order();
    test_caller_refreshes_cross_step_alias_snapshot();
    test_malformed_reached_inputs_and_transaction_rollback();
    std::cout << "RAC Moby admission tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC Moby admission test failure: " << error.what() << '\n';
    return 1;
  }
}
