#include "openrc/rac_moby_spatial_bounds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using openrc::project_rac_moby_spatial_bounds_v1;
using openrc::RacMobySpatialBoundsGateV1;
using openrc::RacMobySpatialBoundsProjectionV1;

constexpr std::uint32_t kFreshBounds = 0x80807f7fU;
constexpr std::uint32_t kCell = 1U << 14U;

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_projection(const RacMobySpatialBoundsProjectionV1 &actual,
                       const std::array<std::uint8_t, 4U> &bytes,
                       const std::uint32_t packed,
                       const RacMobySpatialBoundsGateV1 gate,
                       const std::string &message) {
  expect(actual.bound_bytes == bytes && actual.packed_bits == packed &&
             actual.gate == gate,
         message);
}

void test_inclusive_endpoints_and_fractional_cells() {
  expect_projection(
      project_rac_moby_spatial_bounds_v1(10U * kCell, 20U * kCell, kCell,
                                         kFreshBounds),
      {9U, 19U, 11U, 21U}, 0x150b1309U,
      RacMobySpatialBoundsGateV1::call_spatial_update,
      "Projection must preserve source endpoint order and inclusive maxima");
  expect_projection(project_rac_moby_spatial_bounds_v1(
                        10U * kCell + 3U, 20U * kCell + 7U, 7U, kFreshBounds),
                    {9U, 20U, 10U, 20U}, 0x140a1409U,
                    RacMobySpatialBoundsGateV1::call_spatial_update,
                    "Source shifts each complete C +/- radius, not "
                    "independently scaled inputs");
}

void test_wrapping_arithmetic_and_negative_words() {
  expect_projection(
      project_rac_moby_spatial_bounds_v1(0U, 0U, 1U, kFreshBounds),
      {0xffU, 0xffU, 0U, 0U}, 0x0000ffffU,
      RacMobySpatialBoundsGateV1::rejected_minimum,
      "Negative sub-cell minima must use arithmetic shift, not truncation "
      "toward zero");
  expect_projection(
      project_rac_moby_spatial_bounds_v1(0x7fffffffU, 0x80000000U, 1U,
                                         kFreshBounds),
      {0xffU, 0xffU, 0U, 0U}, 0x0000ffffU,
      RacMobySpatialBoundsGateV1::rejected_minimum,
      "PADDW and PSUBW must wrap before shifting at signed boundaries");
  expect_projection(
      project_rac_moby_spatial_bounds_v1(0xffffffffU, 0xffffffffU, 0xffffffffU,
                                         kFreshBounds),
      {0U, 0U, 0xffU, 0xffU}, 0xffff0000U,
      RacMobySpatialBoundsGateV1::call_spatial_update,
      "Raw negative radius words must retain source wrapping operations");
}

void test_packing_is_truncation_not_clamping() {
  expect_projection(
      project_rac_moby_spatial_bounds_v1(256U * kCell + 5U, 513U * kCell, 0U,
                                         kFreshBounds),
      {0U, 1U, 0U, 1U}, 0x01000100U,
      RacMobySpatialBoundsGateV1::call_spatial_update,
      "Bounds beyond one byte must truncate before the minimum gate");
  expect_projection(
      project_rac_moby_spatial_bounds_v1(10U * kCell, 20U * kCell, 0xffffffffU,
                                         kFreshBounds),
      {10U, 20U, 9U, 19U}, 0x1309140aU,
      RacMobySpatialBoundsGateV1::call_spatial_update,
      "Source post gate does not reject inverted endpoint ordering");
}

void test_low64_equality_and_gate_priority() {
  const auto ordinary = project_rac_moby_spatial_bounds_v1(
      10U * kCell, 20U * kCell, kCell, kFreshBounds);
  expect_projection(
      project_rac_moby_spatial_bounds_v1(10U * kCell, 20U * kCell, kCell,
                                         ordinary.packed_bits),
      ordinary.bound_bytes, ordinary.packed_bits,
      RacMobySpatialBoundsGateV1::unchanged,
      "A nonnegative equal stored word must skip the spatial update");

  expect_projection(
      project_rac_moby_spatial_bounds_v1(64U * kCell, kCell, 0U, 0x01400140U),
      {64U, 1U, 64U, 1U}, 0x01400140U, RacMobySpatialBoundsGateV1::unchanged,
      "Source equality precedes the minimum mask even for invalid minima");

  expect_projection(project_rac_moby_spatial_bounds_v1(
                        127U * kCell + kCell / 2U, 127U * kCell + kCell / 2U,
                        kCell / 2U, kFreshBounds),
                    {127U, 127U, 128U, 128U}, kFreshBounds,
                    RacMobySpatialBoundsGateV1::rejected_minimum,
                    "Equal low32 fresh sentinel must not equal its "
                    "sign-extended LW in low64");

  expect_projection(
      project_rac_moby_spatial_bounds_v1(64U * kCell, 64U * kCell, 64U * kCell,
                                         0x80800000U),
      {0U, 0U, 128U, 128U}, 0x80800000U,
      RacMobySpatialBoundsGateV1::call_spatial_update,
      "Negative high32 source LW must not cancel the later bounds store");
}

void test_minimum_gate_does_not_validate_maxima() {
  expect_projection(
      project_rac_moby_spatial_bounds_v1(64U * kCell, kCell, 1U, kFreshBounds),
      {63U, 0U, 64U, 1U}, 0x0140003fU,
      RacMobySpatialBoundsGateV1::call_spatial_update,
      "The source minimum mask must not become a maximum cell-range check");
  expect_projection(
      project_rac_moby_spatial_bounds_v1(64U * kCell, 64U * kCell, 64U * kCell,
                                         kFreshBounds),
      {0U, 0U, 128U, 128U}, 0x80800000U,
      RacMobySpatialBoundsGateV1::call_spatial_update,
      "Signed maxY rejection belongs after the later spatial-update store");
}

void test_every_minimum_byte_pair_and_sign_extended_old_word() {
  for (std::uint32_t x = 0U; x < 256U; ++x) {
    for (std::uint32_t y = 0U; y < 256U; ++y) {
      const auto actual = project_rac_moby_spatial_bounds_v1(
          x * kCell, y * kCell, 0U, kFreshBounds);
      const auto expected_gate =
          x < 64U && y < 64U ? RacMobySpatialBoundsGateV1::call_spatial_update
                             : RacMobySpatialBoundsGateV1::rejected_minimum;
      expect(actual.gate == expected_gate,
             "Minimum-byte domain must be exactly 0..63 in both lanes");

      const auto same_low_word = project_rac_moby_spatial_bounds_v1(
          x * kCell, y * kCell, 0U, actual.packed_bits);
      const auto expected_same_gate =
          y < 128U ? RacMobySpatialBoundsGateV1::unchanged : expected_gate;
      expect(same_low_word.gate == expected_same_gate,
             "All old sign-bit cases must preserve source low64 equality");
    }
  }
}

// Independent mathematical test model: signed 64-bit wrapping and floor
// division express source word arithmetic without the implementation's shifts.
[[nodiscard]] std::int64_t signed_word(const std::uint32_t bits) {
  return bits < 0x80000000U ? static_cast<std::int64_t>(bits)
                            : static_cast<std::int64_t>(bits) - 0x100000000LL;
}

[[nodiscard]] std::uint8_t endpoint_byte(const std::int64_t unwrapped) {
  auto wrapped = unwrapped % 0x100000000LL;
  if (wrapped < 0) {
    wrapped += 0x100000000LL;
  }
  if (wrapped >= 0x80000000LL) {
    wrapped -= 0x100000000LL;
  }
  const auto quotient =
      wrapped >= 0 ? wrapped / 16384LL : -((-wrapped + 16383LL) / 16384LL);
  auto low_byte = quotient % 256LL;
  if (low_byte < 0) {
    low_byte += 256LL;
  }
  return static_cast<std::uint8_t>(low_byte);
}

void test_wide_integer_reference_at_word_boundaries() {
  constexpr std::array<std::uint32_t, 14U> words{
      0U,          1U,          0x00003fffU, 0x00004000U, 0x00004001U,
      0x003fffffU, 0x00400000U, 0x7ffffffeU, 0x7fffffffU, 0x80000000U,
      0x80000001U, 0xffffbfffU, 0xffffc000U, 0xffffffffU,
  };
  for (const auto x : words) {
    for (const auto y : words) {
      for (const auto radius : words) {
        const auto sx = signed_word(x);
        const auto sy = signed_word(y);
        const auto sr = signed_word(radius);
        const std::array<std::uint8_t, 4U> expected{
            endpoint_byte(sx - sr),
            endpoint_byte(sy - sr),
            endpoint_byte(sx + sr),
            endpoint_byte(sy + sr),
        };
        const auto actual =
            project_rac_moby_spatial_bounds_v1(x, y, radius, kFreshBounds);
        expect(
            actual.bound_bytes == expected,
            "Projection disagrees with wide integer source arithmetic model");
        std::uint32_t packed = 0U;
        std::uint32_t multiplier = 1U;
        for (std::size_t lane = 0U; lane < expected.size(); ++lane) {
          packed += static_cast<std::uint32_t>(expected[lane]) * multiplier;
          if (lane + 1U < expected.size()) {
            multiplier *= 256U;
          }
        }
        expect(actual.packed_bits == packed,
               "Packing must retain exactly the four source endpoint bytes");
      }
    }
  }
}

} // namespace

int main() {
  try {
    test_inclusive_endpoints_and_fractional_cells();
    test_wrapping_arithmetic_and_negative_words();
    test_packing_is_truncation_not_clamping();
    test_low64_equality_and_gate_priority();
    test_minimum_gate_does_not_validate_maxima();
    test_every_minimum_byte_pair_and_sign_extended_old_word();
    test_wide_integer_reference_at_word_boundaries();
    std::cout << "RAC Moby spatial bounds tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC Moby spatial bounds tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
