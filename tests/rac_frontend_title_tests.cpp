#include "openrc/rac_frontend_title.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace openrc;
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F &&f) {
  bool rejected = false;
  try { f(); } catch (const std::runtime_error &) { rejected = true; }
  check(rejected, "Malformed title input was accepted");
}
void word(std::vector<std::byte> &bytes, std::size_t at, std::uint32_t value) {
  for (unsigned i=0U;i<4U;++i) bytes.at(at+i)=static_cast<std::byte>((value>>(8U*i))&255U);
}

void clock_and_input() {
  const auto pulse = compile_rac_frontend_title_pulse_v1();
  check(std::all_of(pulse.begin(),pulse.end(),[](const auto &p){return p.alpha>=64U && p.alpha<=128U;}),
        "Source pulse alpha leaves bounds");
  check(pulse[0U].alpha>=64U && pulse[0U].alpha<=65U &&
        pulse[30U].alpha>=127U && pulse[30U].alpha<=128U &&
        pulse[15U].alpha>=95U && pulse[15U].alpha<=97U,
        "Source pulse does not follow the reached title cycle");
  RacFrontendTitleStateV1 state;
  for (std::uint32_t tick=1U;tick<=181U;++tick) {
    const auto step=step_rac_frontend_title_v1(state,0U,0U,0x3f800000U);
    state=step.state;
    check(state.counter==tick && !step.input_effects.requested_menu,
          "Source title clock or unpressed input differs");
    check(state.logo_alpha==std::min(64U,tick>30U ? tick-30U : 0U),
          "Source logo start/ramp/clamp boundary differs");
    check(state.prompt_alpha==(tick>120U ? pulse[(tick-120U)%60U].alpha : 0U),
          "Source prompt start or cycle boundary differs");
  }
  const auto enter=step_rac_frontend_title_v1({},0U,0x800U,0x3f800000U);
  check(enter.state.counter==1U && enter.input_effects.requested_menu &&
        enter.input_effects.writes.size()==5U,
        "Start before visible logo failed to execute original menu input");
  const auto confirm=step_rac_frontend_title_v1({},0U,0x40U,0x3f800000U);
  check(confirm.input_effects.requested_menu,"Source second accepted input bit disappeared");
  state={200U,64U,128U};
  for(unsigned i=1U;i<=8U;++i) {
    const auto step=step_rac_frontend_title_v1(state,3U,0x840U,0x3f800000U);
    state=step.state;
    check(state.counter==60U && !step.input_effects.requested_menu &&
          state.logo_alpha==(i<4U ? 64U-16U*i : 0U) &&
          state.prompt_alpha==128U-16U*i,
          "Original menu-mode title fade differs");
  }
  const auto wrap=step_rac_frontend_title_v1({0xffffffffU,64U,96U},0U,0U,0x3f800000U);
  check(wrap.state==RacFrontendTitleStateV1{0U,64U,96U},"Title clock did not wrap source word");
  rejects([]{(void)step_rac_frontend_title_v1({},4U,0U,0x3f800000U);});
  rejects([]{(void)step_rac_frontend_title_v1({},0U,0U,0x3f000000U);});
  rejects([]{(void)step_rac_frontend_title_v1({0U,65U,0U},0U,0U,0x3f800000U);});
  check(step_rac_frontend_title_v1({25U,0U,0U},0U,0U,0x3f555555U).state.logo_alpha==1U &&
        step_rac_frontend_title_v1({100U,64U,0U},0U,0U,0x3f555555U).state.prompt_alpha==pulse[1U].alpha &&
        step_rac_frontend_title_v1({1U,64U,128U},3U,0U,0x3f555555U).state.counter==50U,
        "PAL source timer thresholds differ");
}

void packets() {
  check(emit_rac_frontend_title_v1({},448U,{0x7200U,0x7000U},1U,2U).empty(),
        "Invisible title unexpectedly emitted draw");
  const auto draws=emit_rac_frontend_title_v1({150U,64U,97U},448U,{0x7200U,0x7000U},
                                            0x123456789abcdef0ULL,0xfedcba9876543210ULL);
  check(draws.size()==2U && draws[0U].logo && !draws[1U].logo &&
        draws[0U].source_call_pc==0x1eb95cU && draws[1U].source_call_pc==0x1eb9d0U,
        "Source title draw order or call sites differ");
  check(draws[0U].arguments.rectangle_words==std::array<std::uint32_t,4U>{236U,16U,256U,128U} &&
        draws[1U].arguments.rectangle_words==std::array<std::uint32_t,4U>{160U,368U,192U,96U} &&
        draws[0U].arguments.rgbaq==0x40808080U && draws[1U].arguments.rgbaq==0x61808080U,
        "Original title rectangle or alpha ABI differs");
  check(draws[1U].emission.coordinate_words==std::array<std::uint32_t,4U>{0x79f8U,0x85f8U,0x88f8U,0x8ef8U},
        "Title quad lost original half-pixel/offset coordinates");
  check(draws[0U].arguments.tex0==0x123456789abcdef0ULL &&
        draws[1U].arguments.tex0==0xfedcba9876543210ULL,
        "Title TEX0 was truncated or regenerated");
}

void assets() {
  constexpr std::uint32_t base=0x100U,payload=5U*1024U+5U*32768U;
  std::vector<std::byte> source(base+payload+0x20000U);
  word(source,4U,base);word(source,12U,0xe0U);word(source,0x58U,5U);word(source,0x5cU,0x90U);
  word(source,0x68U,0U);word(source,0x80U,payload);word(source,0x84U,payload);
  for(unsigned i=0U;i<5U;++i) {
    word(source,0x90U+i*16U,i*(1024U+32768U));
    word(source,0x94U+i*16U,i*(1024U+32768U)+1024U);
    word(source,0x98U+i*16U,256U);word(source,0x9cU+i*16U,128U);
  }
  const auto palette=base+4U*(1024U+32768U);
  // Logical8 lives at stored16. Alpha255 must survive despite the existing
  // asset decoder's separate canonical min(255,2*a) output convention.
  source[palette+64U]=std::byte{7};source[palette+67U]=std::byte{255};
  std::fill_n(source.begin()+palette+1024U,32768U,std::byte{8});
  source[base+payload+3U]=std::byte{200};
  const RacFrontendTextureLimitsV1 limits{source.size(),5U,256U,128U,32768U,163840U,2000000U};
  const auto result=compile_rac_frontend_title_assets_v1(source,1U,limits);
  check(result.prompt_source_index==4U && result.logo_source_range==RacFrontendTextureRangeV1{base+payload,0x20000U} &&
        result.logo_rgba_source.size()==0x20000U && result.prompt_rgba_source.size()==0x20000U &&
        result.logo_rgba_source[3U]==std::byte{200} && result.prompt_rgba_source[0U]==std::byte{7} &&
        result.prompt_rgba_source[3U]==std::byte{255},"Original title planes/CLUT/alpha differ");
  const auto overlay=compile_rac_frontend_title_logo_overlay_v1(result,512U,448U,60U,0x3f800000U);
  check(overlay.canvas_height==448U && overlay.images.size()==64U && overlay.frames.size()==94U &&
        overlay.frames[29U].draws.empty() && overlay.frames[30U].draws==std::vector<ScreenOverlayDrawV1>{{0U,236,16}} &&
        overlay.frames[93U].draws==std::vector<ScreenOverlayDrawV1>{{63U,236,16}} &&
        overlay.images[63U].rgb_coverage[3U]==std::byte{100} && overlay.coverage_denominator==128U &&
        overlay.loop_begin==UINT32_MAX,"Qualified source logo raster/ramp differs");
  const auto encoded=encode_screen_overlay_v1(overlay);
  check(decode_screen_overlay_v1(encoded)==overlay,"Prepared logo overlay codec changed the source layer");
  auto small=ScreenOverlayLimitsV1{};small.max_bytes=1024U;
  rejects([&]{(void)compile_rac_frontend_title_logo_overlay_v1(result,512U,448U,50U,0x3f555555U,small);});
  const auto pal=compile_rac_frontend_title_logo_overlay_v1(result,512U,448U,50U,0x3f555555U);
  check(pal.frames.size()==89U && pal.frames[24U].draws.empty() && !pal.frames[25U].draws.empty(),
        "Prepared PAL logo ignores actual source time scale");
  check(rac_frontend_title_prompt_index_v1(0U)==4U && rac_frontend_title_prompt_index_v1(1U)==4U &&
        rac_frontend_title_prompt_index_v1(5U)==8U,"Original title language mapping differs");
  rejects([&]{(void)compile_rac_frontend_title_assets_v1(source,2U,limits);});
  word(source,0x84U,static_cast<std::uint32_t>(source.size()));
  rejects([&]{(void)compile_rac_frontend_title_assets_v1(source,1U,limits);});
}

// Independent exact affine oracle: triangle signed areas from original
// 12.4 corner positions, interpolated original UV12.4, then texel offset.
std::array<std::uint32_t,2U> triangle_uv(std::uint32_t x,std::uint32_t y) {
  struct Vertex {std::int64_t x,y,u,v;};
  const std::array<Vertex,4U> corners{{{-8,-8,0,0},{3064,-8,4096,0},
                                      {-8,1528,0,2048},{3064,1528,4096,2048}}};
  const auto cross=[](const Vertex &a,const Vertex &b,const Vertex &c) {
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
  };
  const Vertex p{static_cast<std::int64_t>(x)*16,static_cast<std::int64_t>(y)*16,0,0};
  for (const auto indices:std::array{std::array{0U,1U,2U},std::array{1U,3U,2U}}) {
    const auto a=corners[indices[0]],b=corners[indices[1]],c=corners[indices[2]];
    const auto area=cross(a,b,c),wa=cross(p,b,c),wb=cross(a,p,c),wc=cross(a,b,p);
    if (wa<0 || wb<0 || wc<0) continue;
    return {static_cast<std::uint32_t>((wa*a.u+wb*b.u+wc*c.u)/area-8),
            static_cast<std::uint32_t>((wa*a.v+wb*b.v+wc*c.v)/area-8)};
  }
  throw std::runtime_error("Reference pixel lies outside both original triangles");
}

void prompt_raster() {
  RacFrontendTitleAssetsV1 assets;
  assets.logo_rgba_source.resize(256U*128U*4U,std::byte{128});
  assets.prompt_rgba_source.resize(256U*128U*4U);
  for (std::size_t i=0U;i<assets.prompt_rgba_source.size();++i)
    assets.prompt_rgba_source[i]=static_cast<std::byte>((i*i+71U*i+13U)%(i%4U==3U ? 129U : 256U));
  for (const auto alpha:std::array{1U,64U,65U,96U,127U,128U}) {
    const auto raster=rasterize_rac_frontend_title_prompt_v1(assets,alpha);
    check(raster.width==192U && raster.height==96U,"Prompt was not sampled in its source raster");
    for (unsigned y=0U;y<96U;++y) for (unsigned x=0U;x<192U;++x) {
      const auto uv=triangle_uv(x,y);
      for (unsigned channel=0U;channel<4U;++channel) {
        const auto sample=[&](unsigned dx,unsigned dy) {
          return std::to_integer<unsigned>(assets.prompt_rgba_source[
              ((((uv[1]/16U+dy)%128U)*256U)+(uv[0]/16U+dx)%256U)*4U+channel]);
        };
        const auto uf=uv[0]%16U,vf=uv[1]%16U;
        const auto top=(sample(0U,0U)*(16U-uf)+sample(1U,0U)*uf)/16U;
        const auto bottom=(sample(0U,1U)*(16U-uf)+sample(1U,1U)*uf)/16U;
        auto expected=(top*(16U-vf)+bottom*vf)/16U;
        if (channel==3U) expected=expected*alpha/128U;
        check(std::to_integer<unsigned>(raster.rgb_coverage[(y*192U+x)*4U+channel])==expected,
              "Prompt differs from signed-area affine/four-bit filter reference");
      }
    }
  }
  check(triangle_uv(0U,0U)==std::array{2U,2U} && triangle_uv(1U,2U)==std::array{24U,45U} &&
        triangle_uv(191U,95U)==std::array{4077U,2029U},"Source texel-center phases differ");
  const auto overlay=compile_rac_frontend_title_overlay_v1(assets,512U,448U,50U,0x3f555555U);
  const auto pulse=compile_rac_frontend_title_pulse_v1();
  check(overlay.images.size()==192U && overlay.frames.size()==160U && overlay.loop_begin==100U &&
        overlay.frames[99U].draws.size()==1U && overlay.frames[100U].draws==
        std::vector<ScreenOverlayDrawV1>{{63U,236,16},{63U+pulse[1U].alpha,160,368}} &&
        overlay.frames[159U].draws[1U].image_id==63U+pulse[0U].alpha &&
        screen_overlay_frame_index_v1(overlay,160U)==100U,
        "Complete title lost original prefix, prompt phase, draw order or loop");
  check(decode_screen_overlay_v1(encode_screen_overlay_v1(overlay))==overlay,
        "Complete neutral title overlay changed during codec round trip");
  rejects([&]{(void)rasterize_rac_frontend_title_prompt_v1(assets,0U);});
  rejects([&]{(void)compile_rac_frontend_title_overlay_v1(assets,512U,512U,50U,0x3f555555U);});
  assets.prompt_rgba_source[3U]=std::byte{129};
  rejects([&]{(void)rasterize_rac_frontend_title_prompt_v1(assets,128U);});
}
} // namespace

int main() {
  try {clock_and_input();packets();assets();prompt_raster();std::cout<<"rac_frontend_title_tests: 4 groups passed; 442368 reference pixel components\n";return 0;}
  catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
