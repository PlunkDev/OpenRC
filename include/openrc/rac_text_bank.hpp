#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::size_t kRacTextBankSlotCountV1 = 8U;

struct RacTextBankLimitsV1 {
  std::uint64_t max_input_bytes = 0U;
  std::uint64_t max_total_entries = 0U;
  std::uint64_t max_total_text_bytes = 0U;
};

struct RacTextBankRangeV1 {
  std::uint64_t offset = 0U;
  std::uint64_t size = 0U;

  [[nodiscard]] bool operator==(const RacTextBankRangeV1 &) const = default;
};

struct RacTextBankEntryV1 {
  std::uint32_t key = 0U;
  // Source row +8/+c are retained, not assigned guessed UI/audio meanings.
  std::uint32_t raw_auxiliary_word_8 = 0U;
  std::uint32_t raw_auxiliary_word_c = 0U;
  std::uint32_t relative_text_offset = 0U;
  // Absolute in the input passed to the public parser, excluding NUL/padding.
  RacTextBankRangeV1 text_range;
  // Original single-byte glyph/control stream. No Unicode conversion, control
  // stripping, localization fallback, or formatting policy is applied here.
  std::vector<std::byte> text_bytes;

  [[nodiscard]] bool operator==(const RacTextBankEntryV1 &) const = default;
};

struct RacTextBankV1 {
  RacTextBankRangeV1 range;
  RacTextBankRangeV1 row_directory_range;
  std::uint64_t total_text_bytes = 0U;
  // Preserve source order: the original lookup returns the first matching key.
  std::vector<RacTextBankEntryV1> entries;
};

struct RacTextBankDirectoryV1 {
  std::uint64_t input_bytes = 0U;
  std::uint64_t total_entries = 0U;
  std::uint64_t total_text_bytes = 0U;
  // Numeric source slots, including the empty and partial banks. These are not
  // implicitly mapped to host locale names or to the five subtitle languages.
  std::array<std::uint32_t, kRacTextBankSlotCountV1> slot_offsets{};
  std::array<RacTextBankV1, kRacTextBankSlotCountV1> slots;
  std::array<RacTextBankRangeV1, kRacTextBankSlotCountV1> padding_ranges{};
};

class RacTextBankError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compiler-only RAC1 keyed text format shared by frontend/gameplay. The caller
// supplies exactly one unrelocated logical bank, including its 8-byte header.
// This bounded retail layout requires consecutive, separately stored NUL text
// with minimum zero 4-byte padding. Offset aliases are not established by the
// reference corpus and are explicitly outside this parser's supported layout.
// Auxiliary words and unsorted/duplicate keys are preserved without guessing.
[[nodiscard]] RacTextBankV1
parse_rac_text_bank_v1(std::span<const std::byte> bytes,
                       RacTextBankLimitsV1 limits);

// Complete source sector envelope: eight ordered bank offsets, minimum zero
// 16-byte bank alignment, and minimum zero 2048-byte final alignment. Limits
// apply to the whole directory, not independently to each of its eight slots.
// This is not a cutscene subtitle bank, runtime menu, or prepared UI artifact.
[[nodiscard]] RacTextBankDirectoryV1
parse_rac_text_bank_directory_v1(std::span<const std::byte> bytes,
                                 RacTextBankLimitsV1 limits);

// Original first-match key selection only. A missing key stays missing; the
// source renderer's separate diagnostic/fallback text policy is not invented.
[[nodiscard]] const RacTextBankEntryV1 *
find_rac_text_bank_entry_v1(const RacTextBankV1 &bank,
                            std::uint32_t key) noexcept;

} // namespace openrc
