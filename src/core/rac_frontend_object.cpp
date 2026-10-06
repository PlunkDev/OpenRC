#include "openrc/rac_frontend_object.hpp"
#include "openrc/rac_frontend_numeric.hpp"
#include "openrc/rac_frontend_scene_compile.hpp"
#include "openrc/ee_cop1_numeric.hpp"
#include "openrc/dvp_vu_numeric.hpp"

#include <algorithm>
#include <bit>

namespace openrc {
namespace {
[[noreturn]] void fail(const char *message) { throw RacFrontendObjectError(message); }
void synchronize_selection(RacFrontendObjectV1 &object) {
  object.post.previous_frame=object.animation.indices.previous_frame;
  object.post.previous_sequence=object.animation.indices.previous_sequence;
  object.post.current_sequence=object.animation.indices.current_sequence;
  object.post.phase_bits=object.animation.phase_bits;
}
const RacFrontendObjectScreenV1 &screen_at(
    std::span<const RacFrontendObjectScreenV1> screens, std::uint32_t reference) {
  const RacFrontendObjectScreenV1 *found=nullptr;
  for(const auto &screen:screens) if(screen.source_reference==reference) {
    if(found) fail("Frontend screen has duplicate source ownership");
    found=&screen;
  }
  if(!found || !reference) fail("Reached frontend screen has no source owner");
  return *found;
}
void validate_class(const RacFrontendObjectClassV1 &c) {
  if(c.allocation.class_id_bits!=1138U || c.allocation.class_table_index==255U ||
     !c.allocation.model || !c.allocation.model->sequence0 ||
     c.allocation.model->spatial_reference || c.allocation.model->auxiliary_reference ||
     c.allocation.model->flags || c.animations.clips.empty() ||
     c.animations.clips.size()!=c.post_headers.size())
    fail("Frontend object class is outside the recovered non-spatial owner");
}
void validate_source_owners(const RacFrontendObjectClassV1 &c,
                          const RacMobyAllocateBindingsV1 &bindings) {
  if(!bindings.pvar_base_bits) fail("Frontend PVar source owner is absent");
  const std::uint64_t model_begin=c.allocation.model->reference;
  const auto model_end=model_begin+c.source_bytes;
  const std::uint64_t actor_begin=bindings.cursor.dynamic_begin_bits;
  const auto actor_end=std::uint64_t(bindings.cursor.exclusive_end_bits)+256U;
  const std::uint64_t pvar_begin=*bindings.pvar_base_bits;
  if(actor_end<actor_begin) fail("Frontend actor pool wraps");
  const auto pvar_end=pvar_begin+(actor_end-actor_begin)/2U;
  if(model_end>UINT64_C(0x100000000) || !c.source_bytes ||
     (model_begin<actor_end && actor_begin<model_end) ||
     (model_begin<pvar_end && pvar_begin<model_end))
    fail("Frontend source model aliases mutable object/PVar owners");
  // Resident allocator globals differ in address from Veldin's identical
  // scalar graph. Add the resident source ownership exclusions explicitly.
  for(auto fixed: {0x15f6f0U,0x15fffcU,0x160018U,0x16001cU,0x160020U,0x160028U})
    if((actor_begin<std::uint64_t(fixed)+4U && fixed<actor_end) ||
       (pvar_begin<std::uint64_t(fixed)+4U && fixed<pvar_end) ||
       (model_begin<std::uint64_t(fixed)+4U && fixed<model_end))
      fail("Frontend source pools/model alias allocator globals");
}
} // namespace

RacFrontendObjectClassV1 decode_rac_frontend_object_class_v1(
    std::span<const std::byte> bytes, std::uint32_t reference,
    std::uint8_t class_index, std::uint32_t callback,
    std::uint64_t max_input_bytes) {
  if(!reference || (reference&15U) || std::uint64_t(reference)+bytes.size()>UINT64_C(0x100000000))
    fail("Frontend object model source owner is unaligned or wrapping");
  const auto model=parse_rac_moby_class_v1(bytes,{max_input_bytes,false});
  RacFrontendObjectClassV1 c;
  c.animations=decode_rac_frontend_object_animation_bank_v1(bytes,max_input_bytes);
  c.source_bytes=static_cast<std::uint32_t>(bytes.size());
  c.allocation.class_id_bits=1138;
  c.allocation.class_table_index=class_index;
  c.allocation.callback_reference=callback;
  RacMobyFreshModelV1 fresh;
  fresh.reference=reference;
  fresh.spatial_reference=model.collision_offset ? reference+model.collision_offset : 0U;
  fresh.auxiliary_reference=model.glow_rgba;
  fresh.scale_bits=model.scale_bits;
  fresh.flags=model.mode_bits;
  fresh.byte_06=model.metal_packet_count;
  fresh.byte_0c=model.sequence_count;
  fresh.byte_0e=model.lod_transition;
  fresh.byte_0f=model.shadow_qwords;
  const auto &first=c.animations.clips.front();
  fresh.sequence0=RacMobyFreshSequenceV1{
    reference+first.frame_references.front(),static_cast<std::uint8_t>(first.frame_references.size()),
    first.sound_byte,first.trigger_byte};
  c.allocation.model=fresh;
  for(std::uint32_t i=0;i<c.animations.clips.size();++i) {
    const auto &clip=c.animations.clips[i];
    c.post_headers.push_back({static_cast<std::uint8_t>(i),reference+clip.source_offset,clip.header_bits});
  }
  const auto word=[&](std::uint64_t at) {
    if(at>bytes.size() || bytes.size()-at<4U)
      fail("Frontend joint-selector source word is truncated");
    std::uint32_t value=0;
    for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(bytes[at+i])<<(8*i);
    return value;
  };
  if(!model.joint_metadata_offset || word(model.joint_metadata_offset)!=4U ||
     model.joint_count!=5U)
    fail("Frontend joint-selector directory is outside its four-path owner");
  for(unsigned selector=0;selector<4;++selector) {
    const auto path=word(std::uint64_t(model.joint_metadata_offset)+4U+4U*selector);
    if(path&3U)fail("Frontend joint-selector path is unaligned");
    const auto count=word(path)&0xffffU;
    if(count!=2U || std::uint64_t(path)+4U+count>bytes.size() ||
       bytes[path+4U]!=std::byte{0})
      fail("Frontend joint-selector path is outside the recovered star");
    const auto joint=std::to_integer<std::uint8_t>(bytes[path+4U+count-1U]);
    if(!joint || joint>=model.joint_count)
      fail("Frontend joint-selector tail has no child joint owner");
    c.corner_joints[selector]=joint;
  }
  validate_class(c);
  return c;
}

std::array<RacFrontendObjectV1,14> admit_rac_frontend_objects_v1(
    const RacFrontendObjectClassV1 &c,
    const std::array<std::uint32_t,14> &sequences,
    const std::array<std::uint32_t,3> &camera,
    const RacMobyAllocateBindingsV1 &bindings, game::SessionStateV1 &state,
    std::uint64_t expected_revision, RacMobyAllocateLimitsV1 limits) {
  validate_class(c);validate_source_owners(c,bindings);
  if(state.revision()!=expected_revision) fail("Frontend admission state revision changed");
  auto staged=state;
  std::array<RacFrontendObjectV1,14> objects;
  for(std::size_t i=0;i<objects.size();++i) {
    if(sequences[i]>=c.animations.clips.size()) fail("Frontend initial screen sequence is absent");
    auto allocated=execute_rac_moby_allocate_v1(bindings,staged,staged.revision(),c.allocation,limits);
    if(!allocated.fresh) fail("Original frontend could not admit all fourteen objects");
    auto &o=objects[i];o.admission=std::move(*allocated.fresh);
    o.source_address=bindings.cursor.dynamic_begin_bits+256U*o.admission.physical_slot_index;
    const auto &fresh=o.admission.constructor;
    o.post.model_reference=fresh.model_reference;
    o.post.scale_bits=fresh.scale_bits;
    o.post.flags=fresh.flags;
    o.post.header_cache_key=fresh.header_cache_key;
    o.post.counter_word_bits=fresh.live_index_counter_bits;
    o.post.spatial_reference=fresh.spatial_reference;
    o.post.packed_bounds_bits=fresh.packed_bounds_bits;
    o.post.live_index_bits=fresh.live_index_bits;
    o.animation.speed_bits=fresh.animation_speed_bits;
    o.animation.rate_bits=fresh.animation_rate_bits;
    o.animation.references={fresh.previous_frame_reference-fresh.model_reference,
      fresh.current_frame_reference-fresh.model_reference,fresh.sound_byte,fresh.trigger_byte};
    o.animation.sound_handle=fresh.byte_7d;
    //226770's direct post runs even if caller flags contain bit4.
    update_rac_frontend_object_post_v1(o,c);
    const auto &packed=*bindings.first_packed_word;
    const std::array stores{
      game::SessionStateWriteV1{packed.view_key,packed.element_index+2U*o.admission.physical_slot_index,
                               SessionStateValueTypeV1::u32,0x0e0eU},
      game::SessionStateWriteV1{packed.view_key,packed.element_index+2U*o.admission.physical_slot_index+1U,
                               SessionStateValueTypeV1::u32,0x00202020U}};
    staged.apply_batch(stores,staged.revision());
    o.post.flags=static_cast<std::uint16_t>(o.post.flags&0xfffdU);
    std::copy(camera.begin(),camera.end(),o.post.position_bits.begin());
    const auto &clip=c.animations.clips[sequences[i]];
    o.animation=set_rac_frontend_object_animation_v1(o.animation,c.animations,sequences[i],
      static_cast<std::int32_t>(clip.frame_references.size()-1U)).state;
    synchronize_selection(o);
  }
  state=std::move(staged);
  return objects;
}

void advance_rac_frontend_object_animation_v1(
    RacFrontendObjectV1 &object,const RacFrontendObjectClassV1 &c) {
  validate_class(c);
  object.animation=step_rac_frontend_object_animation_v1(object.animation,c.animations).state;
  synchronize_selection(object);
}
void update_rac_frontend_object_post_v1(
    RacFrontendObjectV1 &object,const RacFrontendObjectClassV1 &c) {
  synchronize_selection(object);
  object.post=execute_rac_moby_zero_rotation_post_v1(object.post,
    {object.source_address,c.post_headers,{}},{255U,1U,7U});
}

std::array<RacMobyPostVectorV1,4> sample_rac_frontend_object_corners_v1(
    const RacFrontendObjectV1 &object,const RacFrontendObjectClassV1 &c,
    const RacRatchetSequenceV1 &sequence,const RacMobyBindRigV1 &bind_rig,
    RacRatchetPoseLimitsV1 limits) {
  validate_class(c);
  const auto &animation=object.animation;
  const auto selected=animation.indices.current_sequence;
  if(!object.source_address || (object.source_address&15U) ||
     std::uint64_t(object.source_address)+256U>UINT64_C(0x100000000) ||
     object.post.model_reference!=c.allocation.model->reference ||
     object.post.scale_bits!=c.allocation.model->scale_bits ||
     selected>=c.animations.clips.size() || animation.indices.previous_sequence!=selected)
    fail("Frontend corner consumer has unresolved source object/sequence ownership");
  const auto &clip=c.animations.clips[selected];
  if(sequence.source_range.offset!=clip.source_offset ||
     sequence.source_range.offset>c.source_bytes ||
     sequence.source_range.size>c.source_bytes-sequence.source_range.offset ||
     sequence.frames.size()!=clip.frame_references.size() ||
     animation.indices.previous_frame>=sequence.frames.size() ||
     animation.indices.current_frame>=sequence.frames.size() ||
     animation.references.previous_frame_reference!=clip.frame_references[animation.indices.previous_frame] ||
     animation.references.current_frame_reference!=clip.frame_references[animation.indices.current_frame])
    fail("Frontend corner sequence/frame bindings disagree with their source owner");
  for(std::size_t i=0;i<sequence.frames.size();++i)
    if(sequence.frames[i].source_offset!=clip.frame_references[i])
      fail("Frontend corner frame source references disagree");
  const auto translations=sample_rac_frontend_menu_joint_translations_v1(sequence,bind_rig,
    animation.indices.previous_frame,animation.indices.current_frame,
    std::bit_cast<float>(animation.phase_bits),limits);
  const auto scale=ee_cop1_mul_bits_v1(object.post.scale_bits,0x3a800000U).bits;
  std::array<RacMobyPostVectorV1,4> corners;
  for(std::size_t i=0;i<corners.size();++i) {
    const auto joint=c.corner_joints[i];
    if(!joint || joint>=translations.size())fail("Frontend corner selector has no source child joint");
    //212168 initializes VF19.w=VF0.w;212204 writes only XYZ. The
    //21222c..238 hierarchy has basis W=0 and translation W=1, so every
    //selected joint+30 qword retains exactly W=1 before20db98.
    RacMobyPostVectorV1 point{0,0,0,0x3f800000U};
    for(std::size_t axis=0;axis<3;++axis)
      point[axis]=dvp_vu_mul_bits_v1(std::bit_cast<std::uint32_t>(translations[joint][axis]),scale).bits;
    point=rac_frontend_transform3_v1(point,object.post.matrix_bits);
    for(std::size_t axis=0;axis<3;++axis)
      point[axis]=dvp_vu_add_bits_v1(point[axis],object.post.position_bits[axis]).bits;
    corners[i]=point;
  }
  return corners;
}

RacFrontendObjectScreenStepV1 step_rac_frontend_object_screen_v1(
    const RacFrontendObjectScreenStateV1 &state,
    std::span<const RacFrontendObjectScreenV1> screens,
    std::array<RacFrontendObjectV1,14> &objects,const RacFrontendObjectClassV1 &c) {
  if(screens.empty() || screens.size()>256U) fail("Frontend screen bank exceeds its bound");
  //21a2fc tests only equality with transition mode1. Screen+3c supplies
  //the other owner modes (actual main46, slot31), not a mode2-only domain.
  RacFrontendObjectScreenStepV1 out{state,{},std::nullopt,false,false};
  if(state.mode==1U) {
    // SLTI signed<1 then MOVN0, otherwise decrement, including source's
    // explicit handling of negative counter words.
    out.state.remaining_updates=std::bit_cast<std::int32_t>(state.remaining_updates)<1
      ? 0U : state.remaining_updates-1U;
    if(out.state.remaining_updates) return out;
    const auto &next=screen_at(screens,state.requested_screen);
    for(std::size_t i=0;i<14;++i)
      if(next.node_references[i] && next.init_callbacks[i])
        fail("Reached frontend node initializer is not integrated");
    out.state.current_screen=state.requested_screen;
    out.state.requested_screen=0;
    out.state.mode=next.active_state;
    out.arrived=true;
    return out;
  }
  if(!state.requested_screen) return out;
  const auto &current=screen_at(screens,state.current_screen);
  const auto &next=screen_at(screens,state.requested_screen);
  for(std::size_t i=0;i<14;++i)
    if(current.node_references[i] && current.cleanup_callbacks[i])
      fail("Reached frontend node cleanup is not integrated");
  // Same-screen requests invert the parent equality test at21a448.
  bool backwards=current.parent_reference==next.source_reference;
  if(current.source_reference==next.source_reference) backwards=!backwards;
  auto updated=objects;
  for(std::size_t i=0;i<14;++i) {
    if(!updated[i].source_address) fail("Frontend transition has an absent admitted object");
    if(next.node_references[i]) out.node_bindings.push_back({next.node_references[i],updated[i].source_address});
    const auto seq=backwards ? current.sequences[i] : next.sequences[i];
    if(seq>=c.animations.clips.size()) fail("Frontend transition sequence is absent");
    const auto frame=backwards ? static_cast<std::int32_t>(c.animations.clips[seq].frame_references.size()-1U) : 0;
    updated[i].animation=set_rac_frontend_object_animation_v1(updated[i].animation,c.animations,seq,frame).state;
    updated[i].animation.speed_bits=backwards ? 0xbf800000U : 0x3f800000U;
    synchronize_selection(updated[i]);
  }
  out.sound_kind=current.source_reference==next.source_reference ? 3U : 4U;
  out.state.mode=1;
  out.state.remaining_updates=12;
  out.state.previous_screen=state.current_screen;
  out.state.current_screen=0;
  out.began_transition=true;
  objects=std::move(updated);
  return out;
}
} // namespace openrc
