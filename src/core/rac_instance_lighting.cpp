#include "openrc/rac_instance_lighting.hpp"

#include "openrc/dvp_vu_execute.hpp"
#include "openrc/dvp_vu_numeric.hpp"
#include "openrc/ee_cop1_numeric.hpp"
#include "openrc/elf.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <string>

namespace openrc {
namespace {
using Vec = std::array<std::uint32_t,4>;
using Acc = DvpVuAccumulatorLaneV1;
constexpr std::uint32_t one = 0x3f800000U;

[[noreturn]] void fail(const char* message) {
  throw RacInstanceLightingError(message);
}
void range(std::span<const std::byte> bytes,std::uint64_t at,std::uint64_t size) {
  if(at>bytes.size() || size>bytes.size()-at) fail("RAC instance lighting source range is truncated");
}
std::uint32_t word(std::span<const std::byte> bytes,std::uint64_t at) {
  range(bytes,at,4);
  std::uint32_t out=0;
  for(unsigned i=0;i<4;++i)out|=std::to_integer<std::uint32_t>(bytes[at+i])<<(8U*i);
  return out;
}
std::uint16_t half(std::span<const std::byte> bytes,std::uint64_t at) {
  range(bytes,at,2);
  return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[at])|
      (std::to_integer<unsigned>(bytes[at+1])<<8U));
}
std::uint64_t elf_offset(const ElfReport& report,std::uint32_t address,std::uint32_t size) {
  for(const auto& section:report.section_headers) {
    if(section.type==8U || address<section.virtual_address)continue;
    const auto relative=static_cast<std::uint64_t>(address)-section.virtual_address;
    if(relative<=section.size && size<=section.size-relative)return section.file_offset+relative;
  }
  fail("RAC instance lighting resident source address is unavailable");
}
std::uint32_t add(std::uint32_t a,std::uint32_t b) {return dvp_vu_add_bits_v1(a,b).bits;}
std::uint32_t sub(std::uint32_t a,std::uint32_t b) {return dvp_vu_sub_bits_v1(a,b).bits;}
std::uint32_t mul(std::uint32_t a,std::uint32_t b) {return dvp_vu_mul_bits_v1(a,b).bits;}
Acc mula(std::uint32_t a,std::uint32_t b) {
  const auto r=dvp_vu_mul_bits_v1(a,b);return {r.bits,r.overflow};
}
Acc madda(Acc a,std::uint32_t b,std::uint32_t c) {
  const auto r=dvp_vu_madd_bits_v1(a,b,c).result;return {r.bits,r.overflow};
}
std::uint32_t length_squared(const Vec& a,std::uint32_t weight) {
  const auto x=mul(a[0],a[0]),y=mul(a[1],a[1]),z=mul(a[2],a[2]);
  const auto xy=dvp_vu_add_bits_v1(x,y);
  return dvp_vu_madd_bits_v1({xy.bits,xy.overflow},weight,z).result.bits;
}
void normalize(Vec& a,std::uint32_t weight) {
  const auto q=dvp_vu_rsqrt_bits_v1(one,length_squared(a,weight));
  for(unsigned i=0;i<3;++i)a[i]=mul(a[i],q);
}
void put(DvpVuExecutionStateV1& state,unsigned reg,const Vec& value) {
  for(unsigned i=0;i<4;++i)state.vf[reg].lanes[i]={value[i],0xffffffffU};
}

DvpVuExecutionStateV1 prepare(const RacInstanceLightingSourceV1& source,
    const std::array<std::uint32_t,16>& matrix,std::uint16_t selector,bool shrub) {
  // 22b980..22ba58 / 238724..238814. The blend is 8-bit fixed point;
  // source shrub MUL masks are XYZ whereas TIE MUL masks include W.
  auto lights=source.directional[selector&15U];
  if((selector&0xff00U)!=0U) {
    const auto other=source.directional[(selector>>4U)&15U];
    const auto integer=(selector>>4U)&0xff0U;
    auto blend=ee_cop1_cvt_s_w_bits_v1(integer).bits;
    if(blend!=0U)blend-=12U<<23U;
    const auto inverse=sub(one,blend);
    for(unsigned row=0;row<4;++row)
      for(unsigned lane=0;lane<4;++lane) {
        const auto left=shrub&&lane==3U?lights[row][lane]:mul(lights[row][lane],inverse);
        const auto right=shrub&&lane==3U?other[row][lane]:mul(other[row][lane],blend);
        lights[row][lane]=add(left,right);
      }
    normalize(lights[1],source.length_weights[0]);
    normalize(lights[3],source.length_weights[0]);
  }
  Vec intensity{add(0,lights[0][3]),add(0,lights[2][3]),0,0};
  lights[0][3]=lights[2][3]=0;
  // The loader writes 0xffff to live instance +1e. The original nibble loop
  // therefore exits at its first branch. No point-light vector is invented.
  std::array<Vec,3> direction{lights[1],lights[3],Vec{}};
  for(auto& a:direction)a[3]=0;

  std::array<Vec,3> basis{};
  for(unsigned row=0;row<3;++row) {
    std::copy_n(matrix.begin()+row*4U,4,basis[row].begin());
    if(shrub) {
      // 22bb78..22bbbc uses VRSQRT directly on each squared column.
      normalize(basis[row],source.length_weights[0]);
    } else {
      // 1ea498..1ea504 first calls 1f9cb8 (VSQRT, VADDQ), then
      // COP1 DIV.S 1/length; 238944..23894c multiplies by stored W.
      auto length=dvp_vu_sqrt_bits_v1(length_squared(basis[row],one));
      length=add(0,length);
      const auto inverse=ee_cop1_div_bits_v1(one,length).bits;
      for(unsigned lane=0;lane<3;++lane)basis[row][lane]=mul(basis[row][lane],inverse);
    }
  }
  // Negative transposed basis, then ordered MULAx/MADDAy/MADDz for each
  // world light. Final transposition gives VF24/25/26 (normal components).
  std::array<Vec,3> transformed{};
  for(unsigned light=0;light<3;++light)
    for(unsigned axis=0;axis<3;++axis) {
      auto acc=mula(sub(0,basis[axis][0]),direction[light][0]);
      acc=madda(acc,sub(0,basis[axis][1]),direction[light][1]);
      transformed[axis][light]=dvp_vu_madd_bits_v1(acc,
          sub(0,basis[axis][2]),direction[light][2]).result.bits;
      transformed[axis][light]=add(0,transformed[axis][light]);
    }
  auto state=make_dvp_vu_execution_state_v1();
  for(unsigned axis=0;axis<3;++axis)put(state,24U+axis,transformed[axis]);
  put(state,27,lights[0]);put(state,28,lights[2]);put(state,29,Vec{});
  put(state,30,intensity);
  return state;
}

RacInstancePaletteV1 palette(const RacInstanceLightingSourceV1& source,
    std::span<const std::byte> bytes,std::uint64_t normal_at,
    DvpVuExecutionStateV1 state,std::span<const std::uint32_t> ambient,bool shrub) {
  const std::size_t count=shrub?24U:64U;
  range(bytes,normal_at,count*8U);
  if(ambient.size()!=count)fail("RAC instance lighting ambient palette has the wrong size");
  RacInstancePaletteV1 result;
  result.rgba.reserve(count);
  for(std::size_t first=0;first<count;first+=4U) {
    const bool second=(first&4U)!=0U;
    const unsigned normal_reg=second?5U:1U;
    const unsigned ambient_reg=second?13U:9U;
    const auto entry=static_cast<std::uint16_t>((shrub?0U:0x2c0U)+(second?0x160U:0U));
    for(unsigned n=0;n<4;++n) {
      Vec normals{},colors{};
      for(unsigned lane=0;lane<4;++lane) {
        const auto h=half(bytes,normal_at+(first+n)*8U+lane*2U);
        normals[lane]=static_cast<std::uint32_t>(static_cast<std::int32_t>(std::bit_cast<std::int16_t>(h)));
        // PEXTLB/PEXTLH/PADDUW: preserve full floating-point bit encodings.
        colors[lane]=0x47800000U+((ambient[first+n]>>(lane*8U))&255U);
      }
      put(state,normal_reg+n,normals);
      put(state,shrub?31U:ambient_reg+n,colors);
    }
    auto executed=execute_dvp_vu_program_v1(source.palette_program,std::move(state),
        {static_cast<std::uint16_t>(entry/8U),false},{44U,1U,1U,1U});
    if(executed.termination!=DvpVuTerminationV1::program_end || executed.executed_instruction_pairs!=44U)
      fail("RAC instance lighting resident palette did not complete its exact source entry");
    result.executed_instruction_pairs+=executed.executed_instruction_pairs;
    state=std::move(executed.final_state);
    const unsigned output_reg=shrub?ambient_reg:normal_reg;
    for(unsigned n=0;n<4;++n) {
      std::array<std::uint8_t,4> rgba{};
      for(unsigned lane=0;lane<4;++lane) {
        const auto value=state.vf[output_reg+n].lanes[lane];
        if(value.known_mask!=0xffffffffU)fail("RAC instance lighting output contains an unknown source lane");
        // PPACH followed by PPACB keeps the low byte of each raw word.
        rgba[lane]=static_cast<std::uint8_t>(value.bits&255U);
      }
      result.rgba.push_back(rgba);
    }
  }
  return result;
}
} // namespace

RacInstanceLightingSourceV1 make_rac_instance_lighting_source_v1(
    std::span<const std::byte> elf,std::span<const std::byte> gameplay) {
  if(elf.size()>64U*1024U*1024U || gameplay.size()>64U*1024U*1024U)
    fail("RAC instance lighting source exceeds its bounded envelope");
  const auto report=inspect_elf(elf);
  if(!report.dvp_overlay_table)fail("RAC instance lighting has no resident DVP overlay table");
  const ElfDvpOverlay* overlay=nullptr;
  for(const auto& candidate:report.dvp_overlay_table->overlays)
    if(candidate.load_memory_address==0x100af0U && candidate.virtual_memory_address==0U && candidate.size==0x580U) {
      if(overlay)fail("RAC instance lighting resident overlay is ambiguous");
      overlay=&candidate;
    }
  if(!overlay)fail("RAC instance lighting resident overlay 436083 is missing");
  RacInstanceLightingSourceV1 out;
  constexpr std::array<std::uint16_t,4> entries{0,0x160U/8U,0x2c0U/8U,0x420U/8U};
  out.palette_program=decode_dvp_vu_program_v1(elf,std::span(overlay,1),entries,
      {64U*1024U*1024U,1U,0x580U,4U,8U,256U});
  if(out.palette_program.instructions.size()!=176U ||
      out.palette_program.unknown_upper_count || out.palette_program.unknown_lower_count)
    fail("RAC instance lighting resident program is not fully decoded");
  const auto at=elf_offset(report,0x15fb80U,16U);
  for(unsigned i=0;i<4;++i)out.length_weights[i]=word(elf,at+i*4U);
  const auto lights=word(gameplay,4U);
  if(lights==0U || (lights&15U)!=0U)fail("RAC instance lighting authored light table is missing or misaligned");
  const auto count=std::min(word(gameplay,lights),12U);
  range(gameplay,static_cast<std::uint64_t>(lights)+16U,static_cast<std::uint64_t>(count)*64U);
  for(unsigned light=0;light<count;++light)
    for(unsigned row=0;row<4;++row)
      for(unsigned lane=0;lane<4;++lane)
        out.directional[light][row][lane]=word(gameplay,
            static_cast<std::uint64_t>(lights)+16U+light*64U+row*16U+lane*4U);
  return out;
}

RacInstancePaletteV1 compile_rac_tie_initial_palette_v1(
    const RacInstanceLightingSourceV1& source,std::span<const std::byte> bytes,
    const RacGameplayTieInstanceV1& instance) {
  static_assert(kRacGameplayTieRecordBytesV1>=0xd4U);
  const auto selector=static_cast<std::uint16_t>(instance.raw_words[0xd0U/4U]);
  auto state=prepare(source,instance.matrix_bits,selector,false);
  std::array<std::uint32_t,64> ambient{};
  for(unsigned i=0;i<64;++i) {
    const auto h=static_cast<std::uint16_t>(instance.raw_words[0x50U/4U+i/2U]>>((i&1U)*16U));
    // R5900 PEXT5 places RGB5 in the upper five bits of each byte; alpha
    // is the original bit15 expanded to bit31. No 5-to-8 bit replication.
    ambient[i]=((h&31U)<<3U)|(((h>>5U)&31U)<<11U)|
        (((h>>10U)&31U)<<19U)|((static_cast<std::uint32_t>(h)&0x8000U)<<16U);
  }
  return palette(source,bytes,word(bytes,0x0cU),std::move(state),ambient,false);
}

RacInstancePaletteV1 compile_rac_shrub_initial_palette_v1(
    const RacInstanceLightingSourceV1& source,std::span<const std::byte> bytes,
    const RacGameplayShrubInstanceV1& instance) {
  static_assert(kRacGameplayShrubRecordBytesV1>=0x64U);
  const auto selector=static_cast<std::uint16_t>(instance.raw_words[0x60U/4U]);
  auto state=prepare(source,instance.matrix_bits,selector,true);
  // 1ea854..1ea880 intentionally uses full source words before packing.
  const auto packed=instance.raw_words[0x50U/4U]|
      (instance.raw_words[0x54U/4U]<<8U)|(instance.raw_words[0x58U/4U]<<16U)|0x80000000U;
  std::array<std::uint32_t,24> ambient{};ambient.fill(packed);
  return palette(source,bytes,word(bytes,0x2cU),std::move(state),ambient,true);
}

} // namespace openrc
