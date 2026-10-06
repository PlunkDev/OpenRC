#pragma once

#include <array>
#include <cstdint>

namespace openrc::detail {

struct Ps2FmacMulValueV1 {
  std::uint32_t bits;
  bool underflow;
  bool overflow;
};

// A full-adder column stores its parity here and its majority one column to
// the left. This is ordinary unsigned integer arithmetic, not host float.
[[nodiscard]] inline std::array<std::uint32_t, 2U>
split_mul_columns_v1(const std::uint32_t a, const std::uint32_t b,
                     const std::uint32_t c) noexcept {
  return {a ^ b ^ c, ((a & b) | (a & c) | (b & c)) << 1U};
}

// Independently derived low-column calculation from the primary author's
// described Booth/reduction topology; not a translation of its software
// implementation. Only columns10..15 can affect this omitted carry. The
// derivation, separate bit-column test oracle and qualification are in
// docs/SOURCE_MULTIPLIER_REFERENCE_V1.md. Both inputs are 24-bit significands.
[[nodiscard]] inline std::uint64_t
ps2_mul_significands_v1(const std::uint32_t left,
                        const std::uint32_t right) noexcept {
  std::array<std::uint32_t, 8U> row{};
  std::array<std::uint32_t, 8U> correction{};
  for (unsigned digit = 0U; digit < row.size(); ++digit) {
    const auto position = digit * 2U;
    const auto group = ((right << 1U) >> position) & 7U;
    // Radix4 digit = low bit + middle bit - twice the high bit.
    const int factor = static_cast<int>(group & 1U) +
                       static_cast<int>((group >> 1U) & 1U) -
                       2 * static_cast<int>(group >> 2U);
    const auto magnitude =
        left * static_cast<std::uint32_t>(factor < 0 ? -factor : factor);
    const auto data = factor < 0 ? ~magnitude : magnitude;
    row[digit] = (data << position) & 0xfc00U;
    correction[digit] = factor < 0 ? 1U << position : 0U;
  }
  const auto deferred = row[5U];
  row[4U] &= ~0x0400U;
  row[5U] &= ~0x0c00U;
  const auto left_first = split_mul_columns_v1(row[1U], row[2U], row[3U]);
  auto right_first = split_mul_columns_v1(row[4U], row[5U], row[6U]);
  // These injections occupy otherwise empty columns. The two column10
  // units below can carry to11; keep that addition before the later split.
  right_first[1U] += correction[6U] + (deferred & 0x0800U);
  row[7U] += (deferred & 0x0400U) + correction[5U];
  const auto left_second =
      split_mul_columns_v1(row[0U], left_first[0U], left_first[1U]);
  const auto right_second =
      split_mul_columns_v1(row[7U], right_first[0U], right_first[1U]);
  const auto third =
      split_mul_columns_v1(left_second[1U], right_second[0U], right_second[1U]);
  auto fourth = split_mul_columns_v1(left_second[0U], third[0U], third[1U]);
  fourth[1U] += correction[7U];

  const auto product = std::uint64_t{left} * right;
  const auto split_column15 = ((fourth[0U] >> 15U) + (fourth[1U] >> 15U)) & 1U;
  const bool missed = split_column15 != ((product >> 15U) & 1U);
  return product - (missed ? std::uint64_t{1U} << 15U : 0U);
}

[[nodiscard]] inline Ps2FmacMulValueV1
ps2_fmac_mul_value_v1(const std::uint32_t left,
                      const std::uint32_t right) noexcept {
  const auto sign = (left ^ right) & 0x80000000U;
  const auto left_exponent = (left >> 23U) & 255U;
  const auto right_exponent = (right >> 23U) & 255U;
  if (left_exponent == 0U || right_exponent == 0U) {
    return {sign, false, false};
  }
  const auto product = ps2_mul_significands_v1(
      (left & 0x007fffffU) | 0x00800000U, (right & 0x007fffffU) | 0x00800000U);
  int exponent = static_cast<int>(left_exponent + right_exponent) - 127;
  auto significand = static_cast<std::uint32_t>(product >> 23U);
  if (significand >= 0x01000000U) {
    significand >>= 1U;
    ++exponent;
  }
  if (exponent > 255) {
    return {sign | 0x7fffffffU, false, true};
  }
  if (exponent <= 0) {
    return {sign, true, false};
  }
  return {sign | (static_cast<std::uint32_t>(exponent) << 23U) |
              (significand & 0x007fffffU),
          false, false};
}

} // namespace openrc::detail
