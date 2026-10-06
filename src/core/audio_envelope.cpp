#include "openrc/audio_envelope.hpp"

#include <algorithm>

namespace openrc {
void validate_audio_envelope_v1(const AudioEnvelopeV1& envelope) {
    if (!envelope.maximum_level || envelope.maximum_level > (1U << 30U) ||
        !envelope.counter_period || envelope.counter_period > (1U << 30U))
        throw AudioEnvelopeError("Audio envelope level/counter domain is invalid");
    for (const auto& stage : envelope.stages)
        if (!stage.counter_increment || stage.counter_increment > envelope.counter_period ||
            !stage.slow_counter_increment || stage.slow_counter_increment > envelope.counter_period ||
            !stage.delta_denominator || stage.delta_denominator > (1U << 30U))
            throw AudioEnvelopeError("Audio envelope stage arithmetic is invalid");
}
AudioEnvelopePlayerV1::AudioEnvelopePlayerV1(AudioEnvelopeV1 envelope) : resource_(envelope) {
    validate_audio_envelope_v1(resource_);
}
std::uint32_t AudioEnvelopePlayerV1::advance_frame() {
    if (state_.stopped) return state_.level;
    if (state_.rendered_frames == UINT64_MAX) throw AudioEnvelopeError("Audio envelope clock overflow");
    const auto& stage = resource_.stages[state_.stage];
    const auto increment = state_.level > stage.slow_above_level ? stage.slow_counter_increment : stage.counter_increment;
    const auto counter = std::uint64_t{state_.counter} + increment;
    if (counter >= resource_.counter_period) {
        const auto numerator = std::int64_t{stage.delta_multiplier} * state_.level + stage.delta_bias;
        const auto divisor = std::int64_t{stage.delta_denominator};
        const auto delta = numerator >= 0 ? numerator / divisor : -((-numerator + divisor - 1) / divisor);
        state_.level = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            std::int64_t{state_.level} + delta, 0, resource_.maximum_level));
        state_.counter = 0;
    } else state_.counter = static_cast<std::uint32_t>(counter);
    ++state_.rendered_frames;
    if (state_.stage == 2) state_.stopped = state_.level == 0;
    else if (stage.decreasing ? state_.level <= stage.target : state_.level >= stage.target) {
        if (++state_.stage == resource_.stages.size()) state_.stopped = true;
    }
    return state_.level;
}
void AudioEnvelopePlayerV1::release() noexcept {
    if (!state_.stopped) { state_.stage = 3; state_.counter = 0; }
}
} // namespace openrc
