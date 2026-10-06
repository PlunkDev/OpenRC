#include "openrc/media_clip.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace openrc;
using Bytes = std::vector<std::byte>;
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template <class F> void rejects(F &&call) {
  try { call(); } catch (const MediaClipError &) { return; }
  throw std::runtime_error("Malformed neutral media was accepted");
}
void put(Bytes &bytes, std::size_t at, std::uint64_t value, unsigned count) {
  for (unsigned i = 0; i < count; ++i)
    bytes.at(at + i) = static_cast<std::byte>(value >> (8U * i));
}
void seal(Bytes &bytes) {
  put(bytes, 16U, bytes.size(), 8U);
  const auto digest = prepared_content_sha256_v1(std::span(bytes).subspan(64U));
  std::copy(digest.begin(), digest.end(), bytes.begin() + 32);
}
MediaClipV1 fixture() {
  MediaClipV1 clip;
  clip.width = 320U; clip.height = 240U;
  clip.frame_rate_numerator = 30000U; clip.frame_rate_denominator = 1001U;
  clip.display_aspect_numerator = 4U; clip.display_aspect_denominator = 3U;
  clip.video = {{0, 0, {std::byte{1U}, std::byte{2U}}},
                {7200, 3600, {std::byte{3U}}},
                {3600, 7200, {std::byte{4U}, std::byte{5U}}}};
  clip.audio_sample_rate = 48000U; clip.audio_channels = 2U;
  clip.audio_start_time = 4500;
  clip.audio = {-32768, -1, 0, 32767};
  return clip;
}
void roundtrip() {
  const auto clip = fixture();
  const auto encoded = encode_media_clip_v1(clip);
  check(encoded.size() == 112U + 3U * 24U + 5U + 8U,
        "Neutral extent/header size changed");
  check(decode_media_clip_v1(encoded) == clip,
        "Media round trip reordered PTS/DTS packets or changed signed PCM");
  check(encode_media_clip_v1(decode_media_clip_v1(encoded)) == encoded,
        "Neutral media encoding is nondeterministic");
  auto silent = clip;
  silent.audio.clear(); silent.audio_sample_rate = silent.audio_channels = 0U;
  silent.audio_start_time = -1;
  silent.video.front().presentation_time = -1;
  silent.video.front().decode_time = -1;
  check(decode_media_clip_v1(encode_media_clip_v1(silent)) == silent,
        "Silent or untimestamped video packet was not preserved");
}
void malformed_encoding() {
  const auto original = encode_media_clip_v1(fixture());
  for (std::size_t count = 0U; count < original.size(); ++count)
    rejects([&] { (void)decode_media_clip_v1(std::span(original).first(count)); });
  for (const auto offset : {0U, 8U, 12U, 16U, 28U, 32U, 64U, 120U}) {
    auto changed = original;
    changed[offset] ^= std::byte{0x80U};
    rejects([&] { (void)decode_media_clip_v1(changed); });
  }
  // Re-seal mutations so their rejection exercises structural validation,
  // independently of the body digest. Counts are attacker supplied, but all
  // fixtures remain below 200 bytes and must fail before large reservations.
  const std::array<std::array<std::uint64_t, 3>, 11> mutations{{
      {24U, 0xffffffffU, 4U}, {104U, UINT64_MAX, 8U},
      {128U, 0xffffffffU, 4U}, {132U, 1U, 4U},
      {64U, 321U, 4U}, {72U, 0U, 4U}, {84U, 0U, 4U},
      {92U, 3U, 4U}, {96U, UINT64_MAX - 1U, 8U},
      {112U, UINT64_C(1) << 33U, 8U}, {120U, UINT64_MAX - 1U, 8U}}};
  for (const auto &mutation : mutations) {
    auto changed = original;
    put(changed, static_cast<std::size_t>(mutation[0]), mutation[1],
        static_cast<unsigned>(mutation[2]));
    seal(changed);
    rejects([&] { (void)decode_media_clip_v1(changed); });
  }
  auto trailing = original;
  trailing.push_back(std::byte{0U}); seal(trailing);
  rejects([&] { (void)decode_media_clip_v1(trailing); });
}
void limits_and_metadata() {
  auto clip = fixture();
  const auto bytes = encode_media_clip_v1(clip);
  MediaClipLimitsV1 limits;
  limits.max_bytes = bytes.size();
  limits.max_packets = 3U; limits.max_audio_samples = 4U;
  limits.max_width = 320U; limits.max_height = 240U;
  check(decode_media_clip_v1(bytes, limits) == clip,
        "Inclusive media limits were rejected");
  for (unsigned kind = 0U; kind < 5U; ++kind) {
    auto narrow = limits;
    if (kind == 0U) --narrow.max_bytes;
    if (kind == 1U) --narrow.max_packets;
    if (kind == 2U) --narrow.max_audio_samples;
    if (kind == 3U) --narrow.max_width;
    if (kind == 4U) --narrow.max_height;
    rejects([&] { (void)decode_media_clip_v1(bytes, narrow); });
    rejects([&] { (void)encode_media_clip_v1(clip, narrow); });
  }
  clip.audio.pop_back();
  rejects([&] { validate_media_clip_v1(clip); });
  clip = fixture(); clip.video.clear();
  rejects([&] { validate_media_clip_v1(clip); });
  clip = fixture(); clip.audio.clear();
  rejects([&] { validate_media_clip_v1(clip); });
  clip = fixture(); clip.video.front().bytes.clear();
  rejects([&] { validate_media_clip_v1(clip); });
}
} // namespace
int main() {
  try {
    roundtrip(); malformed_encoding(); limits_and_metadata();
    std::cout << "media_clip_tests: 3 groups passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "media_clip_tests: " << error.what() << '\n';
    return 1;
  }
}
