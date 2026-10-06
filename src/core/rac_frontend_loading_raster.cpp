#include "openrc/rac_frontend_loading_raster.hpp"
#include "openrc/ps2_palette.hpp"

#include <algorithm>

namespace openrc {
namespace {
[[noreturn]] void fail(const char *message) { throw ScreenOverlayError(message); }
constexpr std::int64_t denominator=INT64_C(1)<<40U;
std::int64_t source_fixed(std::uint32_t bits) {
  if(bits==0U)return 0;
  const auto exponent=bits>>23U;
  // Every reached V is zero or >=1/600 and below1.4. This exact conversion
  // accepts a modest superset but rejects signs, subnormals and lost bits.
  if(exponent<110U||exponent>127U)fail("Loading ST coordinate leaves its qualified raw domain");
  return std::int64_t((bits&0x7fffffU)|0x800000U)<<(exponent-110U);
}
std::int64_t floor_div(std::int64_t numerator,std::int64_t divisor) noexcept {
  auto quotient=numerator/divisor;
  if(numerator%divisor<0)--quotient;
  return quotient;
}
void validate(const RacFrontendTextureV1 &texture,std::uint32_t width) {
  if(texture.entry.width!=width||texture.entry.height!=64U||texture.indices.size()!=width*64U)
    fail("Loading raster source plane has unsupported dimensions");
  for(std::size_t i=3U;i<texture.raw_palette.size();i+=4U)
    if(std::to_integer<unsigned>(texture.raw_palette[i])>128U)
      fail("Loading raster source alpha exceeds its qualified blend domain");
}
unsigned texel(const RacFrontendTextureV1 &texture,std::int64_t x,std::int64_t y,unsigned lane) {
  const auto width=texture.entry.width;
  // Wrapping unsigned conversion preserves signed modulo for powers of two.
  const auto index=std::to_integer<std::uint8_t>(texture.indices[
      (std::uint64_t(y)&63U)*width+(std::uint64_t(x)&(width-1U))]);
  return std::to_integer<unsigned>(texture.raw_palette[psmt8_clut_storage_index_v1(index)*4U+lane]);
}
unsigned interpolate(unsigned a,unsigned b,unsigned fraction) noexcept {
  return (a*(16U-fraction)+b*fraction)>>4U;
}
} // namespace

ScreenOverlayImageV1 rasterize_rac_frontend_loading_band_v1(
    const RacFrontendLoadingAssetsV1 &assets,const RacFrontendLoadingDrawV1 &draw) {
  if(draw.rectangle_words[2]!=512U||draw.rectangle_words[3]!=64U||
      (draw.rgba&0xffffffU)!=0x808080U||(draw.rgba>>24U)>128U||draw.texture_slot>2U||
      draw.uses_stq!=(draw.texture_slot==0U))
    fail("Loading band leaves its original rectangle, modulation or texture domain");
  const auto &texture=draw.texture_slot==0U?assets.tile:assets.cards[draw.texture_slot-1U];
  validate(texture,draw.uses_stq?64U:512U);
  std::int64_t v0=0,v1=0;
  if(draw.uses_stq) {
    if(draw.st_endpoint_bits[0]!=0U||draw.st_endpoint_bits[1]!=0x40800000U)
      fail("Loading tile lost original U0..4/Q1");
    v0=source_fixed(draw.st_endpoint_bits[2]);v1=source_fixed(draw.st_endpoint_bits[3]);
    if(v0>=denominator||v1<=v0||v1>denominator*3/2)
      fail("Loading tile V interval leaves its source scroll domain");
  }
  ScreenOverlayImageV1 image;image.width=512U;image.height=64U;
  image.rgb_coverage.resize(512U*64U*4U);
  const auto alpha=draw.rgba>>24U;
  for(unsigned y=0;y<64U;++y) {
    // Vertices are at pixel coordinates minus0.5. At integer sample centres,
    // ST*texture_size -0.5 gives v0*64 +delta*(y+0.5)-0.5.
    const auto v=draw.uses_stq ? floor_div(16*(v0*128+(v1-v0)*(2*y+1)-denominator),2*denominator) : 16*y;
    const auto iy=floor_div(v,16);const auto fy=static_cast<unsigned>(v-iy*16);
    for(unsigned x=0;x<512U;++x) {
      // U0..4 over512 pixels with64texels: 16*((x+0.5)/2 -0.5).
      const auto u=draw.uses_stq ? std::int64_t(8*x)-4 : std::int64_t(16*x);
      const auto ix=floor_div(u,16);const auto fx=static_cast<unsigned>(u-ix*16);
      for(unsigned lane=0;lane<4U;++lane) {
        const auto row0=interpolate(texel(texture,ix,iy,lane),texel(texture,ix+1,iy,lane),fx);
        const auto row1=interpolate(texel(texture,ix,iy+1,lane),texel(texture,ix+1,iy+1,lane),fx);
        auto filtered=interpolate(row0,row1,fy);
        if(lane==3U)filtered=filtered*alpha>>7U;
        image.rgb_coverage[(y*512U+x)*4U+lane]=static_cast<std::byte>(filtered);
      }
    }
  }
  return image;
}
} // namespace openrc
