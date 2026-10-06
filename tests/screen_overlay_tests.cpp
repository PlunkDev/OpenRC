#include "openrc/screen_overlay.hpp"
#include "openrc/prepared_game_v2.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class F> void reject(F call) {
  bool rejected=false;try {call();} catch(const openrc::ScreenOverlayError&) {rejected=true;}
  check(rejected,"invalid overlay accepted");
}
openrc::ScreenOverlayV1 fixture() {
  openrc::ScreenOverlayV1 value;value.canvas_width=3;value.canvas_height=2;
  value.updates_per_second=50;value.coverage_denominator=128;
  value.images.push_back({2,1,{std::byte{255},std::byte{0},std::byte{13},std::byte{64},
      std::byte{7},std::byte{193},std::byte{88},std::byte{127}}});
  value.frames={{{{0,-1,0}}},{{{0,1,1}}},{{{0,0,0},{0,1,0}}}};
  value.loop_begin=1;return value;
}
void test_codec() {
  auto value=fixture();const auto encoded=openrc::encode_screen_overlay_v1(value);
  check(openrc::decode_screen_overlay_v1(encoded)==value,"roundtrip failed");
  check(openrc::encode_screen_overlay_v1(openrc::decode_screen_overlay_v1(encoded))==encoded,"noncanonical codec");
  auto bad=encoded;bad.back()^=std::byte{1};reject([&]{(void)openrc::decode_screen_overlay_v1(bad);});
  bad=encoded;bad.resize(95);reject([&]{(void)openrc::decode_screen_overlay_v1(bad);});
  bad=encoded;bad[24]=std::byte{255};bad[25]=std::byte{255};reject([&]{(void)openrc::decode_screen_overlay_v1(bad);});
  bad=encoded;bad.push_back(std::byte{0});reject([&]{(void)openrc::decode_screen_overlay_v1(bad);});
  auto invalid=value;invalid.images[0].rgb_coverage[3]=std::byte{129};reject([&]{openrc::validate_screen_overlay_v1(invalid);});
  invalid=value;invalid.frames[0].draws[0].image_id=1;reject([&]{openrc::validate_screen_overlay_v1(invalid);});
  invalid=value;invalid.loop_begin=3;reject([&]{openrc::validate_screen_overlay_v1(invalid);});
  check(openrc::screen_overlay_frame_index_v1(value,0)==0&&openrc::screen_overlay_frame_index_v1(value,3)==1&&
      openrc::screen_overlay_frame_index_v1(value,UINT64_MAX)==1,"prefix/loop clock failed");
  value.loop_begin=UINT32_MAX;check(openrc::screen_overlay_frame_index_v1(value,UINT64_MAX)==2,"hold clock failed");
}
void test_integer_composition() {
  // Every source/destination/coverage combination. Expected is the separate
  // positive weighted-sum identity, including negative-delta floor cases.
  openrc::ScreenOverlayV1 value;value.canvas_width=256;value.canvas_height=256;
  value.updates_per_second=50;value.coverage_denominator=128;
  value.images.push_back({256,256,std::vector<std::byte>(256U*256U*4U)});
  value.frames={{{{0,0,0}}}};
  std::vector<std::byte> background(value.images[0].rgb_coverage.size());
  for(unsigned coverage=0;coverage<=128;++coverage) {
    for(unsigned src=0;src<256;++src) for(unsigned dst=0;dst<256;++dst) {
      const auto i=(src*256U+dst)*4U;
      for(unsigned c=0;c<3;++c) {value.images[0].rgb_coverage[i+c]=static_cast<std::byte>(src);background[i+c]=static_cast<std::byte>(dst);}
      value.images[0].rgb_coverage[i+3]=static_cast<std::byte>(coverage);
      background[i+3]=std::byte{255};
    }
    openrc::composite_screen_overlay_frame_v1(background,value,0);
    for(unsigned src=0;src<256;++src) for(unsigned dst=0;dst<256;++dst)
      check(std::to_integer<unsigned>(background[(src*256U+dst)*4U])==(src*coverage+dst*(128U-coverage))/128U,
          "integer composition differs from weighted-sum identity");
  }
  value=fixture();background.assign(24,std::byte{200});
  openrc::composite_screen_overlay_frame_v1(background,value,0);
  check(background[0]==std::byte{8}&&background[4]==std::byte{200},"negative rectangle clipped incorrectly");
  value.frames[0].draws[0].x=INT32_MAX;value.frames[0].draws[0].y=INT32_MIN;
  const auto before=background;openrc::composite_screen_overlay_frame_v1(background,value,0);
  check(before==background,"offscreen extreme rectangle changed canvas");
}
void test_composition_limits() {
  openrc::ScreenOverlayV1 value;value.canvas_width=1;value.canvas_height=1;
  value.updates_per_second=50;value.coverage_denominator=128;
  value.images.assign(1025,{1,1,{std::byte{73},std::byte{29},std::byte{211},std::byte{128}}});
  value.frames={{{{1024,0,0}}}};
  std::array<std::byte,4> canvas{};const auto before=canvas;
  reject([&]{openrc::composite_screen_overlay_frame_v1(canvas,value,0);});
  check(canvas==before,"default image limit failure changed the canvas");
  openrc::ScreenOverlayLimitsV1 limits;limits.max_images=1025;
  const auto decoded=openrc::decode_screen_overlay_v1(openrc::encode_screen_overlay_v1(value,limits),limits);
  openrc::composite_screen_overlay_frame_v1(canvas,decoded,0,limits);
  check(canvas==std::array{std::byte{73},std::byte{29},std::byte{211},std::byte{255}},
      "composition did not honor the explicitly admitted image bank");
  const auto rendered=canvas;limits.max_bytes=128;
  reject([&]{openrc::composite_screen_overlay_frame_v1(canvas,decoded,0,limits);});
  check(canvas==rendered,"explicit byte limit failure changed the canvas");
}
}
int main() try {
  test_codec();test_integer_composition();test_composition_limits();
  std::cout<<"screen overlay codec/clock/clipping and 8454144 integer blend cases passed\n";return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
