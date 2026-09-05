#pragma once

#include "openrc/actor_animation.hpp"
#include "openrc/actor_pose.hpp"

#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace openrc {

struct ActorAnimationPlaybackStateV1 {
  std::uint32_t clip_id = 0U;
  std::uint32_t frame_index = 0U;
  double phase = 0.0;
  std::uint64_t completed_cycles = 0U;
  std::uint32_t runtime_ticks_per_second = 0U;
  std::uint32_t source_update_accumulator = 0U;
  bool finished = false;

  [[nodiscard]] bool
  operator==(const ActorAnimationPlaybackStateV1 &) const = default;
};

struct ActorAnimationAdvanceV1 {
  std::uint32_t source_updates = 0U;
  std::uint32_t frame_boundaries = 0U;
  std::uint32_t cycle_boundaries = 0U;
  bool finished = false;

  [[nodiscard]] bool operator==(const ActorAnimationAdvanceV1 &) const =
      default;
};

struct ActorAnimationPlaybackLimitsV1 {
  std::uint32_t max_source_updates_per_step = 0U;
  std::uint32_t max_frame_advances_per_step = 0U;
  std::uint32_t max_joints = 0U;
  double minimum_quaternion_length = 0.0;
  float max_absolute_component = 0.0F;
};

class ActorAnimationPlaybackError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

[[nodiscard]] const ActorAnimationClipV1 *find_actor_animation_clip_v1(
    const ActorAnimationBankV1 &bank, std::string_view semantic_key) noexcept;

[[nodiscard]] ActorAnimationPlaybackStateV1
start_actor_animation_playback_v1(const ActorAnimationClipV1 &clip);

// Advances by an explicit number of whole source updates. Each update adds the
// current frame's phase rate before boundaries are resolved, matching the
// source playback order even when adjacent frames use different rates.
[[nodiscard]] ActorAnimationAdvanceV1 advance_actor_animation_playback_v1(
    const ActorAnimationClipV1 &clip,
    std::uint32_t source_updates,
    ActorAnimationPlaybackLimitsV1 limits,
    ActorAnimationPlaybackStateV1 &state);

// Advances exactly one fixed runtime tick using an integer cadence
// accumulator (for example PAL 50 source updates over a 60 Hz simulation).
// A state cannot silently change runtime cadence after its first tick.
[[nodiscard]] ActorAnimationAdvanceV1
advance_actor_animation_fixed_tick_v1(
    const ActorAnimationClipV1 &clip,
    std::uint32_t runtime_ticks_per_second,
    ActorAnimationPlaybackLimitsV1 limits,
    ActorAnimationPlaybackStateV1 &state);

// Samples current->next using shortest-hemisphere normalized quaternion
// interpolation, composes the parent-first hierarchy, applies terminal scale
// without propagating it to children, and returns G plus G * inverse-bind.
// Finite singular matrices are retained for position-only skinning.
[[nodiscard]] ActorPosePaletteV1 sample_actor_animation_pose_v1(
    const ActorAnimationClipV1 &clip,
    const ActorAnimationPlaybackStateV1 &state,
    std::string_view rig_key,
    const ActorRigV1 &rig,
    ActorAnimationPlaybackLimitsV1 limits);

} // namespace openrc
