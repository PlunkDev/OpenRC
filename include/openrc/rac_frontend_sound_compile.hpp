#pragma once

#include "openrc/sblk_audio.hpp"
#include "openrc/audio_program.hpp"
#include "openrc/audio_program_cues.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace openrc {

class RacFrontendSoundCompileError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Compiler-side source ownership. Neither this type nor the source records is
// a neutral runtime resource. Playback PCM lowering is a separate operation.
struct RacFrontendSoundBankV1 {
    SBlkBundleV3 bank;
    SBlkAudioReportV1 audio;
    std::uint32_t source_allocation_hint = 0;
};

struct RacFrontendSoundSourceV1 {
    std::array<std::uint32_t, 5> menu_descriptors{};
    std::array<std::uint16_t, 12> pitch_semitones{};
    std::array<std::uint16_t, 128> pitch_fine{};
    std::array<std::int16_t, 2> stereo_center{};
    std::uint32_t default_effects_volume = 0;
    bool default_stereo = false;
};

struct RacFrontendSoundVoicePlanV1 {
    std::uint32_t variant = 0;
    std::uint32_t descriptor_index = 0;
    std::uint32_t block_index = 0;
    // Input phase increment per output frame, not intrinsic sample-rate data.
    std::uint32_t phase_numerator = 0;
    std::uint32_t phase_denominator = 4096;
    std::uint32_t output_frames_per_second = 48000;
    std::uint16_t adsr1 = 0;
    std::uint16_t adsr2 = 0;
    std::array<std::uint16_t, 2> channel_volume_registers{};
    std::array<std::uint16_t, 2> channel_volume_factors{};
    std::uint32_t group_volume = 0;
    std::uint32_t source_sample_count_before_end = 0;
    // Selected envelope becomes constant during the addressed zero frame.
    std::uint32_t attack_output_ticks = 6;
    std::uint16_t sustained_envelope = 32767;
    std::uint16_t envelope_denominator = 32768;
    std::uint32_t release_output_ticks = 2;
};

[[nodiscard]] RacFrontendSoundBankV1 decode_rac_frontend_sound_bank_v1(
    std::span<const std::byte> bytes);

// Qualifies the original EE definitions, 989snd control source and libsd
// conversion/tables for the supported SCES-50916 revision.
[[nodiscard]] RacFrontendSoundSourceV1 make_rac_frontend_sound_source_v1(
    std::span<const std::byte> ee_elf,
    std::span<const std::byte> sound_irx,
    std::span<const std::byte> libsd_irx);

[[nodiscard]] RacFrontendSoundVoicePlanV1 compile_rac_frontend_sound_voice_plan_v1(
    const RacFrontendSoundSourceV1& source,
    const RacFrontendSoundBankV1& bank,
    std::uint32_t variant,
    std::uint32_t effects_volume,
    bool stereo);

struct RacFrontendSoundProgramVoiceV1 {
    std::uint32_t program_key = 0, voice_index = 0;
    std::uint32_t source_grain = 0, block_index = 0;
    // Retained only on the compiler side until tone control/envelope lowering.
    SBlkItemV1 source_tone;
};
struct RacFrontendSoundProgramsV1 {
    AudioProgramBankV1 control;
    std::vector<RacFrontendSoundProgramVoiceV1> voices;
};
// Admits the three startup and two later scenic programs. Live admission order
// remains the caller's responsibility, not a baked startup-only schedule.
// Initial random state is the resident module state. The caller must qualify
// its actual startup admission/timer order before using this as a live owner.
[[nodiscard]] RacFrontendSoundProgramsV1 compile_rac_frontend_sound_programs_v1(
    const RacFrontendSoundBankV1& bank, std::span<const std::byte> sound_irx);

[[nodiscard]] AudioProgramCuesV1 compile_rac_frontend_sound_cues_v1(
    std::span<const std::byte> boot_executable);

} // namespace openrc
