#include "openrc/rac_frontend_sound_pitch.hpp"

#include <bit>
#include <cstddef>
#include <limits>

namespace openrc {
namespace {
std::int32_t signed_byte(std::uint32_t value, unsigned shift) {
    return std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value >> shift));
}

std::uint16_t note_pitch(const RacFrontendSoundSourceV1& source,
    std::int32_t center, std::int32_t center_fine,
    std::int32_t note, std::int32_t fine) {
    const auto fine_sum = fine + center_fine;
    const auto coarse = fine_sum / 128;
    const auto delta = note + coarse - center;
    const auto octave = delta / 12;
    auto shift = octave - 2;
    auto semitone = delta - octave * 12;
    auto remainder = fine_sum - coarse * 128;
    if (semitone < 0 || (semitone == 0 && remainder < 0)) {
        semitone += 12;
        shift = octave - 3;
    }
    if (remainder < 0) {
        // The qualified note/fine split gives coarse=0 on this path. Keep
        // the complete source adjustment and reject any table escape.
        semitone = semitone - 1 + coarse;
        remainder += 128 * (coarse + 1);
    }
    if (semitone < 0 || semitone >= 12 || remainder < 0 || remainder >= 128)
        throw RacFrontendSoundCompileError("Frontend pitch leaves its qualified table domain");
    const auto product = std::uint32_t{source.pitch_semitones[static_cast<std::size_t>(semitone)]} *
        source.pitch_fine[static_cast<std::size_t>(remainder)];
    auto pitch = static_cast<std::uint32_t>(std::bit_cast<std::int32_t>(product) >> 16);
    // The original conversion rounds only negative octave shifts; it does
    // not left-shift the table product for zero or positive shifts.
    if (shift < 0) {
        const auto right = static_cast<unsigned>(-shift);
        pitch = (pitch + (std::uint32_t{1} << ((right - 1U) & 31U))) >> (right & 31U);
    }
    return static_cast<std::uint16_t>(pitch);
}
} // namespace

std::uint16_t compile_rac_frontend_sound_pitch_v1(
    const RacFrontendSoundSourceV1& source, const SBlkItemV1& tone,
    std::int16_t bend) {
    const auto center = signed_byte(tone.payload[1], 16);
    const auto center_fine = signed_byte(tone.payload[1], 24);
    const auto down = signed_byte(tone.payload[3], 0);
    const auto up = signed_byte(tone.payload[3], 8);
    if (tone.tag != 1 || center == -128 || center_fine < 0 || down < 0 || up < 0)
        throw RacFrontendSoundCompileError("Frontend tone tuning is outside the qualified pitch profile");
    // Signed division truncates toward zero. The two directions deliberately
    // have different endpoint denominators and may have different ranges.
    const auto offset = std::int32_t{bend} * 128 * (bend < 0 ? down : up) /
        (bend < 0 ? 32768 : 32767);
    const auto units = 60 * 128 + offset;
    const auto pitch = note_pitch(source, center < 0 ? -center : center,
        center_fine, units / 128, units % 128);
    if (center < 0) return pitch;
    // Positive source center notes select the original 44.1/48 kHz scaling.
    return static_cast<std::uint16_t>(std::uint32_t{pitch} * 44100U / 48000U);
}

RacFrontendSoundPitchCurveV1 compile_rac_frontend_sound_pitch_curve_v1(
    const RacFrontendSoundSourceV1& source, const SBlkItemV1& tone) {
    RacFrontendSoundPitchCurveV1 result;
    result.phase_increments.reserve(65536);
    for (std::int32_t bend = -32768; bend <= 32767; ++bend)
        result.phase_increments.push_back(compile_rac_frontend_sound_pitch_v1(
            source, tone, static_cast<std::int16_t>(bend)));
    return result;
}

} // namespace openrc
