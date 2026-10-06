#include "openrc/rac_frontend_sound_pcm.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace openrc {
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw RacFrontendSoundCompileError(message);
}

std::int32_t floor_shift(std::int64_t value, unsigned bits) {
    const auto denominator = std::int64_t{1} << bits;
    return static_cast<std::int32_t>(value >= 0 ? value / denominator :
        -((-value + denominator - 1) / denominator));
}

std::vector<std::int16_t> decode_voice_samples(std::span<const std::byte> bytes,
    std::int32_t& previous, std::int32_t& older) {
    require(!bytes.empty() && bytes.size() % 16U == 0U,
        "Frontend sound PCM input is not a whole source frame owner");
    constexpr std::array<std::array<int, 2>, 5> predictors{{
        {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}}};
    std::vector<std::int16_t> result; result.reserve(bytes.size() / 16U * 28U);
    for (std::size_t frame = 0; frame < bytes.size(); frame += 16U) {
        const auto control = std::to_integer<unsigned>(bytes[frame]);
        const auto shift = control & 15U, predictor = control >> 4U;
        require(shift <= 12U && predictor < predictors.size(),
            "Frontend sound PCM frame predictor/shift differs");
        for (unsigned sample = 0; sample < 28U; ++sample) {
            const auto packed = std::to_integer<unsigned>(bytes[frame + 2U + sample / 2U]);
            const auto nibble = static_cast<int>((packed >> ((sample & 1U) * 4U)) & 15U);
            const auto signed_nibble = nibble < 8 ? nibble : nibble - 16;
            const auto residual = signed_nibble * (1 << (12U - shift));
            // Hardware playback rounds the predictor sum. The separately
            // published low-level decode_ps_adpcm truncation contract is intact.
            const auto prediction = floor_shift(std::int64_t{previous} * predictors[predictor][0] +
                std::int64_t{older} * predictors[predictor][1] + 32, 6U);
            const auto value = std::clamp(residual + prediction, -32768, 32767);
            result.push_back(static_cast<std::int16_t>(value));
            older = previous; previous = value;
        }
    }
    return result;
}

} // namespace

RacFrontendSoundInterpolationV1 make_rac_frontend_sound_interpolation_v1() {
    // Numeric kernel reconstruction; no imported emulator source is compiled.
    // Published reference/generator: PCSX2 SPU2/interpolate_table.h. The
    // resulting 1024 signed coefficients are independently compared by probe.
    std::array<double, 512> shape{};
    double integral = 0.0;
    for (std::size_t i = 0; i < shape.size(); ++i) {
        const double x = 511.5 - static_cast<double>(i);
        const double window = 0.42 + 0.5 * std::cos(2.0 * std::numbers::pi * x / 1023.0) +
            0.08 * std::cos(4.0 * std::numbers::pi * x / 1023.0);
        shape[i] = std::sin(std::numbers::pi * x / 500.0) * window / x;
        integral += shape[i];
    }
    constexpr double phase_sum = 32640.0;
    const auto normalization = phase_sum * 128.0 / integral;
    RacFrontendSoundInterpolationV1 result{};
    std::uint64_t digest = UINT64_C(14695981039346656037);
    for (std::size_t phase = 0; phase < result.size(); ++phase) {
        const std::array<std::size_t, 4> indices{255U - phase, 511U - phase, 256U + phase, phase};
        double sum = 0;
        for (const auto i : indices) sum += shape[i] * normalization;
        const auto correction = (sum - phase_sum) / 4.0;
        for (std::size_t tap = 0; tap < 4; ++tap) {
            const auto coefficient = std::lround(shape[indices[tap]] * normalization - correction);
            require(coefficient >= -32768 && coefficient <= 32767,
                "Frontend sound interpolation coefficient exceeds its source range");
            result[phase][tap] = static_cast<std::int16_t>(coefficient);
            const auto bits = static_cast<std::uint16_t>(result[phase][tap]);
            for (unsigned byte = 0; byte < 2; ++byte) {
                digest ^= (bits >> (8U * byte)) & 255U;
                digest *= UINT64_C(1099511628211);
            }
        }
    }
    require(digest == UINT64_C(0x91b2a2e00b4005dd),
        "Frontend sound reconstructed interpolation table differs from the qualified reference");
    return result;
}

AudioClipV1 compile_rac_frontend_sound_pcm_v1(
    const RacFrontendSoundBankV1& bank, const RacFrontendSoundVoicePlanV1& plan) {
    require(plan.block_index < bank.audio.blocks.size() && plan.phase_numerator == 1880U &&
        plan.phase_denominator == 4096U && plan.output_frames_per_second == 48000U &&
        plan.adsr1 == 0x80ffU && plan.adsr2 == 0x9fc0U,
        "Frontend sound PCM plan differs from the qualified fixed voice");
    const auto& block = bank.audio.blocks[plan.block_index];
    require(block.kind == SBlkAudioBlockKind::one_shot && block.content_begin_frame == 1U &&
        plan.source_sample_count_before_end == block.content_end_frame * 28U &&
        std::uint64_t{block.offset} + block.size <= bank.bank.secondary_bytes.size(),
        "Frontend sound PCM sample ownership or end policy differs");
    std::int32_t previous=0,older=0;
    const auto samples = decode_voice_samples(std::span<const std::byte>(bank.bank.secondary_bytes)
        .subspan(block.offset, block.content_end_frame * 16U),previous,older);
    require(std::all_of(samples.begin(), samples.begin() + 28U,
        [](std::int16_t value) { return value == 0; }),
        "Frontend sound PCM lacks the source leading silence for its selected envelope");
    const auto coefficients = make_rac_frontend_sound_interpolation_v1();
    AudioClipV1 output;
    output.sample_rate = 48000U; output.channels = 2U;
    std::uint32_t consumed = 0, fetched = 0, phase = 0;
    // The source voice fetches four samples whenever no more than twelve are
    // buffered. End-without-loop mutes immediately on the final fetch. This
    // models the finite source grain without a hardware/register runtime.
    while (consumed < samples.size()) {
        if (fetched - consumed <= 12U) {
            fetched += 4U;
            if (fetched >= samples.size()) break;
        }
        require(consumed + 4U <= fetched, "Frontend sound sample fetch underrun");
        const auto& kernel = coefficients[(phase & 0xff0U) >> 4U];
        std::int32_t value = 0;
        for (std::uint32_t tap = 0; tap < 4; ++tap)
            value += floor_shift(std::int64_t{samples[consumed + tap]} * kernel[tap], 15U);
        // Attack completes by tick6 inside the addressed all-zero frame. Keep
        // its exact constant envelope multiply for every audible sample.
        value = floor_shift(std::int64_t{value} * 32767, 15U);
        for (const auto gain : plan.channel_volume_factors) {
            require(gain < 32768U, "Frontend sound channel requires signed sweep semantics");
            const auto channel = floor_shift(std::int64_t{value} * gain, 15U);
            output.samples.push_back(static_cast<std::int16_t>(std::clamp(channel, -32768, 32767)));
        }
        phase += plan.phase_numerator;
        consumed += phase >> 12U; phase &= 4095U;
    }
    require(!output.samples.empty() && output.samples.size() % 2U == 0U,
        "Frontend sound PCM produced no complete stereo frames");
    validate_audio_clip_v1(output);
    return output;
}

RacFrontendSoundLoopPcmV1 compile_rac_frontend_sound_loop_pcm_v1(
    const RacFrontendSoundBankV1& bank,std::uint32_t block_index) {
    require(block_index<bank.audio.blocks.size(),"Frontend sound loop block is absent");
    const auto& block=bank.audio.blocks[block_index];
    require(block.kind==SBlkAudioBlockKind::looped && block.loop_start_frame &&
        block.loop_end_frame && *block.loop_start_frame>0U &&
        *block.loop_start_frame<=*block.loop_end_frame &&
        *block.loop_end_frame<block.frame_count &&
        std::uint64_t{block.offset}+block.size<=bank.bank.secondary_bytes.size(),
        "Frontend sound loop ownership is not qualified");
    const auto start=*block.loop_start_frame,end=*block.loop_end_frame+1U;
    require(std::uint64_t{end}*16U<=block.size && std::uint64_t{end}*28U<=1024U*1024U,
        "Frontend sound loop exceeds bounded source samples");
    const auto bytes=std::span<const std::byte>(bank.bank.secondary_bytes).subspan(block.offset,block.size);
    require(bytes[start*16U+1U]==std::byte{6U} && bytes[(end-1U)*16U+1U]==std::byte{3U},
        "Frontend sound loop start/end frame flags differ");
    std::int32_t previous=0,older=0;
    RacFrontendSoundLoopPcmV1 result;
    result.samples=decode_voice_samples(bytes.first(start*16U),previous,older);
    const auto source_loop=bytes.subspan(start*16U,(end-start)*16U);
    std::vector<std::array<std::int32_t,2>> entry_states;
    const auto prefix_samples=result.samples.size();
    const auto loop_samples=(end-start)*28U;
    for(std::uint32_t iteration=0;iteration<=64U;++iteration) {
        const std::array<std::int32_t,2> state{previous,older};
        const auto found=std::find(entry_states.begin(),entry_states.end(),state);
        if(found!=entry_states.end()) {
            const auto cycle_begin=static_cast<std::uint32_t>(found-entry_states.begin());
            result.repeat_begin=static_cast<std::uint32_t>(prefix_samples+std::uint64_t{cycle_begin}*loop_samples);
            result.repeat_end=static_cast<std::uint32_t>(result.samples.size());
            result.source_loops_before_repeat=cycle_begin;
            result.source_loops_per_repeat=iteration-cycle_begin;
            result.repeat_entry_history=state;
            require(result.repeat_begin<result.repeat_end,"Frontend sound loop cycle is empty");
            return result;
        }
        require(iteration<64U && result.samples.size()+loop_samples<=8U*1024U*1024U,
            "Frontend sound input history has no bounded exact repeat");
        entry_states.push_back(state);
        const auto decoded=decode_voice_samples(source_loop,previous,older);
        result.samples.insert(result.samples.end(),decoded.begin(),decoded.end());
    }
    throw RacFrontendSoundCompileError("Frontend sound input history cycle is absent");
}

AudioStreamV1 compile_rac_frontend_sound_stream_v1(
    const RacFrontendSoundBankV1& bank,std::uint32_t block_index) {
    require(block_index<bank.audio.blocks.size(),"Frontend sound stream block is absent");
    const auto& block=bank.audio.blocks[block_index];
    AudioStreamV1 result;
    result.output_sample_rate=48000U;
    result.phase_denominator=4096U;
    result.coefficient_denominator=32768U;
    result.gain_denominator=32768U;
    result.coefficients=make_rac_frontend_sound_interpolation_v1();
    if(block.kind==SBlkAudioBlockKind::looped) {
        auto loop=compile_rac_frontend_sound_loop_pcm_v1(bank,block_index);
        result.input_samples=std::move(loop.samples);
        result.repeat_begin=loop.repeat_begin;result.repeat_end=loop.repeat_end;
    } else {
        require(block.content_begin_frame==1U && block.content_end_frame>1U &&
            std::uint64_t{block.offset}+block.size<=bank.bank.secondary_bytes.size() &&
            std::uint64_t{block.content_end_frame}*16U<=block.size,
            "Frontend sound finite stream owner differs");
        std::int32_t previous=0,older=0;
        result.input_samples=decode_voice_samples(std::span<const std::byte>(bank.bank.secondary_bytes)
            .subspan(block.offset,block.content_end_frame*16U),previous,older);
    }
    validate_audio_stream_v1(result);
    return result;
}

} // namespace openrc
