#include "openrc/ee_cop1_numeric.hpp"

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

} // namespace

int main() {
  try {
    test_zero_and_exact_endpoints();
    test_all_exponents_and_fraction_boundaries();
    test_full_signed_halfword_domain();
    test_precision_transitions_and_raw_samples();
    test_ctc1_writable_fields_and_fixed_rounding();
    std::cout << "ee_cop1_numeric_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ee_cop1_numeric_tests: " << error.what() << '\n';
    return 1;
  }
}
