#pragma once

#include "openrc/sblk_audio.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kSBlkSpuNativeDiagnosticSampleRateHz = 48'000U;

enum class SBlkWavSampleRatePolicyV1 {
    caller_supplied_hz,
    spu_native_48000_diagnostic,
};

struct SBlkWavSampleRateV1 {
    SBlkWavSampleRatePolicyV1 policy =
        SBlkWavSampleRatePolicyV1::caller_supplied_hz;
    // Required and non-zero only for caller_supplied_hz. The diagnostic policy
    // always resolves to kSBlkSpuNativeDiagnosticSampleRateHz.
    std::uint32_t caller_supplied_hz = 0;
};

struct SBlkWavLimitsV1 {
    std::uint64_t max_content_adpcm_bytes = 0;
    std::uint64_t max_content_frames = 0;
    std::uint64_t max_content_samples = 0;
    // Includes the canonical 44-byte RIFF/WAVE header.
    std::uint64_t max_output_bytes = 0;
};

struct SBlkWavExportV1 {
    std::uint32_t physical_block_index = 0;
    SBlkAudioBlockKind block_kind = SBlkAudioBlockKind::one_shot;
    SBlkWavSampleRatePolicyV1 sample_rate_policy =
        SBlkWavSampleRatePolicyV1::caller_supplied_hz;
    std::uint32_t sample_rate_hz = 0;
    std::uint64_t content_frame_count = 0;
    std::uint64_t content_sample_count = 0;
    // Relative to the first exported sample. When present, the loop interval is
    // [loop_sample_begin, loop_sample_end).
    std::optional<std::uint64_t> loop_sample_begin;
    std::optional<std::uint64_t> loop_sample_end;
    std::vector<std::byte> wav_bytes;
};

class SBlkWavError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Encodes only the selected physical block's declared content-frame range.
// `audio` must be the report produced for `bundle` by analyze_sblk_audio_v1.
// No file is written and no sample rate is inferred from another asset.
[[nodiscard]] SBlkWavExportV1 encode_sblk_physical_block_pcm16_mono_wav_v1(
    const SBlkBundleV3& bundle,
    const SBlkAudioReportV1& audio,
    std::uint64_t physical_block_index,
    SBlkWavSampleRateV1 sample_rate,
    SBlkWavLimitsV1 limits);

} // namespace openrc
