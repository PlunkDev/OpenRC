#include "openrc/rac_moby_animation_compile.hpp"

#include "openrc/actor_library.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RacMobyAnimationCompileError(message);
}

[[nodiscard]] bool known_wrap_mode(
    const ActorAnimationWrapModeV1 mode) noexcept {
  return mode == ActorAnimationWrapModeV1::clamp ||
         mode == ActorAnimationWrapModeV1::loop;
}

[[nodiscard]] std::string source_sequence_key(
    const std::string_view prefix, const std::uint32_t slot) {
  std::string result(prefix);
  result.push_back(static_cast<char>('0' + (slot / 100U) % 10U));
  result.push_back(static_cast<char>('0' + (slot / 10U) % 10U));
  result.push_back(static_cast<char>('0' + slot % 10U));
  return result;
}

void validate_source_index(const std::span<const std::byte> class_bytes,
                           const RacMobyClassV1 &source_class) {
  if (class_bytes.empty() || source_class.input_bytes != class_bytes.size() ||
      source_class.sequence_count != source_class.sequence_offsets.size() ||
      source_class.sequence_offsets.empty()) {
    fail("The RAC1 Moby animation source and class index disagree");
  }

  std::uint32_t previous_offset = 0U;
  std::uint32_t occupied_count = 0U;
  for (const auto offset : source_class.sequence_offsets) {
    if (offset == 0U) {
      continue;
    }
    if ((offset & (kRacMobyDataAlignmentV1 - 1U)) != 0U ||
        offset > class_bytes.size() ||
        kRacRatchetSequenceHeaderBytesV1 > class_bytes.size() - offset ||
        (previous_offset != 0U && offset <= previous_offset)) {
      fail("The RAC1 Moby sequence index is not canonical and bounded");
    }
    previous_offset = offset;
    ++occupied_count;
  }
  if (occupied_count == 0U) {
    fail("The RAC1 Moby class has no occupied animation sequence slots");
  }
}

void consider_boundary(const std::uint64_t begin,
                       const std::uint64_t candidate,
                       const std::uint64_t source_size,
                       std::uint64_t &end) {
  if (candidate > begin && candidate <= source_size) {
    end = std::min(end, candidate);
  }
}

[[nodiscard]] RacRatchetSequenceRangeV1
resolve_sequence_range(const RacMobyClassV1 &source_class,
                       const std::uint32_t source_slot) {
  if (source_slot >= source_class.sequence_offsets.size()) {
    fail("A RAC1 Moby animation profile has an out-of-range source slot");
  }
  const std::uint64_t begin = source_class.sequence_offsets[source_slot];
  if (begin == 0U) {
    fail("A RAC1 Moby animation profile selects an empty source slot");
  }

  std::uint64_t end = source_class.input_bytes;
  for (const auto offset : source_class.sequence_offsets) {
    consider_boundary(begin, offset, source_class.input_bytes, end);
  }

  const std::array<std::uint64_t, 8U> structural_offsets{
      source_class.packet_table_offset,
      source_class.collision_offset,
      source_class.skeleton_offset,
      source_class.common_translation_offset,
      source_class.joint_metadata_offset,
      source_class.gif_usage_offset,
      source_class.sound_definitions_offset,
      source_class.shadow_range.offset};
  for (const auto offset : structural_offsets) {
    consider_boundary(begin, offset, source_class.input_bytes, end);
  }
  if (source_class.bangles_offset_qwords != 0U) {
    consider_boundary(
        begin,
        static_cast<std::uint64_t>(source_class.bangles_offset_qwords) *
            kRacMobyDataAlignmentV1,
        source_class.input_bytes, end);
  }

  if (end <= begin) {
    fail("A RAC1 Moby animation slot has no bounded source sequence");
  }
  return {begin, end - begin};
}

[[nodiscard]] ActorJointPoseV1
neutral_joint_pose(const RacRatchetJointPoseV1 &source) {
  return ActorJointPoseV1{source.normalized_rotation_xyzw, source.translation,
                          source.local_scale, source.terminal_scale};
}

[[nodiscard]] PreparedContentDigestV1
canonical_rig_digest(const RacMobyBindRigV1 &bind_rig) {
  auto rig = bind_rig.actor_rig;
  for (auto &joint : rig.joints) {
    for (auto &component : joint.local_bind_transform.values) {
      if (component == 0.0F) {
        component = 0.0F;
      }
    }
    for (auto &component : joint.inverse_bind_transform.values) {
      if (component == 0.0F) {
        component = 0.0F;
      }
    }
  }
  try {
    return actor_rig_content_sha256_v1(rig);
  } catch (const ActorLibraryError &error) {
    fail("Cannot address the RAC1 Moby animation rig: " +
         std::string(error.what()));
  }
}

} // namespace

std::vector<RacMobyAnimationClipProfileV1>
make_rac_moby_complete_animation_profiles_v1(
    const RacMobyClassV1 &source_class,
    const std::span<const RacMobyAnimationClipProfileV1> confirmed_profiles,
    const std::string_view source_sequence_key_prefix,
    const ActorAnimationWrapModeV1 unclassified_wrap_mode) {
  if (source_class.sequence_count != source_class.sequence_offsets.size() ||
      source_class.sequence_offsets.empty() ||
      source_sequence_key_prefix.empty() ||
      !known_wrap_mode(unclassified_wrap_mode)) {
    fail("The complete RAC1 Moby animation profile policy is invalid");
  }

  std::vector<bool> mapped_slots(source_class.sequence_offsets.size(), false);
  std::vector<RacMobyAnimationClipProfileV1> result;
  result.reserve(source_class.sequence_offsets.size());
  for (std::size_t index = 0U; index < confirmed_profiles.size(); ++index) {
    const auto &profile = confirmed_profiles[index];
    if (profile.clip_id != index ||
        profile.source_slot >= source_class.sequence_offsets.size() ||
        source_class.sequence_offsets[profile.source_slot] == 0U ||
        profile.semantic_key.empty() || !known_wrap_mode(profile.wrap_mode) ||
        mapped_slots[profile.source_slot]) {
      fail("A confirmed RAC1 Moby animation profile is invalid");
    }
    if (std::any_of(result.begin(), result.end(),
                    [&profile](const auto &previous) {
                      return previous.semantic_key == profile.semantic_key;
                    })) {
      fail("Confirmed RAC1 Moby animation profiles repeat a semantic key");
    }
    mapped_slots[profile.source_slot] = true;
    result.push_back(profile);
  }

  std::size_t occupied_count = 0U;
  for (std::uint32_t slot = 0U;
       slot < source_class.sequence_offsets.size(); ++slot) {
    if (source_class.sequence_offsets[slot] == 0U) {
      continue;
    }
    ++occupied_count;
    if (mapped_slots[slot]) {
      continue;
    }
    if (result.size() >= std::numeric_limits<std::uint32_t>::max()) {
      fail("The complete RAC1 Moby animation profile exceeds clip IDs");
    }
    auto key = source_sequence_key(source_sequence_key_prefix, slot);
    if (std::any_of(result.begin(), result.end(),
                    [&key](const auto &previous) {
                      return previous.semantic_key == key;
                    })) {
      fail("A source-addressed RAC1 Moby animation key is not unique");
    }
    result.push_back(RacMobyAnimationClipProfileV1{
        static_cast<std::uint32_t>(result.size()), slot, std::move(key),
        unclassified_wrap_mode});
  }

  if (result.empty() || result.size() != occupied_count) {
    fail("The complete RAC1 Moby animation profile does not cover every "
         "occupied source sequence exactly once");
  }
  return result;
}

ActorAnimationBankV1 compile_rac_moby_animation_bank_v1(
    const std::span<const std::byte> class_bytes,
    const RacMobyClassV1 &source_class, const RacMobyBindRigV1 &bind_rig,
    std::string rig_key,
    const std::span<const RacMobyAnimationClipProfileV1> profiles,
    const std::uint32_t source_updates_per_second,
    const RacMobyAnimationCompileLimitsV1 limits) {
  validate_source_index(class_bytes, source_class);
  if (profiles.empty() || profiles.size() > limits.animation.max_clips ||
      profiles.size() > std::numeric_limits<std::uint32_t>::max()) {
    fail("The RAC1 Moby animation profile count is invalid or limited");
  }
  if (rig_key.empty() || !std::isfinite(source_class.scale) ||
      source_class.scale <= 0.0F || source_updates_per_second == 0U ||
      bind_rig.actor_rig.joints.size() != source_class.joint_count ||
      bind_rig.source_common_translations.size() !=
          bind_rig.actor_rig.joints.size()) {
    fail("The RAC1 Moby animation compile policy or rig is incomplete");
  }

  ActorAnimationBankV1 result;
  const auto rig_content_sha256 = canonical_rig_digest(bind_rig);
  result.clips.reserve(profiles.size());
  for (std::size_t profile_index = 0U; profile_index < profiles.size();
       ++profile_index) {
    const auto &profile = profiles[profile_index];
    if (profile.clip_id != profile_index || profile.semantic_key.empty() ||
        !known_wrap_mode(profile.wrap_mode)) {
      fail("A RAC1 Moby animation profile is non-canonical");
    }
    for (std::size_t previous = 0U; previous < profile_index; ++previous) {
      if (profiles[previous].source_slot == profile.source_slot ||
          profiles[previous].semantic_key == profile.semantic_key) {
        fail("RAC1 Moby animation profiles repeat a slot or semantic key");
      }
    }

    RacRatchetSequenceV1 sequence;
    try {
      sequence = parse_rac_moby_sequence_v1(
          class_bytes, resolve_sequence_range(source_class, profile.source_slot),
          limits.sequence);
    } catch (const RacRatchetSequenceError &error) {
      fail("Cannot parse a mapped RAC1 Moby animation: " +
           std::string(error.what()));
    }

    ActorAnimationClipV1 clip;
    clip.id = profile.clip_id;
    clip.semantic_key = profile.semantic_key;
    clip.rig_key = rig_key;
    clip.rig_content_sha256 = rig_content_sha256;
    clip.source_updates_per_second = source_updates_per_second;
    clip.wrap_mode = profile.wrap_mode;
    clip.frames.reserve(sequence.frames.size());
    for (std::size_t frame_index = 0U; frame_index < sequence.frames.size();
         ++frame_index) {
      RacRatchetPoseV1 source_pose;
      try {
        source_pose = decode_rac_ratchet_regular_pose_v1(
            sequence, frame_index, bind_rig, source_class.scale, limits.pose);
      } catch (const RacRatchetPoseError &error) {
        fail("Cannot decode a mapped RAC1 Moby animation pose: " +
             std::string(error.what()));
      }

      ActorAnimationFrameV1 frame;
      frame.phase_rate = sequence.sequence_phase_rate_override != 0.0F
                             ? sequence.sequence_phase_rate_override
                             : sequence.frames[frame_index].phase_rate;
      frame.joint_poses.reserve(source_pose.source_joint_poses.size());
      std::transform(source_pose.source_joint_poses.begin(),
                     source_pose.source_joint_poses.end(),
                     std::back_inserter(frame.joint_poses),
                     neutral_joint_pose);
      clip.frames.push_back(std::move(frame));
    }
    result.clips.push_back(std::move(clip));
  }

  try {
    return canonicalize_actor_animation_bank_v1(std::move(result),
                                                 limits.animation);
  } catch (const ActorAnimationError &error) {
    fail("Cannot canonicalize compiled RAC1 Moby animations: " +
         std::string(error.what()));
  }
}

} // namespace openrc
