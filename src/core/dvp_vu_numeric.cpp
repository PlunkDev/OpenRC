#include "openrc/dvp_vu_numeric.hpp"

#include "ps2_fmac_add_reference.hpp"

#include <stdexcept>

namespace openrc {
namespace {

[[nodiscard]] DvpVuAddResultV1 add_result(const std::uint32_t bits,
                                        const bool underflow = false,
                                        const bool overflow = false) noexcept {
  return {bits, (bits & 0x7f800000U) == 0U, (bits & 0x80000000U) != 0U,
          underflow, overflow};
}

} // namespace

DvpVuAddResultV1 dvp_vu_add_bits_v1(const std::uint32_t left,
                                   const std::uint32_t right) noexcept {
  const auto value = detail::ps2_fmac_add_value_v1(left, right);
  return add_result(value.bits, value.underflow, value.overflow);
}

DvpVuAddResultV1 dvp_vu_sub_bits_v1(const std::uint32_t left,
                                   const std::uint32_t right) noexcept {
  return dvp_vu_add_bits_v1(left, right ^ 0x80000000U);
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
