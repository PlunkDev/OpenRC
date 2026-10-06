#include "openrc/rac_scene_animation_compile.hpp"

#include "openrc/actor_library.hpp"
#include "openrc/dvp_vu_numeric.hpp"
#include "openrc/wad.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RacSceneAnimationCompileError(message);
}

PreparedContentDigestV1 canonical_rig_digest(const RacMobyBindRigV1 &source) {
  auto rig = source.actor_rig;
  for (auto &joint : rig.joints) {
    for (auto &value : joint.local_bind_transform.values) {
      if (value == 0.0F) value = 0.0F;
    }
    for (auto &value : joint.inverse_bind_transform.values) {
      if (value == 0.0F) value = 0.0F;
    }
  }
  return actor_rig_content_sha256_v1(rig);
}

std::uint32_t read_u32(std::span<const std::byte> source, std::size_t at) {
  std::uint32_t result = 0U;
  for (unsigned i = 0U; i < 4U; ++i)
    result |= std::to_integer<std::uint32_t>(source[at+i]) << (8U*i);
  return result;
}

float read_float(std::span<const std::byte> source, std::size_t at) {
  const auto value = std::bit_cast<float>(read_u32(source, at));
  if (!std::isfinite(value)) fail("Scene sample contains a non-finite component");
  return value == 0.0F ? 0.0F : value;
}

RacSceneAnimationTickV1 sample_scene_tick(
    const std::span<const std::byte> decoded_scene,
    const std::uint32_t source_update,
    const SceneAnimationBankLimitsV1 limits,
    const bool frontend_background) {
  SceneAnimationBankV1 scene;
  try { scene = parse_scene_animation_bank_v1(decoded_scene, limits); }
  catch (const SceneAnimationBankError &error) {
    fail("Cannot parse sampled scene animation: " + std::string(error.what()));
  }
  const auto current = source_update / 2U;
  if (source_update >= scene.camera_record_count ||
      current + 1U >= scene.frame_count) {
    fail("Scene update leaves the camera or actor interpolation samples");
  }
  RacSceneAnimationTickV1 result;
  result.source_update = source_update;
  result.current_frame = current;
  result.next_frame = current + 1U;
  const auto camera = static_cast<std::size_t>(scene.camera_track_range.offset) +
                      static_cast<std::size_t>(source_update)*0x20U;
  result.camera.select_next_actor_frame_on_odd_update =
      !frontend_background && decoded_scene[camera+12U] != std::byte{0U};
  result.camera.projection_parameter = frontend_background ?
      std::bit_cast<float>(UINT32_C(0x3f2147ae)) :
      read_float(decoded_scene, camera+28U);
  for (std::size_t axis = 0U; axis < 3U; ++axis) {
    result.camera.position[axis] = read_float(decoded_scene, camera+axis*4U);
    result.camera.rotation_xyz_radians[axis] =
        read_float(decoded_scene, camera+16U+axis*4U);
  }
  // Source BNEL at 29a670 executes its phase=1 delay slot only on odd
  // updates. On an even update that branch-likely slot is annulled.
  result.interpolation = (source_update & 1U) != 0U ?
      (result.camera.select_next_actor_frame_on_odd_update ? 1.0F : 0.5F) : 0.0F;
  const auto left_weight = std::bit_cast<std::uint32_t>(1.0F-result.interpolation);
  const auto right_weight = std::bit_cast<std::uint32_t>(result.interpolation);
  result.actor_world_positions.reserve(scene.actors.size());
  for (const auto &actor : scene.actors) {
    const auto roots = static_cast<std::size_t>(actor.root_transform_offset) +
                       static_cast<std::size_t>(current)*0x10U;
    std::array<float, 3U> position{};
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
      (void)read_float(decoded_scene, roots+axis*4U);
      (void)read_float(decoded_scene, roots+16U+axis*4U);
      const auto left = read_u32(decoded_scene, roots+axis*4U);
      const auto right = read_u32(decoded_scene, roots+16U+axis*4U);
      const auto left_product = dvp_vu_mul_bits_v1(left, left_weight).bits;
      const auto right_product = dvp_vu_mul_bits_v1(right, right_weight).bits;
      const auto value = std::bit_cast<float>(
          dvp_vu_add_bits_v1(left_product, right_product).bits);
      if (!std::isfinite(value)) fail("Scene root interpolation exceeds finite floats");
      position[axis] = value == 0.0F ? 0.0F : value;
    }
    result.actor_world_positions.push_back(position);
  }
  return result;
}

} // namespace

RacSceneAnimationTickV1 sample_rac_scene_animation_tick_v1(
    const std::span<const std::byte> decoded_scene,
    const std::uint32_t source_update,
    const SceneAnimationBankLimitsV1 limits) {
  return sample_scene_tick(decoded_scene, source_update, limits, false);
}

RacSceneAnimationTickV1 sample_rac_frontend_background_tick_v1(
    const std::span<const std::byte> decoded_scene,
    const std::uint32_t source_update,
    const SceneAnimationBankLimitsV1 limits) {
  return sample_scene_tick(decoded_scene, source_update, limits, true);
}

RacFrontendBackgroundClockV1::RacFrontendBackgroundClockV1(
    const std::uint32_t source_duration) : duration_(source_duration) {
  if (duration_ == 0U || duration_ > 70U * 96U)
    fail("Frontend background duration exceeds its source chunk directory");
}

RacFrontendBackgroundPositionV1 RacFrontendBackgroundClockV1::step() {
  ++position_.total_update;
  ++position_.chunk_update;
  position_.reload_chunk = false;
  position_.looped = false;
  if (position_.total_update >= duration_) {
    position_.total_update = 0U;
    position_.chunk_index = 0U;
    position_.chunk_update = 0U;
    position_.reload_chunk = true;
    position_.looped = true;
  } else if (position_.chunk_update >= 96U) {
    ++position_.chunk_index;
    position_.chunk_update = 0U;
    position_.reload_chunk = true;
  }
  return position_;
}

RacFrontendBackgroundChunksV1 decode_rac_frontend_background_v1(
    const std::span<const std::byte> decoded_frontend_wad,
    const SceneAnimationBankLimitsV1 scene_limits,
    const std::uint64_t max_total_decoded_bytes) {
  if (decoded_frontend_wad.size() < 0x84U || max_total_decoded_bytes == 0U)
    fail("Frontend background source envelope or output limit is invalid");
  const auto bank = std::uint64_t{read_u32(decoded_frontend_wad, 4U)} +
                    read_u32(decoded_frontend_wad, 0x80U);
  if (bank > decoded_frontend_wad.size() ||
      decoded_frontend_wad.size() - bank < 0x800U)
    fail("Frontend background directory leaves its source envelope");
  RacFrontendBackgroundChunksV1 result;
  std::uint64_t total_bytes = 0U;
  try {
    for (std::uint32_t index = 0U; index < 70U; ++index) {
      const auto row = static_cast<std::size_t>(bank) + index * 8U;
      const auto bytes = read_u32(decoded_frontend_wad, row + 4U);
      if (bytes == 0U) break;
      const auto at = bank + 0x800U + read_u32(decoded_frontend_wad, row);
      if (at > decoded_frontend_wad.size() ||
          bytes > decoded_frontend_wad.size() - at || bytes < 16U)
        fail("Frontend background chunk leaves its source envelope");
      const auto envelope = decoded_frontend_wad.subspan(
          static_cast<std::size_t>(at), bytes);
      const auto logical_bytes = read_u32(envelope, 3U);
      if (logical_bytes < 16U || logical_bytes > envelope.size())
        fail("Frontend background chunk has an invalid WAD extent");
      auto decoded = decode_wad_bytes(envelope.first(logical_bytes),
          std::min(scene_limits.max_input_bytes, max_total_decoded_bytes-total_bytes));
      if (decoded.bytes.size() > max_total_decoded_bytes-total_bytes)
        fail("Frontend background chunks exceed aggregate output limit");
      total_bytes += decoded.bytes.size();
      const auto scene = parse_scene_animation_bank_v1(decoded.bytes, scene_limits);
      const auto duration = scene.unknown_word_0 & 0xffffU;
      if (index == 0U) {
        (void)RacFrontendBackgroundClockV1(duration);
        result.source_duration = duration;
      }
      if (duration != result.source_duration || scene.scene_record_index != index ||
          scene.scene_record_count != (duration + 95U)/96U ||
          index >= scene.scene_record_count)
        fail("Frontend background chunk metadata disagrees with its source clock");
      const auto updates = std::min(96U, duration-index*96U);
      if (scene.camera_record_count < updates ||
          (updates-1U)/2U + 1U >= scene.frame_count)
        fail("Frontend background chunk lacks its final admitted samples");
      result.decoded_chunks.push_back(std::move(decoded.bytes));
    }
    if (result.decoded_chunks.empty() ||
        result.decoded_chunks.size() != (result.source_duration+95U)/96U)
      fail("Frontend background directory does not cover its complete source loop");
    return result;
  } catch (const WadError &error) {
    fail("Cannot decode frontend background chunk: " + std::string(error.what()));
  } catch (const SceneAnimationBankError &error) {
    fail("Cannot parse frontend background chunk: " + std::string(error.what()));
  }
}

ActorAnimationPlaybackStateV1 rac_scene_actor_playback_state_v1(
    const ActorAnimationClipV1 &clip, const RacSceneAnimationTickV1 &tick) {
  if (tick.current_frame >= clip.frames.size() ||
      tick.next_frame != tick.current_frame + 1U ||
      tick.next_frame >= clip.frames.size() ||
      (tick.interpolation != 0.0F && tick.interpolation != 0.5F &&
       tick.interpolation != 1.0F)) {
    fail("Scene actor selection is invalid for its compiled clip");
  }
  auto state = start_actor_animation_playback_v1(clip);
  state.frame_index = tick.interpolation == 1.0F ?
      tick.next_frame : tick.current_frame;
  state.phase = tick.interpolation == 1.0F ? 0.0 : tick.interpolation;
  return state;
}

ActorAnimationBankV1 compile_rac_scene_animation_bank_v1(
    const std::span<const std::byte> decoded_scene,
    const std::span<const RacSceneAnimationActorBindingV1> bindings,
    const std::uint32_t source_updates_per_second,
    const RacSceneAnimationCompileLimitsV1 limits) {
  if (bindings.empty() || bindings.size() > limits.animation.max_clips ||
      source_updates_per_second == 0U ||
      source_updates_per_second >
          limits.animation.max_source_updates_per_second) {
    fail("The scene animation bindings or source cadence exceed limits");
  }
  try {
    const auto scene = parse_scene_animation_bank_v1(decoded_scene, limits.scene);
    std::vector<bool> seen(scene.actors.size(), false);
    ActorAnimationBankV1 result;
    result.clips.reserve(bindings.size());
    std::uint64_t total_frames = 0U;
    std::uint64_t total_joint_poses = 0U;
    for (const auto &binding : bindings) {
      if (binding.actor_index >= scene.actors.size() ||
          seen[binding.actor_index] || binding.semantic_key.empty() ||
          binding.rig_key.empty() || !std::isfinite(binding.class_scale) ||
          binding.class_scale <= 0.0F) {
        fail("A scene animation actor binding is invalid or repeated");
      }
      seen[binding.actor_index] = true;
      const auto &actor = scene.actors[binding.actor_index];
      if (actor.class_id != binding.expected_class_id) {
        fail("A scene animation binding selects the wrong source class");
      }
      const auto joint_count = binding.bind_rig.actor_rig.joints.size();
      const auto frame_poses = static_cast<std::uint64_t>(actor.frame_count) *
                               joint_count;
      if (joint_count == 0U ||
          joint_count > limits.animation.max_joints_per_frame ||
          actor.frame_count > limits.animation.max_frames_per_clip ||
          actor.frame_count > limits.animation.max_total_frames - total_frames ||
          frame_poses > limits.animation.max_total_joint_poses - total_joint_poses) {
        fail("Scene animation frames or joint poses exceed aggregate limits");
      }
      total_frames += actor.frame_count;
      total_joint_poses += frame_poses;
      const auto sequence_begin = actor.sequence_header_range.offset;
      const auto sequence = parse_rac_scene_sequence_v1(
          decoded_scene,
          {sequence_begin, actor.root_transform_offset - sequence_begin},
          limits.sequence);
      ActorAnimationClipV1 clip;
      clip.id = static_cast<std::uint32_t>(result.clips.size());
      clip.semantic_key = binding.semantic_key;
      clip.rig_key = binding.rig_key;
      clip.rig_content_sha256 = canonical_rig_digest(binding.bind_rig);
      clip.source_updates_per_second = source_updates_per_second;
      clip.wrap_mode = ActorAnimationWrapModeV1::clamp;
      clip.frames.reserve(sequence.frames.size());
      for (std::size_t frame_index = 0U;
           frame_index < sequence.frames.size(); ++frame_index) {
        const auto pose = decode_rac_ratchet_regular_pose_v1(
            sequence, frame_index, binding.bind_rig,
            binding.class_scale, limits.pose);
        ActorAnimationFrameV1 frame;
        frame.phase_rate = sequence.frames[frame_index].phase_rate;
        frame.joint_poses.reserve(pose.source_joint_poses.size());
        for (const auto &joint : pose.source_joint_poses) {
          frame.joint_poses.push_back({joint.normalized_rotation_xyzw,
              joint.translation, joint.local_scale, joint.terminal_scale});
        }
        clip.frames.push_back(std::move(frame));
      }
      result.clips.push_back(std::move(clip));
    }
    return canonicalize_actor_animation_bank_v1(std::move(result), limits.animation);
  } catch (const SceneAnimationBankError &error) {
    fail("Cannot parse scene animation bank: " + std::string(error.what()));
  } catch (const RacRatchetSequenceError &error) {
    fail("Cannot parse scene actor sequence: " + std::string(error.what()));
  } catch (const RacRatchetPoseError &error) {
    fail("Cannot decode scene actor pose: " + std::string(error.what()));
  } catch (const ActorLibraryError &error) {
    fail("Cannot pin scene actor rig: " + std::string(error.what()));
  } catch (const ActorAnimationError &error) {
    fail("Cannot canonicalize scene actor animation: " + std::string(error.what()));
  }
}

} // namespace openrc
