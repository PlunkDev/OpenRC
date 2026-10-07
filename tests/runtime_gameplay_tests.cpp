#include "openrc/runtime_gameplay.hpp"

#include "openrc/rac_pad_input.hpp"
#include "openrc/rac_player_locomotion.hpp"

#include <array>
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

constexpr openrc::CollisionWorldBuildLimitsV1 kWorldLimits{
    1024U, 1024U, 16'384U, 65'536U, openrc::kCollisionDefaultGridCellSizeQ6V1,
};

constexpr auto kEntityGameplayLimits =
    openrc::game::make_runtime_entity_gameplay_limits_v1();

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
void expect_gameplay_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::RuntimeGameplayError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::CollisionWorldV1 floor_world() {
  openrc::CollisionMeshV1 mesh;
  mesh.vertices = {
      {-4096, -4096, 0},
      {4096, -4096, 0},
      {4096, 4096, 0},
      {-4096, 4096, 0},
  };
  mesh.triangles = {
      {{0U, 1U, 2U}, {}, openrc::CollisionLayerV1::world},
      {{0U, 2U, 3U}, {}, openrc::CollisionLayerV1::world},
  };
  return openrc::build_collision_world_v1(std::move(mesh), kWorldLimits);
}

[[nodiscard]] openrc::CollisionWorldV1 empty_world() {
  return openrc::build_collision_world_v1({}, kWorldLimits);
}

[[nodiscard]] openrc::game::RuntimeLevelFoundationV1
foundation(const std::uint32_t level_id = 7U,
           const std::uint32_t spawn_id = 10U,
           const openrc::CollisionVectorV1 spawn = {0.0, 0.0, 0.0},
           const double death_height = -5.0,
           openrc::CollisionWorldV1 collision_world = floor_world()) {
  openrc::game::RuntimeLevelFoundationV1 result;
  result.level_id = level_id;
  result.content_api_version = 1U;
  result.build_id = "runtime-gameplay-test";
  result.collision_world = std::move(collision_world);
  result.bootstrap.level_id = level_id;
  result.bootstrap.death_height_world = death_height;
  result.bootstrap.default_spawn_id = spawn_id;
  result.bootstrap.spawn_points = {
      {spawn_id, spawn, 0.0},
      {spawn_id + 1U, {spawn.x + 2.0, spawn.y - 1.0, spawn.z + 1.0}, 0.5},
  };
  return result;
}

[[nodiscard]] openrc::game::GameInputSampleV1
movement_sample(const std::int16_t x, const std::int16_t y,
                const std::uint32_t held_buttons = 0U) {
  openrc::game::GameInputSampleV1 result;
  result.axes.move_x = x;
  result.axes.move_y = y;
  result.held_buttons = held_buttons;
  return result;
}

[[nodiscard]] openrc::game::RuntimeGameplayEntityContentV1
entity_gameplay_content(const std::uint32_t level_id = 7U) {
  using namespace openrc;

  EntityDefinitionV1 later;
  later.authored_id = 40U;
  later.archetype_key = "openrc.collectible/bolt";
  EntityDefinitionV1 earlier;
  earlier.authored_id = 20U;
  earlier.archetype_key = "openrc.collectible/bolt";

  EntityTransformComponentV1 later_transform;
  later_transform.authored_id = later.authored_id;
  later_transform.transform.position = {-0.25F, 0.0F, 0.75F};
  EntityTransformComponentV1 earlier_transform;
  earlier_transform.authored_id = earlier.authored_id;
  earlier_transform.transform.position = {0.25F, 0.0F, 0.75F};

  EntitySceneV1 entities;
  entities.level_id = level_id;
  entities.definitions = {later, earlier};
  entities.transforms = {later_transform, earlier_transform};

  GameplaySceneV1 gameplay;
  gameplay.level_id = level_id;
  gameplay.collectibles = {
      GameplayCollectibleV1{later.authored_id, "items/bolts", {}, 7U, 0.4F, 0U},
      GameplayCollectibleV1{
          earlier.authored_id, "items/bolts", {}, 5U, 0.4F, 0U},
  };

  return {
      canonicalize_entity_scene_v1(std::move(entities),
                                   kEntityGameplayLimits.entity_scene),
      canonicalize_gameplay_scene_v1(std::move(gameplay),
                                     kEntityGameplayLimits.gameplay_scene),
      kEntityGameplayLimits,
      std::nullopt,
  };
}

[[nodiscard]] openrc::game::RuntimeGameplayEntityContentV1
movement_gated_entity_gameplay_content() {
  using namespace openrc;

  EntityDefinitionV1 definition;
  definition.authored_id = 90U;
  definition.archetype_key = "openrc.collectible/movement-gate";
  EntityTransformComponentV1 transform;
  transform.authored_id = definition.authored_id;
  // The stationary capsule misses this tiny sphere. One full-strength first
  // tick moves the player just far enough for overlap, detecting tick order.
  transform.transform.position = {0.358F, 0.0F, 0.37F};

  EntitySceneV1 entities;
  entities.level_id = 7U;
  entities.definitions = {definition};
  entities.transforms = {transform};

  GameplaySceneV1 gameplay;
  gameplay.level_id = 7U;
  gameplay.collectibles = {GameplayCollectibleV1{
      definition.authored_id, "items/movement-gate", {}, 1U, 0.0001F, 0U}};

  return {
      canonicalize_entity_scene_v1(std::move(entities),
                                   kEntityGameplayLimits.entity_scene),
      canonicalize_gameplay_scene_v1(std::move(gameplay),
                                     kEntityGameplayLimits.gameplay_scene),
      kEntityGameplayLimits,
      std::nullopt,
  };
}

[[nodiscard]] openrc::game::RuntimeGameplayEntityContentV1
wrench_destructible_entity_gameplay_content() {
  using namespace openrc;

  EntityDefinitionV1 definition;
  definition.authored_id = 10U;
  definition.archetype_key = "openrc.destructible/wrench-target";
  definition.flags = kEntityDefinitionInitiallyEnabledV1;

  EntityTransformComponentV1 transform;
  transform.authored_id = definition.authored_id;
  // The production wrench pulse travels along +X at this height.
  transform.transform.position = {1.0F, 0.0F, 0.85F};

  EntitySceneV1 entities;
  entities.level_id = 7U;
  entities.definitions = {definition};
  entities.transforms = {transform};

  GameplaySceneV1 gameplay;
  gameplay.level_id = 7U;

  DestructibleDefinitionV1 destructible;
  destructible.authored_id = definition.authored_id;
  destructible.max_health = 1U;
  destructible.accepted_damage_channels = game::kDamageChannelMeleeV1;
  destructible.hit_radius = 0.25F;
  destructible.drops = {{"openrc.currency/bolts", 1U, 0U}};

  DestructibleSceneV1 destructibles;
  destructibles.level_id = 7U;
  destructibles.destructibles = {destructible};

  return {
      canonicalize_entity_scene_v1(std::move(entities),
                                   kEntityGameplayLimits.entity_scene),
      canonicalize_gameplay_scene_v1(std::move(gameplay),
                                     kEntityGameplayLimits.gameplay_scene),
      kEntityGameplayLimits,
      canonicalize_destructible_scene_v1(
          std::move(destructibles), kEntityGameplayLimits.destructible_scene),
  };
}

void assert_tick_invariants(
    const openrc::game::RuntimeGameplaySnapshotV1 &snapshot,
    const std::uint64_t expected_tick) {
  expect(snapshot.session.next_tick_index == expected_tick &&
             snapshot.player.next_tick_index == expected_tick &&
             snapshot.input_next_tick_index == expected_tick &&
             snapshot.fixed_step_next_tick_index == expected_tick &&
             snapshot.combat.next_tick_index == expected_tick &&
             (!snapshot.entity_gameplay ||
              snapshot.entity_gameplay->next_tick_index == expected_tick),
         "runtime gameplay tick owners diverged");
}

void test_spawn_session_and_production_profile() {
  using namespace openrc::game;

  RuntimeGameplaySessionOptionsV1 options;
  options.deterministic_seed = UINT64_C(0x123456789abcdef0);
  options.spawn_point_id = 11U;
  RuntimeGameplaySessionV1 runtime(foundation(), options);
  const auto state = runtime.snapshot();
  const auto expected_profile = make_runtime_gameplay_profile_v1();

  expect(runtime.profile() == expected_profile &&
             expected_profile.fixed_step ==
                 FixedStepConfigV1{60U, 8U, 250'000'000U},
         "the shared production gameplay profile changed unexpectedly");
  expect(state.session.deterministic_seed == options.deterministic_seed &&
             state.session.active_level_id == 7U &&
             state.session.active_spawn_point_id == 11U &&
             state.session.level_instance_sequence == 1U &&
             state.active_level == ActiveLevelV1{7U, 11U, 1U},
         "initial level request was not committed into session and world");
  expect(state.player.checkpoint.checkpoint_id == 11U &&
             state.player.character.feet_position ==
                 openrc::CollisionVectorV1{2.0, -1.0, 1.0} &&
             runtime.world().entity_count() == 0U,
         "authored spawn was not used or a synthetic player entity leaked in");
  assert_tick_invariants(state, 0U);

  auto invalid_profile = expected_profile;
  invalid_profile.fixed_step.max_steps_per_advance =
      kRuntimeGameplayMaximumStepsPerAdvanceV1 + 1U;
  expect_gameplay_error(
      [&] { validate_runtime_gameplay_profile_v1(invalid_profile); },
      "an unbounded per-frame gameplay catch-up policy was accepted");

  invalid_profile = expected_profile;
  invalid_profile.fixed_step.ticks_per_second = 1U;
  expect_gameplay_error(
      [&] { validate_runtime_gameplay_profile_v1(invalid_profile); },
      "a tick/controller combination exceeding the motion budget was accepted");

  auto low_but_safe_rate = expected_profile;
  low_but_safe_rate.fixed_step.ticks_per_second = 4U;
  validate_runtime_gameplay_profile_v1(low_but_safe_rate);
}

[[nodiscard]] openrc::game::RuntimeGameplaySnapshotV1
run_partition(const std::vector<std::uint64_t> &frame_times) {
  using namespace openrc::game;

  RuntimeGameplaySessionV1 runtime(foundation());
  runtime.submit_input_sample(movement_sample(kGameInputAxisMagnitudeV1, 0));
  std::uint64_t expected_tick = 0U;
  for (const auto elapsed : frame_times) {
    const auto frame = runtime.advance_frame(elapsed);
    expect(frame.fixed_step.first_tick_index == expected_tick &&
               frame.ticks.size() == frame.fixed_step.step_count,
           "a frame returned the wrong replay tick range");
    for (std::size_t index = 0U; index < frame.ticks.size(); ++index) {
      expect(frame.ticks[index].input.tick_index == expected_tick + index &&
                 frame.ticks[index].player_snapshot.next_tick_index ==
                     expected_tick + index + 1U,
             "per-tick gameplay output or player snapshot is not in replay "
             "order");
    }
    if (!frame.ticks.empty()) {
      expect(frame.ticks.back().player_snapshot == frame.snapshot.player,
             "the final per-tick player snapshot disagrees with the frame");
    }
    expected_tick += frame.fixed_step.step_count;
    assert_tick_invariants(frame.snapshot, expected_tick);
  }
  return runtime.snapshot();
}

void test_determinism_across_frame_partitions() {
  std::vector<std::uint64_t> sixty_frames(60U, 16'666'666U);
  sixty_frames.back() += 40U;
  const std::vector<std::uint64_t> ten_frames(10U, 100'000'000U);

  const auto first = run_partition(sixty_frames);
  const auto second = run_partition(ten_frames);
  expect(first == second && first.session.next_tick_index == 60U &&
             first.interpolation_numerator == 0U &&
             first.total_dropped_step_count == 0U,
         "one second of identical input changed with render-frame partition");
}

void test_per_tick_movement_mapping_and_atomic_failure() {
  using namespace openrc::game;

  RuntimeGameplaySessionV1 runtime(foundation());
  const auto sample = movement_sample(kGameInputAxisMagnitudeV1, 0);
  std::uint32_t mapper_calls = 0U;
  const RuntimeMovementMapperV1 rotate_to_y =
      [&mapper_calls](const GameInputCommandV1 &input,
                      const RuntimeMovementAxesV1 source_movement,
                      const double fixed_delta_seconds) {
        expect(input.tick_index == mapper_calls,
               "movement mapper observed an unexpected fixed-tick index");
        expect(fixed_delta_seconds == 1.0 / 60.0,
               "movement mapper received a different delta than the player");
        ++mapper_calls;
        return RuntimeMovementAxesV1{0.0, source_movement.move_x};
      };
  const auto frame = runtime.advance_frame(50'000'000U, sample, rotate_to_y);
  expect(frame.ticks.size() == 3U && mapper_calls == 3U,
         "movement mapper was not invoked exactly once per fixed tick");
  for (const auto &tick : frame.ticks) {
    expect(tick.input.axes.move_x == 0 &&
               tick.input.axes.move_y == kGameInputAxisMagnitudeV1,
           "camera-relative movement was not recorded in world axes");
  }
  expect(frame.snapshot.player.character.feet_position.y > 0.0 &&
             std::abs(frame.snapshot.player.character.feet_position.x) < 1.0e-9,
         "mapped world-space movement did not reach player simulation");

  RuntimeGameplaySessionV1 identity_runtime(foundation());
  const auto identity = identity_runtime.advance_frame(16'666'667U, sample);
  expect(identity.ticks.size() == 1U &&
             identity.ticks[0U].input.axes.move_x ==
                 kGameInputAxisMagnitudeV1 &&
             identity.ticks[0U].input.axes.move_y == 0,
         "an empty movement mapper did not preserve world-space input");

  const auto before_failure = runtime.snapshot();
  const RuntimeMovementMapperV1 invalid_mapper = [](const GameInputCommandV1 &,
                                                    const RuntimeMovementAxesV1,
                                                    const double) {
    return RuntimeMovementAxesV1{std::numeric_limits<double>::quiet_NaN(), 0.0};
  };
  expect_gameplay_error(
      [&] {
        static_cast<void>(runtime.advance_frame(16'666'667U, invalid_mapper));
      },
      "invalid mapped movement entered the replay stream");
  expect(runtime.snapshot() == before_failure,
         "a failed mapped frame partially mutated gameplay state");
}

[[nodiscard]] openrc::game::RuntimeGameplaySessionOptionsV1
exact_motion_test_options() {
  auto options = openrc::game::RuntimeGameplaySessionOptionsV1{};
  options.profile.character.ground_acceleration = 1'000.0;
  options.profile.character.ground_deceleration = 1'000.0;
  options.profile.character.air_acceleration = 1'000.0;
  return options;
}

void settle_runtime_player_on_floor(
    openrc::game::RuntimeGameplaySessionV1 &runtime) {
  for (std::uint32_t tick = 0U;
       tick < 8U && !runtime.snapshot().player.character.grounded; ++tick) {
    static_cast<void>(runtime.advance_frame(16'666'667U));
  }
  expect(runtime.snapshot().player.character.grounded,
         "the exact locomotion test player did not settle on the floor");
}

void test_source_ground_paces_are_exact_without_a_frontend_mapper() {
  using namespace openrc::game;

  // These canonical axes land on adjacent source byte samples: 62/76 is
  // below the strict 0.82 boundary and 63/76 is above it.
  constexpr std::int16_t kBelowFastThreshold = 28'159;
  constexpr std::int16_t kAboveFastThreshold = 28'415;
  const auto below_axes = decode_rac_pad_axes_response_v1(
      movement_sample(kBelowFastThreshold, 0).axes);
  const auto above_axes = decode_rac_pad_axes_response_v1(
      movement_sample(kAboveFastThreshold, 0).axes);
  expect(below_axes.move_x < kRacPlayerStandardFastPaceThresholdV1 &&
             above_axes.move_x >= kRacPlayerStandardFastPaceThresholdV1,
         "the threshold fixture does not straddle the recovered 0.82 rule");

  RuntimeGameplaySessionV1 runtime(foundation(), exact_motion_test_options());
  settle_runtime_player_on_floor(runtime);

  const auto slow = runtime.advance_frame(
      16'666'667U, movement_sample(kBelowFastThreshold, 0));
  expect(slow.ticks.size() == 1U &&
             slow.ticks[0U].input.axes.move_x ==
                 apply_rac_pad_axis_response_v1(kBelowFastThreshold),
         "headless runtime did not record the recovered pad response");
  expect(
      slow.ticks[0U].source_standard_ground_movement.pace ==
              RacPlayerGroundPaceV1::slow &&
          slow.ticks[0U]
                  .source_standard_ground_movement.source_input_magnitude ==
              below_axes.move_x &&
          slow.ticks[0U].source_standard_ground_movement.target_ground_speed ==
              kRacPlayerStandardSlowGroundSpeedV1,
      "the tick lost its unquantized source locomotion descriptor");
  expect_near(
      std::hypot(slow.snapshot.player.character.velocity.x,
                 slow.snapshot.player.character.velocity.y),
      static_cast<double>(kRacPlayerStandardSlowGroundSpeedV1), 1.0e-12,
      "light grounded analog did not reach the exact 0.9 source target");

  const auto fast = runtime.advance_frame(
      16'666'667U, movement_sample(kAboveFastThreshold, 0));
  expect_near(
      std::hypot(fast.snapshot.player.character.velocity.x,
                 fast.snapshot.player.character.velocity.y),
      static_cast<double>(kRacPlayerStandardFastGroundSpeedV1), 1.0e-12,
      "grounded analog above 0.82 did not reach the exact 5.7 source target");
  expect(fast.ticks[0U].source_standard_ground_movement.pace ==
             RacPlayerGroundPaceV1::fast,
         "the tick did not expose the fast source locomotion pace");
}

void test_ground_pace_survives_double_precision_camera_rotation() {
  using namespace openrc::game;

  RuntimeGameplaySessionV1 runtime(foundation(), exact_motion_test_options());
  settle_runtime_player_on_floor(runtime);
  constexpr std::int16_t kAboveFastThreshold = 28'415;
  const RuntimeMovementMapperV1 quarter_turn =
      [](const GameInputCommandV1 &, const RuntimeMovementAxesV1 source,
         const double) {
        return RuntimeMovementAxesV1{-source.move_y, source.move_x};
      };
  const auto frame = runtime.advance_frame(
      16'666'667U, movement_sample(kAboveFastThreshold, 0), quarter_turn);
  const auto &velocity = frame.snapshot.player.character.velocity;
  expect_near(velocity.x, 0.0, 1.0e-12,
              "camera rotation left horizontal speed on source X");
  expect_near(velocity.y,
              static_cast<double>(kRacPlayerStandardFastGroundSpeedV1), 1.0e-12,
              "camera rotation lost the exact grounded target speed");
  expect(frame.ticks.size() == 1U && frame.ticks[0U].input.axes.move_x == 0 &&
             frame.ticks[0U].input.axes.move_y ==
                 apply_rac_pad_axis_response_v1(kAboveFastThreshold),
         "the replay command did not retain rotated source-stick magnitude");
}

void test_airborne_input_remains_proportional_to_source_response() {
  using namespace openrc::game;

  constexpr std::int16_t kLightAxis = 13'000;
  const auto source_axes =
      decode_rac_pad_axes_response_v1(movement_sample(kLightAxis, 0).axes);
  expect(source_axes.move_x > 0.0F &&
             source_axes.move_x < kRacPlayerStandardFastPaceThresholdV1,
         "the airborne fixture is not a light nonzero source input");

  RuntimeGameplaySessionV1 runtime(
      foundation(7U, 10U, {0.0, 0.0, 2.0}, -100.0, empty_world()),
      exact_motion_test_options());
  const auto frame =
      runtime.advance_frame(16'666'667U, movement_sample(kLightAxis, 0));
  const auto expected_speed = static_cast<double>(source_axes.move_x) *
                              runtime.profile().character.maximum_ground_speed;
  expect(!frame.snapshot.player.character.grounded,
         "the airborne proportional-input fixture unexpectedly grounded");
  expect_near(frame.snapshot.player.character.velocity.x, expected_speed,
              1.0e-12,
              "airborne input was incorrectly replaced by a ground pace");
}

void test_staged_frame_snapshot_matches_committed_state() {
  using namespace openrc::game;

  RuntimeGameplaySessionOptionsV1 options;
  options.entity_gameplay = entity_gameplay_content();
  RuntimeGameplaySessionV1 runtime(foundation(), options);

  const auto ticked = runtime.advance_frame(16'666'667U);
  expect(ticked.fixed_step.step_count == 1U &&
             ticked.snapshot == runtime.snapshot(),
         "a staged tick snapshot differs from the committed runtime state");

  const auto jump_mask = game_button_mask_v1(GameButtonV1::jump);
  const auto sampled_without_tick =
      runtime.advance_frame(0U, movement_sample(0, 0, jump_mask));
  expect(sampled_without_tick.fixed_step.step_count == 0U &&
             sampled_without_tick.snapshot == runtime.snapshot() &&
             sampled_without_tick.snapshot.pending_pressed_buttons == jump_mask,
         "a staged no-tick input snapshot differs from committed state");
}

void test_snapshot_preserves_pending_edges() {
  using namespace openrc::game;

  RuntimeGameplaySessionV1 runtime(foundation());
  const auto neutral = runtime.snapshot();
  const auto jump_mask = game_button_mask_v1(GameButtonV1::jump);
  runtime.submit_input_sample(movement_sample(0, 0, jump_mask));
  runtime.submit_input_sample(movement_sample(0, 0, 0U));
  const auto pending = runtime.snapshot();
  expect(pending.raw_input_sample == GameInputSampleV1{} &&
             pending.pending_pressed_buttons == jump_mask &&
             pending.pending_released_buttons == jump_mask &&
             pending != neutral,
         "runtime snapshot lost a complete button tap between ticks");

  const auto consumed = runtime.advance_frame(16'666'667U);
  expect(consumed.ticks.size() == 1U &&
             consumed.snapshot.pending_pressed_buttons == 0U &&
             consumed.snapshot.pending_released_buttons == 0U,
         "consumed runtime input edges remained pending in the snapshot");
}

void test_checkpoint_collision_envelope_is_bounded() {
  using namespace openrc::game;

  RuntimeGameplaySessionV1 runtime(foundation());
  const auto before = runtime.snapshot();
  PlayerCheckpointV1 invalid{
      90U,
      {static_cast<double>(std::numeric_limits<std::int32_t>::max()), 0.0,
       10.0},
      0.0,
  };
  expect_gameplay_error(
      [&] { runtime.set_checkpoint(invalid, true); },
      "an out-of-domain checkpoint was committed to live gameplay");
  expect(runtime.snapshot() == before,
         "a rejected checkpoint partially mutated gameplay state");

  expect_gameplay_error(
      [&] {
        static_cast<void>(RuntimeGameplaySessionV1(
            foundation(7U, 10U, invalid.feet_position, -5.0, empty_world())));
      },
      "an out-of-domain authored spawn entered runtime gameplay");
}

void test_jump_manual_reset_and_input_edges() {
  using namespace openrc::game;

  RuntimeGameplaySessionV1 runtime(foundation());
  for (std::uint32_t frame = 0U; frame < 4U; ++frame) {
    static_cast<void>(runtime.advance_frame(16'666'667U));
  }
  expect(runtime.snapshot().player.character.grounded,
         "player did not settle onto the synthetic floor");

  const auto jump_mask = game_button_mask_v1(GameButtonV1::jump);
  runtime.submit_input_sample(movement_sample(0, 0, jump_mask));
  runtime.submit_input_sample(movement_sample(0, 0, 0U));
  const auto jump = runtime.advance_frame(16'666'667U);
  expect(jump.ticks.size() == 1U &&
             (jump.ticks[0U].input.pressed_buttons & jump_mask) != 0U &&
             (jump.ticks[0U].input.released_buttons & jump_mask) != 0U &&
             jump.snapshot.player.character.velocity.z > 0.0,
         "a complete jump tap between ticks was lost");

  runtime.submit_input_sample(movement_sample(kGameInputAxisMagnitudeV1, 0));
  for (std::uint32_t frame = 0U; frame < 6U; ++frame) {
    static_cast<void>(runtime.advance_frame(16'666'667U));
  }
  const auto reset_mask = game_button_mask_v1(GameButtonV1::reset_checkpoint);
  const auto reset =
      runtime.advance_frame(16'666'667U, movement_sample(0, 0, reset_mask));
  expect(reset.ticks.size() == 1U &&
             reset.ticks[0U].player.reset_reason ==
                 PlayerResetReasonV1::manual &&
             reset.snapshot.player.character.feet_position ==
                 reset.snapshot.player.checkpoint.feet_position &&
             reset.snapshot.player.reset_count == 1U,
         "manual checkpoint reset did not use PlayerSimulation semantics");
  assert_tick_invariants(reset.snapshot,
                         reset.fixed_step.first_tick_index + 1U);
}

void test_death_reset_and_capped_catch_up() {
  using namespace openrc::game;

  RuntimeGameplaySessionV1 falling(
      foundation(7U, 10U, {0.0, 0.0, 0.5}, -1.0, empty_world()));
  bool reset_observed = false;
  for (std::uint32_t frame_index = 0U; frame_index < 180U; ++frame_index) {
    const auto frame = falling.advance_frame(16'666'667U);
    if (!frame.ticks.empty() &&
        frame.ticks.back().player.reset_reason ==
            PlayerResetReasonV1::fell_below_death_height) {
      expect(frame.snapshot.player.character.feet_position ==
                 frame.snapshot.player.checkpoint.feet_position,
             "death reset did not restore the authored spawn");
      reset_observed = true;
      break;
    }
  }
  expect(reset_observed && falling.snapshot().player.reset_count == 1U,
         "falling below the package death plane did not reset the player");

  RuntimeGameplaySessionOptionsV1 options;
  options.profile.fixed_step = FixedStepConfigV1{60U, 4U, 100'000'000U};
  RuntimeGameplaySessionV1 capped(foundation(), options);
  const auto overload = capped.advance_frame(250'000'000U);
  expect(
      overload.fixed_step == FixedStepAdvanceV1{0U, 4U, 2U, 150'000'000U, 0U} &&
          overload.ticks.size() == 4U &&
          overload.snapshot.total_dropped_step_count == 2U &&
          overload.snapshot.total_discarded_elapsed_nanoseconds == 150'000'000U,
      "runtime gameplay did not report bounded catch-up loss exactly");
  assert_tick_invariants(overload.snapshot, 4U);
}

void test_optional_entity_gameplay_absence_is_compatible() {
  using namespace openrc::game;

  RuntimeGameplaySessionV1 runtime(foundation());
  const auto initial = runtime.snapshot();
  expect(!initial.entity_gameplay && runtime.entity_gameplay() == nullptr &&
             runtime.item_total("items/bolts") == 0U,
         "an old level without neutral gameplay content changed shape");

  const auto frame = runtime.advance_frame(16'666'667U);
  expect(frame.ticks.size() == 1U &&
             frame.ticks.front().gameplay_events.empty() &&
             !frame.snapshot.entity_gameplay,
         "an absent gameplay resource emitted state or events");
  assert_tick_invariants(frame.snapshot, 1U);

  expect_gameplay_error(
      [&] { runtime.restore_item_totals({{"items/bolts", 5U}}); },
      "inventory was restored without active neutral gameplay content");
}

void test_collectibles_emit_once_in_deterministic_order_and_sum() {
  using namespace openrc::game;

  RuntimeGameplaySessionOptionsV1 options;
  options.entity_gameplay = entity_gameplay_content();
  RuntimeGameplaySessionV1 first(foundation(), options);
  RuntimeGameplaySessionV1 second(foundation(), options);

  const auto first_frame = first.advance_frame(16'666'667U);
  const auto second_frame = second.advance_frame(16'666'667U);
  const std::vector<EntityGameplayEventV1> expected_events{
      {EntityGameplayEventKindV1::item_collected, 0U, 20U, "items/bolts", 5U},
      {EntityGameplayEventKindV1::item_collected, 0U, 40U, "items/bolts", 7U},
  };
  expect(
      first_frame == second_frame && first_frame.ticks.size() == 1U &&
          first_frame.ticks.front().gameplay_events == expected_events,
      "integrated collectible output is not deterministic authored-ID order");
  expect(first.item_total("items/bolts") == 12U &&
             first.entity_gameplay() != nullptr &&
             first_frame.snapshot.entity_gameplay &&
             first_frame.snapshot.entity_gameplay->item_totals ==
                 std::vector<EntityGameplayItemTotalV1>{{"items/bolts", 12U}},
         "integrated semantic item totals were not exposed in queries and "
         "snapshots");
  assert_tick_invariants(first_frame.snapshot, 1U);

  const auto next = first.advance_frame(16'666'667U);
  expect(next.ticks.size() == 1U &&
             next.ticks.front().gameplay_events.empty() &&
             first.item_total("items/bolts") == 12U,
         "an integrated collectible was consumed more than once");
  assert_tick_invariants(next.snapshot, 2U);
}

void test_collectible_tick_observes_post_movement_player_capsule() {
  using namespace openrc::game;

  RuntimeGameplaySessionOptionsV1 options;
  options.entity_gameplay = movement_gated_entity_gameplay_content();
  RuntimeGameplaySessionV1 stationary(foundation(), options);
  RuntimeGameplaySessionV1 moving(foundation(), options);

  const auto stationary_frame = stationary.advance_frame(16'666'667U);
  const auto moving_frame = moving.advance_frame(
      16'666'667U, movement_sample(kGameInputAxisMagnitudeV1, 0));
  expect(stationary_frame.ticks.size() == 1U &&
             stationary_frame.ticks.front().gameplay_events.empty(),
         "the movement-order fixture overlapped before player movement");
  expect(moving_frame.ticks.size() == 1U &&
             moving_frame.snapshot.player.character.feet_position.x > 0.0 &&
             moving_frame.ticks.front().gameplay_events ==
                 std::vector<EntityGameplayEventV1>{
                     {EntityGameplayEventKindV1::item_collected, 0U, 90U,
                      "items/movement-gate", 1U}},
         "collectible overlap did not observe the player pose after movement");
}

void test_primary_action_destroys_destructible_and_grants_drop() {
  using namespace openrc::game;

  RuntimeGameplaySessionOptionsV1 options;
  options.entity_gameplay = wrench_destructible_entity_gameplay_content();
  RuntimeGameplaySessionV1 runtime(foundation(), options);
  const auto primary = game_button_mask_v1(GameButtonV1::primary_action);

  const auto startup_one =
      runtime.advance_frame(16'666'667U, movement_sample(0, 0, primary));
  expect(startup_one.ticks.size() == 1U &&
             startup_one.ticks.front().combat.attack_started &&
             !startup_one.ticks.front().combat.damage_pulse &&
             startup_one.ticks.front().gameplay_events.empty(),
         "primary action did not enter the production wrench startup phase");

  const auto startup_two =
      runtime.advance_frame(16'666'667U, movement_sample(0, 0));
  expect(startup_two.ticks.size() == 1U &&
             !startup_two.ticks.front().combat.attack_started &&
             !startup_two.ticks.front().combat.damage_pulse &&
             startup_two.ticks.front().gameplay_events.empty(),
         "the second production wrench startup tick emitted damage early");

  const auto active = runtime.advance_frame(16'666'667U);
  const std::vector<EntityGameplayEventV1> expected_events{
      {EntityGameplayEventKindV1::entity_damaged,
       2U,
       10U,
       {},
       0U,
       1U,
       0U,
       1U,
       0U,
       0U},
      {EntityGameplayEventKindV1::entity_destroyed,
       2U,
       10U,
       {},
       0U,
       1U,
       0U,
       1U,
       0U,
       0U},
      {EntityGameplayEventKindV1::item_granted, 2U, 10U,
       "openrc.currency/bolts", 1U, 0U, 0U, 0U, 0U, 0U},
  };
  expect(
      active.ticks.size() == 1U && active.ticks.front().combat.damage_pulse &&
          active.ticks.front().combat.damage_pulse->attack_sequence == 1U &&
          active.ticks.front().combat.damage_pulse->source_authored_id == 0U &&
          active.ticks.front().combat.damage_pulse->damage_channel ==
              kDamageChannelMeleeV1 &&
          active.ticks.front().gameplay_events == expected_events,
      "the production wrench pulse did not destroy the neutral target in "
      "canonical event order");

  const auto *gameplay = runtime.entity_gameplay();
  expect(gameplay != nullptr && gameplay->destroyed(10U) &&
             gameplay->health(10U) == std::optional<std::uint32_t>{0U} &&
             gameplay->world().entity_count() == 0U &&
             runtime.item_total("openrc.currency/bolts") == 1U &&
             active.snapshot.entity_gameplay &&
             active.snapshot.entity_gameplay->item_totals ==
                 std::vector<EntityGameplayItemTotalV1>{
                     {"openrc.currency/bolts", 1U}},
         "destruction and its semantic drop were not committed atomically");
  assert_tick_invariants(active.snapshot, 3U);

  const auto repeated_active_pulse = runtime.advance_frame(16'666'667U);
  expect(repeated_active_pulse.ticks.size() == 1U &&
             repeated_active_pulse.ticks.front().combat.damage_pulse &&
             repeated_active_pulse.ticks.front().gameplay_events.empty() &&
             runtime.item_total("openrc.currency/bolts") == 1U,
         "a later active pulse repeated destroyed-entity events or its drop");
  assert_tick_invariants(repeated_active_pulse.snapshot, 4U);
}

void test_damage_geometry_domain_is_shared_by_producer_and_consumer() {
  using namespace openrc::game;

  RuntimeGameplaySessionOptionsV1 options;
  options.profile = make_runtime_gameplay_profile_v1();
  options.profile.combat.startup_ticks = 0U;
  options.profile.combat.active_ticks = 1U;
  options.profile.combat.recovery_ticks = 0U;
  options.profile.combat.forward_start = 0.0;
  options.profile.combat.forward_end =
      kGameplayDamageMaximumGeometryMagnitudeV1;
  options.entity_gameplay = wrench_destructible_entity_gameplay_content();
  RuntimeGameplaySessionV1 runtime(foundation(), options);

  const auto primary = game_button_mask_v1(GameButtonV1::primary_action);
  const auto frame =
      runtime.advance_frame(16'666'667U, movement_sample(0, 0, primary));
  expect(frame.ticks.size() == 1U && frame.ticks.front().combat.damage_pulse &&
             frame.ticks.front().combat.damage_pulse->capsule_end.x ==
                 kGameplayDamageMaximumGeometryMagnitudeV1 &&
             frame.ticks.front().gameplay_events.size() == 3U &&
             runtime.entity_gameplay()->destroyed(10U) &&
             runtime.item_total("openrc.currency/bolts") == 1U,
         "a boundary-valid producer pulse was rejected or missed by the "
         "entity-gameplay consumer");
  assert_tick_invariants(frame.snapshot, 1U);
}

void test_item_total_restore_and_reload_preserve_persistence() {
  using namespace openrc::game;

  RuntimeGameplaySessionOptionsV1 options;
  options.entity_gameplay = entity_gameplay_content();
  RuntimeGameplaySessionV1 runtime(foundation(), options);
  runtime.restore_item_totals({{"items/bolts", 100U}});
  const auto restored = runtime.snapshot();
  expect(
      restored.entity_gameplay &&
          restored.entity_gameplay->item_totals ==
              std::vector<EntityGameplayItemTotalV1>{{"items/bolts", 100U}} &&
          runtime.item_total("items/bolts") == 100U,
      "session inventory restore was not reflected by its snapshot and query");

  const auto collected = runtime.advance_frame(16'666'667U);
  expect(collected.ticks.size() == 1U &&
             collected.ticks.front().gameplay_events.size() == 2U &&
             runtime.item_total("items/bolts") == 112U,
         "restored inventory did not accumulate collected item amounts");

  runtime.load_level(foundation(8U), entity_gameplay_content(8U));
  const auto reloaded = runtime.snapshot();
  expect(
      reloaded.session.active_level_id == 8U &&
          reloaded.session.next_tick_index == 1U && reloaded.entity_gameplay &&
          reloaded.entity_gameplay->level_id == 8U &&
          reloaded.entity_gameplay->level_instance_sequence == 2U &&
          reloaded.entity_gameplay->next_tick_index == 1U &&
          reloaded.entity_gameplay->item_totals ==
              std::vector<EntityGameplayItemTotalV1>{{"items/bolts", 112U}} &&
          runtime.item_total("items/bolts") == 112U,
      "neutral inventory or global tick origin was lost during level reload");
  assert_tick_invariants(reloaded, 1U);

  const auto recollected = runtime.advance_frame(16'666'667U);
  expect(recollected.ticks.size() == 1U &&
             recollected.ticks.front().gameplay_events ==
                 std::vector<EntityGameplayEventV1>{
                     {EntityGameplayEventKindV1::item_collected, 1U, 20U,
                      "items/bolts", 5U},
                     {EntityGameplayEventKindV1::item_collected, 1U, 40U,
                      "items/bolts", 7U}} &&
             runtime.item_total("items/bolts") == 124U,
         "reloaded collectible state did not reset while totals persisted");
}

void test_optional_entity_content_uses_global_level_instance_sequence() {
  using namespace openrc::game;

  RuntimeGameplaySessionV1 runtime(foundation());
  expect(runtime.snapshot().session.level_instance_sequence == 1U &&
             runtime.entity_gameplay() == nullptr,
         "the no-content fixture did not start at global level instance one");

  runtime.load_level(foundation(8U), entity_gameplay_content(8U));
  const auto first_content = runtime.snapshot();
  const auto first_entity_id = *runtime.entity_gameplay()->find_entity_id(20U);
  expect(first_content.session.level_instance_sequence == 2U &&
             first_content.active_level == ActiveLevelV1{8U, 10U, 2U} &&
             first_content.entity_gameplay &&
             first_content.entity_gameplay->level_instance_sequence == 2U &&
             first_entity_id.level_instance_sequence == 2U,
         "new neutral content did not join the global second level instance");
  runtime.restore_item_totals({{"items/bolts", 77U}});
  const auto seeded = runtime.snapshot();
  expect(seeded.item_totals ==
                 std::vector<EntityGameplayItemTotalV1>{{"items/bolts", 77U}} &&
             seeded.entity_gameplay &&
             seeded.entity_gameplay->item_totals == seeded.item_totals,
         "session and entity gameplay did not share restored inventory");

  runtime.load_level(foundation(9U));
  const auto absent = runtime.snapshot();
  expect(absent.session.level_instance_sequence == 3U &&
             absent.active_level == ActiveLevelV1{9U, 10U, 3U} &&
             absent.item_totals == seeded.item_totals &&
             runtime.item_total("items/bolts") == 77U &&
             !absent.entity_gameplay && runtime.entity_gameplay() == nullptr,
         "the intermediate no-content level lost global identity or inventory");

  runtime.load_level(foundation(10U), entity_gameplay_content(10U));
  const auto second_content = runtime.snapshot();
  const auto second_entity_id = *runtime.entity_gameplay()->find_entity_id(20U);
  expect(second_content.session.level_instance_sequence == 4U &&
             second_content.active_level == ActiveLevelV1{10U, 10U, 4U} &&
             second_content.entity_gameplay &&
             second_content.entity_gameplay->level_instance_sequence == 4U &&
             second_content.item_totals == seeded.item_totals &&
             second_content.entity_gameplay->item_totals ==
                 seeded.item_totals &&
             runtime.item_total("items/bolts") == 77U &&
             second_entity_id.level_instance_sequence == 4U &&
             second_entity_id != first_entity_id &&
             runtime.entity_gameplay()->world().find_entity(first_entity_id) ==
                 nullptr,
         "re-enabled neutral content reused a stale entity identity or local "
         "level instance");
  assert_tick_invariants(second_content, 0U);
}

void test_level_replacement_retains_global_tick_sequence() {
  using namespace openrc::game;

  RuntimeGameplaySessionV1 runtime(foundation());
  const auto before = runtime.advance_frame(
      50'000'000U, movement_sample(kGameInputAxisMagnitudeV1, 0));
  runtime.submit_input_sample(movement_sample(
      0, 0, game_button_mask_v1(GameButtonV1::reset_checkpoint)));

  runtime.load_level(foundation(8U, 20U, {4.0, 5.0, 2.0}, -3.0), 21U);
  const auto after = runtime.snapshot();
  expect(after.session.next_tick_index ==
                 before.snapshot.session.next_tick_index &&
             after.session.active_level_id == 8U &&
             after.session.active_spawn_point_id == 21U &&
             after.session.level_instance_sequence == 2U &&
             after.active_level == ActiveLevelV1{8U, 21U, 2U} &&
             after.player.checkpoint.checkpoint_id == 21U &&
             after.player.next_tick_index ==
                 before.snapshot.player.next_tick_index,
         "level replacement broke persistent session or replay numbering");
  expect(after.raw_input_sample == GameInputSampleV1{},
         "pending input leaked across a committed level boundary");
  assert_tick_invariants(after, before.snapshot.session.next_tick_index);

  const auto first_new_level_tick = runtime.advance_frame(16'666'667U);
  expect(first_new_level_tick.ticks.size() == 1U &&
             first_new_level_tick.ticks[0U].input.pressed_buttons == 0U &&
             first_new_level_tick.ticks[0U].input.released_buttons == 0U,
         "an old level's button edge reached the replacement level");
}

void test_prepared_state_is_owned_across_runtime_ticks_and_levels() {
  using namespace openrc;
  using namespace openrc::game;
  RuntimeGameplaySessionOptionsV1 options;
  options.persistent_state_limits = {2U,   4U,  64U,  1024U, 64U,
                                     128U, 64U, 256U, 16U};
  options.initial_persistent_state = SessionStateInitialV1{
      {"test.runtime/progress",
       {{"progress", 4U}},
       {{"word", "progress", SessionStateValueTypeV1::u32, 0U, 1U, 4U}}},
      {{"progress", {std::byte{7}, std::byte{0}, std::byte{0}, std::byte{0}}}},
  };
  options.entity_gameplay = entity_gameplay_content(7U);
  {
    GameSessionV1 frontend(0x1254U,*options.initial_persistent_state,options.persistent_state_limits);
    const std::array live_writes{SessionStateWriteV1{"word",0U,SessionStateValueTypeV1::u32,0x1234abcdU}};
    frontend.apply_persistent_state_writes(live_writes,0);
    const auto transferred=frontend.snapshot().persistent_state;
    const auto* allocation=frontend.persistent_state()->buffer_bytes("progress").data();
    auto continuation=options;
    continuation.initial_persistent_state.reset();
    continuation.frontend_session.emplace(std::move(frontend));
    RuntimeGameplaySessionV1 entered(foundation(),std::move(continuation));
    expect(entered.snapshot().session.persistent_state==transferred&&
        entered.session().deterministic_seed()==0x1254U&&
        entered.session().persistent_state()->buffer_bytes("progress").data()==allocation,
        "First gameplay admission copied or reinitialized live frontend state");
    expect_gameplay_error([&]{entered.apply_persistent_state_writes(live_writes,0);},
        "Frontend state transfer reset the live revision");
    static_cast<void>(entered.advance_frame(16'666'667U));
    expect(entered.snapshot().session.persistent_state==transferred,
        "First gameplay tick changed transferred frontend progress");
    auto invalid=options;
    invalid.frontend_session.emplace(0U,*options.initial_persistent_state,options.persistent_state_limits);
    expect_gameplay_error([&]{RuntimeGameplaySessionV1 rejected(foundation(),invalid);},
        "Frontend transfer accepted a second initial-state owner");
    invalid.initial_persistent_state.reset();
    static_cast<void>(invalid.frontend_session->request_level(7U));
    expect_gameplay_error([&]{RuntimeGameplaySessionV1 rejected(foundation(),invalid);},
        "Frontend transfer accepted an already requested level");
  }
  RuntimeGameplaySessionV1 runtime(foundation(), options);
  const auto initial_entity = *runtime.entity_gameplay()->find_entity_id(20U);
  expect(&runtime.world() == &runtime.entity_gameplay()->world() &&
             runtime.world().find_entity(initial_entity) != nullptr &&
             runtime.world().active_level() ==
                 std::optional{ActiveLevelV1{7U, 10U, 1U}},
         "session exposed a parallel empty world or lost its actual spawn");
  expect(runtime.session().persistent_state()->read_u32("word", 0U) == 7U,
         "runtime ignored explicitly supplied prepared state");
  const std::array writes{SessionStateWriteV1{
      "word", 0U, SessionStateValueTypeV1::u32, 0xfedcba98U}};
  runtime.apply_persistent_state_writes(writes, 0U);
  const auto committed = runtime.snapshot().session.persistent_state;
  const auto before_callback = runtime.snapshot();
  expect_gameplay_error(
      [&] {
        static_cast<void>(runtime.advance_frame(
            50'000'000U, [&](const GameInputCommandV1 &,
                             RuntimeMovementAxesV1 axes, double) {
              runtime.apply_persistent_state_writes(writes, 1U);
              return axes;
            }));
      },
      "movement callback mutated the committed state outside the staged frame");
  expect(runtime.snapshot() == before_callback,
         "failed callback frame retained a persistent write or another partial "
         "mutation");
  expect_gameplay_error(
      [&] {
        static_cast<void>(runtime.advance_frame(
            50'000'000U, [&](const GameInputCommandV1 &,
                             RuntimeMovementAxesV1 axes, double) {
              static_cast<void>(runtime.advance_frame(0U));
              return axes;
            }));
      },
      "runtime frame advancement accepted a recursive callback");
  expect(runtime.snapshot() == before_callback,
         "reentrant advance changed the committed runtime snapshot");
  expect_gameplay_error(
      [&] {
        static_cast<void>(runtime.advance_frame(
            50'000'000U, [&](const GameInputCommandV1 &,
                             RuntimeMovementAxesV1 axes, double) {
              runtime.load_level(foundation(8U), entity_gameplay_content(8U));
              return axes;
            }));
      },
      "a callback replaced the world underneath a staged frame");
  expect(runtime.snapshot() == before_callback &&
             runtime.world().find_entity(initial_entity) != nullptr,
         "reentrant level replacement partially published a new world");
  expect_gameplay_error(
      [&] {
        static_cast<void>(runtime.advance_frame(
            50'000'000U, [&](const GameInputCommandV1 &,
                             RuntimeMovementAxesV1 axes, double) {
              runtime.restore_item_totals({{"items/bolts", 999U}});
              return axes;
            }));
      },
      "a callback overwrote inventory outside its staged world");
  expect(runtime.snapshot() == before_callback,
         "reentrant inventory restore escaped frame rollback");
  expect_gameplay_error(
      [&] {
        static_cast<void>(runtime.advance_frame(
            50'000'000U, [&](const GameInputCommandV1 &,
                             RuntimeMovementAxesV1 axes, double) {
              auto checkpoint = runtime.player().snapshot().checkpoint;
              checkpoint.checkpoint_id += 100U;
              runtime.set_checkpoint(checkpoint, true);
              return axes;
            }));
      },
      "a callback replaced the checkpoint outside the staged player");
  expect(runtime.snapshot() == before_callback,
         "reentrant checkpoint replacement escaped frame rollback");
  runtime.apply_persistent_state_writes({}, 1U);
  static_cast<void>(runtime.advance_frame(
      16'666'667U,
      [&](const GameInputCommandV1 &, RuntimeMovementAxesV1 axes, double) {
        expect_gameplay_error(
            [&] { runtime.apply_persistent_state_writes({}, 1U); },
            "empty persistent write bypassed active-frame guard");
        return axes;
      }));
  static_cast<void>(runtime.advance_frame(50'000'000U));
  expect(runtime.snapshot().session.persistent_state == committed,
         "fixed-tick session staging lost persistent bytes or advanced their "
         "revision");
  expect(runtime.world().find_entity(initial_entity) == nullptr,
         "the session world did not observe the real entity tick removal");
  const auto before_failure = runtime.snapshot();
  auto invalid = foundation(8U);
  invalid.bootstrap.level_id = 99U;
  expect_gameplay_error([&] { runtime.load_level(invalid); },
                        "invalid foundation unexpectedly loaded");
  expect(runtime.snapshot() == before_failure,
         "failed level replacement changed persistent state or other runtime "
         "state");
  runtime.load_level(foundation(8U));
  runtime.load_level(foundation(7U), entity_gameplay_content(7U));
  const auto reloaded_entity = *runtime.entity_gameplay()->find_entity_id(20U);
  const auto before_content_failure = runtime.snapshot();
  expect_gameplay_error(
      [&] { runtime.load_level(foundation(8U), entity_gameplay_content(7U)); },
      "wrong-level content was accepted after staging a level request");
  expect(runtime.snapshot() == before_content_failure &&
             runtime.world().find_entity(reloaded_entity) != nullptr,
         "failed content materialization lost persistent bytes or live IDs");
  runtime.load_level(foundation(8U, 20U), entity_gameplay_content(8U), 21U);
  expect(&runtime.world() == &runtime.entity_gameplay()->world() &&
             runtime.world().active_level() ==
                 std::optional{ActiveLevelV1{8U, 21U, 4U}} &&
             runtime.world().find_entity(reloaded_entity) == nullptr &&
             runtime.snapshot().session.next_level_request_sequence == 4U &&
             runtime.snapshot().session.next_level_commit_sequence == 4U,
         "entity reload issued a second request or recreated a parallel world");
  expect(
      runtime.snapshot().session.persistent_state == committed,
      "optional-content transition reinitialized or discarded persistent data");
  const auto before_stale = runtime.snapshot();
  expect_gameplay_error(
      [&] { runtime.apply_persistent_state_writes(writes, 0U); },
      "runtime accepted stale state writes");
  expect(runtime.snapshot() == before_stale,
         "stale state operation was not atomic at runtime ownership boundary");
  RuntimeGameplaySessionV1 absent(foundation());
  expect(!absent.snapshot().session.persistent_state,
         "runtime without a prepared state contract created default progress");
  expect_gameplay_error(
      [&] { absent.apply_persistent_state_writes({}, 0U); },
      "runtime without a state contract accepted an operation");
}

void test_frontend_session_transfer_retains_canonical_state() {
  using namespace openrc;
  using namespace openrc::game;
  const SessionStateLimitsV1 limits{2U,   4U,  64U,  1024U, 64U,
                                    128U, 64U, 256U, 16U};
  const SessionStateInitialV1 initial{
      {"test.runtime/frontend",
       {{"progress", 4U}},
       {{"word", "progress", SessionStateValueTypeV1::u32, 0U, 1U, 4U}}},
      {{"progress", {std::byte{3}, std::byte{0}, std::byte{0}, std::byte{0}}}},
  };
  const auto live_frontend = [&] {
    GameSessionV1 frontend(0x5eedU, initial, limits);
    for (std::uint32_t value = 1U; value <= 3U; ++value) {
      const std::array writes{SessionStateWriteV1{
          "word", 0U, SessionStateValueTypeV1::u32, 0x1000U + value}};
      frontend.apply_persistent_state_writes(
          writes, frontend.persistent_state()->revision());
    }
    return frontend;
  };

  {
    auto frontend = live_frontend();
    const auto before = frontend.snapshot();
    const auto *allocation =
        frontend.persistent_state()->buffer_bytes("progress").data();
    RuntimeGameplaySessionOptionsV1 options;
    // The transferred seed wins; neither this seed nor any prepared initial
    // value is applied again at first gameplay admission.
    options.deterministic_seed = 0x0badU;
    options.persistent_state_limits = limits;
    options.entity_gameplay = entity_gameplay_content(7U);
    options.frontend_session.emplace(std::move(frontend));
    RuntimeGameplaySessionV1 entered(foundation(), std::move(options));
    const auto admitted = entered.snapshot().session;
    expect(admitted.deterministic_seed == 0x5eedU &&
               admitted.persistent_state == before.persistent_state &&
               admitted.persistent_state->revision == 3U &&
               entered.session().persistent_state()->read_u32("word", 0U) ==
                   0x1003U &&
               entered.session().persistent_state()->buffer_bytes("progress")
                       .data() == allocation,
           "frontend transfer changed the seed, bytes, revision or owner");
    expect(admitted.active_level_id == 7U &&
               admitted.level_instance_sequence == 1U &&
               admitted.next_level_request_sequence == 1U &&
               admitted.next_tick_index == 0U && !admitted.pending_level_request,
           "frontend transfer did not commit exactly one first-level request");

    std::uint64_t ticks = 0U;
    for (unsigned frame = 0U; frame < 30U; ++frame) {
      ticks += entered
                   .advance_frame(16'666'667U,
                                  movement_sample(0, kGameInputAxisMagnitudeV1))
                   .ticks.size();
    }
    const auto after_ticks = entered.snapshot().session;
    expect(ticks == 30U && after_ticks.next_tick_index == 30U &&
               after_ticks.persistent_state == before.persistent_state &&
               after_ticks.deterministic_seed == 0x5eedU,
           "gameplay ticks changed transferred frontend bytes or revision");
    expect_gameplay_error(
        [&] {
          entered.apply_persistent_state_writes(
              std::array{SessionStateWriteV1{
                  "word", 0U, SessionStateValueTypeV1::u32, 1U}},
              2U);
        },
        "transferred session accepted a stale frontend revision");
    entered.apply_persistent_state_writes(
        std::array{
            SessionStateWriteV1{"word", 0U, SessionStateValueTypeV1::u32, 9U}},
        3U);
    expect(entered.session().persistent_state()->revision() == 4U &&
               entered.session().persistent_state()->read_u32("word", 0U) ==
                   9U,
           "transferred session revision did not advance by its contract");
  }

  const auto expect_rejected = [&](GameSessionV1 session,
                                   const bool with_initial_state,
                                   const std::string &message) {
    RuntimeGameplaySessionOptionsV1 options;
    options.persistent_state_limits = limits;
    if (with_initial_state) {
      options.initial_persistent_state = initial;
    }
    options.frontend_session.emplace(std::move(session));
    expect_gameplay_error(
        [&] {
          RuntimeGameplaySessionV1 rejected(foundation(), std::move(options));
        },
        message);
  };
  expect_rejected(live_frontend(), true,
                  "frontend transfer accepted a second initial-state owner");
  {
    auto ticked = live_frontend();
    GameInputCommandV1 command;
    command.tick_index = 0U;
    ticked.commit_simulation_tick(command);
    expect_rejected(std::move(ticked), false,
                    "frontend transfer accepted a session with gameplay ticks");
  }
  {
    auto requested = live_frontend();
    static_cast<void>(requested.request_level(7U));
    expect_rejected(std::move(requested), false,
                    "frontend transfer accepted a pending level request");
  }
  {
    auto entered = live_frontend();
    WorldV1 world;
    world.load_level(entered, entered.request_level(7U));
    expect_rejected(std::move(entered), false,
                    "frontend transfer accepted an already entered level");
  }
  expect_rejected(GameSessionV1(0x5eedU), false,
                  "frontend transfer accepted a session without prepared "
                  "persistent state");
}

} // namespace

int main() {
  try {
    test_spawn_session_and_production_profile();
    test_determinism_across_frame_partitions();
    test_per_tick_movement_mapping_and_atomic_failure();
    test_source_ground_paces_are_exact_without_a_frontend_mapper();
    test_ground_pace_survives_double_precision_camera_rotation();
    test_airborne_input_remains_proportional_to_source_response();
    test_staged_frame_snapshot_matches_committed_state();
    test_snapshot_preserves_pending_edges();
    test_checkpoint_collision_envelope_is_bounded();
    test_jump_manual_reset_and_input_edges();
    test_death_reset_and_capped_catch_up();
    test_optional_entity_gameplay_absence_is_compatible();
    test_collectibles_emit_once_in_deterministic_order_and_sum();
    test_collectible_tick_observes_post_movement_player_capsule();
    test_primary_action_destroys_destructible_and_grants_drop();
    test_damage_geometry_domain_is_shared_by_producer_and_consumer();
    test_item_total_restore_and_reload_preserve_persistence();
    test_optional_entity_content_uses_global_level_instance_sequence();
    test_level_replacement_retains_global_tick_sequence();
    test_prepared_state_is_owned_across_runtime_ticks_and_levels();
    test_frontend_session_transfer_retains_canonical_state();
    std::cout << "runtime_gameplay_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime_gameplay_tests: " << error.what() << '\n';
    return 1;
  }
}
