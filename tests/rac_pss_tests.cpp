#include "openrc/rac_pss.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace openrc;
using Bytes = std::vector<std::byte>;
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template <class F> void rejects(F &&call) {
  try { call(); } catch (const std::runtime_error &) { return; }
  throw std::runtime_error("Malformed PSS was accepted");
}
Bytes bytes(std::initializer_list<unsigned> values) {
  Bytes out;
  for (auto value : values) out.push_back(static_cast<std::byte>(value));
  return out;
}
void put(Bytes &out, std::size_t at, std::uint32_t value) {
  for (unsigned i = 0U; i < 4U; ++i)
    out.at(at + i) = static_cast<std::byte>(value >> (8U * i));
}
void append(Bytes &out, std::span<const std::byte> part) {
  out.insert(out.end(), part.begin(), part.end());
}
Bytes pack() {
  // Synthetic MPEG-2 pack with all required marker bits and no stuffing.
  return bytes({0, 0, 1, 0xba, 0x44, 0, 4, 0, 4, 1, 0, 0, 3, 0xf8});
}
Bytes sequence(unsigned rate = 3U) {
  // Coded 320x240, sequence marker, synthetic bitrate/VBV fields.
  return bytes({0, 0, 1, 0xb3, 0x14, 0, 0xf0, 0x20U | rate,
                0, 0, 0x20, 0});
}
void timestamp(Bytes &out, std::uint64_t value, unsigned tag) {
  append(out, bytes({(tag << 4U) | unsigned((value >> 29U) & 14U) | 1U,
                     unsigned(value >> 22U),
                     unsigned((value >> 14U) & 254U) | 1U,
                     unsigned(value >> 7U),
                     unsigned((value << 1U) & 254U) | 1U}));
}
void pes(Bytes &out, unsigned id, std::span<const std::byte> payload,
         std::int64_t pts = -1, std::int64_t dts = -1) {
  Bytes header;
  if (pts >= 0) timestamp(header, static_cast<std::uint64_t>(pts), dts >= 0 ? 3U : 2U);
  if (dts >= 0) timestamp(header, static_cast<std::uint64_t>(dts), 1U);
  const auto length = 3U + header.size() + payload.size();
  append(out, bytes({0, 0, 1, id, unsigned(length >> 8U), unsigned(length),
                     0x80, dts >= 0 ? 0xc0U : pts >= 0 ? 0x80U : 0U,
                     unsigned(header.size())}));
  append(out, header); append(out, payload);
}
void end(Bytes &out) { append(out, bytes({0, 0, 1, 0xb9})); }
Bytes video_only() {
  auto out = pack();
  pes(out, 0xe0U, sequence(), 9000, 0);
  end(out);
  return out;
}
Bytes audio_envelope() {
  Bytes out(40U + 128U);
  const auto header = bytes({'S', 'S', 'h', 'd'});
  std::copy(header.begin(), header.end(), out.begin());
  put(out, 4U, 24U); put(out, 8U, 16U); put(out, 12U, 48000U);
  put(out, 16U, 2U); put(out, 20U, 32U); put(out, 36U, 128U);
  const auto body = bytes({'S', 'S', 'b', 'd'});
  std::copy(body.begin(), body.end(), out.begin() + 32);
  for (unsigned block = 0U; block < 4U; ++block) {
    const auto at = 40U + block * 32U;
    const unsigned nibble = block == 0U ? 1U : block == 1U ? 15U :
                            block == 2U ? 2U : 14U;
    std::fill(out.begin() + at + 2U, out.begin() + at + 16U,
              static_cast<std::byte>(nibble * 17U));
    // Predictor history must continue into the second frame of each block.
    out[at + 16U] = std::byte{0x1cU};
  }
  return out;
}
Bytes with_audio(std::span<const std::byte> audio) {
  auto out = pack();
  pes(out, 0xe0U, sequence(), 9000, 0);
  // Both SShd and an ADPCM frame cross PES boundaries. Each PES has its own
  // Sony private header; that header must never reach the audio decoder.
  const std::array<std::size_t, 6U> cuts{0U, 7U, 43U, 81U, 113U, audio.size()};
  for (std::size_t i = 0U; i + 1U < cuts.size(); ++i) {
    auto payload = bytes({0xff, 0xa1, 0, 0});
    append(payload, audio.subspan(cuts[i], cuts[i + 1U] - cuts[i]));
    pes(out, 0xbdU, payload, i == 0U ? 4500 : -1);
  }
  end(out);
  return out;
}
void video_splits_and_timestamps() {
  const auto input = sequence(4U);
  for (std::size_t split = 1U; split < input.size(); ++split) {
    auto source = pack();
    pes(source, 0xe0U, std::span(input).first(split), 9000, 0);
    pes(source, 0xe0U, std::span(input).subspan(split), 18000, 3000);
    pes(source, 0xe0U, bytes({0, 0, 1, 0, 0, 0}), 12000, 6000);
    end(source);
    const auto clip = compile_rac_pss_v1(source, 16U, 9U);
    check(clip.width == 320U && clip.height == 240U &&
              clip.frame_rate_numerator == 30000U &&
              clip.frame_rate_denominator == 1001U &&
              clip.display_aspect_numerator == 16U &&
              clip.display_aspect_denominator == 9U,
          "Split sequence header or authored display aspect changed");
    check(clip.video.size() == 3U && clip.video[1].presentation_time == 18000 &&
              clip.video[2].presentation_time == 12000 &&
              clip.video[1].decode_time == 3000 &&
              clip.video[2].decode_time == 6000,
          "PES byte/decode order was replaced with presentation order");
    Bytes joined = clip.video[0].bytes; append(joined, clip.video[1].bytes);
    check(joined == input && clip.audio.empty() && clip.audio_start_time == -1,
          "PES headers leaked into elementary stream or invented audio");
  }
  auto sequence_extended = sequence(1U);
  append(sequence_extended, bytes({0, 0, 1, 0xb5, 0x10, 0, 0, 0, 0, 0x23}));
  auto source = pack(); pes(source, 0xe0U, sequence_extended); end(source);
  const auto clip = compile_rac_pss_v1(source, 4U, 3U);
  check(clip.frame_rate_numerator == 48000U &&
            clip.frame_rate_denominator == 4004U &&
            clip.video.front().presentation_time == -1,
        "Sequence cadence extension or missing timestamp changed");
}
void split_stereo_audio() {
  const auto source = with_audio(audio_envelope());
  const auto clip = compile_rac_pss_v1(source, 4U, 3U);
  check(clip.audio_sample_rate == 48000U && clip.audio_channels == 2U &&
            clip.audio_start_time == 4500 && clip.audio.size() == 224U,
        "Continuous Sony audio envelope or sample count changed");
  check(clip.audio[0U] == 4096 && clip.audio[1U] == -4096 &&
            clip.audio[56U] == 3840 && clip.audio[57U] == -3840 &&
            clip.audio[112U] == 8192 && clip.audio[113U] == -8192 &&
            clip.audio[168U] == 7680 && clip.audio[169U] == -7680,
        "32-byte channel blocks, PCM interleave or predictor history changed");
  check(decode_media_clip_v1(encode_media_clip_v1(clip)) == clip,
        "Compiled PSS did not survive the neutral package boundary");
}
void interleaved_source_audio_channels() {
  constexpr std::array<unsigned, 5U> channels{0U, 2U, 3U, 4U, 5U};
  std::array<Bytes, channels.size()> envelopes;
  for (std::size_t i=0; i<envelopes.size(); ++i) {
    envelopes[i]=audio_envelope();
    // Distinct, hand-authored predictor-0 first samples per language track.
    envelopes[i][42]=static_cast<std::byte>((i+1U)*17U);
  }
  auto source=pack(); pes(source,0xe0U,sequence(),9000,0);
  const std::array<std::size_t,6U> cuts{0U,7U,43U,81U,113U,envelopes[0].size()};
  for (std::size_t part=0; part+1U<cuts.size(); ++part) {
    for (std::size_t track=0; track<channels.size(); ++track) {
      auto payload=bytes({0xffU,0xa1U,0U,channels[track]});
      append(payload,std::span(envelopes[track]).subspan(cuts[part],cuts[part+1U]-cuts[part]));
      pes(source,0xbdU,payload,part==0U ? 4500+std::int64_t(track)*90 : -1);
    }
  }
  end(source);
  for (std::size_t track=0; track<channels.size(); ++track) {
    auto expected=compile_rac_pss_v1(with_audio(envelopes[track]),4U,3U);
    expected.audio_start_time=4500+std::int64_t(track)*90;
    const auto clip=compile_rac_pss_v1(source,4U,3U,{},channels[track]);
    check(clip==expected && clip.audio[0]==std::int16_t((track+1U)*4096U),
          "Interleaved language PES changed selected PCM, first PTS or video");
    check(decode_media_clip_v1(encode_media_clip_v1(clip))==clip,
          "Selected source audio failed neutral package roundtrip");
  }
  rejects([&] { (void)compile_rac_pss_v1(source,4U,3U); });
  rejects([&] { (void)compile_rac_pss_v1(source,4U,3U,{},1U); });
  rejects([&] { (void)compile_rac_pss_v1(source,4U,3U,{},256U); });
  MediaClipLimitsV1 limits; limits.max_audio_samples=223U;
  rejects([&] { (void)compile_rac_pss_v1(source,4U,3U,limits,5U); });
  limits={}; limits.max_bytes=source.size()-1U;
  rejects([&] { (void)compile_rac_pss_v1(source,4U,3U,limits,5U); });

  auto selected=pack(); pes(selected,0xe0U,sequence(),9000,0);
  // The original demux never feeds an unselected channel into SShd/ADPCM.
  // Its valid PES can therefore contain bytes which fail that decoder.
  pes(selected,0xbdU,bytes({0xffU,0xa1U,0U,2U,0xdeU,0xadU}),0);
  auto payload=bytes({0xffU,0xa1U,0U,0U}); append(payload,envelopes[0]);
  pes(selected,0xbdU,payload,4500); end(selected);
  check(compile_rac_pss_v1(selected,4U,3U,{},0U)==
            compile_rac_pss_v1(with_audio(envelopes[0]),4U,3U),
        "Unselected audio payload reached the selected decoder");
  rejects([&] { (void)compile_rac_pss_v1(selected,4U,3U,{},2U); });
  // Unsupported private framing is rejected even on an unselected channel.
  auto corrupt=selected;
  const auto prefix=bytes({0xffU,0xa1U,0U,2U});
  auto at=std::search(corrupt.begin(),corrupt.end(),prefix.begin(),prefix.end());
  check(at!=corrupt.end(),"Synthetic private header missing");
  *(at+2)=std::byte{1U};
  rejects([&] { (void)compile_rac_pss_v1(corrupt,4U,3U,{},0U); });
}
void malformed_and_limits() {
  const auto original = video_only();
  for (std::size_t size = 0U; size < original.size(); ++size)
    rejects([&] { (void)compile_rac_pss_v1(std::span(original).first(size), 4U, 3U); });
  for (const auto &mutation : std::array<std::array<unsigned, 2U>, 9U>{{
           {4U, 0U}, {17U, 0xe1U}, {18U, 0xffU}, {20U, 0U},
           {21U, 0x40U}, {22U, 4U}, {23U, 0x20U}, {25U, 0U}, {27U, 0U}}}) {
    auto changed = original;
    changed[mutation[0]] = static_cast<std::byte>(mutation[1]);
    rejects([&] { (void)compile_rac_pss_v1(changed, 4U, 3U); });
  }
  auto trailing = original; trailing.push_back(std::byte{0U});
  rejects([&] { (void)compile_rac_pss_v1(trailing, 4U, 3U); });
  for (const auto &mutation : std::array<std::array<unsigned, 2U>, 6U>{{
           {4U, 25U}, {8U, 17U}, {12U, 7999U}, {16U, 3U},
           {20U, 31U}, {36U, 0xffffffffU}}}) {
    auto audio = audio_envelope(); put(audio, mutation[0], mutation[1]);
    const auto source = with_audio(audio);
    rejects([&] { (void)compile_rac_pss_v1(source, 4U, 3U); });
  }
  const auto source = with_audio(audio_envelope());
  MediaClipLimitsV1 limits;
  limits.max_audio_samples = 223U;
  rejects([&] { (void)compile_rac_pss_v1(source, 4U, 3U, limits); });
  limits.max_audio_samples = 224U; limits.max_bytes = source.size();
  rejects([&] { (void)compile_rac_pss_v1(source, 4U, 3U, limits); });
  limits = {}; limits.max_packets = 0U;
  rejects([&] { (void)compile_rac_pss_v1(original, 4U, 3U, limits); });
  limits = {}; limits.max_width = 319U;
  rejects([&] { (void)compile_rac_pss_v1(original, 4U, 3U, limits); });
  rejects([&] { (void)compile_rac_pss_v1(original, 0U, 3U); });
}
} // namespace
int main() {
  try {
    video_splits_and_timestamps(); split_stereo_audio();
    interleaved_source_audio_channels(); malformed_and_limits();
    std::cout << "rac_pss_tests: 4 groups passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rac_pss_tests: " << error.what() << '\n';
    return 1;
  }
}
