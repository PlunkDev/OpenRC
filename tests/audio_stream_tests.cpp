#include "openrc/audio_stream.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <limits>

namespace {
using namespace openrc;
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
template<class F>void rejects(F&& f){try{f();}catch(const AudioStreamError&){return;}throw std::runtime_error("Invalid neutral stream accepted");}
AudioStreamV1 example() {
  AudioStreamV1 r;r.output_sample_rate=48000;r.phase_denominator=4096;r.coefficient_denominator=7;r.gain_denominator=13;
  for(unsigned i=0;i<256;++i)r.coefficients[i]={static_cast<std::int16_t>(int(i%7)-3),3,1,-2};
  r.input_samples={-32768,12345,-201,32767,17,-809,901};r.repeat_begin=3;r.repeat_end=r.input_samples.size();return r;
}
// Independent unbounded logical input position: no repeated cursor state or
// production arithmetic helper. It materializes the addressed input stream.
std::vector<std::int16_t> reference(const AudioStreamV1& r,std::span<const AudioStreamControlV1> controls) {
  const auto floor=[](std::int64_t n,std::uint32_t d){auto q=n/d;if(n%d<0)--q;return q;};
  std::vector<std::int16_t> out;std::uint64_t position=0;
  for(const auto& c:controls) {
    const auto cursor=position/r.phase_denominator;
    const auto row=(position%r.phase_denominator)*256U/r.phase_denominator;
    std::int64_t value=0;
    for(unsigned t=0;t<4;++t) {
      auto index=cursor+t;
      if(index>=r.repeat_end)index=r.repeat_begin+(index-r.repeat_begin)%(r.repeat_end-r.repeat_begin);
      value+=floor(std::int64_t(r.input_samples[index])*r.coefficients[row][t],r.coefficient_denominator);
    }
    value=floor(value*c.envelope,r.gain_denominator);
    for(auto gain:c.channel_gains)out.push_back(static_cast<std::int16_t>(std::clamp<std::int64_t>(floor(value*gain,r.gain_denominator),-32768,32767)));
    position+=c.phase_increment;
  }
  return out;
}
void codec() {
  const auto r=example();const auto bytes=encode_audio_stream_v1(r);
  check(bytes.size()==2160U+2U*r.input_samples.size()&&decode_audio_stream_v1(bytes)==r,"Stream codec lost filter, repeat or signed PCM");
  check(bytes[2160]==std::byte{0}&&bytes[2161]==std::byte{128},"Neutral stream samples are not little endian signed PCM");
  auto rehash=[](std::vector<std::byte>& b){const auto h=prepared_content_sha256_v1(std::span(b).subspan(64));std::copy(h.begin(),h.end(),b.begin()+32);};
  for(std::size_t size=0;size<bytes.size();++size)rejects([&]{(void)decode_audio_stream_v1(std::span(bytes).first(size));});
  auto bad=bytes;bad.back()^=std::byte{1};rejects([&]{(void)decode_audio_stream_v1(bad);});
  for(auto offset:{8U,24U,104U}) {bad=bytes;bad[offset]=std::byte{2};rehash(bad);rejects([&]{(void)decode_audio_stream_v1(bad);});}
  for(auto offset:{64U,68U,72U,76U,80U}) {
    bad=bytes;std::fill_n(bad.begin()+offset,offset==80?8U:4U,std::byte{0});rehash(bad);
    rejects([&]{(void)decode_audio_stream_v1(bad);});
  }
  bad=bytes;bad[88]=std::byte{7};rehash(bad);rejects([&]{(void)decode_audio_stream_v1(bad);});
  bad=bytes;bad[96]=std::byte{6};rehash(bad);rejects([&]{(void)decode_audio_stream_v1(bad);});
  bad=bytes;bad.push_back(std::byte{});rejects([&]{(void)decode_audio_stream_v1(bad);});
  auto limits=AudioStreamLimitsV1{};limits.max_input_samples=6;rejects([&]{(void)decode_audio_stream_v1(bytes,limits);});
  limits={};limits.max_bytes=bytes.size()-1;rejects([&]{(void)decode_audio_stream_v1(bytes,limits);});
  auto shape=r;shape.repeat_end=0;rejects([&]{validate_audio_stream_v1(shape);});shape.repeat_begin=0;
  check(decode_audio_stream_v1(encode_audio_stream_v1(shape))==shape,"Finite stream roundtrip differs");
  shape.input_samples.resize(3);rejects([&]{validate_audio_stream_v1(shape);});
  shape=r;shape.phase_denominator=(1U<<24U)+1U;rejects([&]{validate_audio_stream_v1(shape);});
  shape=r;shape.coefficient_denominator=(1U<<30U)+1U;rejects([&]{validate_audio_stream_v1(shape);});
  shape=r;shape.gain_denominator=(1U<<30U)+1U;rejects([&]{validate_audio_stream_v1(shape);});
}
void untrusted_partitions() {
  const auto resource=example();const auto bytes=encode_audio_stream_v1(resource);
  const auto put=[](std::vector<std::byte>& b,std::size_t at,std::uint64_t value,unsigned width) {
    for(unsigned i=0;i<width;++i){b[at+i]=static_cast<std::byte>(value&255U);value>>=8U;}
  };
  const auto rehash=[](std::vector<std::byte>& b) {
    const auto h=prepared_content_sha256_v1(std::span(b).subspan(64));
    std::copy(h.begin(),h.end(),b.begin()+32);
  };
  auto exact=AudioStreamLimitsV1{};exact.max_bytes=bytes.size();exact.max_input_samples=resource.input_samples.size();
  check(decode_audio_stream_v1(bytes,exact)==resource,"Exact admitted codec bounds rejected a stream");
  // Recompute both size and digest: these cases must reach the structural
  // checks rather than being rejected only by the outer envelope.
  for(const auto extra:{1U,2U}) {
    auto bad=bytes;bad.resize(bad.size()+extra);put(bad,16,bad.size(),8);rehash(bad);
    rejects([&]{(void)decode_audio_stream_v1(bad);});
  }
  for(const auto count:{std::uint64_t{4},std::uint64_t{8},std::numeric_limits<std::uint64_t>::max()}) {
    auto bad=bytes;put(bad,80,count,8);put(bad,88,0,8);put(bad,96,0,8);rehash(bad);
    auto wide=AudioStreamLimitsV1{};
    wide.max_bytes=std::numeric_limits<std::uint64_t>::max();wide.max_input_samples=wide.max_bytes;
    rejects([&]{(void)decode_audio_stream_v1(bad,wide);});
  }
  for(const auto field:{12U,24U,104U}) {
    auto bad=bytes;put(bad,field,field==12?65U:(std::uint64_t{1}<<63U),field==12?4U:8U);rehash(bad);
    rejects([&]{(void)decode_audio_stream_v1(bad);});
  }
  for(const auto field:{64U,68U,72U,76U}) {
    auto bad=bytes;put(bad,field,std::numeric_limits<std::uint32_t>::max(),4);rehash(bad);
    rejects([&]{(void)decode_audio_stream_v1(bad);});
  }
  for(const auto field:{88U,96U}) {
    auto bad=bytes;put(bad,field,std::numeric_limits<std::uint64_t>::max(),8);rehash(bad);
    rejects([&]{(void)decode_audio_stream_v1(bad);});
  }
  auto bad=bytes;bad[0]^=std::byte{1};rejects([&]{(void)decode_audio_stream_v1(bad);});
  auto limits=AudioStreamLimitsV1{};limits.max_bytes=2159;
  rejects([&]{validate_audio_stream_v1(resource,limits);});
  limits={};limits.max_phase_increment=0;rejects([&]{validate_audio_stream_v1(resource,limits);});
  limits={};limits.max_render_frames=0;rejects([&]{validate_audio_stream_v1(resource,limits);});
}
void looping_and_chunks() {
  const auto resource=example();constexpr unsigned frames=8193;
  std::vector<AudioStreamControlV1> controls;
  for(unsigned i=0;i<frames;++i)controls.push_back({i%23?999U+i%9000U:0U,i%14U,
      {13-static_cast<std::int32_t>(i%27U),static_cast<std::int32_t>((i*3U)%27U)-13}});
  controls[117].phase_increment=1U<<24U;
  const auto expected=reference(resource,controls);std::vector<std::int16_t> full(frames*2),chunked(frames*2);
  AudioStreamPlayerV1 whole(resource),pieces(resource);
  check(whole.output_sample_rate()==48000,"Player lost prepared output rate");
  const auto result=whole.render(full,controls);check(result.frames_written==frames&&!result.exhausted&&full==expected,"Per-frame control/loop/filter source order differs");
  constexpr std::array widths{1U,7U,400U,1024U,19U};unsigned at=0,index=0;
  while(at<frames) {
    const auto count=std::min(widths[index++%widths.size()],frames-at);
    const auto made=pieces.render(std::span(chunked).subspan(at*2U,count*2U),std::span(controls).subspan(at,count));
    check(made.frames_written==count&&!made.exhausted,"Chunk ended looping input");at+=count;
  }
  check(chunked==expected&&pieces.state()==whole.state(),"Arbitrary chunk boundaries changed rational phase or samples");
  check(whole.state().input_cursor>=resource.repeat_begin&&whole.state().input_cursor<resource.repeat_end&&whole.state().phase!=0,
      "Repeat cursor or residual phase was reset");
  const AudioStreamControlV1 constant{1882,13,{13,7}};
  controls.assign(frames,constant);whole.reset();pieces.reset();
  (void)whole.render(full,controls);const std::array one{constant};(void)pieces.render(chunked,one);
  check(full==chunked&&whole.state()==pieces.state()&&full==reference(resource,controls),"Constant control differs from per-frame controls");
  auto short_loop=resource;short_loop.input_samples={321};short_loop.repeat_begin=0;short_loop.repeat_end=1;
  short_loop.coefficient_denominator=1;for(auto& row:short_loop.coefficients)row={1,1,1,1};
  AudioStreamPlayerV1 short_player(short_loop);std::array<std::int16_t,4> two{};
  (void)short_player.render(two,one);check(two[0]==1284&&two[2]==1284,"Four lookahead taps do not wrap a one-sample repeat");
}
void rounding_and_extremes() {
  auto r=example();r.input_samples={1,1,1,1};r.repeat_begin=0;r.repeat_end=4;r.coefficient_denominator=2;r.gain_denominator=1;
  for(auto& row:r.coefficients)row={-1,1,0,0};
  AudioStreamPlayerV1 p(r);std::array<std::int16_t,2> output{};const std::array control{AudioStreamControlV1{0,1,{1,1}}};
  (void)p.render(output,control);check(output[0]==-1&&output[1]==-1,"Signed tap floor was moved after summation");
  r.input_samples={-1,0,0,0};r.coefficient_denominator=1;r.gain_denominator=3;
  for(auto& row:r.coefficients)row={1,0,0,0};
  p=AudioStreamPlayerV1(r);const std::array fractional{AudioStreamControlV1{0,1,{1,2}}};
  (void)p.render(output,fractional);check(output[0]==-1&&output[1]==-1,"Signed envelope/channel floors truncated toward zero");
  r.input_samples.assign(4,-32768);r.coefficient_denominator=1;r.gain_denominator=1U<<30U;
  const std::array wide{AudioStreamControlV1{0,1U<<30U,{1U<<30U,1U<<30U}}};
  for(auto& row:r.coefficients)row={-32768,-32768,-32768,-32768};
  p=AudioStreamPlayerV1(r);(void)p.render(output,wide);check(output[0]==32767&&output[1]==32767,"Positive wide intermediates overflowed or missed final clamp");
  for(auto& row:r.coefficients)row={32767,32767,32767,32767};
  p=AudioStreamPlayerV1(r);(void)p.render(output,wide);check(output[0]==-32768&&output[1]==-32768,"Negative wide intermediates overflowed or missed final clamp");
  const std::array quiet{AudioStreamControlV1{0,1,{1U<<30U,1U<<29U}}};
  p=AudioStreamPlayerV1(r);(void)p.render(output,quiet);
  check(output[0]==-4&&output[1]==-2,"Negative intermediate was clipped before envelope/channel attenuation");
  for(auto& row:r.coefficients)row={-32768,-32768,-32768,-32768};
  p=AudioStreamPlayerV1(r);(void)p.render(output,quiet);
  check(output[0]==4&&output[1]==2,"Positive intermediate was clipped before envelope/channel attenuation");
}
void signed_channel_gains() {
  auto r=example();r.input_samples={1,-1,0,0};r.repeat_begin=0;r.repeat_end=4;
  r.coefficient_denominator=1;r.gain_denominator=3;
  for(auto& row:r.coefficients)row={1,0,0,0};
  AudioStreamPlayerV1 player(r);std::array<std::int16_t,4> output{};
  const std::array controls{AudioStreamControlV1{4096,3,{-1,1}},AudioStreamControlV1{4096,3,{-1,1}}};
  (void)player.render(output,controls);
  check(output==std::array<std::int16_t,4>{-1,0,0,-1},"Negative channel gains lost signed floor order");
  r.input_samples.assign(4,-32768);r.gain_denominator=1U<<30U;
  for(auto& row:r.coefficients)row={-32768,-32768,-32768,-32768};
  const std::array wide{AudioStreamControlV1{0,1U<<30U,{-1073741824,1073741824}}};
  player=AudioStreamPlayerV1(r);(void)player.render(output,wide);
  check(output[0]==-32768&&output[1]==32767,"Signed maximum gains overflowed before final clipping");
  const auto before=player.state();const auto initial=output;
  for(const auto invalid:{-1073741825,1073741825,std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()}) {
    std::array<AudioStreamControlV1,2> bad{wide[0],wide[0]};bad.back().channel_gains[0]=invalid;
    rejects([&]{(void)player.render(output,bad);});
    check(output==initial&&player.state()==before,"Invalid signed gain partly mutated output/state");
  }
  // A negative gain inverts the post-envelope value, including its earlier
  // signed floor. Moving inversion ahead of that stage changes this result.
  r.input_samples={-1,0,0,0};r.gain_denominator=3;
  for(auto& row:r.coefficients)row={1,0,0,0};
  player=AudioStreamPlayerV1(r);const std::array fractional{AudioStreamControlV1{0,1,{-3,3}}};
  (void)player.render(output,fractional);
  check(output[0]==1&&output[1]==-1,"Channel inversion moved ahead of envelope floor");
}
void phase_domain_edges() {
  auto r=example();r.input_samples={100,100,100,100,100};r.repeat_begin=2;r.repeat_end=5;
  r.coefficient_denominator=1;r.gain_denominator=1;
  for(unsigned row=0;row<256;++row)r.coefficients[row]={static_cast<std::int16_t>(row),0,0,0};
  const std::array controls{
    AudioStreamControlV1{1,1,{1,1}},AudioStreamControlV1{1,1,{1,1}},
    AudioStreamControlV1{1,1,{1,1}},AudioStreamControlV1{1,1,{1,1}},
    AudioStreamControlV1{std::numeric_limits<std::uint32_t>::max(),1,{1,1}},
    AudioStreamControlV1{std::numeric_limits<std::uint32_t>::max(),1,{1,1}},
    AudioStreamControlV1{0,1,{1,1}}};
  auto limits=AudioStreamLimitsV1{};limits.max_phase_increment=std::numeric_limits<std::uint32_t>::max();
  for(const auto denominator:{1U,3U,257U,1U<<24U}) {
    r.phase_denominator=denominator;AudioStreamPlayerV1 player(r,limits);
    std::array<std::int16_t,controls.size()*2> output{};
    (void)player.render(output,controls);const auto expected=reference(r,controls);
    check(std::equal(output.begin(),output.end(),expected.begin()),"Phase row selection or wide cursor increment lost precision");
    std::uint64_t absolute=0;for(const auto& control:controls)absolute+=control.phase_increment;
    auto cursor=absolute/denominator;
    if(cursor>=r.repeat_end)cursor=r.repeat_begin+(cursor-r.repeat_begin)%(r.repeat_end-r.repeat_begin);
    check(player.state().phase==absolute%denominator&&player.state().input_cursor==cursor,
        "Maximum phase increment changed residual phase or repeat position");
  }
}
void finite_stationary_and_resume() {
  auto r=example();r.repeat_begin=0;r.repeat_end=0;r.coefficient_denominator=1;r.gain_denominator=1;
  r.input_samples={100,200,300,400};for(auto& row:r.coefficients)row={1,0,0,0};
  AudioStreamPlayerV1 player(r);std::array<std::int16_t,12> output{};
  const std::array stationary{AudioStreamControlV1{0,1,{1,1}}};
  const auto held=player.render(output,stationary);
  check(held.frames_written==6&&!held.exhausted&&player.state().input_cursor==0&&player.state().rendered_frames==6,
      "Finite zero-increment voice spuriously advanced or exhausted");
  check(std::all_of(output.begin(),output.end(),[](auto sample){return sample==100;}),"Stationary finite voice changed its input sample");
  const std::array muted{AudioStreamControlV1{0,0,{1,1}}};
  (void)player.render(output,muted);
  check(std::all_of(output.begin(),output.end(),[](auto sample){return sample==0;})&&player.state().rendered_frames==12,
      "Stationary voice ignored later envelope controls or its output clock");
  const std::array resume{AudioStreamControlV1{4095,1,{1,1}},AudioStreamControlV1{1,1,{1,1}}};
  output.fill(1234);const auto ended=player.render(std::span(output).first(4),resume);
  check(ended.frames_written==2&&ended.exhausted&&player.state().input_cursor==1&&player.state().phase==0,
      "Finite voice did not retain sub-sample phase across hold/resume");
  check(std::all_of(output.begin(),output.begin()+4,[](auto sample){return sample==100;})&&output[4]==1234,
      "Finite hold/resume changed the final lookahead sample or output boundary");
  const auto before=player.state();
  const auto samples=output;const std::array bad{AudioStreamControlV1{0,2,{1,1}}};
  rejects([&]{(void)player.render(output,bad);});
  check(player.state()==before&&output==samples,"Exhaustion bypassed atomic control validation");
  player.reset();check(player.state()==AudioStreamPlaybackStateV1{},"Reset failed after finite hold/resume");
  const std::array leap{AudioStreamControlV1{1U<<24U,0,{1,1}}};
  output.fill(1234);const auto jumped=player.render(output,leap);
  check(jumped.frames_written==1&&jumped.exhausted&&player.state().input_cursor==4&&output[0]==0&&output[1]==0&&output[2]==1234,
      "Muted large increment failed to consume finite input or overwrote unused output");
}
void finite_and_atomic_errors() {
  auto r=example();r.repeat_begin=0;r.repeat_end=0;r.coefficient_denominator=1;r.gain_denominator=1;
  for(auto& row:r.coefficients)row={1,0,0,0};
  AudioStreamPlayerV1 p(r);const std::array control{AudioStreamControlV1{4096,1,{1,1}}};std::array<std::int16_t,16> out{};out.fill(1234);
  const auto result=p.render(out,control);
  check(result.frames_written==4&&result.exhausted&&p.state().rendered_frames==4,"Finite input exhausted at the wrong lookahead boundary");
  for(unsigned i=0;i<4;++i)check(out[2*i]==r.input_samples[i]&&out[2*i+1]==r.input_samples[i],"Finite input sample order changed");
  check(std::all_of(out.begin()+8,out.end(),[](auto v){return v==1234;}),"Exhaustion overwrote unrendered output");
  check(p.render(out,control).frames_written==0,"Ended stream produced another frame");p.reset();check(p.state()==AudioStreamPlaybackStateV1{},"Reset retained phase/end state");
  auto unchanged=p.state();const auto initial=out;
  std::array<AudioStreamControlV1,8> bad;bad.fill(control[0]);bad.back().channel_gains[1]=2;
  rejects([&]{(void)p.render(out,bad);});check(out==initial&&p.state()==unchanged,"Late invalid control partly mutated render state/output");
  bad.back()=control[0];bad.back().phase_increment=(1U<<24U)+1U;
  rejects([&]{(void)p.render(out,bad);});check(out==initial&&p.state()==unchanged,"Invalid pitch partly mutated output");
  rejects([&]{(void)p.render(std::span(out).first(3),control);});rejects([&]{(void)p.render(out,std::span(bad).first(2));});
  check(out==initial&&p.state()==unchanged,"Invalid output/control geometry mutated state");
  check(p.render({},{}).frames_written==0&&p.state()==unchanged,"Empty rendering advanced a stream");
  auto limits=AudioStreamLimitsV1{};limits.max_render_frames=7;AudioStreamPlayerV1 small(r,limits);
  rejects([&]{(void)small.render(out,control);});check(small.state()==AudioStreamPlaybackStateV1{},"Frame bound failure advanced stream");
}
} // namespace
int main()try {
  codec();untrusted_partitions();looping_and_chunks();rounding_and_extremes();signed_channel_gains();phase_domain_edges();
  finite_stationary_and_resume();finite_and_atomic_errors();
  std::cout<<"8 neutral audio stream groups PASS: codec and untrusted partitions, phase/loop/chunks and domain edges, signed floors and channel polarity, finite hold/resume, bounded atomic rendering\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
