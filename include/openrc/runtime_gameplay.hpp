#pragma once

#include "openrc/fixed_step.hpp"
#include "openrc/game_world.hpp"
#include "openrc/player_combat.hpp"
#include "openrc/runtime_gameplay_scene.hpp"
#include "openrc/runtime_level_foundation.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc::game {

inline constexpr std::uint32_t kRuntimeGameplayMaximumStepsPerAdvanceV1 = 256U;

// Runtime-owned policy shared by every prepared level. The level package
// supplies collision, authored spawns, and its death plane; none of those
// source-derived values are duplicated here.
struct RuntimeGameplayProfileV1 {
  FixedStepConfigV1 fixed_step;
  CharacterControllerProfileV1 character;
  PlayerCombatProfileV1 combat;

  [[nodiscard]] bool
  operator==(const RuntimeGameplayProfileV1 &) const = default;
};

[[nodiscard]] RuntimeGameplayProfileV1 make_runtime_gameplay_profile_v1();
void validate_runtime_gameplay_profile_v1(
    const RuntimeGameplayProfileV1 &profile);

// Stable allocation policy for materializing the optional neutral scene in
// the shipped runtime. Frontends should use this instead of inventing their
// own entity, collectible, or persistent-inventory bounds.
[[nodiscard]] constexpr EntityGameplayRuntimeLimitsV1
make_runtime_entity_gameplay_limits_v1() {
  return EntityGameplayRuntimeLimitsV1{
      EntitySceneLimitsV1{
          1'000'000U,
          1'000'000U,
          1'000'000U,
          1'000'000U,
          16U,
          256U,
          256U,
          UINT64_C(128) * 1024U * 1024U,
      },
      GameplaySceneLimitsV1{
          1'000'000U,
          256U,
          UINT64_C(128) * 1024U * 1024U,
      },
      DestructibleSceneLimitsV1{
          1'000'000U,
          1'000'000U,
          4096U,
          256U,
          UINT64_C(128) * 1024U * 1024U,
          UINT32_MAX,
          UINT32_MAX,
          1'000'000.0F,
          1'000'000.0F,
      },
      EntityGameplayInventoryLimitsV1{
          1'000'000U,
          256U,
          UINT64_C(128) * 1024U * 1024U,
      },
      4096U,
  };
}

// Optional neutral entity/collectible content for one active level. Keeping
// the validation limits beside the scenes makes every materialization and
// reload use an explicit bounded policy.
struct RuntimeGameplayEntityContentV1 {
  EntitySceneV1 entity_scene;
  GameplaySceneV1 gameplay_scene;
  EntityGameplayRuntimeLimitsV1 limits =
      make_runtime_entity_gameplay_limits_v1();
  // Older prepared packages legitimately omit this optional resource.
  std::optional<DestructibleSceneV1> destructible_scene;

  [[nodiscard]] bool
  operator==(const RuntimeGameplayEntityContentV1 &) const = default;
};

struct RuntimeGameplaySessionOptionsV1 {
  RuntimeGameplayProfileV1 profile = make_runtime_gameplay_profile_v1();
  std::uint64_t deterministic_seed = 0U;
  std::optional<SpawnPointIdV1> spawn_point_id;
  LevelRequestReasonV1 level_request_reason = LevelRequestReasonV1::new_game;
  std::optional<RuntimeGameplayEntityContentV1> entity_gameplay;
};

struct RuntimeMovementAxesV1 {
  std::int16_t move_x = 0;
  std::int16_t move_y = 0;

  [[nodiscard]] bool operator==(const RuntimeMovementAxesV1 &) const = default;
};

// The callback converts device/camera-relative movement into canonical world
// XY. It runs exactly once per emitted fixed tick, immediately before the
// player step, and receives the same explicit delta used by player simulation.
// This lets a separately owned camera advance from look axes without copying
// the runtime's tick-rate policy. An empty mapper preserves the command's
// already-world-space movement axes.
using RuntimeMovementMapperV1 = std::function<RuntimeMovementAxesV1(
    const GameInputCommandV1 &raw_input, double fixed_delta_seconds)>;

struct RuntimeGameplayTickV1 {
  GameInputCommandV1 input;
  PlayerSimulationStepV1 player;
  PlayerCombatStepV1 combat;
  // Canonical authored-ID order, produced after this tick's player movement.
  std::vector<EntityGameplayEventV1> gameplay_events;

  [[nodiscard]] bool operator==(const RuntimeGameplayTickV1 &) const = default;
};

struct RuntimeGameplaySnapshotV1 {
  GameSessionSnapshotV1 session;
  std::optional<ActiveLevelV1> active_level;
  PlayerSimulationSnapshotV1 player;
  GameInputSampleV1 raw_input_sample;
  std::uint32_t pending_pressed_buttons = 0U;
  std::uint32_t pending_released_buttons = 0U;
  std::uint64_t input_next_tick_index = 0U;
  std::uint64_t fixed_step_next_tick_index = 0U;
  std::uint64_t interpolation_numerator = 0U;
  std::uint64_t total_dropped_step_count = 0U;
  std::uint64_t total_discarded_elapsed_nanoseconds = 0U;
  PlayerCombatSnapshotV1 combat;
  // Session-owned canonical inventory survives levels which intentionally
  // omit optional entity-gameplay content.
  std::vector<EntityGameplayItemTotalV1> item_totals;
  std::optional<EntityGameplaySnapshotV1> entity_gameplay;

  [[nodiscard]] bool
  operator==(const RuntimeGameplaySnapshotV1 &) const = default;
};

struct RuntimeGameplayFrameAdvanceV1 {
  FixedStepAdvanceV1 fixed_step;
  std::vector<RuntimeGameplayTickV1> ticks;
  RuntimeGameplaySnapshotV1 snapshot;

  [[nodiscard]] bool
  operator==(const RuntimeGameplayFrameAdvanceV1 &) const = default;
};

class RuntimeGameplayError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Package-only deterministic gameplay coordinator. It deliberately owns no
// renderer or platform clock: a frontend submits quantized input and explicit
// integer elapsed time. Player state remains separate from WorldV1 entities
// until prepared content defines a stable player-archetype contract.
class RuntimeGameplaySessionV1 final {
public:
  explicit RuntimeGameplaySessionV1(
      RuntimeLevelFoundationV1 foundation,
      RuntimeGameplaySessionOptionsV1 options = {});

  // Replaces the active package foundation while retaining persistent session
  // and global replay tick sequences. Pending input is cleared at the level
  // boundary so an old jump/reset edge cannot leak into the new instance.
  void
  load_level(RuntimeLevelFoundationV1 foundation,
             std::optional<SpawnPointIdV1> spawn_point_id = std::nullopt,
             LevelRequestReasonV1 reason = LevelRequestReasonV1::transition);

  // Reloads the foundation and neutral entity/collectible content as one
  // transaction. Persistent semantic item totals survive successful reloads.
  void
  load_level(RuntimeLevelFoundationV1 foundation,
             RuntimeGameplayEntityContentV1 entity_gameplay,
             std::optional<SpawnPointIdV1> spawn_point_id = std::nullopt,
             LevelRequestReasonV1 reason = LevelRequestReasonV1::transition);

  // Samples may be submitted even when a frame emits no fixed tick; button
  // edges remain pending in GameInputStateV1 until exactly one tick consumes
  // them.
  void submit_input_sample(const GameInputSampleV1 &sample);
  void release_input() noexcept;

  [[nodiscard]] RuntimeGameplayFrameAdvanceV1
  advance_frame(std::uint64_t elapsed_nanoseconds,
                const RuntimeMovementMapperV1 &movement_mapper = {});
  [[nodiscard]] RuntimeGameplayFrameAdvanceV1
  advance_frame(std::uint64_t elapsed_nanoseconds,
                const GameInputSampleV1 &sample,
                const RuntimeMovementMapperV1 &movement_mapper = {});

  void set_checkpoint(PlayerCheckpointV1 checkpoint, bool reset_immediately);

  // Restores canonical persistent totals without changing current collected
  // entity state. Neutral content must be active.
  void restore_item_totals(std::vector<EntityGameplayItemTotalV1> totals);

  [[nodiscard]] RuntimeGameplaySnapshotV1 snapshot() const;
  [[nodiscard]] std::uint64_t interpolation_numerator() const noexcept;
  [[nodiscard]] std::uint64_t
  item_total(std::string_view item_key) const noexcept;
  [[nodiscard]] const EntityGameplayRuntimeV1 *entity_gameplay() const noexcept;
  [[nodiscard]] const RuntimeLevelFoundationV1 &foundation() const noexcept;
  [[nodiscard]] const RuntimeGameplayProfileV1 &profile() const noexcept;
  [[nodiscard]] const GameSessionV1 &session() const noexcept;
  [[nodiscard]] const WorldV1 &world() const noexcept;
  [[nodiscard]] const PlayerSimulationV1 &player() const noexcept;
  [[nodiscard]] const PlayerCombatV1 &combat() const noexcept;

private:
  [[nodiscard]] RuntimeGameplayFrameAdvanceV1
  advance_frame_impl(std::uint64_t elapsed_nanoseconds,
                     const std::optional<GameInputSampleV1> &new_sample,
                     const RuntimeMovementMapperV1 &movement_mapper);
  void
  load_level_impl(RuntimeLevelFoundationV1 foundation,
                  std::optional<RuntimeGameplayEntityContentV1> entity_gameplay,
                  std::optional<SpawnPointIdV1> spawn_point_id,
                  LevelRequestReasonV1 reason);
  void validate_tick_invariants() const;

  RuntimeLevelFoundationV1 foundation_;
  RuntimeGameplayProfileV1 profile_;
  GameSessionV1 session_;
  WorldV1 world_;
  PlayerSimulationV1 player_;
  GameInputStateV1 input_;
  FixedStepAccumulatorV1 fixed_step_;
  PlayerCombatV1 combat_;
  std::vector<EntityGameplayItemTotalV1> item_totals_;
  std::optional<EntityGameplayRuntimeV1> entity_gameplay_;
};

} // namespace openrc::game
