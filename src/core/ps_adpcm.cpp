#include "openrc/ps_adpcm.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace openrc {
namespace {

struct PredictorCoefficients {
    std::int32_t first = 0;
    std::int32_t second = 0;
};

constexpr std::array<PredictorCoefficients, 5> kPredictorCoefficients{{
    {0, 0},
    {60, 0},
    {115, -52},
    {98, -55},
    {122, -60},
}};

[[noreturn]] void fail(const std::string& message) {
    throw PsAdpcmError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] bool supported_flags(const std::uint8_t flags) noexcept {
    return flags <= 7U;
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

[[nodiscard]] std::int32_t signed_nibble(
    const std::uint8_t nibble) noexcept {
    return nibble < 8U
        ? static_cast<std::int32_t>(nibble)
        : static_cast<std::int32_t>(nibble) - 16;
}

} // namespace

PsAdpcmDecoded decode_ps_adpcm(
    const std::span<const std::byte> bytes,
    const PsAdpcmLimits limits) {
    const auto input_size = static_cast<std::uint64_t>(bytes.size());
    if (input_size > limits.max_input_bytes) {
        fail("The PS ADPCM input exceeds the caller's byte limit");
    }
    if (bytes.empty()) {
        fail("The PS ADPCM input is empty");
    }
    if (bytes.size() % kPsAdpcmFrameSize != 0U) {
        fail("The PS ADPCM input ends in a partial frame");
    }

    const auto frame_count = input_size / kPsAdpcmFrameSize;
    if (frame_count > limits.max_frames) {
        fail("The PS ADPCM frame count exceeds the caller's limit");
    }
    const auto sample_count = checked_multiply(
        frame_count,
        kPsAdpcmSamplesPerFrame,
        "the PS ADPCM sample count");
    if (sample_count > limits.max_samples) {
        fail("The PS ADPCM sample count exceeds the caller's limit");
    }

    for (std::size_t offset = 0;
         offset < bytes.size();
         offset += kPsAdpcmFrameSize) {
        const auto header = byte_value(bytes[offset]);
        const auto predictor = static_cast<std::uint8_t>(header >> 4U);
        const auto shift = static_cast<std::uint8_t>(header & 0x0fU);
        const auto flags = byte_value(bytes[offset + 1U]);
        if (predictor >= kPredictorCoefficients.size()) {
            fail("A PS ADPCM frame uses an unsupported predictor");
        }
        if (shift > 12U) {
            fail("A PS ADPCM frame uses an unsupported shift");
        }
        if (!supported_flags(flags)) {
            fail("A PS ADPCM frame uses unsupported flags");
        }
    }

    PsAdpcmDecoded result;
    result.input_bytes = input_size;
    result.frame_count = frame_count;
    result.sample_count = sample_count;
    if (frame_count > result.frames.max_size()) {
        fail("The PS ADPCM frame metadata exceeds the host container limit");
    }
    if (sample_count > result.samples.max_size()) {
        fail("The decoded PS ADPCM samples exceed the host container limit");
    }
    result.frames.reserve(static_cast<std::size_t>(frame_count));
    result.samples.reserve(static_cast<std::size_t>(sample_count));

    std::int32_t history_first = 0;
    std::int32_t history_second = 0;
    for (std::size_t offset = 0;
         offset < bytes.size();
         offset += kPsAdpcmFrameSize) {
        const auto header = byte_value(bytes[offset]);
        const auto predictor = static_cast<std::uint8_t>(header >> 4U);
        const auto shift = static_cast<std::uint8_t>(header & 0x0fU);
        const auto flags = byte_value(bytes[offset + 1U]);
        result.frames.push_back(PsAdpcmFrameMetadata{
            static_cast<std::uint64_t>(offset),
            predictor,
            shift,
            flags,
            static_cast<std::uint64_t>(result.samples.size()),
            kPsAdpcmSamplesPerFrame,
        });

        const auto coefficients = kPredictorCoefficients[predictor];
        const auto scale =
            static_cast<std::int32_t>(1U << (12U - shift));
        for (std::size_t packed_index = 2U;
             packed_index < kPsAdpcmFrameSize;
             ++packed_index) {
            const auto packed = byte_value(bytes[offset + packed_index]);
            const std::array<std::uint8_t, 2> nibbles{
                static_cast<std::uint8_t>(packed & 0x0fU),
                static_cast<std::uint8_t>(packed >> 4U),
            };
            for (const auto nibble : nibbles) {
                const auto base_sample = signed_nibble(nibble) * scale;
                const auto predicted_numerator =
                    history_first * coefficients.first +
                    history_second * coefficients.second;
                const auto decoded =
                    base_sample + predicted_numerator / 64;
                const auto clamped = std::clamp(
                    decoded,
                    static_cast<std::int32_t>(
                        std::numeric_limits<std::int16_t>::min()),
                    static_cast<std::int32_t>(
                        std::numeric_limits<std::int16_t>::max()));
                const auto sample = static_cast<std::int16_t>(clamped);
                result.samples.push_back(sample);
                history_second = history_first;
                history_first = sample;
            }
        }
    }

    return result;
}

} // namespace openrc
