#include "openrc/scene_timeline.hpp"

#include <iostream>
#include <limits>

using namespace openrc;
namespace {
void expect(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class Action> void rejects(Action action) {
  try { action(); } catch(const SceneTimelineError&) { return; }
  throw std::runtime_error("Malformed scene timeline was accepted");
}
SceneTimelineV1 fixture() {
  SceneTimelineV1 t;
  t.actor_library_sha256[0]=std::byte{1};t.actor_animation_sha256[0]=std::byte{2};
  t.updates_per_second=50;t.display_aspect_numerator=8;t.display_aspect_denominator=7;
  t.loop=true;t.actors.resize(2);t.actors[1].model_index=1;
  t.samples.resize(3);
  for(std::size_t i=0;i<t.samples.size();++i) {
    auto& s=t.samples[i];s.camera.position={float(i),0,1};
    s.camera.tangent_half_horizontal=0.6F;s.camera.tangent_half_vertical=0.45F;
    s.camera.near_plane=0.1F;s.camera.far_plane=500;s.actors.resize(2);
    s.actors[0].phase=0.5F;s.actors[1].transform.position={float(i*3),2,0};
  }
  return t;
}
void hash_again(std::vector<std::byte>& bytes) {
  auto digest=prepared_content_sha256_v1(std::span(bytes).subspan(64));
  std::copy(digest.begin(),digest.end(),bytes.begin()+32);
}
}
int main() try {
  const auto t=fixture();const auto bytes=encode_scene_timeline_v1(t);
  expect(decode_scene_timeline_v1(bytes)==t,"Timeline roundtrip lost pose, camera or cadence");
  expect(encode_scene_timeline_v1(decode_scene_timeline_v1(bytes))==bytes,"Timeline bytes are not canonical");
  expect(scene_timeline_sample_index_v1(t,3)==0 && scene_timeline_sample_index_v1(t,UINT64_MAX)==0,"Loop clock failed its boundary");
  auto bad=t;bad.loop=false;expect(scene_timeline_sample_index_v1(bad,UINT64_MAX)==2,"Nonlooping timeline does not hold final sample");
  bad=t;bad.samples[0].actors.pop_back();rejects([&]{validate_scene_timeline_v1(bad);});
  bad=t;bad.samples[0].actors[0].phase=1;rejects([&]{validate_scene_timeline_v1(bad);});
  bad=t;bad.samples[0].camera.forward=bad.samples[0].camera.up;rejects([&]{validate_scene_timeline_v1(bad);});
  bad=t;bad.samples[0].camera.position[0]=std::numeric_limits<float>::infinity();rejects([&]{validate_scene_timeline_v1(bad);});
  bad=t;bad.samples[0].actors[0].transform.rotation={0,0,0,0};rejects([&]{validate_scene_timeline_v1(bad);});
  SceneTimelineLimitsV1 limits;limits.max_actor_samples=5;rejects([&]{(void)decode_scene_timeline_v1(bytes,limits);});
  limits={};limits.max_bytes=bytes.size()-1;rejects([&]{(void)encode_scene_timeline_v1(t,limits);});
  auto raw=bytes;raw.back()^=std::byte{1};rejects([&]{(void)decode_scene_timeline_v1(raw);});
  raw=bytes;raw[140]=std::byte{2};hash_again(raw);rejects([&]{(void)decode_scene_timeline_v1(raw);});
  raw=bytes;raw[24]=std::byte{255};rejects([&]{(void)decode_scene_timeline_v1(raw);});
  for(std::size_t size:{0U,63U,159U,200U}) rejects([&]{(void)decode_scene_timeline_v1(std::span(bytes).first(size));});
  ActorLibraryV1 library;library.rigs.resize(1);library.rigs[0].semantic_key="rig";
  library.rigs[0].rig.joints.resize(1);library.rigs[0].content_sha256[0]=std::byte{3};
  library.models.resize(2);for(auto& model:library.models) model.rig_key="rig";
  ActorAnimationBankV1 animation;animation.clips.resize(1);auto& clip=animation.clips[0];
  clip.rig_key="rig";clip.rig_content_sha256=library.rigs[0].content_sha256;
  clip.frames.resize(2);for(auto& frame:clip.frames) frame.joint_poses.resize(1);
  validate_scene_timeline_bindings_v1(t,library,animation);
  bad=t;bad.samples[2].actors[1].clip_index=1;rejects([&]{validate_scene_timeline_bindings_v1(bad,library,animation);});
  bad=t;bad.samples[0].actors[0].frame_index=1;rejects([&]{validate_scene_timeline_bindings_v1(bad,library,animation);});
  clip.rig_content_sha256[1]=std::byte{1};rejects([&]{validate_scene_timeline_bindings_v1(t,library,animation);});
  std::cout<<"Scene timeline codec, admission and clock tests passed\n";return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
