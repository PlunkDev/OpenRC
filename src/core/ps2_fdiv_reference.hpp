#pragma once

#include <cstdint>

namespace openrc::detail {

// Independent scalar derivation of the primary author's described radix-2
// redundant divider. The quotient decision observes the upper-byte sum and
// the OR of column23, not a fully carry-propagated remainder. This distinction
// survives into the final bit; exact integer quotient truncation is different.
// No external code is incorporated. See SOURCE_DIV_ACC_REFERENCE_V1.md.
[[nodiscard]] inline int ps2_div_digit_v1(const std::uint32_t parity,
                                         const std::uint32_t carries) noexcept {
  const auto upper = ((parity >> 24U) + (carries >> 24U)) & 255U;
  if (upper == 0U)
    return ((parity | carries) & 0x00800000U) != 0U ? 1 : 0;
  if (upper < 128U) return 1;
  return upper == 255U ? 0 : -1;
}

[[nodiscard]] inline std::uint32_t
ps2_fdiv_value_v1(const std::uint32_t numerator,
                   const std::uint32_t denominator) noexcept {
  const auto sign = (numerator ^ denominator) & 0x80000000U;
  const auto ne = (numerator >> 23U) & 255U;
  const auto de = (denominator >> 23U) & 255U;
  if (de == 0U) return sign | 0x7fffffffU;
  if (ne == 0U) return sign;

  const auto divisor = ((denominator & 0x007fffffU) | 0x00800000U) << 2U;
  std::uint32_t parity = ((numerator & 0x007fffffU) | 0x00800000U) << 2U;
  std::uint32_t carries = 0U;
  std::int32_t quotient = 0;
  int digit = 1;
  for (int column = 24; column >= 0; --column) {
    quotient += digit * static_cast<std::int32_t>(std::uint32_t{1U} << column);
    // Carries always have a clear low bit. A subtracting digit inserts the
    // two's-complement unit there before a full-adder reduction.
    const auto units = carries | (digit > 0 ? 1U : 0U);
    const auto addend = digit > 0 ? ~divisor : digit < 0 ? divisor : 0U;
    const auto next_parity = parity ^ units ^ addend;
    const auto next_carries = ((parity & units) | ((parity | units) & addend)) << 1U;
    // A zero digit must retain the old redundant representation for the
    // selector, even though the next-cycle remainder still passes the adder.
    digit = digit == 0 ? ps2_div_digit_v1(parity, carries)
                      : ps2_div_digit_v1(next_parity, next_carries);
    parity = next_parity << 1U;
    carries = next_carries << 1U;
  }
  auto significand = static_cast<std::uint32_t>(quotient);
  int exponent = static_cast<int>(ne) - static_cast<int>(de) + 126;
  if (significand >= 0x01000000U) { significand >>= 1U; ++exponent; }
  if (exponent > 255) return sign | 0x7fffffffU;
  if (exponent <= 0) return sign;
  return sign | (static_cast<std::uint32_t>(exponent) << 23U) |
         (significand & 0x007fffffU);
}

// Square-root recurrence uses the same redundant remainder/quotient selector.
// For root prefix P and signed new digit d at weight W, the square correction
// is proportional to d*(P+d*W/2). The remainder's column scale supplies the
// remaining factor. All prefix arithmetic fits in signed64; bit reductions
// explicitly retain the hardware's modulo32 column representation.
[[nodiscard]] inline std::uint32_t
ps2_fsqrt_value_v1(const std::uint32_t source) noexcept {
  const auto exponent = (source >> 23U) & 255U;
  if (exponent == 0U) return 0U;
  std::uint32_t parity = ((source & 0x007fffffU) | 0x00800000U) << 1U;
  if ((exponent & 1U) == 0U) parity <<= 1U;
  std::uint32_t carries = 0U;
  std::int64_t prefix = 0;
  int digit = 1;
  for (int column=24;column>=0;--column) {
    const auto half_weight = std::int64_t{1} << column;
    const auto correction = static_cast<std::uint32_t>(prefix + digit * half_weight);
    prefix += digit * (half_weight * 2);
    const auto units = carries | (digit > 0 ? 1U : 0U);
    const auto addend = digit > 0 ? ~correction : digit < 0 ? correction : 0U;
    const auto next_parity = parity ^ units ^ addend;
    const auto next_carries = ((parity & units) | ((parity | units) & addend)) << 1U;
    digit = digit == 0 ? ps2_div_digit_v1(parity,carries)
                      : ps2_div_digit_v1(next_parity,next_carries);
    parity = next_parity << 1U;
    carries = next_carries << 1U;
  }
  return (((exponent + 127U) / 2U) << 23U) |
         ((static_cast<std::uint32_t>(prefix) >> 2U) & 0x007fffffU);
}
} // namespace openrc::detail
