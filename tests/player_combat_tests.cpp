#include "openrc/player_combat.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_near(const double actual, const double expected,
                 const std::string &message) {
  if (std::abs(actual - expected) > 1.0e-12) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_combat_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::PlayerCombatError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::game::PlayerCombatProfileV1 profile() {
  openrc::game::PlayerCombatProfileV1 result;
  result.semantic_key = "player/attacks/wrench-swing";
  result.source_authored_id = 73U;
  result.damage_channel = openrc::game::kDamageChannelMeleeV1;
  result.damage = 12U;
  result.startup_ticks = 2U;
  result.active_ticks = 3U;
  result.recovery_ticks = 2U;
  result.forward_start = 0.25;
  result.forward_end = 1.25;
  result.vertical_offset = 0.75;
  result.radius = 0.4;
  return result;
}

[[nodiscard]] openrc::game::GameInputCommandV1
command(const std::uint64_t tick, const std::uint32_t held = 0U,
        const std::uint32_t pressed = 0U) {
  openrc::game::GameInputCommandV1 result;
  result.tick_index = tick;
  result.held_buttons = held;
  result.pressed_buttons = pressed;
  return result;
}

[[nodiscard]] openrc::game::PlayerSimulationSnapshotV1
post_movement_player(const std::uint64_t input_tick,
                     const openrc::CollisionVectorV1 feet_position = {},
                     const double facing_yaw_radians = 0.0) {
  openrc::game::PlayerSimulationSnapshotV1 result;
  result.character.feet_position = feet_position;
  result.facing_yaw_radians = facing_yaw_radians;
  result.next_tick_index = input_tick + 1U;
  return result;
}

void test_damage_channels_and_profile_validation() {
  using namespace openrc::game;

  expect(is_single_damage_channel_v1(kDamageChannelMeleeV1) &&
             is_single_damage_channel_v1(kDamageChannelProjectileV1) &&
             is_single_damage_channel_v1(kDamageChannelExplosiveV1) &&
             is_single_damage_channel_v1(kDamageChannelEnvironmentV1) &&
             !is_single_damage_channel_v1(0U) &&
             !is_single_damage_channel_v1(kDamageChannelKnownMaskV1) &&
             !is_single_damage_channel_v1(UINT32_C(1) << 31U),
         "the neutral damage-channel partition is not exact");

  validate_player_combat_profile_v1(profile());

  auto exact_duration_limit = profile();
  exact_duration_limit.startup_ticks = kPlayerCombatMaximumAttackTicksV1 - 1U;
  exact_duration_limit.active_ticks = 1U;
  exact_duration_limit.recovery_ticks = 0U;
  validate_player_combat_profile_v1(exact_duration_limit);

  auto invalid = profile();
  invalid.semantic_key = "Player/attacks/wrench";
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "a non-canonical semantic key was accepted");
  invalid = profile();
  invalid.semantic_key = "player//wrench";
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "an unsafe semantic-key component was accepted");
  invalid = profile();
  invalid.semantic_key.assign(kPlayerCombatMaximumSemanticKeyBytesV1 + 1U, 'a');
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "a semantic key beyond its hard byte limit was accepted");

  invalid = profile();
  invalid.damage_channel = kDamageChannelMeleeV1 | kDamageChannelProjectileV1;
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "multiple damage channels were accepted");
  invalid = profile();
  invalid.damage = 0U;
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "zero player damage was accepted");
  invalid = profile();
  invalid.active_ticks = 0U;
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "an attack without an active tick was accepted");
  invalid = exact_duration_limit;
  invalid.recovery_ticks = 1U;
  expect_combat_error(
      [&] { validate_player_combat_profile_v1(invalid); },
      "an attack beyond the hard phase-duration limit was accepted");

  invalid = profile();
  invalid.forward_start = -0.01;
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "a negative forward capsule start was accepted");
  invalid = profile();
  invalid.forward_end = invalid.forward_start - 0.01;
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "a reversed damage capsule was accepted");
  invalid = profile();
  invalid.vertical_offset = std::numeric_limits<double>::quiet_NaN();
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "a non-finite vertical offset was accepted");
  invalid = profile();
  invalid.radius = 0.0;
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "a non-positive damage radius was accepted");

  auto boundary = profile();
  boundary.forward_start = kGameplayDamageMaximumGeometryMagnitudeV1;
  boundary.forward_end = kGameplayDamageMaximumGeometryMagnitudeV1;
  boundary.vertical_offset = -kGameplayDamageMaximumGeometryMagnitudeV1;
  boundary.radius = kGameplayDamageMaximumGeometryMagnitudeV1;
  validate_player_combat_profile_v1(boundary);
  boundary.radius = kGameplayDamageMinimumRadiusV1;
  validate_player_combat_profile_v1(boundary);

  invalid = profile();
  invalid.forward_end = std::nextafter(
      kGameplayDamageMaximumGeometryMagnitudeV1,
      std::numeric_limits<double>::infinity());
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "a capsule beyond the shared damage domain was accepted");

  invalid = profile();
  invalid.radius = std::nextafter(kGameplayDamageMinimumRadiusV1, 0.0);
  expect_combat_error([&] { validate_player_combat_profile_v1(invalid); },
                      "a sub-Q6 damage radius was accepted");
}

void test_zero_startup_one_active_tick_production_profile() {
  using namespace openrc::game;

  auto production = profile();
  production.startup_ticks = 0U;
  production.active_ticks = 1U;
  production.recovery_ticks = 0U;
  PlayerCombatV1 combat(production);
  const auto primary = game_button_mask_v1(GameButtonV1::primary_action);
  const auto result = combat.fixed_update(
      command(0U, primary, primary),
      post_movement_player(0U, {10.0, 20.0, 3.0}, kPi / 2.0));

  expect(result.attack_started && result.damage_pulse.has_value(),
         "a zero-startup production attack did not hit on its starting tick");
  const auto &pulse = *result.damage_pulse;
  expect(pulse.attack_sequence == 1U &&
             pulse.source_authored_id == production.source_authored_id &&
             pulse.damage_channel == production.damage_channel &&
             pulse.damage == production.damage &&
             pulse.radius == production.radius,
         "the emitted damage pulse lost neutral attack metadata");
  expect_near(pulse.capsule_start.x, 10.0,
              "the pulse did not use post-movement facing for start X");
  expect_near(pulse.capsule_start.y, 20.25,
              "the pulse did not use post-movement facing for start Y");
  expect_near(pulse.capsule_start.z, 3.75,
              "the pulse did not use post-movement feet position for start Z");
  expect_near(pulse.capsule_end.x, 10.0,
              "the pulse did not use post-movement facing for end X");
  expect_near(pulse.capsule_end.y, 21.25,
              "the pulse did not use post-movement facing for end Y");
  expect_near(pulse.capsule_end.z, 3.75,
              "the pulse did not use the authored vertical offset");
  expect(combat.snapshot() ==
             PlayerCombatSnapshotV1{1U, 1U, PlayerCombatPhaseV1::idle, 0U},
         "a one-active-tick attack did not return to idle immediately");

  const auto held_only =
      combat.fixed_update(command(1U, primary, 0U), post_movement_player(1U));
  expect(!held_only.attack_started && !held_only.damage_pulse.has_value() &&
             combat.snapshot().attack_sequence == 1U,
         "held primary action retriggered an idle attack");

  const auto second = combat.fixed_update(command(2U, primary, primary),
                                          post_movement_player(2U));
  expect(second.attack_started && second.damage_pulse.has_value() &&
             second.damage_pulse->attack_sequence == 2U,
         "separate pressed edges did not produce monotonic attack sequences");
}

void test_exact_phase_timeline_and_post_movement_pulses() {
  using namespace openrc::game;

  PlayerCombatV1 combat(profile());
  const auto primary = game_button_mask_v1(GameButtonV1::primary_action);

  const auto startup_one = combat.fixed_update(command(0U, primary, primary),
                                               post_movement_player(0U));
  expect(
      startup_one.attack_started && !startup_one.damage_pulse &&
          combat.snapshot() ==
              PlayerCombatSnapshotV1{1U, 1U, PlayerCombatPhaseV1::startup, 1U},
      "the starting tick did not consume exactly one startup tick");

  const auto startup_two =
      combat.fixed_update(command(1U, primary), post_movement_player(1U));
  expect(
      !startup_two.attack_started && !startup_two.damage_pulse &&
          combat.snapshot() ==
              PlayerCombatSnapshotV1{2U, 1U, PlayerCombatPhaseV1::active, 3U},
      "startup did not transition to the full active phase");

  const auto active_one = combat.fixed_update(
      command(2U, primary), post_movement_player(2U, {2.0, 4.0, 1.0}, 0.0));
  const auto active_two =
      combat.fixed_update(command(3U, primary),
                          post_movement_player(3U, {5.0, 7.0, 2.0}, kPi / 2.0));
  const auto active_three = combat.fixed_update(
      command(4U, primary), post_movement_player(4U, {-3.0, 8.0, 4.0}, kPi));
  expect(active_one.damage_pulse && active_two.damage_pulse &&
             active_three.damage_pulse &&
             active_one.damage_pulse->attack_sequence == 1U &&
             active_two.damage_pulse->attack_sequence == 1U &&
             active_three.damage_pulse->attack_sequence == 1U,
         "active ticks did not emit one pulse for the same attack sequence");
  expect_near(active_one.damage_pulse->capsule_start.x, 2.25,
              "the first active tick used stale player position");
  expect_near(active_two.damage_pulse->capsule_start.y, 7.25,
              "the second active tick used stale player facing");
  expect_near(active_three.damage_pulse->capsule_start.x, -3.25,
              "the third active tick used stale player facing");
  expect(combat.snapshot() ==
             PlayerCombatSnapshotV1{5U, 1U, PlayerCombatPhaseV1::recovery, 2U},
         "the last active tick did not enter the complete recovery phase");

  const auto ignored_press = combat.fixed_update(command(5U, primary, primary),
                                                 post_movement_player(5U));
  expect(
      !ignored_press.attack_started && !ignored_press.damage_pulse &&
          combat.snapshot() ==
              PlayerCombatSnapshotV1{6U, 1U, PlayerCombatPhaseV1::recovery, 1U},
      "a pressed edge during recovery was buffered or retriggered");
  const auto last_recovery =
      combat.fixed_update(command(6U, primary), post_movement_player(6U));
  expect(!last_recovery.attack_started && !last_recovery.damage_pulse &&
             combat.snapshot() ==
                 PlayerCombatSnapshotV1{7U, 1U, PlayerCombatPhaseV1::idle, 0U},
         "the final recovery tick did not transition to idle");

  const auto still_held =
      combat.fixed_update(command(7U, primary), post_movement_player(7U));
  expect(!still_held.attack_started && !still_held.damage_pulse,
         "held input retriggered after recovery without a new pressed edge");
}

void test_snapshot_replay_hash_and_relative_validation() {
  using namespace openrc::game;

  auto replay_profile = profile();
  replay_profile.startup_ticks = 1U;
  replay_profile.active_ticks = 2U;
  replay_profile.recovery_ticks = 1U;
  const auto primary = game_button_mask_v1(GameButtonV1::primary_action);
  PlayerCombatV1 original(replay_profile, 10U);
  static_cast<void>(original.fixed_update(command(10U, primary, primary),
                                          post_movement_player(10U)));
  const auto saved = original.snapshot();
  PlayerCombatV1 restored(replay_profile, saved);
  expect(restored.profile() == replay_profile && restored.snapshot() == saved &&
             hash_player_combat_snapshot_v1(restored.snapshot()) ==
                 hash_player_combat_snapshot_v1(saved),
         "a player-combat snapshot did not restore exactly");

  for (std::uint64_t tick = 11U; tick < 15U; ++tick) {
    const auto input = command(tick, primary);
    const auto player = post_movement_player(
        tick, {static_cast<double>(tick), -2.0, 3.0}, 0.25);
    expect(original.fixed_update(input, player) ==
                   restored.fixed_update(input, player) &&
               original.snapshot() == restored.snapshot() &&
               hash_player_combat_snapshot_v1(original.snapshot()) ==
                   hash_player_combat_snapshot_v1(restored.snapshot()),
           "snapshot replay diverged from the original combat stream");
  }

  auto unknown_phase = saved;
  unknown_phase.phase = static_cast<PlayerCombatPhaseV1>(255U);
  expect_combat_error(
      [&] { validate_player_combat_snapshot_v1(unknown_phase); },
      "an unknown combat phase was accepted");
  auto beyond_hard_limit = saved;
  beyond_hard_limit.phase_ticks_remaining =
      kPlayerCombatMaximumAttackTicksV1 + 1U;
  expect_combat_error(
      [&] { validate_player_combat_snapshot_v1(beyond_hard_limit); },
      "a combat snapshot beyond the hard phase limit was accepted");
  auto outside_profile = saved;
  outside_profile.phase = PlayerCombatPhaseV1::active;
  outside_profile.phase_ticks_remaining = replay_profile.active_ticks + 1U;
  expect_combat_error(
      [&] {
        static_cast<void>(PlayerCombatV1(replay_profile, outside_profile));
      },
      "a replay snapshot outside its profile phase was accepted");
}

void test_tick_contract_overflow_guards_and_strong_commit() {
  using namespace openrc::game;

  auto immediate = profile();
  immediate.startup_ticks = 0U;
  immediate.active_ticks = 1U;
  immediate.recovery_ticks = 0U;
  const auto primary = game_button_mask_v1(GameButtonV1::primary_action);
  PlayerCombatV1 combat(immediate);
  const auto before = combat.snapshot();

  expect_combat_error(
      [&] {
        static_cast<void>(
            combat.fixed_update(command(1U), post_movement_player(1U)));
      },
      "an out-of-order combat command was accepted");
  expect(combat.snapshot() == before,
         "an out-of-order command partially mutated combat state");

  auto stale_player = post_movement_player(0U);
  stale_player.next_tick_index = 0U;
  expect_combat_error(
      [&] {
        static_cast<void>(combat.fixed_update(command(0U), stale_player));
      },
      "a pre-movement player snapshot was accepted");
  expect(combat.snapshot() == before,
         "a stale player snapshot partially mutated combat state");

  auto overflowing_geometry = immediate;
  overflowing_geometry.forward_start =
      kGameplayDamageMaximumGeometryMagnitudeV1;
  overflowing_geometry.forward_end =
      kGameplayDamageMaximumGeometryMagnitudeV1;
  PlayerCombatV1 geometry_combat(overflowing_geometry);
  const auto geometry_before = geometry_combat.snapshot();
  expect_combat_error(
      [&] {
        static_cast<void>(geometry_combat.fixed_update(
            command(0U, primary, primary),
            post_movement_player(
                0U,
                {kGameplayDamageMaximumGeometryMagnitudeV1, 0.0, 0.0},
                0.0)));
      },
      "an out-of-domain world-space damage pulse was accepted");
  expect(geometry_combat.snapshot() == geometry_before,
         "failed capsule construction partially committed attack state");

  const PlayerCombatSnapshotV1 exhausted_attacks{
      0U, std::numeric_limits<std::uint64_t>::max(), PlayerCombatPhaseV1::idle,
      0U};
  PlayerCombatV1 attack_exhausted(immediate, exhausted_attacks);
  expect_combat_error(
      [&] {
        static_cast<void>(attack_exhausted.fixed_update(
            command(0U, primary, primary), post_movement_player(0U)));
      },
      "an attack sequence beyond uint64_t was accepted");
  expect(attack_exhausted.snapshot() == exhausted_attacks,
         "attack-sequence exhaustion partially mutated combat state");

  const PlayerCombatSnapshotV1 exhausted_ticks{
      std::numeric_limits<std::uint64_t>::max(), 0U, PlayerCombatPhaseV1::idle,
      0U};
  PlayerCombatV1 tick_exhausted(immediate, exhausted_ticks);
  auto maximum_tick_player = post_movement_player(0U);
  maximum_tick_player.next_tick_index =
      std::numeric_limits<std::uint64_t>::max();
  expect_combat_error(
      [&] {
        static_cast<void>(tick_exhausted.fixed_update(
            command(std::numeric_limits<std::uint64_t>::max()),
            maximum_tick_player));
      },
      "a combat tick beyond uint64_t was accepted");
  expect(tick_exhausted.snapshot() == exhausted_ticks,
         "tick-sequence exhaustion partially mutated combat state");
}

} // namespace

int main() {
  try {
    test_damage_channels_and_profile_validation();
    test_zero_startup_one_active_tick_production_profile();
    test_exact_phase_timeline_and_post_movement_pulses();
    test_snapshot_replay_hash_and_relative_validation();
    test_tick_contract_overflow_guards_and_strong_commit();
    std::cout << "player_combat_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "player_combat_tests: " << error.what() << '\n';
    return 1;
  }
}
