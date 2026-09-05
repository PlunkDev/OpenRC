#include "openrc/entity_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw EntitySceneError(message);
}

void validate_limits(const EntitySceneLimitsV1 &limits) {
  if (limits.max_definitions == 0U || limits.max_transforms == 0U ||
      limits.max_render_bindings == 0U ||
      limits.max_actor_bindings == 0U ||
      limits.max_player_bindings == 0U ||
      limits.max_archetype_key_bytes == 0U ||
      limits.max_model_key_bytes == 0U ||
      limits.max_total_key_bytes == 0U) {
    fail("EntitySceneV1 caller limits must all be non-zero and bounded");
  }
}

[[nodiscard]] bool key_character(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('_') ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('/');
}

void validate_key(const std::string_view value,
                  const std::uint32_t maximum_bytes,
                  const char *const description) {
  if (value.empty() || value.size() > maximum_bytes || value.front() == '/' ||
      value.back() == '/') {
    fail(std::string(description) + " is not a canonical semantic key");
  }
  for (const char character : value) {
    if (!key_character(static_cast<unsigned char>(character))) {
      fail(std::string(description) +
           " contains a non-canonical character");
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
      fail(std::string(description) + " contains an unsafe component");
    }
    if (separator == std::string_view::npos) {
      break;
    }
    component_begin = separator + 1U;
  }
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

void validate_canonical_float(const float value,
                              const char *const description) {
  if (!std::isfinite(value)) {
    fail(std::string("EntitySceneV1 has a non-finite ") + description);
  }
  if (value == 0.0F && std::signbit(value)) {
    fail(std::string("EntitySceneV1 has non-canonical signed zero in ") +
         description);
  }
}

[[nodiscard]] float canonical_float(const float value,
                                    const char *const description) {
  if (!std::isfinite(value)) {
    fail(std::string("EntitySceneV1 has a non-finite ") + description);
  }
  return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] bool canonical_quaternion_sign(
    const std::array<float, 4U> &rotation) noexcept {
  // W is the preferred hemisphere. Exact 180-degree rotations use X, then Y,
  // then Z as deterministic tie breakers.
  for (const auto index : {3U, 0U, 1U, 2U}) {
    if (rotation[index] != 0.0F) {
      return rotation[index] > 0.0F;
    }
  }
  return true;
}

void validate_transform(const game::WorldTransformV1 &transform) {
  for (const float value : transform.position) {
    validate_canonical_float(value, "world-transform position");
  }
  for (const float value : transform.rotation) {
    validate_canonical_float(value, "world-transform rotation");
  }
  for (const float value : transform.scale) {
    validate_canonical_float(value, "world-transform scale");
    if (value == 0.0F) {
      fail("EntitySceneV1 world transform has a zero scale component");
    }
  }

  double length_squared = 0.0;
  for (const float value : transform.rotation) {
    const auto widened = static_cast<double>(value);
    length_squared += widened * widened;
  }
  if (!(length_squared > 0.0)) {
    fail("EntitySceneV1 world transform has a zero rotation quaternion");
  }
  const auto length = std::sqrt(length_squared);
  if (!std::isfinite(length) ||
      std::abs(length - 1.0) > kEntitySceneQuaternionUnitToleranceV1) {
    fail("EntitySceneV1 world-transform quaternion is not unit length");
  }
  if (!canonical_quaternion_sign(transform.rotation)) {
    fail("EntitySceneV1 world-transform quaternion sign is not canonical");
  }
}

void canonicalize_transform(game::WorldTransformV1 &transform) {
  for (float &value : transform.position) {
    value = canonical_float(value, "world-transform position");
  }
  for (float &value : transform.rotation) {
    value = canonical_float(value, "world-transform rotation");
  }
  for (float &value : transform.scale) {
    value = canonical_float(value, "world-transform scale");
  }
  if (!canonical_quaternion_sign(transform.rotation)) {
    for (float &value : transform.rotation) {
      value = canonical_float(-value, "world-transform rotation");
    }
  }
}

void validate_actor_transform(const ActorAffineTransformV1 &transform) {
  for (const float value : transform.values) {
    validate_canonical_float(value, "actor model-to-entity transform");
  }
}

void canonicalize_actor_transform(ActorAffineTransformV1 &transform) {
  for (float &value : transform.values) {
    value = canonical_float(value, "actor model-to-entity transform");
  }
}

template <typename Item>
void require_authored_order(const std::vector<Item> &items,
                            const char *const description) {
  for (std::size_t index = 1U; index < items.size(); ++index) {
    if (items[index - 1U].authored_id >= items[index].authored_id) {
      fail(std::string("EntitySceneV1 ") + description +
           " authored IDs are duplicate or out of order");
    }
  }
}

template <typename Item> void sort_by_authored_id(std::vector<Item> &items) {
  std::sort(items.begin(), items.end(),
            [](const Item &left, const Item &right) {
              return left.authored_id < right.authored_id;
            });
}

template <typename Item>
[[nodiscard]] bool contains_authored_id(const std::vector<Item> &items,
                                        const std::uint32_t authored_id) {
  const auto found = std::lower_bound(
      items.begin(), items.end(), authored_id,
      [](const Item &item, const std::uint32_t id) {
        return item.authored_id < id;
      });
  return found != items.end() && found->authored_id == authored_id;
}

void require_definition(const EntitySceneV1 &scene,
                        const std::uint32_t authored_id,
                        const char *const description) {
  if (!contains_authored_id(scene.definitions, authored_id)) {
    fail(std::string("EntitySceneV1 ") + description +
         " references a missing definition");
  }
}

} // namespace

void validate_entity_scene_v1(const EntitySceneV1 &scene,
                              const EntitySceneLimitsV1 limits) {
  validate_limits(limits);
  if (scene.schema_version != kEntitySceneSchemaVersionV1) {
    fail("EntitySceneV1 has an unknown schema version");
  }
  if (scene.definitions.size() > limits.max_definitions ||
      scene.transforms.size() > limits.max_transforms ||
      scene.render_bindings.size() > limits.max_render_bindings ||
      scene.actor_bindings.size() > limits.max_actor_bindings ||
      scene.player_bindings.size() > limits.max_player_bindings) {
    fail("EntitySceneV1 exceeds a caller table limit");
  }

  require_authored_order(scene.definitions, "definition");
  require_authored_order(scene.transforms, "transform");
  require_authored_order(scene.render_bindings, "render binding");
  require_authored_order(scene.actor_bindings, "actor binding");
  require_authored_order(scene.player_bindings, "player binding");

  std::uint64_t total_key_bytes = 0U;
  for (const auto &definition : scene.definitions) {
    if ((definition.flags & ~kEntityDefinitionKnownFlagsV1) != 0U) {
      fail("EntitySceneV1 definition has unknown flags");
    }
    validate_key(definition.archetype_key, limits.max_archetype_key_bytes,
                 "EntitySceneV1 archetype key");
    total_key_bytes = checked_add(total_key_bytes,
                                  definition.archetype_key.size(),
                                  "EntitySceneV1 key bytes");
  }

  for (const auto &component : scene.transforms) {
    require_definition(scene, component.authored_id, "transform component");
    validate_transform(component.transform);
  }
  for (const auto &binding : scene.render_bindings) {
    require_definition(scene, binding.authored_id, "render binding");
    if (binding.render_instance_id == UINT32_MAX) {
      fail("EntitySceneV1 render binding has an invalid instance sentinel");
    }
  }
  for (const auto &binding : scene.actor_bindings) {
    require_definition(scene, binding.authored_id, "actor binding");
    if (contains_authored_id(scene.render_bindings, binding.authored_id)) {
      fail("EntitySceneV1 actor and render bindings are mutually exclusive");
    }
    validate_key(binding.model_key, limits.max_model_key_bytes,
                 "EntitySceneV1 actor model key");
    total_key_bytes = checked_add(total_key_bytes, binding.model_key.size(),
                                  "EntitySceneV1 key bytes");
    validate_actor_transform(binding.model_to_entity);
  }

  std::vector<std::uint32_t> player_slots;
  player_slots.reserve(scene.player_bindings.size());
  for (const auto &binding : scene.player_bindings) {
    require_definition(scene, binding.authored_id, "player binding");
    player_slots.push_back(binding.local_player_slot);
  }
  std::sort(player_slots.begin(), player_slots.end());
  if (std::adjacent_find(player_slots.begin(), player_slots.end()) !=
      player_slots.end()) {
    fail("EntitySceneV1 local player slots are not unique");
  }

  if (total_key_bytes > limits.max_total_key_bytes) {
    fail("EntitySceneV1 exceeds the aggregate key-byte limit");
  }

  for (const auto &definition : scene.definitions) {
    const auto is_player =
        contains_authored_id(scene.player_bindings, definition.authored_id);
    const auto has_transform =
        contains_authored_id(scene.transforms, definition.authored_id);
    if (is_player == has_transform) {
      fail(is_player
               ? "EntitySceneV1 player definition has a transform component"
               : "EntitySceneV1 non-player definition lacks a transform component");
    }
  }
}

EntitySceneV1 canonicalize_entity_scene_v1(
    EntitySceneV1 scene, const EntitySceneLimitsV1 limits) {
  sort_by_authored_id(scene.definitions);
  sort_by_authored_id(scene.transforms);
  sort_by_authored_id(scene.render_bindings);
  sort_by_authored_id(scene.actor_bindings);
  sort_by_authored_id(scene.player_bindings);
  for (auto &component : scene.transforms) {
    canonicalize_transform(component.transform);
  }
  for (auto &binding : scene.actor_bindings) {
    canonicalize_actor_transform(binding.model_to_entity);
  }
  validate_entity_scene_v1(scene, limits);
  return scene;
}

} // namespace openrc
