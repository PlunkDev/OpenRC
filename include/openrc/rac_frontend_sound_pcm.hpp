#pragma once

#include "openrc/rac_frontend_sound_compile.hpp"
#include "openrc/audio_clip.hpp"
#include "openrc/audio_stream.hpp"

namespace openrc {

using RacFrontendSoundInterpolationV1 =
    std::array<std::array<std::int16_t, 4>, 256>;

// Reconstructs the qualified numeric interpolation kernel and verifies every
// coefficient through its canonical digest. This is compiler-side DSP only.
[[nodiscard]] RacFrontendSoundInterpolationV1
make_rac_frontend_sound_interpolation_v1();

// Renders one admitted fixed menu grain to ordinary interleaved stereo PCM.
// Includes source predictor rounding, rational phase, four-tap interpolation,
// selected envelope, channel gains and source one-shot end/prefetch semantics.
// Mixing multiple voices and the neutral device/bank lifecycle remain host work.
[[nodiscard]] AudioClipV1 compile_rac_frontend_sound_pcm_v1(
    const RacFrontendSoundBankV1& bank,
    const RacFrontendSoundVoicePlanV1& plan);

struct RacFrontendSoundLoopPcmV1 {
    std::vector<std::int16_t> samples;
    std::uint32_t repeat_begin = 0;
    std::uint32_t repeat_end = 0;
    std::uint32_t source_loops_before_repeat = 0;
    std::uint32_t source_loops_per_repeat = 0;
    std::array<std::int32_t,2> repeat_entry_history{};
};

// Keeps complete source input history until an exact predictor-state cycle is
// proven. The resulting mono input prefix/loop is independent of output phase;
// playback must preserve its continuous resampling phase across each repeat.
[[nodiscard]] RacFrontendSoundLoopPcmV1 compile_rac_frontend_sound_loop_pcm_v1(
    const RacFrontendSoundBankV1& bank, std::uint32_t block_index);

// Neutral input stream only. The consuming source compiler must separately
// provide qualified phase/envelope/channel controls and finite retirement.
[[nodiscard]] AudioStreamV1 compile_rac_frontend_sound_stream_v1(
    const RacFrontendSoundBankV1& bank, std::uint32_t block_index);

} // namespace openrc
