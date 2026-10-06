#include "openrc/rac_frontend_object_animation.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace openrc;
void check(bool value,const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F &&f) {
  bool rejected=false;
  try { f(); } catch(const RacFrontendObjectAnimationError &) { rejected=true; }
  check(rejected,"Unsupported frontend animation domain was accepted");
}
RacFrontendObjectAnimationBankV1 bank() {
  RacFrontendObjectAnimationClipV1 moving;
  moving.rate_override_bits=0x3f000000U;
  moving.frame_references={0x100U,0x120U,0x140U,0x160U,0x180U,0x1a0U,0x1c0U};
  moving.frame_rate_bits.assign(7,0x3f000000U);
  RacFrontendObjectAnimationClipV1 still;
  still.frame_references={0x200U};still.frame_rate_bits={0};
  return {{moving,still}};
}
void boundary_and_stop() {
  auto b=bank();
  auto forward=set_rac_frontend_object_animation_v1({},b,0,0).state;
  forward.speed_bits=0x3f800000U;
  for(unsigned tick=0;tick<12;++tick) forward=step_rac_frontend_object_animation_v1(forward,b).state;
  check(forward.indices.previous_frame==6 && forward.indices.current_frame==0 &&
        forward.phase_bits==0 && forward.speed_bits==0 && forward.flags==3,
        "Forward source wrap must hold the last frame at phase zero");
  const auto held=step_rac_frontend_object_animation_v1(forward,b);
  check(held.state.indices==forward.indices && held.state.phase_bits==0 && held.state.flags==0 &&
        held.writes==std::vector<RacFrontendObjectAnimationWriteV1>{{0x20e67c,0x70,1,0}},
        "Stopped source animation must only clear event flags");
  auto reverse=set_rac_frontend_object_animation_v1({},b,0,6).state;
  reverse.speed_bits=0xbf800000U;
  for(unsigned tick=0;tick<12;++tick) reverse=step_rac_frontend_object_animation_v1(reverse,b).state;
  check(reverse.indices.previous_frame==0 && reverse.indices.current_frame==1 &&
        reverse.phase_bits==0 && reverse.speed_bits==0xbf800000U,
        "Reverse frame zero precedes the source wrap callback by one update");
  reverse=step_rac_frontend_object_animation_v1(reverse,b).state;
  check(reverse.indices.previous_frame==6 && reverse.indices.current_frame==0 &&
        reverse.phase_bits==0x3f800000U && reverse.speed_bits==0 && reverse.flags==3,
        "Reverse source wrap must hold the first frame at phase one");
}
void setter_and_static() {
  auto b=bank();RacFrontendObjectAnimationStateV1 old;
  old.phase_bits=0x3f000000U;old.speed_bits=0xbf800000U;old.flags=0xa7U;
  const auto last=set_rac_frontend_object_animation_v1(old,b,0,99);
  check(last.state.indices.previous_frame==6 && last.state.indices.current_frame==6 &&
        last.state.phase_bits==old.phase_bits && last.state.speed_bits==old.speed_bits &&
        last.state.flags==0xa5U,"Setter changed preserved phase/speed or last-frame clamp");
  auto still=set_rac_frontend_object_animation_v1({},b,1,0).state;
  still.speed_bits=0x3f800000U;still.flags=3;
  const auto update=step_rac_frontend_object_animation_v1(still,b);
  check(update.state.phase_bits==0 && update.state.speed_bits==0x3f800000U &&
        update.state.flags==0 && update.writes.size()==1,
        "Rate-zero frame incorrectly advanced or stopped speed");
}
void domain_checks() {
  auto b=bank();auto s=set_rac_frontend_object_animation_v1({},b,0,0).state;
  for(auto bits:{0x3e800000U,0x80000000U,0x7f800000U}) {
    auto bad=s;bad.phase_bits=bits;
    rejects([&]{(void)step_rac_frontend_object_animation_v1(bad,b);});
  }
  auto bad=s;bad.speed_bits=0x3f000000U;
  rejects([&]{(void)step_rac_frontend_object_animation_v1(bad,b);});
  bad=s;bad.references.previous_frame_reference=0x120U;
  rejects([&]{(void)step_rac_frontend_object_animation_v1(bad,b);});
  bad=s;bad.indices.previous_sequence=255;
  rejects([&]{(void)step_rac_frontend_object_animation_v1(bad,b);});
  rejects([&]{(void)set_rac_frontend_object_animation_v1(s,b,0,-1);});
  b.clips[0].frame_rate_bits[3]=0x3eaaaaabU;
  rejects([&]{(void)set_rac_frontend_object_animation_v1(s,b,0,0);});
}

void source_comparison(const char *path) {
  std::ifstream in(path,std::ios::binary|std::ios::ate);
  check(bool(in),"Cannot open frontend animation source artifact");
  const auto length=in.tellg();check(length>=8 && length<=4*1024*1024,"Source artifact exceeds limit");
  in.seekg(0);std::vector<std::byte> bytes(static_cast<std::size_t>(length));
  in.read(reinterpret_cast<char*>(bytes.data()),length);check(bool(in),"Cannot read source artifact");
  for(unsigned i=0;i<8;++i) check(bytes[i]==std::byte("FROANIM1"[i]),"Wrong animation artifact magic");
  std::size_t cursor=8;
  auto word=[&]() {
    check(cursor<=bytes.size() && bytes.size()-cursor>=4,"Truncated animation artifact");
    std::uint32_t result=0;
    for(unsigned i=0;i<4;++i) result|=std::to_integer<std::uint32_t>(bytes[cursor++])<<(8*i);
    return result;
  };
  const auto n=word();check(n<=bytes.size()-cursor,"Truncated original class");
  const auto b=decode_rac_frontend_object_animation_bank_v1(std::span(bytes).subspan(cursor,n),1024*1024);
  cursor+=n;
  auto state=[&]() {
    RacFrontendObjectAnimationStateV1 s;
    s.indices.previous_frame=static_cast<std::uint8_t>(word());
    s.indices.current_frame=static_cast<std::uint8_t>(word());
    s.indices.previous_sequence=static_cast<std::uint8_t>(word());
    s.indices.current_sequence=static_cast<std::uint8_t>(word());
    s.references.previous_frame_reference=word();s.references.current_frame_reference=word();
    s.references.sound_byte=static_cast<std::uint8_t>(word());
    s.references.trigger_byte=static_cast<std::uint8_t>(word());
    s.phase_bits=word();s.speed_bits=word();s.rate_bits=word();
    s.flags=static_cast<std::uint8_t>(word());s.sound_handle=static_cast<std::uint8_t>(word());return s;
  };
  const auto count=word();check(count<=20000,"Too many source animation cases");
  std::uint64_t total_writes=0;
  for(unsigned i=0;i<count;++i) {
    const auto kind=word(),sequence=word(),frame=word();
    const auto before=state(),after=state();
    const auto writes=word();check(writes<=32,"Too many source writes");
    std::vector<RacFrontendObjectAnimationWriteV1> effects;
    for(unsigned j=0;j<writes;++j) {
      const auto pc=word(),offset=word(),width=word(),value=word();
      effects.push_back({pc,static_cast<std::uint16_t>(offset),static_cast<std::uint8_t>(width),value});
    }
    const auto native=kind==0 ? set_rac_frontend_object_animation_v1(before,b,sequence,static_cast<std::int32_t>(frame))
                              : step_rac_frontend_object_animation_v1(before,b);
    if(native.state!=after || native.writes!=effects)
      throw std::runtime_error("Source/native frontend animation differs in case "+std::to_string(i));
    total_writes+=writes;
  }
  check(cursor==bytes.size(),"Trailing animation source artifact bytes");
  std::cout<<"frontend_object_animation_source: "<<count<<" cases, "<<total_writes<<" writes matched\n";
}
} // namespace
int main(int argc,char **argv) {
  try {
    boundary_and_stop();setter_and_static();domain_checks();
    check(argc<=2,"Expected optional source artifact path");
    if(argc==2) source_comparison(argv[1]);
    std::cout<<"rac_frontend_object_animation_tests: 3 groups passed\n";return 0;
  } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
