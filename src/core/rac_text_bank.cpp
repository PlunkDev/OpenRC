#include "openrc/rac_text_bank.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint64_t kBankHeaderBytes = 8U;
constexpr std::uint64_t kRowBytes = 16U;
constexpr std::uint64_t kDirectoryBytes = kRacTextBankSlotCountV1 * 4U;

[[noreturn]] void fail(const char *const message) {
  throw RacTextBankError(message);
}

[[nodiscard]] std::uint32_t read32(const std::span<const std::byte> bytes,
                                   const std::size_t offset) noexcept {
  std::uint32_t result = 0U;
  for (unsigned index = 0U; index < 4U; ++index) {
    result |= std::to_integer<std::uint32_t>(bytes[offset + index])
              << (index * 8U);
  }
  return result;
}

// All inputs are bounded by the u32 source extent before alignment. u64 keeps
// the required padding calculation from wrapping at the source-domain edge.
[[nodiscard]] std::uint64_t align_up(const std::uint64_t value,
                                     const std::uint64_t alignment) noexcept {
  return (value + alignment - 1U) & ~(alignment - 1U);
}

[[nodiscard]] bool zero_range(const std::span<const std::byte> bytes,
                              const std::uint64_t begin,
                              const std::uint64_t end) {
  const auto range = bytes.subspan(static_cast<std::size_t>(begin),
                                   static_cast<std::size_t>(end - begin));
  return std::ranges::all_of(
      range, [](const auto byte) { return byte == std::byte{0}; });
}

void validate_input(const std::span<const std::byte> bytes,
                    const RacTextBankLimitsV1 limits) {
  if (limits.max_input_bytes == 0U || limits.max_total_entries == 0U ||
      limits.max_total_text_bytes == 0U) {
    fail("RAC text-bank limits must all be non-zero");
  }
  if (bytes.size() > limits.max_input_bytes ||
      bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
    fail("RAC text-bank input exceeds its bounded source extent");
  }
}

[[nodiscard]] RacTextBankV1
parse_bank(const std::span<const std::byte> bytes,
           const std::uint64_t absolute_offset,
           const std::uint64_t remaining_entries,
           const std::uint64_t remaining_text_bytes) {
  if (bytes.size() < kBankHeaderBytes || (bytes.size() & 3U) != 0U ||
      read32(bytes, 4U) != bytes.size()) {
    fail("RAC text bank has an invalid exact logical extent");
  }
  const auto count = read32(bytes, 0U);
  if (count > static_cast<std::uint32_t>(
                  std::numeric_limits<std::int32_t>::max()) ||
      count > remaining_entries ||
      count > (bytes.size() - kBankHeaderBytes) / kRowBytes) {
    fail("RAC text bank has an invalid or excessive row count");
  }
  const auto rows_end = kBankHeaderBytes + kRowBytes * count;
  RacTextBankV1 result;
  result.range = {absolute_offset, bytes.size()};
  result.row_directory_range = {absolute_offset + kBankHeaderBytes,
                                kRowBytes * count};
  result.entries.reserve(count);
  auto text_cursor = rows_end;
  for (std::uint32_t index = 0U; index < count; ++index) {
    const auto row_offset =
        static_cast<std::size_t>(kBankHeaderBytes + kRowBytes * index);
    const auto relative = read32(bytes, row_offset);
    if (relative != text_cursor || relative >= bytes.size()) {
      fail("RAC text bank has an unsupported or out-of-range text offset");
    }
    const auto tail = bytes.subspan(relative);
    const auto terminator = std::ranges::find(tail, std::byte{0});
    if (terminator == tail.end()) {
      fail("RAC text-bank text is not NUL terminated in its logical extent");
    }
    const auto text_size =
        static_cast<std::uint64_t>(terminator - tail.begin());
    if (text_size > remaining_text_bytes - result.total_text_bytes) {
      fail("RAC text bank exceeds the aggregate text-byte limit");
    }
    const auto logical_end = text_cursor + text_size + 1U;
    const auto padded_end = align_up(logical_end, 4U);
    if (padded_end > bytes.size() ||
        !zero_range(bytes, logical_end, padded_end)) {
      fail("RAC text-bank text padding is not its minimum zero envelope");
    }
    RacTextBankEntryV1 entry;
    entry.relative_text_offset = relative;
    entry.key = read32(bytes, row_offset + 4U);
    entry.raw_auxiliary_word_8 = read32(bytes, row_offset + 8U);
    entry.raw_auxiliary_word_c = read32(bytes, row_offset + 12U);
    entry.text_range = {absolute_offset + relative, text_size};
    entry.text_bytes.assign(tail.begin(), terminator);
    result.total_text_bytes += text_size;
    result.entries.push_back(std::move(entry));
    text_cursor = padded_end;
  }
  if (text_cursor != bytes.size()) {
    fail("RAC text-bank rows and text do not close the logical extent");
  }
  return result;
}

} // namespace

RacTextBankV1 parse_rac_text_bank_v1(const std::span<const std::byte> bytes,
                                     const RacTextBankLimitsV1 limits) {
  validate_input(bytes, limits);
  return parse_bank(bytes, 0U, limits.max_total_entries,
                    limits.max_total_text_bytes);
}

RacTextBankDirectoryV1
parse_rac_text_bank_directory_v1(const std::span<const std::byte> bytes,
                                 const RacTextBankLimitsV1 limits) {
  validate_input(bytes, limits);
  if (bytes.size() < 2048U || (bytes.size() & 2047U) != 0U) {
    fail("RAC text directory requires its complete source sector envelope");
  }
  RacTextBankDirectoryV1 result;
  result.input_bytes = bytes.size();
  for (std::size_t index = 0U; index < kRacTextBankSlotCountV1; ++index) {
    const auto offset = read32(bytes, index * 4U);
    if ((index == 0U && offset != kDirectoryBytes) ||
        (index != 0U && offset <= result.slot_offsets[index - 1U]) ||
        (offset & 15U) != 0U || offset > bytes.size() - kBankHeaderBytes) {
      fail("RAC text directory has invalid ordered bank offsets");
    }
    result.slot_offsets[index] = offset;
  }
  for (std::size_t index = 0U; index < kRacTextBankSlotCountV1; ++index) {
    const auto offset = result.slot_offsets[index];
    const auto storage_end = index + 1U < kRacTextBankSlotCountV1
                                 ? result.slot_offsets[index + 1U]
                                 : bytes.size();
    const auto logical_size = read32(bytes, offset + 4U);
    if (logical_size < kBankHeaderBytes ||
        logical_size > storage_end - offset) {
      fail("RAC text-directory bank escapes its own storage slot");
    }
    const auto logical_end = std::uint64_t{offset} + logical_size;
    const auto alignment = index + 1U < kRacTextBankSlotCountV1 ? 16U : 2048U;
    if (align_up(logical_end, alignment) != storage_end ||
        !zero_range(bytes, logical_end, storage_end)) {
      fail("RAC text-directory padding is not its minimum zero envelope");
    }
    auto bank =
        parse_bank(bytes.subspan(offset, logical_size), offset,
                   limits.max_total_entries - result.total_entries,
                   limits.max_total_text_bytes - result.total_text_bytes);
    result.total_entries += bank.entries.size();
    result.total_text_bytes += bank.total_text_bytes;
    result.slots[index] = std::move(bank);
    result.padding_ranges[index] = {logical_end, storage_end - logical_end};
  }
  return result;
}

const RacTextBankEntryV1 *
find_rac_text_bank_entry_v1(const RacTextBankV1 &bank,
                            const std::uint32_t key) noexcept {
  const auto found =
      std::ranges::find(bank.entries, key, &RacTextBankEntryV1::key);
  return found == bank.entries.end() ? nullptr : &*found;
}

} // namespace openrc
