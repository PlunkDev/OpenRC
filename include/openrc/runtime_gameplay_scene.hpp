#pragma once

#include "openrc/collision_world.hpp"
#include "openrc/damage.hpp"
#include "openrc/destructible_scene.hpp"
#include "openrc/entity_scene.hpp"
#include "openrc/game_world.hpp"
#include "openrc/gameplay_scene.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace openrc::game {

inline constexpr std::uint32_t kEntityGameplayMaximumDamagePulsesPerTickV1 =
    1024U;

struct EntityGameplayInventoryLimitsV1 {
  std::uint32_t max_item_totals = 0U;
  std::uint32_t max_item_key_bytes = 0U;
  std::uint64_t max_total_item_key_bytes = 0U;

  [[nodiscard]] bool
  operator==(const EntityGameplayInventoryLimitsV1 &) const = default;
};

struct EntityGameplayRuntimeLimitsV1 {
  EntitySceneLimitsV1 entity_scene;
  GameplaySceneLimitsV1 gameplay_scene;
  DestructibleSceneLimitsV1 destructible_scene;
  EntityGameplayInventoryLimitsV1 inventory;
  std::uint32_t max_damage_sources_per_destructible = 0U;

  [[nodiscard]] bool
  operator==(const EntityGameplayRuntimeLimitsV1 &) const = default;
};

// Canonical per-source replay guard for one destructible. Attack sequences
// must increase independently for each source; repeated active ticks retain
// the same sequence and therefore cannot damage the same target twice.
struct EntityGameplayDamageSourceSequenceV1 {
  std::uint32_t source_authored_id = 0U;
  std::uint64_t last_attack_sequence = 0U;

  [[nodiscard]] bool
  operator==(const EntityGameplayDamageSourceSequenceV1 &) const = default;
};

// The capsule is upright on the canonical world Z axis. feet_position is the
// logical contact point below it, matching CharacterControllerStateV1.
struct EntityGameplayPlayerCapsuleV1 {
  CollisionVectorV1 feet_position;
  double radius = 0.0;
  double height = 0.0;

  [[nodiscard]] bool
  operator==(const EntityGameplayPlayerCapsuleV1 &) const = default;
};

// Applies the complete authored scale-then-rotate-then-translate transform to
// a collectible's local center. EntitySceneV1 guarantees a canonical unit
// X/Y/Z/W quaternion, so this conversion is deterministic and non-throwing.
[[nodiscard]] CollisionVectorV1 world_collectible_center_v1(
    const WorldTransformV1 &transform,
    const GameplayCollectibleV1 &collectible) noexcept;

[[nodiscard]] CollisionVectorV1 world_destructible_center_v1(
    const WorldTransformV1 &transform,
    const DestructibleDefinitionV1 &destructible) noexcept;

enum class EntityGameplayEventKindV1 : std::uint8_t {
  item_collected = 0U,
  entity_damaged,
  entity_destroyed,
  item_granted,
};

// Events are emitted in ascending authored_id order. A semantic item key,
// rather than a source class ID, is the persistent inventory identity.
struct EntityGameplayEventV1 {
  EntityGameplayEventKindV1 kind = EntityGameplayEventKindV1::item_collected;
  std::uint64_t tick_index = 0U;
  std::uint32_t authored_id = 0U;
  std::string item_key;
  std::uint32_t amount = 0U;
  // Damage/destroy events identify their source attack. Item events retain
  // zero in these fields. remaining_health is post-damage state.
  std::uint64_t attack_sequence = 0U;
  std::uint32_t source_authored_id = 0U;
  std::uint32_t damage = 0U;
  std::uint32_t remaining_health = 0U;
  // Canonical zero-based index in the owning destructible's drop list.
  std::uint32_t drop_ordinal = 0U;

  [[nodiscard]] bool operator==(const EntityGameplayEventV1 &) const = default;
};

struct EntityGameplayItemTotalV1 {
  std::string item_key;
  std::uint64_t amount = 0U;

  [[nodiscard]] bool
  operator==(const EntityGameplayItemTotalV1 &) const = default;
};

struct EntityGameplayEntitySnapshotV1 {
  std::uint32_t authored_id = 0U;
  EntityIdV1 entity_id;
  // Player definitions deliberately have no authored world transform. Their
  // WorldV1 identity is still materialized, while player simulation remains
  // authoritative for their live placement.
  std::optional<WorldTransformV1> authored_transform;
  bool enabled = false;
  bool collected = false;
  bool destroyed = false;
  std::optional<std::uint32_t> health;
  // Canonical ascending source_authored_id order. Empty for entities which
  // are not destructible or have not yet accepted damage.
  std::vector<EntityGameplayDamageSourceSequenceV1> damage_source_sequences;

  [[nodiscard]] bool
  operator==(const EntityGameplayEntitySnapshotV1 &) const = default;
};

struct EntityGameplaySnapshotV1 {
  LevelIdV1 level_id = 0U;
  std::uint64_t level_instance_sequence = 0U;
  std::uint64_t next_tick_index = 0U;
  // Canonical lexicographic item_key order. Zero totals are omitted.
  std::vector<EntityGameplayItemTotalV1> item_totals;
  // Canonical ascending authored_id order, including disabled definitions and
  // collected definitions whose WorldV1 entity has already been destroyed.
  std::vector<EntityGameplayEntitySnapshotV1> entities;

  [[nodiscard]] bool
  operator==(const EntityGameplaySnapshotV1 &) const = default;
};

class EntityGameplayRuntimeError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Standalone deterministic materialization and collectible runtime. It owns a
// WorldV1 but is intentionally not coupled to RuntimeGameplaySessionV1. Source
// class IDs and source-format records never cross this boundary.
class EntityGameplayRuntimeV1 final {
public:
  EntityGameplayRuntimeV1() = default;

  // Both scenes must already be canonical and describe the same level. A load
  // or reload is transactional: the previous world remains intact if any
  // validation, materialization, or allocation step fails.
  void load_scene(const EntitySceneV1 &entity_scene,
                  const GameplaySceneV1 &gameplay_scene,
                  EntityGameplayRuntimeLimitsV1 limits,
                  std::uint64_t first_tick_index = 0U,
                  std::optional<std::uint64_t>
                      required_level_instance_sequence = std::nullopt);

  void load_scene(const EntitySceneV1 &entity_scene,
                  const GameplaySceneV1 &gameplay_scene,
                  const DestructibleSceneV1 &destructible_scene,
                  EntityGameplayRuntimeLimitsV1 limits,
                  std::uint64_t first_tick_index = 0U,
                  std::optional<std::uint64_t>
                      required_level_instance_sequence = std::nullopt);

  // Processes collectibles against one externally simulated player capsule.
  // Overflow and invalid tick/input errors leave gameplay state unchanged.
  [[nodiscard]] std::vector<EntityGameplayEventV1>
  fixed_tick(std::uint64_t tick_index,
             const EntityGameplayPlayerCapsuleV1 &player);

  [[nodiscard]] std::vector<EntityGameplayEventV1>
  fixed_tick(std::uint64_t tick_index,
             const EntityGameplayPlayerCapsuleV1 &player,
             std::span<const GameplayDamagePulseV1> damage_pulses);

  // Replaces only persistent neutral inventory after validating canonical key
  // order and bounds. Loading another scene preserves these totals.
  void restore_item_totals(std::vector<EntityGameplayItemTotalV1> totals);

  [[nodiscard]] bool loaded() const noexcept;
  [[nodiscard]] const WorldV1 &world() const;
  [[nodiscard]] EntityGameplaySnapshotV1 snapshot() const;
  [[nodiscard]] std::optional<EntityIdV1>
  find_entity_id(std::uint32_t authored_id) const noexcept;
  [[nodiscard]] std::optional<WorldTransformV1>
  find_authored_transform(std::uint32_t authored_id) const noexcept;
  [[nodiscard]] bool enabled(std::uint32_t authored_id) const;
  [[nodiscard]] bool collected(std::uint32_t authored_id) const;
  [[nodiscard]] bool destroyed(std::uint32_t authored_id) const;
  [[nodiscard]] std::optional<std::uint32_t>
  health(std::uint32_t authored_id) const;
  [[nodiscard]] std::uint64_t
  item_total(std::string_view item_key) const noexcept;
  [[nodiscard]] const std::vector<EntityGameplayItemTotalV1> &
  item_totals() const noexcept;
  [[nodiscard]] std::uint64_t next_tick_index() const;

private:
  struct EntityRecordV1 {
    std::uint32_t authored_id = 0U;
    EntityIdV1 entity_id;
    std::optional<WorldTransformV1> authored_transform;
    std::optional<GameplayCollectibleV1> collectible;
    std::optional<DestructibleDefinitionV1> destructible;
    bool enabled = false;
    bool collected = false;
    bool destroyed = false;
    std::uint32_t health = 0U;
    std::vector<EntityGameplayDamageSourceSequenceV1>
        damage_source_sequences;
  };

  struct LoadedStateV1 {
    GameSessionV1 session;
    WorldV1 world;
    LevelIdV1 level_id = 0U;
    std::uint64_t next_tick_index = 0U;
    EntityGameplayInventoryLimitsV1 inventory_limits;
    std::uint32_t max_damage_sources_per_destructible = 0U;
    std::vector<EntityRecordV1> entities;
  };

  [[nodiscard]] const EntityRecordV1 *
  find_record(std::uint32_t authored_id) const noexcept;

  std::optional<LoadedStateV1> state_;
  std::vector<EntityGameplayItemTotalV1> item_totals_;
};

} // namespace openrc::game
