#pragma once

#include "openrc/audio_voice_bank_player.hpp"
#include "windows_pcm_worker.hpp"

namespace openrc::runtime {

struct WindowsAudioProgramBankContentV1 {
    std::string program_resource_id;
    AudioVoiceBankV1 voices;
    AudioProgramBankV1 programs;
    std::vector<AudioVoiceBankStreamV1> streams;
    std::vector<AudioVoiceBankGainV1> gains;
};
enum class WindowsAudioProgramEventKindV1 { admitted, completed, stopped, cancelled };
struct WindowsAudioProgramEventV1 {
    std::uint64_t owner=0,frame=0;
    std::uint32_t program_key=0;
    WindowsAudioProgramEventKindV1 kind=WindowsAudioProgramEventKindV1::cancelled;
};
struct WindowsAudioProgramAdmissionV1 {
    WindowsPcmWorkerAdmissionKindV1 kind=WindowsPcmWorkerAdmissionKindV1::full;
    std::uint64_t owner=0,command_token=0,earliest_frame=0;
};
struct WindowsAudioProgramBankStatsV1 {
    AudioVoiceBankPlaybackStateV1 playback;
    WindowsPcmWorkerStatsV1 worker;
    std::size_t accepted_owners=0,pending_events=0;
    bool stop_acknowledged=false;
};

// Caller methods belong to one control thread. The worker alone advances the
// neutral program/mixer. Queue acceptance reserves an external owner, while
// take_events is the explicit acknowledgement boundary for the caller's mirror.
// Logical completion never implies that a physical tail or device has retired.
class WindowsAudioProgramBankV1 final {
public:
    explicit WindowsAudioProgramBankV1(WindowsAudioProgramBankContentV1,
        std::uint64_t first_frame=0,std::uint32_t buffer_count=8);
    ~WindowsAudioProgramBankV1();
    WindowsAudioProgramBankV1(const WindowsAudioProgramBankV1&)=delete;
    WindowsAudioProgramBankV1& operator=(const WindowsAudioProgramBankV1&)=delete;
    void start();
    [[nodiscard]] WindowsAudioProgramAdmissionV1 queue(std::uint32_t program_key,std::uint64_t frame);
    [[nodiscard]] WindowsPcmWorkerAdmissionV1 stop(std::uint64_t owner,std::uint64_t frame);
    [[nodiscard]] std::vector<WindowsAudioProgramEventV1> take_events();
    [[nodiscard]] WindowsAudioProgramBankStatsV1 stats() const;
    // Runs the admitted stop paths on the worker, then cancels native queued PCM
    // and joins. Success requires source stop acknowledgement AND device retire.
    // Failures propagate and do not fabricate either acknowledgement.
    void stop_all_and_wait();
private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace openrc::runtime
