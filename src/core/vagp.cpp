#include "openrc/vagp.hpp"

#include "pcm_wav.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw VagpError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_be32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(byte_value(bytes[offset])) << 24U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 8U) |
        static_cast<std::uint32_t>(byte_value(bytes[offset + 3U]));
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

[[nodiscard]] std::uint64_t sector_envelope_size(
    const std::uint64_t logical_bytes) {
    const auto rounded = checked_add(
        logical_bytes,
        kVagpSectorSize - 1U,
        "the VAGp sector envelope");
    return checked_multiply(
        rounded / kVagpSectorSize,
        kVagpSectorSize,
        "the VAGp sector envelope");
}

[[nodiscard]] std::string display_name_from(
    const std::array<std::byte, kVagpNameSize>& raw_name) {
    const auto terminator = std::find(
        raw_name.begin(),
        raw_name.end(),
        std::byte{0});
    std::string result;
    result.reserve(static_cast<std::size_t>(
        std::distance(raw_name.begin(), terminator)));
    for (auto iterator = raw_name.begin(); iterator != terminator; ++iterator) {
        result.push_back(static_cast<char>(byte_value(*iterator)));
    }
    return result;
}

void validate_report_for_wav(const VagpReportV1& report) {
    if (report.version != kVagpV1Version) {
        fail("The in-memory VAGp version is inconsistent");
    }
    if (std::any_of(
            report.reserved_words.begin(),
            report.reserved_words.end(),
            [](const std::uint32_t value) { return value != 0U; })) {
        fail("The in-memory VAGp reserved words are inconsistent");
    }
    if (report.sample_rate == 0U) {
        fail("The in-memory VAGp sample rate is zero");
    }
    const auto expected_logical = checked_add(
        kVagpHeaderSize,
        report.payload_bytes,
        "the in-memory VAGp logical size");
    if (report.logical_bytes != expected_logical ||
        report.input_bytes != checked_add(
            report.logical_bytes,
            report.padding_bytes,
            "the in-memory VAGp input size")) {
        fail("The in-memory VAGp byte counts are inconsistent");
    }
    if (report.payload_bytes == 0U ||
        report.payload_bytes % kPsAdpcmFrameSize != 0U) {
        fail("The in-memory VAGp payload size is inconsistent");
    }
    const auto expected_frames =
        static_cast<std::uint64_t>(report.payload_bytes) /
        kPsAdpcmFrameSize;
    const auto expected_samples = checked_multiply(
        expected_frames,
        kPsAdpcmSamplesPerFrame,
        "the in-memory VAGp sample count");
    if (report.frame_count != expected_frames ||
        report.sample_count != expected_samples ||
        report.decoded.input_bytes != report.payload_bytes ||
        report.decoded.frame_count != report.frame_count ||
        report.decoded.sample_count != report.sample_count ||
        report.decoded.frames.size() != report.frame_count ||
        report.decoded.samples.size() != report.sample_count) {
        fail("The in-memory VAGp decoded counts are inconsistent");
    }
    if (report.frame_count < 3U ||
        report.content_begin_frame != 1U ||
        report.content_end_frame != report.frame_count - 1U) {
        fail("The in-memory VAGp content range is inconsistent");
    }

    for (std::size_t index = 0; index < report.decoded.frames.size(); ++index) {
        const auto& frame = report.decoded.frames[index];
        const auto index64 = static_cast<std::uint64_t>(index);
        if (frame.byte_offset != checked_multiply(
                index64,
                kPsAdpcmFrameSize,
                "a VAGp frame byte offset") ||
            frame.first_sample != checked_multiply(
                index64,
                kPsAdpcmSamplesPerFrame,
                "a VAGp frame sample offset") ||
            frame.sample_count != kPsAdpcmSamplesPerFrame) {
            fail("The in-memory VAGp frame metadata is inconsistent");
        }
        const auto is_penultimate = index64 + 2U == report.frame_count;
        const auto is_terminal = index64 + 1U == report.frame_count;
        const auto expected_flags = is_terminal
            ? std::uint8_t{7}
            : (is_penultimate ? std::uint8_t{1} : std::uint8_t{0});
        if (frame.flags != expected_flags) {
            fail("The in-memory VAGp frame grammar is inconsistent");
        }
    }
}

} // namespace

VagpReportV1 parse_vagp_v1(
    const std::span<const std::byte> bytes,
    const VagpLimits limits) {
    const auto input_bytes = static_cast<std::uint64_t>(bytes.size());
    if (input_bytes > limits.max_input_bytes) {
        fail("The VAGp input exceeds the caller's byte limit");
    }
    if (bytes.size() < kVagpHeaderSize) {
        fail("The VAGp input is too small to contain its fixed header");
    }
    for (std::size_t index = 0; index < kVagpMagic.size(); ++index) {
        if (byte_value(bytes[index]) !=
            static_cast<std::uint8_t>(kVagpMagic[index])) {
            fail("The input does not have a VAGp signature");
        }
    }

    VagpReportV1 result;
    result.input_bytes = input_bytes;
    result.version = read_be32(bytes, 0x04U);
    result.payload_bytes = read_be32(bytes, 0x0cU);
    result.sample_rate = read_be32(bytes, 0x10U);
    result.reserved_words = {
        read_be32(bytes, 0x08U),
        read_be32(bytes, 0x14U),
        read_be32(bytes, 0x18U),
        read_be32(bytes, 0x1cU),
    };
    if (result.version != kVagpV1Version) {
        fail("The VAGp input does not use the supported V1 version");
    }
    if (std::any_of(
            result.reserved_words.begin(),
            result.reserved_words.end(),
            [](const std::uint32_t value) { return value != 0U; })) {
        fail("The VAGp reserved header words are non-zero");
    }
    if (result.sample_rate == 0U) {
        fail("The VAGp sample rate must be non-zero");
    }
    if (result.payload_bytes == 0U) {
        fail("The VAGp ADPCM payload is empty");
    }
    if (result.payload_bytes > limits.max_payload_bytes) {
        fail("The VAGp payload exceeds the caller's byte limit");
    }
    if (result.payload_bytes % kPsAdpcmFrameSize != 0U) {
        fail("The VAGp payload ends in a partial PS ADPCM frame");
    }

    result.logical_bytes = checked_add(
        kVagpHeaderSize,
        result.payload_bytes,
        "the VAGp logical size");
    if (input_bytes < result.logical_bytes) {
        fail("The VAGp input ends before its declared ADPCM payload");
    }
    if (input_bytes != result.logical_bytes) {
        if (input_bytes != sector_envelope_size(result.logical_bytes)) {
            fail("The VAGp input is neither exact logical data nor its minimal sector envelope");
        }
        const auto logical_size = static_cast<std::size_t>(result.logical_bytes);
        const auto padding = bytes.subspan(logical_size);
        if (std::any_of(
                padding.begin(),
                padding.end(),
                [](const std::byte value) { return value != std::byte{0}; })) {
            fail("The VAGp sector padding contains non-zero bytes");
        }
    }
    result.padding_bytes = input_bytes - result.logical_bytes;

    std::copy_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(0x20U),
        result.raw_name.size(),
        result.raw_name.begin());
    result.display_name = display_name_from(result.raw_name);

    result.frame_count =
        static_cast<std::uint64_t>(result.payload_bytes) /
        kPsAdpcmFrameSize;
    if (result.frame_count > limits.max_frames) {
        fail("The VAGp frame count exceeds the caller's limit");
    }
    result.sample_count = checked_multiply(
        result.frame_count,
        kPsAdpcmSamplesPerFrame,
        "the VAGp sample count");
    if (result.sample_count > limits.max_samples) {
        fail("The VAGp sample count exceeds the caller's limit");
    }
    if (result.frame_count < 3U) {
        fail("The VAGp payload is too short for its lead-in and terminal frames");
    }

    const auto payload = bytes.subspan(
        kVagpHeaderSize,
        static_cast<std::size_t>(result.payload_bytes));
    if (!std::all_of(
            payload.begin(),
            payload.begin() + static_cast<std::ptrdiff_t>(kPsAdpcmFrameSize),
            [](const std::byte value) { return value == std::byte{0}; })) {
        fail("The VAGp payload does not begin with a full zero frame");
    }
    for (std::uint64_t index = 1U; index + 2U < result.frame_count; ++index) {
        const auto offset = static_cast<std::size_t>(
            index * kPsAdpcmFrameSize);
        if (byte_value(payload[offset + 1U]) != 0U) {
            fail("A VAGp content frame uses a non-zero control flag");
        }
    }
    const auto penultimate_offset = static_cast<std::size_t>(
        (result.frame_count - 2U) * kPsAdpcmFrameSize);
    const auto terminal_offset = static_cast<std::size_t>(
        (result.frame_count - 1U) * kPsAdpcmFrameSize);
    if (byte_value(payload[penultimate_offset + 1U]) != 1U ||
        byte_value(payload[terminal_offset + 1U]) != 7U) {
        fail("The VAGp payload does not end with the required flag-1/flag-7 frames");
    }

    try {
        result.decoded = decode_ps_adpcm(
            payload,
            PsAdpcmLimits{
                result.payload_bytes,
                result.frame_count,
                result.sample_count,
            });
    } catch (const PsAdpcmError& error) {
        fail("Invalid VAGp PS ADPCM payload: " + std::string(error.what()));
    }
    result.content_begin_frame = 1U;
    result.content_end_frame = result.frame_count - 1U;
    return result;
}

std::vector<std::byte> encode_vagp_pcm16_mono_wav(
    const VagpReportV1& report,
    const std::uint64_t max_output_bytes) {
    if (report.content_end_frame < report.content_begin_frame) {
        fail("The in-memory VAGp content frame range is reversed");
    }
    const auto content_frame_count =
        report.content_end_frame - report.content_begin_frame;
    const auto content_sample_count = checked_multiply(
        content_frame_count,
        kPsAdpcmSamplesPerFrame,
        "the VAGp WAV content sample count");
    const auto data_bytes = checked_multiply(
        content_sample_count,
        sizeof(std::int16_t),
        "the VAGp WAV data size");
    constexpr std::uint64_t kRiffOverhead = 36U;
    if (data_bytes >
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) -
            kRiffOverhead) {
        fail("The VAGp PCM data cannot be represented by classic RIFF/WAVE");
    }

    validate_report_for_wav(report);
    const auto content_sample_begin = checked_multiply(
        report.content_begin_frame,
        kPsAdpcmSamplesPerFrame,
        "the VAGp WAV first sample");
    const auto content_sample_end = checked_add(
        content_sample_begin,
        content_sample_count,
        "the VAGp WAV sample range");
    if (content_sample_end > report.decoded.samples.size()) {
        fail("The in-memory VAGp content samples exceed the decoded PCM");
    }
    const auto first_sample = static_cast<std::size_t>(content_sample_begin);
    const auto content_samples = static_cast<std::size_t>(content_sample_count);
    try {
        return detail::encode_pcm16_mono_wav(
            std::span<const std::int16_t>(report.decoded.samples).subspan(
                first_sample,
                content_samples),
            report.sample_rate,
            max_output_bytes);
    } catch (const detail::PcmWavError& error) {
        fail("Invalid VAGp WAV output: " + std::string(error.what()));
    }
}

} // namespace openrc
