#pragma once

#include "openrc/ps_adpcm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

inline constexpr std::array<char, 4> kVagpMagic{'V', 'A', 'G', 'p'};
inline constexpr std::uint32_t kVagpV1Version = 0x20;
inline constexpr std::size_t kVagpHeaderSize = 0x30;
inline constexpr std::uint64_t kVagpSectorSize = 2048;
inline constexpr std::size_t kVagpNameSize = 16;

struct VagpLimits {
    std::uint64_t max_input_bytes = 0;
    std::uint64_t max_payload_bytes = 0;
    std::uint64_t max_frames = 0;
    std::uint64_t max_samples = 0;
};

struct VagpReportV1 {
    std::uint64_t input_bytes = 0;
    std::uint64_t logical_bytes = 0;
    std::uint64_t padding_bytes = 0;
    std::uint32_t version = 0;
    std::uint32_t payload_bytes = 0;
    std::uint32_t sample_rate = 0;
    // The four reserved words occur at header offsets 0x08, 0x14, 0x18,
    // and 0x1c, in that order.
    std::array<std::uint32_t, 4> reserved_words{};
    std::array<std::byte, kVagpNameSize> raw_name{};
    // Owns the bytes before the first NUL, or all 16 bytes when no NUL is
    // present. The VAGp name field is not required to be NUL-terminated.
    std::string display_name;
    std::uint64_t frame_count = 0;
    std::uint64_t sample_count = 0;
    // Frame indices are relative to the ADPCM payload. The playable content
    // is [content_begin_frame, content_end_frame): it excludes the leading
    // zero frame and the final flag-7 frame, while retaining the flag-1 frame.
    std::uint64_t content_begin_frame = 0;
    std::uint64_t content_end_frame = 0;
    PsAdpcmDecoded decoded;
};

class VagpError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses and decodes the strict VAGp V1 profile used by the supported build.
// The input must be either the exact logical file or its minimal 2048-byte
// sector envelope with zero-filled padding. The returned report owns its name,
// decoded PCM samples, and PS ADPCM frame metadata.
[[nodiscard]] VagpReportV1 parse_vagp_v1(
    std::span<const std::byte> bytes,
    VagpLimits limits);

// Encodes only the report's playable content range as a canonical 44-byte
// RIFF/WAVE PCM, mono, signed-16-bit-little-endian stream. The output limit is
// mandatory and includes the WAV header.
[[nodiscard]] std::vector<std::byte> encode_vagp_pcm16_mono_wav(
    const VagpReportV1& report,
    std::uint64_t max_output_bytes);

} // namespace openrc
