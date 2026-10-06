#include "openrc/rac_shrub_class.hpp"

#include <bit>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool b,const char* what) {if(!b)throw std::runtime_error(what);}
void put(std::vector<std::byte>& b,std::size_t p,std::uint64_t value,unsigned n=4) {
  for(unsigned i=0;i<n;++i) b.at(p+i)=static_cast<std::byte>((value>>(i*8))&255);
}
std::vector<std::byte> source() {
  std::vector<std::byte> b(484);
  put(b,0x20,std::bit_cast<std::uint32_t>(0.125F));put(b,0x28,1,2);put(b,0x2c,80);
  put(b,0x40,272);put(b,0x44,212);
  put(b,272,0x01000404);put(b,276,0x05000000);put(b,280,0x6c068000);
  const auto h=284U;put(b,h,1);put(b,h+4,1);put(b,h+8,6);put(b,h+12,6);
  put(b,h+16,(UINT64_C(3)<<60)|(UINT64_C(3)<<47)|(UINT64_C(1)<<46)|3,8);
  put(b,h+24,0x412);put(b,h+28,5);
  const auto m=h+32;put(b,m+8,0x14);put(b,m+12,0);put(b,m+24,8,8);
  put(b,m+40,0x34,8);put(b,m+48,2,8);put(b,m+56,6,8);
  put(b,380,0x6d068006);
  for(unsigned i=0;i<6;++i) {
    const unsigned actual=i<3?i:2;
    put(b,384+i*8,actual*1024,2);put(b,384+i*8+6,6+actual*3,2);
  }
  put(b,432,0x6d06800c);
  for(unsigned i=0;i<6;++i) put(b,436+i*8+4,4096,2);
  return b;
}
template<class F> void reject(F change) {
  auto b=source();change(b);
  try {static_cast<void>(openrc::parse_rac_shrub_class_v1(b));}
  catch(const openrc::RacShrubClassError&) {return;}
  throw std::runtime_error("Malformed shrub data was accepted");
}
}
int main() try {
  auto b=source();const auto result=openrc::parse_rac_shrub_class_v1(b);
  check(result.packet_count==1&&result.vertices.size()==6&&result.duplicate_padding_writes==3,
      "Source/padding counts differ");
  check(result.primitives.size()==1&&result.primitives[0].triangles.size()==1&&
        result.primitives[0].triangles[0]==std::array<std::uint32_t,3>{0,1,2},"Triangle-list assembly differs");
  check(result.materials.size()==1&&result.materials[0].local_texture_index==2,"Source texture slot differs");
  auto strip=b;
  put(strip,300,(UINT64_C(3)<<60)|(UINT64_C(4)<<47)|(UINT64_C(1)<<46)|4,8);
  for(unsigned i=3;i<6;++i) {put(strip,384+i*8,3072,2);put(strip,384+i*8+6,15,2);}
  const auto strip_result=openrc::parse_rac_shrub_class_v1(strip);
  check(strip_result.primitives[0].triangles==std::vector<std::array<std::uint32_t,3>>{{0,1,2},{2,1,3}},
      "Triangle strip winding or duplicate padding differs");
  reject([](auto& v){v.pop_back();});
  reject([](auto& v){put(v,0x40,0xffffffff);});
  reject([](auto& v){put(v,284+8,200);});
  reject([](auto& v){put(v,436+6,24,2);});
  reject([](auto& v){put(v,384+3*8,1234,2);});
  reject([](auto& v){put(v,284+32+48,16);});
  reject([](auto& v){put(v,384+6,1,2);});
  auto limited=openrc::RacShrubClassLimitsV1{};limited.max_vertices=5;
  try {static_cast<void>(openrc::parse_rac_shrub_class_v1(b,limited));throw std::logic_error("Vertex limit ignored");}
  catch(const openrc::RacShrubClassError&) {}
  std::cout<<"Shrub class packet assembly and malformed bounds passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
