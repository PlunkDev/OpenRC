#include "openrc/dvp_vu_numeric.hpp"
#include "openrc/ee_cop1_numeric.hpp"
#include "ps2_mul_reference_oracle.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void expect(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

// All fixtures below are independently generated contract tests, not copied
// external test tables or newly captured console output.
void test_zero_and_exact_endpoints() {
  using openrc::EeCop1ConversionResultV1;
  expect(!EeCop1ConversionResultV1::fcsr_effects_qualified,
         "value helpers must not qualify FCSR transitions");
  expect(openrc::ee_cop1_cvt_s_w_bits_v1(0U) ==
             EeCop1ConversionResultV1{0U, false},
         "integer zero converts to positive zero");
  expect(openrc::ee_cop1_cvt_s_w_bits_v1(0x80000000U) ==
             EeCop1ConversionResultV1{0xcf000000U, false},
         "INT_MIN is an exact signed-word conversion");
  expect(openrc::ee_cop1_cvt_s_w_bits_v1(0x7fffffffU) ==
             EeCop1ConversionResultV1{0x4effffffU, false},
         "INT_MAX truncates rather than rounds up");
  for (const auto sign : {0U, 0x80000000U}) {
    for (const auto fraction : {0U, 1U, 0x345678U, 0x7fffffU}) {
      expect(openrc::ee_cop1_cvt_w_s_bits_v1(sign | fraction) ==
                 EeCop1ConversionResultV1{0U, false},
             "every exponent-zero sample converts to integer zero");
    }
  }
  expect(openrc::ee_cop1_cvt_w_s_bits_v1(0xcf000000U) ==
             EeCop1ConversionResultV1{0x80000000U, true},
         "negative endpoint takes the encoded clamp path, not an I flag");
  expect(openrc::ee_cop1_cvt_w_s_bits_v1(0x4f000000U) ==
             EeCop1ConversionResultV1{0x7fffffffU, true},
         "positive endpoint saturates to signed maximum");
  expect(openrc::ee_cop1_cvt_w_s_bits_v1(0x4effffffU) ==
             EeCop1ConversionResultV1{0x7fffff80U, false},
         "positive saturation predecessor stays unclamped");
  expect(openrc::ee_cop1_cvt_w_s_bits_v1(0xceffffffU) ==
             EeCop1ConversionResultV1{0x80000080U, false},
         "negative saturation predecessor stays unclamped");
}

// Reference bit weighting, intentionally not the production grouped shift.
std::uint32_t truncated_word_reference(const std::uint32_t bits) {
  const auto exponent = static_cast<int>((bits >> 23U) & 255U) - 127;
  const bool negative = (bits & 0x80000000U) != 0U;
  if (exponent >= 31) {
    return negative ? 0x80000000U : 0x7fffffffU;
  }
  std::uint32_t magnitude = 0U;
  const auto significand = (bits & 0x7fffffU) | 0x800000U;
  for (int bit = 0; bit < 24; ++bit) {
    const auto place = exponent - 23 + bit;
    if (place >= 0 && (significand & (1U << bit)) != 0U) {
      magnitude |= 1U << place;
    }
  }
  return negative ? std::uint32_t{0U} - magnitude : magnitude;
}

void check_single_to_word(const std::uint32_t bits) {
  const auto result = openrc::ee_cop1_cvt_w_s_bits_v1(bits);
  expect(result.bits == truncated_word_reference(bits),
         "single-to-word differs from independent bit weighting");
  expect(result.clamped == (((bits >> 23U) & 255U) > 157U),
         "clamp marker must follow encoded exponent, not sign or result");
}

void test_all_exponents_and_fraction_boundaries() {
  constexpr std::array fractions{0U,        1U,        0x2aaaaaU,
                                 0x3fffffU, 0x400000U, 0x400001U,
                                 0x555555U, 0x7ffffeU, 0x7fffffU};
  for (std::uint32_t exponent = 0U; exponent < 256U; ++exponent) {
    for (const auto fraction : fractions) {
      for (const auto sign : {0U, 0x80000000U}) {
        check_single_to_word(sign | (exponent << 23U) | fraction);
      }
    }
  }
  // A discarded fraction never rounds the magnitude away from zero.
  for (const auto bits : {0x3ffffffeU, 0x3fffffffU, 0xbffffffeU, 0xbfffffffU}) {
    const auto expected = (bits & 0x80000000U) != 0U ? 0xffffffffU : 1U;
    expect(openrc::ee_cop1_cvt_w_s_bits_v1(bits).bits == expected,
           "fractional conversion must truncate, not use nearest or floor");
  }
}

std::uint64_t decoded_integer_magnitude(const std::uint32_t bits) {
  if ((bits & 0x7fffffffU) == 0U) {
    return 0U;
  }
  const auto exponent = static_cast<int>((bits >> 23U) & 255U) - 127;
  expect(exponent >= 0 && exponent <= 31,
         "signed-word output must be a bounded finite integer");
  const auto significand = std::uint64_t{bits & 0x7fffffU} | 0x800000ULL;
  return exponent < 23 ? significand >> (23 - exponent)
                       : significand << (exponent - 23);
}

void check_word_to_single(const std::uint32_t word) {
  const bool negative = (word & 0x80000000U) != 0U;
  const auto magnitude = negative ? std::uint32_t{0U} - word : word;
  const auto result = openrc::ee_cop1_cvt_s_w_bits_v1(word);
  expect(!result.clamped, "word-to-single never uses saturation");
  expect((result.bits & 0x80000000U) == (word & 0x80000000U),
         "word-to-single sign differs");
  const auto decoded = decoded_integer_magnitude(result.bits);
  expect(decoded <= magnitude, "word-to-single rounded away from zero");
  unsigned width = 0U;
  for (auto remaining = magnitude; remaining != 0U; remaining >>= 1U) {
    ++width;
  }
  const auto exponent = (result.bits >> 23U) & 255U;
  expect(exponent == (width == 0U ? 0U : width + 126U),
         "word-to-single must be normalized");
  const std::uint64_t quantum =
      width > 24U ? std::uint64_t{1U} << (width - 24U) : 1U;
  expect(magnitude - decoded < quantum,
         "word-to-single did not choose the greatest representable magnitude");
  expect(openrc::ee_cop1_cvt_w_s_bits_v1(result.bits).bits ==
             (negative ? std::uint32_t{0U} - static_cast<std::uint32_t>(decoded)
                       : static_cast<std::uint32_t>(decoded)),
         "conversion roundtrip differs from the truncated integer");
}

void test_full_signed_halfword_domain() {
  for (std::uint32_t half = 0U; half < 65536U; ++half) {
    const auto word = half < 32768U ? half : half | 0xffff0000U;
    check_word_to_single(word);
    const auto single = openrc::ee_cop1_cvt_s_w_bits_v1(word);
    expect(openrc::ee_cop1_cvt_w_s_bits_v1(single.bits).bits == word,
           "every signed halfword roundtrip must be exact");
  }
}

void test_precision_transitions_and_raw_samples() {
  for (unsigned power = 0U; power < 31U; ++power) {
    const auto center = 1U << power;
    for (std::uint32_t distance = 0U; distance < 260U; ++distance) {
      if (distance <= center) {
        check_word_to_single(center - distance);
        check_word_to_single(std::uint32_t{0U} - (center - distance));
      }
      if (distance <= 0x7fffffffU - center) {
        check_word_to_single(center + distance);
        check_word_to_single(std::uint32_t{0U} - (center + distance));
      }
    }
  }
  std::uint32_t state = 0x26d50731U;
  for (std::uint32_t sample = 0U; sample < 65536U; ++sample) {
    state = state * 1664525U + 1013904223U;
    check_word_to_single(state);
    check_single_to_word(state);
  }
}

void test_ctc1_writable_fields_and_fixed_rounding() {
  constexpr std::uint32_t writable = 0x0083c078U;
  constexpr std::uint32_t fixed = 0x01000001U;
  expect(openrc::ee_cop1_ctc1_fcsr_bits_v1(0U) == fixed,
         "CTC1 zero must preserve the hardware constant fields");
  for (unsigned bit = 0U; bit < 32U; ++bit) {
    const auto input = 1U << bit;
    expect(openrc::ee_cop1_ctc1_fcsr_bits_v1(input) ==
               (fixed | (input & writable)),
           "CTC1 single-bit projection mismatch");
  }
  constexpr std::array positions{3U, 4U, 5U, 6U, 14U, 15U, 16U, 17U, 23U};
  for (unsigned selection = 0U; selection < 512U; ++selection) {
    std::uint32_t selected_bits = 0U;
    for (unsigned i = 0U; i < positions.size(); ++i) {
      if ((selection & (1U << i)) != 0U) {
        selected_bits |= 1U << positions[i];
      }
    }
    const auto result = openrc::ee_cop1_ctc1_fcsr_bits_v1(selected_bits);
    expect(result == (selected_bits | fixed),
           "CTC1 writable combination mismatch");
    expect(openrc::ee_cop1_ctc1_fcsr_bits_v1(selected_bits | ~writable) ==
               result,
           "reserved fields, enables and RM writes must be ignored");
    expect(openrc::ee_cop1_ctc1_fcsr_bits_v1(result) == result,
           "CTC1 projection must be idempotent");
  }
}

void test_add_sub_signed_zeros_and_encoded_ranges() {
  using openrc::EeCop1AddSubResultV1;
  expect(!EeCop1AddSubResultV1::physical_console_qualified,
         "reference arithmetic must not claim physical-console qualification");
  for (const auto left_sign : {0U, 0x80000000U}) {
    for (const auto right_sign : {0U, 0x80000000U}) {
      for (const auto fraction : {0U, 1U, 0x123456U, 0x7fffffU}) {
        const auto left = left_sign | fraction;
        const auto right = right_sign | (0x7fffffU - fraction);
        expect(
            openrc::ee_cop1_add_bits_v1(left, right) ==
                EeCop1AddSubResultV1{left_sign & right_sign, false, false},
            "input exp0 fractions must flush silently with signed-zero rules");
        expect(openrc::ee_cop1_sub_bits_v1(left, right) ==
                   EeCop1AddSubResultV1{left_sign & (right_sign ^ 0x80000000U),
                                        false, false},
               "SUB must invert the second sign before zero combination");
      }
    }
  }
  for (std::uint32_t exponent = 1U; exponent <= 255U; ++exponent) {
    for (const auto fraction : {0U, 3U, 0x255555U, 0x7fffffU}) {
      for (const auto sign : {0U, 0x80000000U}) {
        const auto bits = sign | (exponent << 23U) | fraction;
        expect(
            openrc::ee_cop1_add_bits_v1(bits, sign) ==
                EeCop1AddSubResultV1{bits, false, false},
            "finite exp255 and ordinary values must survive signed-zero add");
        expect(openrc::ee_cop1_sub_bits_v1(bits, bits) ==
                   EeCop1AddSubResultV1{},
               "exact finite cancellation is positive zero without underflow");
        const auto doubled =
            exponent == 255U ? sign | 0x7fffffffU : bits + 0x00800000U;
        expect(openrc::ee_cop1_add_bits_v1(bits, bits) ==
                   EeCop1AddSubResultV1{doubled, false, exponent == 255U},
               "doubling must extend exponent255 then saturate only above it");
      }
    }
  }
}

void test_add_sub_guard_distance_and_underflow_remnants() {
  // A power of two has half-sized predecessor spacing. At exponent distance
  // 24 the smaller normal operand contributes one retained guard; at 25 its
  // entire significand disappears. These are generated model discriminators,
  // not imported external operand/output rows.
  for (std::uint32_t exponent = 26U; exponent <= 255U; ++exponent) {
    const auto anchor = exponent << 23U;
    for (const auto fraction : {0U, 0x2468acU, 0x400000U, 0x7fffffU}) {
      const auto guard = ((exponent - 24U) << 23U) | fraction;
      const auto lost = ((exponent - 25U) << 23U) | fraction;
      expect(openrc::ee_cop1_add_bits_v1(anchor, guard).bits == anchor,
             "positive guard must truncate after result normalization");
      expect(openrc::ee_cop1_sub_bits_v1(anchor, guard).bits == anchor - 1U,
             "one guard must survive subtractive normalization");
      expect(
          openrc::ee_cop1_sub_bits_v1(anchor, lost).bits == anchor,
          "a second guard must not be invented by exact-rational arithmetic");
    }
  }
  for (std::uint32_t difference = 1U; difference <= 4095U; ++difference) {
    auto fraction = difference;
    while (fraction < 0x00800000U) {
      fraction *= 2U;
    }
    fraction &= 0x007fffffU;
    for (const auto sign : {0U, 0x80000000U}) {
      const auto value = openrc::ee_cop1_sub_bits_v1(
          sign | (0x00800000U + difference), sign | 0x00800000U);
      expect(value.bits == (sign | fraction) && value.underflow &&
                 !value.overflow,
             "underflow must retain normalized fraction, not gradual bits");
      expect(openrc::ee_cop1_add_bits_v1(value.bits, 0x3e800000U).bits ==
                 0x3e800000U,
             "underflow remnant becomes zero when reused as an exp0 input");
    }
  }
}

void test_add_sub_exact_integer_domain_and_shared_value_boundary() {
  // Every signed halfword plus/minus these integers is exactly representable.
  // This oracle uses integer arithmetic and the separately tested conversion,
  // rather than restating the aligned-significand implementation.
  constexpr std::array<std::int32_t, 7> deltas{-257, -16, -1, 0, 1, 16, 257};
  for (std::int32_t integer = -32768; integer < 32768; ++integer) {
    const auto left =
        openrc::ee_cop1_cvt_s_w_bits_v1(static_cast<std::uint32_t>(integer))
            .bits;
    for (const auto delta : deltas) {
      const auto right =
          openrc::ee_cop1_cvt_s_w_bits_v1(static_cast<std::uint32_t>(delta))
              .bits;
      const auto add = openrc::ee_cop1_add_bits_v1(left, right);
      const auto sub = openrc::ee_cop1_sub_bits_v1(left, right);
      expect(add.bits == openrc::ee_cop1_cvt_s_w_bits_v1(
                             static_cast<std::uint32_t>(integer + delta))
                             .bits &&
                 !add.underflow && !add.overflow,
             "exact integer-domain ADD disagrees with independent word sum");
      expect(sub.bits == openrc::ee_cop1_cvt_s_w_bits_v1(
                             static_cast<std::uint32_t>(integer - delta))
                             .bits &&
                 !sub.underflow && !sub.overflow,
             "exact integer-domain SUB disagrees with independent word "
             "difference");
    }
  }
  std::uint32_t state = 0x8c9b6301U;
  for (unsigned sample = 0U; sample < 65536U; ++sample) {
    state = state * 1664525U + 1013904223U;
    const auto left = state;
    state = state * 1664525U + 1013904223U;
    const auto right = state;
    const auto add = openrc::ee_cop1_add_bits_v1(left, right);
    const auto sub = openrc::ee_cop1_sub_bits_v1(left, right);
    expect(
        add == openrc::ee_cop1_add_bits_v1(right, left),
        "reference ADD must be commutative without affecting MUL assumptions");
    expect(sub == openrc::ee_cop1_add_bits_v1(left, right ^ 0x80000000U),
           "SUB sign-adjustment identity failed");
    for (const auto subtract : {false, true}) {
      const auto ee = subtract ? sub : add;
      const auto vu = subtract ? openrc::dvp_vu_sub_bits_v1(left, right)
                               : openrc::dvp_vu_add_bits_v1(left, right);
      expect(ee.bits == vu.bits && ee.underflow == vu.underflow &&
                 ee.overflow == vu.overflow,
             "extracted common value rule changed an existing VU result");
      expect(vu.zero == ((vu.bits & 0x7f800000U) == 0U) &&
                 vu.sign == ((vu.bits & 0x80000000U) != 0U),
             "VU-only flag adapter must remain independent of EE FCSR");
    }
  }
}

void test_add_sub_fcsr_events_and_persistence() {
  const auto ordinary = openrc::ee_cop1_add_bits_v1(0x40800000U, 0x40800000U);
  const auto underflow = openrc::ee_cop1_sub_bits_v1(0x00800007U, 0x00800000U);
  const auto overflow = openrc::ee_cop1_add_bits_v1(0x7ff00000U, 0x7ff00000U);
  constexpr std::array positions{3U, 4U, 5U, 6U, 14U, 15U, 16U, 17U, 23U};
  constexpr std::uint32_t mutable_bits = 0x0000c018U;
  for (unsigned selection = 0U; selection < 512U; ++selection) {
    auto prior = 0x01000001U;
    for (unsigned i = 0U; i < positions.size(); ++i) {
      if ((selection & (1U << i)) != 0U) {
        prior |= 1U << positions[i];
      }
    }
    for (const auto &event : {ordinary, underflow, overflow}) {
      const auto current = openrc::ee_cop1_add_sub_fcsr_bits_v1(prior, event);
      expect((current & ~mutable_bits) == (prior & ~mutable_bits),
             "ADD/SUB must preserve I/D causes, C and unrelated FCSR bits");
      expect(((current >> 14U) & 1U) == event.underflow &&
                 ((current >> 15U) & 1U) == event.overflow,
             "ADD/SUB current U/O causes must replace the preceding events");
      expect(
          ((current >> 3U) & 1U) == (((prior >> 3U) & 1U) | event.underflow) &&
              ((current >> 4U) & 1U) == (((prior >> 4U) & 1U) | event.overflow),
          "ADD/SUB sticky U/O must accumulate rather than be replaced");
      expect(openrc::ee_cop1_add_sub_fcsr_bits_v1(current, event) == current,
             "reapplying one event must not clear sticky state");
    }
  }
  auto state = openrc::ee_cop1_ctc1_fcsr_bits_v1(0x00830060U);
  const auto preserved = state & ~mutable_bits;
  state = openrc::ee_cop1_add_sub_fcsr_bits_v1(state, overflow);
  expect((state & mutable_bits) == 0x8010U, "overflow did not set O/SO");
  state = openrc::ee_cop1_add_sub_fcsr_bits_v1(state, ordinary);
  expect((state & mutable_bits) == 0x10U, "ordinary ADD did not clear O cause");
  state = openrc::ee_cop1_add_sub_fcsr_bits_v1(state, underflow);
  expect((state & mutable_bits) == 0x4018U, "underflow lost prior SO sticky");
  state = openrc::ee_cop1_add_sub_fcsr_bits_v1(state, ordinary);
  expect((state & mutable_bits) == 0x18U &&
             (state & ~mutable_bits) == preserved,
         "ordered ADD/SUB events lost sticky or unrelated state");
}

void test_mul_ordered_value_and_fcsr_boundary() {
  std::uint32_t generator = 0x9108732dU;
  for (std::uint32_t sample = 0U; sample < 65536U; ++sample) {
    generator = generator * 1664525U + 1013904223U;
    const auto left = generator;
    generator = generator * 1664525U + 1013904223U;
    const auto right = generator;
    const auto expected = openrc_test::mul_oracle(left, right);
    const auto value = openrc::ee_cop1_mul_bits_v1(left, right);
    expect(value.bits == expected.bits &&
               value.underflow == expected.underflow &&
               value.overflow == expected.overflow,
           "EE MUL differs from independent column oracle");
    const auto vu = openrc::dvp_vu_mul_bits_v1(left, right);
    expect(value.bits == vu.bits && value.underflow == vu.underflow &&
               value.overflow == vu.overflow,
           "shared MUL value must not conflate EE and VU state types");
    const auto prior = sample * 0x10001U;
    const auto fcsr = openrc::ee_cop1_mul_fcsr_bits_v1(prior, value);
    const auto events =
        (value.underflow ? 0x4008U : 0U) | (value.overflow ? 0x8010U : 0U);
    expect(fcsr == ((prior & ~0xc000U) | events),
           "MUL must replace U/O causes, accumulate stickies and preserve "
           "other bits");
  }
  static_assert(!openrc::EeCop1MulResultV1::physical_console_qualified);
  static_assert(!openrc::DvpVuMulResultV1::physical_console_qualified);
}

void test_division_exact_scale_and_range() {
  // Algebraic powers of two need no floating host or a duplicate of the
  // divider's redundant remainder recurrence. Include its extended exp255.
  for(std::uint32_t numerator_exp=0;numerator_exp<256;++numerator_exp)
    for(std::uint32_t denominator_exp=0;denominator_exp<256;++denominator_exp)
      for(const auto sign:std::array{0U,0x80000000U}) {
        const int exponent=static_cast<int>(numerator_exp)-static_cast<int>(denominator_exp)+127;
        const auto expected=denominator_exp==0?sign|0x7fffffffU:
            numerator_exp==0||exponent<=0?sign:
            exponent>255?sign|0x7fffffffU:sign|(static_cast<std::uint32_t>(exponent)<<23U);
        const auto result=openrc::ee_cop1_div_bits_v1((numerator_exp<<23U)|sign,denominator_exp<<23U);
        expect(result.bits==expected,"DIV power-of-two scaling/range differs from integer exponent arithmetic");
      }
  for(std::uint32_t exponent=1;exponent<256;++exponent)
    for(std::uint32_t sample=0;sample<256;++sample) {
      const auto value=(exponent<<23U)|((sample*0x7fffffU)/255U);
      expect(openrc::ee_cop1_div_bits_v1(value,0x3f800000U).bits==value,"DIV right identity changed a finite mantissa");
      expect(openrc::ee_cop1_div_bits_v1(value,value).bits==0x3f800000U,"DIV self quotient is not one");
      expect(openrc::ee_cop1_div_bits_v1(value|0x80000000U,value).bits==0xbf800000U,"DIV sign XOR failed");
    }
  for(const auto zero:std::array{0U,1U,0x7fffffU,0x80000000U,0x807fffffU}) {
    expect(openrc::ee_cop1_div_bits_v1(zero,0xbf800000U).bits==((zero^0xbf800000U)&0x80000000U),
        "DIV exponent-zero numerator did not flush with its sign");
    expect(openrc::ee_cop1_div_bits_v1(0x3f800000U,zero).bits==((zero&0x80000000U)|0x7fffffffU),
        "DIV exponent-zero denominator did not saturate with its sign");
  }
  static_assert(!openrc::EeCop1DivResultV1::physical_console_qualified);
  static_assert(!openrc::EeCop1DivResultV1::fcsr_effects_qualified);
}

} // namespace

int main() {
  try {
    test_zero_and_exact_endpoints();
    test_all_exponents_and_fraction_boundaries();
    test_full_signed_halfword_domain();
    test_precision_transitions_and_raw_samples();
    test_ctc1_writable_fields_and_fixed_rounding();
    test_add_sub_signed_zeros_and_encoded_ranges();
    test_add_sub_guard_distance_and_underflow_remnants();
    test_add_sub_exact_integer_domain_and_shared_value_boundary();
    test_add_sub_fcsr_events_and_persistence();
    test_mul_ordered_value_and_fcsr_boundary();
    test_division_exact_scale_and_range();
    std::cout << "ee_cop1_numeric_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ee_cop1_numeric_tests: " << error.what() << '\n';
    return 1;
  }
}
