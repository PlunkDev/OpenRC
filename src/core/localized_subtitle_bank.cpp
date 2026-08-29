#include "openrc/localized_subtitle_bank.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint32_t kDirectoryAlignment = 0x10U;
constexpr std::uint32_t kTextAlignment = 4U;
constexpr std::uint32_t kSentinelWord = 0xffffffffU;

struct PendingEntryV1 {
    LocalizedSubtitleEntryV1 output;
    std::array<std::uint16_t, kLocalizedSubtitleLanguageCountV1>
        relative_text_offsets{};
};

[[noreturn]] void fail(const std::string& message) {
    throw LocalizedSubtitleBankError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint16_t>(byte_value(bytes[offset + 1U])) << 8U);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t align_up(
    const std::uint64_t value,
    const std::uint64_t alignment,
    const char* const description) {
    const auto mask = alignment - 1U;
    if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
        fail(std::string("Integer overflow while aligning ") + description);
    }
    return (value + mask) & ~mask;
}

[[nodiscard]] bool is_zero(
    const std::span<const std::byte> bytes,
    const std::size_t begin,
    const std::size_t end) {
    return std::all_of(
        bytes.begin() + static_cast<std::ptrdiff_t>(begin),
        bytes.begin() + static_cast<std::ptrdiff_t>(end),
        [](const std::byte value) { return value == std::byte{0}; });
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while counting ") + description);
    }
    return left + right;
}

} // namespace

LocalizedSubtitleDirectoryV1 parse_localized_subtitle_directory_v1(
    const std::span<const std::byte> bytes,
    const std::uint32_t table_offset,
    const LocalizedSubtitleBankLimitsV1 limits) {
    if (limits.max_input_bytes == 0U || limits.max_entries == 0U ||
        limits.max_total_text_bytes == 0U) {
        fail("LocalizedSubtitleBankV1 caller limits must all be non-zero");
    }
    if (bytes.size() > limits.max_input_bytes) {
        fail("LocalizedSubtitleBankV1 exceeds the caller's input-byte limit");
    }
    if (bytes.size() < kLocalizedSubtitleBankEntryBytesV1 ||
        bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
        (bytes.size() & (kDirectoryAlignment - 1U)) != 0U) {
        fail("LocalizedSubtitleBankV1 has an invalid input envelope");
    }
    if (table_offset > bytes.size() -
                           kLocalizedSubtitleBankEntryBytesV1 ||
        (table_offset & (kDirectoryAlignment - 1U)) != 0U) {
        fail("LocalizedSubtitleBankV1 has an invalid directory offset");
    }
    const auto localized_tail_bytes =
        bytes.size() - static_cast<std::size_t>(table_offset);

    LocalizedSubtitleDirectoryV1 result;
    result.table_offset = table_offset;
    std::vector<PendingEntryV1> pending_entries;
    auto cursor = static_cast<std::size_t>(table_offset);
    std::size_t sentinel_offset = 0U;
    bool found_sentinel = false;
    std::uint16_t previous_relative_text_offset = 0U;
    bool has_previous_text_offset = false;
    while (cursor <= bytes.size() - kLocalizedSubtitleBankEntryBytesV1) {
        if (read_le32(bytes, cursor) == kSentinelWord) {
            if (!is_zero(bytes, cursor + sizeof(std::uint32_t),
                         cursor + kLocalizedSubtitleBankEntryBytesV1)) {
                fail("LocalizedSubtitleBankV1 has a malformed directory sentinel");
            }
            sentinel_offset = cursor;
            cursor += kLocalizedSubtitleBankEntryBytesV1;
            found_sentinel = true;
            break;
        }
        if (pending_entries.size() >= limits.max_entries) {
            fail("LocalizedSubtitleBankV1 exceeds the caller's entry limit");
        }
        PendingEntryV1 pending;
        pending.output.directory_range = LocalizedSubtitleRangeV1{
            cursor, kLocalizedSubtitleBankEntryBytesV1};
        pending.output.start_tick = read_le16(bytes, cursor);
        pending.output.end_tick = read_le16(bytes, cursor + 2U);
        if (pending.output.start_tick > pending.output.end_tick) {
            fail("LocalizedSubtitleBankV1 has a reversed tick interval");
        }
        if (read_le16(bytes, cursor + 14U) != 0U) {
            fail("LocalizedSubtitleBankV1 has a non-zero entry reserved field");
        }
        for (std::size_t language = 0U;
             language < kLocalizedSubtitleLanguageCountV1;
             ++language) {
            const auto relative = read_le16(
                bytes, cursor + 4U + language * sizeof(std::uint16_t));
            if (relative == 0U ||
                (relative & (kTextAlignment - 1U)) != 0U ||
                (has_previous_text_offset &&
                 relative <= previous_relative_text_offset) ||
                relative >= localized_tail_bytes) {
                fail("LocalizedSubtitleBankV1 has invalid text offsets");
            }
            pending.relative_text_offsets[language] = relative;
            previous_relative_text_offset = relative;
            has_previous_text_offset = true;
        }
        pending_entries.push_back(std::move(pending));
        cursor += kLocalizedSubtitleBankEntryBytesV1;
    }
    if (!found_sentinel) {
        fail("LocalizedSubtitleBankV1 is missing its directory sentinel");
    }

    const auto directory_end_relative =
        cursor - static_cast<std::size_t>(table_offset);
    if (!pending_entries.empty() &&
        pending_entries.front().relative_text_offsets.front() !=
            directory_end_relative) {
        fail("LocalizedSubtitleBankV1 text does not immediately follow its directory");
    }
    result.directory_range = LocalizedSubtitleRangeV1{
        table_offset,
        cursor - static_cast<std::size_t>(table_offset)};
    result.sentinel_range = LocalizedSubtitleRangeV1{
        sentinel_offset, kLocalizedSubtitleBankEntryBytesV1};
    if (pending_entries.empty()) {
        result.text_range = LocalizedSubtitleRangeV1{cursor, 0U};
        result.trailing_opaque_range = LocalizedSubtitleRangeV1{
            cursor, bytes.size() - cursor};
        return result;
    }

    std::vector<std::uint16_t> all_offsets;
    all_offsets.reserve(
        pending_entries.size() * kLocalizedSubtitleLanguageCountV1);
    for (const auto& pending : pending_entries) {
        all_offsets.insert(
            all_offsets.end(),
            pending.relative_text_offsets.begin(),
            pending.relative_text_offsets.end());
    }

    auto aligned_text_end = cursor;
    for (std::size_t text_index = 0U;
         text_index < all_offsets.size();
         ++text_index) {
        const auto absolute_begin =
            static_cast<std::size_t>(table_offset) +
            all_offsets[text_index];
        const auto next_begin = text_index + 1U < all_offsets.size()
            ? static_cast<std::size_t>(table_offset) +
                  all_offsets[text_index + 1U]
            : bytes.size();
        const auto terminator = std::find(
            bytes.begin() + static_cast<std::ptrdiff_t>(absolute_begin),
            bytes.begin() + static_cast<std::ptrdiff_t>(next_begin),
            std::byte{0});
        if (terminator ==
            bytes.begin() + static_cast<std::ptrdiff_t>(next_begin)) {
            fail("LocalizedSubtitleBankV1 text is not NUL terminated");
        }
        const auto terminator_offset = static_cast<std::size_t>(
            terminator - bytes.begin());
        const auto logical_end = terminator_offset + 1U;
        const auto is_last_text = text_index + 1U == all_offsets.size();
        const auto expected_next = align_up(
            logical_end,
            is_last_text ? kDirectoryAlignment : kTextAlignment,
            is_last_text ? "subtitle tail" : "subtitle text");
        if (expected_next > next_begin ||
            (!is_last_text && expected_next != next_begin) ||
            !is_zero(bytes, logical_end, expected_next)) {
            fail("LocalizedSubtitleBankV1 text padding is not the minimum zero envelope");
        }
        if (is_last_text) {
            aligned_text_end = static_cast<std::size_t>(expected_next);
        }
        const auto text_bytes = terminator_offset - absolute_begin;
        result.total_text_bytes = checked_add(
            result.total_text_bytes,
            text_bytes,
            "LocalizedSubtitleBankV1 text bytes");
        if (result.total_text_bytes > limits.max_total_text_bytes) {
            fail("LocalizedSubtitleBankV1 exceeds the caller's text-byte limit");
        }
        const auto entry_index =
            text_index / kLocalizedSubtitleLanguageCountV1;
        const auto language =
            text_index % kLocalizedSubtitleLanguageCountV1;
        auto& output_text =
            pending_entries[entry_index].output.texts[language];
        output_text.range = LocalizedSubtitleRangeV1{
            absolute_begin, text_bytes};
        output_text.bytes.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(absolute_begin),
            terminator);
    }

    result.entries.reserve(pending_entries.size());
    for (auto& pending : pending_entries) {
        result.entries.push_back(std::move(pending.output));
    }
    result.text_range = LocalizedSubtitleRangeV1{
        cursor, aligned_text_end - cursor};
    result.trailing_opaque_range = LocalizedSubtitleRangeV1{
        aligned_text_end, bytes.size() - aligned_text_end};
    return result;
}

LocalizedSubtitleBankV1 parse_localized_subtitle_bank_v1(
    const std::span<const std::byte> bytes,
    const LocalizedSubtitleBankLimitsV1 limits) {
    constexpr auto kMinimumBytes =
        kLocalizedSubtitleBankHeaderBytesV1 +
        kLocalizedSubtitleBankEntryBytesV1;
    if (bytes.size() < kMinimumBytes) {
        fail("LocalizedSubtitleBankV1 has an invalid input envelope");
    }

    LocalizedSubtitleBankV1 result;
    result.input_bytes = bytes.size();
    for (std::size_t word_index = 0U;
         word_index < result.header_words.size();
         ++word_index) {
        result.header_words[word_index] = read_le32(
            bytes, word_index * sizeof(std::uint32_t));
    }
    result.table_offset = result.header_words[1U];
    result.secondary_offset = result.header_words[5U];
    if (result.header_words[2U] != kLocalizedSubtitleHeaderTagV1 ||
        result.header_words[3U] != kLocalizedSubtitleHeaderKindV1 ||
        result.header_words[4U] != kLocalizedSubtitleBankHeaderBytesV1 ||
        result.header_words[6U] != 0U || result.header_words[7U] != 0U) {
        fail("LocalizedSubtitleBankV1 has an invalid fixed header");
    }
    if (result.table_offset <
            kLocalizedSubtitleBankHeaderBytesV1 +
                kLocalizedSubtitleBankEntryBytesV1 ||
        result.table_offset >= bytes.size() ||
        (result.table_offset & (kDirectoryAlignment - 1U)) != 0U ||
        result.secondary_offset < kLocalizedSubtitleBankHeaderBytesV1 ||
        result.secondary_offset >= result.table_offset ||
        (result.secondary_offset & (kDirectoryAlignment - 1U)) != 0U) {
        fail("LocalizedSubtitleBankV1 has invalid section offsets");
    }
    result.opaque_body_range = LocalizedSubtitleRangeV1{
        kLocalizedSubtitleBankHeaderBytesV1,
        result.table_offset - kLocalizedSubtitleBankHeaderBytesV1};

    auto directory = parse_localized_subtitle_directory_v1(
        bytes, result.table_offset, limits);
    result.directory_range = directory.directory_range;
    result.sentinel_range = directory.sentinel_range;
    result.text_range = directory.text_range;
    result.trailing_opaque_range = directory.trailing_opaque_range;
    result.total_text_bytes = directory.total_text_bytes;
    result.entries = std::move(directory.entries);
    return result;
}

} // namespace openrc
