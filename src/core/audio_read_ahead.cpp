#include "openrc/audio_read_ahead.hpp"

namespace openrc {
void validate_audio_read_ahead_v1(const AudioReadAheadV1& value) {
    if (!value.refill_samples || value.refill_samples > 1048576U ||
        value.input_samples < value.refill_samples || value.input_samples > (UINT64_C(1) << 48U) ||
        value.input_samples % value.refill_samples != 0 ||
        value.refill_when_available_at_most > 1048576U ||
        !value.required_lookahead_samples || value.required_lookahead_samples > value.refill_samples)
        throw AudioReadAheadError("Audio read-ahead policy is invalid");
}
AudioReadAheadPlayerV1::AudioReadAheadPlayerV1(AudioReadAheadV1 value) : resource_(value) {
    validate_audio_read_ahead_v1(resource_);
}
bool AudioReadAheadPlayerV1::before_frame(std::uint64_t cursor) {
    if (state_.stopped) return false;
    if ((!state_.output_frames && cursor != 0) || cursor < state_.input_cursor ||
        cursor - state_.input_cursor > resource_.refill_samples || cursor > state_.fetched_samples ||
        state_.output_frames == UINT64_MAX)
        throw AudioReadAheadError("Audio read-ahead cursor escaped its owned window");
    auto fetched = state_.fetched_samples;
    if (fetched - cursor <= resource_.refill_when_available_at_most) {
        fetched += resource_.refill_samples;
        if (fetched >= resource_.input_samples) {
            state_.fetched_samples = fetched; state_.input_cursor = cursor; state_.stopped = true;
            return false;
        }
    }
    if (fetched - cursor < resource_.required_lookahead_samples)
        throw AudioReadAheadError("Audio read-ahead cannot provide the requested lookahead");
    state_.fetched_samples = fetched; state_.input_cursor = cursor; ++state_.output_frames;
    return true;
}
} // namespace openrc
