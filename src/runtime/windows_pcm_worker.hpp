#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace openrc::runtime {

struct WindowsPcmWorkerCommandV1 {
    // Absolute output-frame coordinate selected by the caller. The worker
    // never derives a source-program tick, phase, voice or DSP operation.
    std::uint64_t frame = 0;
    std::uint32_t kind = 0;
    std::array<std::uint64_t, 4> arguments{};
};
struct WindowsPcmWorkerRenderResultV1 {
    std::uint32_t frames_written = 0;
    bool end_of_input = false;
};
class WindowsPcmWorkerRendererV1 {
public:
    virtual ~WindowsPcmWorkerRendererV1() = default;
    virtual void command(const WindowsPcmWorkerCommandV1&) = 0;
    virtual WindowsPcmWorkerRenderResultV1 render(
        std::uint64_t first_frame, std::span<std::int16_t> interleaved_pcm) = 0;
    // Stops future source work. This is not device completion or an implied
    // voice-release envelope. Called exactly once, on the worker thread.
    virtual void stop() noexcept = 0;
};
struct WindowsPcmWorkerOptionsV1 {
    std::uint32_t channels = 2;
    // Explicitly supplied: e.g. 200 frames at 48000 Hz gives a 240 Hz grid.
    // A command inside a block splits it at the exact requested frame.
    std::uint32_t block_frames = 0, buffer_count = 8, command_limit = 64;
    std::uint64_t first_frame = 0;
};
enum class WindowsPcmWorkerAdmissionKindV1 { accepted, full, late };
struct WindowsPcmWorkerAdmissionV1 {
    WindowsPcmWorkerAdmissionKindV1 kind = WindowsPcmWorkerAdmissionKindV1::full;
    std::uint64_t token = 0, earliest_frame = 0;
};
enum class WindowsPcmWorkerCommandResultKindV1 { applied, cancelled, failed };
struct WindowsPcmWorkerCommandResultV1 {
    std::uint64_t token = 0, frame = 0;
    WindowsPcmWorkerCommandResultKindV1 kind = WindowsPcmWorkerCommandResultKindV1::cancelled;
};
struct WindowsPcmWorkerStatsV1 {
    std::uint64_t first_frame = 0, next_frame = 0, earliest_command_frame = 0;
    std::uint64_t submitted_frames = 0, played_frames = 0;
    std::uint64_t naturally_completed_frames = 0, cancelled_frames = 0;
    std::uint64_t accepted_commands = 0, applied_commands = 0, cancelled_commands = 0;
    std::uint32_t queued_commands = 0, pending_command_results = 0;
    bool started = false, end_of_input = false, stopping = false;
    bool device_retired = false, worker_exited = false, joined = false;
    bool retirement_failed = false, failed = false;
};

// Owns its renderer and a bounded WindowsPcmStreamV1 on one worker thread.
// No source scheduling, gain/pitch conversion, envelopes or mixing policy is
// inferred here. Renderer callbacks are never invoked from the caller thread.
// Its output-frame origin is independent of any source scheduler's clock.
class WindowsPcmWorkerV1 final {
public:
    WindowsPcmWorkerV1(std::unique_ptr<WindowsPcmWorkerRendererV1>, WindowsPcmWorkerOptionsV1);
    ~WindowsPcmWorkerV1();
    WindowsPcmWorkerV1(const WindowsPcmWorkerV1&) = delete;
    WindowsPcmWorkerV1& operator=(const WindowsPcmWorkerV1&) = delete;
    // Commands can be queued before start, including at first_frame.
    void start();
    [[nodiscard]] WindowsPcmWorkerAdmissionV1 submit_command(const WindowsPcmWorkerCommandV1&);
    [[nodiscard]] std::vector<WindowsPcmWorkerCommandResultV1> take_command_results();
    [[nodiscard]] WindowsPcmWorkerStatsV1 stats() const;
    // Cancellation does not wait. stop_and_join is the checked completion
    // barrier: retire all device headers/storage, finish worker, then join.
    void request_stop();
    // A retirement failure is reported while the worker keeps native owners
    // alive for another call. Success never hides a renderer/device failure.
    // Do not invoke concurrently or from a renderer callback.
    void stop_and_join();
    // Natural EOF also retires the device; join without requesting a stop.
    // May wait for source EOF, so only use for known finite sources.
    void join();
private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace openrc::runtime
