#include "openrc/scene_timeline.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace openrc {
namespace {
constexpr std::array<std::byte,8> magic{std::byte{'O'},std::byte{'R'},
    std::byte{'S'},std::byte{'C'},std::byte{'T'},std::byte{'I'},std::byte{'M'},std::byte{'1'}};
[[noreturn]] void fail(const char *message) { throw SceneTimelineError(message); }
void scalar(float value, float bound) {
  if (!std::isfinite(value) || std::abs(value)>bound)
    fail("Scene timeline component exceeds its finite bound");
}
template<std::size_t N> void components(const std::array<float,N>& values, float bound) {
  for (auto value:values) scalar(value,bound);
}
void put(std::vector<std::byte>& out,std::uint64_t value,unsigned count=4) {
  for(unsigned i=0;i<count;++i) { out.push_back(static_cast<std::byte>(value&255));value>>=8; }
}
void floats(std::vector<std::byte>& out, std::span<const float> values) {
  for(auto value:values) put(out,std::bit_cast<std::uint32_t>(value));
}
struct Reader {
  std::span<const std::byte> bytes;
  std::size_t at=0;
  std::span<const std::byte> take(std::size_t count) {
    if(count>bytes.size()-at) fail("Truncated scene timeline");
    auto result=bytes.subspan(at,count);at+=count;return result;
  }
  std::uint64_t get(unsigned count=4) {
    auto data=take(count);std::uint64_t value=0;
    for(unsigned i=0;i<count;++i) value|=std::uint64_t(std::to_integer<unsigned>(data[i]))<<(i*8);
    return value;
  }
  std::uint32_t word() { return static_cast<std::uint32_t>(get()); }
  float number() { return std::bit_cast<float>(word()); }
  void floats(std::span<float> values) { for(auto& value:values) value=number(); }
};
void write_camera(std::vector<std::byte>& out,const SceneCameraV1& camera) {
  floats(out,camera.position);floats(out,camera.right);floats(out,camera.up);floats(out,camera.forward);
  floats(out,std::array{camera.tangent_half_horizontal,camera.tangent_half_vertical,camera.near_plane,camera.far_plane});
}
SceneCameraV1 read_camera(Reader& in) {
  SceneCameraV1 result;in.floats(result.position);in.floats(result.right);
  in.floats(result.up);in.floats(result.forward);
  result.tangent_half_horizontal=in.number();result.tangent_half_vertical=in.number();
  result.near_plane=in.number();result.far_plane=in.number();return result;
}
} // namespace

void validate_scene_camera_v1(const SceneCameraV1& camera,float bound) {
  if(!std::isfinite(bound)||bound<=0) fail("Scene camera bound must be positive");
  components(camera.position,bound);components(camera.right,bound);
  components(camera.up,bound);components(camera.forward,bound);
  for(auto value:{camera.tangent_half_horizontal,camera.tangent_half_vertical,camera.near_plane,camera.far_plane}) {
    scalar(value,bound);if(value<=0) fail("Scene camera frustum must be positive");
  }
  if(camera.far_plane<=camera.near_plane) fail("Scene camera far plane precedes its near plane");
  // Preserve the authored basis without normalizing away its measured errors.
  const auto& r=camera.right;const auto& u=camera.up;const auto& f=camera.forward;
  const double determinant=double(r[0])*(double(u[1])*f[2]-double(u[2])*f[1])
      -double(r[1])*(double(u[0])*f[2]-double(u[2])*f[0])
      +double(r[2])*(double(u[0])*f[1]-double(u[1])*f[0]);
  if(!std::isfinite(determinant)||determinant==0) fail("Scene camera basis is singular");
}

void validate_scene_timeline_v1(const SceneTimelineV1& timeline,SceneTimelineLimitsV1 limits) {
  if(!std::isfinite(limits.max_absolute_component)||limits.max_absolute_component<=0 ||
      !timeline.updates_per_second||timeline.updates_per_second>1000 ||
      !timeline.display_aspect_numerator||!timeline.display_aspect_denominator||
      timeline.display_aspect_numerator>65535||timeline.display_aspect_denominator>65535 ||
      is_zero_prepared_digest_v1(timeline.actor_library_sha256)||
      is_zero_prepared_digest_v1(timeline.actor_animation_sha256)) fail("Invalid scene timeline identity or cadence");
  const auto actors=timeline.actors.size(), samples=timeline.samples.size();
  if(!actors||actors>limits.max_actors||!samples||samples>limits.max_samples||
      samples>limits.max_actor_samples/actors) fail("Scene timeline counts exceed limits");
  const auto byte_count=160U+std::uint64_t(actors)*56U+std::uint64_t(samples)*(64U+std::uint64_t(actors)*56U);
  if(byte_count>limits.max_bytes) fail("Scene timeline exceeds byte limit");
  for(auto value:timeline.clear_color) if(!std::isfinite(value)||value<0||value>1) fail("Invalid scene clear color");
  for(const auto& actor:timeline.actors) components(actor.model_to_entity.values,limits.max_absolute_component);
  for(const auto& sample:timeline.samples) {
    validate_scene_camera_v1(sample.camera,limits.max_absolute_component);
    if(sample.actors.size()!=actors) fail("Scene sample has an incomplete actor partition");
    for(const auto& actor:sample.actors) {
      if(!std::isfinite(actor.phase)||actor.phase<0||actor.phase>=1) fail("Invalid scene animation phase");
      components(actor.transform.position,limits.max_absolute_component);
      components(actor.transform.scale,limits.max_absolute_component);
      components(actor.transform.rotation,1.F);
      double norm=0;for(auto value:actor.transform.rotation) norm+=double(value)*value;
      if(std::abs(norm-1)>1.e-5) fail("Scene actor quaternion is not unit length");
    }
  }
}

void validate_scene_timeline_bindings_v1(const SceneTimelineV1& timeline,
    const ActorLibraryV1& library,const ActorAnimationBankV1& animation) {
  validate_scene_timeline_v1(timeline);
  for(std::size_t i=0;i<timeline.actors.size();++i) {
    const auto& actor=timeline.actors[i];
    if(actor.model_index>=library.models.size()||actor.rig_index>=library.rigs.size()) fail("Scene actor binding leaves its library");
    const auto& rig=library.rigs[actor.rig_index];
    if(library.models[actor.model_index].rig_key!=rig.semantic_key) fail("Scene model binding has the wrong rig");
    for(const auto& sample:timeline.samples) {
      const auto& state=sample.actors[i];
      if(state.clip_index>=animation.clips.size()) fail("Scene animation binding leaves its bank");
      const auto& clip=animation.clips[state.clip_index];
      if(clip.rig_key!=rig.semantic_key||clip.rig_content_sha256!=rig.content_sha256||
          state.frame_index>=clip.frames.size()||
          clip.frames[state.frame_index].joint_poses.size()!=rig.rig.joints.size()||
          (state.phase>0 && (state.frame_index+1U>=clip.frames.size() ||
            clip.frames[state.frame_index+1U].joint_poses.size()!=rig.rig.joints.size())))
        fail("Scene sample does not bind a complete matching animation pose");
    }
  }
}

std::vector<std::byte> encode_scene_timeline_v1(const SceneTimelineV1& timeline,SceneTimelineLimitsV1 limits) {
  validate_scene_timeline_v1(timeline,limits);
  std::vector<std::byte> body;
  body.insert(body.end(),timeline.actor_library_sha256.begin(),timeline.actor_library_sha256.end());
  body.insert(body.end(),timeline.actor_animation_sha256.begin(),timeline.actor_animation_sha256.end());
  put(body,timeline.updates_per_second);put(body,timeline.display_aspect_numerator);
  put(body,timeline.display_aspect_denominator);put(body,timeline.loop?1:0);floats(body,timeline.clear_color);
  for(const auto& actor:timeline.actors) { put(body,actor.model_index);put(body,actor.rig_index);floats(body,actor.model_to_entity.values); }
  for(const auto& sample:timeline.samples) {
    write_camera(body,sample.camera);
    for(const auto& actor:sample.actors) {
      put(body,actor.clip_index);put(body,actor.frame_index);floats(body,std::array{actor.phase});put(body,actor.enabled?1:0);
      floats(body,actor.transform.position);floats(body,actor.transform.rotation);floats(body,actor.transform.scale);
    }
  }
  std::vector<std::byte> out(magic.begin(),magic.end());
  put(out,1);put(out,64);put(out,64U+body.size(),8);put(out,timeline.actors.size());put(out,timeline.samples.size());
  const auto digest=prepared_content_sha256_v1(body);out.insert(out.end(),digest.begin(),digest.end());
  out.insert(out.end(),body.begin(),body.end());return out;
}

SceneTimelineV1 decode_scene_timeline_v1(std::span<const std::byte> bytes,SceneTimelineLimitsV1 limits) {
  if(bytes.size()<160||bytes.size()>limits.max_bytes) fail("Scene timeline byte envelope exceeds limits");
  Reader in{bytes};auto signature=in.take(8);
  if(!std::equal(signature.begin(),signature.end(),magic.begin())||in.get()!=1||in.get()!=64||in.get(8)!=bytes.size()) fail("Invalid scene timeline header");
  const auto actors=in.word(),samples=in.word();
  if(!actors||actors>limits.max_actors||!samples||samples>limits.max_samples||samples>limits.max_actor_samples/actors||
      160U+std::uint64_t(actors)*56U+std::uint64_t(samples)*(64U+std::uint64_t(actors)*56U)!=bytes.size()) fail("Scene timeline counts do not partition its payload");
  auto stored=in.take(32);const auto digest=prepared_content_sha256_v1(bytes.subspan(64));
  if(!std::equal(stored.begin(),stored.end(),digest.begin())) fail("Scene timeline digest mismatch");
  SceneTimelineV1 result;
  auto identity=in.take(32);std::copy(identity.begin(),identity.end(),result.actor_library_sha256.begin());
  identity=in.take(32);std::copy(identity.begin(),identity.end(),result.actor_animation_sha256.begin());
  result.updates_per_second=in.word();result.display_aspect_numerator=in.word();result.display_aspect_denominator=in.word();
  auto flags=in.word();if(flags>1) fail("Unknown scene timeline flags");result.loop=flags==1;in.floats(result.clear_color);
  result.actors.resize(actors);
  for(auto& actor:result.actors) { actor.model_index=in.word();actor.rig_index=in.word();in.floats(actor.model_to_entity.values); }
  result.samples.resize(samples);
  for(auto& sample:result.samples) {
    sample.camera=read_camera(in);sample.actors.resize(actors);
    for(auto& actor:sample.actors) {
      actor.clip_index=in.word();actor.frame_index=in.word();actor.phase=in.number();
      flags=in.word();if(flags>1) fail("Unknown scene actor flags");actor.enabled=flags==1;
      in.floats(actor.transform.position);in.floats(actor.transform.rotation);in.floats(actor.transform.scale);
    }
  }
  validate_scene_timeline_v1(result,limits);return result;
}

std::size_t scene_timeline_sample_index_v1(const SceneTimelineV1& timeline,std::uint64_t elapsed_updates) {
  if(timeline.samples.empty()) fail("Cannot sample an empty scene timeline");
  return static_cast<std::size_t>(timeline.loop?elapsed_updates%timeline.samples.size():
      std::min<std::uint64_t>(elapsed_updates,timeline.samples.size()-1));
}
} // namespace openrc
