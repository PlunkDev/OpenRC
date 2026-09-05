#include "openrc/player_simulation.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr openrc::CollisionWorldBuildLimitsV1 kBuildLimits{
    1024U, 1024U, 16'384U, 65'536U, openrc::kCollisionDefaultGridCellSizeQ6V1,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
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

} // namespace

int main() {
  try {
    test_absolute_death_height_reset();
    test_manual_and_checkpoint_activation_reset();
    test_tick_order_snapshot_and_deterministic_hash();
    test_validation_and_reset_overflow_atomicity();
    std::cout << "player_simulation_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "player_simulation_tests: " << error.what() << '\n';
    return 1;
  }
}
