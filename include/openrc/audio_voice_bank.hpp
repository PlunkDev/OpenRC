#pragma once

#include "openrc/audio_envelope.hpp"
#include "openrc/audio_program.hpp"
#include "openrc/audio_read_ahead.hpp"

#include <optional>
#include <string>

namespace openrc {

// Prepared arithmetic selecting table coordinates from either the voice's
// admission snapshot or its current program owner. Each term divides toward
// zero before addition. The final sum is clamped or wrapped in [minimum,maximum].
struct AudioScalarTermV1 {
    bool live = false;
    std::uint32_t scalar = 0;
    std::int32_t multiplier = 1;
    std::uint32_t divisor = 1;
};
struct AudioScalarProjectionV1 {
    std::int32_t constant = 0, minimum = 0, maximum = 0;
    bool wrap = false;
    std::vector<AudioScalarTermV1> terms;
};
struct AudioPhaseCurveV1 {
    std::int32_t first_control = 0;
    std::vector<std::uint16_t> increments;
};
struct AudioVoiceBindingV1 {
    std::uint32_t program_key = 0, voice_index = 0;
    std::uint32_t stream_index = 0, gain_table_index = 0, phase_curve_index = 0;
    AudioScalarProjectionV1 level, pan, phase;
    AudioEnvelopeV1 envelope;
    std::optional<AudioReadAheadV1> read_ahead;
};
enum class AudioVoiceScheduleOrderV1 : std::uint32_t { observe_program_release_start_modulation };
struct AudioVoiceObservationV1 {
    // The host supplies one persistent PCM clock origin. Admission of another
    // program never resets this observation grid or the program clock.
    std::uint32_t frame_stride = 0, first_frame_offset = 0;
    std::uint32_t zero_observations_before_completion = 0;
    std::uint32_t observations_after_completion_before_retire = 0;
    std::uint32_t zero_observations_after_release = 0;
    // A newly requested voice remains pending until the explicit start stage;
    // it cannot be observed or render PCM before that stage commits its start.
    AudioVoiceScheduleOrderV1 schedule_order = AudioVoiceScheduleOrderV1::observe_program_release_start_modulation;
};
// Resource handles resolve only to separately admitted neutral owners. No
// source tone fields, compressed samples, source addresses or device layouts.
struct AudioVoiceBankV1 {
    AudioVoiceObservationV1 observation;
    std::string program_resource_id;
    std::vector<std::string> stream_resource_ids, gain_resource_ids;
    std::vector<AudioPhaseCurveV1> phase_curves;
    std::vector<AudioVoiceBindingV1> bindings;
};
struct AudioVoiceBankLimitsV1 {
    std::uint64_t max_bytes = 16U * 1024U * 1024U;
    std::uint32_t max_resources = 64, max_curves = 64, max_bindings = 1024;
    std::uint32_t max_curve_values = 65536, max_terms = 16, max_id_bytes = 256;
};
class AudioVoiceBankError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
void validate_audio_scalar_projection_v1(const AudioScalarProjectionV1&, AudioVoiceBankLimitsV1 = {});
[[nodiscard]] std::int32_t evaluate_audio_scalar_projection_v1(
    const AudioScalarProjectionV1&,
    const std::array<std::int32_t, audio_program_scalar_count_v1>& snapshot,
    const std::array<std::int32_t, audio_program_scalar_count_v1>& live);
void validate_audio_voice_bank_v1(const AudioVoiceBankV1&, AudioVoiceBankLimitsV1 = {});
[[nodiscard]] std::vector<std::byte> encode_audio_voice_bank_v1(const AudioVoiceBankV1&, AudioVoiceBankLimitsV1 = {});
[[nodiscard]] AudioVoiceBankV1 decode_audio_voice_bank_v1(std::span<const std::byte>, AudioVoiceBankLimitsV1 = {});

} // namespace openrc
