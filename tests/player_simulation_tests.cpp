#include "openrc/player_simulation.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::CollisionWorldBuildLimitsV1 kBuildLimits{
    1024U, 1024U, 16'384U, 65'536U, openrc::kCollisionDefaultGridCellSizeQ6V1,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_near(const double actual, const double expected,
                 const double tolerance, const std::string &message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_player_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::PlayerSimulationError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::CollisionWorldV1 empty_world() {
  return openrc::build_collision_world_v1({}, kBuildLimits);
}

[[nodiscard]] openrc::CollisionWorldV1 floor_world() {
  openrc::CollisionMeshV1 mesh;
  mesh.vertices = {
      {-640, -640, 0},
      {640, -640, 0},
      {640, 640, 0},
      {-640, 640, 0},
  };
  mesh.triangles = {
      {{0U, 1U, 2U}, {}, openrc::CollisionLayerV1::world},
      {{0U, 2U, 3U}, {}, openrc::CollisionLayerV1::world},
  };
  return openrc::build_collision_world_v1(std::move(mesh), kBuildLimits);
}

[[nodiscard]] openrc::game::CharacterControllerProfileV1 character_profile() {
  openrc::game::CharacterControllerProfileV1 result;
  result.capsule_radius = 0.3;
  result.capsule_height = 1.8;
  result.skin_width = 0.01;
  result.ground_probe_distance = 0.14;
  result.step_height = 0.5;
  result.maximum_slope_degrees = 45.0;
  result.maximum_ground_speed = 4.0;
  result.ground_acceleration = 80.0;
  result.ground_deceleration = 80.0;
  result.air_acceleration = 12.0;
  result.gravity = 20.0;
  result.jump_speed = 7.0;
  result.maximum_fall_speed = 30.0;
  result.maximum_substep_distance = 0.04;
  result.maximum_motion_substeps = 256U;
  result.maximum_slide_iterations = 4U;
  result.maximum_depenetration_iterations = 8U;
  result.collision_layers = openrc::kCollisionAllLayersMaskV1;
  result.query_limits = {4096U, 4096U};
  return result;
}

[[nodiscard]] openrc::game::PlayerSimulationProfileV1
player_profile(const double death_height) {
  return {character_profile(), 60U, death_height};
}

[[nodiscard]] openrc::game::PlayerCheckpointV1 checkpoint() {
  return {17U, {0.0, 0.0, 2.0}, 0.75};
}

[[nodiscard]] openrc::game::GameInputCommandV1
command(const std::uint64_t tick, const std::int16_t move_x = 0,
        const std::int16_t move_y = 0,
        const std::uint32_t pressed_buttons = 0U) {
  openrc::game::GameInputCommandV1 result;
  result.tick_index = tick;
  result.axes.move_x = move_x;
  result.axes.move_y = move_y;
  result.pressed_buttons = pressed_buttons;
  return result;
}

void test_absolute_death_height_reset() {
  using namespace openrc::game;

  const auto world = empty_world();
  PlayerSimulationV1 player(player_profile(0.0), checkpoint());
  PlayerSimulationStepV1 last_step;
  for (std::uint64_t tick = 0U; tick < 180U; ++tick) {
    last_step = player.fixed_update(world, command(tick));
    if (last_step.reset_reason != PlayerResetReasonV1::none) {
      break;
    }
  }
  const auto state = player.snapshot();
  expect(last_step.reset_reason ==
                 PlayerResetReasonV1::fell_below_death_height &&
             state.reset_count == 1U &&
             state.character.feet_position == checkpoint().feet_position &&
             state.facing_yaw_radians == checkpoint().facing_yaw_radians,
         "crossing the authored absolute death plane did not reset to the "
         "checkpoint");
}

void test_manual_and_checkpoint_activation_reset() {
  using namespace openrc::game;

  const auto world = empty_world();
  PlayerSimulationV1 player(player_profile(-100.0), checkpoint());
  static_cast<void>(
      player.fixed_update(world, command(0U, kGameInputAxisMagnitudeV1, 0)));
  const auto manual = player.fixed_update(
      world,
      command(1U, 0, 0, game_button_mask_v1(GameButtonV1::reset_checkpoint)));
  expect(manual.reset_reason == PlayerResetReasonV1::manual &&
             player.snapshot().character.feet_position ==
                 checkpoint().feet_position &&
             player.snapshot().reset_count == 1U,
         "the explicit reset input did not restore the active checkpoint");

  const PlayerCheckpointV1 next_checkpoint{
      88U,
      {4.0, -2.0, 3.0},
      -0.25,
  };
  player.set_checkpoint(next_checkpoint, true);
  const auto activated = player.snapshot();
  expect(activated.checkpoint == next_checkpoint &&
             activated.character.feet_position ==
                 next_checkpoint.feet_position &&
             activated.last_reset_reason ==
                 PlayerResetReasonV1::checkpoint_activated &&
             activated.reset_count == 2U,
         "activating a new checkpoint did not atomically replace the spawn");
}

void test_tick_order_snapshot_and_deterministic_hash() {
  using namespace openrc::game;

  const auto world = floor_world();
  PlayerSimulationV1 first(player_profile(-20.0), checkpoint(), 10U);
  PlayerSimulationV1 second(player_profile(-20.0), checkpoint(), 10U);
  for (std::uint64_t tick = 10U; tick < 100U; ++tick) {
    const auto x = tick < 55U ? kGameInputAxisMagnitudeV1 : 0;
    const auto y = tick >= 55U ? kGameInputAxisMagnitudeV1 : 0;
    const auto buttons =
        tick == 45U ? game_button_mask_v1(GameButtonV1::jump) : 0U;
    const auto input = command(tick, x, y, buttons);
    expect(first.fixed_update(world, input) ==
               second.fixed_update(world, input),
           "identical replay commands produced different step events");
    expect(first.snapshot() == second.snapshot() &&
               hash_player_simulation_snapshot_v1(first.snapshot()) ==
                   hash_player_simulation_snapshot_v1(second.snapshot()),
           "identical replay commands produced different player state");
  }

  const auto saved = first.snapshot();
  PlayerSimulationV1 restored(player_profile(-20.0), saved);
  expect(restored.snapshot() == saved &&
             hash_player_simulation_snapshot_v1(restored.snapshot()) ==
                 hash_player_simulation_snapshot_v1(saved),
         "a player snapshot did not restore exactly");
  expect_player_error(
      [&] {
        static_cast<void>(
            restored.fixed_update(world, command(saved.next_tick_index + 1U)));
      },
      "an out-of-order replay command was accepted");
  expect(restored.snapshot() == saved,
         "a rejected replay command partially mutated player state");

  auto negative_zero = saved;
  auto positive_zero = saved;
  negative_zero.facing_yaw_radians = -0.0;
  positive_zero.facing_yaw_radians = 0.0;
  expect(hash_player_simulation_snapshot_v1(negative_zero) ==
             hash_player_simulation_snapshot_v1(positive_zero),
         "semantic zero values produced different player hashes");
}

void test_partial_analog_magnitude_reaches_character_motion() {
  using namespace openrc::game;

  const auto world = empty_world();
  PlayerSimulationV1 partial(player_profile(-100.0), checkpoint());
  PlayerSimulationV1 full(player_profile(-100.0), checkpoint());
  constexpr std::int16_t kPartialAxis = 16'384;
  for (std::uint64_t tick = 0U; tick < 30U; ++tick) {
    static_cast<void>(
        partial.fixed_update(world, command(tick, kPartialAxis, 0)));
    static_cast<void>(
        full.fixed_update(world, command(tick, kGameInputAxisMagnitudeV1, 0)));
  }

  const auto partial_x = partial.snapshot().character.feet_position.x;
  const auto full_x = full.snapshot().character.feet_position.x;
  expect(partial_x > 0.0 && partial_x < full_x,
         "partial analog travel did not produce a slower nonzero movement");
}

void test_explicit_motion_override_and_jump_authority() {
  using namespace openrc::game;

  auto exact_profile = player_profile(-100.0);
  exact_profile.character.maximum_ground_speed = 5.7;
  const auto world = floor_world();
  PlayerSimulationV1 player(exact_profile, checkpoint());
  std::uint64_t tick = 0U;
  while (!player.snapshot().character.grounded && tick < 120U) {
    static_cast<void>(player.fixed_update(world, command(tick)));
    ++tick;
  }
  expect(player.snapshot().character.grounded,
         "the explicit-motion test player did not reach the floor");

  const CharacterMotionV1 supplied_jump{0.3, 0.4, true, 0.9};
  static_cast<void>(player.fixed_update(world, command(tick), supplied_jump));
  ++tick;
  const auto slow_walk = player.snapshot();
  expect_near(slow_walk.character.velocity.x, 0.54, 1.0e-12,
              "the player override lost the exact target X velocity");
  expect_near(slow_walk.character.velocity.y, 0.72, 1.0e-12,
              "the player override lost the exact target Y velocity");
  expect(slow_walk.character.grounded && slow_walk.character.velocity.z == 0.0,
         "a supplied jump flag bypassed the authoritative input buttons");

  const CharacterMotionV1 supplied_no_jump{0.3, 0.4, false, 0.9};
  static_cast<void>(player.fixed_update(
      world, command(tick, 0, 0, game_button_mask_v1(GameButtonV1::jump)),
      supplied_no_jump));
  expect(!player.snapshot().character.grounded &&
             player.snapshot().character.velocity.z > 0.0,
         "the authoritative jump button did not override supplied motion");
}

void test_explicit_motion_invalid_speed_is_atomic() {
  using namespace openrc::game;

  auto exact_profile = player_profile(-100.0);
  exact_profile.character.maximum_ground_speed = 5.7;
  const auto world = empty_world();
  PlayerSimulationV1 player(exact_profile, checkpoint());
  const auto before = player.snapshot();

  const auto expect_invalid_motion = [&](const CharacterMotionV1 motion,
                                         const std::string &message) {
    expect_player_error(
        [&] {
          static_cast<void>(player.fixed_update(world, command(0U), motion));
        },
        message);
    expect(player.snapshot() == before,
           "a rejected motion override partially mutated player state");
  };
  expect_invalid_motion(
      {1.0, 0.0, false, std::numeric_limits<double>::infinity()},
      "a player override accepted a non-finite target speed");
  expect_invalid_motion({1.0, 0.0, false, -0.01},
                        "a player override accepted a negative target speed");
  expect_invalid_motion({1.0, 0.0, false, 5.700001},
                        "a player override accepted an excessive target speed");
  expect_invalid_motion({0.0, 0.0, false, 0.9},
                        "a player override accepted speed without direction");

  static_cast<void>(
      player.fixed_update(world, command(0U), {1.0, 0.0, false, 0.9}));
  expect(player.snapshot().next_tick_index == 1U,
         "a rejected override consumed the fixed-tick sequence");
}

void test_validation_and_reset_overflow_atomicity() {
  using namespace openrc::game;

  expect_player_error(
      [&] {
        static_cast<void>(
            PlayerSimulationV1(player_profile(3.0), checkpoint()));
      },
      "a checkpoint below the authored death plane was accepted");

  auto invalid_snapshot = PlayerSimulationSnapshotV1{};
  invalid_snapshot.checkpoint = checkpoint();
  invalid_snapshot.last_reset_reason = static_cast<PlayerResetReasonV1>(255U);
  expect_player_error(
      [&] { validate_player_simulation_snapshot_v1(invalid_snapshot); },
      "an unknown reset reason was accepted in replay state");

  PlayerSimulationSnapshotV1 exhausted;
  exhausted.character.feet_position = {0.0, 0.0, 0.001};
  exhausted.character.velocity.z = -1.0;
  exhausted.checkpoint = checkpoint();
  exhausted.facing_yaw_radians = checkpoint().facing_yaw_radians;
  exhausted.reset_count = std::numeric_limits<std::uint64_t>::max();
  PlayerSimulationV1 player(player_profile(0.0), exhausted);
  const auto before = player.snapshot();
  const auto world = empty_world();
  expect_player_error(
      [&] { static_cast<void>(player.fixed_update(world, command(0U))); },
      "a death reset beyond the explicit counter domain was accepted");
  expect(player.snapshot() == before,
         "a failed death reset partially mutated player replay state");
}

[[nodiscard]] std::uint32_t float_bits(const float value) {
  return std::bit_cast<std::uint32_t>(value);
}

struct RacJumpTrajectorySummary {
  std::uint32_t peak_frame = 0U;
  float peak_height = 0.0F;
  std::uint32_t apex_frame = 0U;
  std::uint32_t landing_frame = 0U;
  std::uint32_t terminal_frame = 0U;
  float terminal_velocity = 0.0F;
};

// Free-flight harness for the recovered vertical axis. The feedback is test
// scaffolding, not source behavior: while the takeoff window holds the player
// on the ground the measured displacement is zero, afterwards it equals the
// previous frame's vertical velocity (no collision response).
[[nodiscard]] RacJumpTrajectorySummary
run_rac_jump(const std::uint32_t ramp_frames, const std::uint32_t hold_frames,
             std::vector<openrc::game::RacJumpVerticalStateV1> *frames) {
  using namespace openrc::game;
  const RacJumpVerticalProfileV1 profile{kRacJumpTakeoffFramesPalV1,
                                         ramp_frames};
  auto state = enter_rac_jump_vertical_v1();
  RacJumpTrajectorySummary summary;
  float height = 0.0F;
  float previous = 0.0F;
  bool landed = false;
  for (std::uint32_t frame = 0U; frame < 140U; ++frame) {
    RacJumpVerticalInputV1 input;
    input.jump_held = frame < hold_frames;
    input.measured_vertical_displacement =
        frame <= kRacJumpTakeoffFramesPalV1 ? 0.0F : previous;
    const bool apex_before = state.apex_reached;
    state = step_rac_jump_vertical_v1(state, profile, input);
    if (frames != nullptr) {
      frames->push_back(state);
    }
    if (!apex_before && state.apex_reached) {
      summary.apex_frame = frame;
    }
    if (frame >= kRacJumpTakeoffFramesPalV1) {
      height = height + state.vertical_velocity;
      if (height > summary.peak_height) {
        summary.peak_height = height;
        summary.peak_frame = frame;
      }
      if (!landed && frame > kRacJumpTakeoffFramesPalV1 && height <= 0.0F) {
        landed = true;
        summary.landing_frame = frame;
      }
    }
    if (summary.terminal_frame == 0U &&
        float_bits(state.vertical_velocity) == 0xbf800001U) {
      summary.terminal_frame = frame;
      summary.terminal_velocity = state.vertical_velocity;
    }
    previous = state.vertical_velocity;
  }
  return summary;
}

void test_rac_jump_entry_and_takeoff_window() {
  using namespace openrc::game;

  const auto entry = enter_rac_jump_vertical_v1();
  expect(float_bits(entry.gravity) == 0x3c42a456U &&
             float_bits(entry.apex_gravity) == 0x3c63bb27U &&
             float_bits(entry.target_height) == 0x3fbc28f6U &&
             entry.pending_impulse == 0.0F && entry.applied_impulse == 0.0F &&
             !entry.apex_reached && entry.frames_in_state == 0U,
         "state-7 entry did not reproduce the 0x224d1c vertical writes");

  // 0x224d68..0x224d78 copies the measured displacement +0x110 into +0xe0:
  // the entry velocity is the measured value, whatever was commanded before.
  const auto measured_entry = enter_rac_jump_vertical_v1(-0.0375F);
  expect(float_bits(measured_entry.vertical_velocity) ==
                 float_bits(-0.0375F) &&
             measured_entry.gravity == entry.gravity &&
             measured_entry.pending_impulse == 0.0F,
         "state-7 entry did not copy +0x110.z into +0xe0.z");
  RacJumpVerticalInputV1 takeoff_input;
  takeoff_input.jump_held = true;
  const auto first_takeoff = step_rac_jump_vertical_v1(
      measured_entry, RacJumpVerticalProfileV1{kRacJumpTakeoffFramesPalV1, 12U},
      takeoff_input);
  expect(float_bits(first_takeoff.vertical_velocity) == 0xbc9d4952U,
         "the takeoff window did not replace the measured entry velocity");
  expect_player_error(
      [] {
        static_cast<void>(enter_rac_jump_vertical_v1(
            std::numeric_limits<float>::infinity()));
      },
      "a non-finite measured entry displacement was accepted");

  std::vector<RacJumpVerticalStateV1> frames;
  static_cast<void>(run_rac_jump(12U, 1000U, &frames));
  for (std::uint32_t frame = 0U; frame < kRacJumpTakeoffFramesPalV1; ++frame) {
    expect(float_bits(frames[frame].vertical_velocity) == 0xbc9d4952U &&
               frames[frame].applied_impulse == 0.0F &&
               frames[frame].frames_in_state == frame + 1U,
           "the takeoff window did not press down by 48 * dt^2");
  }
  expect(float_bits(frames[0U].pending_impulse) == 0x3e45835cU &&
             float_bits(frames[0U].target_height) == 0x3fc86d3aU &&
             float_bits(frames[3U].pending_impulse) == 0x3e56e1c8U,
         "the held takeoff did not ramp sqrt((h + h) * g)");
  expect(float_bits(frames[4U].vertical_velocity) == 0x3e3c8aaaU &&
             float_bits(frames[4U].applied_impulse) == 0x3e5c5e19U &&
             frames[4U].pending_impulse == 0.0F,
         "the first airborne frame did not apply the pending impulse");
  expect(float_bits(frames[11U].target_height) == 0x4027ae13U &&
             float_bits(frames[12U].target_height) == 0x4027ae14U &&
             float_bits(frames[12U].applied_impulse) == 0x3e7f7d6eU &&
             float_bits(frames[13U].applied_impulse) == 0x3e7f7d6eU,
         "the height ramp did not clamp to 2.62 at frames(15)");
}

void test_rac_jump_trajectory_peak_flight_and_terminal_speed() {
  const auto held12 = run_rac_jump(12U, 1000U, nullptr);
  expect(held12.peak_frame == 22U &&
             float_bits(held12.peak_height) == 0x3ffdfb56U &&
             held12.apex_frame == 24U && held12.landing_frame == 39U,
         "the held ramp-12 jump left the recovered trajectory");
  const auto held13 = run_rac_jump(13U, 1000U, nullptr);
  expect(held13.peak_frame == 22U &&
             float_bits(held13.peak_height) == 0x3ffab7e6U &&
             held13.landing_frame == 39U,
         "the held ramp-13 jump left the recovered trajectory");
  const auto tap12 = run_rac_jump(12U, 1U, nullptr);
  expect(tap12.peak_frame == 17U &&
             float_bits(tap12.peak_height) == 0x3f97930bU &&
             tap12.landing_frame == 31U,
         "a tapped ramp-12 jump did not stop ramping on release");
  const auto tap13 = run_rac_jump(13U, 1U, nullptr);
  expect(tap13.peak_frame == 17U &&
             float_bits(tap13.peak_height) == 0x3f96c281U,
         "a tapped ramp-13 jump did not stop ramping on release");
  expect(held12.terminal_frame == 95U && tap12.terminal_frame == 90U &&
             float_bits(held12.terminal_velocity) == 0xbf800001U,
         "the fall did not clamp to -(dt * 50)");
}

void test_rac_jump_measured_clamp_and_validation() {
  using namespace openrc::game;
  const RacJumpVerticalProfileV1 profile{kRacJumpTakeoffFramesPalV1, 13U};

  auto airborne = enter_rac_jump_vertical_v1(0.0005F);
  airborne.applied_impulse = 0.25F;
  airborne.frames_in_state = 30U;
  RacJumpVerticalInputV1 input;
  input.measured_vertical_displacement = 0.5F;
  const auto clamped = step_rac_jump_vertical_v1(airborne, profile, input);
  expect(clamped.vertical_velocity == 0.5F - 0.1F && clamped.apex_reached &&
             clamped.gravity == airborne.apex_gravity,
         "vertical velocity fell more than 0.1 below the measured motion");

  airborne.vertical_velocity = -0.999F;
  input.measured_vertical_displacement = -2.0F;
  const auto terminal = step_rac_jump_vertical_v1(airborne, profile, input);
  expect(float_bits(terminal.vertical_velocity) == 0xbf800001U,
         "the terminal clamp did not use the PAL frame step");

  expect_player_error(
      [&] {
        static_cast<void>(step_rac_jump_vertical_v1(
            airborne, RacJumpVerticalProfileV1{}, input));
      },
      "the ambiguous frames(15) ramp length was defaulted silently");
  expect_player_error(
      [&] {
        static_cast<void>(step_rac_jump_vertical_v1(
            airborne, RacJumpVerticalProfileV1{0U, 13U}, input));
      },
      "a zero takeoff window was accepted");
  auto invalid = airborne;
  invalid.gravity = std::numeric_limits<float>::infinity();
  expect_player_error(
      [&] {
        static_cast<void>(step_rac_jump_vertical_v1(invalid, profile, input));
      },
      "a non-finite jump state was accepted");
}

void test_rac_fall_vertical_and_long_phase() {
  using namespace openrc::game;

  float velocity = 0.0F;
  float height = 0.0F;
  std::uint32_t terminal_frame = 0U;
  for (std::uint32_t frame = 0U; frame < 130U; ++frame) {
    velocity = step_rac_fall_vertical_v1(velocity);
    height = height + velocity;
    if (frame == 0U) {
      expect(float_bits(velocity) == 0xbc1d4952U,
             "the first fall frame did not subtract 24 * dt^2");
    }
    if (frame == 5U) {
      expect(float_bits(velocity) == 0xbd6bedfaU &&
                 land_rac_fall_vertical_v1(velocity) == velocity,
             "a slow landing changed the vertical velocity");
    }
    if (frame == 15U) {
      expect(float_bits(velocity) == 0xbe1d4951U &&
                 float_bits(height) == 0xbfa71de6U,
             "the fall left the recovered arithmetic");
    }
    if (frame == 40U) {
      expect(float_bits(land_rac_fall_vertical_v1(velocity)) == 0xbe3851ecU,
             "a fast landing was not raised to dt * -9");
    }
    if (terminal_frame == 0U && float_bits(velocity) == 0xbf800001U) {
      terminal_frame = frame;
    }
  }
  expect(terminal_frame == 104U && float_bits(velocity) == 0xbf800001U,
         "the fall did not hold the -(dt * 50) terminal speed");

  expect(!rac_fall_enters_long_phase_v1(14U, 1.75F) &&
             rac_fall_enters_long_phase_v1(15U, 0.0F) &&
             rac_fall_enters_long_phase_v1(0U, std::nextafter(1.75F, 2.0F)),
         "the slot-11 phase switch left frames(18) / 1.75");
  expect_player_error(
      [] { static_cast<void>(step_rac_fall_vertical_v1(
               std::numeric_limits<float>::quiet_NaN())); },
      "a non-finite fall velocity was accepted");
}

void test_rac_fall_landing_selection() {
  using namespace openrc::game;

  RacFallLandingInputV1 input;
  input.frames_in_state = 20U;
  input.previous_stick_magnitude = 0.6F;
  auto landing = select_rac_fall_landing_v1(input);
  expect(landing.next_state == 2U && landing.entry_selects_sequence &&
             !landing.sequence_selected && !landing.input_lock_written,
         "a short fall with the stick pushed did not continue walking");

  input.previous_stick_magnitude = 0.5F;
  input.planar_speed = 0.0601F;
  landing = select_rac_fall_landing_v1(input);
  expect(landing.next_state == 3U && landing.sequence_selected &&
             landing.sequence_slot == 6U &&
             landing.sequence_blend_frames == 10.0F &&
             !landing.entry_selects_sequence,
         "a fast short landing did not skid into slot 6");

  input.planar_speed = 0.06F;
  landing = select_rac_fall_landing_v1(input);
  expect(landing.next_state == 0U && landing.entry_selects_sequence &&
             !landing.sequence_selected,
         "a slow short landing did not return to idle");

  input.previous_stick_magnitude = 1.0F;
  input.input_lock_frames = 3U;
  landing = select_rac_fall_landing_v1(input);
  expect(landing.next_state == 0U,
         "an input lock did not suppress the walk exit");

  input.long_phase = true;
  input.lock_extension_allowed = true;
  input.frames_in_state = 42U;
  landing = select_rac_fall_landing_v1(input);
  expect(landing.next_state == 0U && landing.sequence_slot == 12U &&
             landing.sequence_argument == 9U &&
             landing.sequence_blend_frames == -1.0F &&
             landing.input_lock_frames == kRacFallLongLockFramesPalV1,
         "the slot-11 landing left the recovered lock");
  // set_state(0, 0) zeroes +0x198 at 0x227db0 before 0x22e7bc reads it, so
  // the frames(50) extension cannot fire on an accepted transition.
  input.frames_in_state = 74U;
  expect(select_rac_fall_landing_v1(input).input_lock_frames ==
             kRacFallLongLockFramesPalV1,
         "the slot-11 landing used the pre-set_state frame counter");
  input.state_is_45 = true;
  input.lock_extension_allowed = false;
  expect(select_rac_fall_landing_v1(input).input_lock_frames ==
             kRacFallLongLockFromState45PalV1,
         "state 45 did not use the frames(10) lock");

  input.frames_in_state = 75U;
  input.lock_extension_allowed = false;
  landing = select_rac_fall_landing_v1(input);
  expect(landing.next_state == 0U && landing.sequence_slot == 12U &&
             landing.sequence_argument == 4U &&
             landing.sequence_blend_frames == 5.0F &&
             !landing.input_lock_written,
         "a frames(90) fall did not take the hard landing");
  input.lock_extension_allowed = true;
  landing = select_rac_fall_landing_v1(input);
  expect(landing.input_lock_written &&
             landing.input_lock_frames == kRacFallHardLockFramesPalV1,
         "the hard landing lock was not frames(22)");

  input.hit_points_positive = false;
  expect(select_rac_fall_landing_v1(input).next_state == 61U,
         "a landing without hit points did not leave through state 61");
}

void test_default_player_profile_is_unchanged_by_jump_model() {
  using namespace openrc::game;
  // The recovered jump model is separate: PlayerSimulationProfileV1 keeps its
  // three-field aggregate and no jump state leaks into the snapshot.
  const PlayerSimulationProfileV1 defaulted{};
  expect(defaulted.fixed_ticks_per_second == 0U &&
             defaulted.death_height_world == 0.0,
         "the default player simulation profile changed");
  static_assert(sizeof(PlayerSimulationSnapshotV1) ==
                    sizeof(CharacterControllerStateV1) +
                        sizeof(PlayerCheckpointV1) + sizeof(double) +
                        2U * sizeof(std::uint64_t) + 8U,
                "the replay snapshot layout changed");
}

} // namespace

int main() {
  try {
    test_rac_jump_entry_and_takeoff_window();
    test_rac_jump_trajectory_peak_flight_and_terminal_speed();
    test_rac_jump_measured_clamp_and_validation();
    test_rac_fall_vertical_and_long_phase();
    test_rac_fall_landing_selection();
    test_default_player_profile_is_unchanged_by_jump_model();
    test_absolute_death_height_reset();
    test_manual_and_checkpoint_activation_reset();
    test_tick_order_snapshot_and_deterministic_hash();
    test_partial_analog_magnitude_reaches_character_motion();
    test_explicit_motion_override_and_jump_authority();
    test_explicit_motion_invalid_speed_is_atomic();
    test_validation_and_reset_overflow_atomicity();
    std::cout << "player_simulation_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "player_simulation_tests: " << error.what() << '\n';
    return 1;
  }
}
