#include "openrc/runtime_player_animation.hpp"

#include "openrc/actor_library.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr openrc::ActorAnimationPlaybackLimitsV1 kLimits{
    4U, 4U, 4U, 1.0e-8, 1.0e6F};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_near(const double actual, const double expected,
                 const std::string &message) {
  if (std::abs(actual - expected) > 1.0e-6) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_runtime_animation_error(Callback &&callback,
                                    const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::RuntimePlayerAnimationError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::ActorRigAssetV1 make_rig_asset() {
  openrc::ActorRigAssetV1 result;
  result.id = 0U;
  result.semantic_key = "actors/player/rig";
  result.rig.joints.resize(1U);
  result.rig.joints[0U].parent_index = -1;
  result.content_sha256 = openrc::actor_rig_content_sha256_v1(result.rig);
  return result;
}

[[nodiscard]] openrc::ActorAnimationClipV1 make_clip(
    const std::uint32_t id, std::string key,
    const openrc::ActorRigAssetV1 &rig, const float translation_base,
    const bool cadence_counter = false) {
  openrc::ActorAnimationClipV1 result;
  result.id = id;
  result.semantic_key = std::move(key);
  result.rig_key = rig.semantic_key;
  result.rig_content_sha256 = rig.content_sha256;
  result.source_updates_per_second = 50U;
  result.wrap_mode = openrc::ActorAnimationWrapModeV1::loop;
  result.frames.resize(cadence_counter ? 1U : 2U);
  for (std::size_t index = 0U; index < result.frames.size(); ++index) {
    auto &frame = result.frames[index];
    frame.phase_rate = cadence_counter ? 1.0F : 0.5F;
    frame.joint_poses.resize(1U);
    frame.joint_poses[0U].translation = {
        translation_base + static_cast<float>(index) * 2.0F, 0.0F, 0.0F};
  }
  return result;
}

[[nodiscard]] openrc::ActorAnimationBankV1 make_bank(
    const openrc::ActorRigAssetV1 &rig,
    const bool cadence_counter = false) {
  openrc::ActorAnimationBankV1 result;
  result.clips = {
      make_clip(0U, "actors/player/idle", rig, 0.0F, cadence_counter),
      make_clip(1U, "actors/player/slow", rig, 10.0F, cadence_counter),
      make_clip(2U, "actors/player/full", rig, 20.0F, cadence_counter),
  };
  return result;
}

[[nodiscard]] openrc::game::RuntimePlayerAnimationProfileV1 profile() {
  return {
      "actors/player/idle",
      "actors/player/slow",
      "actors/player/full",
      // Compatibility-only values: recovered moving animation thresholds are
      // fixed at 2.35/1.90 and these old preview fields are ignored.
      0.25,
      2.0,
      60U,
  };
}

[[nodiscard]] openrc::game::PlayerSimulationSnapshotV1 player(
    const double velocity_x, const double velocity_y,
    const bool grounded = true, const std::uint64_t reset_count = 0U) {
  openrc::game::PlayerSimulationSnapshotV1 result;
  result.character.velocity = {velocity_x, velocity_y, 0.0};
  result.character.grounded = grounded;
  if (grounded) {
    result.character.ground_triangle_index = 0U;
  }
  result.reset_count = reset_count;
  return result;
}

void test_slow_full_hysteresis_and_phase_remap() {
  using openrc::game::RuntimePlayerAnimationV1;

  const auto rig = make_rig_asset();
  const auto bank = make_bank(rig);
  RuntimePlayerAnimationV1 animation(bank, rig, profile(), kLimits);

  expect(animation.active_clip_key() == "actors/player/idle" &&
             animation.playback().phase == 0.0 &&
             animation.playback().runtime_ticks_per_second == 0U,
         "runtime player animation did not start from idle phase zero");

  animation.fixed_update(player(0.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/idle",
         "a stopped player did not retain the honest idle fallback");

  animation.fixed_update(player(3.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/slow" &&
             animation.playback().clip_id == 1U &&
             animation.playback().frame_index == 0U &&
             animation.playback().phase == 0.0 &&
             animation.playback().completed_cycles == 0U &&
             animation.playback().source_update_accumulator == 50U,
         "state-2 entry did not begin with the slot-3 slow clip");
  expect_near(animation.palette().global_joint_transforms[0U].values[3U],
              10.0, "the selected slow clip did not sample frame zero");

  animation.fixed_update(
      player(openrc::game::kRuntimePlayerSlowToFullHorizontalSpeedV1, 0.0));
  expect(animation.active_clip_key() == "actors/player/slow" &&
             animation.playback().phase == 0.5,
         "equality at 2.35 did not retain the slow clip");

  animation.fixed_update(player(std::nextafter(
      openrc::game::kRuntimePlayerSlowToFullHorizontalSpeedV1,
      std::numeric_limits<double>::infinity()), 0.0));
  expect(animation.active_clip_key() == "actors/player/full" &&
             animation.playback().clip_id == 2U &&
             animation.playback().frame_index == 1U &&
             animation.playback().phase == 0.5 &&
             animation.playback().completed_cycles == 0U &&
             animation.playback().source_update_accumulator == 30U,
         "crossing 2.35 did not use the original slow-to-full frame remap");
  expect_near(animation.palette().global_joint_transforms[0U].values[3U],
              21.0, "the remapped full clip did not sample its mapped phase");

  animation.fixed_update(
      player(openrc::game::kRuntimePlayerFullToSlowHorizontalSpeedV1, 0.0));
  expect(animation.active_clip_key() == "actors/player/full",
         "equality at 1.90 did not retain the full clip");

  animation.fixed_update(player(std::nextafter(
      openrc::game::kRuntimePlayerFullToSlowHorizontalSpeedV1, 0.0), 0.0));
  expect(animation.active_clip_key() == "actors/player/slow" &&
             animation.playback().clip_id == 1U &&
             animation.playback().frame_index == 1U &&
             animation.playback().phase == 0.5 &&
             animation.playback().source_update_accumulator == 10U,
         "crossing below 1.90 did not use the original full-to-slow remap");

  animation.fixed_update(player(3.0, 0.0, true, 1U));
  expect(animation.active_clip_key() == "actors/player/slow" &&
             animation.playback().frame_index == 0U &&
             animation.playback().phase == 0.0 &&
             animation.playback().completed_cycles == 0U &&
             animation.playback().source_update_accumulator == 50U,
         "a player reset did not re-enter state-2 through the slow clip");
}

void test_airborne_holds_last_grounded_pose() {
  const auto rig = make_rig_asset();
  const auto bank = make_bank(rig);
  openrc::game::RuntimePlayerAnimationV1 animation(bank, rig, profile(),
                                                   kLimits);

  animation.fixed_update(player(1.0, 0.0));
  animation.fixed_update(player(1.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/slow" &&
             animation.playback().phase == 0.5,
         "the airborne fixture did not establish an advanced slow pose");
  const auto held_playback = animation.playback();
  const auto held_palette = animation.palette();

  for (std::uint32_t tick = 0U; tick < 12U; ++tick) {
    animation.fixed_update(player(8.0, -3.0, false));
  }
  expect(animation.active_clip_key() == "actors/player/slow" &&
             animation.playback() == held_playback &&
             animation.palette() == held_palette,
         "airborne updates changed the last grounded animation pose");
}

void test_pal_source_cadence_over_sixty_hertz_runtime() {
  const auto rig = make_rig_asset();
  const auto bank = make_bank(rig, true);
  openrc::game::RuntimePlayerAnimationV1 animation(bank, rig, profile(),
                                                   kLimits);
  for (std::uint32_t tick = 0U; tick < 60U; ++tick) {
    animation.fixed_update(player(0.0, 0.0));
  }
  expect(animation.active_clip_key() == "actors/player/idle" &&
             animation.playback().completed_cycles == 50U &&
             animation.playback().runtime_ticks_per_second == 60U &&
             animation.playback().source_update_accumulator == 0U &&
             animation.playback().frame_index == 0U &&
             animation.playback().phase == 0.0,
         "runtime player animation drifted in the 50-to-60 cadence");
}

void test_rig_binding_mismatches_fail_closed() {
  const auto rig = make_rig_asset();
  const auto bank = make_bank(rig);

  auto wrong_key_rig = rig;
  wrong_key_rig.semantic_key = "actors/player/other-rig";
  expect_runtime_animation_error(
      [&] {
        static_cast<void>(openrc::game::RuntimePlayerAnimationV1(
            bank, wrong_key_rig, profile(), kLimits));
      },
      "runtime player animation accepted a mismatched rig key");

  auto wrong_digest_bank = bank;
  wrong_digest_bank.clips[0U].rig_content_sha256[0U] ^= std::byte{0x01U};
  expect_runtime_animation_error(
      [&] {
        static_cast<void>(openrc::game::RuntimePlayerAnimationV1(
            wrong_digest_bank, rig, profile(), kLimits));
      },
      "runtime player animation accepted a mismatched rig digest");
}

void test_singular_palette_is_preserved() {
  const auto rig = make_rig_asset();
  auto bank = make_bank(rig);
  for (auto &frame : bank.clips[0U].frames) {
    frame.joint_poses[0U].local_scale[2U] = 0.0F;
  }

  openrc::game::RuntimePlayerAnimationV1 animation(bank, rig, profile(),
                                                   kLimits);
  animation.fixed_update(player(0.0, 0.0));
  const auto &global = animation.palette().global_joint_transforms.at(0U);
  const auto &skin = animation.palette().skin_transforms.at(0U);
  expect(global.values[10U] == 0.0F && skin.values[10U] == 0.0F,
         "runtime player animation repaired an authored singular scale");
  for (const auto component : global.values) {
    expect(std::isfinite(component),
           "the singular global palette contained a non-finite component");
  }
  for (const auto component : skin.values) {
    expect(std::isfinite(component),
           "the singular skin palette contained a non-finite component");
  }
}

void test_profile_validation() {
  const auto rig = make_rig_asset();
  const auto bank = make_bank(rig);

  auto invalid = profile();
  invalid.fixed_ticks_per_second = 0U;
  expect_runtime_animation_error(
      [&] {
        static_cast<void>(openrc::game::RuntimePlayerAnimationV1(
            bank, rig, invalid, kLimits));
      },
      "runtime player animation accepted a zero runtime cadence");
}

void test_playback_limit_preflight() {
  const auto rig = make_rig_asset();

  auto excessive_cadence = make_bank(rig);
  excessive_cadence.clips[0U].source_updates_per_second =
      profile().fixed_ticks_per_second * kLimits.max_source_updates_per_step +
      1U;
  expect_runtime_animation_error(
      [&] {
        static_cast<void>(openrc::game::RuntimePlayerAnimationV1(
            excessive_cadence, rig, profile(), kLimits));
      },
      "runtime player animation accepted a clip whose cadence can exceed "
      "the per-step source-update limit");

  auto excessive_phase_rate = make_bank(rig);
  excessive_phase_rate.clips[0U].frames[0U].phase_rate =
      static_cast<float>(kLimits.max_frame_advances_per_step) + 0.25F;
  expect_runtime_animation_error(
      [&] {
        static_cast<void>(openrc::game::RuntimePlayerAnimationV1(
            excessive_phase_rate, rig, profile(), kLimits));
      },
      "runtime player animation accepted a clip whose phase rate can exceed "
      "the per-step frame-advance limit");
}

} // namespace

int main() {
  try {
    test_slow_full_hysteresis_and_phase_remap();
    test_airborne_holds_last_grounded_pose();
    test_pal_source_cadence_over_sixty_hertz_runtime();
    test_rig_binding_mismatches_fail_closed();
    test_singular_palette_is_preserved();
    test_profile_validation();
    test_playback_limit_preflight();
    std::cout << "runtime_player_animation_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime_player_animation_tests: " << error.what() << '\n';
    return 1;
  }
}
