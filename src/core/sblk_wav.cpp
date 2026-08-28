#include "openrc/sblk_wav.hpp"

#include "openrc/ps_adpcm.hpp"

#include "pcm_wav.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw SBlkWavError(message);
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

[[nodiscard]] std::uint32_t resolve_sample_rate(
    const SBlkWavSampleRateV1 sample_rate) {
    switch (sample_rate.policy) {
    case SBlkWavSampleRatePolicyV1::caller_supplied_hz:
        if (sample_rate.caller_supplied_hz == 0U) {
            fail("The caller-supplied SBlk sample rate is zero");
        }
        return sample_rate.caller_supplied_hz;
    case SBlkWavSampleRatePolicyV1::spu_native_48000_diagnostic:
        if (sample_rate.caller_supplied_hz != 0U) {
            fail("The 48 kHz SPU diagnostic policy cannot carry a caller-supplied rate");
        }
        return kSBlkSpuNativeDiagnosticSampleRateHz;
    default:
        fail("The SBlk WAV sample-rate policy is unsupported");
    }
}

void validate_report_envelope(
    const SBlkBundleV3& bundle,
    const SBlkAudioReportV1& audio) {
    if (audio.blocks.size() != audio.block_count) {
        fail("The in-memory SBlk audio block count is inconsistent");
    }
    if (bundle.secondary_bytes.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        audio.secondary_byte_count != bundle.secondary_bytes.size()) {
        fail("The in-memory SBlk secondary byte count is inconsistent");
    }
    if (bundle.secondary_bytes.size() % kPsAdpcmFrameSize != 0U ||
        audio.frame_count !=
            bundle.secondary_bytes.size() / kPsAdpcmFrameSize) {
        fail("The in-memory SBlk secondary frame count is inconsistent");
    }
}

void validate_selected_block(const SBlkAudioBlock& block) {
    if (block.offset % kPsAdpcmFrameSize != 0U || block.size == 0U ||
        block.size % kPsAdpcmFrameSize != 0U ||
        block.frame_count != block.size / kPsAdpcmFrameSize) {
        fail("The selected SBlk block size and frame count are inconsistent");
    }
    if (block.content_begin_frame >= block.content_end_frame ||
        block.content_end_frame > block.frame_count) {
        fail("The selected SBlk block content range is inconsistent");
    }

    switch (block.kind) {
    case SBlkAudioBlockKind::one_shot:
        if (block.content_begin_frame != 1U ||
            block.content_end_frame + 1U != block.frame_count ||
            block.loop_start_frame.has_value() ||
            block.loop_end_frame.has_value() ||
            block.trailing_padding_frame_count != 0U) {
            fail("The selected one-shot SBlk block metadata is inconsistent");
        }
        break;
    case SBlkAudioBlockKind::looped:
        if (block.content_begin_frame != 1U ||
            !block.loop_start_frame.has_value() ||
            !block.loop_end_frame.has_value() ||
            block.loop_start_frame.value() < block.content_begin_frame ||
            block.loop_start_frame.value() >= block.loop_end_frame.value() ||
            block.loop_end_frame.value() + 1U != block.content_end_frame ||
            block.content_end_frame + block.trailing_padding_frame_count !=
                block.frame_count ||
            block.trailing_padding_frame_count > 1U) {
            fail("The selected looped SBlk block metadata is inconsistent");
        }
        break;
    default:
        fail("The selected SBlk block kind is unsupported");
    }
}

} // namespace

SBlkWavExportV1 encode_sblk_physical_block_pcm16_mono_wav_v1(
    const SBlkBundleV3& bundle,
    const SBlkAudioReportV1& audio,
    const std::uint64_t physical_block_index,
    const SBlkWavSampleRateV1 sample_rate,
    const SBlkWavLimitsV1 limits) {
    const auto resolved_sample_rate = resolve_sample_rate(sample_rate);
    validate_report_envelope(bundle, audio);
    if (physical_block_index >= audio.blocks.size()) {
        fail("The requested physical SBlk block index is out of range");
    }

    const auto& block = audio.blocks[static_cast<std::size_t>(physical_block_index)];
    validate_selected_block(block);
    const auto block_end = checked_add(
        block.offset,
        block.size,
        "the selected SBlk block end");
    if (block_end > bundle.secondary_bytes.size()) {
        fail("The selected SBlk block lies outside the secondary bank");
    }

    const auto content_frame_count =
        static_cast<std::uint64_t>(block.content_end_frame) -
        block.content_begin_frame;
    const auto content_adpcm_bytes = checked_multiply(
        content_frame_count,
        kPsAdpcmFrameSize,
        "the selected SBlk content byte count");
    const auto content_sample_count = checked_multiply(
        content_frame_count,
        kPsAdpcmSamplesPerFrame,
        "the selected SBlk content sample count");
    if (content_adpcm_bytes > limits.max_content_adpcm_bytes) {
        fail("The selected SBlk content exceeds the caller's ADPCM byte limit");
    }
    if (content_frame_count > limits.max_content_frames) {
        fail("The selected SBlk content exceeds the caller's frame limit");
    }
    if (content_sample_count > limits.max_content_samples) {
        fail("The selected SBlk content exceeds the caller's sample limit");
    }

    const auto content_relative_offset = checked_multiply(
        block.content_begin_frame,
        kPsAdpcmFrameSize,
        "the selected SBlk content offset");
    const auto content_offset = checked_add(
        block.offset,
        content_relative_offset,
        "the selected SBlk content address");
    const auto content_end = checked_add(
        content_offset,
        content_adpcm_bytes,
        "the selected SBlk content end");
    if (content_end > block_end || content_end > bundle.secondary_bytes.size()) {
        fail("The selected SBlk content lies outside its physical block");
    }

    PsAdpcmDecoded decoded;
    try {
        decoded = decode_ps_adpcm(
            std::span<const std::byte>(bundle.secondary_bytes).subspan(
                static_cast<std::size_t>(content_offset),
                static_cast<std::size_t>(content_adpcm_bytes)),
            PsAdpcmLimits{
                limits.max_content_adpcm_bytes,
                limits.max_content_frames,
                limits.max_content_samples,
            });
    } catch (const PsAdpcmError& error) {
        fail("Invalid selected SBlk ADPCM content: " + std::string(error.what()));
    }

    SBlkWavExportV1 result;
    result.physical_block_index = static_cast<std::uint32_t>(physical_block_index);
    result.block_kind = block.kind;
    result.sample_rate_policy = sample_rate.policy;
    result.sample_rate_hz = resolved_sample_rate;
    result.content_frame_count = content_frame_count;
    result.content_sample_count = content_sample_count;
    if (block.kind == SBlkAudioBlockKind::looped) {
        result.loop_sample_begin = checked_multiply(
            static_cast<std::uint64_t>(block.loop_start_frame.value()) -
                block.content_begin_frame,
            kPsAdpcmSamplesPerFrame,
            "the selected SBlk loop sample start");
        result.loop_sample_end = checked_multiply(
            static_cast<std::uint64_t>(block.loop_end_frame.value()) + 1U -
                block.content_begin_frame,
            kPsAdpcmSamplesPerFrame,
            "the selected SBlk loop sample end");
        if (result.loop_sample_end.value() > content_sample_count) {
            fail("The selected SBlk loop sample range exceeds its content");
        }
    }

    try {
        result.wav_bytes = detail::encode_pcm16_mono_wav(
            decoded.samples,
            resolved_sample_rate,
            limits.max_output_bytes);
    } catch (const detail::PcmWavError& error) {
        fail("Invalid selected SBlk WAV output: " + std::string(error.what()));
    }
    return result;
}

} // namespace openrc
