#include "openrc/rac_instance_lighting.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void put(std::vector<std::byte>& b,std::size_t at,std::uint32_t value) {
  for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<std::byte>((value>>(8U*i))&255U);
}
// Authored diagnostic program, not copied game microcode: four independent
// entries copy the ambient inputs to the consumer's output registers. This
// tests byte packing, both alternating register groups, and source bounds.
openrc::RacInstanceLightingSourceV1 source() {
  std::vector<std::byte> program(176U*8U);
  for(unsigned entry=0;entry<4;++entry)
    for(unsigned i=0;i<44;++i) {
      auto upper=0x2ffU;
      if(i<4U) {
        const bool shrub=entry<2U,second=(entry&1U)!=0U;
        const auto input=shrub?31U:(second?13U:9U)+i;
        const auto output=(shrub?(second?13U:9U):(second?5U:1U))+i;
        upper=(15U<<21U)|(input<<11U)|(output<<6U)|0x1bU; // MULw VFout,VFin,VF0w
      }
      if(i==42U)upper|=1U<<30U;
      put(program,(entry*44U+i)*8U,0x8000033cU);
      put(program,(entry*44U+i)*8U+4U,upper);
    }
  openrc::ElfDvpOverlay overlay;
  overlay.size=static_cast<std::uint32_t>(program.size());
  constexpr std::array<std::uint16_t,4> entries{0,44,88,132};
  openrc::RacInstanceLightingSourceV1 result;
  result.palette_program=openrc::decode_dvp_vu_program_v1(program,std::span(&overlay,1),entries,
      {program.size(),1,program.size(),4,8,256});
  result.length_weights[0]=0x3f800000U;
  return result;
}
template<class F> void rejects(F call) {
  try {call();}catch(const openrc::RacInstanceLightingError&){return;}
  throw std::runtime_error("Malformed lighting source was accepted");
}
}
int main()try {
  const auto s=source();
  std::vector<std::byte> tie(64U+64U*8U),shrub(64U+24U*8U);
  put(tie,12,64);put(shrub,44,64);
  openrc::RacGameplayTieInstanceV1 t;
  openrc::RacGameplayShrubInstanceV1 h;
  for(unsigned i=0;i<4;++i)t.matrix_bits[i*5U]=h.matrix_bits[i*5U]=0x3f800000U;
  for(unsigned i=0;i<64;++i) {
    const auto color=static_cast<std::uint16_t>((i*1031U)^(i&1U?0x8000U:0U));
    t.raw_words[20U+i/2U]|=static_cast<std::uint32_t>(color)<<((i&1U)*16U);
  }
  const auto tp=openrc::compile_rac_tie_initial_palette_v1(s,tie,t);
  check(tp.rgba.size()==64 && tp.executed_instruction_pairs==704,"TIE palette groups differ");
  for(unsigned i=0;i<64;++i) {
    const auto color=static_cast<std::uint16_t>((i*1031U)^(i&1U?0x8000U:0U));
    check(tp.rgba[i]==std::array<std::uint8_t,4>{static_cast<std::uint8_t>((color&31U)*8U),
      static_cast<std::uint8_t>(((color>>5U)&31U)*8U),static_cast<std::uint8_t>(((color>>10U)&31U)*8U),
      static_cast<std::uint8_t>((color>>15U)*128U)},"PEXT5 or alternating TIE palette indices differ");
  }
  h.raw_words[20]=0x100U;h.raw_words[21]=2U;h.raw_words[22]=0x403U;
  const auto hp=openrc::compile_rac_shrub_initial_palette_v1(s,shrub,h);
  check(hp.rgba.size()==24 && hp.executed_instruction_pairs==264,"Shrub palette groups differ");
  for(const auto c:hp.rgba)check(c==std::array<std::uint8_t,4>{0,3,3,132},"Shrub source full-word RGB packing changed");
  rejects([&]{auto short_source=tie;short_source.pop_back();static_cast<void>(openrc::compile_rac_tie_initial_palette_v1(s,short_source,t));});
  rejects([&]{auto bad=shrub;put(bad,44,0xffffffffU);static_cast<void>(openrc::compile_rac_shrub_initial_palette_v1(s,bad,h));});
  rejects([&]{auto short_program=s;short_program.palette_program.instructions.front().upper.end=true;
    static_cast<void>(openrc::compile_rac_shrub_initial_palette_v1(short_program,shrub,h));});
  std::cout<<"Instance lighting source packing, alternating palette groups and bounded failures passed\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
