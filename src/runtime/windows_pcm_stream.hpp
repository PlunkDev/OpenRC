#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace openrc::runtime {
struct WindowsPcmStreamLimitsV1 {
    std::uint32_t buffer_count=8,max_buffer_frames=2048;
};
struct WindowsPcmBufferCompletionV1 {
    std::uint64_t token=0;
    std::uint32_t frames=0;
    bool naturally_completed=false;
};
struct WindowsPcmStreamStatsV1 {
    std::uint32_t channels=0,buffer_count=0,held_buffers=0,queued_buffers=0,pending_completions=0;
    std::uint64_t submitted_buffers=0,submitted_frames=0,naturally_completed_buffers=0,
        naturally_completed_frames=0,cancelled_buffers=0,cancelled_frames=0,owned_bytes=0;
    bool stopping=false,retired=false;
};

// Thread-confined output for caller-rendered, interleaved PCM16 at 48 kHz.
// Owns every submitted sample until its header is returned and unprepared.
// Creates no worker and infers no source scheduling, looping or DSP policy.
class WindowsPcmStreamV1 final {
public:
    static constexpr std::uint32_t sample_rate=48000;
    explicit WindowsPcmStreamV1(std::uint32_t channels,WindowsPcmStreamLimitsV1 limits={});
    ~WindowsPcmStreamV1();
    WindowsPcmStreamV1(const WindowsPcmStreamV1&)=delete;
    WindowsPcmStreamV1& operator=(const WindowsPcmStreamV1&)=delete;
    // Nonempty, whole sample frames, bounded by max_buffer_frames. Copies the
    // samples before actual prepare/write. Null means bounded backpressure;
    // caller retains the unaccepted input. Accepted tokens are never reused.
    [[nodiscard]] std::optional<std::uint64_t> submit(std::span<const std::int16_t> pcm);
    // Resets the event before scanning all headers, then returns real device
    // completions. Errors retain owners and any earlier acknowledgments for
    // the next successful poll/stop. Polling also samples the device clock.
    [[nodiscard]] std::vector<WindowsPcmBufferCompletionV1> poll();
    // Borrowed manual-reset event; only wait on it, never reset/close it.
    // Signals require poll(), not an assumed completion count. Null after
    // retirement. No wait may outlive this owner or its explicit retirement.
    [[nodiscard]] HANDLE completion_event() const;
    [[nodiscard]] std::uint64_t played_samples() const; // Sample frames.
    [[nodiscard]] WindowsPcmStreamStatsV1 stats() const;
    // No remaining driver-owned headers. Initially true; this is not source
    // EOF or natural completion of an entire source program.
    [[nodiscard]] bool drained() const;
    [[nodiscard]] bool retired() const;
    // Captures clock and per-header natural status before reset; then checks
    // reset/unprepare/close, releases buffers/event, and returns completions.
    // Failure retains unfinished ownership for retry. Successful repetition is
    // harmless; submissions after the first stop request reject.
    [[nodiscard]] std::vector<WindowsPcmBufferCompletionV1> stop_and_retire();
private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
}
