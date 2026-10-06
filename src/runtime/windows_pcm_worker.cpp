#include "windows_pcm_worker.hpp"
#include "windows_pcm_stream.hpp"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace openrc::runtime {
namespace {
void check_event(BOOL result, const char* operation) {
    if (!result) throw std::runtime_error(std::string(operation) + " failed (Windows " +
        std::to_string(GetLastError()) + ")");
}
}
struct WindowsPcmWorkerV1::Implementation {
    struct Command {
        std::uint64_t token = 0;
        WindowsPcmWorkerCommandV1 value;
    };
    const WindowsPcmWorkerOptionsV1 options;
    std::unique_ptr<WindowsPcmWorkerRendererV1> renderer;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::thread thread;
    HANDLE wake = nullptr;
    std::vector<Command> commands;
    std::vector<WindowsPcmWorkerCommandResultV1> results;
    WindowsPcmWorkerStatsV1 state;
    std::exception_ptr failure, retirement_failure;
    std::uint64_t next_token = 1, retry_generation = 0;
    bool command_in_flight = false, abandon = false;

    Implementation(std::unique_ptr<WindowsPcmWorkerRendererV1> source, WindowsPcmWorkerOptionsV1 settings)
        : options(settings), renderer(std::move(source)) {
        if (!renderer || (options.channels != 1 && options.channels != 2) ||
            !options.block_frames || options.block_frames > WindowsPcmStreamV1::sample_rate ||
            !options.buffer_count || options.buffer_count > 32 ||
            !options.command_limit || options.command_limit > 4096)
            throw std::invalid_argument("PCM worker requires a renderer and bounded PCM/command capacities");
        commands.reserve(options.command_limit); results.reserve(options.command_limit);
        state.first_frame = state.next_frame = state.earliest_command_frame = options.first_frame;
        wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!wake) throw std::runtime_error("Cannot create PCM worker wake event");
    }
    ~Implementation() { if (wake) CloseHandle(wake); }

    void start() {
        std::lock_guard lock(mutex);
        if (state.started) throw std::logic_error("PCM worker was already started");
        thread = std::thread([this] { run(); });
        state.started = true;
    }
    void wake_locked() { check_event(SetEvent(wake), "Signal PCM worker"); }
    void complete_command(const Command& command, WindowsPcmWorkerCommandResultKindV1 kind) {
        // Every admitted command reserved its eventual result slot.
        results.push_back({command.token, command.value.frame, kind});
        if (kind == WindowsPcmWorkerCommandResultKindV1::applied) ++state.applied_commands;
        else if (kind == WindowsPcmWorkerCommandResultKindV1::cancelled) ++state.cancelled_commands;
    }
    void cancel_commands_locked() {
        for (const auto& command : commands)
            complete_command(command, WindowsPcmWorkerCommandResultKindV1::cancelled);
        commands.clear();
    }
    void sample_device(WindowsPcmStreamV1& device) {
        const auto position = device.played_samples();
        const auto device_state = device.stats();
        std::lock_guard lock(mutex);
        state.submitted_frames = device_state.submitted_frames;
        state.played_frames = position;
        state.naturally_completed_frames = device_state.naturally_completed_frames;
        state.cancelled_frames = device_state.cancelled_frames;
        state.device_retired = device_state.retired;
    }
    void wait_device(WindowsPcmStreamV1& device) {
        const HANDLE events[]{wake, device.completion_event()};
        const auto status = WaitForMultipleObjects(2, events, FALSE, 100);
        if (status != WAIT_OBJECT_0 && status != WAIT_OBJECT_0 + 1 && status != WAIT_TIMEOUT)
            throw std::runtime_error("PCM worker event wait failed");
    }
    void render_loop(WindowsPcmStreamV1& device) {
        std::vector<std::int16_t> buffer(std::size_t(options.block_frames) * options.channels);
        for (;;) {
            (void)device.poll(); sample_device(device);
            Command command;
            bool has_command = false, at_end = false, stop = false;
            std::uint64_t frame = 0;
            std::uint32_t count = 0;
            {
                std::lock_guard lock(mutex);
                check_event(ResetEvent(wake), "Reset PCM worker wake event");
                stop = state.stopping;
                at_end = state.end_of_input;
                frame = state.next_frame;
                if (!stop && !at_end && !commands.empty() && commands.front().value.frame == frame) {
                    command = commands.front(); commands.erase(commands.begin());
                    command_in_flight = has_command = true;
                } else if (!stop && !at_end && device.stats().held_buffers < options.buffer_count) {
                    count = options.block_frames - static_cast<std::uint32_t>(
                        (frame - options.first_frame) % options.block_frames);
                    if (!commands.empty()) {
                        if (commands.front().value.frame < frame)
                            throw std::logic_error("PCM worker command crossed its reserved output boundary");
                        count = static_cast<std::uint32_t>(std::min<std::uint64_t>(count, commands.front().value.frame - frame));
                    }
                    if (count > UINT64_MAX - frame)
                        throw std::overflow_error("PCM worker output clock overflow");
                    // Caller admission cannot enter this in-flight render.
                    state.earliest_command_frame = frame + count;
                }
            }
            if (stop) break;
            if (has_command) {
                try { renderer->command(command.value); }
                catch (...) {
                    std::lock_guard lock(mutex);
                    complete_command(command, WindowsPcmWorkerCommandResultKindV1::failed);
                    command_in_flight = false; throw;
                }
                std::lock_guard lock(mutex);
                complete_command(command, WindowsPcmWorkerCommandResultKindV1::applied);
                command_in_flight = false;
                continue;
            }
            if (at_end) {
                if (device.drained()) break;
                wait_device(device); continue;
            }
            if (!count) { wait_device(device); continue; }
            auto output = std::span(buffer).first(std::size_t(count) * options.channels);
            const auto rendered = renderer->render(frame, output);
            if (rendered.frames_written > count || (!rendered.end_of_input && rendered.frames_written != count))
                throw std::runtime_error("PCM renderer returned a partial block without source EOF");
            bool stopped_during_render = false;
            {
                std::lock_guard lock(mutex); stopped_during_render = state.stopping;
            }
            if (!stopped_during_render && rendered.frames_written) {
                if (!device.submit(output.first(std::size_t(rendered.frames_written) * options.channels)))
                    throw std::logic_error("PCM worker lost its reserved device buffer");
            }
            {
                std::lock_guard lock(mutex);
                state.next_frame = frame + rendered.frames_written;
                state.earliest_command_frame = state.next_frame;
                if (rendered.end_of_input) { state.end_of_input = true; cancel_commands_locked(); }
            }
            if (stopped_during_render) break;
        }
    }
    void run() noexcept {
        std::unique_ptr<WindowsPcmStreamV1> device;
        try {
            bool stop = false;
            { std::lock_guard lock(mutex); stop = state.stopping; }
            if (!stop) {
                device = std::make_unique<WindowsPcmStreamV1>(options.channels,
                    WindowsPcmStreamLimitsV1{options.buffer_count, options.block_frames});
                render_loop(*device);
            }
        } catch (...) {
            std::lock_guard lock(mutex); failure = std::current_exception(); state.failed = true; state.stopping = true;
        }
        renderer->stop();
        {
            std::lock_guard lock(mutex); cancel_commands_locked();
        }
        for (;;) {
            try {
                if (device) { (void)device->stop_and_retire(); sample_device(*device); }
                std::lock_guard lock(mutex);
                state.device_retired = true; state.retirement_failed = false; retirement_failure = nullptr;
                break;
            } catch (...) {
                std::unique_lock lock(mutex);
                retirement_failure = std::current_exception(); state.retirement_failed = true;
                const auto failed_generation = retry_generation;
                changed.notify_all();
                changed.wait(lock, [&] { return abandon || retry_generation != failed_generation; });
                if (abandon) break;
                state.retirement_failed = false;
            }
        }
        // Destruction is on the same thread as stream construction. Its
        // best-effort fallback retains borrowed native storage on failure.
        device.reset(); renderer.reset();
        {
            std::lock_guard lock(mutex); state.worker_exited = true;
        }
        changed.notify_all();
    }
    void join(bool stop) {
        {
            std::unique_lock lock(mutex);
            if (thread.joinable() && thread.get_id() == std::this_thread::get_id())
                throw std::logic_error("PCM worker cannot join itself");
            if (stop) {
                state.stopping = true; ++retry_generation;
                state.retirement_failed = false;
                changed.notify_all();
                if (wake) wake_locked();
            }
            if (!state.started) {
                if (!stop) throw std::logic_error("PCM worker has not started");
                thread = std::thread([this] { run(); }); state.started = true;
            }
            changed.wait(lock, [&] { return state.worker_exited || state.retirement_failed; });
            if (state.retirement_failed && !state.worker_exited) std::rethrow_exception(retirement_failure);
        }
        if (thread.joinable()) thread.join();
        std::exception_ptr pending_failure;
        {
            std::lock_guard lock(mutex);
            state.joined = true;
            if (wake) { check_event(CloseHandle(wake), "Close PCM worker wake event"); wake = nullptr; }
            pending_failure = failure ? failure : retirement_failure;
        }
        if (pending_failure) std::rethrow_exception(pending_failure);
    }
};

WindowsPcmWorkerV1::WindowsPcmWorkerV1(std::unique_ptr<WindowsPcmWorkerRendererV1> renderer,
    WindowsPcmWorkerOptionsV1 options) : implementation_(std::make_unique<Implementation>(std::move(renderer), options)) {}
WindowsPcmWorkerV1::~WindowsPcmWorkerV1() {
    try { implementation_->join(true); }
    catch (...) {
        {
            std::lock_guard lock(implementation_->mutex); implementation_->abandon = true;
        }
        implementation_->changed.notify_all();
        if (implementation_->thread.joinable()) implementation_->thread.join();
    }
}
void WindowsPcmWorkerV1::start() { implementation_->start(); }
WindowsPcmWorkerAdmissionV1 WindowsPcmWorkerV1::submit_command(const WindowsPcmWorkerCommandV1& value) {
    auto& s = *implementation_; std::lock_guard lock(s.mutex);
    if (s.state.stopping || s.state.end_of_input || s.state.worker_exited || s.state.failed)
        throw std::logic_error("PCM worker no longer accepts commands");
    WindowsPcmWorkerAdmissionV1 result; result.earliest_frame = s.state.earliest_command_frame;
    if (value.frame < result.earliest_frame) { result.kind = WindowsPcmWorkerAdmissionKindV1::late; return result; }
    if (s.commands.size() + s.results.size() + s.command_in_flight >= s.options.command_limit) return result;
    if (s.next_token == UINT64_MAX) throw std::overflow_error("PCM worker command token overflow");
    // Signal while holding the queue lock, before committing admission. A
    // wake failure cannot consume a token whose caller received no receipt.
    s.wake_locked();
    result.kind = WindowsPcmWorkerAdmissionKindV1::accepted; result.token = s.next_token++;
    const auto position = std::upper_bound(s.commands.begin(), s.commands.end(), value.frame,
        [](std::uint64_t frame, const auto& command) { return frame < command.value.frame; });
    s.commands.insert(position, {result.token, value}); ++s.state.accepted_commands;
    return result;
}
std::vector<WindowsPcmWorkerCommandResultV1> WindowsPcmWorkerV1::take_command_results() {
    auto& s = *implementation_;
    std::vector<WindowsPcmWorkerCommandResultV1> result; result.reserve(s.options.command_limit);
    std::lock_guard lock(s.mutex); result.swap(s.results); return result;
}
WindowsPcmWorkerStatsV1 WindowsPcmWorkerV1::stats() const {
    const auto& s = *implementation_; std::lock_guard lock(s.mutex); auto result = s.state;
    result.queued_commands = static_cast<std::uint32_t>(s.commands.size() + s.command_in_flight);
    result.pending_command_results = static_cast<std::uint32_t>(s.results.size()); return result;
}
void WindowsPcmWorkerV1::request_stop() {
    auto& s = *implementation_; std::lock_guard lock(s.mutex); s.state.stopping = true;
    if (s.wake) s.wake_locked();
}
void WindowsPcmWorkerV1::stop_and_join() { implementation_->join(true); }
void WindowsPcmWorkerV1::join() { implementation_->join(false); }

} // namespace openrc::runtime
