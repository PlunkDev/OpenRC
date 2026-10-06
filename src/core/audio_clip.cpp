#include "openrc/audio_clip.hpp"
#include "openrc/prepared_game_v2.hpp"
#include <algorithm>
#include <array>
#include <bit>

namespace openrc {
namespace {
constexpr std::array<std::byte,8> magic{std::byte{'O'},std::byte{'R'},std::byte{'A'},std::byte{'U'},
  std::byte{'D'},std::byte{'I'},std::byte{'O'},std::byte{'1'}};
[[noreturn]] void fail(const char* message){throw AudioClipError(message);}
void put(std::vector<std::byte>& out,std::uint64_t v,unsigned n=4){
  for(unsigned i=0;i<n;++i){out.push_back(std::byte(v&255U));v>>=8U;}
}
struct Reader {
  std::span<const std::byte> bytes;std::size_t at=0;
  std::uint64_t get(unsigned n=4){
    if(n>bytes.size()-at)fail("Truncated audio clip");
    std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(std::to_integer<unsigned>(bytes[at++]))<<(i*8U);
    return v;
  }
};
void shape(std::uint32_t rate,std::uint32_t channels,std::uint64_t samples,AudioClipLimitsV1 limits){
  if((channels!=1U&&channels!=2U)||rate<8000U||rate>192000U||!samples||samples%channels||
    samples>limits.max_samples||limits.max_bytes<80U||samples>(limits.max_bytes-80U)/2U)
    fail("Audio clip shape exceeds its neutral PCM limits");
}
}
void validate_audio_clip_v1(const AudioClipV1& clip,AudioClipLimitsV1 limits){
  shape(clip.sample_rate,clip.channels,clip.samples.size(),limits);
}
std::vector<std::byte> encode_audio_clip_v1(const AudioClipV1& clip,AudioClipLimitsV1 limits){
  validate_audio_clip_v1(clip,limits);
  std::vector<std::byte> body;body.reserve(16U+clip.samples.size()*2U);
  put(body,clip.sample_rate);put(body,clip.channels);put(body,clip.samples.size(),8U);
  for(const auto sample:clip.samples)put(body,std::bit_cast<std::uint16_t>(sample),2U);
  std::vector<std::byte> out(magic.begin(),magic.end());put(out,1U);put(out,64U);put(out,64U+body.size(),8U);
  put(out,0U,8U);const auto hash=prepared_content_sha256_v1(body);out.insert(out.end(),hash.begin(),hash.end());
  out.insert(out.end(),body.begin(),body.end());return out;
}
AudioClipV1 decode_audio_clip_v1(std::span<const std::byte> bytes,AudioClipLimitsV1 limits){
  if(bytes.size()<80U||bytes.size()>limits.max_bytes||!std::equal(magic.begin(),magic.end(),bytes.begin()))
    fail("Invalid audio clip envelope");
  Reader in{bytes,8U};
  if(in.get()!=1U||in.get()!=64U||in.get(8U)!=bytes.size()||in.get(8U))fail("Invalid audio clip header");
  const auto hash=prepared_content_sha256_v1(bytes.subspan(64U));
  if(!std::equal(hash.begin(),hash.end(),bytes.begin()+32U))fail("Audio clip digest mismatch");
  in.at=64U;AudioClipV1 out;out.sample_rate=static_cast<std::uint32_t>(in.get());out.channels=static_cast<std::uint32_t>(in.get());
  const auto count=in.get(8U);shape(out.sample_rate,out.channels,count,limits);
  if(count!=(bytes.size()-80U)/2U||(bytes.size()-80U)%2U)fail("Audio samples do not partition the payload");
  out.samples.reserve(static_cast<std::size_t>(count));
  for(std::uint64_t i=0;i<count;++i)out.samples.push_back(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(in.get(2U))));
  return out;
}
}
