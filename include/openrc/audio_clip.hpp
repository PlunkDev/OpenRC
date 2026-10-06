#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {
// Neutral, finite interleaved PCM16. Source pitch, envelopes and compression
// have already been lowered by the compiler. No video owner is implied.
struct AudioClipV1 {
  std::uint32_t sample_rate=0,channels=0;
  std::vector<std::int16_t> samples;
  bool operator==(const AudioClipV1&)const=default;
};
struct AudioClipLimitsV1 {
  std::uint64_t max_bytes=64U*1024U*1024U;
  std::uint64_t max_samples=32U*1024U*1024U;
};
class AudioClipError final:public std::runtime_error {
public:using std::runtime_error::runtime_error;
};
void validate_audio_clip_v1(const AudioClipV1&,AudioClipLimitsV1={});
[[nodiscard]] std::vector<std::byte> encode_audio_clip_v1(const AudioClipV1&,AudioClipLimitsV1={});
[[nodiscard]] AudioClipV1 decode_audio_clip_v1(std::span<const std::byte>,AudioClipLimitsV1={});
}
