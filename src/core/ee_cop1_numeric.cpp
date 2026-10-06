#include "openrc/ee_cop1_numeric.hpp"

#include "ps2_fmac_add_reference.hpp"
#include "ps2_fmac_mul_reference.hpp"
#include "ps2_fdiv_reference.hpp"
#include "ps2_fmac_acc_reference.hpp"

#include <bit>

namespace openrc {

EeCop1DivResultV1 ee_cop1_div_bits_v1(const std::uint32_t numerator,
                                      const std::uint32_t denominator) noexcept {
  return {detail::ps2_fdiv_value_v1(numerator, denominator)};
}

EeCop1MaddResultV1 ee_cop1_madd_bits_v1(
    const EeCop1AccumulatorV1 accumulator, const std::uint32_t left,
    const std::uint32_t right, const bool subtract) noexcept {
  const auto value = detail::ps2_fmac_acc_value_v1(
      accumulator.bits, accumulator.overflow, left, right, subtract);
  return {{value.result.bits, value.result.underflow, value.result.overflow},
          {value.product.bits, value.product.underflow, value.product.overflow}};
}

EeCop1MulResultV1 ee_cop1_mul_bits_v1(const std::uint32_t left,
                                      const std::uint32_t right) noexcept {
  const auto value = detail::ps2_fmac_mul_value_v1(left, right);
  return {value.bits, value.underflow, value.overflow};
}

std::uint32_t
ee_cop1_mul_fcsr_bits_v1(const std::uint32_t prior_fcsr,
                         const EeCop1MulResultV1 &result) noexcept {
  return ee_cop1_add_sub_fcsr_bits_v1(
      prior_fcsr, {result.bits, result.underflow, result.overflow});
}

EeCop1AddSubResultV1 ee_cop1_add_bits_v1(const std::uint32_t left,
                                         const std::uint32_t right) noexcept {
  const auto value = detail::ps2_fmac_add_value_v1(left, right);
  return {value.bits, value.underflow, value.overflow};
}

EeCop1AddSubResultV1 ee_cop1_sub_bits_v1(const std::uint32_t left,
                                         const std::uint32_t right) noexcept {
  return ee_cop1_add_bits_v1(left, right ^ 0x80000000U);
}

std::uint32_t
ee_cop1_add_sub_fcsr_bits_v1(const std::uint32_t prior_fcsr,
                             const EeCop1AddSubResultV1 &result) noexcept {
  constexpr std::uint32_t cause_underflow = 1U << 14U;
  constexpr std::uint32_t cause_overflow = 1U << 15U;
  constexpr std::uint32_t sticky_underflow = 1U << 3U;
  constexpr std::uint32_t sticky_overflow = 1U << 4U;
  const auto current = prior_fcsr & ~(cause_underflow | cause_overflow);
  return current |
         (result.underflow ? cause_underflow | sticky_underflow : 0U) |
         (result.overflow ? cause_overflow | sticky_overflow : 0U);
}

EeCop1ConversionResultV1
ee_cop1_cvt_s_w_bits_v1(const std::uint32_t source_word) noexcept {
  const auto sign = source_word & 0x80000000U;
  // Unsigned negation also handles INT_MIN without signed overflow.
  const auto magnitude =
      sign != 0U ? std::uint32_t{0U} - source_word : source_word;
  if (magnitude == 0U) {
    return {};
  }
  const auto width = static_cast<int>(std::bit_width(magnitude));
  const auto significand =
      width > 24 ? magnitude >> (width - 24) : magnitude << (24 - width);
  const auto exponent = static_cast<std::uint32_t>(width + 126);
  return {sign | (exponent << 23U) | (significand & 0x007fffffU), false};
}

EeCop1ConversionResultV1
ee_cop1_cvt_w_s_bits_v1(const std::uint32_t source_single) noexcept {
  const auto exponent = (source_single >> 23U) & 0xffU;
  if (exponent < 127U) {
    return {};
  }
  const bool negative = (source_single & 0x80000000U) != 0U;
  if (exponent > 157U) {
    return {negative ? 0x80000000U : 0x7fffffffU, true};
  }
  const auto significand = (source_single & 0x007fffffU) | 0x00800000U;
  // For unclamped operands, right shifts are <=23 and left shifts <=7.
  const auto magnitude = exponent < 150U ? significand >> (150U - exponent)
                                         : significand << (exponent - 150U);
  return {negative ? std::uint32_t{0U} - magnitude : magnitude, false};
}

std::uint32_t
ee_cop1_ctc1_fcsr_bits_v1(const std::uint32_t source_word) noexcept {
  constexpr std::uint32_t writable =
      (1U << 23U) | (0x0fU << 14U) | (0x0fU << 3U);
  constexpr std::uint32_t fixed = (1U << 24U) | 1U;
  return (source_word & writable) | fixed;
}

} // namespace openrc
