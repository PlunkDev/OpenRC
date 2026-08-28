#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc::detail {

class PcmWavError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Encodes a canonical 44-byte RIFF/WAVE header followed by mono PCM16LE data.
// The sample rate is caller-owned metadata and must be non-zero. The output
// limit includes the header.
[[nodiscard]] std::vector<std::byte> encode_pcm16_mono_wav(
    std::span<const std::int16_t> samples,
    std::uint32_t sample_rate_hz,
    std::uint64_t max_output_bytes);

} // namespace openrc::detail
