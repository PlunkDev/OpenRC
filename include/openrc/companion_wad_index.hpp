#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kCompanionTerminalWadIndexV1HeaderOffset = 0x80;
inline constexpr std::uint32_t kCompanionTerminalWadIndexV1MinimumTableOffset = 0x90;
inline constexpr std::uint32_t kCompanionTerminalWadIndexV1RecordSize = 0x10;
inline constexpr std::uint32_t kCompanionTerminalWadIndexV1TableAlignment = 0x10;
inline constexpr std::uint32_t kCompanionTerminalWadIndexV1TargetAlignment = 0x40;

struct CompanionTerminalWadIndexLimits {
    std::uint64_t max_index_bytes = 0;
    std::uint64_t max_target_bytes = 0;
    std::uint64_t max_records = 0;
    std::uint64_t max_total_logical_bytes = 0;
};

struct CompanionTerminalWadRange {
    // The containing span is identified by the field that owns this range.
    std::uint64_t offset = 0;
    std::uint64_t size = 0;

    [[nodiscard]] bool operator==(const CompanionTerminalWadRange&) const = default;
};

struct CompanionTerminalWadIndexRecordV1 {
    CompanionTerminalWadRange index_record_range;
    std::array<std::uint32_t, 4> raw_words{};
    std::uint32_t target_offset = 0;
    std::uint32_t opaque_word = 0;
    std::uint32_t logical_size = 0;
    std::uint32_t reserved_word = 0;
    std::uint64_t target_end = 0;
    CompanionTerminalWadRange logical_range;
    CompanionTerminalWadRange padding_after_range;

    // Owns exactly one logical WadV1 record. Alignment padding is deliberately
    // excluded and the returned bytes never borrow from the target input.
    std::vector<std::byte> wad_bytes;
};

struct CompanionTerminalWadIndexV1 {
    std::uint64_t index_input_bytes = 0;
    std::uint64_t target_input_bytes = 0;

    // These ranges are relative to index_bytes. Both opaque ranges belong to
    // the broader companion format and are reported without assigning them
    // terminal-index semantics.
    CompanionTerminalWadRange opaque_prefix_range;
    CompanionTerminalWadRange index_words_range;
    CompanionTerminalWadRange opaque_between_range;
    CompanionTerminalWadRange table_range;

    std::array<std::uint32_t, 4> raw_index_words{};
    std::uint32_t record_count = 0;
    std::uint32_t table_offset = 0;
    std::uint32_t opaque_header_word = 0;
    std::uint32_t indexed_size = 0;

    // Owns the complete broader-format prefix [0, table_offset), including
    // both opaque ranges and the four raw terminal-index words.
    std::vector<std::byte> pre_table_bytes;

    // These ranges are relative to target_bytes. The bytes preceding the
    // first indexed WadV1 remain opaque and are not copied into the report.
    CompanionTerminalWadRange opaque_target_prefix_range;
    CompanionTerminalWadRange terminal_chain_range;
    std::uint64_t total_logical_bytes = 0;
    std::uint64_t total_padding_bytes = 0;
    std::vector<CompanionTerminalWadIndexRecordV1> records;
};

class CompanionTerminalWadIndexError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses the terminal WadV1 index embedded in raw primary-extent0/subrange2
// and validates it against the complete decoded primary-extent0/subrange10
// target. The parser is deliberately neutral: it neither decodes the owned
// WadV1 records nor assigns meaning to either opaque word.
[[nodiscard]] CompanionTerminalWadIndexV1 parse_companion_terminal_wad_index_v1(
    std::span<const std::byte> index_bytes,
    std::span<const std::byte> target_bytes,
    CompanionTerminalWadIndexLimits limits);

} // namespace openrc
