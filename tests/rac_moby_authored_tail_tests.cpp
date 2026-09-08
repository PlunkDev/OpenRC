#include "openrc/rac_moby_authored_tail.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_authored_word_addition_and_overflow() {
  struct Case {
    std::array<std::uint32_t, 3U> words;
    std::uint32_t packed;
  };
  constexpr std::array cases{
      Case{{0U, 0U, 0U}, 0U},
      Case{{0x12U, 0x34U, 0x56U}, 0x00563412U},
      Case{{0x100U, 1U, 0U}, 0x200U},
      Case{{0xffffffffU, 1U, 0U}, 0xffU},
      Case{{0U, 0x10000U, 0U}, 0x01000000U},
      Case{{0U, 0U, 0x10000U}, 0U},
      Case{{0U, 0x800000U, 0U}, 0x80000000U},
      Case{{0xffffffffU, 0xffffffffU, 0xffffffffU}, 0xfffefeffU}};
  for (const auto &test : cases) {
    const auto result = openrc::recover_rac_moby_authored_tail_v1(
        test.words, 0x81234567U, 0xffffffffU);
    const auto high = static_cast<std::uint64_t>(test.packed) << 32U;
    expect(result.cached_color_bits == test.packed &&
               result.helper_packed_store_bits == high &&
               result.light_word_bits == 0x81234567U &&
               result.color_word_bits == test.packed &&
               result.final_packed_bits == (high | 0x81234567ULL) &&
               !result.reference_helper_index_bits,
           "Authored color words must retain wrapping addition and ordered stores");
  }
}

void test_reference_gate_is_full_word_not_sign_or_low_half() {
  for (const auto index : std::array<std::uint32_t, 9U>{
           0U, 1U, 18U, 19U, 0xffffU, 0x7fffffffU, 0x80000000U,
           0xfffeffffU, 0xfffffffeU}) {
    const auto result =
        openrc::recover_rac_moby_authored_tail_v1({1U, 2U, 3U}, 0U, index);
    expect(result.reference_helper_index_bits == index,
           "Every word except exact ffffffff must reach the reference helper");
  }
  const auto skipped =
      openrc::recover_rac_moby_authored_tail_v1({1U, 2U, 3U}, 0U, 0xffffffffU);
  expect(!skipped.reference_helper_index_bits &&
             skipped.cached_color_bits == 0x30201U,
         "Reference skip must not skip the earlier color stores");
}

void test_light_word_overwrites_only_low32() {
  for (const auto light : std::array<std::uint32_t, 6U>{
           0U, 1U, 0x7fffffffU, 0x80000000U, 0x7fc01234U, 0xffffffffU}) {
    const auto result = openrc::recover_rac_moby_authored_tail_v1(
        {0x12345678U, 0U, 0U}, light, 0U);
    expect(result.helper_packed_store_bits == 0x1234567800000000ULL &&
               result.final_packed_bits == (0x1234567800000000ULL | light) &&
               result.cached_color_bits == 0x12345678U &&
               result.light_word_bits == light,
           "Light SW must preserve upper color word without sign-extension bleed");
  }
}

void test_full64_helper_has_no_channel_masks() {
  using openrc::pack_rac_moby_color_words_v1;
  expect(pack_rac_moby_color_words_v1(0xffffffff80000000ULL, 0U, 0U, 0U) ==
             0x8000000000000000ULL,
         "DSLL32 must discard only shifted-out upper bits");
  expect(pack_rac_moby_color_words_v1(0U, 0x8000000000000000ULL, 0U, 0U) ==
             0x8000000000000000ULL,
         "Second argument is full64, not one byte or low word");
  expect(pack_rac_moby_color_words_v1(0U, 0U, 0x10000U, 0U) == 0x1000000U &&
             pack_rac_moby_color_words_v1(0U, 0U, 0U, 0x10000U) == 0x100000000ULL,
         "Shifted operands must retain out-of-byte-range bits");
  for (std::uint32_t operand = 0U; operand < 4U; ++operand) {
    for (std::uint32_t bit = 0U; bit < 64U; ++bit) {
      std::array<std::uint64_t, 4U> inputs{};
      inputs[operand] = std::uint64_t{1} << bit;
      const auto shift = std::array<std::uint32_t, 4U>{32U, 0U, 8U, 16U}[operand];
      const auto expected = bit + shift < 64U
                                ? std::uint64_t{1} << (bit + shift)
                                : 0U;
      expect(pack_rac_moby_color_words_v1(
                 inputs[0], inputs[1], inputs[2], inputs[3]) == expected,
             "Generic packed helper single-bit mapping changed");
    }
  }
  expect(pack_rac_moby_color_words_v1(~0ULL, ~0ULL, ~0ULL, ~0ULL) == ~0ULL,
         "The generic helper combines overlapping values with OR, not addition");
}

} // namespace

int main() {
  try {
    test_authored_word_addition_and_overflow();
    test_reference_gate_is_full_word_not_sign_or_low_half();
    test_light_word_overwrites_only_low32();
    test_full64_helper_has_no_channel_masks();
    std::cout << "RAC authored Moby tail tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
