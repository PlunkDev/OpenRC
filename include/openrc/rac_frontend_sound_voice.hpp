#pragma once

#include "openrc/rac_frontend_sound_compile.hpp"
#include "openrc/audio_envelope.hpp"
#include "openrc/audio_read_ahead.hpp"
#include "openrc/audio_gain_table.hpp"

namespace openrc {

struct RacFrontendSoundGainSourceV1 {
    std::array<std::array<std::int16_t, 2>, 180> pan{};
};
struct RacFrontendSoundGainInputV1 {
    std::uint32_t descriptor_volume = 0, tone_volume = 0;
    std::int32_t pan_degrees = 0;
    std::array<std::int16_t, 2> previous_panned{};
    std::uint32_t group_volume = 0, stereo_mode = 0;
};
struct RacFrontendSoundGainV1 {
    std::array<std::int16_t, 2> panned{}, channel_factors{};
};
[[nodiscard]] RacFrontendSoundGainSourceV1 make_rac_frontend_sound_gain_source_v1(
    std::span<const std::byte> sound_irx);
// Compiler-side source arithmetic. The retained panned pair determines source
// phase continuity when pan crosses front/rear hemispheres; it is not optional.
[[nodiscard]] RacFrontendSoundGainV1 evaluate_rac_frontend_sound_gain_v1(
    const RacFrontendSoundGainSourceV1&, const RacFrontendSoundGainInputV1&);
// Six retained phase classes are lowered to ordinary finite-state table cells.
// The other gain argument is selected by level_index in [0,127].
[[nodiscard]] AudioGainTableV1 compile_rac_frontend_sound_gain_table_v1(
    const RacFrontendSoundGainSourceV1&, std::uint32_t fixed_volume,
    bool variable_descriptor_volume, std::uint32_t group_volume, std::uint32_t stereo_mode);
[[nodiscard]] AudioEnvelopeV1 compile_rac_frontend_sound_envelope_v1(
    std::uint16_t source_adsr1, std::uint16_t source_adsr2);
[[nodiscard]] AudioReadAheadV1 compile_rac_frontend_sound_read_ahead_v1(
    const RacFrontendSoundBankV1&, std::uint32_t block_index);

} // namespace openrc
