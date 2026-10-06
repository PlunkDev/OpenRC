#include "openrc/rac_frontend_loading.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace openrc;
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
template<class F> void rejects(F &&call) {
  try { call(); } catch(const std::runtime_error &) { return; }
  throw std::runtime_error("Invalid loading-card input was accepted");
}
using Bytes=std::vector<std::byte>;
void put(Bytes &b,std::size_t at,std::uint32_t value) {
  for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<std::byte>(value>>(8*i));
}
RacFrontendTextureLimitsV1 limits() { return {1048576U,3U,512U,64U,32768U,69632U,354304U}; }
Bytes assets() {
  Bytes data(80U+5152U+2U*33824U);put(data,0U,18U);
  std::size_t at=80U;
  for(unsigned i=0;i<3;++i) {
    const auto width=i==0U?64U:512U;
    put(data,4U+i*4U,static_cast<std::uint32_t>(at));
    put(data,at,0x50494632U);put(data,at+8U,width);put(data,at+12U,64U);
    put(data,at+16U,19U);put(data,at+28U,1U);
    // Logical index8 uses raw palette16; raw alpha retains its GS byte.
    put(data,at+32U+16U*4U,0x40030201U+i);
    std::fill(data.begin()+at+1056U,data.begin()+at+1056U+width*64U,std::byte{8U});
    at+=1056U+width*64U;
  }
  return data;
}
void asset_checks() {
  auto data=assets();const auto parsed=parse_rac_frontend_loading_assets_v1(data,0U,1U,limits());
  check(parsed.tile.entry.width==64U&&parsed.cards[1].entry.width==512U&&
            parsed.cards[1].entry.source_index==2U,"Source tile/card selection differs");
  check(parsed.tile.raw_palette[67U]==std::byte{64U}&&parsed.tile.rgba[3]==std::byte{128U}&&
            parsed.cards[1].rgba[0]==std::byte{3U},"PIF CLUT permutation or raw alpha was lost");
  const auto alias=parse_rac_frontend_loading_assets_v1(data,0U,0U,limits());
  check(alias.cards[0].entry==alias.cards[1].entry&&alias.cards[0].rgba==alias.cards[1].rgba,
        "Same-card source alias changed");
  auto bad=data;put(bad,8U,80U);rejects([&]{(void)parse_rac_frontend_loading_assets_v1(bad,0,1,limits());});
  bad=data;put(bad,4U,0xfffffff0U);rejects([&]{(void)parse_rac_frontend_loading_assets_v1(bad,0,1,limits());});
  bad=data;bad.pop_back();rejects([&]{(void)parse_rac_frontend_loading_assets_v1(bad,0,1,limits());});
  auto bounded=limits();--bounded.max_total_pixels;
  rejects([&]{(void)parse_rac_frontend_loading_assets_v1(data,0,1,bounded);});
  bounded=limits();--bounded.max_total_output_bytes;
  rejects([&]{(void)parse_rac_frontend_loading_assets_v1(data,0,1,bounded);});
  rejects([&]{(void)parse_rac_frontend_loading_assets_v1(data,17,0,limits());});
}
void clock_checks() {
  auto begin=begin_rac_frontend_loading_card_v1(3,4,200,true,0,0,0xffffffffU);
  check(begin.requested_level==0U&&begin.clear_level_counters&&begin.state.render_pending,
        "Original asynchronous level request is absent");
  auto state=begin.state;
  rejects([&]{(void)advance_rac_frontend_loading_card_v1(state,0,0xffffffffU,std::nullopt);});
  for(unsigned frame=0;frame<211U;++frame) {
    check(state.frame==frame&&state.render_pending,"Original loading wait ended early");
    state=advance_rac_frontend_loading_card_v1(state,0,0xffffffffU,0U);
  }
  check(state.duration==230U&&state.load_pending,"Pending load did not extend to frame+20");
  state=advance_rac_frontend_loading_card_v1(state,0,0xffffffffU,7U);
  check(!state.load_pending&&state.duration==230U,"Nonzero completion did not stop polling");
  rejects([&]{(void)advance_rac_frontend_loading_card_v1(state,0,0xffffffffU,1U);});
  while(state.render_pending)state=advance_rac_frontend_loading_card_v1(state,0,0xffffffffU,std::nullopt);
  check(state.frame==230U,"Final presentation count changed");
  for(const auto dc:{3U,0x7fffffffU}) {
    const auto skipped=begin_rac_frontend_loading_card_v1(0,1,200,true,0,dc,0xffffffffU);
    check(skipped.requested_level==0U&&!skipped.state.render_pending,
          "Skipped card gate incorrectly skipped earlier level request");
  }
  check(!begin_rac_frontend_loading_card_v1(0,1,0,false,0,0,0xffffffffU).state.render_pending&&
        !begin_rac_frontend_loading_card_v1(0,1,200,false,0,0,0).state.render_pending,
        "Initial signed-duration or card gate changed");
  const auto first=begin_rac_frontend_loading_card_v1(0,1,200,false,0,0,0xffffffffU).state;
  const auto stopped=advance_rac_frontend_loading_card_v1(first,3,0xffffffffU,std::nullopt);
  check(stopped.frame==1U&&!stopped.render_pending,"Post-I/O card state did not stop loop");
}
void drawing_checks() {
  auto state=begin_rac_frontend_loading_card_v1(0,1,200,false,0,0,0xffffffffU).state;
  auto draw=execute_rac_frontend_loading_draws_v1(state,224U);
  check(draw.size()==2U&&draw[0].rectangle_words[1]==178U&&draw[0].rgba==0x00808080U&&
        draw[1].rgba==0x80808080U,"Initial source alpha, Y or fixed card colour changed");
  state.frame=65;draw=execute_rac_frontend_loading_draws_v1(state,224U);
  check(draw.size()==4U&&draw[2].rectangle_words[1]==224U&&draw[2].rgba==0x04808080U&&
        draw[2].source_call_pc==0x2331fcU,"Original second card reveal or centre reload changed");
  state.frame=199;draw=execute_rac_frontend_loading_draws_v1(state,224U);
  check(draw[0].rgba==0x08808080U&&draw[2].rgba==0x08808080U,"Last-frame fade differs");
  state.first_card=state.second_card;draw=execute_rac_frontend_loading_draws_v1(state,224U);
  check(draw.size()==2U&&draw[0].rectangle_words[1]==192U&&draw[1].source_call_pc==0x233118U,
        "Single authored card did not take its original source call path");
  RacFrontendStqQuadInputsV1 quad{{0U,224U,512U,64U},{0x7000U,0x7000U},
                                {0U,0x40800000U,0U,0x3ecccccdU},0xffffeeee12345678ULL,0x9876543210ULL};
  const auto packet=emit_rac_frontend_stq_quad_v1(quad);
  check(packet.coordinate_words==std::array<std::uint32_t,4U>{0x6ff8U,0x8ff8U,0x7df8U,0x81f8U}&&
        packet.packet[40U]==std::byte{0x54U}&&packet.packet[48U]==std::byte{0x78U}&&
        packet.packet[54U]==std::byte{0x80U}&&packet.packet[55U]==std::byte{0x3fU},
        "STQ source coordinates, PRIM or forced Q differ");
}
void source_comparison(const char *path) {
  std::ifstream in(path,std::ios::binary|std::ios::ate);check(bool(in),"Cannot open source loading fixture");
  const auto size=in.tellg();check(size>=12&&size<=4*1048576,"Source loading fixture exceeds limit");
  in.seekg(0);Bytes bytes(static_cast<std::size_t>(size));in.read(reinterpret_cast<char*>(bytes.data()),size);
  check(bool(in),"Truncated source loading fixture");
  for(unsigned i=0;i<8;++i)check(bytes[i]==std::byte("FLOAD001"[i]),"Source loading magic differs");
  std::size_t at=8;
  const auto get=[&]() {check(at<=bytes.size()&&bytes.size()-at>=4,"Truncated source loading word");
    std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::to_integer<std::uint32_t>(bytes[at++])<<(8*i);return v;};
  const auto count=get();check(count<=4096,"Source STQ cases exceed limit");
  for(unsigned i=0;i<count;++i) {
    RacFrontendStqQuadInputsV1 input;
    for(auto &v:input.rectangle_words)v=get();for(auto &v:input.screen_offset_reads)v=get();
    for(auto &v:input.st_endpoint_bits)v=get();
    auto low=get();input.rgbaq=low|(std::uint64_t(get())<<32U);
    low=get();input.tex0=low|(std::uint64_t(get())<<32U);
    const auto native=emit_rac_frontend_stq_quad_v1(input);
    for(const auto v:native.coordinate_words)check(v==get(),"Source STQ coordinate differs");
    check(at<=bytes.size()&&bytes.size()-at>=native.packet.size(),"Truncated source STQ packet");
    check(std::equal(native.packet.begin(),native.packet.end(),bytes.begin()+at),"Source/native STQ packet differs");
    at+=native.packet.size();
  }
  const auto clocks=get();check(clocks<=4096,"Source clock cases exceed limit");
  for(unsigned i=0;i<clocks;++i) {
    RacFrontendLoadingCardStateV1 state;state.first_card=0;state.second_card=1;
    state.frame=get();state.duration=get();state.load_pending=get()!=0;state.render_pending=true;
    const auto dc=get(),e4=get(),poll=get();
    const auto native=advance_rac_frontend_loading_card_v1(state,dc,e4,
        state.load_pending?std::optional(poll):std::nullopt);
    check(native.frame==get()&&native.duration==get()&&native.load_pending==(get()!=0)&&
              native.render_pending==(get()!=0),"Source/native loading clock differs");
  }
  const auto draws=get();check(draws<=4096,"Source draw cases exceed limit");
  for(unsigned i=0;i<draws;++i) {
    RacFrontendLoadingCardStateV1 state;state.first_card=get();state.second_card=get();
    state.frame=get();state.duration=get();state.render_pending=true;
    const auto origin=get(),count=get();
    const auto native=execute_rac_frontend_loading_draws_v1(state,origin);
    check(native.size()==count,"Source loading draw count differs");
    for(const auto &call:native) {
      check(call.source_call_pc==get()&&call.texture_slot==get(),"Source loading draw owner differs");
      for(const auto v:call.rectangle_words)check(v==get(),"Source loading rectangle differs");
      check(call.rgba==get()&&call.uses_stq==(get()!=0),"Source loading draw RGBA or STQ differs");
      for(const auto v:call.st_endpoint_bits)check(v==get(),"Source numeric scrolling endpoint differs");
    }
  }
  check(at==bytes.size(),"Trailing source loading fixture bytes");
  std::cout<<"loading_source: "<<count<<" STQ packets, "<<clocks<<" clocks, "<<draws<<" draws matched\n";
}
} // namespace
int main(int argc,char **argv) {
  try {asset_checks();clock_checks();drawing_checks();check(argc<=2,"Expected optional source loading fixture");
    if(argc==2)source_comparison(argv[1]);std::cout<<"rac_frontend_loading_tests: 3 groups passed\n";return 0;}
  catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
