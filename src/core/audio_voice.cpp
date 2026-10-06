#include "openrc/audio_voice.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace openrc {
namespace {
AudioStreamV1 validate_voice(AudioStreamV1 stream, const AudioEnvelopeV1& envelope,
    const std::optional<AudioReadAheadV1>& read_ahead, AudioStreamLimitsV1 limits) {
    validate_audio_stream_v1(stream, limits);
    validate_audio_envelope_v1(envelope);
    if (envelope.maximum_level > stream.gain_denominator)
        throw AudioVoiceError("Audio voice envelope exceeds the stream gain domain");
    if (read_ahead) {
        validate_audio_read_ahead_v1(*read_ahead);
        if (stream.repeat_end || read_ahead->input_samples != stream.input_samples.size() ||
            read_ahead->required_lookahead_samples < 4U ||
            read_ahead->refill_when_available_at_most + 1U < read_ahead->required_lookahead_samples)
            throw AudioVoiceError("Audio voice read-ahead does not describe its finite stream");
    }
    return stream;
}
}

AudioVoicePlayerV1::AudioVoicePlayerV1(AudioStreamV1 stream, AudioEnvelopeV1 envelope,
    std::optional<AudioReadAheadV1> read_ahead, AudioStreamLimitsV1 limits) :
    limits_(limits), gain_denominator_(stream.gain_denominator),
    phase_denominator_(stream.phase_denominator), read_ahead_definition_(read_ahead),
    stream_(validate_voice(std::move(stream), envelope, read_ahead, limits), limits),
    envelope_(envelope) {
    if (read_ahead) read_ahead_.emplace(*read_ahead);
}
AudioVoiceStateV1 AudioVoicePlayerV1::state() const noexcept {
    return {stream_.state(), envelope_.state(),
        read_ahead_ ? std::optional{read_ahead_->state()} : std::nullopt, stop_reason_};
}
std::uint32_t AudioVoicePlayerV1::output_sample_rate() const noexcept {
    return stream_.output_sample_rate();
}
void AudioVoicePlayerV1::release() noexcept {
    if (stop_reason_ == AudioVoiceStopReasonV1::none) envelope_.release();
}
void AudioVoicePlayerV1::stop() noexcept {
    if (stop_reason_ == AudioVoiceStopReasonV1::none) stop_reason_ = AudioVoiceStopReasonV1::forced;
}
void AudioVoicePlayerV1::validate_control(const AudioVoiceControlV1& control) const {
    const auto gain = static_cast<std::int32_t>(gain_denominator_);
    if (control.phase_increment > limits_.max_phase_increment ||
        control.channel_gains[0] < -gain || control.channel_gains[0] > gain ||
        control.channel_gains[1] < -gain || control.channel_gains[1] > gain ||
        (read_ahead_definition_ && std::uint64_t{control.phase_increment} >
            std::uint64_t{std::min(read_ahead_definition_->refill_samples,
                read_ahead_definition_->refill_when_available_at_most + 1U)} * phase_denominator_))
        throw AudioVoiceError("Audio voice control exceeds its admitted gain or input movement");
}
AudioVoiceRenderResultV1 AudioVoicePlayerV1::render(
    std::span<std::int16_t> output, std::span<const AudioVoiceControlV1> controls) {
    const auto frames = output.size() / 2U;
    if (output.size() % 2U || frames > limits_.max_render_frames ||
        (controls.size() != 1U && controls.size() != frames) ||
        frames > std::numeric_limits<std::uint64_t>::max() - envelope_.state().rendered_frames)
        throw AudioVoiceError("Audio voice output bounds or control count differs");
    for (const auto& control : controls) validate_control(control);
    AudioVoiceRenderResultV1 result;
    while (result.frames_written < frames && stop_reason_ == AudioVoiceStopReasonV1::none) {
        if (stream_.state().exhausted ||
            (read_ahead_ && !read_ahead_->before_frame(stream_.state().input_cursor))) {
            stop_reason_ = AudioVoiceStopReasonV1::input_exhausted;
            break;
        }
        const auto& control = controls[controls.size() == 1U ? 0U : result.frames_written];
        const AudioStreamControlV1 stream_control{
            control.phase_increment, envelope_.advance_frame(), control.channel_gains};
        const auto written = stream_.render(output.subspan(std::size_t{result.frames_written} * 2U, 2U),
            std::span{&stream_control, 1U});
        result.frames_written += written.frames_written;
        if (envelope_.state().stopped) stop_reason_ = AudioVoiceStopReasonV1::envelope_stopped;
        else if (written.exhausted) stop_reason_ = AudioVoiceStopReasonV1::input_exhausted;
    }
    result.stop_reason = stop_reason_;
    return result;
}

} // namespace openrc
