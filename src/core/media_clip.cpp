#include "openrc/media_clip.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>

namespace openrc {
namespace {
constexpr std::array<std::byte, 8> magic{
    std::byte{'O'}, std::byte{'R'}, std::byte{'M'}, std::byte{'E'},
    std::byte{'D'}, std::byte{'I'}, std::byte{'A'}, std::byte{'1'}};
[[noreturn]] void fail(const char *message) { throw MediaClipError(message); }
void put(std::vector<std::byte> &out, std::uint64_t value, unsigned size) {
  for (unsigned i = 0; i < size; ++i) {
    out.push_back(static_cast<std::byte>(value & 255U));
    value >>= 8U;
  }
}
struct Reader {
  std::span<const std::byte> input;
  std::size_t position = 0;
  std::span<const std::byte> take(std::uint64_t count) {
    if (count > input.size() - position) fail("Truncated media clip");
    auto result = input.subspan(position, static_cast<std::size_t>(count));
    position += static_cast<std::size_t>(count);
    return result;
  }
  std::uint64_t get(unsigned count) {
    auto data = take(count);
    std::uint64_t result = 0;
    for (unsigned i = 0; i < count; ++i)
      result |= std::uint64_t(std::to_integer<unsigned>(data[i])) << (8U * i);
    return result;
  }
};
} // namespace

void validate_media_clip_v1(const MediaClipV1 &clip, MediaClipLimitsV1 limits) {
  if (!clip.width || !clip.height || clip.width > limits.max_width ||
      clip.height > limits.max_height || (clip.width & 1U) || (clip.height & 1U))
    fail("Unsupported media video dimensions");
  if (!clip.frame_rate_numerator || !clip.frame_rate_denominator ||
      clip.frame_rate_numerator > 120'000U || clip.frame_rate_denominator > 10'000U ||
      !clip.display_aspect_numerator || !clip.display_aspect_denominator ||
      clip.display_aspect_numerator > 65535U || clip.display_aspect_denominator > 65535U)
    fail("Invalid media cadence or display aspect");
  if (clip.video.empty() || clip.video.size() > limits.max_packets)
    fail("Media video packet count exceeds limits");
  std::uint64_t size = 112U;
  for (const auto &packet : clip.video) {
    if (packet.bytes.empty() || packet.bytes.size() > UINT32_MAX ||
        packet.presentation_time < -1 || packet.decode_time < -1 ||
        packet.presentation_time >= (INT64_C(1) << 33) ||
        packet.decode_time >= (INT64_C(1) << 33))
      fail("Invalid media packet size or timestamp");
    if (packet.bytes.size() + 24U > limits.max_bytes ||
        size > limits.max_bytes - packet.bytes.size() - 24U)
      fail("Media video exceeds byte limit");
    size += packet.bytes.size() + 24U;
  }
  if (clip.audio.size() > limits.max_audio_samples || size > limits.max_bytes ||
      clip.audio.size() > (limits.max_bytes - size) / 2U)
    fail("Media audio exceeds limits");
  if (clip.audio.empty()) {
    if (clip.audio_channels || clip.audio_sample_rate || clip.audio_start_time != -1)
      fail("Empty media audio has nonempty metadata");
  } else if ((clip.audio_channels != 1 && clip.audio_channels != 2) ||
             clip.audio_sample_rate < 8000 || clip.audio_sample_rate > 192000 ||
             clip.audio.size() % clip.audio_channels || clip.audio_start_time < 0 ||
             clip.audio_start_time >= (INT64_C(1) << 33)) {
    fail("Invalid PCM media audio metadata");
  }
}

std::vector<std::byte> encode_media_clip_v1(const MediaClipV1 &clip,
                                           MediaClipLimitsV1 limits) {
  validate_media_clip_v1(clip, limits);
  std::vector<std::byte> body;
  for (auto word : {clip.width, clip.height, clip.frame_rate_numerator,
                    clip.frame_rate_denominator, clip.display_aspect_numerator,
                    clip.display_aspect_denominator, clip.audio_sample_rate,
                    clip.audio_channels}) put(body, word, 4);
  put(body, std::bit_cast<std::uint64_t>(clip.audio_start_time), 8);
  put(body, clip.audio.size(), 8);
  for (const auto &packet : clip.video) {
    put(body, std::bit_cast<std::uint64_t>(packet.presentation_time), 8);
    put(body, std::bit_cast<std::uint64_t>(packet.decode_time), 8);
    put(body, packet.bytes.size(), 4);
    put(body, 0, 4);
    body.insert(body.end(), packet.bytes.begin(), packet.bytes.end());
  }
  for (auto sample : clip.audio) put(body, std::bit_cast<std::uint16_t>(sample), 2);
  std::vector<std::byte> result(magic.begin(), magic.end());
  put(result, 1, 4); put(result, 64, 4); put(result, 64U + body.size(), 8);
  put(result, clip.video.size(), 4); put(result, 0, 4);
  auto digest = prepared_content_sha256_v1(body);
  result.insert(result.end(), digest.begin(), digest.end());
  result.insert(result.end(), body.begin(), body.end());
  return result;
}

MediaClipV1 decode_media_clip_v1(std::span<const std::byte> bytes,
                               MediaClipLimitsV1 limits) {
  if (bytes.size() > limits.max_bytes || bytes.size() < 112U)
    fail("Media clip size exceeds bounds");
  Reader in{bytes};
  auto signature = in.take(8);
  if (!std::equal(signature.begin(), signature.end(), magic.begin()) ||
      in.get(4) != 1 || in.get(4) != 64 || in.get(8) != bytes.size())
    fail("Invalid media clip header");
  auto packet_count = in.get(4);
  if (!packet_count || packet_count > limits.max_packets || in.get(4) != 0)
    fail("Invalid media packet count or flags");
  auto stored_digest = in.take(32);
  auto digest = prepared_content_sha256_v1(bytes.subspan(64));
  if (!std::equal(stored_digest.begin(), stored_digest.end(), digest.begin()))
    fail("Media clip digest mismatch");
  MediaClipV1 clip;
  clip.width = static_cast<std::uint32_t>(in.get(4));
  clip.height = static_cast<std::uint32_t>(in.get(4));
  clip.frame_rate_numerator = static_cast<std::uint32_t>(in.get(4));
  clip.frame_rate_denominator = static_cast<std::uint32_t>(in.get(4));
  clip.display_aspect_numerator = static_cast<std::uint32_t>(in.get(4));
  clip.display_aspect_denominator = static_cast<std::uint32_t>(in.get(4));
  clip.audio_sample_rate = static_cast<std::uint32_t>(in.get(4));
  clip.audio_channels = static_cast<std::uint32_t>(in.get(4));
  clip.audio_start_time = std::bit_cast<std::int64_t>(in.get(8));
  auto audio_count = in.get(8);
  if (audio_count > limits.max_audio_samples || audio_count > (bytes.size() - in.position) / 2U ||
      packet_count > (bytes.size() - in.position) / 25U)
    fail("Media sample counts exceed input");
  clip.video.reserve(static_cast<std::size_t>(packet_count));
  for (std::uint64_t i = 0; i < packet_count; ++i) {
    MediaVideoPacketV1 packet;
    packet.presentation_time = std::bit_cast<std::int64_t>(in.get(8));
    packet.decode_time = std::bit_cast<std::int64_t>(in.get(8));
    auto size = in.get(4);
    if (in.get(4) != 0) fail("Unknown media packet flags");
    auto payload = in.take(size);
    packet.bytes.assign(payload.begin(), payload.end());
    clip.video.push_back(std::move(packet));
  }
  if (audio_count != (bytes.size() - in.position) / 2U || (bytes.size() - in.position) % 2U)
    fail("Truncated or trailing media audio");
  clip.audio.reserve(static_cast<std::size_t>(audio_count));
  for (std::uint64_t i = 0; i < audio_count; ++i)
    clip.audio.push_back(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(in.get(2))));
  validate_media_clip_v1(clip, limits);
  return clip;
}
} // namespace openrc
