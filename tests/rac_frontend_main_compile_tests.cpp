#include "openrc/rac_frontend_main_compile.hpp"
#include "openrc/ps2_palette.hpp"

#include <bit>
#include <iostream>

namespace {
using namespace openrc;
void check(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F &&f) {
  bool rejected=false;try{f();}catch(const std::runtime_error &){rejected=true;}
  check(rejected,"Unsupported main raster input was accepted");
}
RacFrontendTextureV1 font() {
  RacFrontendTextureV1 result;result.entry.source_index=3U;
  result.entry.width=256U;result.entry.height=128U;result.indices.resize(32768U);
  constexpr std::array<unsigned,5U> alphas{0U,3U,4U,64U,128U};
  for(unsigned i=0U;i<alphas.size();++i) {
    const auto at=std::size_t(psmt8_clut_storage_index_v1(static_cast<std::uint8_t>(i)))*4U;
    for(unsigned c=0U;c<3U;++c)result.raw_palette[at+c]=std::byte{128};
    result.raw_palette[at+3U]=static_cast<std::byte>(alphas[i]);
  }
  for(unsigned y=0U;y<128U;++y)for(unsigned x=0U;x<256U;++x)
    result.indices[y*256U+x]=static_cast<std::byte>((x+2U*y)%5U);
  return result;
}
RacFrontendListDrawPlanV1 plan(std::int32_t x,std::int32_t y,std::uint32_t color) {
  RacFontMetricTableV1 metrics;metrics.rows[65U]={32U,48U,0,8};
  RacIntegerGlyphInputsV1 in;
  in.x_word=std::bit_cast<std::uint32_t>(x);in.y_word=std::bit_cast<std::uint32_t>(y);
  in.rgbaq=color;in.screen_offset_words={31744U,31744U};in.tex0=123U;
  RacFrontendListDrawPlanV1 out;out.font_source_id=3U;out.returned_word=2U;out.batch_ended=true;
  out.glyph_calls.push_back({0U,false,false,{},execute_rac_integer_glyph_v1(
      std::array{std::byte{65},std::byte{}},metrics,in)});
  return out;
}
void raster() {
  const auto texture=font();
  constexpr std::array<unsigned,5U> alphas{0U,3U,4U,64U,128U};
  for(const auto xy:std::array{std::array{-3,-2},std::array{3,4},std::array{28,21}}) {
    const auto draw=plan(xy[0U],xy[1U],0x80ff8040U);
    const auto image=compile_rac_frontend_list_rtt_v1(draw,texture,32U,24U,0x80100808U);
    for(int y=0;y<24;++y)for(int x=0;x<32;++x) {
      std::array<int,3U> expected{8,8,16};
      if(x>=xy[0U] && y>=xy[1U] && x<xy[0U]+16 && y<xy[1U]+16) {
        const auto a=alphas[(32U+unsigned(x-xy[0U])+2U*(48U+unsigned(y-xy[1U])))%5U];
        if(a>=4U) {
          constexpr std::array<int,3U> source{64,128,255};
          for(unsigned c=0U;c<3U;++c)
            expected[c]=(source[c]*int(a)+expected[c]*(128-int(a)))/128;
        }
      }
      const auto at=std::size_t(y*32+x)*4U;
      for(unsigned c=0U;c<3U;++c)
        check(std::to_integer<int>(image.rgb_coverage[at+c])==expected[c],
              "Original glyph sampling, GEQUAL4, MODULATE, clipping or RTT blending differs");
      check(image.rgb_coverage[at+3U]==std::byte{128},"FIX128 final copy became a translucent glyph layer");
    }
  }
  auto draw=plan(3,4,0x80000000U);
  const auto dark=compile_rac_frontend_list_rtt_v1(draw,texture,32U,24U,0x80100808U);
  check(std::to_integer<unsigned>(dark.rgb_coverage[(4U*32U+3U)*4U])<8U,
        "Negative-delta shadow floor was replaced by truncation toward zero");
  rejects([&]{(void)compile_rac_frontend_list_rtt_v1(draw,texture,32U,128U,0x80100808U);});
  draw.returned_word=1U;
  rejects([&]{(void)compile_rac_frontend_list_rtt_v1(draw,texture,32U,24U,0x80100808U);});
  draw=plan(3,4,0x80ff8040U);draw.glyph_calls[0U].plan.draws[0U].emission.packet[0U]^=std::byte{1};
  rejects([&]{(void)compile_rac_frontend_list_rtt_v1(draw,texture,32U,24U,0x80100808U);});
}
void lists() {
  RacFrontendMainAssetsV1 assets;assets.clear_rgba=0x80100808U;assets.timer_argument=10U;
  assets.shadow_x_y={2U,2U};assets.focused_node=100U;
  for(unsigned i=0U;i<4U;++i) {
    auto texture=font();texture.entry.source_index=i;texture.entry.pixel_offset=1024U;
    assets.textures.textures.push_back(std::move(texture));
  }
  assets.textures.payload_range={0U,33792U};
  for(auto &metric:assets.metrics.tables)metric.rows[65U]={32U,48U,0,8};
  assets.text.entries.push_back({1U,0U,0U,0U,{}, {std::byte{65}}});
  std::array<RacFrontendProjectedBoundsV1,3U> bounds;
  for(unsigned i=0U;i<3U;++i) {
    assets.nodes[i]={100U+i,i+2U,0U,4U,0U,{{1,4,1U,0,0},{}}};
    bounds[i].width_height_x_y={32U,24U,60U+i*40U,200U};
  }
  const auto result=compile_rac_frontend_main_lists_v1(assets,bounds,{1,0,0},0x3f555555U,50U);
  check(result.images.size()==3U && result.frames.size()==1U && result.canvas_height==448U &&
        result.frames[0U].draws==std::vector<ScreenOverlayDrawV1>{{0U,61,201},{1U,101,201},{2U,141,201}},
        "Main list source order or owner+1 projected positions differ");
  check(decode_screen_overlay_v1(encode_screen_overlay_v1(result))==result,"Main list neutral codec differs");
  check(result.images[0U]!=result.images[1U],"Initial focused row age was dropped");
  rejects([&]{(void)compile_rac_frontend_main_lists_v1(assets,bounds,{1,0,0},0x3f800000U,50U);});
}
} // namespace
int main() {
  try {raster();lists();std::cout<<"rac_frontend_main_compile_tests: 2 groups passed\n";return 0;}
  catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
