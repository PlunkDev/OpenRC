#pragma once

#include "openrc/audio_clip.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace openrc::runtime {

struct WindowsAudioBankClipV1 {
    std::uint32_t key=0;
    AudioClipV1 clip;
};
enum class WindowsAudioVoiceStateV1 { queued, prepared, active, retiring };
enum class WindowsAudioVoiceEventKindV1 { started, natural_completion, stopped, cancelled };
struct WindowsAudioVoiceEventV1 {
    std::uint64_t token=0;
    std::uint32_t clip_key=0;
    std::uint32_t owner=0;
    WindowsAudioVoiceEventKindV1 kind=WindowsAudioVoiceEventKindV1::cancelled;
    std::uint64_t played_samples=0; // Sample frames, preserved before device reset.
};
struct WindowsAudioVoiceV1 {
    std::uint64_t token=0;
    std::uint32_t clip_key=0;
    std::uint32_t owner=0;
    WindowsAudioVoiceStateV1 state=WindowsAudioVoiceStateV1::queued;
    std::uint64_t played_samples=0;
};
struct WindowsAudioBankStatsV1 {
    bool loaded=false,stopping=false;
    std::size_t clips=0,queued=0,prepared=0,active=0,retiring=0,pending_events=0;
    std::uint64_t pcm_bytes=0,accepted=0,started=0,naturally_completed=0,stopped=0,cancelled=0;
};

// A single-threaded native lifetime owner for already lowered finite PCM.
// This host queue does not infer original source scheduling, loops, voices,
// gain or bank-completion rules. Each key owns its immutable PCM storage.
class WindowsAudioBankV1 final {
public:
    static constexpr std::size_t voice_limit=30;
    static constexpr std::size_t event_limit=256;
    explicit WindowsAudioBankV1(std::vector<WindowsAudioBankClipV1> clips,
        std::uint64_t max_pcm_bytes=64U*1024U*1024U);
    ~WindowsAudioBankV1();
    WindowsAudioBankV1(const WindowsAudioBankV1&)=delete;
    WindowsAudioBankV1& operator=(const WindowsAudioBankV1&)=delete;

    // Reserves a pending slot and its eventual event capacity. No device is
    // opened until pump(). Exhaustion/backpressure rejects without admission.
    [[nodiscard]] std::uint64_t queue(std::uint32_t clip_key,std::uint32_t owner=0);
    // Observes actual completion, retires finished owners, then starts pending
    // voices in token order. Device errors throw and retain retryable owners;
    // already emitted events remain available through take_events().
    void pump();
    // Cancels pending voices and synchronously resets/unprepares/closes every
    // opened owner. Tries every owner on error, retains failed ones, then throws.
    // Calling again retries unfinished retirement; new queues reject meanwhile.
    void stop_all_and_wait();
    // Requires no pending or opened voices. Releases the owned PCM allocations.
    // Repetition is harmless; subsequent queue/pump calls reject.
    void unload();
    // Counts describe acknowledged lifecycle state. They do not poll hardware;
    // an active slot persists until pump/stop observes and retires its owner.
    [[nodiscard]] WindowsAudioBankStatsV1 stats() const noexcept;
    [[nodiscard]] std::vector<WindowsAudioVoiceV1> voices() const;
    // A completed/cancelled token is stale and rejects, rather than referring
    // to a subsequently reused slot. Terminal facts remain in the event queue.
    [[nodiscard]] WindowsAudioVoiceV1 voice(std::uint64_t token) const;
    [[nodiscard]] std::vector<WindowsAudioVoiceEventV1> take_events();

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
}
