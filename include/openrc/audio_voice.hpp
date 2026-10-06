#pragma once

#include "openrc/audio_envelope.hpp"
#include "openrc/audio_read_ahead.hpp"
#include "openrc/audio_stream.hpp"

#include <optional>

namespace openrc {

struct AudioVoiceControlV1 {
    std::uint32_t phase_increment = 0;
    std::array<std::int32_t, 2> channel_gains{};
    bool operator==(const AudioVoiceControlV1&) const = default;
};
enum class AudioVoiceStopReasonV1 { none, input_exhausted, envelope_stopped, forced };
struct AudioVoiceStateV1 {
    AudioStreamPlaybackStateV1 stream;
    AudioEnvelopeStateV1 envelope;
    std::optional<AudioReadAheadStateV1> read_ahead;
    AudioVoiceStopReasonV1 stop_reason = AudioVoiceStopReasonV1::none;
    bool operator==(const AudioVoiceStateV1&) const = default;
};
struct AudioVoiceRenderResultV1 {
    std::uint32_t frames_written = 0;
    AudioVoiceStopReasonV1 stop_reason = AudioVoiceStopReasonV1::none;
};
class AudioVoiceError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};

// An owned neutral stream and envelope. Input availability is checked before
// advancing the envelope; output uses that frame's new envelope level. Release
// and input exhaustion do not report completion of any program or audio device.
class AudioVoicePlayerV1 final {
public:
    AudioVoicePlayerV1(AudioStreamV1, AudioEnvelopeV1,
        std::optional<AudioReadAheadV1> = {}, AudioStreamLimitsV1 = {});
    [[nodiscard]] AudioVoiceStateV1 state() const noexcept;
    [[nodiscard]] std::uint32_t output_sample_rate() const noexcept;
    void validate_control(const AudioVoiceControlV1&) const;
    void release() noexcept;
    void stop() noexcept;
    // Stereo PCM, constant or per-frame controls; no allocation while rendering.
    // Rejects all invalid controls before mutation. Unwritten samples retain
    // their original contents, allowing the caller to distinguish the tail.
    [[nodiscard]] AudioVoiceRenderResultV1 render(
        std::span<std::int16_t>, std::span<const AudioVoiceControlV1>);
private:
    const AudioStreamLimitsV1 limits_;
    const std::uint32_t gain_denominator_, phase_denominator_;
    const std::optional<AudioReadAheadV1> read_ahead_definition_;
    AudioStreamPlayerV1 stream_;
    AudioEnvelopePlayerV1 envelope_;
    std::optional<AudioReadAheadPlayerV1> read_ahead_;
    AudioVoiceStopReasonV1 stop_reason_ = AudioVoiceStopReasonV1::none;
};

} // namespace openrc
