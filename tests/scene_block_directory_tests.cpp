#include "openrc/scene_block_directory.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr openrc::SceneBlockDirectoryLimits kGenerousLimits{
    2048U,
    16U,
    2048U,
};

using SectionBoundaries = std::array<
    std::uint32_t,
    openrc::kSceneBlockSectionBoundaryCount>;

constexpr SectionBoundaries kFirstSectionBoundaries{
    0x00U, 0x20U, 0x50U, 0x70U, 0x80U,
    0xa0U, 0xb0U, 0xc0U, 0xe0U};
constexpr SectionBoundaries kSecondSectionBoundaries{
    0x00U, 0x10U, 0x40U, 0x50U, 0x70U,
    0xa0U, 0xc0U, 0xe0U, 0x100U};
constexpr SectionBoundaries kThirdSectionBoundaries{
    0x00U, 0x30U, 0x40U, 0x70U, 0x90U,
    0xb0U, 0xe0U, 0x110U, 0x120U};

void expect(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void expect_directory_error(Function&& function, const char* message) {
    try {
        std::invoke(std::forward<Function>(function));
    } catch (const openrc::SceneBlockDirectoryError&) {
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

[[nodiscard]] std::uint32_t float_bits(const float value) {
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] constexpr std::uint32_t pack_halfwords(
    const std::uint32_t low,
    const std::uint32_t high) {
    return (low & 0xffffU) | ((high & 0xffffU) << 16U);
}

[[nodiscard]] std::size_t entry_word_offset(
    const std::size_t entry,
    const std::size_t word) {
    return openrc::kSceneBlockDirectoryV1Stride +
        entry * openrc::kSceneBlockDirectoryV1Stride + word * 4U;
}

void write_entry(
    const std::span<std::byte> bytes,
    const std::size_t entry,
    const std::array<float, 4> values,
    const std::uint32_t block_offset,
    const SectionBoundaries& section_boundaries,
    const std::uint32_t marker) {
    for (std::size_t component = 0; component < values.size(); ++component) {
        write_le32(
            bytes,
            entry_word_offset(entry, component),
            float_bits(values[component]));
    }
    write_le32(bytes, entry_word_offset(entry, 4U), block_offset);
    write_le32(
        bytes,
        entry_word_offset(entry, 5U),
        pack_halfwords(section_boundaries[0], section_boundaries[1]));
    write_le32(
        bytes,
        entry_word_offset(entry, 6U),
        pack_halfwords(section_boundaries[3], section_boundaries[4]));
    write_le32(
        bytes,
        entry_word_offset(entry, 7U),
        pack_halfwords(section_boundaries[2], section_boundaries[5]));
    write_le32(bytes, entry_word_offset(entry, 8U), marker);
    write_le32(bytes, entry_word_offset(entry, 9U), marker ^ 0xa5a5a5a5U);
    write_le32(bytes, entry_word_offset(entry, 10U), marker ^ 0x3c3c3c3cU);
    write_le32(
        bytes,
        entry_word_offset(entry, 11U),
        pack_halfwords(
            (section_boundaries[8] - section_boundaries[7]) /
                openrc::kSceneBlockSectionAlignment,
            section_boundaries[7]));
    write_le32(
        bytes,
        entry_word_offset(entry, 12U),
        pack_halfwords(section_boundaries[6], section_boundaries[7]));
    write_le32(
        bytes,
        entry_word_offset(entry, 13U),
        openrc::kSceneBlockSectionMarker);
    write_le32(bytes, entry_word_offset(entry, 14U), section_boundaries.back());
    write_le32(bytes, entry_word_offset(entry, 15U), marker ^ 0x5a5a5a5aU);
}

[[nodiscard]] std::vector<std::byte> valid_directory() {
    constexpr std::size_t kInputBytes = 0x4c0U;
    constexpr std::uint32_t kDeclaredCount = 3U;
    constexpr std::uint32_t kFirstBlockOffset = 0xc0U;
    constexpr std::uint32_t kSecondBlockOffset = 0x1e0U;
    constexpr std::uint32_t kThirdBlockOffset = 0x320U;

    std::vector<std::byte> bytes(kInputBytes, std::byte{0});
    write_le32(bytes, 0x00U, openrc::kSceneBlockDirectoryV1Stride);
    write_le32(bytes, 0x04U, kDeclaredCount);
    write_le32(bytes, 0x08U, float_bits(2.5F));
    write_entry(
        bytes,
        0U,
        {1.0F, -2.0F, 0.0F, 4.0F},
        kFirstBlockOffset,
        kFirstSectionBoundaries,
        0x11223344U);
    write_entry(
        bytes,
        1U,
        {-4.0F, 8.0F, 12.0F, 0.5F},
        kSecondBlockOffset,
        kSecondSectionBoundaries,
        0x55667788U);

    for (std::size_t offset = kFirstBlockOffset; offset < 0x1e0U; ++offset) {
        bytes[offset] = static_cast<std::byte>(
            static_cast<std::uint8_t>((offset * 3U) & 0xffU));
    }
    for (std::size_t offset = kSecondBlockOffset; offset < 0x320U; ++offset) {
        bytes[offset] = static_cast<std::byte>(
            static_cast<std::uint8_t>((offset * 5U + 1U) & 0xffU));
    }
    for (std::size_t offset = kThirdBlockOffset; offset < 0x480U; ++offset) {
        bytes[offset] = static_cast<std::byte>(
            static_cast<std::uint8_t>((offset * 7U + 2U) & 0xffU));
    }
    for (std::size_t offset = 0x480U; offset < bytes.size(); ++offset) {
        bytes[offset] = static_cast<std::byte>(
            static_cast<std::uint8_t>((offset * 11U + 3U) & 0xffU));
    }
    // The final descriptor occupies the first block's fixed prefix and points
    // to the final envelope after the two descriptors stored after the header.
    write_entry(
        bytes,
        2U,
        {16.0F, 32.0F, -8.0F, 2.0F},
        kThirdBlockOffset,
        kThirdSectionBoundaries,
        0x99aabbccU);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> copied_range(
    const std::span<const std::byte> bytes,
    const std::size_t offset,
    const std::size_t size) {
    return std::vector<std::byte>(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
}

void expect_section_layout(
    const openrc::SceneBlockDirectoryEntryV1& entry,
    const SectionBoundaries& expected_boundaries,
    const std::uint64_t expected_remainder_offset) {
    expect(
        entry.section_layout.relative_boundaries == expected_boundaries,
        "scene section boundaries are wrong");
    for (std::size_t index = 0U;
         index < openrc::kSceneBlockSectionCount;
         ++index) {
        const auto expected_size =
            expected_boundaries[index + 1U] - expected_boundaries[index];
        expect(
            entry.section_layout.ranges[index] == openrc::SceneBlockRange{
                expected_remainder_offset + expected_boundaries[index],
                expected_size},
            "an absolute scene section range is wrong");
    }
    expect(
        entry.section_layout.ranges.front().offset ==
                entry.remainder_range.offset &&
            entry.section_layout.ranges.back().offset +
                    entry.section_layout.ranges.back().size ==
                entry.block_end,
        "scene sections do not partition the complete block remainder");
}

void test_valid_directory_and_owned_ranges() {
    auto bytes = valid_directory();
    const auto first_expected = copied_range(bytes, 0xc0U, 0x120U);
    const auto second_expected = copied_range(bytes, 0x1e0U, 0x140U);
    const auto third_expected = copied_range(bytes, 0x320U, 0x160U);
    const auto trailing_expected = copied_range(bytes, 0x480U, 0x40U);
    const auto report = openrc::parse_scene_block_directory_v1(
        bytes,
        kGenerousLimits);

    expect(report.input_bytes == 0x4c0U, "scene directory input size is wrong");
    expect(report.stride_bytes == 0x40U, "scene directory stride is wrong");
    expect(report.declared_count == 3U, "scene directory count is wrong");
    expect(report.header_float == 2.5F, "scene directory header float is wrong");
    expect(report.directory_bytes == 0xc0U, "scene directory size is wrong");
    expect(report.record_count == 3U, "scene directory record count is wrong");
    expect(report.entries.size() == 3U, "scene directory entry vector is wrong");
    expect(
        report.overlapped_entry_range == openrc::SceneBlockRange{0xc0U, 0x40U},
        "scene overlapped descriptor range is wrong");
    expect(report.owned_byte_count == 0x400U, "scene owned byte count is wrong");
    expect(report.chain_end == 0x480U, "scene block chain end is wrong");
    expect(
        report.trailing_range == openrc::SceneBlockRange{0x480U, 0x40U},
        "scene trailing range is wrong");
    expect(report.trailing_bytes == trailing_expected, "scene trailing bytes are wrong");
    expect(
        report.raw_header_words[0] == 0x40U &&
            report.raw_header_words[1] == 3U &&
            report.raw_header_words[2] == float_bits(2.5F),
        "raw scene header words were not preserved");

    const auto& first = report.entries[0];
    expect(first.directory_entry_offset == 0x40U, "first entry offset is wrong");
    expect(
        first.float_values == std::array<float, 4>{1.0F, -2.0F, 0.0F, 4.0F},
        "first entry floats are wrong");
    expect(first.block_offset == 0xc0U, "first block offset is wrong");
    expect(first.opaque_size == 0xe0U, "first opaque size is wrong");
    expect(first.block_end == 0x1e0U, "first block end is wrong");
    expect(
        first.block_range == openrc::SceneBlockRange{0xc0U, 0x120U} &&
            first.prefix_range == openrc::SceneBlockRange{0xc0U, 0x40U} &&
            first.remainder_range == openrc::SceneBlockRange{0x100U, 0xe0U},
        "first neutral block ranges are wrong");
    expect_section_layout(first, kFirstSectionBoundaries, 0x100U);
    expect(first.block_bytes == first_expected, "first owned block bytes are wrong");
    expect(
        first.raw_words[8] == 0x11223344U &&
            first.raw_words[9] == (0x11223344U ^ 0xa5a5a5a5U) &&
            first.raw_words[13] == openrc::kSceneBlockSectionMarker &&
            first.raw_words[15] == (0x11223344U ^ 0x5a5a5a5aU),
        "first raw entry words were not preserved");

    const auto& second = report.entries[1];
    expect(second.directory_entry_offset == 0x80U, "second entry offset is wrong");
    expect(second.block_offset == 0x1e0U, "second block offset is wrong");
    expect(second.opaque_size == 0x100U, "second opaque size is wrong");
    expect(second.block_end == 0x320U, "second block end is wrong");
    expect(
        second.block_range == openrc::SceneBlockRange{0x1e0U, 0x140U} &&
            second.prefix_range == openrc::SceneBlockRange{0x1e0U, 0x40U} &&
            second.remainder_range == openrc::SceneBlockRange{0x220U, 0x100U},
        "second neutral block ranges are wrong");
    expect_section_layout(second, kSecondSectionBoundaries, 0x220U);
    expect(second.block_bytes == second_expected, "second owned block bytes are wrong");

    const auto& third = report.entries[2];
    expect(
        third.directory_entry_offset == 0xc0U,
        "overlapped entry offset is wrong");
    expect(
        third.float_values == std::array<float, 4>{16.0F, 32.0F, -8.0F, 2.0F},
        "overlapped entry floats are wrong");
    expect(third.block_offset == 0x320U, "third block offset is wrong");
    expect(third.opaque_size == 0x120U, "third opaque size is wrong");
    expect(third.block_end == 0x480U, "third block end is wrong");
    expect(
        third.block_range == openrc::SceneBlockRange{0x320U, 0x160U} &&
            third.prefix_range == openrc::SceneBlockRange{0x320U, 0x40U} &&
            third.remainder_range == openrc::SceneBlockRange{0x360U, 0x120U},
        "third neutral block ranges are wrong");
    expect_section_layout(third, kThirdSectionBoundaries, 0x360U);
    expect(third.block_bytes == third_expected, "third owned block bytes are wrong");
    expect(
        third.raw_words[8] == 0x99aabbccU &&
            third.raw_words[9] == (0x99aabbccU ^ 0xa5a5a5a5U) &&
            third.raw_words[13] == openrc::kSceneBlockSectionMarker &&
            third.raw_words[15] == (0x99aabbccU ^ 0x5a5a5a5aU),
        "overlapped raw entry words were not preserved");
    expect(
        report.overlapped_entry_range == first.prefix_range,
        "the final descriptor does not overlap the first block prefix");

    std::fill(bytes.begin(), bytes.end(), std::byte{0xff});
    expect(
        report.entries[0].block_bytes == first_expected &&
            report.entries[1].block_bytes == second_expected &&
            report.entries[2].block_bytes == third_expected &&
            report.trailing_bytes == trailing_expected,
        "scene directory report borrows byte ranges from its input");
}

void test_empty_trailing_range() {
    auto bytes = valid_directory();
    bytes.resize(0x480U);
    const auto report = openrc::parse_scene_block_directory_v1(
        bytes,
        kGenerousLimits);
    expect(
        report.trailing_range == openrc::SceneBlockRange{0x480U, 0U} &&
            report.trailing_bytes.empty(),
        "an empty scene trailing range is wrong");
    expect(report.owned_byte_count == 0x3c0U, "owned bytes without trailing data are wrong");
}

void test_zero_opaque_size_is_rejected() {
    auto bytes = valid_directory();
    write_le32(bytes, entry_word_offset(0U, 14U), 0U);
    expect_directory_error(
        [&] {
            (void)openrc::parse_scene_block_directory_v1(
                bytes,
                kGenerousLimits);
        },
        "a zero-size scene remainder was accepted");
}

void test_mandatory_limits() {
    const auto bytes = valid_directory();
    expect_directory_error(
        [&] {
            (void)openrc::parse_scene_block_directory_v1(
                bytes,
                openrc::SceneBlockDirectoryLimits{0U, 16U, 2048U});
        },
        "a zero input limit was accepted");
    expect_directory_error(
        [&] {
            (void)openrc::parse_scene_block_directory_v1(
                bytes,
                openrc::SceneBlockDirectoryLimits{2048U, 0U, 2048U});
        },
        "a zero record limit was accepted");
    expect_directory_error(
        [&] {
            (void)openrc::parse_scene_block_directory_v1(
                bytes,
                openrc::SceneBlockDirectoryLimits{2048U, 16U, 0U});
        },
        "a zero owned-byte limit was accepted");
    expect_directory_error(
        [&] {
            (void)openrc::parse_scene_block_directory_v1(
                bytes,
                openrc::SceneBlockDirectoryLimits{0x4bfU, 16U, 2048U});
        },
        "the scene input limit was ignored");
    expect_directory_error(
        [&] {
            (void)openrc::parse_scene_block_directory_v1(
                bytes,
                openrc::SceneBlockDirectoryLimits{2048U, 2U, 2048U});
        },
        "the scene record limit was ignored");
    expect_directory_error(
        [&] {
            (void)openrc::parse_scene_block_directory_v1(
                bytes,
                openrc::SceneBlockDirectoryLimits{2048U, 16U, 0x3ffU});
        },
        "the scene owned-byte limit was ignored");
}

void test_header_rejections() {
    {
        const std::vector<std::byte> bytes(
            openrc::kSceneBlockDirectoryV1Stride - 1U,
            std::byte{0});
        expect_directory_error(
            [&] {
                (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits);
            },
            "a truncated scene header was accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(bytes, 0x00U, 0x30U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a bad scene directory stride was accepted");
    }
    for (const auto count : std::array<std::uint32_t, 2>{0U, 1U}) {
        auto bytes = valid_directory();
        write_le32(bytes, 0x04U, count);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a scene directory without records was accepted");
    }
    for (const auto bits : std::array<std::uint32_t, 4>{
             0U,
             float_bits(-1.0F),
             0x7f800000U,
             0x7fc00000U}) {
        auto bytes = valid_directory();
        write_le32(bytes, 0x08U, bits);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "an invalid scene header float was accepted");
    }
    for (std::size_t word = 3U; word < 16U; ++word) {
        auto bytes = valid_directory();
        write_le32(bytes, word * 4U, 1U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a non-zero scene reserved header word was accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(bytes, 0x04U, 19U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a scene descriptor span extending beyond input was accepted");
    }
}

void test_entry_float_rejections() {
    for (std::size_t component = 0U; component < 4U; ++component) {
        auto bytes = valid_directory();
        write_le32(bytes, entry_word_offset(0U, component), 0x7f800000U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a non-finite scene entry float was accepted");
    }
    for (const auto value : std::array<float, 2>{0.0F, -1.0F}) {
        auto bytes = valid_directory();
        write_le32(bytes, entry_word_offset(0U, 3U), float_bits(value));
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a non-positive fourth scene entry float was accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(bytes, entry_word_offset(2U, 0U), 0x7fc00000U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a non-finite overlapped descriptor float was accepted");
    }
}

void test_section_layout_rejections() {
    {
        auto bytes = valid_directory();
        write_le32(
            bytes,
            entry_word_offset(0U, 5U),
            pack_halfwords(0x10U, kFirstSectionBoundaries[1]));
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a scene section layout with a non-zero start was accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(bytes, entry_word_offset(0U, 13U), 0U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "an invalid scene section marker was accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(
            bytes,
            entry_word_offset(0U, 7U),
            pack_halfwords(
                kFirstSectionBoundaries[1],
                kFirstSectionBoundaries[5]));
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "duplicate scene section boundaries were accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(
            bytes,
            entry_word_offset(0U, 6U),
            pack_halfwords(0x40U, kFirstSectionBoundaries[4]));
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "descending scene section boundaries were accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(
            bytes,
            entry_word_offset(0U, 6U),
            pack_halfwords(kFirstSectionBoundaries[3], 0x81U));
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a misaligned scene section boundary was accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(
            bytes,
            entry_word_offset(0U, 12U),
            pack_halfwords(kFirstSectionBoundaries[6], 0xd0U));
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "disagreeing scene final-boundary copies were accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(
            bytes,
            entry_word_offset(0U, 11U),
            pack_halfwords(1U, kFirstSectionBoundaries[7]));
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a scene final-section size mismatch was accepted");
    }
}

void test_block_chain_rejections() {
    {
        auto bytes = valid_directory();
        write_le32(bytes, entry_word_offset(0U, 4U), 0xc1U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "an unaligned scene block offset was accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(bytes, entry_word_offset(0U, 14U), 0x21U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "an unaligned scene opaque size was accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(bytes, entry_word_offset(0U, 4U), 0xd0U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a wrong first scene block offset was accepted");
    }
    for (const auto offset : std::array<std::uint32_t, 2>{0x1d0U, 0x1f0U}) {
        auto bytes = valid_directory();
        write_le32(bytes, entry_word_offset(1U, 4U), offset);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a gap or overlap in the scene block chain was accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(bytes, entry_word_offset(2U, 4U), 0x330U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a gap before the final scene block was accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(bytes, entry_word_offset(1U, 14U), 0x400U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a scene block extending beyond input was accepted");
    }
    {
        auto bytes = valid_directory();
        write_le32(bytes, entry_word_offset(1U, 14U), 0xfffffff0U);
        expect_directory_error(
            [&] { (void)openrc::parse_scene_block_directory_v1(bytes, kGenerousLimits); },
            "a huge scene block extent was accepted");
    }
}

} // namespace

int main() {
    try {
        test_valid_directory_and_owned_ranges();
        test_empty_trailing_range();
        test_zero_opaque_size_is_rejected();
        test_mandatory_limits();
        test_header_rejections();
        test_entry_float_rejections();
        test_section_layout_rejections();
        test_block_chain_rejections();
        std::cout << "SceneBlockDirectoryV1 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SceneBlockDirectoryV1 tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
