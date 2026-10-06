#include "openrc/dvp_vu_numeric.hpp"

#include "ps2_fmac_add_reference.hpp"
#include "ps2_fmac_mul_reference.hpp"
#include "ps2_fmac_acc_reference.hpp"
#include "ps2_fdiv_reference.hpp"

#include <stdexcept>

namespace openrc {

std::uint32_t dvp_vu_div_bits_v1(std::uint32_t numerator,std::uint32_t denominator) noexcept {
  return detail::ps2_fdiv_value_v1(numerator,denominator);
}
namespace {

[[nodiscard]] DvpVuAddResultV1
add_result(const std::uint32_t bits, const bool underflow = false,
           const bool overflow = false) noexcept {
  return {bits, (bits & 0x7f800000U) == 0U, (bits & 0x80000000U) != 0U,
          underflow, overflow};
}

} // namespace

std::uint32_t dvp_vu_sqrt_bits_v1(const std::uint32_t source) noexcept {
  return detail::ps2_fsqrt_value_v1(source);
}

std::uint32_t dvp_vu_rsqrt_bits_v1(const std::uint32_t numerator,
                                  const std::uint32_t radicand) noexcept {
  return detail::ps2_fdiv_value_v1(numerator,
                                  detail::ps2_fsqrt_value_v1(radicand));
}

DvpVuMulResultV1 dvp_vu_mul_bits_v1(const std::uint32_t left,
                                    const std::uint32_t right) noexcept {
  const auto value = detail::ps2_fmac_mul_value_v1(left, right);
  return {value.bits, (value.bits & 0x7f800000U) == 0U,
          (value.bits & 0x80000000U) != 0U, value.underflow, value.overflow};
}

DvpVuAddResultV1 dvp_vu_add_bits_v1(const std::uint32_t left,
                                    const std::uint32_t right) noexcept {
  const auto value = detail::ps2_fmac_add_value_v1(left, right);
  return add_result(value.bits, value.underflow, value.overflow);
}

DvpVuAddResultV1 dvp_vu_sub_bits_v1(const std::uint32_t left,
                                    const std::uint32_t right) noexcept {
  return dvp_vu_add_bits_v1(left, right ^ 0x80000000U);
}

DvpVuMaddResultV1 dvp_vu_madd_bits_v1(
    const DvpVuAccumulatorLaneV1 accumulator, const std::uint32_t left,
    const std::uint32_t right, const bool subtract) noexcept {
  const auto value = detail::ps2_fmac_acc_value_v1(
      accumulator.bits, accumulator.overflow, left, right, subtract);
  DvpVuMaddResultV1 out;
  out.result = add_result(value.result.bits, value.result.underflow, value.result.overflow);
  out.product = {value.product.bits, (value.product.bits & 0x7f800000U) == 0U,
                 (value.product.bits & 0x80000000U) != 0U,
                 value.product.underflow, value.product.overflow};
  out.sticky_events = static_cast<std::uint8_t>(
      ((out.result.zero || out.product.zero) ? 1U : 0U) |
      ((out.result.sign || out.product.sign) ? 2U : 0U) |
      ((out.result.underflow || out.product.underflow) ? 4U : 0U) |
      ((out.result.overflow || out.product.overflow) ? 8U : 0U));
  return out;
}

std::uint32_t dvp_vu_ftoi_bits_v1(const std::uint32_t source_bits,
                                  const int fractional_bits) {
  if (fractional_bits != 0 && fractional_bits != 4 && fractional_bits != 12 &&
      fractional_bits != 15) {
    throw std::invalid_argument(
        "VU FTOI fractional bits must be 0, 4, 12, or 15");
  }

  // Independent implementation of the documented binary format, truncation,
  // and conversion saturation; no emulator implementation is incorporated.
  // SCE VU User's Manual v6.0 (April 2002), printed pp. 26-28 and 77-80:
  const auto encoded_exponent = (source_bits >> 23U) & 0xffU;
  const auto integer_exponent =
      static_cast<int>(encoded_exponent) - 127 + fractional_bits;
  if (integer_exponent < 0) {
    // This includes both signed zeros and every exponent-zero encoding.
    return 0U;
  }

  const bool negative = (source_bits & 0x80000000U) != 0U;
  if (integer_exponent >= 31) {
    // -2^31 is exactly representable as the negative result limit. Values
    // farther from zero, including every exponent-255 encoding, saturate.
    return negative ? 0x80000000U : 0x7fffffffU;
  }

  const auto significand = (source_bits & 0x007fffffU) | 0x00800000U;
  const auto magnitude = integer_exponent < 23
                             ? significand >> (23 - integer_exponent)
                             : significand << (integer_exponent - 23);
  // Shifts above are bounded to [0, 23] right or [0, 7] left; magnitude is
  // <= 0x7fffff80. Unsigned subtraction defines negative result bits without
  // signed overflow or implementation-dependent narrowing conversions.
  return negative ? std::uint32_t{0U} - magnitude : magnitude;
}

} // namespace openrc
