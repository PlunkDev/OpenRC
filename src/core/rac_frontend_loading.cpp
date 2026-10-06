#include "openrc/rac_frontend_loading.hpp"
#include "openrc/ee_cop1_numeric.hpp"
#include "openrc/ps2_palette.hpp"

#include <algorithm>
#include <bit>

namespace openrc {
namespace {
[[noreturn]] void fail(const char *message) { throw RacFrontendTextureError(message); }
std::int32_t signed_word(std::uint32_t value) noexcept {
  return std::bit_cast<std::int32_t>(value);
}
std::uint32_t read32(std::span<const std::byte> bytes,std::size_t at) {
  if(at>bytes.size()||bytes.size()-at<4U) fail("Truncated loading-card field");
  std::uint32_t result=0;
  for(unsigned i=0;i<4;++i) result|=std::to_integer<std::uint32_t>(bytes[at+i])<<(8U*i);
  return result;
}
void put(std::span<std::byte> bytes,std::size_t at,std::uint64_t value,unsigned width=8U) noexcept {
  for(unsigned i=0;i<width;++i) bytes[at+i]=static_cast<std::byte>(value>>(8U*i));
}
bool card_gate(std::uint32_t dc,std::uint32_t e4) noexcept {
  return signed_word(dc)<3 && signed_word(e4)<0;
}
void validate(const RacFrontendLoadingCardStateV1 &state) {
  if(state.first_card>=17U||state.second_card>=17U||state.frame>1048576U||
      (signed_word(state.duration)>1048576))
    fail("Loading-card state exceeds its bounded source owner");
  if(state.render_pending && (signed_word(state.duration)<=0 || state.frame>=state.duration))
    fail("Loading-card render state is inconsistent with source loop gate");
}
} // namespace

RacFrontendLoadingAssetsV1 parse_rac_frontend_loading_assets_v1(
    std::span<const std::byte> source,std::uint32_t first,std::uint32_t second,
    RacFrontendTextureLimitsV1 limits) {
  if(source.size()>limits.max_input_bytes||source.size()>0xffffffffULL||source.size()<80U||read32(source,0)!=18U||
      first>=17U||second>=17U||limits.max_textures<3U)
    fail("Unsupported loading-card directory or limits");
  const std::array<std::uint32_t,3U> rows{0U,first+1U,second+1U};
  std::array<RacFrontendTextureEntryV1,3U> entries;
  std::uint64_t total_pixels=0,total_bytes=0;
  for(std::size_t i=0;i<rows.size();++i) {
    const auto at=read32(source,4U+rows[i]*4U);
    if(at<80U||at%16U||at>source.size()||source.size()-at<32U+1024U)
      fail("Loading-card PIF leaves its source owner");
    const auto width=i==0 ? 64U : 512U;
    if(read32(source,at)!=0x50494632U||read32(source,at+8U)!=width||
        read32(source,at+12U)!=64U||read32(source,at+16U)!=19U||
        read32(source,at+20U)!=0U||read32(source,at+24U)!=0U||read32(source,at+28U)!=1U)
      fail("Unsupported source loading-card PIF2 layout");
    const auto pixels=std::uint64_t(width)*64U;
    const auto bytes=2048U+5U*pixels;
    if(width>limits.max_width||64U>limits.max_height||pixels>limits.max_pixels_per_texture||
        pixels>limits.max_total_pixels-total_pixels||bytes>limits.max_total_output_bytes-total_bytes||
        pixels>source.size()-at-1056U||std::uint64_t(at)+1056U+pixels>0xffffffffULL)
      fail("Loading-card decoded texture exceeds source or output limit");
    total_pixels+=pixels;total_bytes+=bytes;
    auto &entry=entries[i];
    entry.source_index=rows[i];entry.width=width;entry.height=64;
    entry.palette_offset=at+32U;entry.pixel_offset=at+1056U;
    entry.table_entry_range={4U+rows[i]*4U,4U};
    entry.palette_range={entry.palette_offset,1024U};entry.pixel_range={entry.pixel_offset,pixels};
    for(std::size_t j=0;j<i;++j) {
      const auto begin=entries[j].palette_offset-32U;
      const auto end=entries[j].pixel_offset+entries[j].pixel_range.size;
      if(at<end&&begin<at+1056U+pixels &&
          (begin!=at||end!=at+1056U+pixels||entries[j].source_index!=rows[i]))
        fail("Loading-card PIF owners overlap without a source row alias");
    }
  }
  RacFrontendLoadingAssetsV1 result;
  const std::array<RacFrontendTextureV1*,3U> textures{&result.tile,&result.cards[0],&result.cards[1]};
  for(std::size_t i=0;i<textures.size();++i) {
    auto &texture=*textures[i];texture.entry=entries[i];
    const auto &entry=entries[i];
    std::copy_n(source.begin()+entry.palette_offset,1024U,texture.raw_palette.begin());
    for(unsigned index=0;index<256U;++index) {
      const auto raw=psmt8_clut_storage_index_v1(static_cast<std::uint8_t>(index))*4U;
      for(unsigned lane=0;lane<3U;++lane) texture.palette_rgba[index*4U+lane]=texture.raw_palette[raw+lane];
      texture.palette_rgba[index*4U+3U]=static_cast<std::byte>(ps2_alpha_to_rgba8_v1(
          std::to_integer<std::uint8_t>(texture.raw_palette[raw+3U])));
    }
    const auto pixels=source.subspan(entry.pixel_offset,entry.pixel_range.size);
    texture.indices.assign(pixels.begin(),pixels.end());texture.rgba.resize(pixels.size()*4U);
    for(std::size_t p=0;p<pixels.size();++p)
      std::copy_n(texture.palette_rgba.begin()+std::to_integer<unsigned>(pixels[p])*4U,4U,texture.rgba.begin()+p*4U);
  }
  return result;
}

RacFrontendLoadingCardBeginV1 begin_rac_frontend_loading_card_v1(
    std::uint32_t first,std::uint32_t second,std::uint32_t duration,bool load,
    std::uint32_t level,std::uint32_t dc,std::uint32_t e4) {
  RacFrontendLoadingCardBeginV1 result;
  result.state={first,second,0U,duration,load,signed_word(duration)>0&&card_gate(dc,e4)};
  validate(result.state);
  if(load) { result.requested_level=level;result.clear_level_counters=true; }
  return result;
}

RacFrontendLoadingCardStateV1 advance_rac_frontend_loading_card_v1(
    const RacFrontendLoadingCardStateV1 &state,std::uint32_t dc,std::uint32_t e4,
    std::optional<std::uint32_t> poll) {
  validate(state);
  if(!state.render_pending||state.load_pending!=poll.has_value())
    fail("Loading-card advance lacks its actual reached I/O completion");
  auto result=state;
  if(result.load_pending) {
    if(*poll!=0U) result.load_pending=false;
    else result.duration=std::max(result.duration,result.frame+20U);
  }
  ++result.frame;
  result.render_pending=result.frame<result.duration&&card_gate(dc,e4);
  validate(result);
  return result;
}

std::vector<RacFrontendLoadingDrawV1> execute_rac_frontend_loading_draws_v1(
    const RacFrontendLoadingCardStateV1 &state,std::uint32_t origin_y) {
  validate(state);
  if(!state.render_pending) fail("Loading-card draw is outside its reached source loop");
  auto alpha=state.frame<=31U ? state.frame*4U : 128U;
  if(signed_word(state.duration-16U)<signed_word(state.frame)) alpha=(state.duration-state.frame)*8U;
  const auto scroll=ee_cop1_mul_bits_v1(ee_cop1_cvt_s_w_bits_v1(state.frame%600U).bits,0x3ada740eU).bits;
  const std::array<std::uint32_t,4U> st{0U,0x40800000U,
      ee_cop1_add_bits_v1(scroll,0U).bits,ee_cop1_add_bits_v1(scroll,0x3ecccccdU).bits};
  std::vector<RacFrontendLoadingDrawV1> result;
  result.reserve(4U);
  const auto pair=[&](std::uint32_t y,std::uint32_t a,std::uint32_t card,
                      std::uint32_t st_pc,std::uint32_t card_pc) {
    result.push_back({st_pc,0U,{0U,y,512U,64U},(a<<24U)|0x00808080U,true,st});
    result.push_back({card_pc,card,{0U,y,512U,64U},0x80808080U,false,{}});
  };
  if(state.first_card==state.second_card) pair(origin_y-32U,alpha,1U,0x2330d0U,0x233118U);
  else {
    pair(origin_y-46U,alpha,1U,0x233170U,0x2331b0U);
    if(state.frame>=65U) {
      if(state.frame<96U) alpha=(state.frame-64U)*4U;
      // Source2331d4/23320c reload centre Y without another subtraction.
      pair(origin_y,alpha,2U,0x2331fcU,0x23322cU);
    }
  }
  return result;
}

RacIntegerQuadEmissionV1 emit_rac_frontend_stq_quad_v1(
    const RacFrontendStqQuadInputsV1 &input) noexcept {
  RacIntegerQuadInputsV1 integer;
  integer.rectangle_words=input.rectangle_words;integer.screen_offset_reads=input.screen_offset_reads;
  auto result=emit_rac_integer_quad_v1(integer);
  auto &packet=result.packet;
  put(packet,24U,0x0000052525252106ULL);
  put(packet,32U,input.tex0);put(packet,40U,0x54U);
  put(packet,48U,(input.rgbaq&0xffffffffULL)|0x3f80000000000000ULL);
  constexpr std::array<unsigned,4U> xs{0U,1U,0U,1U},ys{2U,2U,3U,3U};
  for(std::size_t i=0;i<4U;++i)
    put(packet,56U+i*16U,std::uint64_t(input.st_endpoint_bits[xs[i]])|
        (std::uint64_t(input.st_endpoint_bits[ys[i]])<<32U));
  return result;
}
} // namespace openrc
