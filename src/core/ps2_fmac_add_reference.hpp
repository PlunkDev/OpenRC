#pragma once

#include <bit>
#include <cstdint>

namespace openrc::detail {

// Shared compiler-side value rule, not EE FCSR, VU MAC or an ACC transition.
// Independently derived in OpenRC; extracted unchanged from the existing VU
// adder after separate COP1 corroboration. Finite reference qualification is
// described at the public EE/VU boundaries, not promoted by this reuse.
struct Ps2FmacAddValueV1 {
  std::uint32_t bits = 0U;
  bool underflow = false;
  bool overflow = false;
};

[[nodiscard]] inline std::uint32_t
ps2_add_aligned_significand_v1(const std::uint32_t bits,
                               const int common_exponent) noexcept {
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

[[nodiscard]] inline Ps2FmacAddValueV1
ps2_fmac_add_value_v1(const std::uint32_t left,
                      const std::uint32_t right) noexcept {
  const auto left_exponent = static_cast<int>((left >> 23U) & 0xffU);
  const auto right_exponent = static_cast<int>((right >> 23U) & 0xffU);
  const auto common_exponent =
      left_exponent > right_exponent ? left_exponent : right_exponent;
  const auto left_magnitude =
      ps2_add_aligned_significand_v1(left, common_exponent);
  const auto right_magnitude =
      ps2_add_aligned_significand_v1(right, common_exponent);
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
    return {sign, false, false};
  }

  // The common exponent describes a 25-bit significand (including its guard);
  // normalize to 24 result bits, independently of any host floating point.
  const auto width = static_cast<int>(std::bit_width(magnitude));
  const auto exponent = common_exponent + width - 25;
  if (exponent > 255) {
    return {sign | 0x7fffffffU, false, true};
  }
  const auto normalized =
      width > 24 ? magnitude >> (width - 24) : magnitude << (24 - width);
  const auto fraction = normalized & 0x007fffffU;
  if (exponent <= 0) {
    // Reported EE/VU0 cancellation underflow is not an IEEE denormal: the
    // normalized fraction survives with exponent zero and an underflow event.
    // https://github.com/PCSX2/pcsx2/pull/12001#issuecomment-5074203305
    return {sign | fraction, true, false};
  }
  return {sign | (static_cast<std::uint32_t>(exponent) << 23U) | fraction,
          false, false};
}

} // namespace openrc::detail
