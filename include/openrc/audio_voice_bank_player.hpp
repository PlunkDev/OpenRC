#pragma once

#include "openrc/audio_gain_table.hpp"
#include "openrc/audio_voice_bank.hpp"
#include "openrc/audio_voice_mixer.hpp"

namespace openrc {

struct AudioVoiceBankStreamV1 { std::string resource_id; AudioStreamV1 stream; };
struct AudioVoiceBankGainV1 { std::string resource_id; AudioGainTableV1 table; };
struct AudioVoiceBankPlaybackStateV1 {
    std::uint64_t next_frame = 0, program_tick = 0, random_draws = 0;
    std::size_t instances = 0, pending_starts = 0, physical_voices = 0, attached_voices = 0;
    std::uint64_t started = 0, completed = 0, retired = 0;
    bool stopped = false;
    bool operator==(const AudioVoiceBankPlaybackStateV1&) const = default;
};

// Resolves neutral resource handles and owns the program/physical-voice bridge.
// Its frame numbers are relative to one persistent host audio origin. Starting
// another program never resets the observation grid or random owner.
// Admission requires acyclic immediate stop paths within scheduler bounds;
// stop paths may not delay, wait for ownership or admit new voices.
class AudioVoiceBankPlayerV1 final {
public:
    AudioVoiceBankPlayerV1(AudioVoiceBankV1, std::string program_resource_id,
        AudioProgramBankV1, std::vector<AudioVoiceBankStreamV1>,
        std::vector<AudioVoiceBankGainV1>, std::uint64_t first_frame = 0,
        AudioVoiceMixerLimitsV1 = {}, AudioProgramLimitsV1 = {});
    ~AudioVoiceBankPlayerV1();
    AudioVoiceBankPlayerV1(const AudioVoiceBankPlayerV1&) = delete;
    AudioVoiceBankPlayerV1& operator=(const AudioVoiceBankPlayerV1&) = delete;
    [[nodiscard]] std::uint64_t admit(std::uint32_t program_key);
    void stop_instance(std::uint64_t instance);
    // Executes prepared stop paths, hard-mutes every physical tail and retires
    // the owned voices. Device buffers remain the separate device owner's job.
    void stop_all();
    [[nodiscard]] AudioVoiceBankPlaybackStateV1 state() const noexcept;
    [[nodiscard]] std::optional<AudioProgramInstanceStateV1> instance_state(std::uint64_t) const;
    void render(std::span<std::int16_t> interleaved_stereo);
private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace openrc
