#include "openrc/rac_frontend_environment_compile.hpp"
#include "openrc/render_scene_io.hpp"
#include "openrc/dvp_vu_numeric.hpp"

#include <iostream>
#include <string>

namespace {
void expect(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
void put(std::vector<std::byte> &b,std::size_t at,std::uint32_t v,std::size_t n=4U) {
  for(std::size_t i=0U;i<n;++i)b.at(at+i)=static_cast<std::byte>((v>>(8U*i))&255U);
}
openrc::RenderSceneLimitsV1 limits() {
  return {8U,1U,8U,16U,4U,32U,4U,512U,512U,262144U,1024U,4096U,1048576U};
}
std::vector<std::byte> source() {
  std::vector<std::byte>b(0x3400U);
  put(b,6U,4U,2U);put(b,10U,256U,2U);put(b,12U,8U,2U);put(b,14U,4U,2U);
  put(b,0x10U,0x50U);put(b,0x14U,0x100U);put(b,0x18U,0x40U);put(b,0x1cU,0x1000U);
  // Texture 4: 2x2 linear indices. Index 8 maps to CLUT storage slot16.
  put(b,0x90U,0U);put(b,0x94U,1024U);put(b,0x98U,2U);put(b,0x9cU,2U);
  put(b,0x100U+16U*4U,0x40203040U);put(b,0x500U,0x08080808U);
  for(std::uint32_t shell=0U;shell<4U;++shell) {
    const auto sh=0x3000U+shell*0x100U,p=sh+0x40U,d=sh+16U;
    put(b,0x20U+shell*4U,sh);put(b,sh,1U);put(b,sh+4U,shell==0U?1U:0U);
    put(b,d+16U,p);put(b,d+20U,3U,2U);put(b,d+22U,1U,2U);
    put(b,d+24U,0U,2U);put(b,d+26U,24U,2U);put(b,d+28U,36U,2U);put(b,d+30U,40U,2U);
    for(std::uint32_t v=0U;v<3U;++v) {
      put(b,p+v*8U,v==0U?0xfc00U:1024U,2U);put(b,p+v*8U+2U,v*1024U,2U);
      put(b,p+v*8U+4U,2048U,2U);put(b,p+v*8U+6U,128U,2U);
      put(b,p+24U+v*4U,shell==0U?0x80332211U:0x10000800U);
    }
    put(b,p+36U,(shell==0U?0xff000000U:0x04000000U)|0x00020100U);
  }
  return b;
}
void sky() {
  const auto b=source();const auto r=openrc::compile_rac_frontend_sky_shells_v1(b,limits());
  expect(r.shell_count==4U && r.cluster_count==4U && r.source_vertices==12U && r.source_triangles==4U,"Sky lost source geometry");
  const auto &s=r.render_scene;
  expect(s.textures.size()==1U && s.materials.size()==4U,"Sky binding deduplication or draw order changed");
  expect(s.meshes[0].vertices[0].x==-1.0F && s.meshes[0].vertices[2].y==2.0F && s.meshes[0].vertices[0].rgba8==0x80332211U,"Sky changed authored gradient or source XYZ units");
  expect(s.meshes[1].vertices[0].u==0.5F && s.meshes[1].vertices[0].v==1.0F && s.meshes[1].vertices[0].rgba8==0x80808080U,"Sky changed original ST/Q or modulation color");
  const auto &rgba=s.textures[0].mips[0].rgba8;
  expect(rgba[0]==std::byte{0x40U} && rgba[1]==std::byte{0x30U} && rgba[2]==std::byte{0x20U} && rgba[3]==std::byte{0x40U},"Sky expanded raw alpha or changed CLUT addressing");
  for(std::size_t i=0U;i<4U;++i) {
    const auto &m=s.materials[i];const auto &instance=s.instances[i];
    expect(m.color_math==openrc::RenderSceneColorMathV1::encoded_integer && m.blend_mode==openrc::RenderSceneBlendModeV1::source_over && m.blend_denominator==128U && m.interpolation==openrc::RenderSceneInterpolationV1::affine && m.depth_test==openrc::RenderSceneDepthTestV1::always && m.depth_write==(i==0U),"Sky source material contract changed");
    expect(instance.mesh_id==i && instance.camera_relative && instance.project_to_far_plane,"Sky shell ordering or camera relation changed");
  }
  const openrc::RenderSceneIoLimitsV1 io{1048576U,limits()};
  expect(openrc::decode_render_scene_v1(openrc::encode_render_scene_v1(s,io),io)==s,"Sky material extension failed codec round trip");
  const auto reject=[&](std::vector<std::byte> bad,openrc::RenderSceneLimitsV1 lim) {
    bool failed=false;try{(void)openrc::compile_rac_frontend_sky_shells_v1(bad,lim);}
    catch(const std::exception &){failed=true;}expect(failed,"Malformed or excessive sky was accepted");
  };
  for(const auto &[at,value,n]:std::array<std::array<std::uint32_t,3U>,7U>{
      std::array{6U,3U,2U},{0x3010U+16U,0xfffffff0U,4U},{0x3004U,0U,4U},
      {0x3000U+0x40U+36U,0xff000300U,4U},{0x3100U+0x40U+36U,0x03020100U,4U},
      {0x100U+16U*4U,0x81203040U,4U},{0x98U,3U,4U}}) {
    auto bad=b;put(bad,at,value,n);reject(std::move(bad),limits());
  }
  auto small=limits();small.max_vertices=11U;reject(b,small);
  small=limits();small.max_total_rgba8_bytes=15U;reject(b,small);
  auto short_input=b;short_input.resize(0x3341U);reject(std::move(short_input),limits());
}
void fog() {
  std::vector<std::byte> source(0x40U);put(source,0U,0x10U);
  put(source,0x1cU,0x123456a1U);put(source,0x20U,0xffffffb2U);put(source,0x24U,0xc3U);
  put(source,0x28U,0U);put(source,0x2cU,0x44800000U);
  put(source,0x30U,0x437f0000U);put(source,0x34U,0x42fe0000U);
  auto f=openrc::compile_rac_frontend_fog_v1(source);
  expect(f.rgb8==0x00c3b2a1U,"Fog owner did not retain its LBU color semantics");
  expect(f.world_gradient_bits==0xc3000000U && f.world_offset_bits==0x437f0000U &&
      f.projection_scale_bits==0xc0800000U && f.projection_offset_bits==0x437f0000U &&
      f.depth_to_w_bits==0xbe000000U,"Fog coefficient operation order or distance units changed");
  const auto sample=[&](std::uint32_t depth){return openrc::evaluate_rac_frontend_terrain_fog_v1(
      f,openrc::dvp_vu_mul_bits_v1(depth,f.depth_to_w_bits).bits);};
  expect(sample(0U)==255U && sample(0x44000000U)==191U && sample(0x44800000U)==127U &&
      sample(0x45000000U)==127U && sample(0xc4000000U)==255U,
      "Fog factor clamp, near/far endpoint, or XYZF2 extraction changed");
  // Nonzero near distance catches a missing intercept or world-unit conversion.
  put(source,0x28U,0x44800000U);put(source,0x2cU,0x45000000U);
  put(source,0x30U,0x43600000U);put(source,0x34U,0x42c00000U);
  f=openrc::compile_rac_frontend_fog_v1(source);
  expect(f.world_offset_bits==0x43b00000U && f.projection_offset_bits==0x43b00000U &&
      sample(0x44800000U)==224U && sample(0x44c00000U)==160U && sample(0x45000000U)==96U,
      "Fog nonzero near-distance intercept changed");
  expect(openrc::evaluate_rac_frontend_terrain_fog_v1(f,0xc33f4000U)==160U,
      "Fog factor must truncate the fractional source factor");
  for(const auto &[at,bits]:std::array<std::array<std::uint32_t,2U>,4U>{
      std::array{0x2cU,0x44800000U},{0x28U,0xbf800000U},
      {0x30U,0x7f800000U},{0x34U,0x43800000U}}) {
    auto bad=source;put(bad,at,bits);bool failed=false;
    try{(void)openrc::compile_rac_frontend_fog_v1(bad);}catch(const std::exception&){failed=true;}
    expect(failed,"Invalid fog domain was accepted");
  }
}
}
int main()try {sky();fog();std::cout<<"Frontend environment sky/fog tests passed\n";return 0;}
catch(const std::exception &e){std::cerr<<"Frontend environment tests failed: "<<e.what()<<'\n';return 1;}
