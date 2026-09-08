#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace openrc {

// Complete raw low64 packed-store helper. Inputs are source register values,
// not RGBA channels: no operand is masked to a byte before the shifts/ORs.
// The result is the original eight-byte store, not a source-memory mutation.
[[nodiscard]] std::uint64_t pack_rac_moby_color_words_v1(
    std::uint64_t high_word, std::uint64_t low_word,
    std::uint64_t shifted_8, std::uint64_t shifted_16) noexcept;

struct RacMobyAuthoredTailV1 {
  // Source write order after the earlier complete post-step:
  // 1. SW cached_color_bits at actor+80, in the packed-helper call delay.
  // 2. SD helper_packed_store_bits at actor+38, in the helper return delay.
  // 3. SW light_word_bits at actor+38, replacing only that low word.
  std::uint32_t cached_color_bits = 0U;
  std::uint64_t helper_packed_store_bits = 0U;
  std::uint32_t light_word_bits = 0U;
  std::uint32_t color_word_bits = 0U;
  std::uint64_t final_packed_bits = 0U;
  // Only full32(-1) suppresses the NEXT helper call. A value here preserves
  // its raw input; it does not mean a reference was registered. That helper
  // additionally reads the current level/base table and may write a shared
  // destination. It must finish before live-count advance / next admission.
  std::optional<std::uint32_t> reference_helper_index_bits;

  [[nodiscard]] bool operator==(const RacMobyAuthoredTailV1 &) const = default;
};

// Compiler-only authored tail, reached for every accepted placement, including
// genuine null models. color_words are raw R+64/+68/+6c; light_word_bits is
// the raw R+70 word (bit-preserving conversion from the parser's light_index),
// and reference_index_bits is raw R+74. Word shifts/additions wrap before the
// full64 helper store. Nothing is clamped, alpha-converted or interpreted as
// floating point. No earlier constructor/post/spatial helper is substituted,
// no shared reference store is executed, and no runtime package is produced.
[[nodiscard]] RacMobyAuthoredTailV1 recover_rac_moby_authored_tail_v1(
    const std::array<std::uint32_t, 3U> &color_words,
    std::uint32_t light_word_bits,
    std::uint32_t reference_index_bits) noexcept;

} // namespace openrc
