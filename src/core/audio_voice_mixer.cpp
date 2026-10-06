#include "openrc/audio_voice_mixer.hpp"

#include <algorithm>
#include <limits>

namespace openrc {
AudioVoiceMixerV1::AudioVoiceMixerV1(std::uint32_t rate, AudioVoiceMixerLimitsV1 limits) :
    sample_rate_(rate), limits_(limits) {
    if (rate < 8000U || rate > 192000U || !limits.max_voices || limits.max_voices > 4096U ||
        !limits.max_render_frames || limits.max_render_frames > 1048576U)
        throw AudioVoiceMixerError("Audio mixer rate or ownership bounds are invalid");
    slots_.resize(limits.max_voices);
}
AudioVoiceMixerV1::Slot& AudioVoiceMixerV1::find(std::uint64_t token) {
    for (auto& slot : slots_) if (slot.player && slot.token == token) return slot;
    throw AudioVoiceMixerError("Audio mixer voice token is not owned");
}
const AudioVoiceMixerV1::Slot& AudioVoiceMixerV1::find(std::uint64_t token) const {
    for (const auto& slot : slots_) if (slot.player && slot.token == token) return slot;
    throw AudioVoiceMixerError("Audio mixer voice token is not owned");
}
std::uint64_t AudioVoiceMixerV1::admit(std::unique_ptr<AudioVoicePlayerV1> voice, AudioVoiceControlV1 control) {
    if (!voice || voice->output_sample_rate() != sample_rate_ ||
        voice->state().stop_reason != AudioVoiceStopReasonV1::none || next_token_ == UINT64_MAX)
        throw AudioVoiceMixerError("Audio mixer cannot admit this voice");
    voice->validate_control(control);
    for (auto& slot : slots_) if (!slot.player) {
        slot.token = next_token_++; slot.control = control; slot.player = std::move(voice);
        return slot.token;
    }
    throw AudioVoiceMixerError("Audio mixer physical voice capacity is exhausted");
}
void AudioVoiceMixerV1::set_control(std::uint64_t token, AudioVoiceControlV1 control) {
    auto& slot = find(token); slot.player->validate_control(control); slot.control = control;
}
void AudioVoiceMixerV1::release(std::uint64_t token) { find(token).player->release(); }
void AudioVoiceMixerV1::stop(std::uint64_t token) { find(token).player->stop(); }
void AudioVoiceMixerV1::stop_all() noexcept {
    for (auto& slot : slots_) if (slot.player) slot.player->stop();
}
void AudioVoiceMixerV1::retire(std::uint64_t token) {
    auto& slot = find(token);
    if (slot.player->state().stop_reason == AudioVoiceStopReasonV1::none)
        throw AudioVoiceMixerError("Audio mixer cannot retire an unfinished voice");
    slot.player.reset(); slot.token = 0;
}
AudioVoiceStateV1 AudioVoiceMixerV1::voice_state(std::uint64_t token) const { return find(token).player->state(); }
std::size_t AudioVoiceMixerV1::owned_voices() const noexcept {
    return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const auto& slot){ return bool(slot.player); }));
}
void AudioVoiceMixerV1::render(std::span<std::int16_t> output) {
    const auto frames = output.size() / 2U;
    if (output.size() % 2U || frames > limits_.max_render_frames || frames > UINT64_MAX - rendered_frames_)
        throw AudioVoiceMixerError("Audio mixer output span exceeds its frame bounds");
    // Check each independently admitted clock before the first state change.
    for (const auto& slot : slots_) if (slot.player &&
        frames > UINT64_MAX - slot.player->state().envelope.rendered_frames)
        throw AudioVoiceMixerError("Audio mixer would overflow a voice clock");
    for (std::size_t frame = 0; frame < frames; ++frame) {
        std::array<std::int64_t,2> mixed{};
        for (auto& slot : slots_) if (slot.player) {
            std::array<std::int16_t,2> voice_pcm{};
            static_cast<void>(slot.player->render(voice_pcm, std::span{&slot.control,1U}));
            for (unsigned channel = 0; channel < 2; ++channel) mixed[channel] += voice_pcm[channel];
        }
        for (unsigned channel = 0; channel < 2; ++channel)
            output[frame*2U+channel] = static_cast<std::int16_t>(std::clamp<std::int64_t>(mixed[channel],-32768,32767));
    }
    rendered_frames_ += frames;
}
} // namespace openrc
