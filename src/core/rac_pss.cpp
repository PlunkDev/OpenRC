#include "openrc/rac_pss.hpp"
#include "openrc/ps_adpcm.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>

namespace openrc {
namespace {
[[noreturn]] void fail(const char *message) { throw MediaClipError(message); }
std::uint32_t u8(std::span<const std::byte> b, std::size_t p) {
  if (p >= b.size()) fail("Truncated PSS field");
  return std::to_integer<std::uint32_t>(b[p]);
}
std::uint32_t le32(std::span<const std::byte> b, std::size_t p) {
  return u8(b,p) | (u8(b,p+1)<<8U) | (u8(b,p+2)<<16U) | (u8(b,p+3)<<24U);
}
std::int64_t timestamp(std::span<const std::byte> b, std::size_t p, unsigned tag) {
  if ((u8(b,p)>>4U) != tag || !(u8(b,p)&1U) || !(u8(b,p+2)&1U) || !(u8(b,p+4)&1U))
    fail("Invalid PSS timestamp markers");
  return (std::int64_t((u8(b,p)>>1U)&7U)<<30U) |
         (std::int64_t(u8(b,p+1))<<22U) |
         (std::int64_t(u8(b,p+2)>>1U)<<15U) |
         (std::int64_t(u8(b,p+3))<<7U) | (u8(b,p+4)>>1U);
}
bool tag_at(std::span<const std::byte> b, std::size_t p, const char *tag) {
  return p <= b.size() && b.size()-p >= 4 &&
         u8(b,p)==static_cast<unsigned char>(tag[0]) &&
         u8(b,p+1)==static_cast<unsigned char>(tag[1]) &&
         u8(b,p+2)==static_cast<unsigned char>(tag[2]) &&
         u8(b,p+3)==static_cast<unsigned char>(tag[3]);
}
MediaClipV1 compile(std::span<const std::byte> bytes,
                              std::uint32_t display_aspect_numerator,
                              std::uint32_t display_aspect_denominator,
                              MediaClipLimitsV1 limits,
                              std::optional<std::uint32_t> selected_channel) {
  if (bytes.empty() || bytes.size() > limits.max_bytes)
    fail("PSS source size exceeds limits");
  if (selected_channel && *selected_channel > 255U)
    fail("PSS source audio channel exceeds its byte domain");
  MediaClipV1 result;
  result.display_aspect_numerator = display_aspect_numerator;
  result.display_aspect_denominator = display_aspect_denominator;
  std::vector<std::byte> audio;
  std::vector<std::byte> sequence_prefix;
  bool ended = false, saw_pack = false, saw_audio = false;
  bool matched_audio = false;
  std::size_t position = 0;
  while (position < bytes.size()) {
    if (bytes.size()-position < 4 || u8(bytes,position) || u8(bytes,position+1) ||
        u8(bytes,position+2)!=1) fail("Invalid PSS packet start");
    const auto id = u8(bytes,position+3);
    if (id == 0xb9U) {
      position += 4;
      ended = true;
      break;
    }
    if (id == 0xbaU) {
      if ((u8(bytes,position+4)&0xc4U)!=0x44U || !(u8(bytes,position+6)&4U) ||
          !(u8(bytes,position+8)&4U) || !(u8(bytes,position+9)&1U) ||
          (u8(bytes,position+12)&3U)!=3U)
        fail("Invalid MPEG-2 pack markers");
      auto count = 14U+(u8(bytes,position+13)&7U);
      if (count > bytes.size()-position) fail("Truncated PSS pack");
      for (std::size_t i=14; i<count; ++i)
        if (u8(bytes,position+i)!=255U) fail("Invalid pack stuffing");
      position += count;
      saw_pack = true;
      continue;
    }
    if (!saw_pack) fail("PSS data precedes first pack");
    auto length = (u8(bytes,position+4)<<8U) | u8(bytes,position+5);
    if (length > bytes.size()-position-6U) fail("Truncated PSS packet");
    const auto end = position+6U+length;
    if (id == 0xbbU || id == 0xbeU) { position=end; continue; }
    if (id != 0xe0U && id != 0xbdU) fail("Unsupported PSS stream ID");
    if (length < 3 || (u8(bytes,position+6)&0xc0U)!=0x80U)
      fail("Invalid PSS PES header");
    auto header_size = u8(bytes,position+8);
    auto payload_start = position+9U+header_size;
    if (payload_start > end) fail("PSS PES header exceeds packet");
    const auto pts_dts = u8(bytes,position+7)>>6U;
    std::int64_t pts=-1, dts=-1;
    if (pts_dts == 1) fail("Invalid PSS PTS/DTS flags");
    if (pts_dts >= 2) {
      if (header_size < (pts_dts==3 ? 10U : 5U)) fail("Truncated PSS timestamp");
      pts=timestamp(bytes,position+9,pts_dts);
      if (pts_dts==3) dts=timestamp(bytes,position+14,1);
    }
    auto payload = bytes.subspan(payload_start,end-payload_start);
    if (id == 0xe0U) {
      if (result.video.size() >= limits.max_packets || payload.empty())
        fail("PSS video packet limit or empty packet");
      if (sequence_prefix.size()<256) {
        auto amount=std::min<std::size_t>(256-sequence_prefix.size(),payload.size());
        sequence_prefix.insert(sequence_prefix.end(),payload.begin(),payload.begin()+amount);
      }
      result.video.push_back({pts,dts,{payload.begin(),payload.end()}});
    } else {
      // Sony MPEG private-stream audio: ff a1 00 <channel>, then a continuous
      // SShd/SSbd stream across PES boundaries (not an ADPCM reset per PES).
      if (payload.size()<4 || u8(payload,0)!=255 || u8(payload,1)!=0xa1 ||
          u8(payload,2)!=0 || (!selected_channel && u8(payload,3)!=0))
        fail("Unsupported PSS private audio header");
      saw_audio = true;
      if (!selected_channel || u8(payload,3)==*selected_channel) {
        if (!matched_audio) result.audio_start_time=pts;
        matched_audio = true;
        audio.insert(audio.end(),payload.begin()+4,payload.end());
      }
    }
    position=end;
  }
  if (!ended || position!=bytes.size()) fail("Missing PSS end or trailing source data");
  if (saw_audio && (!matched_audio || audio.empty()))
    fail("PSS source audio channel has no supported payload");
  if (sequence_prefix.size()<12 || !tag_at(sequence_prefix,0,"\0\0\1\xb3"))
    fail("PSS video lacks initial MPEG sequence header");
  result.width=(u8(sequence_prefix,4)<<4U)|(u8(sequence_prefix,5)>>4U);
  result.height=((u8(sequence_prefix,5)&15U)<<8U)|u8(sequence_prefix,6);
  constexpr std::array<std::uint32_t,9> rates{0,24000,24,25,30000,30,50,60000,60};
  auto rate=u8(sequence_prefix,7)&15U;
  if (!rate || rate>=rates.size()) fail("Unsupported MPEG frame rate");
  result.frame_rate_numerator=rates[rate];
  result.frame_rate_denominator=(rate==1 || rate==4 || rate==7) ? 1001U : 1U;
  // Sequence extension: preserve the standard rational cadence extension.
  for (std::size_t p=8;p+10<=sequence_prefix.size();++p) {
    if (!tag_at(sequence_prefix,p,"\0\0\1\xb5") || (u8(sequence_prefix,p+4)>>4U)!=1U) continue;
    result.width |= (((u8(sequence_prefix,p+5)&1U)<<1U)|(u8(sequence_prefix,p+6)>>7U))<<12U;
    result.height |= ((u8(sequence_prefix,p+6)>>5U)&3U)<<12U;
    result.frame_rate_numerator *= ((u8(sequence_prefix,p+9)>>5U)&3U)+1U;
    result.frame_rate_denominator *= (u8(sequence_prefix,p+9)&31U)+1U;
    break;
  }
  std::uint64_t prepared_size = 112U;
  for (const auto &packet : result.video) {
    const auto packet_size = std::uint64_t(packet.bytes.size()) + 24U;
    if (packet_size > limits.max_bytes ||
        prepared_size > limits.max_bytes - packet_size)
      fail("PSS neutral video exceeds byte limit");
    prepared_size += packet_size;
  }
  if (!audio.empty()) {
    if (audio.size()<40 || !tag_at(audio,0,"SShd") || le32(audio,4)!=24 ||
        le32(audio,8)!=16 || !tag_at(audio,32,"SSbd") || le32(audio,36)!=audio.size()-40)
      fail("Unsupported PSS SShd/SSbd audio envelope");
    result.audio_sample_rate=le32(audio,12);
    result.audio_channels=le32(audio,16);
    if (result.audio_sample_rate < 8000U || result.audio_sample_rate > 192000U)
      fail("Unsupported PSS audio sample rate");
    const auto block=le32(audio,20);
    if ((result.audio_channels!=1 && result.audio_channels!=2) || !block || block%16 ||
        block>1024U || (audio.size()-40)%(block*result.audio_channels))
      fail("Unsupported PSS audio channel interleave");
    const auto frames=(audio.size()-40)/16U;
    if (frames > limits.max_audio_samples/28U) fail("PSS decoded audio exceeds sample limit");
    if (frames * 28U > (limits.max_bytes - prepared_size) / 2U)
      fail("PSS neutral audio exceeds byte limit");
    std::vector<std::vector<std::int16_t>> channels;
    for (std::uint32_t channel=0;channel<result.audio_channels;++channel) {
      std::vector<std::byte> encoded;
      for (std::size_t p=40U+channel*block;p<audio.size();p+=block*result.audio_channels)
        encoded.insert(encoded.end(),audio.begin()+p,audio.begin()+p+block);
      channels.push_back(decode_ps_adpcm(encoded,{limits.max_bytes,frames,limits.max_audio_samples}).samples);
    }
    result.audio.reserve(frames*28U);
    for (std::size_t i=0;i<channels.front().size();++i)
      for (const auto &channel:channels) result.audio.push_back(channel[i]);
  }
  validate_media_clip_v1(result,limits);
  return result;
}
} // namespace

MediaClipV1 compile_rac_pss_v1(std::span<const std::byte> bytes,
                              std::uint32_t display_aspect_numerator,
                              std::uint32_t display_aspect_denominator,
                              MediaClipLimitsV1 limits) {
  return compile(bytes, display_aspect_numerator, display_aspect_denominator,
                 limits, std::nullopt);
}

MediaClipV1 compile_rac_pss_v1(std::span<const std::byte> bytes,
                              std::uint32_t display_aspect_numerator,
                              std::uint32_t display_aspect_denominator,
                              MediaClipLimitsV1 limits,
                              std::uint32_t source_audio_channel) {
  return compile(bytes, display_aspect_numerator, display_aspect_denominator,
                 limits, source_audio_channel);
}
} // namespace openrc
