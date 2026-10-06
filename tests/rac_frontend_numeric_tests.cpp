#include "openrc/rac_frontend_numeric.hpp"
#include "openrc/ee_cop1_numeric.hpp"
#include "openrc/dvp_vu_numeric.hpp"

#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool result,const char *message) { if(!result) throw std::runtime_error(message); }
std::uint32_t word(std::istream &in) {
  std::uint32_t out=0;
  for(unsigned i=0;i<4;++i) {
    const auto byte=in.get();check(byte!=std::char_traits<char>::eof(),"Truncated camera source fixture");
    out|=std::uint32_t(static_cast<unsigned char>(byte))<<(i*8);
  }
  return out;
}
void compound_arithmetic() {
  using namespace openrc;
  // Exact power-of-two products and aligned sums give independent algebraic
  // expectations over signs, both destinations and true accumulator overflow.
  for(std::uint32_t value=1;value<1024;++value) {
    const auto raw=ee_cop1_cvt_s_w_bits_v1(value).bits;
    const auto expected=ee_cop1_cvt_s_w_bits_v1(value*2U+1U).bits;
    const auto result=ee_cop1_madd_bits_v1({0x3f800000U,false},raw,0x40000000U);
    check(result.result.bits==expected&&!result.result.overflow&&!result.product.underflow,
          "Exact integer MADD algebra changed");
    check(dvp_vu_madd_bits_v1({0x3f800000U,false},raw,0x40000000U).result.bits==expected,
          "EE/VU ordered compound value diverged");
  }
  const auto positive_overflow=ee_cop1_madd_bits_v1({0xffffffffU,true},0x7fffffffU,0x40000000U);
  check(positive_overflow.result.bits==0x7fffffffU&&positive_overflow.result.overflow,
        "Product overflow did not take precedence over ACC overflow");
  check(ee_cop1_madd_bits_v1({0xffffffffU,true},0x3f800000U,0x3f800000U).result.bits==0xffffffffU,
        "ACC overflow latch was lost by a finite product");
  const auto underflow=dvp_vu_madd_bits_v1({0x3f800000U,false},0x80800000U,0x00800000U);
  check(underflow.result.bits==0x3f800000U&&!underflow.result.underflow&&underflow.product.underflow&&
        (underflow.sticky_events&7U)==7U,"Intermediate product Z/S/U disappeared from sticky events");
  const auto subtract=dvp_vu_madd_bits_v1({0x40000000U,false},0xbf800000U,0x3f800000U,true);
  check(subtract.result.bits==0x40400000U&&(subtract.sticky_events&2U),
        "MSUB changed the original intermediate product sign event");
}
void camera_contract() {
  using namespace openrc;
  const auto camera=execute_rac_frontend_camera_v1({0U,0U,0U},{0U,0U,0U});
  check(camera.render_width==512&&camera.render_height==448&&
        camera.display_width==512&&camera.display_height==512,"Source viewport/display geometry changed");
  check(std::bit_cast<std::uint32_t>(camera.camera.tangent_half_horizontal)==0x3f2147afU&&
        std::bit_cast<std::uint32_t>(camera.camera.tangent_half_vertical)==0x3ef3daf9U,
        "Effective source projection scale was replaced by nominal parameter");
  check(camera.camera.near_plane==0.03125F&&camera.camera.far_plane==728.0F,
        "Original 1024-unit projection conversion changed");
  bool rejected=false;
  try { (void)execute_rac_frontend_camera_v1({0U,0U,0U},{0x7f800000U,0U,0U}); }
  catch(const RacMobyPostError &) { rejected=true; }
  check(rejected,"Camera accepted an Euler input beyond the source domain");
}
void source_fixture(const char *path) {
  std::ifstream in(path,std::ios::binary);check(bool(in),"Cannot open camera source fixture");
  std::array<char,8> magic{};in.read(magic.data(),magic.size());
  check(magic==std::array<char,8>{'F','R','C','A','M','R','0','1'},"Wrong camera fixture magic");
  const auto count=word(in);check(count>0&&count<10000,"Invalid camera fixture count");
  for(std::uint32_t i=0;i<count;++i) {
    std::array<std::uint32_t,3> rotation{};for(auto &v:rotation)v=word(in);
    const auto actual=openrc::execute_rac_frontend_camera_v1({0,0,0},rotation);
    for(const auto &column:actual.rotation_rows) for(const auto v:column)
      if(v!=word(in))throw std::runtime_error("Original camera rotation mismatch case "+std::to_string(i));
    for(const auto &column:actual.view_columns) for(const auto v:column)
      if(v!=word(in))throw std::runtime_error("Original camera view mismatch case "+std::to_string(i));
  }
  check(in.get()==std::char_traits<char>::eof(),"Trailing camera fixture bytes");
  std::cout<<"Original camera source/reference cases: "<<count<<'\n';
}
void projection_fixture(const char *path) {
  std::ifstream in(path,std::ios::binary);check(bool(in),"Cannot open projector source fixture");
  std::array<char,8> magic{};in.read(magic.data(),magic.size());
  check(magic==std::array<char,8>{'F','R','P','R','O','J','0','1'},"Wrong projector fixture magic");
  const auto count=word(in);check(count>0&&count<10000,"Invalid projector fixture count");
  for(std::uint32_t i=0;i<count;++i) {
    openrc::RacMobyPostVectorV1 first{},second{};
    for(auto &v:first)v=word(in);for(auto &v:second)v=word(in);
    openrc::RacFrontendProjectionStateV1 state;
    for(auto &v:state.camera_position_bits)v=word(in);
    for(auto &column:state.projection_view_columns) for(auto &v:column)v=word(in);
    for(auto &v:state.screen_scale_bits)v=word(in);
    for(auto &v:state.origin_words)v=word(in);
    const auto actual=openrc::execute_rac_frontend_project_bounds_v1(first,second,state);
    for(const auto v:actual.width_height_x_y)
      if(v!=word(in))throw std::runtime_error("Original projector mismatch case "+std::to_string(i));
  }
  check(in.get()==std::char_traits<char>::eof(),"Trailing projector fixture bytes");
  std::cout<<"Original projector source/reference cases: "<<count<<'\n';
}
void menu_projection_fixture(const char *path) {
  std::ifstream in(path,std::ios::binary);check(bool(in),"Cannot open menu projector source fixture");
  std::array<char,8> magic{};in.read(magic.data(),magic.size());
  check(magic==std::array<char,8>{'F','R','M','P','R','J','0','1'},"Wrong menu projector fixture magic");
  const auto actual=openrc::execute_rac_frontend_menu_projection_v1();
  for(const auto v:actual.camera_position_bits)check(v==word(in),"Menu source camera mismatch");
  for(const auto &column:actual.projection_view_columns) for(const auto v:column)
    check(v==word(in),"Menu source projection/view matrix mismatch");
  for(const auto v:actual.screen_scale_bits)check(v==word(in),"Menu source screen scale mismatch");
  for(const auto v:actual.origin_words)check(v==word(in),"Menu source origin mismatch");
  check(in.get()==std::char_traits<char>::eof(),"Trailing menu projector fixture bytes");
  std::cout<<"Original menu projection owner source/reference passed\n";
}
}
int main(int argc,char **argv) {
  try { compound_arithmetic();camera_contract();if(argc>=2)source_fixture(argv[1]);
    if(argc>=3)projection_fixture(argv[2]);
    if(argc==4)menu_projection_fixture(argv[3]);else check(argc<=3,"Expected up to three source fixture paths");
    std::cout<<"Frontend numeric contracts passed\n";return 0;
  } catch(const std::exception &error) { std::cerr<<error.what()<<'\n';return 1; }
}
