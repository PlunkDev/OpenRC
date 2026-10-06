#pragma once

#include "openrc/rac_frontend_sound_compile.hpp"

#include <cstdint>
#include <vector>

namespace openrc {

// Compiler-side lowering for the qualified note-60/fine-0/zero-modifier
// frontend profile. At playback the control is the signed saturated sum of
// the owner's base and modulated bend. The prepared values are neutral input
// phase increments; neither source tuning bytes nor source conversion tables
// need to cross the compiler/runtime boundary. Equal curves may be shared.
struct RacFrontendSoundPitchCurveV1 {
    static constexpr std::int32_t first_control = -32768;
    static constexpr std::uint32_t phase_denominator = 4096;
    std::vector<std::uint16_t> phase_increments;
    bool operator==(const RacFrontendSoundPitchCurveV1&) const = default;
};

[[nodiscard]] std::uint16_t compile_rac_frontend_sound_pitch_v1(
    const RacFrontendSoundSourceV1& source, const SBlkItemV1& tone,
    std::int16_t bend);

// Exactly 65536 entries, from bend -32768 through +32767 inclusive.
[[nodiscard]] RacFrontendSoundPitchCurveV1 compile_rac_frontend_sound_pitch_curve_v1(
    const RacFrontendSoundSourceV1& source, const SBlkItemV1& tone);

} // namespace openrc
