#include "openrc/loading_presentation.hpp"

#include <iostream>

namespace {
using namespace openrc;
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
template<class F>void rejects(F &&f){try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error("Invalid loading presentation was accepted");}
LoadingPresentationV1 fixture() {
  LoadingPresentationV1 p;p.fade_in_updates=32;p.fade_out_updates=16;
  p.library.canvas_width=8;p.library.canvas_height=8;p.library.updates_per_second=50;p.library.coverage_denominator=128;p.library.loop_begin=0;
  for(unsigned i=0;i<3;++i)p.library.images.push_back({1,1,{std::byte{7},std::byte{31},std::byte{91},static_cast<std::byte>(i==0?97:128)}});
  p.library.frames={{{{0,0,0}}},{{{0,1,0}}}};p.bands={{1,0,2,0,0,0},{2,0,4,65,64,96}};return p;
}
void codec() {
  auto p=fixture();const auto bytes=encode_loading_presentation_v1(p);
  check(decode_loading_presentation_v1(bytes)==p,"Loading presentation roundtrip differs");
  for(std::size_t n=0;n<bytes.size();++n)rejects([&]{(void)decode_loading_presentation_v1(std::span(bytes).first(n));});
  auto bad=bytes;bad.back()^=std::byte{1};rejects([&]{(void)decode_loading_presentation_v1(bad);});
  p.bands[0].label_image=0;rejects([&]{validate_loading_presentation_v1(p);});
  p=fixture();p.bands[1].reveal_ramp_end=64;rejects([&]{validate_loading_presentation_v1(p);});
  p=fixture();p.bands[0].x=INT32_MAX;rejects([&]{validate_loading_presentation_v1(p);});
}
void actual_clock_materialization() {
  const auto p=fixture();
  for(auto duration:{1U,15U,65U,66U,80U,96U,200U,231U,650U})for(unsigned frame=0;frame<duration;++frame) {
    const auto out=materialize_loading_presentation_v1(p,frame,duration);
    unsigned alpha=frame<=31U?frame*4U:128U;
    if(std::int64_t(frame)>std::int64_t(duration)-16)alpha=(duration-frame)*8U;
    check(out.frames[0].draws.size()==(frame>=65?4U:2U)&&out.frames[0].draws[0].x==static_cast<std::int32_t>(frame%2),"Neutral reveal gate or periodic frame differs");
    check(std::to_integer<unsigned>(out.images[0].rgb_coverage[3])==97U*alpha/128U&&out.images[1].rgb_coverage[3]==std::byte{128},"Background opacity or static label changed");
    if(frame>=65) {
      const auto second=frame<96?(frame-64U)*4U:alpha;
      check(std::to_integer<unsigned>(out.images[2].rgb_coverage[3])==97U*second/128U,"Reveal ramp lost precedence over end fade");
    }
  }
  rejects([&]{(void)materialize_loading_presentation_v1(p,200,200);});
  rejects([&]{(void)materialize_loading_presentation_v1(p,0,0);});
}
}
int main(){try{codec();actual_clock_materialization();std::cout<<"2 loading presentation groups passed\n";return 0;}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
