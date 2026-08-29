#include "openrc/localized_subtitle_bank.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::LocalizedSubtitleBankLimitsV1 kLimits{
    0x1000U,
    32U,
    0x1000U,
};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_le16(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_le32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

[[nodiscard]] std::vector<std::byte> make_bank() {
    std::vector<std::byte> bytes(0xa0U, std::byte{0});
    write_le32(bytes, 0x00U, 0x123U);
    write_le32(bytes, 0x04U, 0x40U);
    write_le32(bytes, 0x08U, openrc::kLocalizedSubtitleHeaderTagV1);
    write_le32(bytes, 0x0cU, openrc::kLocalizedSubtitleHeaderKindV1);
    write_le32(
        bytes, 0x10U, openrc::kLocalizedSubtitleBankHeaderBytesV1);
    write_le32(bytes, 0x14U, 0x30U);

    for (std::size_t entry = 0U; entry < 2U; ++entry) {
        const auto entry_offset = 0x40U + entry * 0x10U;
        write_le16(
            bytes,
            entry_offset,
            static_cast<std::uint16_t>(10U + entry * 11U));
        write_le16(
            bytes,
            entry_offset + 2U,
            static_cast<std::uint16_t>(20U + entry * 10U));
        for (std::size_t language = 0U;
             language < openrc::kLocalizedSubtitleLanguageCountV1;
             ++language) {
            const auto text_index =
                entry * openrc::kLocalizedSubtitleLanguageCountV1 + language;
            const auto relative = static_cast<std::uint16_t>(
                0x30U + text_index * 4U);
            write_le16(
                bytes,
                entry_offset + 4U + language * 2U,
                relative);
            bytes[0x40U + relative] = static_cast<std::byte>(
                static_cast<unsigned char>('A' + text_index));
        }
    }
    write_le32(bytes, 0x60U, 0xffffffffU);
    return bytes;
}

template <typename Mutation>
void expect_rejected(Mutation&& mutation, const std::string& message) {
    auto bytes = make_bank();
    std::invoke(std::forward<Mutation>(mutation), bytes);
    try {
        (void)openrc::parse_localized_subtitle_bank_v1(bytes, kLimits);
    } catch (const openrc::LocalizedSubtitleBankError&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_valid_bank_and_owned_text() {
    auto source = make_bank();
    const auto result = openrc::parse_localized_subtitle_bank_v1(
        source, kLimits);
    expect(
        result.input_bytes == 0xa0U && result.table_offset == 0x40U &&
            result.secondary_offset == 0x30U && result.entries.size() == 2U &&
            result.total_text_bytes == 10U,
        "localized subtitle header or totals are wrong");
    expect(
        result.opaque_body_range ==
                openrc::LocalizedSubtitleRangeV1{0x20U, 0x20U} &&
            result.directory_range ==
                openrc::LocalizedSubtitleRangeV1{0x40U, 0x30U} &&
            result.sentinel_range ==
                openrc::LocalizedSubtitleRangeV1{0x60U, 0x10U} &&
            result.text_range ==
                openrc::LocalizedSubtitleRangeV1{0x70U, 0x30U},
        "localized subtitle ranges are wrong");
    expect(
        result.entries[0U].start_tick == 10U &&
            result.entries[0U].end_tick == 20U &&
            result.entries[1U].start_tick == 21U &&
            result.entries[1U].end_tick == 30U,
        "localized subtitle tick intervals changed");
    for (std::size_t text_index = 0U; text_index < 10U; ++text_index) {
        const auto entry =
            text_index / openrc::kLocalizedSubtitleLanguageCountV1;
        const auto language =
            text_index % openrc::kLocalizedSubtitleLanguageCountV1;
        const auto& text = result.entries[entry].texts[language];
        expect(
            text.range == openrc::LocalizedSubtitleRangeV1{
                              0x70U + text_index * 4U,
                              1U} &&
                text.bytes == std::vector<std::byte>{static_cast<std::byte>(
                                  static_cast<unsigned char>('A' + text_index))},
            "localized subtitle text or absolute range is wrong");
    }
    std::fill(source.begin(), source.end(), std::byte{0});
    expect(
        result.entries[1U].texts[4U].bytes.front() == std::byte{'J'},
        "localized subtitle result borrowed its input text");
}

void test_limits() {
    const auto bytes = make_bank();
    for (const auto limits : {
             openrc::LocalizedSubtitleBankLimitsV1{0U, 32U, 100U},
             openrc::LocalizedSubtitleBankLimitsV1{1000U, 0U, 100U},
             openrc::LocalizedSubtitleBankLimitsV1{1000U, 32U, 0U}}) {
        try {
            (void)openrc::parse_localized_subtitle_bank_v1(bytes, limits);
        } catch (const openrc::LocalizedSubtitleBankError&) {
            continue;
        }
        throw std::runtime_error("a zero localized subtitle limit was accepted");
    }
    for (const auto limits : {
             openrc::LocalizedSubtitleBankLimitsV1{0x9fU, 32U, 100U},
             openrc::LocalizedSubtitleBankLimitsV1{0x1000U, 1U, 100U},
             openrc::LocalizedSubtitleBankLimitsV1{0x1000U, 32U, 9U}}) {
        try {
            (void)openrc::parse_localized_subtitle_bank_v1(bytes, limits);
        } catch (const openrc::LocalizedSubtitleBankError&) {
            continue;
        }
        throw std::runtime_error("a localized subtitle bound was ignored");
    }
}

void test_empty_directory_and_opaque_suffix() {
    auto empty = make_bank();
    empty.resize(0x50U, std::byte{0});
    std::fill(empty.begin() + 0x40, empty.end(), std::byte{0});
    write_le32(empty, 0x40U, 0xffffffffU);
    const auto empty_result = openrc::parse_localized_subtitle_bank_v1(
        empty, kLimits);
    expect(
        empty_result.entries.empty() &&
            empty_result.directory_range ==
                openrc::LocalizedSubtitleRangeV1{0x40U, 0x10U} &&
            empty_result.text_range ==
                openrc::LocalizedSubtitleRangeV1{0x50U, 0U},
        "an empty localized subtitle directory was not preserved");

    auto with_suffix = make_bank();
    with_suffix.resize(0xb0U, std::byte{0});
    with_suffix[0xa0U] = std::byte{0x5a};
    with_suffix[0xafU] = std::byte{0xa5};
    const auto suffix_result = openrc::parse_localized_subtitle_bank_v1(
        with_suffix, kLimits);
    expect(
        suffix_result.text_range ==
                openrc::LocalizedSubtitleRangeV1{0x70U, 0x30U} &&
            suffix_result.trailing_opaque_range ==
                openrc::LocalizedSubtitleRangeV1{0xa0U, 0x10U},
        "localized subtitle opaque suffix was not isolated");

    auto with_64k_tail = make_bank();
    with_64k_tail.resize(0x10040U, std::byte{0});
    with_64k_tail.back() = std::byte{0x5a};
    const auto large_suffix_result =
        openrc::parse_localized_subtitle_bank_v1(
            with_64k_tail,
            openrc::LocalizedSubtitleBankLimitsV1{
                0x20000U, 32U, 0x1000U});
    expect(
        large_suffix_result.text_range ==
                openrc::LocalizedSubtitleRangeV1{0x70U, 0x30U} &&
            large_suffix_result.trailing_opaque_range ==
                openrc::LocalizedSubtitleRangeV1{0xa0U, 0xffa0U},
        "a 64 KiB subtitle envelope with an opaque suffix was rejected");

    auto empty_text = make_bank();
    empty_text[0x70U] = std::byte{0};
    const auto empty_text_result =
        openrc::parse_localized_subtitle_bank_v1(empty_text, kLimits);
    expect(
        empty_text_result.entries[0U].texts[0U].bytes.empty() &&
            empty_text_result.entries[0U].texts[0U].range ==
                openrc::LocalizedSubtitleRangeV1{0x70U, 0U},
        "an intentionally empty localized subtitle was rejected");
}

void test_header_directory_and_text_rejections() {
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, 0x08U, 0U); },
        "a bad localized subtitle tag was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, 0x0cU, 2U); },
        "a bad localized subtitle kind was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, 0x10U, 0x30U); },
        "a bad localized subtitle header size was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, 0x18U, 1U); },
        "a non-zero localized subtitle reserved header was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, 0x04U, 0x44U); },
        "an unaligned localized subtitle table was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, 0x14U, 0x40U); },
        "an invalid localized subtitle secondary offset was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, 0x60U, 0U); },
        "a missing localized subtitle sentinel was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le16(bytes, 0x40U, 30U);
            write_le16(bytes, 0x42U, 20U);
        },
        "a reversed localized subtitle interval was accepted");
    expect_rejected(
        [](auto& bytes) { write_le16(bytes, 0x4eU, 1U); },
        "a non-zero localized subtitle entry reserve was accepted");
    expect_rejected(
        [](auto& bytes) { write_le16(bytes, 0x44U, 0x31U); },
        "an unaligned localized subtitle text offset was accepted");
    expect_rejected(
        [](auto& bytes) { write_le16(bytes, 0x46U, 0x30U); },
        "a repeated localized subtitle text offset was accepted");
    expect_rejected(
        [](auto& bytes) { write_le16(bytes, 0x44U, 0x34U); },
        "a gap after the localized subtitle directory was accepted");
    expect_rejected(
        [](auto& bytes) {
            std::fill(
                bytes.begin() + 0x70,
                bytes.begin() + 0x74,
                std::byte{'X'});
        },
        "localized subtitle text without a NUL was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[0x72U] = std::byte{1}; },
        "non-zero localized subtitle text padding was accepted");
    expect_rejected(
        [](auto& bytes) { bytes.back() = std::byte{1}; },
        "non-zero localized subtitle tail padding was accepted");
}

} // namespace

int main() {
    try {
        test_valid_bank_and_owned_text();
        test_limits();
        test_empty_directory_and_opaque_suffix();
        test_header_directory_and_text_rejections();
        std::cout << "OpenRC LocalizedSubtitleBankV1 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC LocalizedSubtitleBankV1 tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
