#include "openrc/dvp_vu_numeric.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

constexpr std::array<int, 4U> kFractionalBits{0, 4, 12, 15};

void expect_conversion(const std::uint32_t input, const int fractional_bits,
                       const std::uint32_t expected) {
  const auto actual = openrc::dvp_vu_ftoi_bits_v1(input, fractional_bits);
  if (actual != expected) {
    std::cerr << "FTOI" << fractional_bits << " input=" << std::hex << input
              << " expected=" << expected << " actual=" << actual << '\n';
    throw std::runtime_error("VU integer conversion mismatch");
  }
}

void test_documented_conversion_examples() {
  // Specification-derived fixtures, NOT captures from a physical PS2.
  // SCE VU User's Manual v6.0, printed pp. 77-80. The raw encodings below
  // are the binary32 inputs corresponding to its decimal examples.
  struct Example {
    std::uint32_t bits;
    std::array<std::int32_t, 4U> results;
  };
  constexpr std::array examples{
      Example{0xbee66666U, {0, -7, -1843, -14745}},
      Example{0x3ee66666U, {0, 7, 1843, 14745}},
      Example{0x3f0ccccdU, {0, 8, 2252, 18022}},
      Example{0x42f6e666U, {123, 1975, 505651, 4045209}},
      Example{0xc2f6e666U, {-123, -1975, -505651, -4045209}},
  };
  for (const auto &example : examples) {
    for (std::size_t i = 0U; i < kFractionalBits.size(); ++i) {
      expect_conversion(example.bits, kFractionalBits[i],
                        static_cast<std::uint32_t>(example.results[i]));
    }
  }
}

void test_zero_denormal_and_extended_exponent() {
  constexpr std::array zero_encodings{
      0x00000000U, 0x80000000U, 0x00000001U, 0x80000001U, 0x003fffffU,
      0x803fffffU, 0x007fffffU, 0x807fffffU, 0x00800000U, 0x80800000U,
  };
  constexpr std::array large_magnitudes{
      0x7f000000U, 0x7f7fffffU, 0x7f800000U, 0x7f800001U,
      0x7fbfffffU, 0x7fc00000U, 0x7fffffffU,
  };
  for (const auto fractional_bits : kFractionalBits) {
    for (const auto input : zero_encodings) {
      expect_conversion(input, fractional_bits, 0U);
    }
    for (const auto magnitude : large_magnitudes) {
      expect_conversion(magnitude, fractional_bits, 0x7fffffffU);
      expect_conversion(magnitude | 0x80000000U, fractional_bits, 0x80000000U);
    }
  }
}

void test_truncation_and_saturation_neighbors() {
  for (const auto fractional_bits : kFractionalBits) {
    const auto exponent_adjustment = static_cast<std::uint32_t>(fractional_bits)
                                     << 23U;
    // Scale each exact raw input by 2^-fractional_bits. Its fixed-point
    // integer result must consequently equal the FTOI0 result below.
    struct Neighbor {
      std::uint32_t magnitude;
      std::uint32_t positive_result;
      std::uint32_t negative_result;
    };
    constexpr std::array neighbors{
        Neighbor{0x3f7fffffU, 0U, 0U},
        Neighbor{0x3f800000U, 1U, 0xffffffffU},
        Neighbor{0x3f800001U, 1U, 0xffffffffU},
        Neighbor{0x3fc00000U, 1U, 0xffffffffU},
        Neighbor{0x3fffffffU, 1U, 0xffffffffU},
        Neighbor{0x40000000U, 2U, 0xfffffffeU},
        Neighbor{0x403fffffU, 2U, 0xfffffffeU},
        Neighbor{0x4affffffU, 0x007fffffU, 0xff800001U},
        Neighbor{0x4b000000U, 0x00800000U, 0xff800000U},
        Neighbor{0x4b7fffffU, 0x00ffffffU, 0xff000001U},
        Neighbor{0x4b800000U, 0x01000000U, 0xff000000U},
        Neighbor{0x4b800001U, 0x01000002U, 0xfefffffeU},
        Neighbor{0x4efffffeU, 0x7fffff00U, 0x80000100U},
        Neighbor{0x4effffffU, 0x7fffff80U, 0x80000080U},
        Neighbor{0x4f000000U, 0x7fffffffU, 0x80000000U},
        Neighbor{0x4f000001U, 0x7fffffffU, 0x80000000U},
        Neighbor{0x4f7fffffU, 0x7fffffffU, 0x80000000U},
    };
    for (const auto &neighbor : neighbors) {
      const auto input = neighbor.magnitude - exponent_adjustment;
      expect_conversion(input, fractional_bits, neighbor.positive_result);
      expect_conversion(input | 0x80000000U, fractional_bits,
                        neighbor.negative_result);
    }
  }
}

// Independent, wider integer-rational evaluation of the documented numeric
// value. The production helper directly shifts a 24-bit significand; this
// reference multiplies and divides in 64 bits after bounding the exponent.
// It is a mathematical oracle, not an original-hardware comparison.
std::uint32_t rational_reference(const std::uint32_t input,
                                 const int fractional_bits) {
  const auto encoded_exponent = (input >> 23U) & 0xffU;
  if (encoded_exponent == 0U) {
    return 0U;
  }
  const auto power = static_cast<int>(encoded_exponent) - 127 + fractional_bits;
  if (power < 0) {
    return 0U;
  }
  const bool negative = (input & 0x80000000U) != 0U;
  if (power > 31) {
    return negative ? 0x80000000U : 0x7fffffffU;
  }
  const std::uint64_t numerator =
      (std::uint64_t{0x00800000U} + (input & 0x007fffffU)) *
      (std::uint64_t{1U} << power);
  const auto magnitude = numerator / 0x00800000U;
  const auto limit =
      negative ? std::uint64_t{0x80000000U} : std::uint64_t{0x7fffffffU};
  const auto bounded =
      static_cast<std::uint32_t>(magnitude > limit ? limit : magnitude);
  return negative ? std::uint32_t{0U} - bounded : bounded;
}

void test_every_exponent_and_distributed_significands() {
  for (const auto fractional_bits : kFractionalBits) {
    for (std::uint32_t exponent = 0U; exponent < 256U; ++exponent) {
      for (std::uint32_t sample = 0U; sample < 256U; ++sample) {
        const auto mantissa = sample * 0x007fffffU / 255U;
        for (const std::uint32_t sign : {0U, 0x80000000U}) {
          const auto input = sign | (exponent << 23U) | mantissa;
          expect_conversion(input, fractional_bits,
                            rational_reference(input, fractional_bits));
        }
      }
    }
  }
}

void test_invalid_fractional_formats_rejected() {
  constexpr std::array invalid{
      std::numeric_limits<int>::min(), -1, 1, 3, 5, 11, 13, 14, 16, 31, 32,
      std::numeric_limits<int>::max(),
  };
  for (const auto fractional_bits : invalid) {
    bool rejected = false;
    try {
      static_cast<void>(
          openrc::dvp_vu_ftoi_bits_v1(0x3f800000U, fractional_bits));
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    if (!rejected) {
      throw std::runtime_error("Accepted invalid VU fractional format");
    }
  }
}

} // namespace

int main() {
  try {
    test_documented_conversion_examples();
    test_zero_denormal_and_extended_exponent();
    test_truncation_and_saturation_neighbors();
    test_every_exponent_and_distributed_significands();
    test_invalid_fractional_formats_rejected();
    std::cout << "VU integer conversion tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
