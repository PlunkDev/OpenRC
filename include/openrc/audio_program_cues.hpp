#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

struct AudioProgramCueV1 {
    std::uint32_t key = 0, program_key = 0;
    std::uint64_t first_scene_sample = 0, last_scene_sample = 0;
    bool operator==(const AudioProgramCueV1&) const = default;
};
// Uses the owning scene's actual sample index, including its authored repeat.
// Every cue maintains one logical program owner while its inclusive window is
// active. Completed owners may be readmitted on the next scene update. The host
// supplies source-qualified owner validity at this observation boundary; queue
// acceptance by itself is not a completion or validity acknowledgement.
// Outside the window, a valid owner is stopped.
struct AudioProgramCuesV1 {
    std::string voice_bank_resource_id, timeline_resource_id;
    std::uint32_t updates_per_second = 0;
    std::vector<AudioProgramCueV1> cues;
    bool operator==(const AudioProgramCuesV1&) const = default;
};
struct AudioProgramCueLimitsV1 {
    std::uint64_t max_bytes = 1024U * 1024U;
    std::uint32_t max_cues = 256, max_id_bytes = 256;
};
class AudioProgramCueError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
enum class AudioProgramCueActionV1 { none, admit, stop };
[[nodiscard]] AudioProgramCueActionV1 audio_program_cue_action_v1(
    const AudioProgramCueV1&, std::uint64_t scene_sample, bool owner_valid);
void validate_audio_program_cues_v1(const AudioProgramCuesV1&, AudioProgramCueLimitsV1 = {});
[[nodiscard]] std::vector<std::byte> encode_audio_program_cues_v1(const AudioProgramCuesV1&, AudioProgramCueLimitsV1 = {});
[[nodiscard]] AudioProgramCuesV1 decode_audio_program_cues_v1(std::span<const std::byte>, AudioProgramCueLimitsV1 = {});

} // namespace openrc
