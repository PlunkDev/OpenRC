#include "openrc/rac_frontend_geometry.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char *message) {if(!condition)throw std::runtime_error(message);}
std::uint32_t word(std::istream &in) {
  std::uint32_t result=0;
  for(unsigned i=0;i<4;++i) {auto v=in.get();check(v!=std::char_traits<char>::eof(),"Truncated source geometry fixture");result|=std::uint32_t(static_cast<unsigned char>(v))<<(i*8);}
  return result;
}
void source(const char *path) {
  std::ifstream in(path,std::ios::binary);check(bool(in),"Cannot open source geometry fixture");
  std::array<char,8> magic{};in.read(magic.data(),magic.size());
  check(magic==std::array<char,8>{'F','R','G','E','O','M','0','1'},"Wrong source geometry fixture");
  const auto count=word(in);check(count>0&&count<10000,"Invalid geometry fixture count");
  for(std::uint32_t i=0;i<count;++i) {
    std::array<openrc::RacMobyPostVectorV1,4> corners{};
    for(auto &corner:corners)for(auto &lane:corner)lane=word(in);
    const auto actual=openrc::complete_rac_frontend_geometry_v1(corners);
    check(actual.corners==corners,"Source corner values changed in length tail");
    for(const auto value:actual.edge_length_bits)check(value==word(in),"Original edge-length source mismatch");
  }
  check(in.get()==std::char_traits<char>::eof(),"Trailing source geometry data");
  std::cout<<"Original geometry tail source/reference cases: "<<count<<'\n';
}
}
int main(int argc,char **argv) {
  try {
    std::array<openrc::RacMobyPostVectorV1,4> corners{};
    corners[1]={0x40400000U,0x40800000U,0x41400000U,0x7fffffffU}; //3,4,12 ->13
    corners[2]={0xc0400000U,0xc0800000U,0U,0xffffffffU}; //-3,-4,0 ->5
    const auto result=openrc::complete_rac_frontend_geometry_v1(corners);
    check(result.edge_length_bits==std::array<std::uint32_t,2>{0x41500000U,0x40a00000U},
          "Exact Pythagorean edges or ignored W lane changed");
    if(argc==2)source(argv[1]);else check(argc==1,"Expected optional source geometry fixture");
    std::cout<<"Frontend complete geometry contracts passed\n";return 0;
  } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
