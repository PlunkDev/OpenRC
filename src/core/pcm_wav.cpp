#include "pcm_wav.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace openrc::detail {
namespace {

constexpr std::uint64_t kWavHeaderSize = 44U;
constexpr std::uint64_t kRiffOverhead = 36U;

[[noreturn]] void fail(const std::string& message) {
    throw PcmWavError(message);
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
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left * right;
}

void write_fourcc(
    const std::span<std::byte> output,
    const std::size_t offset,
    const std::array<char, 4>& value) noexcept {
    for (std::size_t index = 0; index < value.size(); ++index) {
        output[offset + index] = static_cast<std::byte>(
            static_cast<unsigned char>(value[index]));
    }
}

void write_le16(
    const std::span<std::byte> output,
    const std::size_t offset,
    const std::uint16_t value) noexcept {
    output[offset] = static_cast<std::byte>(value & 0xffU);
    output[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_le32(
    const std::span<std::byte> output,
    const std::size_t offset,
    const std::uint32_t value) noexcept {
    output[offset] = static_cast<std::byte>(value & 0xffU);
    output[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    output[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    output[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

} // namespace

std::vector<std::byte> encode_pcm16_mono_wav(
    const std::span<const std::int16_t> samples,
    const std::uint32_t sample_rate_hz,
    const std::uint64_t max_output_bytes) {
    if (sample_rate_hz == 0U) {
        fail("The PCM WAV sample rate is zero");
    }
    if (sample_rate_hz >
        std::numeric_limits<std::uint32_t>::max() / sizeof(std::int16_t)) {
        fail("The PCM WAV byte rate cannot be represented");
    }

    const auto sample_count = static_cast<std::uint64_t>(samples.size());
    const auto data_bytes = checked_multiply(
        sample_count,
        sizeof(std::int16_t),
        "the PCM WAV data size");
    if (data_bytes >
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) -
            kRiffOverhead) {
        fail("The PCM data cannot be represented by classic RIFF/WAVE");
    }

    const auto output_bytes = checked_add(
        kWavHeaderSize,
        data_bytes,
        "the PCM WAV output size");
    if (output_bytes > max_output_bytes) {
        fail("The PCM WAV output exceeds the caller's byte limit");
    }
    if (output_bytes > std::numeric_limits<std::size_t>::max() ||
        output_bytes > std::vector<std::byte>{}.max_size()) {
        fail("The PCM WAV output exceeds the host container limit");
    }

    std::vector<std::byte> output(
        static_cast<std::size_t>(output_bytes),
        std::byte{0});
    const auto bytes = std::span<std::byte>(output);
    write_fourcc(bytes, 0x00U, {'R', 'I', 'F', 'F'});
    write_le32(
        bytes,
        0x04U,
        static_cast<std::uint32_t>(kRiffOverhead + data_bytes));
    write_fourcc(bytes, 0x08U, {'W', 'A', 'V', 'E'});
    write_fourcc(bytes, 0x0cU, {'f', 'm', 't', ' '});
    write_le32(bytes, 0x10U, 16U);
    write_le16(bytes, 0x14U, 1U); // PCM
    write_le16(bytes, 0x16U, 1U); // mono
    write_le32(bytes, 0x18U, sample_rate_hz);
    write_le32(
        bytes,
        0x1cU,
        sample_rate_hz * static_cast<std::uint32_t>(sizeof(std::int16_t)));
    write_le16(bytes, 0x20U, static_cast<std::uint16_t>(sizeof(std::int16_t)));
    write_le16(bytes, 0x22U, 16U);
    write_fourcc(bytes, 0x24U, {'d', 'a', 't', 'a'});
    write_le32(bytes, 0x28U, static_cast<std::uint32_t>(data_bytes));

    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto bits = static_cast<std::uint16_t>(samples[index]);
        const auto offset = static_cast<std::size_t>(kWavHeaderSize) +
            index * sizeof(std::int16_t);
        output[offset] = static_cast<std::byte>(bits & 0xffU);
        output[offset + 1U] = static_cast<std::byte>((bits >> 8U) & 0xffU);
    }
    return output;
}

} // namespace openrc::detail
