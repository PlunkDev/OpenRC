#pragma once

#include "openrc/audio_voice_bank.hpp"
#include "openrc/audio_gain_table.hpp"
#include "openrc/rac_frontend_sound_pcm.hpp"

namespace openrc {

struct RacFrontendSoundPreparedBankV1 {
    AudioProgramBankV1 program;
    AudioVoiceBankV1 voices;
    std::vector<AudioStreamV1> streams;
    std::vector<AudioGainTableV1> gains;
};
// Compiler-only lowering of all five scenic programs and thirteen distinct
// input owners, under the admitted default effects/stereo invocation profile.
[[nodiscard]] RacFrontendSoundPreparedBankV1 compile_rac_frontend_ambient_bank_v1(
    const RacFrontendSoundSourceV1&, const RacFrontendSoundBankV1&,
    std::span<const std::byte> sound_irx);

} // namespace openrc
