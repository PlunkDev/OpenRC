#include "openrc/actor_animation_player.hpp"

#include "openrc/actor_library.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw ActorAnimationPlaybackError(message);
}

void validate_limits(const ActorAnimationPlaybackLimitsV1 limits) {
  if (limits.max_source_updates_per_step == 0U ||
      limits.max_frame_advances_per_step == 0U || limits.max_joints == 0U ||
      !std::isfinite(limits.minimum_quaternion_length) ||
      limits.minimum_quaternion_length <= 0.0 ||
      !std::isfinite(limits.max_absolute_component) ||
      limits.max_absolute_component <= 0.0F) {
    fail("Actor animation playback limits must be finite and explicit");
  }
}

[[nodiscard]] float canonical_float(const double value,
                                    const ActorAnimationPlaybackLimitsV1 limits,
                                    const char *const description) {
  if (!std::isfinite(value) ||
      std::abs(value) > static_cast<double>(limits.max_absolute_component) ||
      value > static_cast<double>(std::numeric_limits<float>::max()) ||
      value < -static_cast<double>(std::numeric_limits<float>::max())) {
    fail(std::string("Actor animation produced an invalid ") + description);
  }
  const auto result = static_cast<float>(value);
  if (!std::isfinite(result)) {
    fail(std::string("Actor animation overflowed ") + description);
  }
  return result == 0.0F ? 0.0F : result;
}

void validate_state(const ActorAnimationClipV1 &clip,
                    const ActorAnimationPlaybackStateV1 &state) {
  if (clip.frames.empty() || state.clip_id != clip.id ||
      state.frame_index >= clip.frames.size() || !std::isfinite(state.phase) ||
      state.phase < 0.0 || state.phase >= 1.0 ||
      (state.runtime_ticks_per_second == 0U &&
       state.source_update_accumulator != 0U) ||
      (state.runtime_ticks_per_second != 0U &&
       state.source_update_accumulator >= state.runtime_ticks_per_second) ||
      (state.finished &&
       (clip.wrap_mode != ActorAnimationWrapModeV1::clamp ||
        state.frame_index + 1U != clip.frames.size() || state.phase != 0.0))) {
    fail("Actor animation playback state is invalid for its clip");
  }
}

void validate_transform(const ActorAffineTransformV1 &transform,
                        const ActorAnimationPlaybackLimitsV1 limits,
                        const char *const description) {
  for (const auto value : transform.values) {
    static_cast<void>(canonical_float(value, limits, description));
  }
}

[[nodiscard]] ActorAffineTransformV1
compose(const ActorAffineTransformV1 &parent,
        const ActorAffineTransformV1 &local,
        const ActorAnimationPlaybackLimitsV1 limits) {
  ActorAffineTransformV1 result;
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      double value = 0.0;
      for (std::size_t inner = 0U; inner < 3U; ++inner) {
        value += static_cast<double>(parent.values[row * 4U + inner]) *
                 local.values[inner * 4U + column];
      }
      result.values[row * 4U + column] =
          canonical_float(value, limits, "composed linear component");
    }
    double translation = parent.values[row * 4U + 3U];
    for (std::size_t inner = 0U; inner < 3U; ++inner) {
      translation +=
          static_cast<double>(parent.values[row * 4U + inner]) *
          local.values[inner * 4U + 3U];
    }
    result.values[row * 4U + 3U] =
        canonical_float(translation, limits, "composed translation");
  }
  return result;
}

[[nodiscard]] float lerp(const float left, const float right,
                         const double phase,
                         const ActorAnimationPlaybackLimitsV1 limits,
                         const char *const description) {
  return canonical_float(static_cast<double>(left) +
                             (static_cast<double>(right) - left) * phase,
                         limits, description);
}

[[nodiscard]] ActorJointPoseV1
interpolate_joint(const ActorJointPoseV1 &current,
                  const ActorJointPoseV1 &next,
                  const double phase,
                  const ActorAnimationPlaybackLimitsV1 limits) {
  ActorJointPoseV1 result;
  double dot = 0.0;
  for (std::size_t component = 0U; component < 4U; ++component) {
    dot += static_cast<double>(current.normalized_rotation_xyzw[component]) *
           next.normalized_rotation_xyzw[component];
  }
  const auto hemisphere = dot < 0.0 ? -1.0 : 1.0;
  double squared_length = 0.0;
  for (std::size_t component = 0U; component < 4U; ++component) {
    const auto value =
        static_cast<double>(current.normalized_rotation_xyzw[component]) *
            (1.0 - phase) +
        static_cast<double>(next.normalized_rotation_xyzw[component]) *
            hemisphere * phase;
    result.normalized_rotation_xyzw[component] =
        canonical_float(value, limits, "quaternion component");
    squared_length += value * value;
  }
  const auto length = std::sqrt(squared_length);
  if (!std::isfinite(length) || length < limits.minimum_quaternion_length) {
    fail("Actor animation interpolation produced a zero quaternion");
  }
  for (auto &component : result.normalized_rotation_xyzw) {
    component = canonical_float(static_cast<double>(component) / length,
                                limits, "normalized quaternion component");
  }

  for (std::size_t axis = 0U; axis < 3U; ++axis) {
    result.translation[axis] = lerp(current.translation[axis],
                                    next.translation[axis], phase, limits,
                                    "translation component");
    result.local_scale[axis] = lerp(current.local_scale[axis],
                                    next.local_scale[axis], phase, limits,
                                    "local-scale component");
    result.terminal_scale[axis] = lerp(
        current.terminal_scale[axis], next.terminal_scale[axis], phase, limits,
        "terminal-scale component");
  }
  return result;
}

[[nodiscard]] ActorAffineTransformV1
local_transform(const ActorJointPoseV1 &joint,
                const ActorAnimationPlaybackLimitsV1 limits) {
  const auto x = static_cast<double>(joint.normalized_rotation_xyzw[0U]);
  const auto y = static_cast<double>(joint.normalized_rotation_xyzw[1U]);
  const auto z = static_cast<double>(joint.normalized_rotation_xyzw[2U]);
  const auto w = static_cast<double>(joint.normalized_rotation_xyzw[3U]);
  const std::array<double, 9U> rotation{
      1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w),
      2.0 * (x * z + y * w),       2.0 * (x * y + z * w),
      1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w),
      2.0 * (x * z - y * w),       2.0 * (y * z + x * w),
      1.0 - 2.0 * (x * x + y * y),
  };
  ActorAffineTransformV1 result;
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      result.values[row * 4U + column] = canonical_float(
          rotation[row * 3U + column] * joint.local_scale[column], limits,
          "local linear component");
    }
    result.values[row * 4U + 3U] = joint.translation[row];
  }
  return result;
}

[[nodiscard]] ActorAffineTransformV1
terminal_transform(const ActorAffineTransformV1 &hierarchical,
                   const std::array<float, 3U> &scale,
                   const ActorAnimationPlaybackLimitsV1 limits) {
  auto result = hierarchical;
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      result.values[row * 4U + column] = canonical_float(
          static_cast<double>(hierarchical.values[row * 4U + column]) *
              scale[column],
          limits, "terminally scaled component");
    }
  }
  return result;
}

} // namespace

const ActorAnimationClipV1 *find_actor_animation_clip_v1(
    const ActorAnimationBankV1 &bank,
    const std::string_view semantic_key) noexcept {
  const auto found = std::find_if(
      bank.clips.begin(), bank.clips.end(),
      [semantic_key](const ActorAnimationClipV1 &clip) {
        return clip.semantic_key == semantic_key;
      });
  return found == bank.clips.end() ? nullptr : &*found;
}

ActorAnimationPlaybackStateV1
start_actor_animation_playback_v1(const ActorAnimationClipV1 &clip) {
  if (clip.frames.empty()) {
    fail("Cannot start an empty actor animation clip");
  }
  return ActorAnimationPlaybackStateV1{clip.id, 0U, 0.0, 0U, 0U, 0U, false};
}

ActorAnimationAdvanceV1 advance_actor_animation_playback_v1(
    const ActorAnimationClipV1 &clip, const std::uint32_t source_updates,
    const ActorAnimationPlaybackLimitsV1 limits,
    ActorAnimationPlaybackStateV1 &state) {
  validate_limits(limits);
  validate_state(clip, state);
  if (source_updates > limits.max_source_updates_per_step) {
    fail("Actor animation exceeded its per-step source-update limit");
  }

  auto next = state;
  ActorAnimationAdvanceV1 result;
  result.source_updates = source_updates;
  for (std::uint32_t update = 0U;
       update < source_updates && !next.finished; ++update) {
    const auto rate = clip.frames[next.frame_index].phase_rate;
    if (!std::isfinite(rate) || rate < 0.0F) {
      fail("Actor animation frame has an invalid phase rate");
    }
    if (rate == 0.0F) {
      continue;
    }
    next.phase += rate;
    if (!std::isfinite(next.phase)) {
      fail("Actor animation phase overflowed");
    }
    while (next.phase >= 1.0 && !next.finished) {
      next.phase -= 1.0;
      if (result.frame_boundaries >= limits.max_frame_advances_per_step) {
        fail("Actor animation exceeded its per-step frame-advance limit");
      }
      ++result.frame_boundaries;
      if (next.frame_index + 1U < clip.frames.size()) {
        ++next.frame_index;
        continue;
      }
      if (clip.wrap_mode == ActorAnimationWrapModeV1::loop) {
        next.frame_index = 0U;
        if (next.completed_cycles ==
            std::numeric_limits<std::uint64_t>::max()) {
          fail("Actor animation cycle counter is exhausted");
        }
        ++next.completed_cycles;
        ++result.cycle_boundaries;
      } else if (clip.wrap_mode == ActorAnimationWrapModeV1::clamp) {
        next.phase = 0.0;
        next.finished = true;
      } else {
        fail("Actor animation has an unknown wrap mode");
      }
    }
  }
  if (next.phase == 0.0) {
    next.phase = 0.0;
  }
  result.finished = next.finished;
  state = next;
  return result;
}

ActorAnimationAdvanceV1 advance_actor_animation_fixed_tick_v1(
    const ActorAnimationClipV1 &clip,
    const std::uint32_t runtime_ticks_per_second,
    const ActorAnimationPlaybackLimitsV1 limits,
    ActorAnimationPlaybackStateV1 &state) {
  validate_limits(limits);
  validate_state(clip, state);
  if (runtime_ticks_per_second == 0U ||
      clip.source_updates_per_second == 0U) {
    fail("Actor animation fixed cadence must be non-zero");
  }
  auto next = state;
  if (next.runtime_ticks_per_second == 0U) {
    next.runtime_ticks_per_second = runtime_ticks_per_second;
  } else if (next.runtime_ticks_per_second != runtime_ticks_per_second) {
    fail("Actor animation runtime cadence changed without a reset");
  }
  const auto accumulated =
      static_cast<std::uint64_t>(next.source_update_accumulator) +
      clip.source_updates_per_second;
  const auto source_updates = accumulated / runtime_ticks_per_second;
  if (source_updates > std::numeric_limits<std::uint32_t>::max()) {
    fail("Actor animation source-update count exceeds uint32_t");
  }
  next.source_update_accumulator =
      static_cast<std::uint32_t>(accumulated % runtime_ticks_per_second);
  auto result = advance_actor_animation_playback_v1(
      clip, static_cast<std::uint32_t>(source_updates), limits, next);
  state = next;
  return result;
}

ActorPosePaletteV1 sample_actor_animation_pose_v1(
    const ActorAnimationClipV1 &clip,
    const ActorAnimationPlaybackStateV1 &state,
    const std::string_view rig_key,
    const ActorRigV1 &rig,
    const ActorAnimationPlaybackLimitsV1 limits) {
  validate_limits(limits);
  validate_state(clip, state);
  if (rig_key.empty() || clip.rig_key != rig_key ||
      is_zero_prepared_digest_v1(clip.rig_content_sha256) ||
      clip.rig_content_sha256 != actor_rig_content_sha256_v1(rig)) {
    fail("Actor animation clip does not match the selected rig");
  }
  const auto joint_count = rig.joints.size();
  if (joint_count == 0U || joint_count > limits.max_joints) {
    fail("Actor animation rig has an invalid or limited joint count");
  }
  for (std::size_t index = 0U; index < joint_count; ++index) {
    const auto &joint = rig.joints[index];
    if ((index == 0U && joint.parent_index != -1) ||
        (index != 0U &&
         (joint.parent_index < 0 ||
          static_cast<std::size_t>(joint.parent_index) >= index))) {
      fail("Actor animation rig is not one parent-first hierarchy");
    }
    validate_transform(joint.inverse_bind_transform, limits,
                       "inverse-bind transform");
  }

  const auto &current = clip.frames[state.frame_index];
  std::size_t next_index = state.frame_index;
  if (state.frame_index + 1U < clip.frames.size()) {
    next_index = state.frame_index + 1U;
  } else if (clip.wrap_mode == ActorAnimationWrapModeV1::loop) {
    next_index = 0U;
  }
  const auto &next = clip.frames[next_index];
  if (current.joint_poses.size() != joint_count ||
      next.joint_poses.size() != joint_count) {
    fail("Actor animation frame and rig joint counts disagree");
  }

  std::vector<ActorAffineTransformV1> hierarchical;
  hierarchical.reserve(joint_count);
  ActorPosePaletteV1 result;
  result.global_joint_transforms.reserve(joint_count);
  result.skin_transforms.reserve(joint_count);
  for (std::size_t index = 0U; index < joint_count; ++index) {
    const auto pose = interpolate_joint(current.joint_poses[index],
                                        next.joint_poses[index], state.phase,
                                        limits);
    auto global_hierarchical = local_transform(pose, limits);
    const auto parent = rig.joints[index].parent_index;
    if (parent >= 0) {
      global_hierarchical =
          compose(hierarchical[static_cast<std::size_t>(parent)],
                  global_hierarchical, limits);
    }
    validate_transform(global_hierarchical, limits,
                       "hierarchical joint transform");
    hierarchical.push_back(global_hierarchical);

    auto global =
        terminal_transform(global_hierarchical, pose.terminal_scale, limits);
    auto skin = compose(global, rig.joints[index].inverse_bind_transform,
                        limits);
    validate_transform(global, limits, "global joint transform");
    validate_transform(skin, limits, "skin transform");
    result.global_joint_transforms.push_back(std::move(global));
    result.skin_transforms.push_back(std::move(skin));
  }
  return result;
}

} // namespace openrc
