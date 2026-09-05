#include "openrc/actor_animation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace openrc {
namespace {

constexpr double kNormalizedQuaternionLengthTolerance = 1.0e-6;

[[noreturn]] void fail(const std::string &message) {
  throw ActorAnimationError(message);
}

void validate_limits(const ActorAnimationLimitsV1 &limits) {
  if (limits.max_clips == 0U || limits.max_frames_per_clip == 0U ||
      limits.max_total_frames == 0U || limits.max_joints_per_frame == 0U ||
      limits.max_total_joint_poses == 0U ||
      limits.max_semantic_key_bytes == 0U ||
      limits.max_total_semantic_key_bytes == 0U ||
      limits.max_source_updates_per_second == 0U ||
      !std::isfinite(limits.max_absolute_component) ||
      !(limits.max_absolute_component > 0.0F) ||
      !std::isfinite(limits.minimum_quaternion_length) ||
      !(limits.minimum_quaternion_length > 0.0)) {
    fail("ActorAnimationBankV1 caller limits must all be positive and finite");
  }
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

[[nodiscard]] bool key_character(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('_') ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('/');
}

void validate_key(const std::string_view value,
                  const std::uint32_t maximum_bytes,
                  const char *const description) {
  if (value.empty() || value.size() > maximum_bytes || value.front() == '/' ||
      value.back() == '/') {
    fail(std::string("ActorAnimationBankV1 ") + description +
         " is not a canonical semantic key");
  }
  for (const char character : value) {
    if (!key_character(static_cast<unsigned char>(character))) {
      fail(std::string("ActorAnimationBankV1 ") + description +
           " contains a non-canonical character");
    }
  }
  std::size_t component_begin = 0U;
  while (component_begin < value.size()) {
    const auto separator = value.find('/', component_begin);
    const auto component_end =
        separator == std::string_view::npos ? value.size() : separator;
    const auto component =
        value.substr(component_begin, component_end - component_begin);
    if (component.empty() || component == "." || component == "..") {
      fail(std::string("ActorAnimationBankV1 ") + description +
           " contains an unsafe component");
    }
    if (separator == std::string_view::npos) {
      break;
    }
    component_begin = separator + 1U;
  }
}

void validate_component(const float value, const float maximum_absolute,
                        const char *const description) {
  if (!std::isfinite(value)) {
    fail(std::string("ActorAnimationBankV1 has a non-finite ") + description);
  }
  if (std::fabs(value) > maximum_absolute) {
    fail(std::string("ActorAnimationBankV1 ") + description +
         " exceeds its caller limit");
  }
  if (value == 0.0F && std::signbit(value)) {
    fail(std::string("ActorAnimationBankV1 has non-canonical signed zero in ") +
         description);
  }
}

[[nodiscard]] float canonical_component(const float value,
                                        const float maximum_absolute,
                                        const char *const description) {
  if (!std::isfinite(value)) {
    fail(std::string("ActorAnimationBankV1 has a non-finite ") + description);
  }
  if (std::fabs(value) > maximum_absolute) {
    fail(std::string("ActorAnimationBankV1 ") + description +
         " exceeds its caller limit");
  }
  return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] double
quaternion_length(const std::array<float, 4U> &quaternion) noexcept {
  return std::hypot(std::hypot(static_cast<double>(quaternion[0U]),
                               static_cast<double>(quaternion[1U])),
                    std::hypot(static_cast<double>(quaternion[2U]),
                               static_cast<double>(quaternion[3U])));
}

[[nodiscard]] bool
needs_sign_flip(const std::array<float, 4U> &quaternion) noexcept {
  // The identity dot product is W. At exactly 180 degrees, X/Y/Z provide a
  // deterministic tie break so q and -q still have one representation.
  constexpr std::array<std::size_t, 4U> kSignOrder{3U, 0U, 1U, 2U};
  for (const auto component : kSignOrder) {
    if (quaternion[component] < 0.0F) {
      return true;
    }
    if (quaternion[component] > 0.0F) {
      return false;
    }
  }
  return false;
}

void validate_quaternion(const std::array<float, 4U> &quaternion,
                         const ActorAnimationLimitsV1 &limits) {
  for (const float component : quaternion) {
    validate_component(component, limits.max_absolute_component,
                       "joint quaternion component");
  }
  const auto length = quaternion_length(quaternion);
  if (!std::isfinite(length) || length < limits.minimum_quaternion_length) {
    fail("ActorAnimationBankV1 joint quaternion is too short");
  }
  if (std::fabs(length - 1.0) > kNormalizedQuaternionLengthTolerance) {
    fail("ActorAnimationBankV1 joint quaternion is not normalized");
  }
  if (needs_sign_flip(quaternion)) {
    fail("ActorAnimationBankV1 joint quaternion has a non-canonical sign");
  }
}

void canonicalize_quaternion(std::array<float, 4U> &quaternion,
                             const ActorAnimationLimitsV1 &limits) {
  for (float &component : quaternion) {
    component = canonical_component(component, limits.max_absolute_component,
                                    "joint quaternion component");
  }
  const auto length = quaternion_length(quaternion);
  if (!std::isfinite(length) || length < limits.minimum_quaternion_length) {
    fail("ActorAnimationBankV1 joint quaternion is too short");
  }
  for (float &component : quaternion) {
    const auto normalized =
        static_cast<float>(static_cast<double>(component) / length);
    component = normalized == 0.0F ? 0.0F : normalized;
  }
  if (needs_sign_flip(quaternion)) {
    for (float &component : quaternion) {
      component = component == 0.0F ? 0.0F : -component;
    }
  }
}

void validate_pose(const ActorJointPoseV1 &pose,
                   const ActorAnimationLimitsV1 &limits) {
  validate_quaternion(pose.normalized_rotation_xyzw, limits);
  for (const float component : pose.translation) {
    validate_component(component, limits.max_absolute_component,
                       "joint translation component");
  }
  for (const float component : pose.local_scale) {
    validate_component(component, limits.max_absolute_component,
                       "joint local-scale component");
  }
  for (const float component : pose.terminal_scale) {
    validate_component(component, limits.max_absolute_component,
                       "joint terminal-scale component");
  }
}

void canonicalize_pose(ActorJointPoseV1 &pose,
                       const ActorAnimationLimitsV1 &limits) {
  canonicalize_quaternion(pose.normalized_rotation_xyzw, limits);
  for (float &component : pose.translation) {
    component = canonical_component(component, limits.max_absolute_component,
                                    "joint translation component");
  }
  for (float &component : pose.local_scale) {
    component = canonical_component(component, limits.max_absolute_component,
                                    "joint local-scale component");
  }
  for (float &component : pose.terminal_scale) {
    component = canonical_component(component, limits.max_absolute_component,
                                    "joint terminal-scale component");
  }
}

[[nodiscard]] bool
valid_wrap_mode(const ActorAnimationWrapModeV1 mode) noexcept {
  return mode == ActorAnimationWrapModeV1::clamp ||
         mode == ActorAnimationWrapModeV1::loop;
}

} // namespace

void validate_actor_animation_bank_v1(const ActorAnimationBankV1 &bank,
                                      const ActorAnimationLimitsV1 limits) {
  validate_limits(limits);
  if (bank.schema_version != kActorAnimationBankSchemaVersionV1) {
    fail("ActorAnimationBankV1 has an unknown schema version");
  }
  if (bank.clips.size() > limits.max_clips ||
      bank.clips.size() > std::numeric_limits<std::uint32_t>::max()) {
    fail("ActorAnimationBankV1 exceeds its caller or format clip limit");
  }

  std::uint64_t total_frames = 0U;
  std::uint64_t total_joint_poses = 0U;
  std::uint64_t total_key_bytes = 0U;
  std::set<std::string, std::less<>> semantic_keys;
  for (std::size_t clip_index = 0U; clip_index < bank.clips.size();
       ++clip_index) {
    const auto &clip = bank.clips[clip_index];
    if (clip.id != clip_index) {
      fail("ActorAnimationBankV1 clip IDs are not canonical dense table "
           "indices");
    }
    validate_key(clip.semantic_key, limits.max_semantic_key_bytes,
                 "clip semantic key");
    validate_key(clip.rig_key, limits.max_semantic_key_bytes, "rig key");
    if (!semantic_keys.insert(clip.semantic_key).second) {
      fail("ActorAnimationBankV1 has duplicate clip semantic keys");
    }
    if (is_zero_prepared_digest_v1(clip.rig_content_sha256)) {
      fail("ActorAnimationBankV1 clip has a zero rig content digest");
    }
    total_key_bytes = checked_add(total_key_bytes, clip.semantic_key.size(),
                                  "ActorAnimationBankV1 key bytes");
    total_key_bytes = checked_add(total_key_bytes, clip.rig_key.size(),
                                  "ActorAnimationBankV1 key bytes");
    if (total_key_bytes > limits.max_total_semantic_key_bytes) {
      fail("ActorAnimationBankV1 exceeds its aggregate key-byte limit");
    }

    if (clip.source_updates_per_second == 0U ||
        clip.source_updates_per_second > limits.max_source_updates_per_second) {
      fail("ActorAnimationBankV1 source cadence must be positive and within "
           "its caller limit");
    }
    if (!valid_wrap_mode(clip.wrap_mode)) {
      fail("ActorAnimationBankV1 clip has an unknown wrap mode");
    }
    if (clip.frames.empty() ||
        clip.frames.size() > limits.max_frames_per_clip ||
        clip.frames.size() > std::numeric_limits<std::uint32_t>::max()) {
      fail("ActorAnimationBankV1 clip has an invalid frame count");
    }
    total_frames = checked_add(total_frames, clip.frames.size(),
                               "ActorAnimationBankV1 total frame count");
    if (total_frames > limits.max_total_frames) {
      fail("ActorAnimationBankV1 exceeds its aggregate frame limit");
    }

    const auto joint_count = clip.frames.front().joint_poses.size();
    if (joint_count == 0U || joint_count > limits.max_joints_per_frame ||
        joint_count > std::numeric_limits<std::uint32_t>::max()) {
      fail("ActorAnimationBankV1 clip has an invalid rig joint count");
    }
    for (const auto &frame : clip.frames) {
      if (!std::isfinite(frame.phase_rate) || frame.phase_rate < 0.0F ||
          frame.phase_rate > limits.max_absolute_component ||
          (frame.phase_rate == 0.0F && std::signbit(frame.phase_rate))) {
        fail("ActorAnimationBankV1 frame phase rate must be finite, "
             "nonnegative, bounded, and canonical");
      }
      if (frame.joint_poses.size() != joint_count) {
        fail("ActorAnimationBankV1 frame does not have one pose per rig "
             "joint");
      }
      total_joint_poses =
          checked_add(total_joint_poses, frame.joint_poses.size(),
                      "ActorAnimationBankV1 total joint-pose count");
      if (total_joint_poses > limits.max_total_joint_poses) {
        fail("ActorAnimationBankV1 exceeds its aggregate joint-pose limit");
      }
      for (const auto &pose : frame.joint_poses) {
        validate_pose(pose, limits);
      }
    }
  }
}

ActorAnimationBankV1
canonicalize_actor_animation_bank_v1(ActorAnimationBankV1 bank,
                                     const ActorAnimationLimitsV1 limits) {
  validate_limits(limits);
  std::sort(
      bank.clips.begin(), bank.clips.end(),
      [](const ActorAnimationClipV1 &left, const ActorAnimationClipV1 &right) {
        return left.id < right.id;
      });
  for (auto &clip : bank.clips) {
    if (clip.source_updates_per_second == 0U ||
        clip.source_updates_per_second > limits.max_source_updates_per_second) {
      fail("ActorAnimationBankV1 source cadence must be positive and within "
           "its caller limit");
    }
    for (auto &frame : clip.frames) {
      frame.phase_rate =
          canonical_component(frame.phase_rate, limits.max_absolute_component,
                              "frame phase-rate component");
      if (frame.phase_rate < 0.0F) {
        fail("ActorAnimationBankV1 frame phase rate is negative");
      }
      for (auto &pose : frame.joint_poses) {
        canonicalize_pose(pose, limits);
      }
    }
  }
  validate_actor_animation_bank_v1(bank, limits);
  return bank;
}

ActorAnimationBankV1 compose_actor_animation_banks_v1(
    const std::span<const ActorAnimationBankV1> banks,
    const ActorAnimationLimitsV1 limits) {
  validate_limits(limits);
  ActorAnimationBankV1 result;
  std::set<std::string, std::less<>> semantic_keys;
  for (const auto &bank : banks) {
    validate_actor_animation_bank_v1(bank, limits);
    for (const auto &clip : bank.clips) {
      if (!semantic_keys.insert(clip.semantic_key).second) {
        fail("ActorAnimationBankV1 composition repeats a clip semantic key");
      }
      if (result.clips.size() >= std::numeric_limits<std::uint32_t>::max()) {
        fail("ActorAnimationBankV1 composition exceeds the clip ID domain");
      }
      auto appended = clip;
      appended.id = static_cast<std::uint32_t>(result.clips.size());
      result.clips.push_back(std::move(appended));
    }
  }
  return canonicalize_actor_animation_bank_v1(std::move(result), limits);
}

} // namespace openrc
