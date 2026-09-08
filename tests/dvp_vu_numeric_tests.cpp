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

void expect_add(const std::uint32_t left, const std::uint32_t right,
                const std::uint32_t bits, const bool underflow = false,
                const bool overflow = false) {
  const openrc::DvpVuAddResultV1 expected{
      bits, (bits & 0x7f800000U) == 0U, (bits & 0x80000000U) != 0U,
      underflow, overflow};
  const auto actual = openrc::dvp_vu_add_bits_v1(left, right);
  if (actual != expected) {
    std::cerr << "ADD left=" << std::hex << left << " right=" << right
              << " expected=" << bits << " actual=" << actual.bits << '\n';
    throw std::runtime_error("VU ADD reference-model value/flags mismatch");
  }
}

void test_add_zero_input_classes() {
  // Independently generated contract tests, not copied hardware fixtures.
  for (std::uint32_t sample = 0U; sample < 257U; ++sample) {
    const auto fraction = sample * 0x007fffffU / 256U;
    for (const auto left_sign : {0U, 0x80000000U}) {
      for (const auto right_sign : {0U, 0x80000000U}) {
        expect_add(left_sign | fraction, right_sign | (fraction ^ 0x007fffffU),
                   left_sign & right_sign);
      }
    }
    expect_add(fraction, 0x00800000U, 0x00800000U);
    expect_add(0x80000000U | fraction, 0x00800000U, 0x00800000U);
  }
}

void test_add_identity_cancellation_and_doubling() {
  for (std::uint32_t exponent = 1U; exponent < 256U; ++exponent) {
    for (std::uint32_t sample = 0U; sample < 64U; ++sample) {
      const auto fraction = sample * 0x007fffffU / 63U;
      for (const auto sign : {0U, 0x80000000U}) {
        const auto value = sign | (exponent << 23U) | fraction;
        expect_add(value, 0U, value);
        expect_add(0x80000000U, value, value);
        expect_add(value, value ^ 0x80000000U, 0U);
        const auto doubled = exponent == 255U
                                 ? sign | 0x7fffffffU
                                 : value + 0x00800000U;
        expect_add(value, value, doubled, false, exponent == 255U);
      }
    }
  }
}

void test_add_guard_and_carry_boundaries() {
  // Sweep scales rather than importing another project's selected vectors.
  // Below a power of two, representable spacing is half that above it. One
  // retained guard admits that half-ULP decrement, but drops a quarter-ULP.
  for (std::uint32_t exponent = 27U; exponent < 256U; ++exponent) {
    const auto base = exponent << 23U;
    const auto ulp = (exponent - 23U) << 23U;
    const auto half_ulp = (exponent - 24U) << 23U;
    const auto quarter_ulp = (exponent - 25U) << 23U;
    for (const auto sign : {0U, 0x80000000U}) {
      expect_add(base | sign, ulp | sign, (base + 1U) | sign);
      expect_add(base | sign, half_ulp | sign, base | sign);
      expect_add(base | sign, quarter_ulp | sign, base | sign);
      expect_add(base | sign, ulp | (sign ^ 0x80000000U),
                 (base - 2U) | sign);
      expect_add(base | sign, half_ulp | (sign ^ 0x80000000U),
                 (base - 1U) | sign);
      expect_add(base | sign, quarter_ulp | (sign ^ 0x80000000U), base | sign);
      const auto maximum = base | 0x007fffffU;
      expect_add(maximum | sign, half_ulp | sign, maximum | sign);
      expect_add(maximum | sign, ulp | sign,
                 (exponent == 255U ? 0x7fffffffU : base + 0x00800000U) | sign,
                 false, exponent == 255U);
    }
  }
}

void test_add_underflow_fraction_and_following_flush() {
  // Synthetic cancellation differences 3, 5, 7 and 9 at the smallest normal
  // exponent. Normalizing those integers gives the fractions below. This
  // checks the reported underflow rule, not a physical capture of these pairs.
  struct Difference {
    std::uint32_t delta;
    std::uint32_t fraction;
  };
  constexpr std::array differences{
      Difference{3U, 0x00400000U}, Difference{5U, 0x00200000U},
      Difference{7U, 0x00600000U}, Difference{9U, 0x00100000U},
  };
  for (const auto &difference : differences) {
    for (const auto sign : {0U, 0x80000000U}) {
      const auto larger = sign | (0x00801200U + difference.delta);
      const auto smaller = (sign ^ 0x80000000U) | 0x00801200U;
      const auto remnant = sign | difference.fraction;
      expect_add(larger, smaller, remnant, true);
      // An arithmetic input with that encoding is subsequently flushed; its
      // previous U flag is history, not a cause raised by this next ADD.
      expect_add(remnant, sign, sign);
    }
  }
  expect_add(0x00803401U, 0x80803400U, 0U, true);
  expect_add(0x80803401U, 0x00803400U, 0x80000000U, true);
  expect_add(0x00ffffffU, 0x80800000U, 0x007ffffeU, true);
}

void test_add_sub_metamorphic_raw_domain() {
  std::uint32_t generator = 0x18297a43U;
  for (std::uint32_t iteration = 0U; iteration < 65536U; ++iteration) {
    generator = generator * 1664525U + 1013904223U;
    const auto left = generator;
    generator = generator * 1664525U + 1013904223U;
    const auto right = generator;
    const auto result = openrc::dvp_vu_add_bits_v1(left, right);
    if (result != openrc::dvp_vu_add_bits_v1(right, left) ||
        openrc::dvp_vu_sub_bits_v1(left, right) !=
            openrc::dvp_vu_add_bits_v1(left, right ^ 0x80000000U) ||
        result.zero != ((result.bits & 0x7f800000U) == 0U) ||
        result.sign != ((result.bits & 0x80000000U) != 0U) ||
        (result.underflow && result.overflow) ||
        (result.underflow && !result.zero)) {
      throw std::runtime_error("VU ADD/SUB metamorphic invariant failed");
    }
    if ((result.bits & 0x7fffffffU) != 0U || result.underflow) {
      auto reversed = result;
      reversed.bits ^= 0x80000000U;
      reversed.sign = !reversed.sign;
      if (reversed != openrc::dvp_vu_add_bits_v1(left ^ 0x80000000U,
                                               right ^ 0x80000000U)) {
        throw std::runtime_error("VU ADD sign reversal invariant failed");
      }
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
    test_add_zero_input_classes();
    test_add_identity_cancellation_and_doubling();
    test_add_guard_and_carry_boundaries();
    test_add_underflow_fraction_and_following_flush();
    test_add_sub_metamorphic_raw_domain();
    std::cout << "VU numeric conversion/reference-model tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
