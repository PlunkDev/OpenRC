#include "openrc/runtime_gameplay_scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace allocation_failure {

bool enabled = false;
std::size_t allocations_before_failure = 0U;

[[nodiscard]] void *allocate(const std::size_t requested_size) {
  if (enabled) {
    if (allocations_before_failure == 0U) {
      enabled = false;
      throw std::bad_alloc{};
    }
    --allocations_before_failure;
  }
  if (auto *const result = std::malloc(requested_size == 0U ? 1U
                                                            : requested_size)) {
    return result;
  }
  throw std::bad_alloc{};
}

void fail_after(const std::size_t successful_allocations) noexcept {
  allocations_before_failure = successful_allocations;
  enabled = true;
}

void disable() noexcept { enabled = false; }

} // namespace allocation_failure

void *operator new(const std::size_t size) {
  return allocation_failure::allocate(size);
}

void *operator new[](const std::size_t size) {
  return allocation_failure::allocate(size);
}

void operator delete(void *const allocation) noexcept {
  std::free(allocation);
}

void operator delete[](void *const allocation) noexcept {
  std::free(allocation);
}

void operator delete(void *const allocation, const std::size_t) noexcept {
  std::free(allocation);
}

void operator delete[](void *const allocation, const std::size_t) noexcept {
  std::free(allocation);
}

namespace {

constexpr std::uint32_t kDestructibleAuthoredId = 10U;

constexpr openrc::game::EntityGameplayRuntimeLimitsV1 kLimits{
    openrc::EntitySceneLimitsV1{
        32U,
        32U,
        32U,
        32U,
        4U,
        64U,
        64U,
        1024U,
    },
    openrc::GameplaySceneLimitsV1{32U, 64U, 1024U},
    openrc::DestructibleSceneLimitsV1{
        16U,
        64U,
        16U,
        64U,
        1024U,
        10'000U,
        10'000U,
        1000.0F,
        1000.0F,
    },
    openrc::game::EntityGameplayInventoryLimitsV1{32U, 64U, 1024U},
    16U,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_runtime_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::EntityGameplayRuntimeError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::game::EntityGameplayPlayerCapsuleV1
player_at(const double x, const double y, const double z = 0.0) {
  return {{x, y, z}, 0.5, 2.0};
}

struct Scenes {
  openrc::EntitySceneV1 entities;
  openrc::GameplaySceneV1 gameplay;
};

struct DestructibleScenes {
  openrc::EntitySceneV1 entities;
  openrc::GameplaySceneV1 gameplay;
  openrc::DestructibleSceneV1 destructibles;
};

[[nodiscard]] Scenes make_scenes(const std::uint32_t level_id = 7U) {
  using namespace openrc;

  EntityDefinitionV1 player;
  player.authored_id = 5U;
  player.archetype_key = "openrc.player/default";

  EntityDefinitionV1 bolts;
  bolts.authored_id = 20U;
  bolts.archetype_key = "openrc.collectible/bolt";

  EntityDefinitionV1 health;
  health.authored_id = 40U;
  health.archetype_key = "openrc.collectible/health";

  EntityDefinitionV1 disabled;
  disabled.authored_id = 60U;
  disabled.archetype_key = "openrc.collectible/disabled";
  disabled.flags = 0U;

  EntityDefinitionV1 distant;
  distant.authored_id = 80U;
  distant.archetype_key = "openrc.collectible/bolt";

  EntityTransformComponentV1 bolts_transform;
  bolts_transform.authored_id = bolts.authored_id;
  bolts_transform.transform.position = {0.25F, 0.0F, 0.75F};

  EntityTransformComponentV1 health_transform;
  health_transform.authored_id = health.authored_id;
  health_transform.transform.position = {-0.25F, 0.0F, 1.0F};

  EntityTransformComponentV1 disabled_transform;
  disabled_transform.authored_id = disabled.authored_id;
  disabled_transform.transform.position = {0.0F, 0.0F, 1.0F};

  EntityTransformComponentV1 distant_transform;
  distant_transform.authored_id = distant.authored_id;
  distant_transform.transform.position = {100.0F, 0.0F, 1.0F};

  EntitySceneV1 entity_scene;
  entity_scene.level_id = level_id;
  entity_scene.definitions = {player, bolts, health, disabled, distant};
  entity_scene.transforms = {bolts_transform, health_transform,
                             disabled_transform, distant_transform};
  entity_scene.player_bindings = {{player.authored_id, 0U}};

  GameplaySceneV1 gameplay_scene;
  gameplay_scene.level_id = level_id;
  gameplay_scene.collectibles = {
      GameplayCollectibleV1{bolts.authored_id, "items/bolts", {}, 5U, 0.4F, 0U},
      GameplayCollectibleV1{
          health.authored_id, "items/health", {}, 1U, 0.4F, 0U},
      GameplayCollectibleV1{
          disabled.authored_id, "items/bolts", {}, 50U, 0.4F, 0U},
      GameplayCollectibleV1{
          distant.authored_id, "items/bolts", {}, 7U, 0.4F, 0U},
  };
  return {canonicalize_entity_scene_v1(std::move(entity_scene),
                                       kLimits.entity_scene),
          canonicalize_gameplay_scene_v1(std::move(gameplay_scene),
                                         kLimits.gameplay_scene)};
}

[[nodiscard]] DestructibleScenes
make_destructible_scenes(const std::uint32_t level_id = 7U) {
  using namespace openrc;

  auto base = make_scenes(level_id);

  EntityDefinitionV1 crate;
  crate.authored_id = kDestructibleAuthoredId;
  crate.archetype_key = "openrc.destructible/crate";
  base.entities.definitions.push_back(crate);

  EntityTransformComponentV1 crate_transform;
  crate_transform.authored_id = crate.authored_id;
  crate_transform.transform.position = {4.0F, 0.0F, 1.0F};
  base.entities.transforms.push_back(crate_transform);

  DestructibleDefinitionV1 destructible;
  destructible.authored_id = crate.authored_id;
  destructible.max_health = 50U;
  destructible.accepted_damage_channels =
      game::kDamageChannelMeleeV1 | game::kDamageChannelProjectileV1;
  destructible.local_hit_center = {0.0F, 0.0F, 0.0F};
  destructible.hit_radius = 0.75F;
  // Reversed input order proves that event ordinals follow canonical drop
  // order, not source order.
  destructible.drops = {
      {"loot/scrap", 2U, 0U},
      {"items/bolts", 3U, 0U},
  };

  DestructibleSceneV1 destructible_scene;
  destructible_scene.level_id = level_id;
  destructible_scene.destructibles.push_back(std::move(destructible));

  return {
      canonicalize_entity_scene_v1(std::move(base.entities),
                                   kLimits.entity_scene),
      std::move(base.gameplay),
      canonicalize_destructible_scene_v1(std::move(destructible_scene),
                                         kLimits.destructible_scene),
  };
}

[[nodiscard]] openrc::game::GameplayDamagePulseV1 damage_at_crate(
    const std::uint64_t attack_sequence, const std::uint32_t source_authored_id,
    const openrc::game::DamageChannelMaskV1 channel,
    const std::uint32_t damage) {
  return {
      attack_sequence,
      source_authored_id,
      channel,
      damage,
      {4.0, 0.0, 1.0},
      {4.0, 0.0, 1.0},
      0.25,
  };
}

[[nodiscard]] const openrc::game::EntityGameplayEntitySnapshotV1 &
find_snapshot_entity(const openrc::game::EntityGameplaySnapshotV1 &snapshot,
                     const std::uint32_t authored_id) {
  const auto found = std::lower_bound(
      snapshot.entities.begin(), snapshot.entities.end(), authored_id,
      [](const openrc::game::EntityGameplayEntitySnapshotV1 &entity,
         const std::uint32_t id) { return entity.authored_id < id; });
  if (found == snapshot.entities.end() || found->authored_id != authored_id) {
    throw std::runtime_error("test snapshot is missing authored entity " +
                             std::to_string(authored_id));
  }
  return *found;
}

[[nodiscard]] openrc::game::EntityGameplayEventV1 damage_event(
    const openrc::game::EntityGameplayEventKindV1 kind,
    const std::uint64_t tick_index, const std::uint64_t attack_sequence,
    const std::uint32_t source_authored_id, const std::uint32_t damage,
    const std::uint32_t remaining_health) {
  openrc::game::EntityGameplayEventV1 result;
  result.kind = kind;
  result.tick_index = tick_index;
  result.authored_id = kDestructibleAuthoredId;
  result.attack_sequence = attack_sequence;
  result.source_authored_id = source_authored_id;
  result.damage = damage;
  result.remaining_health = remaining_health;
  return result;
}

[[nodiscard]] openrc::game::EntityGameplayEventV1
grant_event(const std::uint64_t tick_index, const std::string &item_key,
            const std::uint32_t amount, const std::uint32_t drop_ordinal) {
  openrc::game::EntityGameplayEventV1 result;
  result.kind = openrc::game::EntityGameplayEventKindV1::item_granted;
  result.tick_index = tick_index;
  result.authored_id = kDestructibleAuthoredId;
  result.item_key = item_key;
  result.amount = amount;
  result.drop_ordinal = drop_ordinal;
  return result;
}

void test_materialization_collect_once_and_queries() {
  using namespace openrc::game;
  const auto scenes = make_scenes();
  EntityGameplayRuntimeV1 runtime;
  runtime.load_scene(scenes.entities, scenes.gameplay, kLimits);

  expect(runtime.loaded() && runtime.world().entity_count() == 5U &&
             runtime.snapshot().entities.size() == 5U,
         "entity gameplay did not materialize every definition");
  const auto player_id = runtime.find_entity_id(5U);
  const auto bolts_id = runtime.find_entity_id(20U);
  expect(player_id && bolts_id && player_id->slot == 0U &&
             bolts_id->slot == 1U &&
             runtime.world().find_entity(*player_id) != nullptr &&
             !runtime.find_authored_transform(5U) &&
             runtime.find_authored_transform(20U).has_value(),
         "authored IDs, player materialization, or immutable transforms are "
         "wrong");
  expect(!runtime.enabled(60U) && !runtime.collected(60U) &&
             runtime.world().find_entity(*runtime.find_entity_id(60U)) !=
                 nullptr,
         "an initially disabled definition was not retained as disabled world "
         "state");

  const auto first = runtime.fixed_tick(0U, player_at(0.0, 0.0));
  expect(first ==
             std::vector<EntityGameplayEventV1>{
                 {EntityGameplayEventKindV1::item_collected, 0U, 20U,
                  "items/bolts", 5U},
                 {EntityGameplayEventKindV1::item_collected, 0U, 40U,
                  "items/health", 1U},
             },
         "collectible events are not canonical or complete");
  expect(runtime.item_total("items/bolts") == 5U &&
             runtime.item_total("items/health") == 1U &&
             runtime.item_total("items/missing") == 0U,
         "neutral inventory totals are wrong");
  expect(!runtime.enabled(20U) && runtime.collected(20U) &&
             runtime.world().find_entity(*bolts_id) == nullptr &&
             !runtime.enabled(40U) && runtime.collected(40U),
         "collected entities were not disabled and destroyed exactly once");
  expect(!runtime.collected(60U) && runtime.enabled(80U),
         "disabled or out-of-range collectibles were consumed");

  const auto second = runtime.fixed_tick(1U, player_at(0.0, 0.0));
  expect(second.empty() && runtime.item_total("items/bolts") == 5U,
         "a collectible emitted twice or changed inventory twice");
  expect_runtime_error(
      [&] { static_cast<void>(runtime.fixed_tick(1U, player_at(0.0, 0.0))); },
      "a duplicate fixed tick was accepted");
}

void test_canonicalized_input_order_is_irrelevant() {
  using namespace openrc;
  using namespace openrc::game;
  const auto canonical = make_scenes();
  auto shuffled = canonical;
  std::reverse(shuffled.entities.definitions.begin(),
               shuffled.entities.definitions.end());
  std::reverse(shuffled.entities.transforms.begin(),
               shuffled.entities.transforms.end());
  std::reverse(shuffled.gameplay.collectibles.begin(),
               shuffled.gameplay.collectibles.end());
  shuffled.entities = canonicalize_entity_scene_v1(std::move(shuffled.entities),
                                                   kLimits.entity_scene);
  shuffled.gameplay = canonicalize_gameplay_scene_v1(
      std::move(shuffled.gameplay), kLimits.gameplay_scene);

  EntityGameplayRuntimeV1 first;
  EntityGameplayRuntimeV1 second;
  first.load_scene(canonical.entities, canonical.gameplay, kLimits);
  second.load_scene(shuffled.entities, shuffled.gameplay, kLimits);
  expect(first.snapshot() == second.snapshot(),
         "canonical-equivalent input changed materialized entity IDs");
  expect(first.fixed_tick(0U, player_at(0.0, 0.0)) ==
             second.fixed_tick(0U, player_at(0.0, 0.0)),
         "canonical-equivalent input changed event order");
}

void test_reload_preserves_inventory_and_replaces_world() {
  using namespace openrc::game;
  EntityGameplayRuntimeV1 runtime;
  const auto first_scene = make_scenes(7U);
  runtime.load_scene(first_scene.entities, first_scene.gameplay, kLimits);
  static_cast<void>(runtime.fixed_tick(0U, player_at(0.0, 0.0)));
  const auto old_bolts_id = *runtime.find_entity_id(20U);

  const auto second_scene = make_scenes(8U);
  runtime.load_scene(second_scene.entities, second_scene.gameplay, kLimits,
                     50U);
  const auto reloaded = runtime.snapshot();
  const auto new_bolts_id = *runtime.find_entity_id(20U);
  expect(reloaded.level_id == 8U && reloaded.level_instance_sequence == 2U &&
             reloaded.next_tick_index == 50U &&
             runtime.item_total("items/bolts") == 5U &&
             runtime.item_total("items/health") == 1U,
         "reload lost persistent item totals or explicit tick origin");
  expect(new_bolts_id.level_instance_sequence !=
                 old_bolts_id.level_instance_sequence &&
             runtime.world().find_entity(old_bolts_id) == nullptr &&
             runtime.world().find_entity(new_bolts_id) != nullptr &&
             runtime.enabled(20U) && !runtime.collected(20U),
         "reload retained stale world IDs or collectible state");

  const auto next = runtime.fixed_tick(50U, player_at(0.0, 0.0));
  expect(next.size() == 2U && runtime.item_total("items/bolts") == 10U &&
             runtime.item_total("items/health") == 2U,
         "reloaded scene did not add to persistent semantic totals");
}

void test_bad_cross_resource_loads_are_transactional() {
  using namespace openrc;
  using namespace openrc::game;
  EntityGameplayRuntimeV1 runtime;
  const auto valid = make_scenes();
  runtime.load_scene(valid.entities, valid.gameplay, kLimits);
  const auto before = runtime.snapshot();

  auto wrong_level = valid.gameplay;
  wrong_level.level_id = valid.entities.level_id + 1U;
  expect_runtime_error(
      [&] { runtime.load_scene(valid.entities, wrong_level, kLimits); },
      "different EntitySceneV1 and GameplaySceneV1 levels were accepted");
  expect(runtime.snapshot() == before,
         "a wrong-level reload changed the live runtime");

  auto missing_entity = valid.gameplay;
  missing_entity.collectibles.back().authored_id = 999U;
  missing_entity = canonicalize_gameplay_scene_v1(std::move(missing_entity),
                                                  kLimits.gameplay_scene);
  expect_runtime_error(
      [&] { runtime.load_scene(valid.entities, missing_entity, kLimits); },
      "a collectible referencing a missing definition was accepted");
  expect(runtime.snapshot() == before,
         "a missing-entity reload changed the live runtime");

  GameplaySceneV1 player_collectible;
  player_collectible.level_id = valid.entities.level_id;
  player_collectible.collectibles = {
      GameplayCollectibleV1{5U, "items/player", {}, 1U, 1.0F, 0U}};
  player_collectible = canonicalize_gameplay_scene_v1(
      std::move(player_collectible), kLimits.gameplay_scene);
  expect_runtime_error(
      [&] { runtime.load_scene(valid.entities, player_collectible, kLimits); },
      "a collectible without an authored transform was accepted");
  expect(runtime.snapshot() == before,
         "a missing-transform reload changed the live runtime");

  auto invalid_entities = valid.entities;
  invalid_entities.transforms.erase(invalid_entities.transforms.begin());
  expect_runtime_error(
      [&] { runtime.load_scene(invalid_entities, valid.gameplay, kLimits); },
      "an invalid canonical entity scene was accepted");
  expect(runtime.snapshot() == before,
         "an invalid-entity reload changed the live runtime");
}

void test_destructible_load_and_cross_resource_validation() {
  using namespace openrc;
  using namespace openrc::game;
  const auto scenes = make_destructible_scenes();
  EntityGameplayRuntimeV1 runtime;
  runtime.load_scene(scenes.entities, scenes.gameplay, scenes.destructibles,
                     kLimits);

  const auto crate_id = runtime.find_entity_id(kDestructibleAuthoredId);
  const auto loaded = runtime.snapshot();
  const auto &crate =
      find_snapshot_entity(loaded, kDestructibleAuthoredId);
  expect(crate_id.has_value() && runtime.world().entity_count() == 6U &&
             runtime.world().find_entity(*crate_id) != nullptr &&
             runtime.enabled(kDestructibleAuthoredId) &&
             !runtime.collected(kDestructibleAuthoredId) &&
             !runtime.destroyed(kDestructibleAuthoredId) &&
             runtime.health(kDestructibleAuthoredId) == 50U &&
             !runtime.health(20U) && crate.health == 50U &&
             !crate.destroyed && crate.damage_source_sequences.empty(),
         "destructible state was not materialized with exact initial health");

  const auto center = world_destructible_center_v1(
      *runtime.find_authored_transform(kDestructibleAuthoredId),
      scenes.destructibles.destructibles.front());
  expect(std::abs(center.x - 4.0) < 0.000001 &&
             std::abs(center.y) < 0.000001 &&
             std::abs(center.z - 1.0) < 0.000001,
         "destructible local hit center was not mapped into world space");

  auto wrong_level = scenes.destructibles;
  ++wrong_level.level_id;
  expect_runtime_error(
      [&] {
        runtime.load_scene(scenes.entities, scenes.gameplay, wrong_level,
                           kLimits);
      },
      "different EntitySceneV1 and DestructibleSceneV1 levels were accepted");

  auto missing_definition = scenes.destructibles;
  missing_definition.destructibles.front().authored_id = 999U;
  missing_definition = canonicalize_destructible_scene_v1(
      std::move(missing_definition), kLimits.destructible_scene);
  expect_runtime_error(
      [&] {
        runtime.load_scene(scenes.entities, scenes.gameplay,
                           missing_definition, kLimits);
      },
      "a destructible referencing a missing definition was accepted");

  auto missing_transform = scenes.entities;
  std::erase_if(missing_transform.transforms, [](const auto &transform) {
    return transform.authored_id == kDestructibleAuthoredId;
  });
  expect_runtime_error(
      [&] {
        runtime.load_scene(missing_transform, scenes.gameplay,
                           scenes.destructibles, kLimits);
      },
      "a destructible without an authored transform was accepted");

  auto nonuniform_transform = scenes.entities;
  const auto transform = std::lower_bound(
      nonuniform_transform.transforms.begin(),
      nonuniform_transform.transforms.end(), kDestructibleAuthoredId,
      [](const auto &candidate, const std::uint32_t authored_id) {
        return candidate.authored_id < authored_id;
      });
  expect(transform != nonuniform_transform.transforms.end() &&
             transform->authored_id == kDestructibleAuthoredId,
         "the destructible test fixture lost its authored transform");
  transform->transform.scale = {100.0F, 1.0F, 1.0F};
  expect_runtime_error(
      [&] {
        runtime.load_scene(nonuniform_transform, scenes.gameplay,
                           scenes.destructibles, kLimits);
      },
      "a destructible with a non-uniform entity scale was accepted");

  auto distant_world_sphere = scenes.entities;
  const auto distant_transform = std::lower_bound(
      distant_world_sphere.transforms.begin(),
      distant_world_sphere.transforms.end(), kDestructibleAuthoredId,
      [](const auto &candidate, const std::uint32_t authored_id) {
        return candidate.authored_id < authored_id;
      });
  distant_transform->transform.position[0U] = static_cast<float>(
      kGameplayDamageMaximumGeometryMagnitudeV1 * 2.0);
  expect_runtime_error(
      [&] {
        runtime.load_scene(distant_world_sphere, scenes.gameplay,
                           scenes.destructibles, kLimits);
      },
      "an out-of-domain world hit-sphere center was accepted");

  auto oversized_world_sphere = scenes.entities;
  const auto oversized_transform = std::lower_bound(
      oversized_world_sphere.transforms.begin(),
      oversized_world_sphere.transforms.end(), kDestructibleAuthoredId,
      [](const auto &candidate, const std::uint32_t authored_id) {
        return candidate.authored_id < authored_id;
      });
  const auto oversized_scale = static_cast<float>(
      kGameplayDamageMaximumGeometryMagnitudeV1 * 2.0);
  oversized_transform->transform.scale = {
      oversized_scale, oversized_scale, oversized_scale};
  expect_runtime_error(
      [&] {
        runtime.load_scene(oversized_world_sphere, scenes.gameplay,
                           scenes.destructibles, kLimits);
      },
      "an oversized transformed world hit sphere was accepted");

  auto undersized_world_sphere = scenes.entities;
  const auto undersized_transform = std::lower_bound(
      undersized_world_sphere.transforms.begin(),
      undersized_world_sphere.transforms.end(), kDestructibleAuthoredId,
      [](const auto &candidate, const std::uint32_t authored_id) {
        return candidate.authored_id < authored_id;
      });
  undersized_transform->transform.scale = {
      1.0F / 128.0F, 1.0F / 128.0F, 1.0F / 128.0F};
  expect_runtime_error(
      [&] {
        runtime.load_scene(undersized_world_sphere, scenes.gameplay,
                           scenes.destructibles, kLimits);
      },
      "a sub-Q6 transformed world hit sphere was accepted");

  auto collectible_conflict = scenes.destructibles;
  collectible_conflict.destructibles.front().authored_id = 20U;
  collectible_conflict = canonicalize_destructible_scene_v1(
      std::move(collectible_conflict), kLimits.destructible_scene);
  expect_runtime_error(
      [&] {
        runtime.load_scene(scenes.entities, scenes.gameplay,
                           collectible_conflict, kLimits);
      },
      "one authored entity was accepted as collectible and destructible");

  auto inventory_limited = kLimits;
  inventory_limited.inventory.max_item_totals = 2U;
  expect_runtime_error(
      [&] {
        runtime.load_scene(scenes.entities, scenes.gameplay,
                           scenes.destructibles, inventory_limited);
      },
      "destructible drop keys were omitted from the inventory key union");
  expect(runtime.snapshot() == loaded && runtime.world().entity_count() == 6U,
         "a rejected destructible reload partially replaced live state");
}

void test_destructible_damage_channels_health_events_and_attack_identity() {
  using namespace openrc::game;
  const auto scenes = make_destructible_scenes();
  EntityGameplayRuntimeV1 runtime;
  runtime.load_scene(scenes.entities, scenes.gameplay, scenes.destructibles,
                     kLimits);
  const auto distant_player = player_at(50.0, 50.0);

  const auto tick_with = [&](const std::uint64_t tick_index,
                             const GameplayDamagePulseV1 &pulse) {
    const std::array pulses{pulse};
    return runtime.fixed_tick(tick_index, distant_player, pulses);
  };

  const auto rejected = damage_at_crate(
      1U, 5U, kDamageChannelExplosiveV1, 20U);
  expect(tick_with(0U, rejected).empty() &&
             runtime.health(kDestructibleAuthoredId) == 50U,
         "a destructible accepted a damage channel outside its authored mask");

  const auto active_melee =
      damage_at_crate(1U, 5U, kDamageChannelMeleeV1, 20U);
  expect(tick_with(1U, active_melee) ==
             std::vector<EntityGameplayEventV1>{damage_event(
                 EntityGameplayEventKindV1::entity_damaged, 1U, 1U, 5U, 20U,
                 30U)} &&
             runtime.health(kDestructibleAuthoredId) == 30U,
         "an accepted damage pulse did not reduce health exactly once");
  const auto once = runtime.snapshot();
  const auto &once_crate =
      find_snapshot_entity(once, kDestructibleAuthoredId);
  expect(once_crate.health == 30U &&
             once_crate.damage_source_sequences ==
                 std::vector<EntityGameplayDamageSourceSequenceV1>{
                     {5U, 1U}},
         "snapshot lost destructible health or last-hit identity");

  expect(tick_with(2U, active_melee).empty() &&
             runtime.health(kDestructibleAuthoredId) == 30U,
         "an active attack damaged the same entity on a second tick");

  const auto same_attack_other_source =
      damage_at_crate(1U, 6U, kDamageChannelMeleeV1, 20U);
  expect(tick_with(3U, same_attack_other_source) ==
             std::vector<EntityGameplayEventV1>{damage_event(
                 EntityGameplayEventKindV1::entity_damaged, 3U, 1U, 6U, 20U,
                 10U)} &&
             runtime.health(kDestructibleAuthoredId) == 10U,
         "attack identity did not include its authored source");

  expect(tick_with(4U, active_melee).empty() &&
             tick_with(5U, same_attack_other_source).empty() &&
             runtime.health(kDestructibleAuthoredId) == 10U,
         "alternating damage sources bypassed per-source attack replay "
         "protection");

  const auto finishing_projectile =
      damage_at_crate(2U, 5U, kDamageChannelProjectileV1, 100U);
  const auto destroyed = tick_with(6U, finishing_projectile);
  expect(destroyed ==
             std::vector<EntityGameplayEventV1>{
                 damage_event(EntityGameplayEventKindV1::entity_damaged, 6U,
                               2U, 5U, 10U, 0U),
                 damage_event(EntityGameplayEventKindV1::entity_destroyed, 6U,
                               2U, 5U, 10U, 0U),
                 grant_event(6U, "items/bolts", 3U, 0U),
                 grant_event(6U, "loot/scrap", 2U, 1U),
             },
         "destruction did not emit exact damage, destroy, and drop events");
  expect(runtime.health(kDestructibleAuthoredId) == 0U &&
             runtime.destroyed(kDestructibleAuthoredId) &&
             !runtime.enabled(kDestructibleAuthoredId) &&
             !runtime.collected(kDestructibleAuthoredId) &&
             runtime.item_total("items/bolts") == 3U &&
             runtime.item_total("loot/scrap") == 2U &&
             runtime.world().find_entity(
                 *runtime.find_entity_id(kDestructibleAuthoredId)) == nullptr,
         "destruction did not commit health, world, flags, and inventory");
  expect(tick_with(7U, finishing_projectile).empty() &&
             runtime.item_total("items/bolts") == 3U,
         "a destroyed entity accepted another active damage pulse");
}

void test_out_of_domain_damage_pulses_are_transactional() {
  using namespace openrc::game;
  const auto scenes = make_destructible_scenes();
  EntityGameplayRuntimeV1 runtime;
  runtime.load_scene(scenes.entities, scenes.gameplay, scenes.destructibles,
                     kLimits);
  const auto before = runtime.snapshot();

  auto projection_overflow =
      damage_at_crate(1U, 5U, kDamageChannelMeleeV1, 20U);
  projection_overflow.capsule_start = {3.0e155, 3.0e155, 1.0};
  projection_overflow.capsule_end = {3.08e155, 2.92e155, 1.0};
  projection_overflow.radius = 5.0e155;
  const std::array projection_pulses{projection_overflow};
  expect_runtime_error(
      [&] {
        static_cast<void>(runtime.fixed_tick(
            0U, player_at(50.0, 50.0), projection_pulses));
      },
      "a pulse whose projection dot product overflows was accepted");
  expect(runtime.snapshot() == before,
         "projection-domain rejection partially committed gameplay state");

  auto near_binary64_limit =
      damage_at_crate(1U, 5U, kDamageChannelMeleeV1, 20U);
  const auto maximum = std::numeric_limits<double>::max();
  near_binary64_limit.capsule_start = {maximum / 2.0, 0.0, 0.0};
  near_binary64_limit.capsule_end = near_binary64_limit.capsule_start;
  near_binary64_limit.radius = maximum / 4.0;
  const std::array limit_pulses{near_binary64_limit};
  expect_runtime_error(
      [&] {
        static_cast<void>(runtime.fixed_tick(
            0U, player_at(50.0, 50.0), limit_pulses));
      },
      "a pulse near the binary64 limit was accepted");
  expect(runtime.snapshot() == before,
         "binary64-limit rejection partially committed gameplay state");

  auto adjacent_coordinate =
      damage_at_crate(1U, 5U, kDamageChannelMeleeV1, 20U);
  adjacent_coordinate.capsule_start.x = std::nextafter(
      kGameplayDamageMaximumGeometryMagnitudeV1,
      std::numeric_limits<double>::infinity());
  adjacent_coordinate.capsule_end = adjacent_coordinate.capsule_start;
  const std::array adjacent_coordinate_pulses{adjacent_coordinate};
  expect_runtime_error(
      [&] {
        static_cast<void>(runtime.fixed_tick(
            0U, player_at(50.0, 50.0), adjacent_coordinate_pulses));
      },
      "a pulse one binary64 step beyond the coordinate domain was accepted");
  expect(runtime.snapshot() == before,
         "adjacent coordinate rejection partially committed gameplay state");

  auto subminimum_radius =
      damage_at_crate(1U, 5U, kDamageChannelMeleeV1, 20U);
  subminimum_radius.radius =
      std::nextafter(kGameplayDamageMinimumRadiusV1, 0.0);
  const std::array subminimum_radius_pulses{subminimum_radius};
  expect_runtime_error(
      [&] {
        static_cast<void>(runtime.fixed_tick(
            0U, player_at(50.0, 50.0), subminimum_radius_pulses));
      },
      "a pulse one binary64 step below the radius domain was accepted");
  expect(runtime.snapshot() == before,
         "adjacent radius rejection partially committed gameplay state");
}

void test_damage_capsule_endpoint_and_long_interior_regions() {
  using namespace openrc::game;
  const auto scenes = make_destructible_scenes();
  const auto distant_player = player_at(50.0, 50.0);

  EntityGameplayRuntimeV1 interior_runtime;
  interior_runtime.load_scene(scenes.entities, scenes.gameplay,
                              scenes.destructibles, kLimits);
  auto long_interior =
      damage_at_crate(1U, 5U, kDamageChannelMeleeV1, 20U);
  long_interior.capsule_start = {
      -kGameplayDamageMaximumGeometryMagnitudeV1, 0.0, 1.0};
  long_interior.capsule_end = {
      kGameplayDamageMaximumGeometryMagnitudeV1, 0.0, 1.0};
  long_interior.radius = kGameplayDamageMinimumRadiusV1;
  const std::array interior_pulses{long_interior};
  expect(interior_runtime.fixed_tick(0U, distant_player, interior_pulses) ==
             std::vector<EntityGameplayEventV1>{damage_event(
                 EntityGameplayEventKindV1::entity_damaged, 0U, 1U, 5U, 20U,
                 30U)},
         "a boundary-valid long segment lost its interior overlap to "
         "projection cancellation");

  EntityGameplayRuntimeV1 diagonal_runtime;
  diagonal_runtime.load_scene(scenes.entities, scenes.gameplay,
                              scenes.destructibles, kLimits);
  auto diagonal_miss =
      damage_at_crate(1U, 5U, kDamageChannelMeleeV1, 20U);
  diagonal_miss.capsule_start = {
      -kGameplayDamageMaximumGeometryMagnitudeV1,
      -kGameplayDamageMaximumGeometryMagnitudeV1, 1.0};
  diagonal_miss.capsule_end = {
      kGameplayDamageMaximumGeometryMagnitudeV1,
      kGameplayDamageMaximumGeometryMagnitudeV1, 1.0};
  const std::array diagonal_pulses{diagonal_miss};
  expect(diagonal_runtime.fixed_tick(0U, distant_player, diagonal_pulses)
             .empty() &&
             diagonal_runtime.health(kDestructibleAuthoredId) == 50U,
         "cross-product cancellation caused a boundary-valid diagonal "
         "segment to hit a distant sphere");

  EntityGameplayRuntimeV1 start_runtime;
  start_runtime.load_scene(scenes.entities, scenes.gameplay,
                           scenes.destructibles, kLimits);
  auto before_start =
      damage_at_crate(1U, 5U, kDamageChannelMeleeV1, 20U);
  before_start.capsule_start = {
      std::nextafter(5.0, std::numeric_limits<double>::infinity()), 0.0, 1.0};
  before_start.capsule_end = {6.0, 0.0, 1.0};
  const std::array before_start_pulses{before_start};
  expect(start_runtime.fixed_tick(0U, distant_player, before_start_pulses)
             .empty(),
         "the infinite line before a capsule start was treated as interior");
  before_start.capsule_start.x = 5.0;
  const std::array touching_start_pulses{before_start};
  expect(start_runtime.fixed_tick(1U, distant_player, touching_start_pulses) ==
             std::vector<EntityGameplayEventV1>{damage_event(
                 EntityGameplayEventKindV1::entity_damaged, 1U, 1U, 5U, 20U,
                 30U)},
         "the inclusive capsule start sphere did not register contact");

  EntityGameplayRuntimeV1 end_runtime;
  end_runtime.load_scene(scenes.entities, scenes.gameplay,
                         scenes.destructibles, kLimits);
  auto after_end =
      damage_at_crate(1U, 5U, kDamageChannelMeleeV1, 20U);
  after_end.capsule_start = {6.0, 0.0, 1.0};
  after_end.capsule_end = {
      std::nextafter(5.0, std::numeric_limits<double>::infinity()), 0.0, 1.0};
  const std::array after_end_pulses{after_end};
  expect(end_runtime.fixed_tick(0U, distant_player, after_end_pulses).empty(),
         "the infinite line after a capsule end was treated as interior");
  after_end.capsule_end.x = 5.0;
  const std::array touching_end_pulses{after_end};
  expect(end_runtime.fixed_tick(1U, distant_player, touching_end_pulses) ==
             std::vector<EntityGameplayEventV1>{damage_event(
                 EntityGameplayEventKindV1::entity_damaged, 1U, 1U, 5U, 20U,
                 30U)},
         "the inclusive capsule end sphere did not register contact");
}

void test_destructible_damage_replay_history_is_bounded_and_transactional() {
  using namespace openrc::game;
  const auto scenes = make_destructible_scenes();
  const auto distant_player = player_at(50.0, 50.0);

  auto bounded_limits = kLimits;
  bounded_limits.max_damage_sources_per_destructible = 1U;
  EntityGameplayRuntimeV1 bounded_runtime;
  bounded_runtime.load_scene(scenes.entities, scenes.gameplay,
                             scenes.destructibles, bounded_limits);
  const std::array first_source{damage_at_crate(
      2U, 5U, kDamageChannelMeleeV1, 1U)};
  static_cast<void>(
      bounded_runtime.fixed_tick(0U, distant_player, first_source));
  const auto before_new_source = bounded_runtime.snapshot();
  const std::array second_source{damage_at_crate(
      1U, 6U, kDamageChannelMeleeV1, 1U)};
  expect_runtime_error(
      [&] {
        static_cast<void>(bounded_runtime.fixed_tick(
            1U, distant_player, second_source));
      },
      "a destructible accepted more damage sources than its replay-history "
      "limit");
  expect(bounded_runtime.snapshot() == before_new_source &&
             bounded_runtime.next_tick_index() == 1U,
         "damage-source limit failure partially committed gameplay state");

  const std::array regressed_source{damage_at_crate(
      1U, 5U, kDamageChannelMeleeV1, 1U)};
  expect_runtime_error(
      [&] {
        static_cast<void>(bounded_runtime.fixed_tick(
            1U, distant_player, regressed_source));
      },
      "a damage source was allowed to regress its attack sequence");
  expect(bounded_runtime.snapshot() == before_new_source &&
             bounded_runtime.next_tick_index() == 1U,
         "regressed damage sequence partially committed gameplay state");
}

void test_destructible_drop_overflow_is_transactional() {
  using namespace openrc::game;
  const auto scenes = make_destructible_scenes();
  EntityGameplayRuntimeV1 runtime;
  runtime.load_scene(scenes.entities, scenes.gameplay, scenes.destructibles,
                     kLimits);
  runtime.restore_item_totals({
      {"loot/scrap", std::numeric_limits<std::uint64_t>::max() - 1U},
  });
  const auto before = runtime.snapshot();
  const auto crate_id =
      *runtime.find_entity_id(kDestructibleAuthoredId);
  const std::array pulse{damage_at_crate(
      1U, 5U, kDamageChannelProjectileV1, 100U)};

  expect_runtime_error(
      [&] {
        static_cast<void>(runtime.fixed_tick(
            0U, player_at(50.0, 50.0), pulse));
      },
      "an overflowing destructible drop total was accepted");
  expect(runtime.snapshot() == before &&
             runtime.enabled(kDestructibleAuthoredId) &&
             !runtime.destroyed(kDestructibleAuthoredId) &&
             runtime.health(kDestructibleAuthoredId) == 50U &&
             runtime.next_tick_index() == 0U &&
             runtime.item_total("items/bolts") == 0U &&
             runtime.item_total("loot/scrap") ==
                 std::numeric_limits<std::uint64_t>::max() - 1U &&
             runtime.world().find_entity(crate_id) != nullptr,
         "drop overflow partially committed health, world, inventory, or tick");
}

void test_destructibles_coexist_with_collectibles_and_inventory_restore() {
  using namespace openrc::game;
  const auto scenes = make_destructible_scenes();
  EntityGameplayRuntimeV1 runtime;
  runtime.load_scene(scenes.entities, scenes.gameplay, scenes.destructibles,
                     kLimits);
  runtime.restore_item_totals({
      {"items/bolts", 10U},
      {"loot/scrap", 4U},
  });
  const auto restored = runtime.snapshot();
  expect(restored.item_totals ==
             std::vector<EntityGameplayItemTotalV1>{
                 {"items/bolts", 10U},
                 {"loot/scrap", 4U},
             } &&
             find_snapshot_entity(restored, kDestructibleAuthoredId).health ==
                 50U,
         "snapshot did not preserve restored inventory and destructible state");

  const std::array pulse{damage_at_crate(
      1U, 5U, kDamageChannelProjectileV1, 50U)};
  const auto events = runtime.fixed_tick(0U, player_at(0.0, 0.0), pulse);
  expect(events ==
             std::vector<EntityGameplayEventV1>{
                 damage_event(EntityGameplayEventKindV1::entity_damaged, 0U,
                              1U, 5U, 50U, 0U),
                 damage_event(EntityGameplayEventKindV1::entity_destroyed, 0U,
                              1U, 5U, 50U, 0U),
                 grant_event(0U, "items/bolts", 3U, 0U),
                 grant_event(0U, "loot/scrap", 2U, 1U),
                 {EntityGameplayEventKindV1::item_collected, 0U, 20U,
                  "items/bolts", 5U},
                 {EntityGameplayEventKindV1::item_collected, 0U, 40U,
                  "items/health", 1U},
             },
         "collectible and destructible events did not coexist deterministically");
  expect(runtime.collected(20U) && runtime.collected(40U) &&
             runtime.destroyed(kDestructibleAuthoredId) &&
             runtime.item_total("items/bolts") == 18U &&
             runtime.item_total("items/health") == 1U &&
             runtime.item_total("loot/scrap") == 6U,
         "combined collectible and destructible inventory totals are wrong");

  const auto after = runtime.snapshot();
  EntityGameplayRuntimeV1 restored_runtime;
  restored_runtime.load_scene(scenes.entities, scenes.gameplay,
                              scenes.destructibles, kLimits);
  restored_runtime.restore_item_totals(after.item_totals);
  expect(restored_runtime.snapshot().item_totals == after.item_totals &&
             restored_runtime.health(kDestructibleAuthoredId) == 50U &&
             !restored_runtime.destroyed(kDestructibleAuthoredId),
         "snapshot inventory could not be restored with destructible drop keys");
}

void test_item_overflow_is_transactional() {
  using namespace openrc::game;
  EntityGameplayRuntimeV1 runtime;
  const auto scenes = make_scenes();
  runtime.load_scene(scenes.entities, scenes.gameplay, kLimits);
  runtime.restore_item_totals({
      {"items/bolts", std::numeric_limits<std::uint64_t>::max() - 4U},
  });
  const auto before = runtime.snapshot();
  expect_runtime_error(
      [&] { static_cast<void>(runtime.fixed_tick(0U, player_at(0.0, 0.0))); },
      "an overflowing semantic item total was accepted");
  expect(runtime.snapshot() == before && runtime.enabled(20U) &&
             !runtime.collected(20U) &&
             runtime.world().find_entity(*runtime.find_entity_id(20U)) !=
                 nullptr,
         "overflow partially mutated inventory, entity state, or WorldV1");
}

void test_allocation_failures_are_transactional() {
  using namespace openrc::game;
  const auto scenes = make_scenes();
  std::size_t injected_failures = 0U;
  bool completed = false;

  // Advance the failure point through every allocation made by a successful
  // tick. This includes WorldV1::destroy_entity growing its free-slot table
  // after it has already cleared a slot in the staged world.
  for (std::size_t failure_point = 0U; failure_point < 64U; ++failure_point) {
    EntityGameplayRuntimeV1 runtime;
    runtime.load_scene(scenes.entities, scenes.gameplay, kLimits);
    const auto before = runtime.snapshot();
    const auto before_entity_count = runtime.world().entity_count();
    const auto bolts_id = *runtime.find_entity_id(20U);
    const auto health_id = *runtime.find_entity_id(40U);

    allocation_failure::fail_after(failure_point);
    try {
      const auto events = runtime.fixed_tick(0U, player_at(0.0, 0.0));
      allocation_failure::disable();
      expect(events.size() == 2U,
             "allocation fault sweep changed the successful tick result");
      completed = true;
      break;
    } catch (const std::bad_alloc &) {
      allocation_failure::disable();
      ++injected_failures;
      expect(runtime.snapshot() == before &&
                 runtime.world().entity_count() == before_entity_count &&
                 runtime.world().find_entity(bolts_id) != nullptr &&
                 runtime.world().find_entity(health_id) != nullptr,
             "an allocation failure partially committed gameplay state");
    } catch (...) {
      allocation_failure::disable();
      throw;
    }
  }

  expect(completed && injected_failures != 0U,
         "allocation fault sweep did not reach a successful fixed tick");
}

void test_local_center_and_conservative_non_uniform_scale() {
  using namespace openrc;
  using namespace openrc::game;
  auto scenes = make_scenes();
  auto &transform = scenes.entities.transforms.front();
  transform.transform.position = {10.0F, 10.0F, 0.0F};
  transform.transform.rotation = {0.0F, 0.0F, 0.70710677F, 0.70710677F};
  transform.transform.scale = {2.0F, 1.0F, 1.0F};
  auto &collectible = scenes.gameplay.collectibles.front();
  collectible.local_center = {1.0F, 0.0F, 1.0F};
  collectible.collection_radius = 0.25F;
  scenes.entities = canonicalize_entity_scene_v1(std::move(scenes.entities),
                                                 kLimits.entity_scene);
  scenes.gameplay = canonicalize_gameplay_scene_v1(std::move(scenes.gameplay),
                                                   kLimits.gameplay_scene);

  const auto center = world_collectible_center_v1(
      scenes.entities.transforms.front().transform,
      scenes.gameplay.collectibles.front());
  expect(std::abs(center.x - 10.0) < 0.000001 &&
             std::abs(center.y - 12.0) < 0.000001 &&
             std::abs(center.z - 1.0) < 0.000001,
         "exported collectible world-center conversion is wrong");

  EntityGameplayRuntimeV1 runtime;
  runtime.load_scene(scenes.entities, scenes.gameplay, kLimits);
  const auto events = runtime.fixed_tick(0U, player_at(10.0, 12.9));
  expect(events.size() == 1U && events.front().authored_id == 20U,
         "local collectible center or conservative sphere scale is wrong");
}

void test_unloaded_queries_and_bad_capsule() {
  using namespace openrc::game;
  EntityGameplayRuntimeV1 runtime;
  expect(!runtime.loaded() && !runtime.find_entity_id(7U) &&
             runtime.item_total("items/bolts") == 0U,
         "unloaded non-throwing queries are inconsistent");
  expect_runtime_error([&] { static_cast<void>(runtime.snapshot()); },
                       "an unloaded snapshot was returned");

  const auto scenes = make_scenes();
  runtime.load_scene(scenes.entities, scenes.gameplay, kLimits);
  auto invalid = player_at(0.0, 0.0);
  invalid.radius = 0.0;
  const auto before = runtime.snapshot();
  expect_runtime_error(
      [&] { static_cast<void>(runtime.fixed_tick(0U, invalid)); },
      "an invalid player capsule was accepted");
  expect(runtime.snapshot() == before,
         "an invalid capsule changed deterministic gameplay state");
}

} // namespace

int main() {
  try {
    test_materialization_collect_once_and_queries();
    test_canonicalized_input_order_is_irrelevant();
    test_reload_preserves_inventory_and_replaces_world();
    test_bad_cross_resource_loads_are_transactional();
    test_destructible_load_and_cross_resource_validation();
    test_destructible_damage_channels_health_events_and_attack_identity();
    test_destructible_damage_replay_history_is_bounded_and_transactional();
    test_out_of_domain_damage_pulses_are_transactional();
    test_damage_capsule_endpoint_and_long_interior_regions();
    test_destructible_drop_overflow_is_transactional();
    test_destructibles_coexist_with_collectibles_and_inventory_restore();
    test_item_overflow_is_transactional();
    test_allocation_failures_are_transactional();
    test_local_center_and_conservative_non_uniform_scale();
    test_unloaded_queries_and_bad_capsule();
    std::cout << "OpenRC runtime gameplay-scene tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OpenRC runtime gameplay-scene tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
