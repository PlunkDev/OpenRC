#include "openrc/rac_frontend_loading_raster.hpp"
#include "openrc/ps2_palette.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace openrc;
void check(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F &&call) {try{call();}catch(const std::runtime_error &){return;}
  throw std::runtime_error("Unsupported loading raster was accepted");}
RacFrontendTextureV1 texture(unsigned width) {
  RacFrontendTextureV1 texture;texture.entry.width=width;texture.entry.height=64;
  texture.indices.resize(width*64U);
  for(unsigned i=0;i<256U;++i) {
    const auto at=psmt8_clut_storage_index_v1(static_cast<std::uint8_t>(i))*4U;
    texture.raw_palette[at]=static_cast<std::byte>(i);
    texture.raw_palette[at+1]=static_cast<std::byte>(255U-i);
    texture.raw_palette[at+2]=std::byte{30};texture.raw_palette[at+3]=std::byte{128};
  }
  return texture;
}
void raster() {
  RacFrontendLoadingAssetsV1 assets;assets.tile=texture(64);
  assets.cards[0]=texture(512);assets.cards[1]=texture(512);
  for(unsigned y=0;y<64U;++y)for(unsigned x=0;x<64U;++x)
    assets.tile.indices[y*64U+x]=static_cast<std::byte>(x+y*3U);
  auto state=begin_rac_frontend_loading_card_v1(0,1,200,false,0,0,0xffffffffU).state;
  state.frame=32;auto draws=execute_rac_frontend_loading_draws_v1(state,224);
  // A separately constructed exact quarter interval makes first V=-3/8,
  // forcing signed wrap and independent four-neighbour interpolation.
  draws[0].st_endpoint_bits[2]=0U;draws[0].st_endpoint_bits[3]=0x3e800000U;
  const auto image=rasterize_rac_frontend_loading_band_v1(assets,draws[0]);
  // x neighbours63,0 atfraction12; y neighbours63,0 atfraction10.
  // Red top floor((252*4+189*12)/16)=204, bottom15, final85.
  check(image.rgb_coverage[0]==std::byte{85}&&image.rgb_coverage[2]==std::byte{30}&&
        image.rgb_coverage[3]==std::byte{128},"STQ half-pixel, repeat or staged4-bit filtering changed");
  draws[0].rgba=0x40808080U;
  const auto half=rasterize_rac_frontend_loading_band_v1(assets,draws[0]);
  check(half.rgb_coverage[0]==image.rgb_coverage[0]&&half.rgb_coverage[3]==std::byte{64},
        "Alpha modulation changed display RGB or used UNORM255");
  for(unsigned i=0;i<assets.cards[0].indices.size();++i)assets.cards[0].indices[i]=static_cast<std::byte>(i);
  const auto card=rasterize_rac_frontend_loading_band_v1(assets,draws[1]);
  check(card.rgb_coverage[0]==std::byte{0}&&card.rgb_coverage[4]==std::byte{1}&&
        card.rgb_coverage[255U*4U]==std::byte{255}&&card.rgb_coverage[3]==std::byte{128},
        "Integer1:1 card introduced filtering or converted raw alpha early");
  auto bad=draws[0];bad.st_endpoint_bits[2]=0x80000000U;
  rejects([&]{(void)rasterize_rac_frontend_loading_band_v1(assets,bad);});
  bad=draws[0];bad.st_endpoint_bits[1]=0x3f800000U;
  rejects([&]{(void)rasterize_rac_frontend_loading_band_v1(assets,bad);});
  assets.tile.raw_palette[3]=std::byte{129};
  rejects([&]{(void)rasterize_rac_frontend_loading_band_v1(assets,draws[0]);});
}
}
int main() {try{raster();std::cout<<"rac_frontend_loading_raster_tests: 1 group passed\n";return 0;}
  catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
