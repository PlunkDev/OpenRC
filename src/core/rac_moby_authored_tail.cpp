#include "openrc/rac_moby_authored_tail.hpp"

namespace openrc {

std::uint64_t pack_rac_moby_color_words_v1(
    const std::uint64_t high_word, const std::uint64_t low_word,
    const std::uint64_t shifted_8, const std::uint64_t shifted_16) noexcept {
  // PAL v2.00 leaf251308..251324: DSLL32, DSLL8, DSLL16, three ORs, SD.
  return (high_word << 32U) | low_word | (shifted_8 << 8U) |
         (shifted_16 << 16U);
}

RacMobyAuthoredTailV1 recover_rac_moby_authored_tail_v1(
    const std::array<std::uint32_t, 3U> &color_words,
    const std::uint32_t light_word_bits,
    const std::uint32_t reference_index_bits) noexcept {
  // Original243584..24359c adds shifted third+second, then first. Inputs are
  // words, so overlapping bits can carry; OR is not equivalent.
  const std::uint32_t shifted_pair =
      (color_words[2U] << 16U) + (color_words[1U] << 8U);
  const std::uint32_t packed_color = shifted_pair + color_words[0U];

  RacMobyAuthoredTailV1 result;
  result.cached_color_bits = packed_color;
  // Source passes a sign-extended word in a1, but DSLL32 discards those upper
  // bits. The other three helper arguments are genuinely zero at this call.
  result.helper_packed_store_bits =
      pack_rac_moby_color_words_v1(packed_color, 0U, 0U, 0U);
  result.light_word_bits = light_word_bits;
  result.color_word_bits = packed_color;
  result.final_packed_bits = result.helper_packed_store_bits | light_word_bits;
  if (reference_index_bits != 0xffffffffU) {
    result.reference_helper_index_bits = reference_index_bits;
  }
  return result;
}

} // namespace openrc
