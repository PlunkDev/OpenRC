#include "openrc/rac_frontend_environment_compile.hpp"

#include "openrc/elf.hpp"
#include "openrc/hash.hpp"
#include "openrc/dvp_vu_numeric.hpp"
#include "openrc/dvp_vu_execute.hpp"
#include "openrc/ee_cop1_numeric.hpp"
#include "openrc/rac_frontend_numeric.hpp"
#include "openrc/ps2_palette.hpp"
#include "openrc/scene_block_geometry.hpp"
#include "../runtime/level_scene_render_compile.hpp"

#include <algorithm>
#include <bit>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>

namespace openrc {
namespace {
[[noreturn]] void fail(const std::string &s) { throw RacFrontendEnvironmentError(s); }
std::span<const std::byte> slice(std::span<const std::byte> b,
    std::uint64_t at, std::uint64_t size) {
  if(at>b.size() || size>b.size()-at) fail("Frontend environment source range is outside its owner");
  return b.subspan(static_cast<std::size_t>(at),static_cast<std::size_t>(size));
}
std::uint32_t word(std::span<const std::byte> b,std::uint64_t at) {
  const auto v=slice(b,at,4U); std::uint32_t r=0U;
  for(unsigned i=0U;i<4U;++i)r|=std::to_integer<std::uint32_t>(v[i])<<(8U*i);
  return r;
}
std::uint16_t half(std::span<const std::byte> b,std::uint64_t at) {
  const auto v=slice(b,at,2U);
  return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(v[0]) |
      (std::to_integer<std::uint16_t>(v[1])<<8U));
}
std::span<const std::byte> gs_source(std::span<const std::byte> b,
    const RacFrontendEnvironmentLimitsV1 &limits) {
  const auto base=word(b,0U),count=word(b,8U),table=word(b,12U);
  if(count==0U || count>limits.max_gs_uploads)fail("Frontend environment GS upload count exceeds its limit");
  slice(b,table,std::uint64_t{count}*16U);
  std::uint64_t total=0U;
  for(std::uint32_t i=0U;i<count;++i) {
    const auto at=std::uint64_t{table}+i*16U;
    const auto format=word(b,at),wh=word(b,at+4U);
    const auto width=wh&65535U,height=wh>>16U;
    if(word(b,at+12U)!=total)fail("Frontend environment GS uploads are not contiguous");
    std::uint64_t bytes=0U;
    if(format==0U)bytes=1024U;
    else if(format==2U)bytes=512U;
    else if(format==19U && width && height)bytes=std::max(UINT64_C(256),std::uint64_t{width}*height);
    else fail("Frontend environment GS upload format is unsupported");
    if(total>limits.textures.max_gs_ram_bytes || bytes>limits.textures.max_gs_ram_bytes-total)
      fail("Frontend environment GS image exceeds its limit");
    total+=bytes;
  }
  return slice(b,base,total);
}
}

RacFrontendEnvironmentAssetsV1 decode_rac_frontend_environment_assets_v1(
    std::span<const std::byte> wad,RacFrontendEnvironmentLimitsV1 limits) {
  if(wad.size()<0x88U || wad.size()>limits.max_source_bytes || !limits.max_class_entries)
    fail("Frontend environment source or class limit is invalid");
  const auto shared=std::uint64_t{word(wad,4U)};
  std::set<std::uint64_t> starts{wad.size()};
  std::vector<RacFrontendEnvironmentClassV1> ties,shrubs;
  std::uint64_t total=0U;
  for(const auto [count_field,table_field,stride]:
      {std::array{0x18U,0x1cU,32U},std::array{0x20U,0x24U,32U},std::array{0x28U,0x2cU,48U}}) {
    const auto count=word(wad,count_field),table=word(wad,table_field);
    if(total>limits.max_class_entries || count>limits.max_class_entries-total)
      fail("Frontend environment class directories exceed aggregate limit");
    total+=count;
    slice(wad,table,std::uint64_t{count}*stride);
    std::set<std::uint32_t> ids;
    for(std::uint32_t i=0U;i<count;++i) {
      const auto row=std::uint64_t{table}+std::uint64_t{i}*stride;
      const auto relative=word(wad,row);
      const auto id=word(wad,row+4U);
      if(!ids.insert(id).second)fail("Frontend environment class directory repeats an identity");
      if(!relative) {
        if(count_field!=0x18U)fail("Frontend static environment class has no local payload");
        continue;
      }
      const auto at=shared+relative;
      slice(wad,at,1U);starts.insert(at);
      if(count_field==0x18U)continue;
      RacFrontendEnvironmentClassV1 entry;entry.class_id=id;entry.source_offset=at;
      bool end=false;
      for(std::size_t j=0U;j<16U;++j) {
        const auto t=std::to_integer<std::uint8_t>(wad[static_cast<std::size_t>(row)+16U+j]);
        entry.texture_slots[j]=t;
        if(t==255U)end=true;
        else if(end)fail("Frontend environment texture slot follows sentinel");
        else ++entry.used_texture_slot_count;
        if(stride==48U)entry.secondary_texture_slots[j]=std::to_integer<std::uint8_t>(wad[static_cast<std::size_t>(row)+32U+j]);
      }
      (count_field==0x20U?ties:shrubs).push_back(std::move(entry));
    }
  }
  for(const auto field:{0x10U,0x14U,0x60U,0x64U,0x68U,0x78U,0x7cU,0x80U,0x84U}) {
    const auto at=shared+word(wad,field);slice(wad,at,0U);starts.insert(at);
  }
  const auto resource=[&](std::uint64_t at) {
    const auto next=starts.upper_bound(at);
    if(next==starts.end())fail("Frontend environment resource has no next boundary");
    return slice(wad,at,*next-at);
  };
  const auto gs=gs_source(wad,limits);
  const auto texture_bank=[&](unsigned count_field,unsigned table_field) {
    return decode_rac_level_moby_texture_bank_v1(
        slice(wad,word(wad,table_field),std::uint64_t{word(wad,count_field)}*16U),
        wad,gs,shared+word(wad,0x60U),limits.textures);
  };
  RacFrontendEnvironmentAssetsV1 result;
  result.terrain_textures=texture_bank(0x30U,0x34U);
  result.tie_textures=texture_bank(0x40U,0x44U);
  result.shrub_textures=texture_bank(0x48U,0x4cU);
  const auto gameplay_source=resource(shared+word(wad,0x7cU));
  result.gameplay=parse_rac_gameplay_environment_v1(gameplay_source,limits.gameplay);
  result.gameplay_source.assign(gameplay_source.begin(),gameplay_source.end());
  result.terrain=parse_scene_block_directory_v1(resource(shared+word(wad,0x10U)),limits.terrain);
  // Header+78 is not an independently consumed resource boundary in 1eabe8;
  // its value can fall inside sky texels. The next installed pixel-store owner
  // is header+60. Bound the complete sky graph by that real owner boundary.
  const auto sky_begin=shared+word(wad,0x14U),sky_end=shared+word(wad,0x60U);
  if(sky_end<=sky_begin)fail("Frontend sky and texture-store owners overlap");
  const auto sky=slice(wad,sky_begin,sky_end-sky_begin);result.sky_source.assign(sky.begin(),sky.end());
  if(ties.size()!=result.gameplay.tie_class_ids.size() || shrubs.size()!=result.gameplay.shrub_class_ids.size())
    fail("Frontend environment gameplay and asset class counts disagree");
  for(std::size_t i=0U;i<ties.size();++i) {
    auto &entry=ties[i];
    if(entry.class_id!=result.gameplay.tie_class_ids[i])fail("Frontend TIE class order disagrees with gameplay");
    for(std::size_t j=0U;j<entry.used_texture_slot_count;++j)
      if(entry.texture_slots[j]>=result.tie_textures.textures.size())fail("Frontend TIE texture slot is outside bank");
    const auto b=resource(entry.source_offset);
    auto model=parse_rac_tie_class_v1(b,limits.tie_class);
    if(model.texture_count>entry.used_texture_slot_count)fail("Frontend TIE model exceeds bound material slots");
    result.tie_models.push_back({entry.class_id,entry.texture_slots,entry.used_texture_slot_count,std::move(model)});
    entry.source_bytes.assign(b.begin(),b.end());
  }
  for(std::size_t i=0U;i<shrubs.size();++i) {
    auto &entry=shrubs[i];
    if(entry.class_id!=result.gameplay.shrub_class_ids[i])fail("Frontend shrub class order disagrees with gameplay");
    for(std::size_t j=0U;j<entry.used_texture_slot_count;++j)
      if(entry.texture_slots[j]>=result.shrub_textures.textures.size())fail("Frontend shrub texture slot is outside bank");
    const auto b=resource(entry.source_offset);entry.source_bytes.assign(b.begin(),b.end());
  }
  result.shrub_classes=std::move(shrubs);
  result.tie_classes=std::move(ties);
  return result;
}

RacFrontendFogV1 compile_rac_frontend_fog_v1(
    const std::span<const std::byte> gameplay_source) {
  const auto settings=word(gameplay_source,0U);
  const auto source=slice(gameplay_source,settings,0x28U);
  RacFrontendFogV1 out;
  // 1ea040..1ea06c performs LBU on each four-byte color field.
  for(unsigned channel=0U;channel<3U;++channel)
    out.rgb8|=(word(source,0x0cU+channel*4U)&255U)<<(channel*8U);
  out.near_distance_bits=word(source,0x18U);
  out.far_distance_bits=word(source,0x1cU);
  out.near_factor_bits=word(source,0x20U);
  out.far_factor_bits=word(source,0x24U);
  const auto finite=[](std::uint32_t bits) {return (bits&0x7f800000U)!=0x7f800000U;};
  for(const auto bits:{out.near_distance_bits,out.far_distance_bits,
                       out.near_factor_bits,out.far_factor_bits})
    if(!finite(bits))fail("Frontend fog setting is outside the finite source domain");
  const auto near_distance=std::bit_cast<float>(out.near_distance_bits);
  const auto far_distance=std::bit_cast<float>(out.far_distance_bits);
  const auto near_factor=std::bit_cast<float>(out.near_factor_bits);
  const auto far_factor=std::bit_cast<float>(out.far_factor_bits);
  if(near_distance<0.0F || far_distance<=near_distance || far_factor<0.0F ||
     near_factor>255.0F || far_factor>=near_factor)
    fail("Frontend fog setting is outside the qualified decreasing factor domain");
  const auto mul=[](auto a,auto b){return ee_cop1_mul_bits_v1(a,b).bits;};
  const auto sub=[](auto a,auto b){return ee_cop1_sub_bits_v1(a,b).bits;};
  const auto div=[](auto a,auto b){return ee_cop1_div_bits_v1(a,b).bits;};
  // 1f32f8..1f33c8; keep the separate source product and subtraction.
  const auto distance_range=sub(out.far_distance_bits,out.near_distance_bits);
  const auto factor_range=sub(out.far_factor_bits,out.near_factor_bits);
  const auto scaled_distance_range=mul(distance_range,0x3a800000U);
  out.world_gradient_bits=div(factor_range,scaled_distance_range);
  out.projection_scale_bits=div(mul(factor_range,0x42000000U),distance_range);
  out.projection_offset_bits=div(sub(mul(out.near_factor_bits,out.far_distance_bits),
      mul(out.far_factor_bits,out.near_distance_bits)),distance_range);
  out.world_offset_bits=sub(out.near_factor_bits,
      mul(mul(out.near_distance_bits,0x3a800000U),out.world_gradient_bits));
  // 1f33e4 / 1f3468; frontend projection near distance is source constant32.
  out.depth_to_w_bits=mul(div(0x3f800000U,0x42000000U),out.projection_scale_bits);
  return out;
}

std::uint8_t evaluate_rac_frontend_terrain_fog_v1(
    const RacFrontendFogV1 &fog,const std::uint32_t projected_w) {
  auto value=dvp_vu_add_bits_v1(projected_w,fog.projection_offset_bits).bits;
  // Inputs and these clamps are positive finite factors. This is the
  // emitter's MAXy then MINIz, followed by FTOI4.w (not round-to-nearest).
  if((value&0x80000000U)!=0U || value<fog.far_factor_bits)value=fog.far_factor_bits;
  if(value>fog.near_factor_bits)value=fog.near_factor_bits;
  return static_cast<std::uint8_t>((dvp_vu_ftoi_bits_v1(value,4)>>4U)&255U);
}

namespace {
std::uint32_t sky_random(std::uint32_t &state) {
  state=state*UINT32_C(0x41c64e6d)+12345U;return state&UINT32_C(0x7fffffff);
}
std::uint32_t sky_random_limit(std::uint32_t &state,std::uint32_t limit) {
  return ((sky_random(state)>>16U)&0x7fffU)%limit;
}
std::uint32_t sky_mul(std::uint32_t a,std::uint32_t b) {return ee_cop1_mul_bits_v1(a,b).bits;}
std::uint32_t sky_add(std::uint32_t a,std::uint32_t b) {return ee_cop1_add_bits_v1(a,b).bits;}
std::uint32_t sky_integer(std::int32_t value) {
  return ee_cop1_cvt_s_w_bits_v1(std::bit_cast<std::uint32_t>(value)).bits;
}
std::uint32_t sky_random_angle(std::uint32_t &state) {
  const auto integer=static_cast<std::int32_t>((sky_random(state)>>16U)&4095U)-2048;
  return sky_mul(sky_mul(sky_integer(integer),0x40490fdbU),0x3a000000U);
}
std::uint32_t sky_trig(const RacFrontendSkySpriteSourceV1 &source,
    std::uint32_t angle,bool cosine) {
  auto state=make_dvp_vu_execution_state_v1();state.vf[1].lanes[0]={angle,UINT32_C(0xffffffff)};
  const auto result=execute_dvp_vu_program_v1(source.trigonometry,std::move(state),
      {static_cast<std::uint16_t>((cosine?0xc80U:0xc90U)/8U),false},{32U,1U,1U,1U});
  const auto output=result.final_state.vf[1].lanes[0];
  if(result.termination!=DvpVuTerminationV1::program_end || output.known_mask!=UINT32_C(0xffffffff))
    fail("Frontend sky trigonometry did not return a fully known source result");
  return output.bits;
}
void sky_position(const RacFrontendSkySpriteSourceV1 &source,
    RacFrontendSkySpriteV1 &sprite,std::uint32_t azimuth,std::uint32_t elevation) {
  // 1f9f90 is source cosine, 1f9fa8 source sine. Each source call has a
  // separate COP1 multiply; moving sprites reflect Z after cosine.
  const auto cosine0=sky_trig(source,azimuth,true);
  const auto sine1=sky_trig(source,elevation,false);
  sprite.position_bits[0]=sky_mul(sky_mul(cosine0,sine1),0x42480000U);
  const auto sine0=sky_trig(source,azimuth,false);
  const auto sine1_again=sky_trig(source,elevation,false);
  sprite.position_bits[1]=sky_mul(sky_mul(sine0,sine1_again),0x42480000U);
  auto z=sky_trig(source,elevation,true);
  if(!sprite.stationary)z&=UINT32_C(0x7fffffff);
  sprite.position_bits[2]=sky_mul(z,0x42480000U);
}
}

RacFrontendSkySpriteSourceV1 make_rac_frontend_sky_sprite_source_v1(
    std::span<const std::byte> elf_bytes) {
  if(elf_bytes.size()>64U*1024U*1024U)fail("Frontend sky ELF exceeds its source limit");
  Sha256 hash;hash.update(elf_bytes);
  if(hex_digest(hash.finish())!="17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b")
    fail("Frontend sky ELF is outside the qualified resident program");
  const auto elf=inspect_elf(elf_bytes);
  if(!elf.dvp_overlay_table)fail("Frontend sky ELF has no DVP overlay table");
  std::vector<ElfDvpOverlay> overlays;
  for(const auto &o:elf.dvp_overlay_table->overlays)
    if(o.name==".DVP.overlay..0xc80.28259.24.0" && o.load_memory_address==0x10e4d0U &&
       o.virtual_memory_address==0xc80U && o.size==0x2f0U)overlays.push_back(o);
  if(overlays.size()!=1U)fail("Frontend sky trigonometry overlay is missing or ambiguous");
  constexpr std::array<std::uint16_t,2> entries{0xc80U/8U,0xc90U/8U};
  RacFrontendSkySpriteSourceV1 out;
  out.trigonometry=decode_dvp_vu_program_v1(elf_bytes,overlays,entries,
      {64U*1024U*1024U,1U,0x2f0U,2U,256U,512U});
  const auto resident=[&](std::uint32_t address) {
    std::optional<std::array<std::uint32_t,4U>> value;
    for(const auto &p:elf.program_headers)
      if(p.type==1U && address>=p.virtual_address && address-p.virtual_address<=p.file_size &&
          16U<=p.file_size-(address-p.virtual_address)) {
        if(value)fail("Frontend sky coefficient has ambiguous resident ownership");
        std::array<std::uint32_t,4U> row;
        for(unsigned lane=0U;lane<4U;++lane)
          row[lane]=word(elf_bytes,std::uint64_t{p.file_offset}+address-p.virtual_address+lane*4U);
        value=row;
      }
    if(!value)fail("Frontend sky coefficient is not resident file data");
    return *value;
  };
  out.atan_coefficients0=resident(0x1de6a0U);
  out.atan_coefficients1=resident(0x1de6b0U);
  out.atan_positive_quadrants=resident(0x1de6c0U);
  out.billboard_axes=resident(0x1defb0U);
  return out;
}

RacFrontendSkySpritesV1 initialize_rac_frontend_sky_sprites_v1(
    const RacFrontendSkySpriteSourceV1 &source,std::span<const std::byte> sky) {
  if(sky.size()>64U*1024U*1024U || half(sky,4U)!=0U || half(sky,8U)!=0U || half(sky,10U)!=256U)
    fail("Frontend sky sprite state is outside its initialization owner");
  const auto records=slice(sky,word(sky,0x1cU),256U*32U);
  RacFrontendSkySpritesV1 out;out.random_state=12345U;
  for(std::size_t i=0U;i<out.sprites.size();++i) {
    auto &sprite=out.sprites[i];sprite.stationary=i<246U;
    // Moving particles retain the source allocation's rotation field.
    sprite.rotation_bits=word(records,i*32U+8U);
    if(!sprite.stationary) {
      sprite.phase0=static_cast<std::uint16_t>(sky_random(out.random_state)>>16U);
      sprite.phase1=static_cast<std::uint16_t>(sky_random(out.random_state)>>16U);
      sprite.size_bits=0x3e23d70aU;continue;
    }
    (void)sky_random_limit(out.random_state,256U); // overwritten by base color
    sprite.rotation_bits=sky_random_angle(out.random_state);
    sprite.size_bits=sky_mul(sky_integer(static_cast<std::int32_t>(sky_random_limit(out.random_state,24U)+32U)),0x3b800000U);
    auto azimuth=sky_add(0xc0400000U,sky_mul(sky_random_angle(out.random_state),0x3e4ccccdU));
    // 1fa748 wraps once, retaining the pre-wrap negative comparison.
    const auto value=std::bit_cast<float>(azimuth);
    if(value>=std::bit_cast<float>(UINT32_C(0x40490fdb))) {
      azimuth=ee_cop1_sub_bits_v1(azimuth,0x40490fdbU).bits;
      azimuth=ee_cop1_sub_bits_v1(azimuth,0x40490fdbU).bits;
    }
    if(value<std::bit_cast<float>(UINT32_C(0xc0490fdb))) {
      azimuth=sky_add(azimuth,0x40490fdbU);azimuth=sky_add(azimuth,0x40490fdbU);
    }
    const auto elevation=sky_add(sky_mul(sky_random_angle(out.random_state),0x3db851ecU),0x3f99999aU);
    sky_position(source,sprite,azimuth,elevation);
    const auto color=sky_random_limit(out.random_state,24U);
    const auto alpha=sky_random_limit(out.random_state,32U)<<24U;
    if(((sky_random(out.random_state)>>16U)&1U)!=0U)
      sprite.base_rgba8=(color<<16U)+0x30505050U+alpha;
    else sprite.base_rgba8=((color<<8U)+0x30505050U+alpha)|color;
  }
  step_rac_frontend_sky_sprites_v1(source,out);return out;
}

void step_rac_frontend_sky_sprites_v1(const RacFrontendSkySpriteSourceV1 &source,
    RacFrontendSkySpritesV1 &state) {
  if(state.update_count==UINT32_MAX)fail("Frontend sky update count exceeds its bound");
  for(auto &sprite:state.sprites) {
    if(sprite.stationary) {
      const auto random=sky_random(state.random_state)>>16U;
      sprite.rgba8=sprite.base_rgba8+((random&0x1f00U)<<10U)+0xffdfdfe0U+
          ((random&0x1f0U)<<6U)+((random&0x1fU)<<2U);
    } else {
      ++sprite.phase0;++sprite.phase1;
      const auto azimuth=sky_mul(sky_integer(static_cast<std::int32_t>(sprite.phase0&4095U)-2048),0x3ac90fdbU);
      const auto elevation=sky_mul(sky_integer(static_cast<std::int32_t>(sprite.phase1&4095U)-2048),0x3ac90fdbU);
      sky_position(source,sprite,azimuth,elevation);
      sprite.rgba8=(sprite.phase0&63U)<8U?0x702020f0U:0x202020f0U;
    }
  }
  ++state.update_count;
}

RacFrontendSkyBillboardFrameV1 sample_rac_frontend_sky_billboards_v1(
    const RacFrontendSkySpriteSourceV1 &source,const RacFrontendSkySpritesV1 &sprites,
    const std::array<std::array<std::uint32_t,4U>,4U> &view,
    const RacFrontendFogV1 &fog) {
  using Vector=std::array<std::uint32_t,4U>;
  constexpr std::uint32_t one=0x3f800000U,near=0x42000000U,far=0x49360000U;
  const auto finite=[](std::uint32_t bits){return (bits&0x7f800000U)!=0x7f800000U;};
  if(sprites.update_count==0U || source.trigonometry.instructions.empty() ||
      source.billboard_axes!=Vector{one,0x3f880000U,0U,0U} ||
      source.atan_positive_quadrants!=Vector{one,0U,0xbf800000U,0x3fc90fdbU} ||
      view[3]!=Vector{0U,0U,0U,one})
    fail("Frontend billboard inputs lack the qualified source initialization");
  for(unsigned column=0U;column<3U;++column) {
    if(view[column][3]!=0U)fail("Frontend billboard view is not rotation-only");
    for(const auto lane:view[column])if(!finite(lane))fail("Frontend billboard view is not finite");
  }
  if(!finite(fog.projection_scale_bits) || !finite(fog.projection_offset_bits) ||
      !finite(fog.depth_to_w_bits) || (fog.projection_scale_bits&0x7fffffffU)==0U ||
      (fog.depth_to_w_bits&0x7fffffffU)==0U)
    fail("Frontend billboard fog projection is outside the source domain");
  const auto emul=[](auto a,auto b){return ee_cop1_mul_bits_v1(a,b).bits;};
  const auto eadd=[](auto a,auto b){return ee_cop1_add_bits_v1(a,b).bits;};
  const auto esub=[](auto a,auto b){return ee_cop1_sub_bits_v1(a,b).bits;};
  const auto ediv=[](auto a,auto b){return ee_cop1_div_bits_v1(a,b).bits;};
  const auto vmul=[](auto a,auto b){return dvp_vu_mul_bits_v1(a,b).bits;};
  const auto vadd=[](auto a,auto b){return dvp_vu_add_bits_v1(a,b).bits;};
  const auto vsub=[](auto a,auto b){return dvp_vu_sub_bits_v1(a,b).bits;};
  const auto atan=[&](std::uint32_t tangent) {
    // Reached1fa058 branch has positive tangent<1, quadrant entry0.
    const auto u=ediv(esub(tangent,one),eadd(tangent,one));
    const auto u2=vmul(u,u),u4=vmul(u2,u2),u8=vmul(u4,u4);
    Vector first{u,vmul(u,u2),vmul(u,u4),vmul(vmul(u,u4),u2)},second;
    for(unsigned lane=0U;lane<4U;++lane) {
      second[lane]=vmul(vmul(first[lane],u8),source.atan_coefficients1[lane]);
      first[lane]=vmul(first[lane],source.atan_coefficients0[lane]);
    }
    const auto sum=dvp_vu_add_bits_v1(first[0],first[1]);
    DvpVuAccumulatorLaneV1 acc{sum.bits,sum.overflow};
    for(const auto term:{first[2],first[3],second[0],second[1],second[2],second[3]}) {
      const auto next=dvp_vu_madd_bits_v1(acc,one,term);acc={next.result.bits,next.result.overflow};
    }
    return eadd(emul(eadd(0x3f490fdbU,acc.bits),source.atan_positive_quadrants[0]),source.atan_positive_quadrants[1]);
  };
  RacFrontendSkyBillboardFrameV1 out;
  out.tangent_bits={0x3f2147aeU,emul(0x3f2147aeU,0x3f418937U)};
  for(unsigned lane=0U;lane<2U;++lane)
    out.frustum_bits[lane]=ediv(one,sky_trig(source,atan(out.tangent_bits[lane]),true));
  // 1f3140 ->1fa540 ->22cf90. First three projected view columns are
  // scaled by1024 before VIF; translation retains the original raw depth.
  const auto range=emul(near,esub(far,near));
  const auto depth=emul(ediv(eadd(far,near),range),0xcafffbe0U);
  const auto translation=emul(ediv(emul(emul(near,0xc0000000U),far),range),0xcafffbe0U);
  const std::array<Vector,4U> projection{{
      {ediv(0x43800000U,emul(out.tangent_bits[0],near)),0U,0U,0U},
      {0U,ediv(0x43600000U,emul(out.tangent_bits[1],near)),0U,0U},
      {0U,0U,depth,fog.depth_to_w_bits},{0U,0U,translation,0U}}};
  std::array<Vector,4U> projected_view;
  for(unsigned column=0U;column<4U;++column) {
    projected_view[column]=rac_frontend_transform4_v1(view[column],projection);
    if(column<3U)for(auto &lane:projected_view[column])lane=vmul(lane,0x44800000U);
  }
  for(std::size_t ordinal=0U;ordinal<sprites.sprites.size();++ordinal) {
    const auto &sprite=sprites.sprites[ordinal];
    if(!finite(sprite.size_bits) || (sprite.size_bits&0x80000000U)!=0U ||
        (sprite.size_bits&0x7fffffffU)==0U || !finite(sprite.rotation_bits))
      fail("Frontend billboard size or rotation is outside its source domain");
    for(const auto lane:sprite.position_bits)
      if(!finite(lane))fail("Frontend billboard position is outside its source domain");
    Vector center{};
    for(unsigned lane=0U;lane<3U;++lane) {
      const auto product=dvp_vu_mul_bits_v1(view[0][lane],sprite.position_bits[0]);
      DvpVuAccumulatorLaneV1 acc{product.bits,product.overflow};
      for(unsigned component=1U;component<3U;++component) {
        const auto next=dvp_vu_madd_bits_v1(acc,view[component][lane],sprite.position_bits[component]);
        acc={next.result.bits,next.result.overflow};
      }
      center[lane]=acc.bits;
    }
    bool visible=true;
    for(unsigned lane=0U;lane<2U;++lane) {
      const auto extent=vmul(out.frustum_bits[lane],sprite.size_bits);
      const auto adjusted=vsub(center[lane]&0x7fffffffU,extent);
      if((vsub(vmul(out.tangent_bits[lane],center[2]),adjusted)&0x80000000U)!=0U)visible=false;
    }
    if(!visible)continue;
    RacFrontendSkyBillboardV1 billboard;
    billboard.sprite_ordinal=static_cast<std::uint16_t>(ordinal);billboard.rgba8=sprite.rgba8;
    billboard.size_q_bits=dvp_vu_div_bits_v1(vmul(sprite.size_bits,0x44600000U),center[2]);
    const auto cosine=vmul(billboard.size_q_bits,sky_trig(source,sprite.rotation_bits,true));
    const auto sine=vmul(billboard.size_q_bits,sky_trig(source,sprite.rotation_bits,false));
    Vector projected;
    for(unsigned lane=0U;lane<4U;++lane) {
      // 221571 f0..108 starts with translation, then X,Y,Z products.
      const auto product=dvp_vu_mul_bits_v1(projected_view[3][lane],one);
      DvpVuAccumulatorLaneV1 acc{product.bits,product.overflow};
      for(unsigned component=0U;component<3U;++component) {
        const auto next=dvp_vu_madd_bits_v1(acc,projected_view[component][lane],sprite.position_bits[component]);
        acc={next.result.bits,next.result.overflow};
      }
      projected[lane]=acc.bits;
    }
    billboard.projection_q_bits=dvp_vu_div_bits_v1(fog.projection_scale_bits,projected[3]);
    for(unsigned lane=0U;lane<3U;++lane)projected[lane]=vmul(projected[lane],billboard.projection_q_bits);
    const std::array<std::uint32_t,2U> first{vmul(source.billboard_axes[0],cosine),vmul(source.billboard_axes[1],sine)};
    const std::array<std::uint32_t,2U> second{vsub(0U,sine),vmul(source.billboard_axes[1],cosine)};
    for(unsigned vertex=0U;vertex<4U;++vertex)for(unsigned lane=0U;lane<2U;++lane) {
      const auto offset=(vertex==0U || vertex==3U)?first[lane]:second[lane];
      const auto origin=vertex<2U?vadd(0x45000000U,offset):vsub(0x45000000U,offset);
      billboard.xy16[vertex][lane]=static_cast<std::uint16_t>(dvp_vu_ftoi_bits_v1(vadd(origin,projected[lane]),4));
    }
    // Caller-unknown VF12/13.ZW are multiplied by architectural0 at entry0.
    // All16 signed-zero combinations disappear when added to these positive
    // origins; the source probe compares full original packets for every case.
    billboard.z24=(dvp_vu_ftoi_bits_v1(vadd(0x4afffc20U,projected[2]),4)>>4U)&0x00ffffffU;
    billboard.fog=static_cast<std::uint8_t>((dvp_vu_ftoi_bits_v1(fog.projection_offset_bits,4)>>4U)&255U);
    out.billboards.push_back(billboard);
  }
  return out;
}

RacFrontendTerrainCompileResultV1 compile_rac_frontend_terrain_v1(
    const RacFrontendEnvironmentAssetsV1 &assets,std::span<const std::byte> elf_bytes,
    RenderSceneLimitsV1 render_limits) {
  const auto limits=runtime::make_level_scene_recovery_limits_v1();
  if(elf_bytes.size()>limits.scene_block_load.max_elf_bytes)fail("Frontend terrain ELF exceeds its limit");
  Sha256 hash;hash.update(elf_bytes);
  if(hex_digest(hash.finish())!="17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b")
    fail("Frontend terrain ELF is outside the qualified resident program");
  const auto elf=inspect_elf(elf_bytes);
  if(!elf.dvp_overlay_table)fail("Frontend terrain ELF has no DVP overlay table");
  std::vector<ElfDvpOverlay> overlays;
  for(const auto &o:elf.dvp_overlay_table->overlays)
    if(o.name.find(".55907.")!=std::string::npos)overlays.push_back(o);
  constexpr std::array<std::uint16_t,7U> entries{0U,6U,8U,10U,14U,16U,20U};
  const auto program=decode_dvp_vu_program_v1(elf_bytes,overlays,entries,limits.scene_block_load.program);
  std::optional<std::span<const std::byte>> preamble_bytes;
  constexpr auto va=UINT64_C(0x1deac0),size=UINT64_C(17)*16U;
  for(const auto &p:elf.program_headers)if(p.type==1U && va>=p.virtual_address && va-p.virtual_address<=p.file_size && size<=p.file_size-(va-p.virtual_address)) {
    if(preamble_bytes)fail("Frontend terrain preamble has ambiguous ELF ownership");
    preamble_bytes=slice(elf_bytes,std::uint64_t{p.file_offset}+va-p.virtual_address,size);
  }
  if(!preamble_bytes)fail("Frontend terrain preamble is not file backed");
  const auto preamble=parse_scene_block_task_preamble_v1(*preamble_bytes);
  const auto profile=runtime::make_level_scene_recovery_profile_v1();
  auto initialized=std::make_unique<SceneBlockTaskInitializationResultV1>(
      initialize_scene_block_task_execution_v1(program,preamble,profile.frame_input,limits.scene_block_execution.task.dvp));
  if(!initialized->ready_state)fail("Frontend terrain initialization did not complete");
  if(assets.terrain.entries.empty() || assets.terrain.entries.size()>limits.max_aggregate_records)
    fail("Frontend terrain record count is outside its limit");
  runtime::LevelSceneRecoveryResultV1 recovered;
  std::vector<runtime::SceneGeometry3dV1> pieces;
  RacFrontendTerrainCompileResultV1 result;
  for(const auto &entry:assets.terrain.entries) {
    auto execution=std::make_unique<SceneBlockTaskRecordExecutionV1>(execute_scene_block_task_record_v1(
        entry,16U,program,*initialized->ready_state,limits.scene_block_execution.task));
    if(execution->vu_execution.termination!=DvpVuTerminationV1::program_end) {
      const auto &v=execution->vu_execution;std::ostringstream message;
      message<<"Frontend terrain record "<<result.executed_records<<" did not reach source E termination; reason="
        <<static_cast<unsigned>(v.termination)<<" pairs="<<v.executed_instruction_pairs<<std::hex
        <<" pc="<<v.stopped_instruction_address.value_or(v.final_state.pc);
      for(std::size_t i=0U;i<v.final_state.vi.size();++i)
        message<<" vi"<<i<<'='<<v.final_state.vi[i].bits<<'/'<<v.final_state.vi[i].known_mask;
      const auto report=[&](const char *name,const DvpVuWordV1 w){message<<' '<<name<<'='<<w.bits<<'/'<<w.known_mask;};
      report("MAC",v.final_state.mac_flags);report("STATUS",v.final_state.status_flags);report("CLIP",v.final_state.clip_flags);
      for(const auto w:v.final_state.accumulator_overflow)report("ACCoverflow",w);
      message<<" trace=";
      const auto first=v.instruction_trace.size()>32U?v.instruction_trace.size()-32U:0U;
      for(auto i=first;i<v.instruction_trace.size();++i)message<<v.instruction_trace[i]<<',';
      for(std::size_t q=0U;q<9U;++q) {
        message<<" ram"<<q<<'=';
        for(const auto w:v.final_state.data_memory[q].lanes)message<<w.bits<<'/'<<w.known_mask<<',';
      }
      fail(message.str());
    }
    const auto &events=execution->vu_execution.xgkick_events;
    if(events.empty() || std::any_of(events.begin(),events.end(),[](const auto &e){return !e.packet_complete;}))
      fail("Frontend terrain record has no complete GS stream");
    const auto gs=decode_dvp_vu_xgkick_gs_stream_v1(events,limits.scene_block_execution.gs);
    const auto geometry=recover_scene_block_source_geometry_v1(*initialized->ready_state,*execution,gs,
        {limits.scene_block_execution.gs.max_vertices,limits.scene_block_execution.task.bridge});
    auto piece=runtime::build_scene_geometry_3d_material_v1(geometry,gs);
    if(piece.unresolved_material_triangle_count || piece.vertices_without_stq)
      fail("Frontend terrain record has unresolved source texture data");
    if(result.source_vertices>render_limits.max_vertices || piece.geometry.vertices.size()>render_limits.max_vertices-result.source_vertices ||
       result.source_triangles>render_limits.max_triangle_indices/3U || piece.geometry.emitted_triangle_count>render_limits.max_triangle_indices/3U-result.source_triangles)
      fail("Frontend terrain aggregate geometry exceeds neutral limits");
    for(auto batch:piece.material_batches) {
      if(!batch.texture_index || *batch.texture_index>=assets.terrain_textures.textures.size())
        fail("Frontend terrain material references a missing texture");
      batch.first_triangle+=result.source_triangles;
      recovered.terrain_material_batches.push_back(batch);
    }
    result.source_vertices+=piece.geometry.vertices.size();
    result.source_triangles+=piece.geometry.emitted_triangle_count;
    pieces.push_back(std::move(piece.geometry));++result.executed_records;
  }
  recovered.source=runtime::merge_scene_geometries_3d_v1(pieces,{render_limits.max_vertices,render_limits.max_triangle_indices});
  recovered.terrain_triangle_count=result.source_triangles;
  recovered.tfrag_texture_bank=assets.terrain_textures;
  for(auto &texture:recovered.tfrag_texture_bank->textures) {
    if(texture.indices.size()>texture.rgba.size()/4U ||
       texture.indices.size()*4U!=texture.rgba.size())
      fail("Frontend terrain texture has inconsistent raw palette indices");
    for(std::size_t p=0U;p<texture.indices.size();++p) {
      const auto index=std::to_integer<std::size_t>(texture.indices[p]);
      std::copy_n(texture.palette_raw_rgba.begin()+index*4U,4U,
                  texture.rgba.begin()+p*4U);
    }
  }
  result.render_scene=runtime::compile_level_scene_render_v1(recovered,{1024.0F,render_limits});
  // The original frontend binder 204918 writes TCC=RGBA, TFX=MODULATE
  // (204ac4..204ad8), and CLAMP=0 (204b78). Keep integer source channels:
  // the preview compiler's sRGB interpretation and /255 modulation do not
  // express those registers.
  for(auto &texture:result.render_scene.textures)
    texture.color_space=RenderSceneTextureColorSpaceV1::linear;
  for(auto &material:result.render_scene.materials) {
    material.color_math=RenderSceneColorMathV1::encoded_integer;
    material.texture_modulation_denominator=128U;
    // 1e9e90..1e9e9c restores TEST=5360b after the sky: GEQUAL96,
    // failed fragments write RGB only. Seed1deaf0 has PRIM.ABE=1 and
    // 22c53c..22c548 restores source-over ALPHA with denominator128.
    material.blend_mode=RenderSceneBlendModeV1::source_over;
    material.blend_denominator=128U;
    material.alpha_mode=RenderSceneAlphaModeV1::mask;
    material.alpha_cutoff_rgba8=96U;
    material.alpha_failure=RenderSceneAlphaFailureV1::rgb_only;
  }
  // A source-over triangle with source alpha128 is exactly opaque. Prove
  // that alpha for every texel/mip and every referenced vertex of a material;
  // interpolating a constant128 preserves128. This removes destination reads
  // only when both GEQUAL96 and the /128 blend reduce identically.
  std::vector<bool> opaque(result.render_scene.materials.size(),true);
  for(const auto &material:result.render_scene.materials) {
    if(!material.base_color_texture_id || !material.use_vertex_color ||
       material.base_color_rgba8!=UINT32_C(0xffffffff)) {
      opaque[material.id]=false;continue;
    }
    for(const auto &mip:result.render_scene.textures[*material.base_color_texture_id].mips)
      for(std::size_t at=3U;at<mip.rgba8.size();at+=4U)
        if(mip.rgba8[at]!=std::byte{128U})opaque[material.id]=false;
  }
  for(const auto &mesh:result.render_scene.meshes)
    for(const auto &draw:mesh.draw_ranges)
      for(auto at=draw.first_index;at<draw.first_index+draw.index_count;++at)
        if((mesh.vertices[mesh.triangle_indices[at]].rgba8>>24U)!=128U)
          opaque[draw.material_id]=false;
  for(auto &material:result.render_scene.materials)if(opaque[material.id]) {
    material.blend_mode=RenderSceneBlendModeV1::opaque;
    material.blend_denominator=255U;
    material.alpha_mode=RenderSceneAlphaModeV1::opaque;
    material.alpha_cutoff_rgba8=0U;
    material.alpha_failure=RenderSceneAlphaFailureV1::discard;
    ++result.opaque_materials;
  }
  for(const auto &mesh:result.render_scene.meshes)
    for(const auto &draw:mesh.draw_ranges)
      if(opaque[draw.material_id])result.opaque_triangles+=draw.index_count/3U;
  result.render_scene=canonicalize_render_scene_v1(std::move(result.render_scene),render_limits);
  return result;
}

RacFrontendSkyCompileResultV1 compile_rac_frontend_sky_shells_v1(
    const std::span<const std::byte> source, const RenderSceneLimitsV1 limits) {
  if(source.size()>UINT64_C(64)*1024U*1024U)
    fail("Frontend sky source exceeds its bounded owner");
  slice(source,0U,0x40U);
  // This is the reached frontend owner, not the other sky modes whose shell
  // matrices and ordering differ. Active sprites are allocated by 22c188.
  if(half(source,4U)!=0U || half(source,6U)!=4U || half(source,8U)!=0U ||
     half(source,10U)!=256U || half(source,12U)!=8U || half(source,14U)!=4U)
    fail("Frontend sky header is outside the qualified four-shell owner");
  const auto definitions=word(source,0x10U), pixels=word(source,0x14U);
  slice(source,definitions,8U*16U);
  slice(source,word(source,0x18U),4U);
  slice(source,word(source,0x1cU),256U*32U);
  RacFrontendSkyCompileResultV1 result;
  auto &scene=result.render_scene;
  std::map<std::uint8_t,std::uint32_t> texture_ids;
  std::uint64_t texture_bytes=0U, draw_ranges=0U;
  const auto texture=[&](const std::uint8_t index) -> std::uint32_t {
    if(const auto found=texture_ids.find(index);found!=texture_ids.end())return found->second;
    // The authored shells use 4..7; 0..3 belong to the separate sprite owner.
    if(index<4U || index>=8U || scene.textures.size()>=limits.max_textures)
      fail("Frontend sky shell texture is outside its binding domain");
    const auto at=std::uint64_t{definitions}+std::uint64_t{index}*16U;
    const auto width=word(source,at+8U),height=word(source,at+12U);
    const auto texels=std::uint64_t{width}*height;
    if(!width || !height || (width&(width-1U)) || (height&(height-1U)) ||
       width>limits.max_texture_width || height>limits.max_texture_height ||
       texels>limits.max_texels_per_texture || texture_bytes>limits.max_total_rgba8_bytes ||
       texels>(limits.max_total_rgba8_bytes-texture_bytes)/4U)
      fail("Frontend sky texture dimensions exceed neutral limits");
    const auto palette=slice(source,std::uint64_t{pixels}+word(source,at),1024U);
    const auto indices=slice(source,std::uint64_t{pixels}+word(source,at+4U),texels);
    RenderSceneTextureMipV1 mip; mip.width=width;mip.height=height;
    mip.rgba8.resize(static_cast<std::size_t>(texels)*4U);
    for(std::size_t p=0U;p<indices.size();++p) {
      const auto logical=std::to_integer<std::uint8_t>(indices[p]);
      const auto storage=psmt8_clut_storage_index_v1(logical);
      // Keep source alpha 0..128. Expanding it to 0..255 changes GS blending.
      for(std::size_t c=0U;c<4U;++c)mip.rgba8[p*4U+c]=palette[storage*4U+c];
      if(std::to_integer<unsigned>(mip.rgba8[p*4U+3U])>128U)
        fail("Frontend sky texture alpha exceeds the qualified blend domain");
    }
    RenderSceneTextureV1 t;t.id=static_cast<std::uint32_t>(scene.textures.size());
    t.color_space=RenderSceneTextureColorSpaceV1::linear;t.mips.push_back(std::move(mip));
    texture_ids.emplace(index,t.id);scene.textures.push_back(std::move(t));texture_bytes+=texels*4U;
    return static_cast<std::uint32_t>(scene.textures.size()-1U);
  };
  for(std::uint32_t shell=0U;shell<4U;++shell) {
    const auto sh=word(source,0x20U+shell*4U);
    const auto count=word(source,sh),flags=word(source,std::uint64_t{sh}+4U);
    if(!count || count>4096U || flags!=(shell==0U?1U:0U))
      fail("Frontend sky shell flags or cluster count differ from its source owner");
    slice(source,std::uint64_t{sh}+16U,std::uint64_t{count}*32U);
    if(scene.meshes.size()>=limits.max_meshes || scene.instances.size()>=limits.max_instances)
      fail("Frontend sky shells exceed neutral instance limits");
    RenderSceneMeshV1 mesh;mesh.id=shell;
    std::map<std::uint8_t,std::uint32_t> materials;
    const auto material=[&](const std::uint8_t index) -> std::uint32_t {
      if(const auto f=materials.find(index);f!=materials.end())return f->second;
      if(scene.materials.size()>=limits.max_materials)
        fail("Frontend sky materials exceed neutral limits");
      RenderSceneMaterialV1 m;m.id=static_cast<std::uint32_t>(scene.materials.size());
      m.color_math=RenderSceneColorMathV1::encoded_integer;
      m.blend_mode=RenderSceneBlendModeV1::source_over;
      m.interpolation=RenderSceneInterpolationV1::affine;
      m.depth_test=RenderSceneDepthTestV1::always;m.depth_write=shell==0U;
      m.blend_denominator=128U;
      if(shell!=0U) {
        m.base_color_texture_id=texture(index);m.texture_modulation_denominator=128U;
        m.address_u=m.address_v=RenderSceneAddressModeV1::clamp_to_edge;
      }
      else if(index!=255U)fail("Frontend gradient shell has a texture binding");
      materials.emplace(index,m.id);scene.materials.push_back(m);return m.id;
    };
    for(std::uint32_t cluster=0U;cluster<count;++cluster) {
      const auto d=std::uint64_t{sh}+16U+std::uint64_t{cluster}*32U;
      const auto payload=word(source,d+16U);
      const auto nv=half(source,d+20U),nt=half(source,d+22U);
      const auto positions=half(source,d+24U),attributes=half(source,d+26U);
      const auto triangles=half(source,d+28U),bytes=half(source,d+30U);
      if(!nv || nv>256U || !nt || result.source_vertices>limits.max_vertices ||
         nv>limits.max_vertices-result.source_vertices ||
         result.source_triangles>limits.max_triangle_indices/3U ||
         nt>limits.max_triangle_indices/3U-result.source_triangles)
        fail("Frontend sky cluster geometry exceeds its domain or limits");
      const auto data=slice(source,payload,bytes);
      slice(data,positions,std::uint64_t{nv}*8U);
      slice(data,attributes,std::uint64_t{nv}*4U);
      slice(data,triangles,std::uint64_t{nt}*4U);
      const auto first_vertex=static_cast<std::uint32_t>(mesh.vertices.size());
      for(std::uint32_t v=0U;v<nv;++v) {
        const auto p=std::uint64_t{positions}+v*8U,a=std::uint64_t{attributes}+v*4U;
        RenderSceneVertexV1 vertex;
        vertex.x=static_cast<float>(std::bit_cast<std::int16_t>(half(data,p)))/1024.0F;
        vertex.y=static_cast<float>(std::bit_cast<std::int16_t>(half(data,p+2U)))/1024.0F;
        vertex.z=static_cast<float>(std::bit_cast<std::int16_t>(half(data,p+4U)))/1024.0F;
        if(shell==0U)vertex.rgba8=word(data,a);
        else {
          // 22d5d0 PEXTLH zero-extends before VITOF12; the ST pair is unsigned.
          vertex.u=static_cast<float>(half(data,a))/4096.0F;
          vertex.v=static_cast<float>(half(data,a+2U))/4096.0F;
          const auto alpha=std::to_integer<std::uint32_t>(data[static_cast<std::size_t>(p)+6U]);
          vertex.rgba8=UINT32_C(0x00808080)|(alpha<<24U);
        }
        if((vertex.rgba8>>24U)>128U)fail("Frontend sky vertex alpha exceeds the qualified domain");
        mesh.vertices.push_back(vertex);
      }
      for(std::uint32_t t=0U;t<nt;++t) {
        const auto face=slice(data,std::uint64_t{triangles}+t*4U,4U);
        const auto id=material(std::to_integer<std::uint8_t>(face[3]));
        if(mesh.draw_ranges.empty() || mesh.draw_ranges.back().material_id!=id) {
          if(draw_ranges>=limits.max_draw_ranges)fail("Frontend sky draw ranges exceed neutral limits");
          mesh.draw_ranges.push_back({id,mesh.triangle_indices.size(),0U});++draw_ranges;
        }
        for(std::size_t k=0U;k<3U;++k) {
          const auto local=std::to_integer<std::uint32_t>(face[k]);
          if(local>=nv)fail("Frontend sky face references a missing cluster vertex");
          mesh.triangle_indices.push_back(first_vertex+local);
        }
        mesh.draw_ranges.back().index_count+=3U;
      }
      result.source_vertices+=nv;result.source_triangles+=nt;++result.cluster_count;
    }
    scene.meshes.push_back(std::move(mesh));
    RenderSceneInstanceV1 instance;instance.id=shell;instance.mesh_id=shell;
    instance.camera_relative=true;instance.project_to_far_plane=true;
    scene.instances.push_back(instance);++result.shell_count;
  }
  scene=canonicalize_render_scene_v1(std::move(scene),limits);
  return result;
}
} // namespace openrc
