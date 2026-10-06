#pragma once

#include "openrc/audio_voice.hpp"

#include <memory>

namespace openrc {

struct AudioVoiceMixerLimitsV1 {
    std::uint32_t max_voices = 64, max_render_frames = 1048576;
};
class AudioVoiceMixerError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
// Physical PCM ownership only. A finished voice keeps its slot and token until
// the caller explicitly retires it. Program callbacks, observation cadence and
// logical detach are separate, prepared control policies of the bank owner.
class AudioVoiceMixerV1 final {
public:
    explicit AudioVoiceMixerV1(std::uint32_t sample_rate, AudioVoiceMixerLimitsV1 = {});
    [[nodiscard]] std::uint64_t admit(std::unique_ptr<AudioVoicePlayerV1>, AudioVoiceControlV1);
    void set_control(std::uint64_t token, AudioVoiceControlV1);
    void release(std::uint64_t token);
    void stop(std::uint64_t token);
    void stop_all() noexcept;
    void retire(std::uint64_t token);
    [[nodiscard]] AudioVoiceStateV1 voice_state(std::uint64_t token) const;
    [[nodiscard]] std::size_t owned_voices() const noexcept;
    [[nodiscard]] std::uint64_t rendered_frames() const noexcept { return rendered_frames_; }
    // Every physical voice advances once per output frame. Sum signed voice
    // PCM in wide integers, then saturate each complete channel sum to PCM16.
    // Writes silence when no voice emits a frame; no rendering allocations.
    void render(std::span<std::int16_t> interleaved_stereo);
private:
    struct Slot {
        std::uint64_t token = 0;
        std::unique_ptr<AudioVoicePlayerV1> player;
        AudioVoiceControlV1 control;
    };
    [[nodiscard]] Slot& find(std::uint64_t);
    [[nodiscard]] const Slot& find(std::uint64_t) const;
    std::uint32_t sample_rate_;
    AudioVoiceMixerLimitsV1 limits_;
    std::vector<Slot> slots_;
    std::uint64_t next_token_ = 1, rendered_frames_ = 0;
};

} // namespace openrc
