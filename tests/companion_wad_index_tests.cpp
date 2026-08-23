#include "openrc/companion_wad_index.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kTableOffset = 0xb0;
constexpr std::size_t kRecordCount = 3;
constexpr std::size_t kIndexSize =
    kTableOffset + kRecordCount *
        openrc::kCompanionTerminalWadIndexV1RecordSize;
constexpr std::size_t kTargetSize = 0x180;
constexpr std::array<std::size_t, kRecordCount> kTargetOffsets{
    0x40U,
    0x80U,
    0x100U,
};
constexpr std::array<std::size_t, kRecordCount> kLogicalSizes{
    0x23U,
    0x51U,
    0x50U,
};
constexpr openrc::CompanionTerminalWadIndexLimits kGenerousLimits{
    4096U,
    4096U,
    32U,
    4096U,
};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void expect_index_error(Function&& function, const std::string& message) {
    try {
        std::invoke(std::forward<Function>(function));
    } catch (const openrc::CompanionTerminalWadIndexError&) {
        return;
    }
    throw std::runtime_error(message);
}

void write_le32(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void write_record(
    const std::span<std::byte> index,
    const std::size_t record_index,
    const std::uint32_t target_offset,
    const std::uint32_t opaque_word,
    const std::uint32_t logical_size,
    const std::uint32_t reserved_word = 0U) {
    const auto offset = kTableOffset + record_index *
        openrc::kCompanionTerminalWadIndexV1RecordSize;
    write_le32(index, offset, target_offset);
    write_le32(index, offset + 4U, opaque_word);
    write_le32(index, offset + 8U, logical_size);
    write_le32(index, offset + 12U, reserved_word);
}

void write_wad(
    const std::span<std::byte> target,
    const std::size_t offset,
    const std::size_t logical_size,
    const std::uint8_t seed) {
    for (std::size_t index = 0; index < logical_size; ++index) {
        target[offset + index] = static_cast<std::byte>(
            static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index)));
    }
    target[offset] = std::byte{'W'};
    target[offset + 1U] = std::byte{'A'};
    target[offset + 2U] = std::byte{'D'};
    write_le32(
        target,
        offset + 3U,
        static_cast<std::uint32_t>(logical_size));
}

struct Fixture {
    std::vector<std::byte> index;
    std::vector<std::byte> target;
};

[[nodiscard]] Fixture valid_fixture() {
    Fixture fixture{
        std::vector<std::byte>(kIndexSize, std::byte{0}),
        std::vector<std::byte>(kTargetSize, std::byte{0}),
    };

    // Neither the broader companion prefix nor the target prefix is terminal
    // alignment padding, so deliberately make both non-zero.
    for (std::size_t offset = 0; offset < kTableOffset; ++offset) {
        fixture.index[offset] = static_cast<std::byte>(
            static_cast<std::uint8_t>((offset * 7U + 3U) & 0xffU));
    }
    for (std::size_t offset = 0; offset < kTargetOffsets[0]; ++offset) {
        fixture.target[offset] = static_cast<std::byte>(
            static_cast<std::uint8_t>((offset * 5U + 1U) & 0xffU));
    }

    write_le32(fixture.index, 0x80U, static_cast<std::uint32_t>(kRecordCount));
    write_le32(fixture.index, 0x84U, static_cast<std::uint32_t>(kTableOffset));
    write_le32(fixture.index, 0x88U, 0xfedcba98U);
    write_le32(fixture.index, 0x8cU, static_cast<std::uint32_t>(kTargetSize));

    for (std::size_t index = 0; index < kRecordCount; ++index) {
        write_record(
            fixture.index,
            index,
            static_cast<std::uint32_t>(kTargetOffsets[index]),
            static_cast<std::uint32_t>(0x81000000U + index * 0x101U),
            static_cast<std::uint32_t>(kLogicalSizes[index]));
        write_wad(
            fixture.target,
            kTargetOffsets[index],
            kLogicalSizes[index],
            static_cast<std::uint8_t>(0x21U + index * 0x10U));
    }
    return fixture;
}

[[nodiscard]] std::vector<std::byte> copied_range(
    const std::span<const std::byte> bytes,
    const std::size_t offset,
    const std::size_t size) {
    return std::vector<std::byte>(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
}

void test_valid_index_and_owned_records() {
    auto fixture = valid_fixture();
    const auto expected_pre_table = copied_range(fixture.index, 0U, kTableOffset);
    std::array<std::vector<std::byte>, kRecordCount> expected_wads;
    for (std::size_t index = 0; index < kRecordCount; ++index) {
        expected_wads[index] = copied_range(
            fixture.target,
            kTargetOffsets[index],
            kLogicalSizes[index]);
    }

    const auto report = openrc::parse_companion_terminal_wad_index_v1(
        fixture.index,
        fixture.target,
        kGenerousLimits);
    expect(report.index_input_bytes == kIndexSize, "index byte count is wrong");
    expect(report.target_input_bytes == kTargetSize, "target byte count is wrong");
    expect(
        report.opaque_prefix_range == openrc::CompanionTerminalWadRange{0U, 0x80U},
        "opaque companion prefix range is wrong");
    expect(
        report.index_words_range == openrc::CompanionTerminalWadRange{0x80U, 0x10U},
        "terminal index-word range is wrong");
    expect(
        report.opaque_between_range == openrc::CompanionTerminalWadRange{0x90U, 0x20U},
        "opaque between range is wrong");
    expect(
        report.table_range == openrc::CompanionTerminalWadRange{kTableOffset, 0x30U},
        "terminal table range is wrong");
    expect(report.record_count == kRecordCount, "record count is wrong");
    expect(report.table_offset == kTableOffset, "table offset is wrong");
    expect(report.opaque_header_word == 0xfedcba98U, "opaque header word changed");
    expect(report.indexed_size == kTargetSize, "indexed size is wrong");
    expect(
        report.raw_index_words == std::array<std::uint32_t, 4>{
            3U,
            0xb0U,
            0xfedcba98U,
            0x180U,
        },
        "raw terminal index words changed");
    expect(report.pre_table_bytes == expected_pre_table, "pre-table bytes changed");
    expect(
        report.opaque_target_prefix_range ==
            openrc::CompanionTerminalWadRange{0U, 0x40U},
        "opaque target prefix range is wrong");
    expect(
        report.terminal_chain_range ==
            openrc::CompanionTerminalWadRange{0x40U, 0x140U},
        "terminal chain range is wrong");
    expect(report.total_logical_bytes == 0xc4U, "aggregate logical bytes are wrong");
    expect(report.total_padding_bytes == 0x7cU, "aggregate padding bytes are wrong");
    expect(report.records.size() == kRecordCount, "record vector size is wrong");

    constexpr std::array<std::size_t, kRecordCount> kPaddingSizes{
        0x1dU,
        0x2fU,
        0x30U,
    };
    for (std::size_t index = 0; index < kRecordCount; ++index) {
        const auto& record = report.records[index];
        expect(
            record.index_record_range == openrc::CompanionTerminalWadRange{
                kTableOffset + index * 0x10U,
                0x10U,
            },
            "record's index range is wrong");
        expect(record.target_offset == kTargetOffsets[index], "target offset is wrong");
        expect(record.logical_size == kLogicalSizes[index], "logical size is wrong");
        expect(
            record.opaque_word == 0x81000000U + index * 0x101U,
            "opaque record word changed");
        expect(record.raw_words[1] == record.opaque_word, "raw record words changed");
        expect(record.reserved_word == 0U, "reserved record word is wrong");
        expect(
            record.logical_range == openrc::CompanionTerminalWadRange{
                kTargetOffsets[index],
                kLogicalSizes[index],
            },
            "logical WadV1 range is wrong");
        expect(
            record.padding_after_range == openrc::CompanionTerminalWadRange{
                kTargetOffsets[index] + kLogicalSizes[index],
                kPaddingSizes[index],
            },
            "record padding range is wrong");
        expect(record.wad_bytes == expected_wads[index], "owned WadV1 bytes changed");
    }

    std::fill(fixture.index.begin(), fixture.index.end(), std::byte{0xff});
    std::fill(fixture.target.begin(), fixture.target.end(), std::byte{0xff});
    expect(report.pre_table_bytes == expected_pre_table, "pre-table bytes were borrowed");
    for (std::size_t index = 0; index < kRecordCount; ++index) {
        expect(report.records[index].wad_bytes == expected_wads[index], "WadV1 bytes were borrowed");
    }
}

void test_unknown_opaque_values_and_empty_between_range() {
    auto fixture = valid_fixture();
    fixture.index.erase(
        fixture.index.begin() + 0x90,
        fixture.index.begin() + static_cast<std::ptrdiff_t>(kTableOffset));
    write_le32(fixture.index, 0x84U, 0x90U);
    write_le32(fixture.index, 0x88U, 0xffffffffU);
    for (std::size_t index = 0; index < kRecordCount; ++index) {
        const auto offset = 0x90U + index * 0x10U;
        write_le32(fixture.index, offset + 4U, static_cast<std::uint32_t>(
            0xffffffffU - static_cast<std::uint32_t>(index)));
    }

    const auto report = openrc::parse_companion_terminal_wad_index_v1(
        fixture.index,
        fixture.target,
        kGenerousLimits);
    expect(report.table_offset == 0x90U, "minimum table offset was rejected");
    expect(report.opaque_between_range.size == 0U, "empty opaque range is wrong");
    expect(report.opaque_header_word == 0xffffffffU, "unknown header word was rejected");
    expect(report.records[0].opaque_word == 0xffffffffU, "unknown record word was rejected");
}

void test_mandatory_limits() {
    const auto fixture = valid_fixture();
    for (std::size_t limit_index = 0; limit_index < 4U; ++limit_index) {
        auto limits = kGenerousLimits;
        if (limit_index == 0U) {
            limits.max_index_bytes = 0U;
        } else if (limit_index == 1U) {
            limits.max_target_bytes = 0U;
        } else if (limit_index == 2U) {
            limits.max_records = 0U;
        } else {
            limits.max_total_logical_bytes = 0U;
        }
        expect_index_error(
            [&] {
                (void)openrc::parse_companion_terminal_wad_index_v1(
                    fixture.index,
                    fixture.target,
                    limits);
            },
            "a zero mandatory caller limit was accepted");
    }

    for (std::size_t limit_index = 0; limit_index < 4U; ++limit_index) {
        auto limits = kGenerousLimits;
        if (limit_index == 0U) {
            limits.max_index_bytes = kIndexSize - 1U;
        } else if (limit_index == 1U) {
            limits.max_target_bytes = kTargetSize - 1U;
        } else if (limit_index == 2U) {
            limits.max_records = kRecordCount - 1U;
        } else {
            limits.max_total_logical_bytes = 0xc3U;
        }
        expect_index_error(
            [&] {
                (void)openrc::parse_companion_terminal_wad_index_v1(
                    fixture.index,
                    fixture.target,
                    limits);
            },
            "a caller limit violation was accepted");
    }
}

void test_header_and_table_rejections() {
    {
        auto fixture = valid_fixture();
        fixture.index.resize(0x8fU);
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "a truncated fixed index header was accepted");
    }
    {
        auto fixture = valid_fixture();
        write_le32(fixture.index, 0x80U, 0U);
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "an empty terminal index was accepted");
    }
    for (const auto offset : std::array<std::uint32_t, 3>{
             0x80U,
             0x91U,
             0xfffffff0U,
         }) {
        auto fixture = valid_fixture();
        write_le32(fixture.index, 0x84U, offset);
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "an invalid or out-of-bounds table offset was accepted");
    }
    {
        auto fixture = valid_fixture();
        write_le32(fixture.index, 0x80U, std::numeric_limits<std::uint32_t>::max());
        auto limits = kGenerousLimits;
        limits.max_records = std::numeric_limits<std::uint32_t>::max();
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, limits); },
            "a huge widened table-size calculation was accepted");
    }
    {
        auto fixture = valid_fixture();
        write_le32(fixture.index, 0x8cU, static_cast<std::uint32_t>(kTargetSize - 1U));
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "an indexed-size mismatch was accepted");
    }
    {
        auto fixture = valid_fixture();
        fixture.index.push_back(std::byte{0});
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "trailing index bytes after the table were accepted");
    }
    {
        auto fixture = valid_fixture();
        fixture.index.pop_back();
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "a truncated index table was accepted");
    }
}

void test_record_rejections() {
    {
        auto fixture = valid_fixture();
        write_le32(fixture.index, kTableOffset + 12U, 1U);
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "a non-zero record reserved word was accepted");
    }
    {
        auto fixture = valid_fixture();
        write_le32(fixture.index, kTableOffset, 0x41U);
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "an unaligned target offset was accepted");
    }
    {
        auto fixture = valid_fixture();
        write_le32(fixture.index, kTableOffset + 8U, 0x0fU);
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "a too-small logical WadV1 size was accepted");
    }
    {
        auto fixture = valid_fixture();
        write_record(fixture.index, 2U, 0xfffffff0U, 9U, 0x40U);
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "an out-of-bounds target range was accepted");
    }
    {
        auto fixture = valid_fixture();
        fixture.target[kTargetOffsets[1]] = std::byte{'X'};
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "a bad WadV1 signature was accepted");
    }
    {
        auto fixture = valid_fixture();
        write_le32(
            fixture.target,
            kTargetOffsets[1] + 3U,
            static_cast<std::uint32_t>(kLogicalSizes[1] + 1U));
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "a WadV1 logical-size mismatch was accepted");
    }
}

void test_chain_and_padding_rejections() {
    for (const auto offset : std::array<std::uint32_t, 2>{0x70U, 0x90U}) {
        auto fixture = valid_fixture();
        write_record(
            fixture.index,
            1U,
            offset,
            0x12345678U,
            static_cast<std::uint32_t>(kLogicalSizes[1]));
        if (offset == 0x70U) {
            write_wad(fixture.target, 0x70U, kLogicalSizes[1], 0x31U);
        } else {
            write_wad(fixture.target, 0x90U, kLogicalSizes[1], 0x31U);
        }
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "a gap or overlap in the aligned terminal chain was accepted");
    }
    {
        auto fixture = valid_fixture();
        fixture.target[kTargetOffsets[0] + kLogicalSizes[0]] = std::byte{1};
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "non-zero inter-record padding was accepted");
    }
    {
        auto fixture = valid_fixture();
        fixture.target[kTargetOffsets[2] + kLogicalSizes[2]] = std::byte{1};
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "non-zero final padding was accepted");
    }
    {
        auto fixture = valid_fixture();
        constexpr std::size_t kShortFinalSize = 0x40U;
        write_record(
            fixture.index,
            2U,
            static_cast<std::uint32_t>(kTargetOffsets[2]),
            0xabcdef01U,
            static_cast<std::uint32_t>(kShortFinalSize));
        write_wad(fixture.target, kTargetOffsets[2], kShortFinalSize, 0x41U);
        expect_index_error(
            [&] { (void)openrc::parse_companion_terminal_wad_index_v1(
                fixture.index, fixture.target, kGenerousLimits); },
            "a final aligned end before indexed_size was accepted");
    }
}

} // namespace

int main() {
    try {
        test_valid_index_and_owned_records();
        test_unknown_opaque_values_and_empty_between_range();
        test_mandatory_limits();
        test_header_and_table_rejections();
        test_record_rejections();
        test_chain_and_padding_rejections();
        std::cout << "CompanionTerminalWadIndexV1 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CompanionTerminalWadIndexV1 tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
