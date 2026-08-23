#include "openrc/companion_wad_index.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint64_t kWadV1MinimumLogicalSize = 0x10;

[[noreturn]] void fail(const std::string& message) {
    throw CompanionTerminalWadIndexError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left * right;
}

[[nodiscard]] std::uint64_t align_up_64(
    const std::uint64_t value,
    const char* description) {
    constexpr auto alignment =
        static_cast<std::uint64_t>(kCompanionTerminalWadIndexV1TargetAlignment);
    constexpr auto mask = alignment - 1U;
    static_assert((alignment & mask) == 0U);
    if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
        fail(std::string("Integer overflow while aligning ") + description);
    }
    return (value + mask) & ~mask;
}

void require_host_range(
    const CompanionTerminalWadRange range,
    const std::size_t input_size,
    const char* description) {
    const auto available = static_cast<std::uint64_t>(input_size);
    if (range.offset > available || range.size > available - range.offset) {
        fail(std::string(description) + " exceeds its input span");
    }
    if (range.offset > std::numeric_limits<std::size_t>::max() ||
        range.size > std::numeric_limits<std::size_t>::max()) {
        fail(std::string(description) + " exceeds the host address space");
    }
    const auto offset = static_cast<std::size_t>(range.offset);
    const auto size = static_cast<std::size_t>(range.size);
    if (offset > static_cast<std::size_t>(
            std::numeric_limits<std::ptrdiff_t>::max()) ||
        size > static_cast<std::size_t>(
            std::numeric_limits<std::ptrdiff_t>::max()) - offset) {
        fail(std::string(description) + " cannot be addressed by host iterators");
    }
}

[[nodiscard]] bool range_is_zero(
    const std::span<const std::byte> bytes,
    const CompanionTerminalWadRange range,
    const char* description) {
    require_host_range(range, bytes.size(), description);
    const auto offset = static_cast<std::size_t>(range.offset);
    const auto size = static_cast<std::size_t>(range.size);
    const auto values = bytes.subspan(offset, size);
    return std::all_of(
        values.begin(),
        values.end(),
        [](const std::byte value) { return value == std::byte{0}; });
}

[[nodiscard]] std::vector<std::byte> copy_range(
    const std::span<const std::byte> bytes,
    const CompanionTerminalWadRange range,
    const char* description) {
    require_host_range(range, bytes.size(), description);
    std::vector<std::byte> result;
    if (range.size > result.max_size()) {
        fail(std::string(description) + " exceeds the host container limit");
    }
    const auto offset = static_cast<std::size_t>(range.offset);
    const auto size = static_cast<std::size_t>(range.size);
    result.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
    return result;
}

[[nodiscard]] bool has_wad_magic(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return byte_value(bytes[offset]) == static_cast<std::uint8_t>('W') &&
        byte_value(bytes[offset + 1U]) == static_cast<std::uint8_t>('A') &&
        byte_value(bytes[offset + 2U]) == static_cast<std::uint8_t>('D');
}

} // namespace

CompanionTerminalWadIndexV1 parse_companion_terminal_wad_index_v1(
    const std::span<const std::byte> index_bytes,
    const std::span<const std::byte> target_bytes,
    const CompanionTerminalWadIndexLimits limits) {
    if (limits.max_index_bytes == 0U ||
        limits.max_target_bytes == 0U ||
        limits.max_records == 0U ||
        limits.max_total_logical_bytes == 0U) {
        fail("CompanionTerminalWadIndexV1 caller limits must all be non-zero");
    }

    const auto index_size = static_cast<std::uint64_t>(index_bytes.size());
    const auto target_size = static_cast<std::uint64_t>(target_bytes.size());
    if (index_size > limits.max_index_bytes) {
        fail("The CompanionTerminalWadIndexV1 input exceeds the caller's index-byte limit");
    }
    if (target_size > limits.max_target_bytes) {
        fail("The CompanionTerminalWadIndexV1 target exceeds the caller's target-byte limit");
    }
    if (target_bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail("The CompanionTerminalWadIndexV1 target exceeds its 32-bit indexed size");
    }
    if (index_bytes.size() <
        static_cast<std::size_t>(kCompanionTerminalWadIndexV1MinimumTableOffset)) {
        fail("The CompanionTerminalWadIndexV1 input is smaller than 0x90 bytes");
    }

    CompanionTerminalWadIndexV1 result;
    result.index_input_bytes = index_size;
    result.target_input_bytes = target_size;
    result.opaque_prefix_range = CompanionTerminalWadRange{
        0U,
        kCompanionTerminalWadIndexV1HeaderOffset};
    result.index_words_range = CompanionTerminalWadRange{
        kCompanionTerminalWadIndexV1HeaderOffset,
        kCompanionTerminalWadIndexV1MinimumTableOffset -
            kCompanionTerminalWadIndexV1HeaderOffset};

    const auto header_offset = static_cast<std::size_t>(
        kCompanionTerminalWadIndexV1HeaderOffset);
    for (std::size_t word_index = 0;
         word_index < result.raw_index_words.size();
         ++word_index) {
        result.raw_index_words[word_index] =
            read_le32(index_bytes, header_offset + word_index * 4U);
    }
    result.record_count = result.raw_index_words[0];
    result.table_offset = result.raw_index_words[1];
    result.opaque_header_word = result.raw_index_words[2];
    result.indexed_size = result.raw_index_words[3];

    if (result.record_count == 0U) {
        fail("CompanionTerminalWadIndexV1 must contain at least one record");
    }
    if (result.record_count > limits.max_records) {
        fail("CompanionTerminalWadIndexV1 record count exceeds the caller's limit");
    }
    if (result.table_offset <
        kCompanionTerminalWadIndexV1MinimumTableOffset) {
        fail("CompanionTerminalWadIndexV1 table begins before offset 0x90");
    }
    if (result.table_offset %
        kCompanionTerminalWadIndexV1TableAlignment != 0U) {
        fail("CompanionTerminalWadIndexV1 table offset is not 0x10-aligned");
    }
    if (result.indexed_size != target_bytes.size()) {
        fail("CompanionTerminalWadIndexV1 indexed size does not equal the target span size");
    }

    const auto table_bytes = checked_multiply(
        result.record_count,
        kCompanionTerminalWadIndexV1RecordSize,
        "the CompanionTerminalWadIndexV1 table size");
    const auto table_end = checked_add(
        result.table_offset,
        table_bytes,
        "the CompanionTerminalWadIndexV1 table end");
    if (table_end != index_size) {
        fail("CompanionTerminalWadIndexV1 table does not end exactly at index EOF");
    }

    result.opaque_between_range = CompanionTerminalWadRange{
        kCompanionTerminalWadIndexV1MinimumTableOffset,
        static_cast<std::uint64_t>(result.table_offset) -
            kCompanionTerminalWadIndexV1MinimumTableOffset};
    result.table_range = CompanionTerminalWadRange{
        result.table_offset,
        table_bytes};

    // A uint32 record count always fits this vector on the supported 64-bit
    // host, while a 32-bit vector's element-size-adjusted max_size can be much
    // smaller. Keep the meaningful host check without a tautological x64
    // comparison under strict warnings.
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        if (static_cast<std::uint64_t>(result.record_count) >
            static_cast<std::uint64_t>(result.records.max_size())) {
            fail("CompanionTerminalWadIndexV1 record metadata exceeds the host container limit");
        }
    }
    result.records.reserve(static_cast<std::size_t>(result.record_count));

    // First pass validates every descriptor and all target ranges without
    // copying any caller-controlled byte range.
    for (std::uint64_t record_index = 0U;
         record_index < result.record_count;
         ++record_index) {
        const auto record_offset = checked_add(
            result.table_offset,
            checked_multiply(
                record_index,
                kCompanionTerminalWadIndexV1RecordSize,
                "a CompanionTerminalWadIndexV1 record offset"),
            "a CompanionTerminalWadIndexV1 record offset");
        const auto record_offset_size = static_cast<std::size_t>(record_offset);

        CompanionTerminalWadIndexRecordV1 record;
        record.index_record_range = CompanionTerminalWadRange{
            record_offset,
            kCompanionTerminalWadIndexV1RecordSize};
        for (std::size_t word_index = 0;
             word_index < record.raw_words.size();
             ++word_index) {
            record.raw_words[word_index] =
                read_le32(index_bytes, record_offset_size + word_index * 4U);
        }
        record.target_offset = record.raw_words[0];
        record.opaque_word = record.raw_words[1];
        record.logical_size = record.raw_words[2];
        record.reserved_word = record.raw_words[3];

        if (record.reserved_word != 0U) {
            fail("A CompanionTerminalWadIndexV1 record has a non-zero reserved word");
        }
        if (record.target_offset %
            kCompanionTerminalWadIndexV1TableAlignment != 0U) {
            fail("A CompanionTerminalWadIndexV1 target offset is not 0x10-aligned");
        }
        if (record.logical_size < kWadV1MinimumLogicalSize) {
            fail("A CompanionTerminalWadIndexV1 logical size is smaller than a WadV1 header");
        }

        record.target_end = checked_add(
            record.target_offset,
            record.logical_size,
            "a CompanionTerminalWadIndexV1 target range end");
        record.logical_range = CompanionTerminalWadRange{
            record.target_offset,
            record.logical_size};
        require_host_range(
            record.logical_range,
            target_bytes.size(),
            "A CompanionTerminalWadIndexV1 logical WadV1 range");
        const auto target_offset = static_cast<std::size_t>(record.target_offset);
        if (!has_wad_magic(target_bytes, target_offset)) {
            fail("A CompanionTerminalWadIndexV1 target record lacks the WadV1 signature");
        }
        if (read_le32(target_bytes, target_offset + 3U) != record.logical_size) {
            fail("A CompanionTerminalWadIndexV1 target WadV1 logical size does not match its index record");
        }

        result.total_logical_bytes = checked_add(
            result.total_logical_bytes,
            record.logical_size,
            "the CompanionTerminalWadIndexV1 aggregate logical size");
        if (result.total_logical_bytes > limits.max_total_logical_bytes) {
            fail("CompanionTerminalWadIndexV1 logical bytes exceed the caller's aggregate limit");
        }
        result.records.push_back(std::move(record));
    }

    // The first target offset is intentionally not required to be zero. It
    // separates the terminal chain from an earlier opaque part of subrange10.
    result.opaque_target_prefix_range = CompanionTerminalWadRange{
        0U,
        result.records.front().target_offset};
    result.terminal_chain_range = CompanionTerminalWadRange{
        result.records.front().target_offset,
        target_size - result.records.front().target_offset};

    for (std::size_t record_index = 0;
         record_index < result.records.size();
         ++record_index) {
        auto& record = result.records[record_index];
        const auto padded_end = align_up_64(
            record.target_end,
            "a CompanionTerminalWadIndexV1 target record end");
        const auto expected_next = record_index + 1U < result.records.size()
            ? static_cast<std::uint64_t>(
                  result.records[record_index + 1U].target_offset)
            : static_cast<std::uint64_t>(result.indexed_size);
        if (padded_end != expected_next) {
            fail("CompanionTerminalWadIndexV1 records do not form the required 0x40-aligned terminal chain");
        }

        record.padding_after_range = CompanionTerminalWadRange{
            record.target_end,
            padded_end - record.target_end};
        if (!range_is_zero(
                target_bytes,
                record.padding_after_range,
                "CompanionTerminalWadIndexV1 alignment padding")) {
            fail("CompanionTerminalWadIndexV1 alignment padding is non-zero");
        }
        result.total_padding_bytes = checked_add(
            result.total_padding_bytes,
            record.padding_after_range.size,
            "the CompanionTerminalWadIndexV1 aggregate padding size");
    }

    // Preflight every host-container limit before copying even the first
    // caller-controlled byte. This keeps a late oversized record from
    // producing a partially populated report on narrower hosts.
    const std::vector<std::byte> byte_container_probe;
    if (static_cast<std::uint64_t>(result.table_offset) >
        static_cast<std::uint64_t>(byte_container_probe.max_size())) {
        fail("CompanionTerminalWadIndexV1 pre-table bytes exceed the host container limit");
    }
    for (const auto& record : result.records) {
        if (static_cast<std::uint64_t>(record.logical_size) >
            static_cast<std::uint64_t>(byte_container_probe.max_size())) {
            fail("A CompanionTerminalWadIndexV1 logical WadV1 record exceeds the host container limit");
        }
    }

    // All record descriptors, relationships, aggregate limits, host ranges,
    // signatures, padding bytes, and allocation sizes have passed before the
    // first owned copy.
    const CompanionTerminalWadRange pre_table_range{0U, result.table_offset};
    result.pre_table_bytes = copy_range(
        index_bytes,
        pre_table_range,
        "The CompanionTerminalWadIndexV1 pre-table bytes");
    for (auto& record : result.records) {
        record.wad_bytes = copy_range(
            target_bytes,
            record.logical_range,
            "A CompanionTerminalWadIndexV1 logical WadV1 record");
    }
    return result;
}

} // namespace openrc
