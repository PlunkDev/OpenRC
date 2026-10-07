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

[[nodiscard]] openrc::ActorAnimationBankV1
make_airborne_bank(const openrc::ActorRigAssetV1 &rig) {
  auto result = make_bank(rig);
  result.clips.push_back(make_clip(3U, "actors/player/jump", rig, 30.0F));
  result.clips.push_back(make_clip(4U, "actors/player/fall", rig, 40.0F));
  return result;
}

[[nodiscard]] openrc::game::RuntimePlayerAnimationProfileV1
airborne_profile(std::string jump, std::string fall) {
  auto result = profile();
  result.jump_clip_key = std::move(jump);
  result.fall_clip_key = std::move(fall);
  return result;
}

[[nodiscard]] openrc::game::PlayerSimulationSnapshotV1
airborne(const double velocity_x, const double velocity_z) {
  auto result = player(velocity_x, 0.0, false);
  result.character.velocity.z = velocity_z;
  return result;
}

void test_empty_airborne_profile_keeps_v1_hold() {
  const auto rig = make_rig_asset();
  const auto bank = make_airborne_bank(rig);
  openrc::game::RuntimePlayerAnimationV1 animation(bank, rig, profile(),
                                                   kLimits);
  animation.fixed_update(player(1.0, 0.0));
  const auto held_playback = animation.playback();
  const auto held_palette = animation.palette();
  for (std::uint32_t tick = 0U; tick < 6U; ++tick) {
    animation.fixed_update(airborne(1.0, tick < 3U ? 4.0 : -4.0));
  }
  expect(animation.active_clip_key() == "actors/player/slow" &&
             animation.playback() == held_playback &&
             animation.palette() == held_palette,
         "an empty airborne profile did not keep the V1 held pose");
}

void test_ground_takeoff_flight_landing_ground() {
  const auto rig = make_rig_asset();
  const auto bank = make_airborne_bank(rig);
  openrc::game::RuntimePlayerAnimationV1 animation(
      bank, rig, airborne_profile("actors/player/jump", "actors/player/fall"),
      kLimits);

  animation.fixed_update(player(1.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/slow",
         "the airborne fixture did not start on the ground");

  // Takeoff: the first airborne tick with upward velocity enters the jump
  // role at source frame zero.
  animation.fixed_update(airborne(1.0, 4.0));
  expect(animation.active_clip_key() == "actors/player/jump" &&
             animation.playback().clip_id == 3U &&
             animation.playback().frame_index == 0U &&
             animation.playback().phase == 0.0 &&
             animation.playback().source_update_accumulator == 50U,
         "takeoff did not start the jump clip");
  expect_near(animation.palette().global_joint_transforms[0U].values[3U],
              30.0, "the jump clip did not sample its first frame");

  // Flight: descending keeps the role chosen at takeoff and advances it.
  animation.fixed_update(airborne(1.0, -4.0));
  expect(animation.active_clip_key() == "actors/player/jump" &&
             animation.playback().phase == 0.5,
         "the jump clip did not keep playing through the descent");

  // Landing: grounded motion re-enters state 2 through slot 3 at frame zero.
  animation.fixed_update(player(1.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/slow" &&
             animation.playback().frame_index == 0U &&
             animation.playback().phase == 0.0,
         "landing did not return to the grounded slot-3 entry");

  // The recovered slot-3 to slot-4 remap still applies after a landing:
  // floor(0 * 2 / 2) + 1 modulo 2.
  animation.fixed_update(player(3.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/full" &&
             animation.playback().frame_index == 1U,
         "the post-landing slot remap left the recovered contract");

  // Walking off a ledge without upward velocity selects the fall role, and
  // a stopped landing returns to idle.
  animation.fixed_update(airborne(3.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/fall" &&
             animation.playback().clip_id == 4U &&
             animation.playback().frame_index == 0U,
         "an unsupported drop did not select the fall clip");
  animation.fixed_update(airborne(3.0, 4.0));
  expect(animation.active_clip_key() == "actors/player/fall",
         "the airborne role changed in mid-air");
  animation.fixed_update(player(0.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/idle",
         "a stopped landing did not return to idle");
}

void test_missing_airborne_role_holds_and_validation() {
  const auto rig = make_rig_asset();
  const auto bank = make_airborne_bank(rig);
  openrc::game::RuntimePlayerAnimationV1 animation(
      bank, rig, airborne_profile("", "actors/player/fall"), kLimits);
  animation.fixed_update(player(1.0, 0.0));
  const auto held_playback = animation.playback();
  animation.fixed_update(airborne(1.0, 4.0));
  animation.fixed_update(airborne(1.0, -4.0));
  expect(animation.active_clip_key() == "actors/player/slow" &&
             animation.playback() == held_playback,
         "a jump without a jump clip did not hold the grounded pose");

  using openrc::game::RuntimePlayerAnimationV1;
  expect_runtime_animation_error(
      [&] {
        RuntimePlayerAnimationV1 invalid(
            bank, rig, airborne_profile("actors/player/missing", ""), kLimits);
      },
      "a missing jump clip was accepted");
  expect_runtime_animation_error(
      [&] {
        RuntimePlayerAnimationV1 invalid(
            bank, rig, airborne_profile("actors/player/slow", ""), kLimits);
      },
      "a jump clip aliasing a grounded clip was accepted");
  expect_runtime_animation_error(
      [&] {
        RuntimePlayerAnimationV1 invalid(
            bank, rig,
            airborne_profile("actors/player/jump", "actors/player/jump"),
            kLimits);
      },
      "identical jump and fall clips were accepted");
}

[[nodiscard]] openrc::ActorAnimationClipV1
make_frames_clip(const std::uint32_t id, std::string key,
                 const openrc::ActorRigAssetV1 &rig, const float base,
                 const std::size_t frame_count) {
  auto result = make_clip(id, std::move(key), rig, base);
  result.frames.resize(frame_count, result.frames.front());
  for (std::size_t index = 0U; index < frame_count; ++index) {
    result.frames[index].joint_poses[0U].translation = {
        base + static_cast<float>(index), 0.0F, 0.0F};
  }
  return result;
}

[[nodiscard]] openrc::ActorAnimationBankV1
make_fall_phase_bank(const openrc::ActorRigAssetV1 &rig) {
  auto result = make_airborne_bank(rig);
  result.clips.push_back(
      make_frames_clip(5U, "actors/player/long-fall", rig, 50.0F, 2U));
  result.clips.push_back(
      make_frames_clip(6U, "actors/player/fall-landing", rig, 100.0F, 30U));
  return result;
}

[[nodiscard]] openrc::game::RuntimePlayerAnimationProfileV1
fall_phase_profile() {
  auto result = airborne_profile("actors/player/jump", "actors/player/fall");
  result.long_fall_clip_key = "actors/player/long-fall";
  result.fall_landing_clip_key = "actors/player/fall-landing";
  return result;
}

void test_fall_long_phase_landing_ground() {
  const auto rig = make_rig_asset();
  const auto bank = make_fall_phase_bank(rig);
  openrc::game::RuntimePlayerAnimationV1 animation(bank, rig,
                                                   fall_phase_profile(),
                                                   kLimits);
  animation.fixed_update(player(1.0, 0.0));

  // Flight: 17 ticks are 14 PAL frames, the 18th reaches frames(18) = 15
  // and switches to the slot-11 role.
  for (std::uint32_t tick = 1U; tick <= 17U; ++tick) {
    animation.fixed_update(airborne(1.0, -1.0));
    expect(animation.active_clip_key() == "actors/player/fall",
           "the fall left its first phase before frames(18)");
  }
  animation.fixed_update(airborne(1.0, -1.0));
  expect(animation.active_clip_key() == "actors/player/long-fall" &&
             animation.playback().frame_index == 0U,
         "the fall did not switch to the slot-11 role at frames(18)");

  // Landing: slot 12 starts at source frame 9 and holds through the
  // frames(7) = 6 PAL-frame input lock even while moving.
  animation.fixed_update(player(1.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/fall-landing" &&
             animation.playback().clip_id == 6U &&
             animation.playback().frame_index == 9U,
         "the long-fall landing did not start slot 12 at source frame 9");
  expect_near(animation.palette().global_joint_transforms[0U].values[3U],
              109.0, "the landing clip did not sample source frame 9");
  for (std::uint32_t tick = 1U; tick <= 7U; ++tick) {
    animation.fixed_update(player(1.0, 0.0));
    expect(animation.active_clip_key() == "actors/player/fall-landing",
           "motion ended the landing inside the input lock");
  }

  // Ground: after six PAL landing frames motion resumes the grounded slot 3.
  animation.fixed_update(player(1.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/slow" &&
             animation.playback().frame_index == 0U,
         "the landing did not hand over to the grounded selection");
}

void test_hard_fall_landing_runs_to_completion() {
  const auto rig = make_rig_asset();
  const auto bank = make_fall_phase_bank(rig);
  openrc::game::RuntimePlayerAnimationV1 animation(bank, rig,
                                                   fall_phase_profile(),
                                                   kLimits);
  animation.fixed_update(player(0.0, 0.0));
  // 90 ticks are frames(90) = 75 PAL frames: the hard-landing branch.
  for (std::uint32_t tick = 0U; tick < 90U; ++tick) {
    animation.fixed_update(airborne(0.0, -1.0));
  }
  animation.fixed_update(player(0.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/fall-landing" &&
             animation.playback().frame_index == 4U,
         "the hard landing did not start slot 12 at source frame 4");

  bool returned_to_idle = false;
  for (std::uint32_t tick = 0U; tick < 100U && !returned_to_idle; ++tick) {
    animation.fixed_update(player(0.0, 0.0));
    returned_to_idle = animation.active_clip_key() == "actors/player/idle";
    if (!returned_to_idle) {
      expect(animation.active_clip_key() == "actors/player/fall-landing" &&
                 (animation.playback().frame_index >= 4U ||
                  animation.playback().completed_cycles == 1U),
             "the standing hard landing left slot 12 before completing it");
    }
  }
  expect(returned_to_idle,
         "the completed hard landing did not return to idle");
}

void test_short_fall_and_jump_skip_fall_landing() {
  const auto rig = make_rig_asset();
  const auto bank = make_fall_phase_bank(rig);
  openrc::game::RuntimePlayerAnimationV1 animation(bank, rig,
                                                   fall_phase_profile(),
                                                   kLimits);
  animation.fixed_update(player(1.0, 0.0));
  for (std::uint32_t tick = 0U; tick < 10U; ++tick) {
    animation.fixed_update(airborne(1.0, -1.0));
  }
  animation.fixed_update(player(1.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/slow",
         "a short fall played the slot-12 landing");

  for (std::uint32_t tick = 0U; tick < 100U; ++tick) {
    animation.fixed_update(airborne(1.0, tick == 0U ? 4.0 : -1.0));
  }
  expect(animation.active_clip_key() == "actors/player/jump",
         "a long jump switched to a fall-phase role");
  animation.fixed_update(player(1.0, 0.0));
  expect(animation.active_clip_key() == "actors/player/slow",
         "a jump landing played the fall-landing clip");

  using openrc::game::RuntimePlayerAnimationV1;
  expect_runtime_animation_error(
      [&] {
        auto invalid = profile();
        invalid.fall_landing_clip_key = "actors/player/fall-landing";
        RuntimePlayerAnimationV1 rejected(bank, rig, invalid, kLimits);
      },
      "a fall-landing clip without a fall clip was accepted");
  expect_runtime_animation_error(
      [&] {
        auto invalid = fall_phase_profile();
        invalid.fall_landing_clip_key = "actors/player/long-fall";
        invalid.long_fall_clip_key.clear();
        RuntimePlayerAnimationV1 rejected(bank, rig, invalid, kLimits);
      },
      "a fall-landing clip without source frame 9 was accepted");
}

int main() {
  try {
    test_fall_long_phase_landing_ground();
    test_hard_fall_landing_runs_to_completion();
    test_short_fall_and_jump_skip_fall_landing();
    test_empty_airborne_profile_keeps_v1_hold();
    test_ground_takeoff_flight_landing_ground();
    test_missing_airborne_role_holds_and_validation();
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
