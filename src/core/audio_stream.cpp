#include "openrc/audio_stream.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace openrc {
namespace {
constexpr std::array<std::byte,8> magic{std::byte{'O'},std::byte{'R'},std::byte{'A'},std::byte{'U'},
  std::byte{'S'},std::byte{'T'},std::byte{'R'},std::byte{'1'}};
constexpr std::uint64_t fixed_bytes=64U+48U+256U*4U*2U;
[[noreturn]] void fail(const char* s){throw AudioStreamError(s);}
void put(std::vector<std::byte>& out,std::uint64_t value,unsigned width=4) {
  for(unsigned i=0;i<width;++i){out.push_back(static_cast<std::byte>(value&255U));value>>=8U;}
}
struct Reader {
  std::span<const std::byte> bytes;std::size_t at=0;
  std::uint64_t get(unsigned width=4) {
    if(at>bytes.size()||width>bytes.size()-at)fail("Truncated audio stream");
    std::uint64_t v=0;for(unsigned i=0;i<width;++i)v|=std::uint64_t(std::to_integer<unsigned>(bytes[at++]))<<(8U*i);return v;
  }
};
void shape(std::uint32_t rate,std::uint32_t phase,std::uint32_t coefficient,std::uint32_t gain,
    std::uint64_t count,std::uint64_t begin,std::uint64_t end,AudioStreamLimitsV1 limits) {
  if(rate<8000U||rate>192000U||!phase||phase>(1U<<24U)||!coefficient||coefficient>(1U<<30U)||
      !gain||gain>(1U<<30U)||!count||count>limits.max_input_samples||limits.max_bytes<fixed_bytes||
      count>(limits.max_bytes-fixed_bytes)/2U||!limits.max_phase_increment||!limits.max_render_frames||
      (end?(begin>=end||end!=count):(begin!=0||count<4U)))
    fail("Audio stream shape or repeat exceeds its neutral limits");
}
std::int64_t down(std::int64_t value,std::uint32_t denominator) {
  // Four PCM16/coefficient products sum to at most 2^32 in magnitude.
  // Admitted envelope/gains are at most 2^30 and cannot amplify after their
  // division, so every multiply is at most 2^62. Negation and this floor's
  // added denominator therefore remain representable, including the extrema.
  return value>=0?value/denominator:-((-value+denominator-1U)/denominator);
}
std::uint64_t input_index(std::uint64_t index,const AudioStreamV1& resource) {
  if(resource.repeat_end&&index>=resource.repeat_end)
    return resource.repeat_begin+(index-resource.repeat_end)%(resource.repeat_end-resource.repeat_begin);
  return index;
}
} // namespace

void validate_audio_stream_v1(const AudioStreamV1& value,AudioStreamLimitsV1 limits) {
  shape(value.output_sample_rate,value.phase_denominator,value.coefficient_denominator,value.gain_denominator,
      value.input_samples.size(),value.repeat_begin,value.repeat_end,limits);
}
std::vector<std::byte> encode_audio_stream_v1(const AudioStreamV1& value,AudioStreamLimitsV1 limits) {
  validate_audio_stream_v1(value,limits);std::vector<std::byte> body;
  body.reserve(static_cast<std::size_t>(fixed_bytes-64U+value.input_samples.size()*2U));
  put(body,value.output_sample_rate);put(body,value.phase_denominator);put(body,value.coefficient_denominator);put(body,value.gain_denominator);
  put(body,value.input_samples.size(),8);put(body,value.repeat_begin,8);put(body,value.repeat_end,8);put(body,0,8);
  for(const auto& row:value.coefficients)for(auto sample:row)put(body,std::bit_cast<std::uint16_t>(sample),2);
  for(auto sample:value.input_samples)put(body,std::bit_cast<std::uint16_t>(sample),2);
  std::vector<std::byte> out(magic.begin(),magic.end());put(out,1);put(out,64);put(out,64U+body.size(),8);put(out,0,8);
  const auto hash=prepared_content_sha256_v1(body);out.insert(out.end(),hash.begin(),hash.end());
  out.insert(out.end(),body.begin(),body.end());return out;
}
AudioStreamV1 decode_audio_stream_v1(std::span<const std::byte> bytes,AudioStreamLimitsV1 limits) {
  if(bytes.size()<fixed_bytes||bytes.size()>limits.max_bytes||!std::equal(magic.begin(),magic.end(),bytes.begin()))
    fail("Invalid audio stream envelope");
  Reader in{bytes,8};
  if(in.get()!=1||in.get()!=64||in.get(8)!=bytes.size()||in.get(8))fail("Invalid audio stream header");
  const auto hash=prepared_content_sha256_v1(bytes.subspan(64));
  if(!std::equal(hash.begin(),hash.end(),bytes.begin()+32))fail("Audio stream digest mismatch");
  in.at=64;AudioStreamV1 out;
  out.output_sample_rate=static_cast<std::uint32_t>(in.get());out.phase_denominator=static_cast<std::uint32_t>(in.get());
  out.coefficient_denominator=static_cast<std::uint32_t>(in.get());out.gain_denominator=static_cast<std::uint32_t>(in.get());
  const auto count=in.get(8);out.repeat_begin=in.get(8);out.repeat_end=in.get(8);
  if(in.get(8))fail("Invalid audio stream flags");
  shape(out.output_sample_rate,out.phase_denominator,out.coefficient_denominator,out.gain_denominator,count,out.repeat_begin,out.repeat_end,limits);
  if((bytes.size()-fixed_bytes)%2U||count!=(bytes.size()-fixed_bytes)/2U||count>out.input_samples.max_size())
    fail("Audio stream samples do not partition the payload");
  for(auto& row:out.coefficients)for(auto& sample:row)sample=std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(in.get(2)));
  out.input_samples.resize(static_cast<std::size_t>(count));
  for(auto& sample:out.input_samples)sample=std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(in.get(2)));
  return out;
}

AudioStreamPlayerV1::AudioStreamPlayerV1(AudioStreamV1 resource,AudioStreamLimitsV1 limits):
    resource_(std::move(resource)),limits_(limits) {validate_audio_stream_v1(resource_,limits_);}
const AudioStreamPlaybackStateV1& AudioStreamPlayerV1::state()const noexcept{return state_;}
std::uint32_t AudioStreamPlayerV1::output_sample_rate()const noexcept{return resource_.output_sample_rate;}
void AudioStreamPlayerV1::reset()noexcept{state_={};}
AudioStreamRenderResultV1 AudioStreamPlayerV1::render(
    std::span<std::int16_t> output,std::span<const AudioStreamControlV1> controls) {
  const auto frames=output.size()/2U;
  if(output.size()%2U||frames>limits_.max_render_frames||(controls.size()!=1U&&controls.size()!=frames)||
      frames>std::numeric_limits<std::uint64_t>::max()-state_.rendered_frames)
    fail("Audio stream render bounds or control count differs");
  const auto gain_bound=static_cast<std::int32_t>(resource_.gain_denominator);
  for(const auto& control:controls)if(control.phase_increment>limits_.max_phase_increment||control.envelope>resource_.gain_denominator||
      control.channel_gains[0]<-gain_bound||control.channel_gains[0]>gain_bound||
      control.channel_gains[1]<-gain_bound||control.channel_gains[1]>gain_bound)
    fail("Audio stream control exceeds its qualified numeric range");
  AudioStreamRenderResultV1 result;
  while(result.frames_written<frames&&!state_.exhausted) {
    const auto& control=controls[controls.size()==1U?0U:result.frames_written];
    const auto& kernel=resource_.coefficients[std::uint64_t(state_.phase)*256U/resource_.phase_denominator];
    std::int64_t value=0;
    for(unsigned tap=0;tap<4;++tap) {
      const auto index=input_index(state_.input_cursor+tap,resource_);
      value+=down(std::int64_t(resource_.input_samples[static_cast<std::size_t>(index)])*kernel[tap],resource_.coefficient_denominator);
    }
    value=down(value*control.envelope,resource_.gain_denominator);
    for(unsigned channel=0;channel<2;++channel) {
      const auto sample=down(value*control.channel_gains[channel],resource_.gain_denominator);
      output[std::size_t(result.frames_written)*2U+channel]=static_cast<std::int16_t>(std::clamp<std::int64_t>(sample,-32768,32767));
    }
    const auto next=std::uint64_t(state_.phase)+control.phase_increment;
    state_.input_cursor=input_index(state_.input_cursor+next/resource_.phase_denominator,resource_);
    state_.phase=static_cast<std::uint32_t>(next%resource_.phase_denominator);
    if(!resource_.repeat_end&&state_.input_cursor+3U>=resource_.input_samples.size()) {
      state_.input_cursor=std::min<std::uint64_t>(state_.input_cursor,resource_.input_samples.size());state_.exhausted=true;
    }
    ++result.frames_written;++state_.rendered_frames;
  }
  result.exhausted=state_.exhausted;return result;
}
} // namespace openrc
