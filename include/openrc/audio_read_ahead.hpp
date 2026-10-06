#pragma once

#include <cstdint>
#include <stdexcept>

namespace openrc {

// Explicit prepared end policy for a finite input with bounded read-ahead.
// Reaching the final refill retires this voice before that output frame. The
// decoded input and interpolation are separate AudioStreamV1 owners.
struct AudioReadAheadV1 {
    std::uint64_t input_samples = 0;
    std::uint32_t refill_samples = 0, refill_when_available_at_most = 0;
    std::uint32_t required_lookahead_samples = 0;
};
struct AudioReadAheadStateV1 {
    std::uint64_t fetched_samples = 0, input_cursor = 0, output_frames = 0;
    bool stopped = false;
    bool operator==(const AudioReadAheadStateV1&) const = default;
};
class AudioReadAheadError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
void validate_audio_read_ahead_v1(const AudioReadAheadV1&);
class AudioReadAheadPlayerV1 final {
public:
    explicit AudioReadAheadPlayerV1(AudioReadAheadV1);
    [[nodiscard]] const AudioReadAheadStateV1& state() const noexcept { return state_; }
    // Called once before each output frame, with the stream's reached input
    // cursor. False is the prepared EOF event, not device completion. Cursor
    // movement may vary each frame but cannot skip beyond a refill quantum.
    [[nodiscard]] bool before_frame(std::uint64_t input_cursor);
private:
    AudioReadAheadV1 resource_;
    AudioReadAheadStateV1 state_;
};

} // namespace openrc
