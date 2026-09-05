#include "openrc/runtime_gameplay_scene.hpp"

#include <algorithm>
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
    openrc::game::EntityGameplayInventoryLimitsV1{32U, 64U, 1024U},
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
