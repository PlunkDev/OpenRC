#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::size_t kPsAdpcmFrameSize = 16;
inline constexpr std::uint32_t kPsAdpcmSamplesPerFrame = 28;

struct PsAdpcmLimits {
    std::uint64_t max_input_bytes = 0;
    std::uint64_t max_frames = 0;
    std::uint64_t max_samples = 0;
};

struct PsAdpcmFrameMetadata {
    std::uint64_t byte_offset = 0;
    std::uint8_t predictor = 0;
    std::uint8_t shift = 0;
    std::uint8_t flags = 0;
    std::uint64_t first_sample = 0;
    std::uint32_t sample_count = 0;

    [[nodiscard]] bool operator==(
        const PsAdpcmFrameMetadata&) const = default;
};

struct PsAdpcmDecoded {
    std::uint64_t input_bytes = 0;
    std::uint64_t frame_count = 0;
    std::uint64_t sample_count = 0;
    std::vector<std::int16_t> samples;
    std::vector<PsAdpcmFrameMetadata> frames;
};

class PsAdpcmError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Decodes every complete PlayStation ADPCM frame linearly. Flag values 0-7
// are preserved solely as metadata: they never stop, loop, silence, or
// otherwise alter low-level sample decoding. The returned report owns every
// sample and metadata record.
[[nodiscard]] PsAdpcmDecoded decode_ps_adpcm(
    std::span<const std::byte> bytes,
    PsAdpcmLimits limits);

} // namespace openrc
