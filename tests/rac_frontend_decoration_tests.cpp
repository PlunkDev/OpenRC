#include "openrc/rac_frontend_decoration.hpp"
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace openrc;
void check(bool result,const char *message) {if(!result)throw std::runtime_error(message);}
void contracts() {
  RacFrontendDecorationInputsV1 in;
  in.object_present=in.pvar_present=in.pal=true;in.rectangle_words={20,30,75,150};
  in.state.random_seed_word=1;
  const auto out=execute_rac_frontend_decoration_v1(in);
  check(out.draws.size()==3&&out.random_calls==1&&out.state.random_seed_word==0x41c67ea6U,
        "Source single shared-LCG step changed");
  check(out.draws[0].asset==RacFrontendDecorationAssetV1::sprite_bank_e99e_variant7&&out.draws[0].full_image_uv,
        "Source sprite-bank decoration was replaced by catalog bitmap");
  check(out.draws[1].uv_rectangle_words==std::array<std::uint32_t,4>{0,0,75,225}&&
        out.draws[2].rectangle_words==std::array<std::uint32_t,4>{21,31,73,149},
        "Source overlay UV or border threshold changed");
  in.state.flash_word=1;in.state.age_word=126;
  const auto flash=execute_rac_frontend_decoration_v1(in);
  check(flash.draws.size()==4&&flash.random_calls==2&&flash.state.age_word==128&&
        flash.draws[1].alpha==UINT64_C(0x8000000068),"Source flash peak/order changed");
  in.state.age_word=254;
  const auto ending=execute_rac_frontend_decoration_v1(in);
  check(ending.state.age_word==256&&!ending.state.flash_word&&ending.draws[1].alpha==0x68U,
        "Source flash final draw did not precede reset");
  in.rectangle_words[1]=448;in.state.flash_word=0;
  check(!execute_rac_frontend_decoration_v1(in).culled,"PAL source y448 prematurely culled");
  in.pal=false;check(execute_rac_frontend_decoration_v1(in).culled,"NTSC source height gate ignored");
  in.rectangle_words={512,0,1,1};
  const auto culled=execute_rac_frontend_decoration_v1(in);
  check(culled.culled&&!culled.random_calls&&culled.draws.empty(),"Culled source draw consumed RNG");
  const auto camera=execute_rac_frontend_menu_camera_v1();
  check(camera.position==std::array<float,3>{256,256,64}&&camera.right[1]==-1&&
        camera.up[2]==1&&camera.forward[0]==1&&
        std::bit_cast<std::uint32_t>(camera.tangent_half_horizontal)==0x3f2147afU&&
        std::bit_cast<std::uint32_t>(camera.tangent_half_vertical)==0x3ef3daf9U&&
        camera.near_plane==0.03125F&&camera.far_plane==728,
        "Original menu model camera diverged from projector units/basis");
}
std::uint32_t word(std::istream &in) {
  std::uint32_t value=0;
  for(unsigned i=0;i<4;++i) {auto b=in.get();check(b!=std::char_traits<char>::eof(),"Truncated decoration fixture");value|=std::uint32_t(static_cast<unsigned char>(b))<<(i*8);}
  return value;
}
std::uint64_t wide(std::istream &in) {const auto lo=word(in);return lo|(std::uint64_t(word(in))<<32);}
void source(const char *path) {
  std::ifstream in(path,std::ios::binary);check(bool(in),"Missing decoration fixture");
  std::array<char,8> magic{};in.read(magic.data(),magic.size());
  check(magic==std::array<char,8>{'F','R','D','E','C','O','0','1'},"Wrong decoration fixture");
  const auto count=word(in);check(count>0&&count<10000,"Invalid decoration fixture count");
  for(std::uint32_t i=0;i<count;++i) {
    RacFrontendDecorationInputsV1 input;
    input.object_present=word(in)!=0;input.pvar_present=word(in)!=0;input.pal=word(in)!=0;
    for(auto &v:input.rectangle_words)v=word(in);
    input.state={word(in),word(in),word(in)};
    const auto actual=execute_rac_frontend_decoration_v1(input);
    check(actual.state.flash_word==word(in)&&actual.state.age_word==word(in)&&actual.state.random_seed_word==word(in),"Source decoration state mismatch");
    check(actual.random_calls==word(in)&&actual.culled==(word(in)!=0)&&actual.draws.size()==word(in),"Source decoration control mismatch");
    for(const auto &draw:actual.draws) {
      check(std::uint32_t(draw.asset)==word(in)&&draw.source_call_pc==word(in),"Source decoration binding/order mismatch");
      for(const auto v:draw.rectangle_words)check((draw.full_image_uv?v<<4U:v)==word(in),"Source decoration rectangle mismatch");
      check(draw.full_image_uv==(word(in)!=0),"Source full-image UV mismatch");
      for(const auto v:draw.uv_rectangle_words)check(v==word(in),"Source decoration UV mismatch");
      check(draw.rgbaq==wide(in)&&draw.alpha==wide(in),"Source decoration color/blend mismatch");
    }
  }
  check(in.get()==std::char_traits<char>::eof(),"Trailing decoration source bytes");
  std::cout<<"Original decoration source/reference cases: "<<count<<'\n';
}
}
int main(int argc,char **argv) {
  try {contracts();if(argc==2)source(argv[1]);else check(argc==1,"Expected optional decoration fixture");
    std::cout<<"Frontend decoration contracts passed\n";return 0;
  } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
