#include "openrc/rac_frontend_menu_actor_compile.hpp"

#include "openrc/rac_frontend_decoration.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>

namespace openrc {
namespace {
[[noreturn]] void fail(const char *message) {
  throw RacFrontendMenuActorCompileError(message);
}
constexpr auto rig_key="frontend/menu/rig";
constexpr auto model_key="frontend/menu/model";
std::string clip_key(std::size_t slot) {
  std::string result="frontend/menu/slot/";
  result.push_back(static_cast<char>('0'+slot/10U));
  result.push_back(static_cast<char>('0'+slot%10U));
  return result;
}
bool source_zero(std::uint32_t bits) {return (bits&0x7fffffffU)==0U;}
} // namespace

RacFrontendMenuActorCompileResultV1 compile_rac_frontend_menu_actor_v1(
    std::span<const std::byte> bytes,const RacLevelMobyTextureBankV1 &textures,
    const std::array<std::uint8_t,16U> &slots,std::uint8_t used,
    const std::array<std::uint32_t,14U> &sequences,std::uint32_t cadence,
    RacFrontendMenuActorCompileLimitsV1 limits) {
  if(cadence!=50U && cadence!=60U)fail("Menu actor source cadence is unsupported");
  const auto source=parse_rac_moby_class_v1(bytes,limits.source_class);
  // Validate the actual five-joint/path/sequence owner before invoking the
  // existing generic geometry and animation compilers.
  (void)decode_rac_frontend_object_class_v1(bytes,0x400000U,4U,0U,
      limits.source_class.max_input_bytes);
  std::array<RacMobyAnimationClipProfileV1,14U> profiles;
  RacFrontendMenuActorCompileResultV1 out;
  out.source_sequences=sequences;out.source_class_scale_bits=source.scale_bits;
  for(std::size_t i=0U;i<profiles.size();++i) {
    if(sequences[i]>=source.sequence_offsets.size() || !source.sequence_offsets[sequences[i]] ||
       std::find(sequences.begin(),sequences.begin()+i,sequences[i])!=sequences.begin()+i)
      fail("Menu source sequence selection is absent or repeated");
    const auto id=static_cast<std::uint32_t>(i);
    profiles[i]={id,sequences[i],clip_key(i),ActorAnimationWrapModeV1::clamp};
    out.slot_clip_ids[i]=id;
  }
  auto geometry=compile_rac_moby_complete_bind_pose_geometry_v1(
      bytes,source,RacMobyLodV1::high,limits.bind_pose);
  out.actor_animation=compile_rac_moby_animation_bank_v1(bytes,source,
      geometry.bind_rig,rig_key,profiles,cadence,limits.animation);
  RacActorLibraryCompileRequestV1 request;
  request.rig_semantic_key=rig_key;request.model_semantic_key=model_key;
  request.bind_pose=std::move(geometry);request.texture_bank=textures;
  request.texture_slots=slots;request.used_texture_slot_count=used;
  out.actor_library=compile_rac_actor_library_v1(request,limits.actors);
  return out;
}

SceneTimelineV1 compile_rac_frontend_menu_actor_timeline_v1(
    const RacFrontendMenuActorCompileResultV1 &compiled,
    const std::array<RacFrontendMainGeometryFrameV1,13U> &entry,
    RacFrontendMenuActorTimelineLimitsV1 limits) {
  validate_actor_library_v1(compiled.actor_library,limits.actors.library);
  validate_actor_animation_bank_v1(compiled.actor_animation,limits.animation.bank);
  if(compiled.actor_library.models.size()!=1U || compiled.actor_library.rigs.size()!=1U ||
     compiled.actor_animation.clips.size()!=14U ||
     compiled.actor_library.models[0U].semantic_key!=model_key ||
     compiled.actor_library.rigs[0U].semantic_key!=rig_key ||
     !std::isfinite(std::bit_cast<float>(compiled.source_class_scale_bits)) ||
     std::bit_cast<float>(compiled.source_class_scale_bits)<=0.0F ||
     limits.timeline.max_actors<14U || limits.timeline.max_samples<13U ||
     limits.timeline.max_actor_samples<182U)
    fail("Menu actor resources or timeline limits do not cover fourteen source objects");
  SceneTimelineV1 out;
  out.actor_library_sha256=prepared_content_sha256_v1(
      encode_actor_library_v1(compiled.actor_library,limits.actors));
  out.actor_animation_sha256=prepared_content_sha256_v1(
      encode_actor_animation_bank_v1(compiled.actor_animation,limits.animation));
  out.updates_per_second=compiled.actor_animation.clips[0U].source_updates_per_second;
  if(out.updates_per_second!=50U && out.updates_per_second!=60U)
    fail("Menu timeline source cadence is unsupported");
  out.display_aspect_numerator=512U;out.display_aspect_denominator=512U;
  out.actors.resize(14U);out.samples.reserve(13U);
  const auto camera=execute_rac_frontend_menu_camera_v1();
  for(std::size_t tick=0U;tick<entry.size();++tick) {
    const auto &frame=entry[tick];
    if(frame.main_active!=(tick+1U==entry.size()))
      fail("Menu timeline did not retain the original twelve-update entry gate");
    SceneTimelineSampleV1 sample;sample.camera=camera;sample.actors.reserve(14U);
    for(std::size_t slot=0U;slot<14U;++slot) {
      const auto clip_id=compiled.slot_clip_ids[slot];
      if(clip_id!=slot)fail("Menu slot to clip order changed");
      const auto &clip=compiled.actor_animation.clips[clip_id];
      const auto &state=frame.models[slot];
      const auto &animation=state.animation;const auto &post=state.post;
      if(clip.semantic_key!=clip_key(slot) || clip.rig_key!=rig_key ||
         clip.source_updates_per_second!=out.updates_per_second ||
         clip.wrap_mode!=ActorAnimationWrapModeV1::clamp ||
         animation.indices.previous_sequence!=compiled.source_sequences[slot] ||
         animation.indices.current_sequence!=compiled.source_sequences[slot] ||
         post.previous_sequence!=animation.indices.previous_sequence ||
         post.current_sequence!=animation.indices.current_sequence ||
         post.previous_frame!=animation.indices.previous_frame || post.phase_bits!=animation.phase_bits ||
         post.scale_bits!=compiled.source_class_scale_bits || post.spatial_reference || post.state_byte ||
         animation.indices.previous_frame>=clip.frames.size() ||
         animation.indices.current_frame>=clip.frames.size())
        fail("Menu model snapshot disagrees with its source pose and neutral clip owners");
      for(std::size_t axis=0U;axis<3U;++axis) {
        if(!source_zero(post.rotation_bits[axis]))fail("Menu object rotation is outside the zero-rotation owner");
        for(std::size_t lane=0U;lane<4U;++lane)
          if(axis==lane ? post.matrix_bits[axis][lane]!=0x3f800000U :
              !source_zero(post.matrix_bits[axis][lane]))
            fail("Menu post matrix is outside the executed identity rotation");
      }
      if(tick+1U==entry.size() && !source_zero(animation.speed_bits) && !source_zero(animation.rate_bits))
        fail("Menu entry ended while its source object was still moving");
      SceneTimelineActorSampleV1 actor;actor.clip_index=clip_id;
      actor.enabled=state.enabled;
      actor.frame_index=animation.indices.previous_frame;
      if(animation.phase_bits==0x3f800000U)actor.frame_index=animation.indices.current_frame;
      else if(animation.phase_bits==0x3f000000U) {
        if(std::uint32_t(animation.indices.previous_frame)+1U!=animation.indices.current_frame)
          fail("Menu half-phase does not select adjacent neutral frames");
        actor.phase=0.5F;
      } else if(!source_zero(animation.phase_bits))fail("Menu pose phase is outside its executed endpoint/half domain");
      for(std::size_t axis=0U;axis<3U;++axis) {
        const auto position=std::bit_cast<float>(post.position_bits[axis]);
        actor.transform.position[axis]=position==0.0F ? 0.0F : position;
      }
      // Ordinary geometry and joint translations already include class scale.
      // Identity WorldTransform scale must not apply it for a second time.
      sample.actors.push_back(actor);
    }
    out.samples.push_back(std::move(sample));
  }
  validate_scene_timeline_v1(out,limits.timeline);
  validate_scene_timeline_bindings_v1(out,compiled.actor_library,compiled.actor_animation);
  return out;
}
} // namespace openrc
