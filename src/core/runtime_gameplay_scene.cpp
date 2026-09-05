#include "openrc/runtime_gameplay_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace openrc::game {

CollisionVectorV1 world_collectible_center_v1(
    const WorldTransformV1 &transform,
    const GameplayCollectibleV1 &collectible) noexcept {
  const auto local_x = static_cast<double>(collectible.local_center[0U]) *
                       static_cast<double>(transform.scale[0U]);
  const auto local_y = static_cast<double>(collectible.local_center[1U]) *
                       static_cast<double>(transform.scale[1U]);
  const auto local_z = static_cast<double>(collectible.local_center[2U]) *
                       static_cast<double>(transform.scale[2U]);
  const auto qx = static_cast<double>(transform.rotation[0U]);
  const auto qy = static_cast<double>(transform.rotation[1U]);
  const auto qz = static_cast<double>(transform.rotation[2U]);
  const auto qw = static_cast<double>(transform.rotation[3U]);

  const auto tx = 2.0 * (qy * local_z - qz * local_y);
  const auto ty = 2.0 * (qz * local_x - qx * local_z);
  const auto tz = 2.0 * (qx * local_y - qy * local_x);
  const auto rotated_x = local_x + qw * tx + (qy * tz - qz * ty);
  const auto rotated_y = local_y + qw * ty + (qz * tx - qx * tz);
  const auto rotated_z = local_z + qw * tz + (qx * ty - qy * tx);
  return {
      static_cast<double>(transform.position[0U]) + rotated_x,
      static_cast<double>(transform.position[1U]) + rotated_y,
      static_cast<double>(transform.position[2U]) + rotated_z,
  };
}

namespace {

[[noreturn]] void fail(const std::string &message) {
  throw EntityGameplayRuntimeError(message);
}

[[nodiscard]] bool item_key_character(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('_') ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('/');
}

void validate_item_key(const std::string_view value,
                       const std::uint32_t maximum_bytes) {
  if (value.empty() || value.size() > maximum_bytes || value.front() == '/' ||
      value.back() == '/') {
    fail("Entity-gameplay inventory contains a non-canonical item key");
  }
  for (const char character : value) {
    if (!item_key_character(static_cast<unsigned char>(character))) {
      fail("Entity-gameplay inventory item key has a non-canonical character");
    }
  }
  std::size_t component_begin = 0U;
  while (component_begin < value.size()) {
    const auto separator = value.find('/', component_begin);
    const auto component_end =
        separator == std::string_view::npos ? value.size() : separator;
    const auto component =
        value.substr(component_begin, component_end - component_begin);
    if (component.empty() || component == "." || component == "..") {
      fail("Entity-gameplay inventory item key has an unsafe component");
    }
    if (separator == std::string_view::npos) {
      break;
    }
    component_begin = separator + 1U;
  }
}

void validate_inventory_limits(const EntityGameplayInventoryLimitsV1 &limits) {
  if (limits.max_item_totals == 0U || limits.max_item_key_bytes == 0U ||
      limits.max_total_item_key_bytes == 0U) {
    fail("Entity-gameplay inventory limits must all be explicit and non-zero");
  }
}

void validate_item_totals(const std::vector<EntityGameplayItemTotalV1> &totals,
                          const EntityGameplayInventoryLimitsV1 &limits) {
  validate_inventory_limits(limits);
  if (totals.size() > limits.max_item_totals) {
    fail("Entity-gameplay inventory exceeds its item-total limit");
  }
  std::uint64_t total_key_bytes = 0U;
  for (std::size_t index = 0U; index < totals.size(); ++index) {
    const auto &total = totals[index];
    if (index != 0U && totals[index - 1U].item_key >= total.item_key) {
      fail("Entity-gameplay item totals are duplicate or out of order");
    }
    validate_item_key(total.item_key, limits.max_item_key_bytes);
    if (total.amount == 0U) {
      fail("Canonical entity-gameplay inventory contains a zero total");
    }
    if (total.item_key.size() >
        limits.max_total_item_key_bytes - total_key_bytes) {
      fail("Entity-gameplay inventory exceeds its aggregate key-byte limit");
    }
    total_key_bytes += total.item_key.size();
  }
}

void validate_inventory_key_union(
    std::vector<std::string_view> keys,
    const EntityGameplayInventoryLimitsV1 &limits) {
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  if (keys.size() > limits.max_item_totals) {
    fail("Entity-gameplay scene and inventory exceed the item-total limit");
  }
  std::uint64_t total_key_bytes = 0U;
  for (const auto key : keys) {
    if (key.size() > limits.max_total_item_key_bytes - total_key_bytes) {
      fail("Entity-gameplay scene and inventory exceed the aggregate key-byte "
           "limit");
    }
    total_key_bytes += key.size();
  }
}

void validate_inventory_union(
    const std::vector<EntityGameplayItemTotalV1> &totals,
    const GameplaySceneV1 &scene,
    const EntityGameplayInventoryLimitsV1 &limits) {
  std::vector<std::string_view> keys;
  keys.reserve(totals.size() + scene.collectibles.size());
  for (const auto &total : totals) {
    keys.push_back(total.item_key);
  }
  for (const auto &collectible : scene.collectibles) {
    validate_item_key(collectible.item_key, limits.max_item_key_bytes);
    keys.push_back(collectible.item_key);
  }
  validate_inventory_key_union(std::move(keys), limits);
}

void add_item_total(std::vector<EntityGameplayItemTotalV1> &totals,
                    const std::string &item_key, const std::uint32_t amount,
                    const EntityGameplayInventoryLimitsV1 &limits) {
  if (amount == 0U) {
    return;
  }
  const auto candidate = std::lower_bound(
      totals.begin(), totals.end(), item_key,
      [](const EntityGameplayItemTotalV1 &total, const std::string &key) {
        return total.item_key < key;
      });
  if (candidate != totals.end() && candidate->item_key == item_key) {
    if (amount >
        std::numeric_limits<std::uint64_t>::max() - candidate->amount) {
      fail("Collectible item total would overflow uint64_t");
    }
    candidate->amount += amount;
    return;
  }
  if (totals.size() >= limits.max_item_totals) {
    fail("Collectible would exceed the item-total limit");
  }
  totals.insert(candidate, EntityGameplayItemTotalV1{item_key, amount});
}

[[nodiscard]] const EntityDefinitionV1 *
find_definition(const EntitySceneV1 &scene, const std::uint32_t authored_id) {
  const auto candidate = std::lower_bound(
      scene.definitions.begin(), scene.definitions.end(), authored_id,
      [](const EntityDefinitionV1 &definition, const std::uint32_t id) {
        return definition.authored_id < id;
      });
  return candidate != scene.definitions.end() &&
                 candidate->authored_id == authored_id
             ? &*candidate
             : nullptr;
}

[[nodiscard]] const EntityTransformComponentV1 *
find_transform(const EntitySceneV1 &scene, const std::uint32_t authored_id) {
  const auto candidate = std::lower_bound(
      scene.transforms.begin(), scene.transforms.end(), authored_id,
      [](const EntityTransformComponentV1 &component, const std::uint32_t id) {
        return component.authored_id < id;
      });
  return candidate != scene.transforms.end() &&
                 candidate->authored_id == authored_id
             ? &*candidate
             : nullptr;
}

[[nodiscard]] EntityArchetypeIdV1
archetype_id_for(const std::vector<std::string> &keys, const std::string &key) {
  const auto candidate = std::lower_bound(keys.begin(), keys.end(), key);
  if (candidate == keys.end() || *candidate != key) {
    fail("An EntitySceneV1 archetype key disappeared during materialization");
  }
  const auto distance = static_cast<std::uint64_t>(candidate - keys.begin());
  if (distance > std::numeric_limits<EntityArchetypeIdV1>::max()) {
    fail("EntitySceneV1 has too many neutral archetypes for WorldV1");
  }
  return static_cast<EntityArchetypeIdV1>(distance);
}

void validate_player_capsule(const EntityGameplayPlayerCapsuleV1 &player) {
  if (!std::isfinite(player.feet_position.x) ||
      !std::isfinite(player.feet_position.y) ||
      !std::isfinite(player.feet_position.z) || !std::isfinite(player.radius) ||
      !std::isfinite(player.height) || !(player.radius > 0.0) ||
      player.height < 2.0 * player.radius) {
    fail("The entity-gameplay player capsule is invalid");
  }
  const auto lower_center = player.feet_position.z + player.radius;
  const auto upper_center =
      player.feet_position.z + player.height - player.radius;
  if (!std::isfinite(lower_center) || !std::isfinite(upper_center)) {
    fail("The entity-gameplay player capsule exceeds the world-coordinate "
         "domain");
  }
}

struct WorldCollectibleSphere {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double radius = 0.0;
};

[[nodiscard]] WorldCollectibleSphere
world_collectible_sphere(const WorldTransformV1 &transform,
                         const GameplayCollectibleV1 &collectible) noexcept {
  // WorldTransformV1 stores an X/Y/Z/W unit quaternion. The local center uses
  // the complete scale-then-rotate-then-translate entity transform. A
  // non-uniformly scaled sphere is conservatively represented by its largest
  // absolute scale so collection never misses the authored ellipsoid.
  const auto center = world_collectible_center_v1(transform, collectible);
  const auto maximum_scale =
      std::max({std::abs(static_cast<double>(transform.scale[0U])),
                std::abs(static_cast<double>(transform.scale[1U])),
                std::abs(static_cast<double>(transform.scale[2U]))});
  return WorldCollectibleSphere{
      center.x,
      center.y,
      center.z,
      static_cast<double>(collectible.collection_radius) * maximum_scale};
}

[[nodiscard]] bool overlaps(const EntityGameplayPlayerCapsuleV1 &player,
                            const WorldTransformV1 &transform,
                            const GameplayCollectibleV1 &collectible) noexcept {
  const auto lower_z = player.feet_position.z + player.radius;
  const auto upper_z = player.feet_position.z + player.height - player.radius;
  const auto sphere = world_collectible_sphere(transform, collectible);
  const auto closest_z = std::clamp(sphere.z, lower_z, upper_z);
  const auto dx = sphere.x - player.feet_position.x;
  const auto dy = sphere.y - player.feet_position.y;
  const auto dz = sphere.z - closest_z;
  const auto radius = player.radius + sphere.radius;
  return dx * dx + dy * dy + dz * dz <= radius * radius;
}

} // namespace

void EntityGameplayRuntimeV1::load_scene(
    const EntitySceneV1 &entity_scene, const GameplaySceneV1 &gameplay_scene,
    const EntityGameplayRuntimeLimitsV1 limits,
    const std::uint64_t first_tick_index) {
  try {
    validate_entity_scene_v1(entity_scene, limits.entity_scene);
  } catch (const EntitySceneError &error) {
    fail("Cannot load entity gameplay: invalid EntitySceneV1: " +
         std::string(error.what()));
  }
  try {
    validate_gameplay_scene_v1(gameplay_scene, limits.gameplay_scene);
  } catch (const GameplaySceneError &error) {
    fail("Cannot load entity gameplay: invalid GameplaySceneV1: " +
         std::string(error.what()));
  }
  if (entity_scene.level_id != gameplay_scene.level_id) {
    fail("EntitySceneV1 and GameplaySceneV1 describe different levels");
  }
  validate_item_totals(item_totals_, limits.inventory);
  validate_inventory_union(item_totals_, gameplay_scene, limits.inventory);

  // Cross-resource validation precedes all mutation, including staged WorldV1
  // creation, so a bad gameplay reference cannot partially replace a level.
  for (const auto &collectible : gameplay_scene.collectibles) {
    if (find_definition(entity_scene, collectible.authored_id) == nullptr) {
      fail(
          "GameplaySceneV1 collectible references a missing entity definition");
    }
    if (find_transform(entity_scene, collectible.authored_id) == nullptr) {
      fail("GameplaySceneV1 collectible entity has no authored transform");
    }
  }

  LoadedStateV1 staged;
  if (state_) {
    staged.session = GameSessionV1(state_->session.snapshot());
  }
  staged.level_id = entity_scene.level_id;
  staged.next_tick_index = first_tick_index;
  staged.inventory_limits = limits.inventory;

  const auto reason = state_ ? LevelRequestReasonV1::transition
                             : LevelRequestReasonV1::new_game;
  try {
    const auto request = staged.session.request_level(entity_scene.level_id,
                                                      std::nullopt, reason);
    staged.world.load_level(staged.session, request);
  } catch (const GameWorldError &error) {
    fail("Cannot create the entity-gameplay WorldV1 level: " +
         std::string(error.what()));
  }

  std::vector<std::string> archetype_keys;
  archetype_keys.reserve(entity_scene.definitions.size());
  for (const auto &definition : entity_scene.definitions) {
    archetype_keys.push_back(definition.archetype_key);
  }
  std::sort(archetype_keys.begin(), archetype_keys.end());
  archetype_keys.erase(
      std::unique(archetype_keys.begin(), archetype_keys.end()),
      archetype_keys.end());

  staged.entities.reserve(entity_scene.definitions.size());
  for (const auto &definition : entity_scene.definitions) {
    const auto *const transform =
        find_transform(entity_scene, definition.authored_id);
    WorldEntityDefinitionV1 world_definition;
    world_definition.archetype_id =
        archetype_id_for(archetype_keys, definition.archetype_key);
    if (transform != nullptr) {
      world_definition.transform = transform->transform;
    }

    EntityIdV1 entity_id;
    try {
      entity_id = staged.world.spawn_entity(world_definition);
    } catch (const GameWorldError &error) {
      fail("Cannot materialize EntitySceneV1 definition " +
           std::to_string(definition.authored_id) + ": " + error.what());
    }
    EntityRecordV1 record;
    record.authored_id = definition.authored_id;
    record.entity_id = entity_id;
    if (transform != nullptr) {
      record.authored_transform = transform->transform;
    }
    record.enabled =
        (definition.flags & kEntityDefinitionInitiallyEnabledV1) != 0U;
    staged.entities.push_back(std::move(record));
  }

  for (const auto &collectible : gameplay_scene.collectibles) {
    const auto record = std::lower_bound(
        staged.entities.begin(), staged.entities.end(), collectible.authored_id,
        [](const EntityRecordV1 &entity, const std::uint32_t id) {
          return entity.authored_id < id;
        });
    if (record == staged.entities.end() ||
        record->authored_id != collectible.authored_id) {
      fail("A validated collectible disappeared during materialization");
    }
    record->collectible = collectible;
  }

  static_assert(std::is_nothrow_move_constructible_v<LoadedStateV1> &&
                std::is_nothrow_move_assignable_v<LoadedStateV1>);
  state_ = std::move(staged);
}

std::vector<EntityGameplayEventV1> EntityGameplayRuntimeV1::fixed_tick(
    const std::uint64_t tick_index,
    const EntityGameplayPlayerCapsuleV1 &player) {
  if (!state_) {
    fail("Entity gameplay cannot tick before a level is loaded");
  }
  validate_player_capsule(player);
  if (tick_index != state_->next_tick_index) {
    fail("Entity gameplay received an out-of-order fixed tick");
  }
  if (tick_index == std::numeric_limits<std::uint64_t>::max()) {
    fail("The entity-gameplay tick sequence is exhausted");
  }

  std::vector<std::size_t> collected_indices;
  std::vector<EntityGameplayEventV1> events;
  auto staged = *state_;
  collected_indices.reserve(staged.entities.size());
  events.reserve(staged.entities.size());
  auto next_item_totals = item_totals_;

  // entities is canonical by authored_id, so both mutation and event order are
  // deterministic and independent of source vector allocation addresses.
  for (std::size_t index = 0U; index < staged.entities.size(); ++index) {
    const auto &entity = staged.entities[index];
    if (!entity.enabled || entity.collected || !entity.collectible) {
      continue;
    }
    if (!entity.authored_transform) {
      fail("A materialized collectible lost its immutable transform");
    }
    if (!overlaps(player, *entity.authored_transform, *entity.collectible)) {
      continue;
    }
    if (staged.world.find_entity(entity.entity_id) == nullptr) {
      fail("An enabled collectible is missing from WorldV1");
    }
    add_item_total(next_item_totals, entity.collectible->item_key,
                   entity.collectible->amount, staged.inventory_limits);
    collected_indices.push_back(index);
    events.push_back(EntityGameplayEventV1{
        EntityGameplayEventKindV1::item_collected, tick_index,
        entity.authored_id, entity.collectible->item_key,
        entity.collectible->amount});
  }

  for (const auto index : collected_indices) {
    auto &entity = staged.entities[index];
    if (!staged.world.destroy_entity(entity.entity_id)) {
      fail("A collectible could not be destroyed exactly once");
    }
    entity.enabled = false;
    entity.collected = true;
  }

  ++staged.next_tick_index;
  // No operation after this point may fail: committing both staged values is
  // the strong-transaction boundary for WorldV1, flags, inventory, and tick.
  static_assert(noexcept(*state_ = std::move(staged)));
  static_assert(noexcept(item_totals_ = std::move(next_item_totals)));
  *state_ = std::move(staged);
  item_totals_ = std::move(next_item_totals);
  return events;
}

void EntityGameplayRuntimeV1::restore_item_totals(
    std::vector<EntityGameplayItemTotalV1> totals) {
  if (!state_) {
    fail("Entity gameplay cannot restore inventory before a level is loaded");
  }
  validate_item_totals(totals, state_->inventory_limits);
  std::vector<std::string_view> keys;
  keys.reserve(totals.size() + state_->entities.size());
  for (const auto &total : totals) {
    keys.push_back(total.item_key);
  }
  for (const auto &entity : state_->entities) {
    if (entity.collectible) {
      keys.push_back(entity.collectible->item_key);
    }
  }
  validate_inventory_key_union(std::move(keys), state_->inventory_limits);
  item_totals_ = std::move(totals);
}

bool EntityGameplayRuntimeV1::loaded() const noexcept {
  return state_.has_value();
}

const WorldV1 &EntityGameplayRuntimeV1::world() const {
  if (!state_) {
    fail("Entity gameplay has no loaded WorldV1");
  }
  return state_->world;
}

EntityGameplaySnapshotV1 EntityGameplayRuntimeV1::snapshot() const {
  if (!state_) {
    fail("Entity gameplay has no loaded snapshot");
  }
  const auto &active = state_->world.active_level();
  if (!active || active->level_id != state_->level_id) {
    fail("Entity gameplay has inconsistent active-level state");
  }

  EntityGameplaySnapshotV1 result;
  result.level_id = state_->level_id;
  result.level_instance_sequence = active->instance_sequence;
  result.next_tick_index = state_->next_tick_index;
  result.item_totals = item_totals_;
  result.entities.reserve(state_->entities.size());
  for (const auto &entity : state_->entities) {
    result.entities.push_back(EntityGameplayEntitySnapshotV1{
        entity.authored_id, entity.entity_id, entity.authored_transform,
        entity.enabled, entity.collected});
  }
  return result;
}

const EntityGameplayRuntimeV1::EntityRecordV1 *
EntityGameplayRuntimeV1::find_record(
    const std::uint32_t authored_id) const noexcept {
  if (!state_) {
    return nullptr;
  }
  const auto candidate = std::lower_bound(
      state_->entities.begin(), state_->entities.end(), authored_id,
      [](const EntityRecordV1 &entity, const std::uint32_t id) {
        return entity.authored_id < id;
      });
  return candidate != state_->entities.end() &&
                 candidate->authored_id == authored_id
             ? &*candidate
             : nullptr;
}

std::optional<EntityIdV1> EntityGameplayRuntimeV1::find_entity_id(
    const std::uint32_t authored_id) const noexcept {
  const auto *const record = find_record(authored_id);
  return record == nullptr ? std::nullopt
                           : std::optional<EntityIdV1>(record->entity_id);
}

std::optional<WorldTransformV1>
EntityGameplayRuntimeV1::find_authored_transform(
    const std::uint32_t authored_id) const noexcept {
  const auto *const record = find_record(authored_id);
  return record == nullptr ? std::nullopt : record->authored_transform;
}

bool EntityGameplayRuntimeV1::enabled(const std::uint32_t authored_id) const {
  const auto *const record = find_record(authored_id);
  if (record == nullptr) {
    fail("Entity gameplay has no such authored entity");
  }
  return record->enabled;
}

bool EntityGameplayRuntimeV1::collected(const std::uint32_t authored_id) const {
  const auto *const record = find_record(authored_id);
  if (record == nullptr) {
    fail("Entity gameplay has no such authored entity");
  }
  return record->collected;
}

std::uint64_t EntityGameplayRuntimeV1::item_total(
    const std::string_view item_key) const noexcept {
  const auto candidate = std::lower_bound(
      item_totals_.begin(), item_totals_.end(), item_key,
      [](const EntityGameplayItemTotalV1 &total, const std::string_view key) {
        return total.item_key < key;
      });
  return candidate != item_totals_.end() && candidate->item_key == item_key
             ? candidate->amount
             : 0U;
}

std::uint64_t EntityGameplayRuntimeV1::next_tick_index() const {
  if (!state_) {
    fail("Entity gameplay has no tick state before load");
  }
  return state_->next_tick_index;
}

} // namespace openrc::game
