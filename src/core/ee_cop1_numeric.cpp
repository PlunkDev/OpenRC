#include "openrc/ee_cop1_numeric.hpp"

#include <bit>

namespace openrc {

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
