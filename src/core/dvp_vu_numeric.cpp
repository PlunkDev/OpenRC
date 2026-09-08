#include "openrc/dvp_vu_numeric.hpp"

#include <bit>
#include <stdexcept>

namespace openrc {
namespace {

[[nodiscard]] DvpVuAddResultV1 add_result(const std::uint32_t bits,
                                        const bool underflow = false,
                                        const bool overflow = false) noexcept {
  return {bits, (bits & 0x7f800000U) == 0U, (bits & 0x80000000U) != 0U,
          underflow, overflow};
}

[[nodiscard]] std::uint32_t aligned_significand(
    const std::uint32_t bits, const int common_exponent) noexcept {
  const auto exponent = static_cast<int>((bits >> 23U) & 0xffU);
  if (exponent == 0) {
    return 0U;
  }
  const auto distance = common_exponent - exponent;
  // A restored significand plus one guard occupies 25 bits. Larger shifts
  // discard it completely; bounding before the shift avoids undefined C++.
  if (distance >= 25) {
    return 0U;
  }
  return (((bits & 0x007fffffU) | 0x00800000U) << 1U) >> distance;
}

} // namespace

DvpVuAddResultV1 dvp_vu_add_bits_v1(const std::uint32_t left,
                                   const std::uint32_t right) noexcept {
  const auto left_exponent = static_cast<int>((left >> 23U) & 0xffU);
  const auto right_exponent = static_cast<int>((right >> 23U) & 0xffU);
  const auto common_exponent =
      left_exponent > right_exponent ? left_exponent : right_exponent;
  const auto left_magnitude = aligned_significand(left, common_exponent);
  const auto right_magnitude = aligned_significand(right, common_exponent);
  auto sign = left & 0x80000000U;
  std::uint32_t magnitude = 0U;
  if (((left ^ right) & 0x80000000U) == 0U) {
    // Each aligned operand is at most 0x1fffffe; their unsigned sum fits in
    // 26 bits. Same-sign zero also retains the common sign.
    magnitude = left_magnitude + right_magnitude;
  } else if (left_magnitude >= right_magnitude) {
    magnitude = left_magnitude - right_magnitude;
    if (magnitude == 0U) {
      sign = 0U;
    }
  } else {
    magnitude = right_magnitude - left_magnitude;
    sign = right & 0x80000000U;
  }
  if (magnitude == 0U) {
    return add_result(sign);
  }

  // This is an independent bit-width derivation of the public numeric rules,
  // not a translated emulator implementation. The common exponent describes
  // a 25-bit significand (including its guard); normalize to 24 result bits.
  const auto width = static_cast<int>(std::bit_width(magnitude));
  const auto exponent = common_exponent + width - 25;
  if (exponent > 255) {
    return add_result(sign | 0x7fffffffU, false, true);
  }
  const auto normalized = width > 24 ? magnitude >> (width - 24)
                                     : magnitude << (24 - width);
  const auto fraction = normalized & 0x007fffffU;
  if (exponent <= 0) {
    // Reported VU0 ADD cancellation underflow is not an IEEE denormal: the
    // normalized fraction survives, while the exponent and zero flag say 0.
    // https://github.com/PCSX2/pcsx2/pull/12001#issuecomment-5074203305
    return add_result(sign | fraction, true);
  }
  return add_result(sign | (static_cast<std::uint32_t>(exponent) << 23U) |
                    fraction);
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
