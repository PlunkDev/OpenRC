#include "openrc/rac_frontend_scene_compile.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <set>
#include <utility>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RacFrontendSceneCompileError(message);
}

void require_range(std::span<const std::byte> bytes, std::uint64_t at,
                   std::uint64_t count, const char *what) {
  if (at > bytes.size() || count > bytes.size()-at)
    fail(std::string("Frontend ")+what+" leaves its source envelope");
}

std::uint32_t u32(std::span<const std::byte> bytes, std::uint64_t at) {
  require_range(bytes, at, 4U, "word");
  std::uint32_t result = 0U;
  for (unsigned i = 0U; i < 4U; ++i)
    result |= std::to_integer<std::uint32_t>(bytes[static_cast<std::size_t>(at)+i]) << (8U*i);
  return result;
}

struct ClassRow {
  std::uint64_t at = 0U;
  std::array<std::uint8_t, 16U> slots{};
  std::uint8_t used_slots = 0U;
};

// The original uploads the GS images in this order, advancing the relative
// destination by 1024/512 bytes for palette formats or max(w*h,256) for PSMT8.
// For this admitted packed layout source and relative destination coincide;
// verify every row before using that span in the existing palette decoder.
std::span<const std::byte> gs_source(std::span<const std::byte> wad,
                                   const RacFrontendSceneCompileLimitsV1 &limits) {
  const auto count = u32(wad, 8U);
  const auto directory = u32(wad, 12U);
  const auto base = u32(wad, 0U);
  if (count == 0U || count > limits.max_gs_uploads)
    fail("Frontend GS upload count exceeds limits");
  require_range(wad, directory, std::uint64_t{count}*16U, "GS upload directory");
  std::uint64_t transferred = 0U;
  for (std::uint32_t i = 0U; i < count; ++i) {
    const auto row = std::uint64_t{directory}+i*16U;
    const auto format = u32(wad, row);
    const auto dimensions = u32(wad, row+4U);
    const auto width = dimensions & 0xffffU;
    const auto height = dimensions >> 16U;
    if (u32(wad, row+12U) != transferred)
      fail("Frontend GS uploads do not use the admitted contiguous source layout");
    std::uint64_t bytes = 0U;
    if (format == 0U) bytes = 1024U;
    else if (format == 2U) bytes = 512U;
    else if (format == 19U && width != 0U && height != 0U)
      bytes = std::max(UINT64_C(256), std::uint64_t{width}*height);
    else fail("Frontend GS upload uses an unsupported original format");
    if (transferred > limits.textures.max_gs_ram_bytes ||
        bytes > limits.textures.max_gs_ram_bytes-transferred)
      fail("Frontend GS uploads exceed their aggregate byte limit");
    require_range(wad, std::uint64_t{base}+transferred, bytes, "GS image");
    transferred += bytes;
  }
  return wad.subspan(base, static_cast<std::size_t>(transferred));
}

} // namespace

RacFrontendSceneCompileResultV1 compile_rac_frontend_scene_v1(
    const std::span<const std::byte> wad,
    const std::uint32_t source_updates_per_second,
    const RacFrontendSceneCompileLimitsV1 limits) {
  if (wad.size() < 0x88U || wad.size() > limits.max_source_bytes ||
      limits.max_asset_directory_entries == 0U || limits.max_gs_uploads == 0U)
    fail("Frontend scene source or directory limits are invalid");
  std::string stage = "source directory";
  try {
    RacFrontendSceneCompileResultV1 result;
    result.source_background = decode_rac_frontend_background_v1(
        wad, limits.animation.scene, limits.max_decoded_scene_bytes);
    const auto first_scene = parse_scene_animation_bank_v1(
        result.source_background.decoded_chunks.front(), limits.animation.scene);
    if (first_scene.actors.size() > limits.actors.max_models ||
        first_scene.actors.size() > limits.actors.max_rigs ||
        first_scene.actors.size()*result.source_background.decoded_chunks.size() >
            limits.animation.animation.max_clips)
      fail("Frontend scene actor or clip count exceeds neutral limits");

    const auto shared_base = std::uint64_t{u32(wad, 4U)};
    std::set<std::uint64_t> starts{wad.size()};
    std::map<std::uint32_t, ClassRow> classes;
    std::uint64_t directory_entries = 0U;
    // All original class directories bound neighboring Moby assets. Tie and
    // shrub data are not parsed as Moby classes or added as scene actors.
    for (const auto [count_field, table_field, stride] :
         {std::array{0x18U,0x1cU,32U}, std::array{0x20U,0x24U,32U},
          std::array{0x28U,0x2cU,48U}}) {
      const auto count = u32(wad, count_field);
      const auto table = u32(wad, table_field);
      if (count > limits.max_asset_directory_entries-directory_entries)
        fail("Frontend asset directories exceed their aggregate count limit");
      directory_entries += count;
      require_range(wad, table, std::uint64_t{count}*stride, "asset directory");
      for (std::uint32_t index = 0U; index < count; ++index) {
        const auto row = std::uint64_t{table}+std::uint64_t{index}*stride;
        const auto offset = u32(wad, row);
        if (offset == 0U) continue;
        const auto at = shared_base+offset;
        require_range(wad, at, 1U, "class start");
        starts.insert(at);
        if (count_field != 0x18U) continue;
        ClassRow entry;
        entry.at = at;
        bool ended = false;
        for (std::size_t slot = 0U; slot < entry.slots.size(); ++slot) {
          const auto value = std::to_integer<std::uint8_t>(wad[static_cast<std::size_t>(row)+16U+slot]);
          entry.slots[slot] = value;
          if (value == 0xffU) ended = true;
          else if (ended) fail("Frontend class texture map has data after its sentinel");
          else ++entry.used_slots;
        }
        if (!classes.emplace(u32(wad, row+4U), entry).second)
          fail("Frontend class directory repeats a class identity");
      }
    }
    for (const auto field : {0x10U,0x14U,0x60U,0x64U,0x68U,0x78U,0x7cU,0x80U,0x84U}) {
      const auto at = shared_base+u32(wad, field);
      require_range(wad, at, 0U, "shared resource start");
      starts.insert(at);
    }

    const auto texture_count = u32(wad, 0x38U);
    const auto texture_table = u32(wad, 0x3cU);
    require_range(wad, texture_table, std::uint64_t{texture_count}*16U, "Moby texture directory");
    const auto textures = decode_rac_level_moby_texture_bank_v1(
        wad.subspan(texture_table, static_cast<std::size_t>(texture_count)*16U), wad,
        gs_source(wad, limits), shared_base+u32(wad, 0x60U), limits.textures);
    std::vector<ActorLibraryV1> libraries;
    std::vector<RacSceneAnimationActorBindingV1> bindings;
    for (std::uint32_t index = 0U; index < first_scene.actors.size(); ++index) {
      const auto id = first_scene.actors[index].class_id;
      stage = "actor "+std::to_string(index)+" class "+std::to_string(id);
      const auto found = classes.find(id);
      if (found == classes.end()) fail("Frontend background actor has no local class asset");
      const auto &entry = found->second;
      const auto end = starts.upper_bound(entry.at);
      if (end == starts.end()) fail("Frontend class asset has no bounded end");
      const auto bytes = wad.subspan(static_cast<std::size_t>(entry.at),
                                     static_cast<std::size_t>(*end-entry.at));
      const auto source_class = parse_rac_moby_class_v1(bytes, limits.moby_class);
      const auto key = "frontend/background/actor/"+std::to_string(index);
      RacActorLibraryCompileRequestV1 request;
      request.rig_semantic_key = key+"/rig";
      request.model_semantic_key = key+"/model";
      request.bind_pose = compile_rac_moby_complete_bind_pose_geometry_v1(
          bytes, source_class, RacMobyLodV1::high, limits.bind_pose);
      request.texture_slots = entry.slots;
      request.used_texture_slot_count = entry.used_slots;
      request.texture_bank = textures;
      bindings.push_back({index, id, {}, request.rig_semantic_key,
                          request.bind_pose.bind_rig, source_class.scale});
      libraries.push_back(compile_rac_actor_library_v1(request, limits.actors));
      result.actors.push_back({index, id, request.rig_semantic_key, request.model_semantic_key});
    }
    result.actor_library = compose_actor_libraries_v1(libraries, limits.actors);

    std::vector<ActorAnimationBankV1> animation_banks;
    std::uint64_t total_frames = 0U, total_joint_poses = 0U;
    std::uint32_t next_clip = 0U;
    for (std::size_t chunk = 0U; chunk < result.source_background.decoded_chunks.size(); ++chunk) {
      stage = "animation chunk "+std::to_string(chunk);
      const auto &bytes = result.source_background.decoded_chunks[chunk];
      const auto scene = parse_scene_animation_bank_v1(bytes, limits.animation.scene);
      if (scene.actors.size() != bindings.size())
        fail("Frontend background changes actor count between chunks");
      std::vector<std::uint32_t> clip_ids;
      for (auto &binding : bindings) {
        binding.semantic_key = "frontend/background/chunk/"+std::to_string(chunk)+
                               "/actor/"+std::to_string(binding.actor_index);
        clip_ids.push_back(next_clip++);
      }
      auto remaining = limits.animation;
      remaining.animation.max_total_frames -= total_frames;
      remaining.animation.max_total_joint_poses -= total_joint_poses;
      auto bank = compile_rac_scene_animation_bank_v1(bytes, bindings,
          source_updates_per_second, remaining);
      for (const auto &clip : bank.clips) {
        total_frames += clip.frames.size();
        for (const auto &frame : clip.frames) total_joint_poses += frame.joint_poses.size();
      }
      animation_banks.push_back(std::move(bank));
      result.chunk_actor_clip_ids.push_back(std::move(clip_ids));
    }
    result.actor_animation = compose_actor_animation_banks_v1(
        animation_banks, limits.animation.animation);
    return result;
  } catch (const RacFrontendSceneCompileError &) {
    throw;
  } catch (const std::exception &error) {
    fail("Cannot compile original frontend scene "+stage+": "+std::string(error.what()));
  }
}

std::array<std::array<float, 3U>, 5U>
sample_rac_frontend_menu_joint_translations_v1(
    const RacRatchetSequenceV1 &sequence, const RacMobyBindRigV1 &bind_rig,
    const std::uint32_t current_frame, const std::uint32_t next_frame,
    const float phase, const RacRatchetPoseLimitsV1 limits) {
  const bool wrap_endpoint = current_frame+std::uint64_t{1U} == sequence.frames.size() &&
      next_frame == 0U && (phase == 0.0F || phase == 1.0F);
  if (bind_rig.actor_rig.joints.size() != 5U ||
      current_frame >= sequence.frames.size() || next_frame >= sequence.frames.size() ||
      (next_frame != current_frame && next_frame != current_frame+1U && !wrap_endpoint) ||
      (phase != 0.0F && phase != 0.5F && phase != 1.0F))
    fail("Frontend menu joints leave the qualified five-joint adjacent-frame domain");
  for (std::size_t joint = 0U; joint < 5U; ++joint)
    if (bind_rig.actor_rig.joints[joint].parent_index != (joint == 0U ? -1 : 0))
      fail("Frontend menu joint hierarchy is not the qualified star");
  const auto u16 = [&](const std::uint64_t at) {
    require_range(sequence.encoded_bytes, at, 2U, "menu joint halfword");
    return static_cast<std::uint16_t>(
        std::to_integer<unsigned>(sequence.encoded_bytes[at]) |
        (std::to_integer<unsigned>(sequence.encoded_bytes[at+1U]) << 8U));
  };
  const auto translations = [&](const std::uint32_t frame_index) {
    // Reuse regular-frame partition/tag/duplicate validation, in source units.
    // Its host matrix output is deliberately not used as a numeric oracle.
    (void)decode_rac_ratchet_regular_pose_v1(sequence, frame_index, bind_rig,
                                            1024.0F, limits);
    const auto &frame = sequence.frames[frame_index];
    if (frame.primary_payload_range.size != 40U || frame.supplemental_a_count > 1U ||
        frame.supplemental_b_count != 5U)
      fail("Frontend menu frame leaves the qualified quaternion/translation domain");
    for (std::size_t joint = 0U; joint < 5U; ++joint) {
      const auto at = frame.primary_payload_range.offset+joint*8U;
      const std::array<std::uint16_t, 4U> expected = joint == 0U ?
          std::array<std::uint16_t,4U>{0U,0U,0x8000U,0U} :
          std::array<std::uint16_t,4U>{0U,0U,0U,0x7fffU};
      for (std::size_t axis = 0U; axis < 4U; ++axis)
        if (u16(at+axis*2U) != expected[axis])
          fail("Frontend menu rotation requires an unqualified quaternion path");
    }
    if (frame.supplemental_a_count != 0U &&
        u16(frame.supplemental_a_payload_range.offset+6U) != 0U)
      fail("Frontend menu local/child scale requires an unqualified hierarchy path");
    std::array<std::array<std::int32_t,3U>,5U> result{};
    for (std::size_t record = 0U; record < 5U; ++record) {
      const auto at = frame.supplemental_b_payload_range.offset+record*8U;
      const auto joint = u16(at+6U); // Bounds/uniqueness proved by regular decoder.
      for (std::size_t axis = 0U; axis < 3U; ++axis)
        result[joint][axis] = std::bit_cast<std::int16_t>(u16(at+axis*2U));
    }
    return result;
  };
  const auto left = translations(current_frame), right = translations(next_frame);
  const std::int32_t numerator = phase == 0.0F ? 0 : phase == 0.5F ? 1 : 2;
  std::array<std::array<std::int32_t,3U>,5U> half_units{};
  for (std::size_t joint = 0U; joint < 5U; ++joint)
    for (std::size_t axis = 0U; axis < 3U; ++axis)
      half_units[joint][axis] = (2-numerator)*left[joint][axis]+numerator*right[joint][axis];
  std::array<std::array<float,3U>,5U> result{};
  // 211be8/211bec blend exact signed i16/half-integers. Adjacent same-sequence
  // branch211c0c skips normalization. Root quaternion produces diag(-1,-1,1),
  // so the actual hierarchy VMULA/VMADDA/VMADD chain21222c..238 contains only
  // zero and unit products. Every partial sum is a half-integer of magnitude
  // <=65536: no discarded significand/ACC bits and no rounded cancellation.
  // Scale tag0 is skipped at211df8/211b28, independently of its magnitudes.
  // Original stopped endpoints retain last->0 indices. At phase0 the source
  // bypasses interpolation; at phase1 its non-adjacent quaternion branch does
  // normalize. Root squared length is exactly one, with VRSQRT(1,1)=1. Each
  // child has only w nonzero: its normalized w (however rounded) is multiplied
  // only by zero xyz and its matrix remains exactly identity. Thus this
  // translation result does not substitute an approximate quaternion oracle.
  for (std::size_t joint = 0U; joint < 5U; ++joint)
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
      const auto value = joint == 0U ? half_units[0U][axis] :
          half_units[0U][axis]+(axis == 2U ? 1 : -1)*half_units[joint][axis];
      result[joint][axis] = static_cast<float>(value)*0.5F;
    }
  return result;
}

SceneTimelineV1 compile_rac_frontend_timeline_v1(
    const RacFrontendSceneCompileResultV1 &scene,
    const SceneCameraV1 &camera, const std::uint32_t viewport_width,
    const std::uint32_t viewport_height, const RacFrontendTimelineCompileLimitsV1 limits) {
  const auto count = scene.actors.size();
  const auto duration = scene.source_background.source_duration;
  if (count == 0U || count > limits.timeline.max_actors || duration == 0U ||
      duration > limits.timeline.max_samples || duration > limits.timeline.max_actor_samples/count ||
      scene.source_background.decoded_chunks.empty() ||
      scene.chunk_actor_clip_ids.size() != scene.source_background.decoded_chunks.size() ||
      scene.actor_animation.clips.empty() || viewport_width == 0U || viewport_height == 0U ||
      viewport_width > 65535U || viewport_height > 65535U)
    fail("Frontend timeline inputs exceed their source/output domain");
  const auto byte_count = 160U+std::uint64_t{count}*56U+
      std::uint64_t{duration}*(64U+std::uint64_t{count}*56U);
  if (byte_count > limits.timeline.max_bytes)
    fail("Frontend timeline exceeds its encoded byte limit");
  validate_scene_camera_v1(camera, limits.timeline.max_absolute_component);
  validate_actor_library_v1(scene.actor_library, limits.actors.library);
  validate_actor_animation_bank_v1(scene.actor_animation, limits.animation.bank);
  SceneTimelineV1 result;
  result.actor_library_sha256 = prepared_content_sha256_v1(
      encode_actor_library_v1(scene.actor_library, limits.actors));
  result.actor_animation_sha256 = prepared_content_sha256_v1(
      encode_actor_animation_bank_v1(scene.actor_animation, limits.animation));
  result.updates_per_second = scene.actor_animation.clips.front().source_updates_per_second;
  result.display_aspect_numerator = viewport_width;
  result.display_aspect_denominator = viewport_height;
  result.loop = true;
  for (const auto &clip : scene.actor_animation.clips)
    if (clip.source_updates_per_second != result.updates_per_second)
      fail("Frontend animation clips disagree on source update cadence");
  for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
    const auto &actor = scene.actors[ordinal];
    if (actor.source_actor_index != ordinal)
      fail("Frontend timeline actor ordinal identity changed");
    const auto model = std::find_if(scene.actor_library.models.begin(), scene.actor_library.models.end(),
        [&](const auto &value) { return value.semantic_key == actor.model_key; });
    const auto rig = std::find_if(scene.actor_library.rigs.begin(), scene.actor_library.rigs.end(),
        [&](const auto &value) { return value.semantic_key == actor.rig_key; });
    if (model == scene.actor_library.models.end() || rig == scene.actor_library.rigs.end())
      fail("Frontend timeline actor has no matching neutral model/rig");
    result.actors.push_back({model->id, rig->id, {}});
  }
  std::array<std::byte,32U> source_camera{};
  for (std::size_t chunk = 0U; chunk < scene.source_background.decoded_chunks.size(); ++chunk) {
    const auto &bytes = scene.source_background.decoded_chunks[chunk];
    const auto decoded = parse_scene_animation_bank_v1(bytes, limits.scene);
    if (decoded.actors.size() != count || scene.chunk_actor_clip_ids[chunk].size() != count ||
        decoded.scene_record_index != chunk || decoded.scene_record_count != (duration+95U)/96U ||
        (decoded.unknown_word_0&0xffffU) != duration)
      fail("Frontend timeline chunk identity or duration changed");
    for (std::size_t actor = 0U; actor < count; ++actor)
      if (decoded.actors[actor].class_id != scene.actors[actor].source_class_id)
        fail("Frontend timeline source actor changed class between chunks");
    const auto camera_at = static_cast<std::size_t>(decoded.camera_track_range.offset);
    if (chunk == 0U) std::copy_n(bytes.begin()+camera_at, 32U, source_camera.begin());
    for (std::size_t frame = 0U; frame < decoded.camera_record_count; ++frame)
      if (!std::equal(source_camera.begin(), source_camera.end(), bytes.begin()+camera_at+frame*32U))
        fail("Frontend timeline source camera is not constant");
  }
  const auto first = sample_rac_frontend_background_tick_v1(
      scene.source_background.decoded_chunks.front(), 0U, limits.scene);
  // 1f3140's ordered matrix DIV followed by neutral inverse adaptation is
  // one ULP above the input tangent. Bind both original and effective bits.
  if (camera.position != first.camera.position ||
      std::bit_cast<std::uint32_t>(first.camera.projection_parameter) != 0x3f2147aeU ||
      std::bit_cast<std::uint32_t>(camera.tangent_half_horizontal) != 0x3f2147afU ||
      std::bit_cast<std::uint32_t>(camera.tangent_half_vertical) != 0x3ef3daf9U)
    fail("Frontend timeline camera disagrees with its original owner input");
  RacFrontendBackgroundClockV1 clock(duration);
  result.samples.reserve(duration);
  for (std::uint32_t update = 0U; update < duration; ++update) {
    const auto position = clock.step();
    if (position.chunk_index >= scene.source_background.decoded_chunks.size())
      fail("Frontend timeline clock leaves its source chunk directory");
    const auto tick = sample_rac_frontend_background_tick_v1(
        scene.source_background.decoded_chunks[position.chunk_index], position.chunk_update, limits.scene);
    SceneTimelineSampleV1 sample;
    sample.camera = camera;
    for (std::size_t actor = 0U; actor < count; ++actor) {
      const auto clip_id = scene.chunk_actor_clip_ids[position.chunk_index][actor];
      if (clip_id >= scene.actor_animation.clips.size())
        fail("Frontend timeline clip binding leaves its neutral animation bank");
      const auto state = rac_scene_actor_playback_state_v1(scene.actor_animation.clips[clip_id], tick);
      SceneTimelineActorSampleV1 output;
      output.clip_index = clip_id;
      output.frame_index = state.frame_index;
      output.phase = static_cast<float>(state.phase);
      output.transform.position = tick.actor_world_positions[actor];
      sample.actors.push_back(output);
    }
    result.samples.push_back(std::move(sample));
  }
  validate_scene_timeline_v1(result, limits.timeline);
  validate_scene_timeline_bindings_v1(result, scene.actor_library, scene.actor_animation);
  return result;
}

} // namespace openrc
