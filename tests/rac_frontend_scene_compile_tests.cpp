#include "openrc/rac_frontend_scene_compile.hpp"
#include "openrc/actor_pose.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>

namespace {

void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void u16(std::vector<std::byte> &b, std::size_t p, std::uint16_t v) {
  b.at(p)=static_cast<std::byte>(v&255U);b.at(p+1U)=static_cast<std::byte>(v>>8U);
}
void u32(std::vector<std::byte> &b, std::size_t p, std::uint32_t v) {
  for (unsigned i=0U;i<4U;++i)b.at(p+i)=static_cast<std::byte>((v>>(8U*i))&255U);
}
void f32(std::vector<std::byte> &b, std::size_t p, float v) { u32(b,p,std::bit_cast<std::uint32_t>(v)); }

constexpr openrc::RacFrontendSceneCompileLimitsV1 limits{
    0x10000U,32U,32U,0x10000U,{0x10000U,false},
    {{0x10000U,8U,1.e-8},{{0x10000U,64U,64U,256U,256U,64U,1024U},16U,1024U,4096U},1024U},
    {0x10000U,0x10000U,0x10000U,16U,8U,8U,64U,1024U,4096U},
    {8U,8U,128U,2048U,8U,64U,16U,1U,16U,16U,8U,128U,1024U,4096U,8U,8U,64U,4096U},
    {{0x10000U,8U,64U,8U,0x1000U},{0x10000U,0x10000U,32U,8U},
     {8U,0x1000U,16U,16U,1.e-8},{16U,64U,256U,8U,2048U,128U,4096U,120U,1.e6F,1.e-8}}};

std::vector<std::byte> synthetic_class() {
  std::vector<std::byte> b(0x1e0U);
  u32(b,0U,0x50U);b[4U]=std::byte{1};b[7U]=std::byte{1};b[8U]=std::byte{1};
  u32(b,0x14U,0x180U);u32(b,0x18U,0x1c0U);u32(b,0x1cU,0x1d0U);
  f32(b,0x24U,1024.0F);f32(b,0x3cU,2.0F);
  u32(b,0x50U,0x60U);u16(b,0x54U,7U);u16(b,0x56U,4U);u32(b,0x58U,0xd0U);
  b[0x5cU]=std::byte{10};b[0x5dU]=std::byte{2};b[0x5eU]=std::byte{1};b[0x5fU]=std::byte{3};
  u32(b,0x60U,0x750380c2U);u16(b,0x68U,4096U);u16(b,0x6eU,4096U);
  u32(b,0x70U,0x6e03812dU);b[0x74U]=std::byte{0xff};b[0x75U]=std::byte{3};
  const std::array<std::uint8_t,8U> indices{0x81U,0x82U,3U,1U,1U,1U,0U,0U};
  for (std::size_t i=0U;i<indices.size();++i)b[0x78U+i]=static_cast<std::byte>(indices[i]);
  u32(b,0x80U,0x6c048130U);
  // One source pre-loop joint upload initializes VU0 slot zero. Each regular
  // vertex reads it and uploads the same joint into a distinct scratch slot.
  u32(b,0xd0U,1U);u32(b,0xdcU,3U);u32(b,0xe4U,3U);
  u32(b,0xe8U,0x30U);u32(b,0xecU,0xa0U);
  for (unsigned i=0U;i<3U;++i)b[0x103U+i*16U]=std::byte{4};
  u16(b,0x11aU,1U);u16(b,0x12cU,1U);
  u16(b,0x164U,5U);u16(b,0x166U,6U);u16(b,0x168U,7U);
  f32(b,0x180U,1.0F);f32(b,0x194U,1.0F);f32(b,0x1a8U,1.0F);
  return b;
}

std::vector<std::byte> synthetic_scene() {
  std::vector<std::byte> b(0x1c0U);
  u32(b,0U,2U);u32(b,8U,openrc::kSceneAnimationBankTagV1);u32(b,12U,2U);u32(b,16U,0x20U);
  for (unsigned actor=0U;actor<2U;++actor) {
    const auto at=0x80U+actor*0xa0U,seq=at+0x10U;
    u32(b,0x14U+actor*4U,at);u32(b,at,7U);u32(b,at+4U,1U);u32(b,at+12U,at+0x80U);
    b[seq+0x10U]=std::byte{2};b[seq+0x12U]=std::byte{255};b[seq+0x13U]=std::byte{255};
    for(unsigned frame=0U;frame<2U;++frame) {
      const auto base=seq+0x30U+frame*0x20U;
      u32(b,seq+0x1cU+frame*4U,0x30U+frame*0x20U);f32(b,base,0.5F);
      u16(b,base+6U,1U);u16(b,base+8U,8U);u16(b,base+12U,8U);u16(b,base+14U,1U);
      u16(b,base+0x16U,32767U);u16(b,base+0x18U,static_cast<std::uint16_t>(actor*10U+frame*2U));
      f32(b,at+0x80U+frame*16U,100.0F+actor*20U+frame*8U);
    }
  }
  return b;
}

std::vector<std::byte> literal_wad(const std::vector<std::byte> &source) {
  std::vector<std::byte> b(16U);b[0]=std::byte{'W'};b[1]=std::byte{'A'};b[2]=std::byte{'D'};
  for(std::size_t at=0U;at<source.size();) {
    if(at)b.insert(b.end(),{std::byte{0x11},std::byte{0},std::byte{0}});
    const auto count=std::min<std::size_t>(273U,source.size()-at);
    expect(count>=18U,"bad synthetic literal split");
    b.push_back(std::byte{0});b.push_back(static_cast<std::byte>(count-18U));
    b.insert(b.end(),source.begin()+at,source.begin()+at+count);at+=count;
  }
  u32(b,3U,static_cast<std::uint32_t>(b.size()));return b;
}

std::vector<std::byte> fixture() {
  const auto scene=literal_wad(synthetic_scene());
  std::vector<std::byte> b(0x1400U+scene.size());
  u32(b,0U,0x300U);u32(b,4U,0x800U);u32(b,8U,1U);u32(b,12U,0x240U);
  u32(b,0x10U,0x2e0U);u32(b,0x18U,1U);u32(b,0x1cU,0xa0U);
  u32(b,0x38U,1U);u32(b,0x3cU,0x100U);u32(b,0x60U,0x300U);u32(b,0x80U,0x400U);
  u32(b,0xa0U,0x100U);u32(b,0xa4U,7U);
  std::fill(b.begin()+0xb0U,b.begin()+0xc0U,std::byte{255});b[0xb0U]=std::byte{0};
  u16(b,0x104U,1U);u16(b,0x106U,1U);u16(b,0x108U,3U);
  b[0x300U]=std::byte{255};b[0x303U]=std::byte{128};
  const auto model=synthetic_class();std::copy(model.begin(),model.end(),b.begin()+0x900U);
  u32(b,0xc04U,static_cast<std::uint32_t>(scene.size()));std::copy(scene.begin(),scene.end(),b.begin()+0x1400U);
  return b;
}

void test_source_composition() {
  const auto result=openrc::compile_rac_frontend_scene_v1(fixture(),50U,limits);
  expect(result.actors.size()==2U&&result.actor_library.models.size()==2U&&
         result.actor_animation.clips.size()==2U,"Scene actors with a shared source class were merged");
  expect(result.chunk_actor_clip_ids==std::vector<std::vector<std::uint32_t>>{{0U,1U}},"Clip bindings lost source ordinal order");
  expect(result.actor_library.models[0U].textures[0U].mips[0U].rgba8==
         std::vector<std::byte>{std::byte{255},std::byte{0},std::byte{0},std::byte{255}},
         "Class slot/palette binding did not reach the neutral model");
  const auto tick=openrc::sample_rac_frontend_background_tick_v1(result.source_background.decoded_chunks[0U],1U,limits.animation.scene);
  for(std::size_t actor=0U;actor<2U;++actor) {
    const auto &clip=result.actor_animation.clips[actor];const auto &rig=result.actor_library.rigs[actor];
    const auto pose=openrc::sample_actor_animation_pose_v1(clip,
        openrc::rac_scene_actor_playback_state_v1(clip,tick),rig.semantic_key,rig.rig,{16U,16U,8U,1.e-8,1.e6F});
    expect(pose.global_joint_transforms[0U].values[3U]==static_cast<float>(actor*10U+1U),
           "Compiled actor sampled another source ordinal's animation");
    const auto vertices=openrc::pose_actor_mesh_positions_v1(result.actor_library.models[actor].meshes[0U],pose,{},
        {8U,1024U,1.e-8,1.e-8});
    expect(vertices.size()==3U,"The original triangle did not survive the existing skinning pipeline");
  }
  openrc::SceneCameraV1 camera;
  camera.right={1.0F,0.0F,0.0F};camera.up={0.0F,1.0F,0.0F};camera.forward={0.0F,0.0F,-1.0F};
  camera.tangent_half_horizontal=std::bit_cast<float>(0x3f2147afU);
  camera.tangent_half_vertical=std::bit_cast<float>(0x3ef3daf9U);
  camera.near_plane=0.03125F;camera.far_plane=728.0F;
  const openrc::RacFrontendTimelineCompileLimitsV1 bounded{
      {0x10000U,limits.actors},{0x10000U,limits.animation.animation},limits.animation.scene,{}};
  const auto timeline=openrc::compile_rac_frontend_timeline_v1(result,camera,512U,448U,bounded);
  expect(timeline.samples.size()==2U&&timeline.loop&&timeline.updates_per_second==50U,
         "Frontend timeline lost its original duration/cadence/loop");
  expect(timeline.samples[0U].actors[0U].phase==0.5F&&timeline.samples[1U].actors[0U].phase==0.0F&&
         timeline.samples[0U].actors[0U].transform.position[0U]==104.0F&&
         timeline.samples[1U].actors[0U].transform.position[0U]==100.0F,
         "Frontend timeline did not sample first update1 and loop update0");
  expect(timeline.actor_library_sha256==openrc::prepared_content_sha256_v1(
      openrc::encode_actor_library_v1(result.actor_library,bounded.actors)),"Timeline did not pin encoded library payload");
  expect(openrc::decode_scene_timeline_v1(openrc::encode_scene_timeline_v1(timeline))==timeline,
         "Compiled frontend timeline did not survive the neutral codec");
  auto changed=result;f32(changed.source_background.decoded_chunks[0U],0x40U,1.0F);
  bool rejected_camera=false;
  try{(void)openrc::compile_rac_frontend_timeline_v1(changed,camera,512U,448U,bounded);}
  catch(const openrc::RacFrontendSceneCompileError &){rejected_camera=true;}
  expect(rejected_camera,"Timeline admitted a changing camera through its constant-camera contract");
}

template<class Mutation> void reject(Mutation mutation) {
  auto bytes=fixture();auto bounded=limits;mutation(bytes,bounded);
  try { (void)openrc::compile_rac_frontend_scene_v1(bytes,50U,bounded); }
  catch(const openrc::RacFrontendSceneCompileError &) { return; }
  throw std::runtime_error("Invalid frontend source or output limit was accepted");
}

void test_qualified_menu_joint_extractor() {
  std::vector<std::byte> bytes(0x110U);
  bytes[0x10U]=std::byte{2};bytes[0x11U]=std::byte{255};
  for (unsigned frame=0U;frame<2U;++frame) {
    const auto at=0x30U+frame*0x70U;
    u32(bytes,0x1cU+frame*4U,at);f32(bytes,at,0.5F);
    u16(bytes,at+6U,6U);u16(bytes,at+8U,40U);u16(bytes,at+10U,1U);
    u16(bytes,at+12U,48U);u16(bytes,at+14U,5U);
    for(unsigned joint=0U;joint<5U;++joint) {
      u16(bytes,at+0x10U+joint*8U+(joint==0U?4U:6U),joint==0U?0x8000U:0x7fffU);
      const auto record=at+0x40U+joint*8U;
      u16(bytes,record,static_cast<std::uint16_t>(joint==0U?32767-frame:32768+frame));
      u16(bytes,record+2U,static_cast<std::uint16_t>(joint==0U?-200+static_cast<int>(frame)*100:30+frame*2));
      u16(bytes,record+4U,static_cast<std::uint16_t>(joint==0U?3+frame:7+frame));
      u16(bytes,record+6U,static_cast<std::uint16_t>(joint));
    }
    // A large, nonuniform terminal root scale must not scale child offsets.
    u16(bytes,at+0x38U,65000U);u16(bytes,at+0x3aU,123U);u16(bytes,at+0x3cU,0U);
  }
  auto sequence=openrc::parse_rac_ratchet_sequence_v1(bytes,{0U,bytes.size()},limits.animation.sequence);
  openrc::RacMobyBindRigV1 bind;bind.actor_rig.joints.resize(5U);bind.source_common_translations.resize(5U);
  for(std::size_t i=0U;i<5U;++i)bind.actor_rig.joints[i].parent_index=i==0U?-1:0;
  const auto sample=[&](float phase){return openrc::sample_rac_frontend_menu_joint_translations_v1(
      sequence,bind,0U,1U,phase,limits.animation.pose);};
  const auto result=sample(0.5F);
  expect(result[0U]==std::array<float,3U>{32766.5F,-150.0F,3.5F},"Root half-phase translation changed");
  expect(result[1U]==std::array<float,3U>{65534.0F,-181.0F,11.0F},"Source half-turn hierarchy or terminal-scale branch changed");
  expect(sample(0.0F)[1U][0U]==65535.0F&&sample(1.0F)[1U][0U]==65533.0F,"Endpoint sampling lost signed i16 extrema");
  expect(openrc::sample_rac_frontend_menu_joint_translations_v1(sequence,bind,1U,0U,0.0F,
      limits.animation.pose)[1U][0U]==65533.0F,"Forward stop lost its last->0 endpoint");
  expect(openrc::sample_rac_frontend_menu_joint_translations_v1(sequence,bind,1U,0U,1.0F,
      limits.animation.pose)[1U][0U]==65535.0F,"Reverse stop lost its last->0 endpoint");
  bool rejected_wrap=false;
  try{(void)openrc::sample_rac_frontend_menu_joint_translations_v1(sequence,bind,1U,0U,0.5F,limits.animation.pose);}
  catch(const openrc::RacFrontendSceneCompileError &){rejected_wrap=true;}
  expect(rejected_wrap,"Non-endpoint wrap sampling was admitted");
  const auto reject_sample=[&](float phase) {
    try{(void)sample(phase);}catch(const openrc::RacFrontendSceneCompileError &){return;}
    throw std::runtime_error("Unqualified menu joint path was accepted");
  };
  reject_sample(0.25F);
  u16(sequence.encoded_bytes,0x6eU,0x8000U);reject_sample(0.5F);
  u16(sequence.encoded_bytes,0x6eU,0U);
  u16(sequence.encoded_bytes,0x40U,1U);reject_sample(0.5F);
  u16(sequence.encoded_bytes,0x40U,0U);
  bind.actor_rig.joints[2U].parent_index=1;reject_sample(0.5F);
}

} // namespace

int main() try {
  test_source_composition();
  test_qualified_menu_joint_extractor();
  reject([](auto &b,auto &){u32(b,0x24cU,16U);});
  reject([](auto &b,auto &){b[0xb2U]=std::byte{1};});
  reject([](auto &b,auto &){u32(b,0xa4U,8U);});
  reject([](auto &,auto &l){l.actors.max_models=1U;});
  reject([](auto &,auto &l){l.animation.animation.max_total_frames=3U;});
  reject([](auto &,auto &l){l.max_source_bytes=64U;});
  std::cout<<"RAC frontend scene compilation tests passed\n";return 0;
} catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
