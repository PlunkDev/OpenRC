#include "openrc/rac_ratchet_animation_compile.hpp"

#include "openrc/actor_library.hpp"

#include <algorithm>
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
  throw RacRatchetAnimationCompileError(message);
}

void validate_source_index(std::span<const std::byte> decoded_level_core,
                           const RacLevelCoreIndexV1 &level_core) {
  if (decoded_level_core.empty() ||
      level_core.decoded_asset_input_bytes != decoded_level_core.size()) {
    fail("The RAC1 Ratchet animation source and level-core index disagree");
  }
  std::uint32_t previous_offset = 0U;
  for (std::size_t index = 0U; index < level_core.ratchet_sequences.size();
       ++index) {
    const auto &sequence = level_core.ratchet_sequences[index];
    if (sequence.asset_offset == 0U ||
        sequence.asset_range.offset != sequence.asset_offset ||
        sequence.asset_range.offset > decoded_level_core.size() ||
        sequence.asset_range.size == 0U ||
        sequence.asset_range.size >
            decoded_level_core.size() - sequence.asset_range.offset ||
        (index != 0U && sequence.asset_offset <= previous_offset)) {
      fail("The RAC1 Ratchet sequence index is not canonical and bounded");
    }
    previous_offset = sequence.asset_offset;
  }
}

[[nodiscard]] const RacLevelCoreRatchetSequenceV1 &
resolve_sequence(const RacLevelCoreIndexV1 &level_core,
                 const std::uint32_t source_slot) {
  if (source_slot >= kRacLevelCoreRatchetSequenceCountV1) {
    fail("A RAC1 Ratchet animation profile has an out-of-range source slot");
  }
  const auto source_offset = level_core.ratchet_sequence_offsets[source_slot];
  if (source_offset == 0U) {
    fail("A RAC1 Ratchet animation profile selects an empty source slot");
  }
  const auto found = std::lower_bound(
      level_core.ratchet_sequences.begin(),
      level_core.ratchet_sequences.end(), source_offset,
      [](const RacLevelCoreRatchetSequenceV1 &candidate,
         const std::uint32_t offset) {
        return candidate.asset_offset < offset;
      });
  if (found == level_core.ratchet_sequences.end() ||
      found->asset_offset != source_offset) {
    fail("A RAC1 Ratchet animation slot has no bounded source sequence");
  }
  return *found;
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
    fail("Cannot address the RAC1 Ratchet animation rig: " +
         std::string(error.what()));
  }
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

} // namespace

std::vector<RacRatchetAnimationClipProfileV1>
make_rac_ratchet_complete_animation_profiles_v1(
    const RacLevelCoreIndexV1 &level_core,
    const std::span<const RacRatchetAnimationClipProfileV1>
        confirmed_profiles,
    const std::string_view source_sequence_key_prefix,
    const ActorAnimationWrapModeV1 unclassified_wrap_mode) {
  if (source_sequence_key_prefix.empty() ||
      !known_wrap_mode(unclassified_wrap_mode)) {
    fail("The complete RAC1 Ratchet animation profile policy is invalid");
  }

  std::array<bool, kRacLevelCoreRatchetSequenceCountV1> mapped_slots{};
  std::vector<RacRatchetAnimationClipProfileV1> result;
  result.reserve(level_core.ratchet_sequences.size());
  for (std::size_t index = 0U; index < confirmed_profiles.size(); ++index) {
    const auto &profile = confirmed_profiles[index];
    if (profile.clip_id != index ||
        profile.source_slot >= kRacLevelCoreRatchetSequenceCountV1 ||
        level_core.ratchet_sequence_offsets[profile.source_slot] == 0U ||
        profile.semantic_key.empty() || !known_wrap_mode(profile.wrap_mode) ||
        mapped_slots[profile.source_slot]) {
      fail("A confirmed RAC1 Ratchet animation profile is invalid");
    }
    if (std::any_of(result.begin(), result.end(),
                    [&profile](const auto &previous) {
                      return previous.semantic_key == profile.semantic_key;
                    })) {
      fail("Confirmed RAC1 Ratchet animation profiles repeat a semantic key");
    }
    mapped_slots[profile.source_slot] = true;
    result.push_back(profile);
  }

  for (std::uint32_t slot = 0U;
       slot < kRacLevelCoreRatchetSequenceCountV1; ++slot) {
    if (level_core.ratchet_sequence_offsets[slot] == 0U ||
        mapped_slots[slot]) {
      continue;
    }
    if (result.size() >= std::numeric_limits<std::uint32_t>::max()) {
      fail("The complete RAC1 Ratchet animation profile exceeds clip IDs");
    }
    auto key = source_sequence_key(source_sequence_key_prefix, slot);
    if (std::any_of(result.begin(), result.end(),
                    [&key](const auto &previous) {
                      return previous.semantic_key == key;
                    })) {
      fail("A source-addressed RAC1 Ratchet animation key is not unique");
    }
    result.push_back(RacRatchetAnimationClipProfileV1{
        static_cast<std::uint32_t>(result.size()), slot, std::move(key),
        unclassified_wrap_mode});
  }

  if (result.empty() || result.size() != level_core.ratchet_sequences.size()) {
    fail("The complete RAC1 Ratchet animation profile does not cover every "
         "indexed source sequence exactly once");
  }
  return result;
}

ActorAnimationBankV1 compile_rac_ratchet_animation_bank_v1(
    const std::span<const std::byte> decoded_level_core,
    const RacLevelCoreIndexV1 &level_core,
    const RacMobyBindRigV1 &bind_rig,
    const float class_scale,
    std::string rig_key,
    const std::span<const RacRatchetAnimationClipProfileV1> profiles,
    const std::uint32_t source_updates_per_second,
    const RacRatchetAnimationCompileLimitsV1 limits) {
  validate_source_index(decoded_level_core, level_core);
  if (profiles.empty() || profiles.size() > limits.animation.max_clips ||
      profiles.size() > std::numeric_limits<std::uint32_t>::max()) {
    fail("The RAC1 Ratchet animation profile count is invalid or limited");
  }
  if (rig_key.empty() || !std::isfinite(class_scale) || class_scale <= 0.0F ||
      source_updates_per_second == 0U) {
    fail("The RAC1 Ratchet animation compile policy is incomplete");
  }

  ActorAnimationBankV1 result;
  const auto rig_content_sha256 = canonical_rig_digest(bind_rig);
  result.clips.reserve(profiles.size());
  for (std::size_t profile_index = 0U; profile_index < profiles.size();
       ++profile_index) {
    const auto &profile = profiles[profile_index];
    if (profile.clip_id != profile_index || profile.semantic_key.empty() ||
        !known_wrap_mode(profile.wrap_mode)) {
      fail("A RAC1 Ratchet animation profile is non-canonical");
    }
    for (std::size_t previous = 0U; previous < profile_index; ++previous) {
      if (profiles[previous].source_slot == profile.source_slot ||
          profiles[previous].semantic_key == profile.semantic_key) {
        fail("RAC1 Ratchet animation profiles repeat a slot or semantic key");
      }
    }

    const auto &source = resolve_sequence(level_core, profile.source_slot);
    RacRatchetSequenceV1 sequence;
    try {
      sequence = parse_rac_ratchet_sequence_v1(
          decoded_level_core,
          RacRatchetSequenceRangeV1{source.asset_range.offset,
                                    source.asset_range.size},
          limits.sequence);
    } catch (const RacRatchetSequenceError &error) {
      fail("Cannot parse a mapped RAC1 Ratchet animation: " +
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
            sequence, frame_index, bind_rig, class_scale, limits.pose);
      } catch (const RacRatchetPoseError &error) {
        fail("Cannot decode a mapped RAC1 Ratchet animation pose: " +
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
    fail("Cannot canonicalize compiled RAC1 Ratchet animations: " +
         std::string(error.what()));
  }
}

} // namespace openrc
