#pragma once

#include <array>
#include <bit>
#include <cstdint>

// Independently authored bit-column oracle for the stated reference circuit,
// not a physical-console oracle or imported external implementation/table.
// Unlike production's packed window, this keeps every column0..16 and uses
// one-bit full adders plus ripple injection. Bits above16 cannot affect15.
namespace openrc_test {

using MulRow = std::array<unsigned, 17U>;

inline std::array<MulRow, 2U> split_rows(const MulRow &a, const MulRow &b,
                                         const MulRow &c) {
  std::array<MulRow, 2U> result{};
  for (unsigned column = 0U; column < a.size(); ++column) {
    const auto population = a[column] + b[column] + c[column];
    result[0U][column] = population % 2U;
    if (column + 1U < a.size()) {
      result[1U][column + 1U] = population / 2U;
    }
  }
  return result;
}

inline void increment_column(MulRow &row, unsigned column) {
  while (column < row.size()) {
    row[column] = 1U - row[column];
    if (row[column++] != 0U) {
      return;
    }
  }
}

inline std::uint64_t mul_significand_oracle(const std::uint32_t a,
                                            const std::uint32_t b) {
  std::array<MulRow, 8U> rows{};
  std::array<bool, 8U> negative{};
  for (unsigned index = 0U; index < rows.size(); ++index) {
    const auto shift = 2U * index;
    const auto selector = ((std::uint64_t{b} * 2U) >> shift) % 8U;
    int factor = 0;
    switch (selector) {
    case 1U:
    case 2U:
      factor = 1;
      break;
    case 3U:
      factor = 2;
      break;
    case 4U:
      factor = -2;
      break;
    case 5U:
    case 6U:
      factor = -1;
      break;
    default:
      break;
    }
    negative[index] = factor < 0;
    auto magnitude =
        std::uint64_t{a} * static_cast<unsigned>(factor < 0 ? -factor : factor);
    if (negative[index])
      magnitude = ~magnitude;
    const auto aligned = magnitude << shift;
    for (unsigned column = 0U; column < rows[index].size(); ++column)
      rows[index][column] = static_cast<unsigned>((aligned >> column) & 1U);
  }
  const auto deferred = rows[5U];
  for (unsigned bit = 0U; bit < 11U; ++bit)
    rows[4U][bit] = 0U;
  for (unsigned bit = 0U; bit < 12U; ++bit)
    rows[5U][bit] = 0U;
  const auto first_a = split_rows(rows[1U], rows[2U], rows[3U]);
  auto first_b = split_rows(rows[4U], rows[5U], rows[6U]);
  if (negative[6U])
    increment_column(first_b[1U], 12U);
  if (deferred[11U])
    increment_column(first_b[1U], 11U);
  if (deferred[10U])
    increment_column(rows[7U], 10U);
  if (negative[5U])
    increment_column(rows[7U], 10U);
  const auto second_a = split_rows(rows[0U], first_a[0U], first_a[1U]);
  const auto second_b = split_rows(rows[7U], first_b[0U], first_b[1U]);
  const auto third = split_rows(second_a[1U], second_b[0U], second_b[1U]);
  auto fourth = split_rows(second_a[0U], third[0U], third[1U]);
  if (negative[7U])
    increment_column(fourth[1U], 14U);
  const auto hardware_column = (fourth[0U][15U] + fourth[1U][15U]) % 2U;
  const auto exact = std::uint64_t{a} * b;
  return exact - (hardware_column != ((exact / 32768U) % 2U) ? 32768U : 0U);
}

struct MulExpected {
  std::uint32_t bits;
  bool underflow;
  bool overflow;
};

inline MulExpected mul_oracle(const std::uint32_t a, const std::uint32_t b) {
  const auto sign = (a ^ b) & 0x80000000U;
  const auto ea = (a / 0x800000U) % 256U;
  const auto eb = (b / 0x800000U) % 256U;
  if (ea == 0U || eb == 0U)
    return {sign, false, false};
  const auto product = mul_significand_oracle(0x800000U + a % 0x800000U,
                                              0x800000U + b % 0x800000U);
  const auto shift = std::bit_width(product) - 24U;
  const auto exponent = static_cast<int>(ea + eb + shift) - 150;
  if (exponent < 1)
    return {sign, true, false};
  if (exponent > 255)
    return {sign | 0x7fffffffU, false, true};
  const auto significand = product / (std::uint64_t{1U} << shift);
  return {sign | (static_cast<std::uint32_t>(exponent) * 0x800000U) |
              static_cast<std::uint32_t>(significand % 0x800000U),
          false, false};
}

} // namespace openrc_test
