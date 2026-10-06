#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>

namespace openrc {

// Prepared fixed-point envelope arithmetic. Stages contain ordinary numeric
// coefficients, never packed device registers. One step corresponds to one
// output PCM frame. The four stages are attack, decay, sustain and release.
struct AudioEnvelopeStageV1 {
    std::uint32_t counter_increment = 0, slow_counter_increment = 0;
    std::uint32_t slow_above_level = UINT32_MAX;
    std::int32_t delta_multiplier = 0, delta_bias = 0;
    std::uint32_t delta_denominator = 1, target = 0;
    bool decreasing = false;
};
struct AudioEnvelopeV1 {
    std::uint32_t maximum_level = 0, counter_period = 0;
    std::array<AudioEnvelopeStageV1, 4> stages{};
};
class AudioEnvelopeError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
void validate_audio_envelope_v1(const AudioEnvelopeV1&);
struct AudioEnvelopeStateV1 {
    std::uint32_t level = 0, counter = 0, stage = 0;
    std::uint64_t rendered_frames = 0;
    bool stopped = false;
    bool operator==(const AudioEnvelopeStateV1&) const = default;
};
class AudioEnvelopePlayerV1 final {
public:
    explicit AudioEnvelopePlayerV1(AudioEnvelopeV1);
    [[nodiscard]] const AudioEnvelopeStateV1& state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t advance_frame();
    // Retains the reached level and starts release with a fresh counter.
    // Repeated release calls restart that counter, so the owner must send its
    // actual release events rather than polling this method continuously.
    void release() noexcept;
private:
    AudioEnvelopeV1 resource_;
    AudioEnvelopeStateV1 state_;
};

} // namespace openrc
