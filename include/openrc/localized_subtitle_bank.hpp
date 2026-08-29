#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kLocalizedSubtitleBankHeaderBytesV1 = 0x20U;
inline constexpr std::uint32_t kLocalizedSubtitleBankEntryBytesV1 = 0x10U;
inline constexpr std::size_t kLocalizedSubtitleLanguageCountV1 = 5U;
inline constexpr std::uint32_t kLocalizedSubtitleHeaderTagV1 = 0xfffffffaU;
inline constexpr std::uint32_t kLocalizedSubtitleHeaderKindV1 = 1U;

struct LocalizedSubtitleBankLimitsV1 {
    std::uint64_t max_input_bytes = 0U;
    std::uint64_t max_entries = 0U;
    std::uint64_t max_total_text_bytes = 0U;
};

struct LocalizedSubtitleRangeV1 {
    std::uint64_t offset = 0U;
    std::uint64_t size = 0U;

    [[nodiscard]] bool operator==(
        const LocalizedSubtitleRangeV1&) const = default;
};

struct LocalizedSubtitleTextV1 {
    // Absolute range in the decoded payload, excluding the terminating NUL
    // and alignment padding. Raw bytes retain the game's single-byte text
    // encoding; this parser does not guess a Unicode code page.
    LocalizedSubtitleRangeV1 range;
    std::vector<std::byte> bytes;

    [[nodiscard]] bool operator==(
        const LocalizedSubtitleTextV1&) const = default;
};

struct LocalizedSubtitleEntryV1 {
    LocalizedSubtitleRangeV1 directory_range;
    std::uint16_t start_tick = 0U;
    std::uint16_t end_tick = 0U;
    std::array<LocalizedSubtitleTextV1,
               kLocalizedSubtitleLanguageCountV1> texts;

    [[nodiscard]] bool operator==(
        const LocalizedSubtitleEntryV1&) const = default;
};

struct LocalizedSubtitleDirectoryV1 {
    std::uint32_t table_offset = 0U;
    LocalizedSubtitleRangeV1 directory_range;
    LocalizedSubtitleRangeV1 sentinel_range;
    // Includes text terminators and their minimum zero alignment padding.
    LocalizedSubtitleRangeV1 text_range;
    // Some retail payloads retain unrelated/non-deterministic bytes after the
    // final aligned string. They are preserved as an opaque borrowed range.
    LocalizedSubtitleRangeV1 trailing_opaque_range;
    std::uint64_t total_text_bytes = 0U;
    std::vector<LocalizedSubtitleEntryV1> entries;
};

struct LocalizedSubtitleBankV1 {
    std::uint64_t input_bytes = 0U;
    std::array<std::uint32_t, 8U> header_words{};
    std::uint32_t table_offset = 0U;
    std::uint32_t secondary_offset = 0U;

    // Bytes after the partially understood 0x20-byte header and before the
    // localized directory remain opaque and are not copied.
    LocalizedSubtitleRangeV1 opaque_body_range;
    LocalizedSubtitleRangeV1 directory_range;
    LocalizedSubtitleRangeV1 sentinel_range;
    // Includes text terminators and their minimum zero alignment padding.
    LocalizedSubtitleRangeV1 text_range;
    LocalizedSubtitleRangeV1 trailing_opaque_range;
    std::uint64_t total_text_bytes = 0U;
    std::vector<LocalizedSubtitleEntryV1> entries;
};

class LocalizedSubtitleBankError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses one complete decoded payload carrying the PAL five-language subtitle
// directory observed in local resource WAD runs. The broader body is retained
// as an opaque range, while every directory entry, relative text offset, NUL,
// and minimum 4/16-byte padding envelope is validated under explicit limits.
[[nodiscard]] LocalizedSubtitleBankV1 parse_localized_subtitle_bank_v1(
    std::span<const std::byte> bytes,
    LocalizedSubtitleBankLimitsV1 limits);

// Parses the PAL five-language directory and text tail beginning at an
// explicitly supplied absolute offset. This is shared by the standalone
// one-actor subtitle-bank parser and the broader scene-animation parser.
[[nodiscard]] LocalizedSubtitleDirectoryV1
parse_localized_subtitle_directory_v1(
    std::span<const std::byte> bytes,
    std::uint32_t table_offset,
    LocalizedSubtitleBankLimitsV1 limits);

} // namespace openrc
