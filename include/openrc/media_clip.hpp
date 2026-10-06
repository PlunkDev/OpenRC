#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Neutral prepared media: standard MPEG-2 elementary video and interleaved
// PCM16 audio. No disc sectors, PES headers, private-stream IDs or PS ADPCM
// reach the runtime. Packet timestamps are in the common 90 kHz media clock;
// -1 means no timestamp was present. Decode order is retained, including B
// pictures whose presentation order differs from their byte order.
struct MediaVideoPacketV1 {
  std::int64_t presentation_time = -1;
  std::int64_t decode_time = -1;
  std::vector<std::byte> bytes;
  bool operator==(const MediaVideoPacketV1 &) const = default;
};

struct MediaClipV1 {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t frame_rate_numerator = 0;
  std::uint32_t frame_rate_denominator = 0;
  // Intended display aspect, independent of coded pixel dimensions.
  std::uint32_t display_aspect_numerator = 0;
  std::uint32_t display_aspect_denominator = 0;
  std::vector<MediaVideoPacketV1> video;
  std::uint32_t audio_sample_rate = 0;
  std::uint32_t audio_channels = 0;
  std::int64_t audio_start_time = -1;
  std::vector<std::int16_t> audio;
  bool operator==(const MediaClipV1 &) const = default;
};

struct MediaClipLimitsV1 {
  std::uint64_t max_bytes = 128U * 1024U * 1024U;
  std::uint32_t max_packets = 100'000U;
  std::uint64_t max_audio_samples = 32U * 1024U * 1024U;
  std::uint32_t max_width = 1920U;
  std::uint32_t max_height = 1088U;
};

class MediaClipError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void validate_media_clip_v1(const MediaClipV1 &clip, MediaClipLimitsV1 limits = {});
[[nodiscard]] std::vector<std::byte> encode_media_clip_v1(
    const MediaClipV1 &clip, MediaClipLimitsV1 limits = {});
[[nodiscard]] MediaClipV1 decode_media_clip_v1(
    std::span<const std::byte> bytes, MediaClipLimitsV1 limits = {});

} // namespace openrc
