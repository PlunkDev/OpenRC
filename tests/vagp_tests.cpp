#include "openrc/vagp.hpp"

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

constexpr openrc::VagpLimits kGenerousLimits{
    4096U,
    4096U,
    256U,
    256U * openrc::kPsAdpcmSamplesPerFrame,
};

void expect(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void expect_vagp_error(Function&& function, const char* message) {
    try {
        std::invoke(std::forward<Function>(function));
    } catch (const openrc::VagpError&) {
        return;
    }
    throw std::runtime_error(message);
}

void write_be32(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>((value >> 24U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>(value & 0xffU);
}

[[nodiscard]] std::uint16_t read_le16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(bytes[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) {
    return static_cast<std::uint32_t>(
        std::to_integer<std::uint8_t>(bytes[offset])) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::vector<std::byte> valid_vagp() {
    constexpr std::size_t kFrameCount = 4U;
    constexpr std::size_t kPayloadBytes =
        kFrameCount * openrc::kPsAdpcmFrameSize;
    std::vector<std::byte> bytes(
        openrc::kVagpHeaderSize + kPayloadBytes,
        std::byte{0});
    std::copy_n("VAGp", 4U, reinterpret_cast<char*>(bytes.data()));
    write_be32(bytes, 0x04U, openrc::kVagpV1Version);
    write_be32(bytes, 0x0cU, static_cast<std::uint32_t>(kPayloadBytes));
    write_be32(bytes, 0x10U, 44056U);
    constexpr std::array<char, openrc::kVagpNameSize> kName{
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
        'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    };
    for (std::size_t index = 0; index < kName.size(); ++index) {
        bytes[0x20U + index] = static_cast<std::byte>(kName[index]);
    }

    const auto content_offset = openrc::kVagpHeaderSize +
        openrc::kPsAdpcmFrameSize;
    bytes[content_offset] = std::byte{0x0c};
    bytes[content_offset + 2U] = std::byte{0x1f};
    const auto flag_one_offset = content_offset + openrc::kPsAdpcmFrameSize;
    bytes[flag_one_offset] = std::byte{0x0c};
    bytes[flag_one_offset + 1U] = std::byte{1};
    bytes[flag_one_offset + 2U] = std::byte{0x21};
    const auto terminal_offset = flag_one_offset + openrc::kPsAdpcmFrameSize;
    bytes[terminal_offset + 1U] = std::byte{7};
    return bytes;
}

void test_exact_logical_parse_and_owned_report() {
    auto bytes = valid_vagp();
    const auto report = openrc::parse_vagp_v1(bytes, kGenerousLimits);
    expect(report.input_bytes == bytes.size(), "VAGp input size is wrong");
    expect(report.logical_bytes == bytes.size(), "VAGp logical size is wrong");
    expect(report.padding_bytes == 0U, "exact VAGp unexpectedly has padding");
    expect(report.version == openrc::kVagpV1Version, "VAGp version is wrong");
    expect(report.payload_bytes == 64U, "VAGp payload size is wrong");
    expect(report.sample_rate == 44056U, "VAGp sample rate is wrong");
    expect(
        report.reserved_words == std::array<std::uint32_t, 4>{},
        "VAGp reserved fields are wrong");
    expect(
        report.display_name == "ABCDEFGHIJKLMNOP",
        "non-NUL-terminated VAGp display name is wrong");
    expect(
        report.raw_name.front() == std::byte{'A'} &&
            report.raw_name.back() == std::byte{'P'},
        "raw VAGp name was not preserved");
    expect(report.frame_count == 4U, "VAGp frame count is wrong");
    expect(report.sample_count == 112U, "VAGp sample count is wrong");
    expect(
        report.content_begin_frame == 1U &&
            report.content_end_frame == 3U,
        "VAGp content frame range is wrong");
    expect(
        report.decoded.frames.size() == 4U &&
            report.decoded.samples.size() == 112U,
        "VAGp decoded data has the wrong size");
    expect(
        report.decoded.frames[0].flags == 0U &&
            report.decoded.frames[1].flags == 0U &&
            report.decoded.frames[2].flags == 1U &&
            report.decoded.frames[3].flags == 7U,
        "VAGp decoded flags are wrong");
    expect(
        report.decoded.samples[28U] == -1 &&
            report.decoded.samples[29U] == 1,
        "VAGp content PCM is wrong");

    std::fill(bytes.begin(), bytes.end(), std::byte{0xff});
    expect(
        report.display_name == "ABCDEFGHIJKLMNOP" &&
            report.raw_name.front() == std::byte{'A'} &&
            report.decoded.samples[28U] == -1,
        "VAGp report borrows from its input");
}

void test_name_terminator_and_sector_padding() {
    auto exact = valid_vagp();
    exact[0x23U] = std::byte{0};
    exact[0x24U] = std::byte{'X'};
    const auto named = openrc::parse_vagp_v1(exact, kGenerousLimits);
    expect(named.display_name == "ABC", "VAGp display name did not stop at NUL");
    expect(named.raw_name[4] == std::byte{'X'}, "raw name tail was not preserved");

    auto padded = exact;
    padded.resize(openrc::kVagpSectorSize, std::byte{0});
    const auto report = openrc::parse_vagp_v1(padded, kGenerousLimits);
    expect(report.input_bytes == openrc::kVagpSectorSize, "padded input size is wrong");
    expect(report.logical_bytes == exact.size(), "padded logical size is wrong");
    expect(
        report.padding_bytes == openrc::kVagpSectorSize - exact.size(),
        "VAGp padding size is wrong");

    padded.back() = std::byte{1};
    expect_vagp_error(
        [&] { (void)openrc::parse_vagp_v1(padded, kGenerousLimits); },
        "non-zero VAGp padding was accepted");

    auto partial_padding = exact;
    partial_padding.push_back(std::byte{0});
    expect_vagp_error(
        [&] { (void)openrc::parse_vagp_v1(partial_padding, kGenerousLimits); },
        "non-sector VAGp padding was accepted");

    auto extra_sector = exact;
    extra_sector.resize(openrc::kVagpSectorSize * 2U, std::byte{0});
    expect_vagp_error(
        [&] { (void)openrc::parse_vagp_v1(extra_sector, kGenerousLimits); },
        "an extra VAGp storage sector was accepted");
}

void test_header_and_size_rejections() {
    {
        auto bytes = valid_vagp();
        bytes[0] = std::byte{'B'};
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "bad VAGp magic was accepted");
    }
    {
        auto bytes = valid_vagp();
        write_be32(bytes, 0x04U, openrc::kVagpV1Version + 1U);
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "bad VAGp version was accepted");
    }
    for (const auto offset : std::array<std::size_t, 4>{0x08U, 0x14U, 0x18U, 0x1cU}) {
        auto bytes = valid_vagp();
        write_be32(bytes, offset, 1U);
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "a non-zero VAGp reserved word was accepted");
    }
    {
        auto bytes = valid_vagp();
        write_be32(bytes, 0x10U, 0U);
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "a zero VAGp sample rate was accepted");
    }
    {
        auto bytes = valid_vagp();
        write_be32(bytes, 0x0cU, 0U);
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "an empty VAGp payload was accepted");
    }
    {
        auto bytes = valid_vagp();
        write_be32(bytes, 0x0cU, 80U);
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "a truncated VAGp payload was accepted");
    }
    {
        auto bytes = valid_vagp();
        write_be32(bytes, 0x0cU, 48U);
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "bytes outside the VAGp logical size were accepted");
    }
    {
        auto bytes = valid_vagp();
        bytes.resize(openrc::kVagpHeaderSize + 63U);
        write_be32(bytes, 0x0cU, 63U);
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "a partial VAGp ADPCM frame was accepted");
    }
    {
        const std::vector<std::byte> short_header(openrc::kVagpHeaderSize - 1U);
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(short_header, kGenerousLimits); },
            "a truncated VAGp header was accepted");
    }
}

void test_frame_grammar_and_adpcm_rejections() {
    const auto content_offset = openrc::kVagpHeaderSize +
        openrc::kPsAdpcmFrameSize;
    {
        auto bytes = valid_vagp();
        bytes[openrc::kVagpHeaderSize + 2U] = std::byte{1};
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "a non-zero VAGp lead-in frame was accepted");
    }
    {
        auto bytes = valid_vagp();
        bytes[content_offset + 1U] = std::byte{2};
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "a flagged VAGp content frame was accepted");
    }
    {
        auto bytes = valid_vagp();
        bytes[content_offset + openrc::kPsAdpcmFrameSize + 1U] = std::byte{0};
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "VAGp without a flag-1 frame was accepted");
    }
    {
        auto bytes = valid_vagp();
        bytes.back() = std::byte{0};
        const auto terminal_offset = content_offset +
            2U * openrc::kPsAdpcmFrameSize;
        bytes[terminal_offset + 1U] = std::byte{0};
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "VAGp without a flag-7 frame was accepted");
    }
    {
        auto bytes = valid_vagp();
        bytes.resize(openrc::kVagpHeaderSize + 2U * openrc::kPsAdpcmFrameSize);
        write_be32(
            bytes,
            0x0cU,
            static_cast<std::uint32_t>(2U * openrc::kPsAdpcmFrameSize));
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "a two-frame VAGp payload was accepted");
    }
    {
        auto bytes = valid_vagp();
        bytes[content_offset] = std::byte{0x50};
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "an unsupported VAGp predictor was accepted");
    }
    {
        auto bytes = valid_vagp();
        bytes[content_offset] = std::byte{0x0d};
        expect_vagp_error(
            [&] { (void)openrc::parse_vagp_v1(bytes, kGenerousLimits); },
            "an unsupported VAGp shift was accepted");
    }
}

void test_parser_limits() {
    const auto bytes = valid_vagp();
    expect_vagp_error(
        [&] {
            (void)openrc::parse_vagp_v1(
                bytes,
                openrc::VagpLimits{bytes.size() - 1U, 64U, 4U, 112U});
        },
        "the VAGp input byte limit was ignored");
    expect_vagp_error(
        [&] {
            (void)openrc::parse_vagp_v1(
                bytes,
                openrc::VagpLimits{bytes.size(), 63U, 4U, 112U});
        },
        "the VAGp payload byte limit was ignored");
    expect_vagp_error(
        [&] {
            (void)openrc::parse_vagp_v1(
                bytes,
                openrc::VagpLimits{bytes.size(), 64U, 3U, 112U});
        },
        "the VAGp frame limit was ignored");
    expect_vagp_error(
        [&] {
            (void)openrc::parse_vagp_v1(
                bytes,
                openrc::VagpLimits{bytes.size(), 64U, 4U, 111U});
        },
        "the VAGp sample limit was ignored");
}

void test_canonical_content_wav() {
    const auto report = openrc::parse_vagp_v1(valid_vagp(), kGenerousLimits);
    const auto wav = openrc::encode_vagp_pcm16_mono_wav(report, 156U);
    expect(wav.size() == 156U, "VAGp WAV size is wrong");
    expect(
        std::equal(wav.begin(), wav.begin() + 4, std::as_bytes(std::span("RIFF", 4U)).begin()),
        "VAGp WAV RIFF signature is wrong");
    expect(read_le32(wav, 0x04U) == 148U, "VAGp WAV RIFF size is wrong");
    expect(
        std::equal(wav.begin() + 8, wav.begin() + 12, std::as_bytes(std::span("WAVE", 4U)).begin()),
        "VAGp WAV WAVE signature is wrong");
    expect(
        std::equal(wav.begin() + 12, wav.begin() + 16, std::as_bytes(std::span("fmt ", 4U)).begin()),
        "VAGp WAV fmt signature is wrong");
    expect(read_le32(wav, 0x10U) == 16U, "VAGp WAV fmt size is wrong");
    expect(read_le16(wav, 0x14U) == 1U, "VAGp WAV format is not PCM");
    expect(read_le16(wav, 0x16U) == 1U, "VAGp WAV is not mono");
    expect(read_le32(wav, 0x18U) == 44056U, "VAGp WAV sample rate is wrong");
    expect(read_le32(wav, 0x1cU) == 88112U, "VAGp WAV byte rate is wrong");
    expect(read_le16(wav, 0x20U) == 2U, "VAGp WAV block alignment is wrong");
    expect(read_le16(wav, 0x22U) == 16U, "VAGp WAV bit depth is wrong");
    expect(
        std::equal(wav.begin() + 36, wav.begin() + 40, std::as_bytes(std::span("data", 4U)).begin()),
        "VAGp WAV data signature is wrong");
    expect(read_le32(wav, 0x28U) == 112U, "VAGp WAV data size is wrong");

    for (std::size_t index = 0; index < 56U; ++index) {
        const auto expected_bits = static_cast<std::uint16_t>(
            report.decoded.samples[28U + index]);
        expect(
            read_le16(wav, 44U + index * 2U) == expected_bits,
            "VAGp WAV PCM does not match the content frame range");
    }
}

void test_wav_rejections() {
    const auto report = openrc::parse_vagp_v1(valid_vagp(), kGenerousLimits);
    expect_vagp_error(
        [&] { (void)openrc::encode_vagp_pcm16_mono_wav(report, 155U); },
        "the VAGp WAV output limit was ignored");

    auto inconsistent = report;
    inconsistent.decoded.samples.pop_back();
    expect_vagp_error(
        [&] {
            (void)openrc::encode_vagp_pcm16_mono_wav(
                inconsistent,
                std::numeric_limits<std::uint64_t>::max());
        },
        "inconsistent decoded VAGp samples were accepted");

    auto high_rate_bytes = valid_vagp();
    write_be32(high_rate_bytes, 0x10U, 0x80000000U);
    const auto high_rate = openrc::parse_vagp_v1(high_rate_bytes, kGenerousLimits);
    expect_vagp_error(
        [&] {
            (void)openrc::encode_vagp_pcm16_mono_wav(
                high_rate,
                std::numeric_limits<std::uint64_t>::max());
        },
        "an overflowing VAGp WAV byte rate was accepted");

    auto riff_overflow = report;
    riff_overflow.content_end_frame =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    try {
        (void)openrc::encode_vagp_pcm16_mono_wav(
            riff_overflow,
            std::numeric_limits<std::uint64_t>::max());
        throw std::runtime_error("a classic RIFF size overflow was accepted");
    } catch (const openrc::VagpError& error) {
        expect(
            std::string(error.what()).find("RIFF/WAVE") != std::string::npos,
            "the RIFF overflow test did not reach the RIFF size guard");
    }
}

} // namespace

int main() {
    try {
        test_exact_logical_parse_and_owned_report();
        test_name_terminator_and_sector_padding();
        test_header_and_size_rejections();
        test_frame_grammar_and_adpcm_rejections();
        test_parser_limits();
        test_canonical_content_wav();
        test_wav_rejections();
        std::cout << "VAGp tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "VAGp tests failed: " << error.what() << '\n';
        return 1;
    }
}
