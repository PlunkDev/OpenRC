#include "openrc/actor_animation_player.hpp"

#include "openrc/actor_library.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr openrc::ActorAnimationPlaybackLimitsV1 kLimits{
    8U, 16U, 8U, 1.0e-8, 1.0e6F};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_near(const float actual, const float expected,
                 const std::string &message) {
  if (std::abs(actual - expected) > 1.0e-5F) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] openrc::ActorRigV1 make_rig() {
  openrc::ActorRigV1 rig;
  rig.joints.resize(2U);
  rig.joints[0U].parent_index = -1;
  rig.joints[1U].parent_index = 0;
  return rig;
}

[[nodiscard]] openrc::ActorAnimationClipV1
make_clip(const openrc::ActorRigV1 &rig,
          const openrc::ActorAnimationWrapModeV1 wrap_mode =
              openrc::ActorAnimationWrapModeV1::loop) {
  openrc::ActorAnimationClipV1 clip;
  clip.id = 3U;
  clip.semantic_key = "actors/test/move";
  clip.rig_key = "actors/test/rig";
  clip.rig_content_sha256 = openrc::actor_rig_content_sha256_v1(rig);
  clip.source_updates_per_second = 50U;
  clip.wrap_mode = wrap_mode;
  clip.frames.resize(2U);
  clip.frames[0U].phase_rate = 0.5F;
  clip.frames[1U].phase_rate = 0.25F;
  for (auto &frame : clip.frames) {
    frame.joint_poses.resize(2U);
  }
  clip.frames[0U].joint_poses[0U].local_scale = {2.0F, 1.0F, 1.0F};
  clip.frames[0U].joint_poses[1U].translation = {1.0F, 0.0F, 0.0F};
  clip.frames[0U].joint_poses[1U].terminal_scale = {1.0F, 0.0F, 1.0F};
  clip.frames[1U].joint_poses[0U].normalized_rotation_xyzw = {
      0.0F, 0.0F, 0.0F, -1.0F};
  clip.frames[1U].joint_poses[0U].translation = {10.0F, 0.0F, 0.0F};
  clip.frames[1U].joint_poses[1U].translation = {1.0F, 0.0F, 0.0F};
  clip.frames[1U].joint_poses[1U].terminal_scale = {1.0F, 0.0F, 1.0F};
  return clip;
}

template <typename Callback>
void expect_rejected(Callback &&callback, const std::string &message) {
  try {
    callback();
  } catch (const openrc::ActorAnimationPlaybackError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_source_update_timing_and_boundaries() {
  const auto rig = make_rig();
  const auto clip = make_clip(rig);
  auto state = openrc::start_actor_animation_playback_v1(clip);
  const auto first = openrc::advance_actor_animation_playback_v1(
      clip, 1U, kLimits, state);
  expect(first == openrc::ActorAnimationAdvanceV1{1U, 0U, 0U, false} &&
             state.frame_index == 0U && state.phase == 0.5,
         "one source update did not advance half of frame zero");
  const auto second = openrc::advance_actor_animation_playback_v1(
      clip, 1U, kLimits, state);
  expect(second.frame_boundaries == 1U && state.frame_index == 1U &&
             state.phase == 0.0,
         "the first frame boundary was not exact");
  const auto wrapped = openrc::advance_actor_animation_playback_v1(
      clip, 4U, kLimits, state);
  expect(wrapped.source_updates == 4U && wrapped.frame_boundaries == 1U &&
             wrapped.cycle_boundaries == 1U && state.frame_index == 0U &&
             state.phase == 0.0 && state.completed_cycles == 1U,
         "loop boundary did not preserve source update timing");
}

void test_integer_pal_cadence_over_sixty_hertz_runtime() {
  const auto rig = make_rig();
  auto clip = make_clip(rig);
  clip.frames[0U].phase_rate = 0.0F;
  auto state = openrc::start_actor_animation_playback_v1(clip);
  std::uint32_t updates = 0U;
  for (std::uint32_t tick = 0U; tick < 60U; ++tick) {
    updates += openrc::advance_actor_animation_fixed_tick_v1(
                   clip, 60U, kLimits, state)
                   .source_updates;
  }
  expect(updates == 50U && state.runtime_ticks_per_second == 60U &&
             state.source_update_accumulator == 0U,
         "PAL 50-to-60 cadence drifted over one runtime second");
  expect_rejected(
      [&] {
        static_cast<void>(openrc::advance_actor_animation_fixed_tick_v1(
            clip, 120U, kLimits, state));
      },
      "playback accepted a cadence change without reset");
}

void test_shortest_quaternion_blend_and_singular_palette() {
  const auto rig = make_rig();
  const auto clip = make_clip(rig);
  auto state = openrc::start_actor_animation_playback_v1(clip);
  state.phase = 0.5;
  const auto palette = openrc::sample_actor_animation_pose_v1(
      clip, state, "actors/test/rig", rig, kLimits);
  expect(palette.global_joint_transforms.size() == 2U &&
             palette.skin_transforms.size() == 2U,
         "animation sampling returned the wrong palette size");
  expect_near(palette.global_joint_transforms[0U].values[0U], 1.5F,
              "local scale interpolation is wrong");
  expect_near(palette.global_joint_transforms[0U].values[3U], 5.0F,
              "translation interpolation is wrong");
  expect_near(palette.global_joint_transforms[1U].values[3U], 6.5F,
              "hierarchical translation did not inherit local scale");
  expect_near(palette.global_joint_transforms[1U].values[5U], 0.0F,
              "terminal zero scale was not preserved");
  expect_near(palette.global_joint_transforms[0U].values[5U], 1.0F,
              "terminal scale leaked to the parent");
}

void test_clamp_and_fail_closed_binding() {
  const auto rig = make_rig();
  auto clip = make_clip(rig, openrc::ActorAnimationWrapModeV1::clamp);
  auto state = openrc::start_actor_animation_playback_v1(clip);
  static_cast<void>(openrc::advance_actor_animation_playback_v1(
      clip, 6U, kLimits, state));
  expect(state.finished && state.frame_index == 1U && state.phase == 0.0,
         "clamped animation did not stop on its final frame");

  clip.rig_content_sha256[0U] ^= std::byte{0x01U};
  expect_rejected(
      [&] {
        static_cast<void>(openrc::sample_actor_animation_pose_v1(
            clip, state, "actors/test/rig", rig, kLimits));
      },
      "animation sampling accepted a stale rig digest");
  auto invalid_state = state;
  invalid_state.phase = 1.0;
  expect_rejected(
      [&] {
        static_cast<void>(openrc::advance_actor_animation_playback_v1(
            clip, 0U, kLimits, invalid_state));
      },
      "animation playback accepted phase one");
}

} // namespace

int main() {
  try {
    test_source_update_timing_and_boundaries();
    test_integer_pal_cadence_over_sixty_hertz_runtime();
    test_shortest_quaternion_blend_and_singular_palette();
    test_clamp_and_fail_closed_binding();
    std::cout << "Actor animation player tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Actor animation player test failure: " << error.what()
              << '\n';
    return 1;
  }
}
