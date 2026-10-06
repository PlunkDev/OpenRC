#include "openrc/rac_frontend_menu_actor_compile.hpp"
#include "openrc/actor_animation_player.hpp"
#include "openrc/hash.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace openrc;
void check(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F &&f) {
  bool rejected=false;try{f();}catch(const std::runtime_error &){rejected=true;}
  check(rejected,"Unresolved menu model source/neutral binding was accepted");
}
constexpr ActorLibraryLimitsV1 actors{1U,1U,128U,256U,255U,255U,16U,1U,16U,17U,
    4U,65536U,65536U,196608U,4096U,4096U,16000000U,64000000U};
constexpr ActorAnimationLimitsV1 animation{14U,255U,3570U,255U,910350U,128U,
    4096U,120U,1.e6F,1.e-8};
constexpr RacFrontendMenuActorTimelineLimitsV1 timeline_limits{
    {64U*1024U*1024U,actors},{64U*1024U*1024U,animation},{}};
constexpr RacFrontendMenuActorCompileLimitsV1 compile_limits{
    {1048576U,false},{{1048576U,255U,1.e-8},
      {{1048576U,65536U,65536U,65536U,65536U,65536U,65536U},255U,65536U,65536U},65536U},
    actors,{{1048576U,1048576U,255U,255U},{255U,1048576U,65535U,65535U,1.e-8},animation}};

RacFrontendMenuActorCompileResultV1 neutral_fixture() {
  RacFrontendMenuActorCompileResultV1 result;result.source_class_scale_bits=0x3e800000U;
  ActorRigAssetV1 rig;rig.semantic_key="frontend/menu/rig";rig.rig.joints.resize(1U);
  ActorModelV1 model;model.semantic_key="frontend/menu/model";model.rig_key=rig.semantic_key;
  model.materials.resize(1U);model.meshes.resize(1U);
  auto &mesh=model.meshes[0U];mesh.vertices.resize(3U);
  mesh.vertices[1U].x=1.0F;mesh.vertices[2U].y=1.0F;
  for(auto &v:mesh.vertices)v.skin={1U,{0U,0U,0U},{1U,0U,0U},1U};
  mesh.triangle_indices={0U,1U,2U};mesh.draw_ranges={{0U,0U,3U}};
  result.actor_library=canonicalize_actor_library_v1({1U,{rig},{model}},actors);
  for(std::uint32_t slot=0U;slot<14U;++slot) {
    result.source_sequences[slot]=100U+slot;result.slot_clip_ids[slot]=slot;
    ActorAnimationClipV1 clip;clip.id=slot;clip.rig_key=rig.semantic_key;
    clip.semantic_key="frontend/menu/slot/"+std::string(slot<10U?"0":"")+std::to_string(slot);
    clip.rig_content_sha256=result.actor_library.rigs[0U].content_sha256;
    clip.source_updates_per_second=50U;clip.frames.resize(7U);
    for(std::size_t f=0U;f<clip.frames.size();++f) {
      clip.frames[f].phase_rate=0.5F;clip.frames[f].joint_poses.resize(1U);
      clip.frames[f].joint_poses[0U].translation[0U]=float(f);
    }
    result.actor_animation.clips.push_back(std::move(clip));
  }
  result.actor_animation=canonicalize_actor_animation_bank_v1(result.actor_animation,animation);
  return result;
}
std::array<RacFrontendMainGeometryFrameV1,13U> entry_fixture(
    const RacFrontendMenuActorCompileResultV1 &compiled) {
  std::array<RacFrontendMainGeometryFrameV1,13U> entry;
  for(std::size_t tick=0U;tick<entry.size();++tick) {
    entry[tick].main_active=tick==12U;
    for(std::size_t slot=0U;slot<14U;++slot) {
      auto &state=entry[tick].models[slot];auto &a=state.animation;auto &p=state.post;
      state.enabled=slot!=6U;
      a.indices.previous_sequence=a.indices.current_sequence=static_cast<std::uint8_t>(compiled.source_sequences[slot]);
      a.indices.previous_frame=tick==12U?6U:static_cast<std::uint8_t>(5U-tick/2U);
      a.indices.current_frame=tick==12U?0U:static_cast<std::uint8_t>(a.indices.previous_frame+1U);
      a.phase_bits=tick==12U?0x3f800000U:tick%2U==0U?0x3f000000U:0U;
      a.speed_bits=tick==12U?0U:0xbf800000U;a.rate_bits=0x3f000000U;
      p.previous_sequence=a.indices.previous_sequence;p.current_sequence=a.indices.current_sequence;
      p.previous_frame=a.indices.previous_frame;p.phase_bits=a.phase_bits;
      p.scale_bits=compiled.source_class_scale_bits;p.position_bits={0x43800000U,0x43800000U,0x42800000U,0U};
      for(unsigned axis=0U;axis<3U;++axis)p.matrix_bits[axis][axis]=0x3f800000U;
    }
  }
  return entry;
}
void timeline() {
  const auto compiled=neutral_fixture();const auto entry=entry_fixture(compiled);
  const auto result=compile_rac_frontend_menu_actor_timeline_v1(compiled,entry,timeline_limits);
  check(result.actors.size()==14U && result.samples.size()==13U && !result.loop &&
      result.display_aspect_numerator==512U && result.display_aspect_denominator==512U &&
      scene_timeline_sample_index_v1(result,1000U)==12U,"Menu instance count, display or final hold differs");
  for(std::size_t tick=0U;tick<13U;++tick)for(std::size_t slot=0U;slot<14U;++slot) {
    const auto &sample=result.samples[tick].actors[slot];
    check(sample.enabled==(slot!=6U),"Menu source visibility was replaced by unconditional model draws");
    const auto &clip=compiled.actor_animation.clips[sample.clip_index];
    auto playback=start_actor_animation_playback_v1(clip);
    playback.frame_index=sample.frame_index;playback.phase=sample.phase;
    const auto pose=sample_actor_animation_pose_v1(clip,playback,clip.rig_key,
        compiled.actor_library.rigs[0U].rig,{16U,16U,255U,1.e-8,1.e6F});
    const auto expected=tick==12U?0.0F:5.5F-float(tick)*0.5F;
    check(pose.global_joint_transforms[0U].values[3U]==expected,
        "Reverse entry or last-to-zero phase-one endpoint sampled the wrong neutral pose");
    check(sample.transform.scale==std::array{1.0F,1.0F,1.0F} &&
        sample.transform.position==std::array{256.0F,256.0F,64.0F},
        "Menu source class scale was applied twice or world position changed");
  }
  check(decode_scene_timeline_v1(encode_scene_timeline_v1(result))==result,
      "Menu timeline codec changed samples or exact resource digests");
  const auto reject_state=[&](auto mutate) {auto changed=entry;mutate(changed);
    rejects([&]{(void)compile_rac_frontend_menu_actor_timeline_v1(compiled,changed,timeline_limits);});};
  reject_state([](auto &v){v[0U].main_active=true;});
  reject_state([](auto &v){v[12U].models[0U].animation.speed_bits=0xbf800000U;});
  reject_state([](auto &v){v[0U].models[0U].post.scale_bits=0x3f800000U;});
  reject_state([](auto &v){v[0U].models[0U].post.rotation_bits[2U]=0x3f800000U;});
  reject_state([](auto &v){v[0U].models[0U].post.matrix_bits[0U][0U]=0U;});
  reject_state([](auto &v){v[0U].models[0U].animation.indices.current_frame=0U;});
  reject_state([](auto &v){v[0U].models[0U].post.previous_sequence=0U;});
  reject_state([](auto &v){v[0U].models[0U].post.position_bits[0U]=0x7f800000U;});
  auto changed=compiled;changed.slot_clip_ids[0U]=14U;
  rejects([&]{(void)compile_rac_frontend_menu_actor_timeline_v1(changed,entry,timeline_limits);});
  auto limited=timeline_limits;limited.timeline.max_actor_samples=181U;
  rejects([&]{(void)compile_rac_frontend_menu_actor_timeline_v1(compiled,entry,limited);});
  rejects([&]{(void)compile_rac_frontend_menu_actor_v1({}, {},{},0U,{},50U,compile_limits);});
  rejects([&]{(void)compile_rac_frontend_menu_actor_v1({}, {},{},0U,{},49U,compile_limits);});
}

std::vector<std::byte> read(const char *path) {
  std::ifstream stream(std::filesystem::path(path),std::ios::binary|std::ios::ate);
  if(!stream)throw std::runtime_error("Cannot read original menu fixture");
  const auto size=stream.tellg();check(size>0 && size<64*1024*1024,"Menu fixture exceeds bounded bytes");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));stream.seekg(0);
  stream.read(reinterpret_cast<char *>(bytes.data()),size);check(bool(stream),"Truncated menu fixture");return bytes;
}
std::uint32_t word(std::span<const std::byte> bytes,std::size_t at) {
  check(at<=bytes.size() && bytes.size()-at>=4U,"Truncated source fixture word");
  std::uint32_t v=0U;for(unsigned i=0U;i<4U;++i)v|=std::to_integer<std::uint32_t>(bytes[at+i])<<(8U*i);return v;
}
void active_source(const char *path,const std::array<RacFrontendMainGeometryFrameV1,13> &entry,
    const RacFrontendObjectAnimationBankV1 &bank) {
  const auto bytes=read(path);
  check(bytes.size()==47888 && hex_digest(prepared_content_sha256_v1(bytes))==
      "8ac7ae0b9081e5aa69be750b1db480a0defaf6a59207411035c9502064b0502b",
      "Original main active-animation fixture differs");
  constexpr char magic[]="MAINACT1";
  check(std::equal(bytes.begin(),bytes.begin()+8,reinterpret_cast<const std::byte *>(magic)) &&
      word(bytes,8)==64 && word(bytes,12)==14,"Original active-animation fixture header differs");
  std::size_t at=16;
  std::array<RacFrontendObjectAnimationStateV1,14> states;
  for(unsigned tick=0;tick<64;++tick) {
    const std::array<std::uint32_t,5> expected_root{
      tick<12?1U:46U,tick<12?0U:0x1d4948U,tick<12?0x1d4948U:0U,0x1d4948U,tick<12?12U-tick:0U};
    for(const auto value:expected_root) {check(word(bytes,at)==value,"Original active main screen gate differs");at+=4;}
    for(unsigned slot=0;slot<14;++slot) {
      auto &s=states[slot];
      if(tick<13)s=entry[tick].models[slot].animation;
      else s=step_rac_frontend_object_animation_v1(s,bank).state;
      const std::array<std::uint32_t,13> words{s.indices.previous_frame,s.indices.current_frame,
        s.indices.previous_sequence,s.indices.current_sequence,s.references.previous_frame_reference,
        s.references.current_frame_reference,s.references.sound_byte,s.references.trigger_byte,
        s.phase_bits,s.speed_bits,s.rate_bits,s.flags,s.sound_handle};
      for(const auto value:words) {check(word(bytes,at)==value,
          "Compiled menu animation state differs from original entry/active instructions");at+=4;}
      if(tick>=13) {
        auto settled=entry.back().models[slot].animation;settled.flags=0;
        check(s==settled,"Active main actor changed after the original reverse stop");
      }
    }
  }
  check(at==bytes.size(),"Original active-animation fixture has trailing bytes");
  std::cout<<"Main active source:64 updates/896 object states match; pose holds from sample12 (30433 instructions)\n";
}
void real_source(const char *wad_path,const char *screen_path,const char *elf_path,const char *active_path) {
  const auto wad=read(wad_path);const auto fixture=read(screen_path);
  constexpr char magic[]="FROOBJT1";
  check(fixture.size()>=16U && std::equal(fixture.begin(),fixture.begin()+8U,
      reinterpret_cast<const std::byte *>(magic)),"Wrong original screen fixture tag");
  const auto class_size=word(fixture,8U);check(class_size<fixture.size()-16U,"Truncated source class fixture");
  const auto bytes=std::span(fixture).subspan(12U,class_size);
  std::size_t at=12U+class_size;check(word(fixture,at)==2U,"Screen fixture directory differs");at+=4U;
  auto take=[&]{const auto v=word(fixture,at);at+=4U;return v;};
  RacFrontendMainAssetsV1 assets;auto &screen=assets.screen;
  assets.initial_visibility=decode_rac_frontend_main_visibility_v1(read(elf_path),32U*1024U*1024U);
  screen.source_reference=take();screen.parent_reference=take();screen.active_state=take();
  for(auto &v:screen.sequences)v=take();for(auto &v:screen.node_references)v=take();
  for(auto &v:screen.init_callbacks)v=take();for(auto &v:screen.cleanup_callbacks)v=take();
  for(unsigned i=0U;i<3U;++i)assets.nodes[i].object_slot=2U+i;
  std::array<std::uint8_t,16U> slots;slots.fill(255U);bool found=false;unsigned used=0U;
  check(word(wad,0x18U)<=256U,"Unbounded source Moby directory");
  for(unsigned i=0U;i<word(wad,0x18U);++i) {
    const auto row=word(wad,0x1cU)+i*32U;
    if(word(wad,row+4U)!=1138U)continue;
    const auto begin=std::uint64_t(word(wad,4U))+word(wad,row);
    check(begin<=wad.size() && class_size<=wad.size()-begin &&
        std::equal(bytes.begin(),bytes.end(),wad.begin()+begin),"Source screen class differs from frontend directory");
    for(unsigned j=0U;j<16U;++j) {slots[j]=static_cast<std::uint8_t>(word(wad,row+16U+(j&~3U))>>(8U*(j&3U)));
      if(slots[j]!=255U)++used;}
    check(!found,"Duplicate menu class directory entry");found=true;
  }
  check(found,"Original class1138 is absent");
  std::uint64_t gs_size=0U;const auto uploads=word(wad,8U),directory=word(wad,12U);
  check(uploads<=4096U,"Unbounded GS fixture directory");
  for(unsigned i=0U;i<uploads;++i) {
    const auto row=directory+i*16U,type=word(wad,row),dimensions=word(wad,row+4U);
    check(word(wad,row+12U)==gs_size,"Source GS texture rows are not contiguous");
    if(type==0U)gs_size+=1024U;else if(type==2U)gs_size+=512U;
    else {check(type==19U,"Unknown source GS fixture format");
      gs_size+=std::max(UINT64_C(256),std::uint64_t(dimensions&65535U)*(dimensions>>16U));}
  }
  const auto gs_at=word(wad,0U),table=word(wad,0x3cU);const auto count=word(wad,0x38U);
  check(gs_at<=wad.size() && gs_size<=wad.size()-gs_at && table<=wad.size() &&
      count<256U && std::uint64_t(count)*16U<=wad.size()-table,"Source texture fixture leaves WAD");
  const auto textures=decode_rac_level_moby_texture_bank_v1(std::span(wad).subspan(table,count*16U),wad,
      std::span(wad).subspan(gs_at,static_cast<std::size_t>(gs_size)),
      std::uint64_t(word(wad,4U))+word(wad,0x60U),
      {64U*1024U*1024U,64U*1024U*1024U,64U*1024U*1024U,255U,4096U,4096U,16000000U,16000000U,64000000U});
  const auto compiled=compile_rac_frontend_menu_actor_v1(bytes,textures,slots,
      static_cast<std::uint8_t>(used),screen.sequences,50U,compile_limits);
  const auto entry=compile_rac_frontend_main_entry_geometry_v1(bytes,assets);
  const auto result=compile_rac_frontend_menu_actor_timeline_v1(compiled,entry,timeline_limits);
  check(std::all_of(assets.initial_visibility->enabled_words.begin(),assets.initial_visibility->enabled_words.end(),
      [](auto value){return value==1U;}) && assets.initial_visibility->slot_six_enabled_word==0U,
      "Original startup visibility fixture differs from its qualified source domain");
  for(const auto &sample:result.samples)for(std::size_t i=0U;i<14U;++i)
    check(sample.actors[i].enabled==(i!=6U),"Original initial slot6 gate was lost");
  const auto source_class=decode_rac_frontend_object_class_v1(bytes,0x400000U,4U,0U,1048576U);
  if(active_path)active_source(active_path,entry,source_class.animations);
  float max_error=0.0F;std::size_t components=0U,vertices=0U;
  for(std::size_t tick=0U;tick<13U;++tick)for(std::size_t slot=0U;slot<14U;++slot) {
    const auto &sample=result.samples[tick].actors[slot];
    const auto &clip=compiled.actor_animation.clips[sample.clip_index];
    auto playback=start_actor_animation_playback_v1(clip);playback.frame_index=sample.frame_index;playback.phase=sample.phase;
    const auto pose=sample_actor_animation_pose_v1(clip,playback,clip.rig_key,
        compiled.actor_library.rigs[0U].rig,{16U,16U,255U,1.e-8,1.e6F});
    for(std::size_t corner=0U;corner<4U;++corner)for(std::size_t axis=0U;axis<3U;++axis) {
      const auto neutral=pose.global_joint_transforms[source_class.corner_joints[corner]].values[axis*4U+3U]+
          sample.transform.position[axis];
      const auto original=std::bit_cast<float>(entry[tick].corners[slot][corner][axis]);
      max_error=std::max(max_error,std::abs(neutral-original));++components;
    }
    for(const auto &mesh:compiled.actor_library.models[0U].meshes)
      vertices+=pose_actor_mesh_positions_v1(mesh,pose,{}, {255U,65536U,1.e-8,1.e-8}).size();
  }
  std::cout<<"menu source: 14 clips, 13 updates, "<<components<<" corner components, max error="<<max_error
           <<", "<<vertices<<" posed vertices\n";
  check(max_error<=0.0001220703125F,"Neutral menu pose differs from original source corner extraction by more than four ULP at256");
  check(vertices>0U,"Original menu models emitted no renderable vertices");
  check(decode_actor_library_v1(encode_actor_library_v1(compiled.actor_library,timeline_limits.actors),timeline_limits.actors)==compiled.actor_library &&
      decode_actor_animation_bank_v1(encode_actor_animation_bank_v1(compiled.actor_animation,timeline_limits.animation),timeline_limits.animation)==compiled.actor_animation &&
      decode_scene_timeline_v1(encode_scene_timeline_v1(result))==result,"Menu neutral resources did not survive their codecs");
}
} // namespace
int main(int argc,char **argv) {
  try {
    check(argc==1 || argc==4 || argc==5,
        "Usage: menu actor tests [frontend.wad.decoded.bin screen-source.bin boot.elf [active-source.bin]]");
    timeline();if(argc>=4)real_source(argv[1],argv[2],argv[3],argc==5?argv[4]:nullptr);
    std::cout<<"rac_frontend_menu_actor_compile_tests: passed\n";return 0;
  } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
