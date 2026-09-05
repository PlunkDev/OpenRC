#include "openrc/rac_moby_actor_scene_compile.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RacMobyActorSceneCompileError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left, const std::uint64_t right,
    const char *const description) {
  if (left != 0U &&
      right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left * right;
}

void validate_base_scene(const EntitySceneV1 &scene,
                         const EntitySceneLimitsV1 limits) {
  try {
    validate_entity_scene_v1(scene, limits);
  } catch (const EntitySceneError &error) {
    fail("The base EntitySceneV1 is not canonical: " +
         std::string(error.what()));
  }
}

void validate_single_model_library(const ActorLibraryV1 &library,
                                   const std::string_view model_key,
                                   const ActorLibraryLimitsV1 limits) {
  try {
    validate_actor_library_v1(library, limits);
  } catch (const ActorLibraryError &error) {
    fail("The Moby actor library is not canonical: " +
         std::string(error.what()));
  }
  if (library.models.size() != 1U ||
      library.models.front().semantic_key != model_key) {
    fail("The Moby actor profile does not select the library's sole model");
  }
}

void validate_profile(const RacMobyActorSceneCompileProfileV1 &profile,
                      const EntitySceneLimitsV1 limits) {
  EntitySceneV1 probe;
  probe.definitions.push_back(EntityDefinitionV1{0U, profile.archetype_key});
  probe.transforms.push_back(EntityTransformComponentV1{0U, {}});
  probe.actor_bindings.push_back(EntityActorBindingV1{
      0U, profile.model_semantic_key, profile.model_to_entity});
  try {
    static_cast<void>(canonicalize_entity_scene_v1(std::move(probe), limits));
  } catch (const EntitySceneError &error) {
    fail("The Moby actor profile is invalid for EntitySceneV1: " +
         std::string(error.what()));
  }
}

template <typename Item>
[[nodiscard]] bool contains_authored_id(const std::vector<Item> &items,
                                        const std::uint32_t authored_id) {
  const auto found =
      std::lower_bound(items.begin(), items.end(), authored_id,
                       [](const Item &item, const std::uint32_t id) {
                         return item.authored_id < id;
                       });
  return found != items.end() && found->authored_id == authored_id;
}

void require_append_count(const std::size_t current,
                          const std::size_t additional,
                          const std::uint32_t caller_limit,
                          const std::size_t host_limit,
                          const char *const description) {
  const auto total = checked_add(current, additional, description);
  if (total > caller_limit ||
      total > std::numeric_limits<std::uint32_t>::max() ||
      current > host_limit || additional > host_limit - current) {
    fail(std::string(description) +
         " exceeds its caller, format, or host limit");
  }
}

[[nodiscard]] std::uint64_t entity_key_bytes(const EntitySceneV1 &scene) {
  std::uint64_t result = 0U;
  for (const auto &definition : scene.definitions) {
    result = checked_add(result, definition.archetype_key.size(),
                         "The base entity key-byte count");
  }
  for (const auto &binding : scene.actor_bindings) {
    result = checked_add(result, binding.model_key.size(),
                         "The base entity key-byte count");
  }
  return result;
}

[[nodiscard]] game::WorldTransformV1
compile_transform(const RacGameplayMobyInstanceV1 &source) {
  if (!std::isfinite(source.scale) || source.scale <= 0.0F ||
      std::ranges::any_of(
          source.position,
          [](const float value) { return !std::isfinite(value); }) ||
      std::ranges::any_of(
          source.rotation,
          [](const float value) { return !std::isfinite(value); })) {
    fail("A matching RAC Moby actor placement has an invalid transform");
  }

  const auto half_x = static_cast<double>(source.rotation[0U]) * 0.5;
  const auto half_y = static_cast<double>(source.rotation[1U]) * 0.5;
  const auto half_z = static_cast<double>(source.rotation[2U]) * 0.5;
  const auto qxs = std::sin(half_x);
  const auto qxc = std::cos(half_x);
  const auto qys = std::sin(half_y);
  const auto qyc = std::cos(half_y);
  const auto qzs = std::sin(half_z);
  const auto qzc = std::cos(half_z);

  game::WorldTransformV1 result;
  result.position = source.position;
  result.rotation = {
      static_cast<float>(qzc * qyc * qxs - qzs * qys * qxc),
      static_cast<float>(qzc * qys * qxc + qzs * qyc * qxs),
      static_cast<float>(qzs * qyc * qxc - qzc * qys * qxs),
      static_cast<float>(qzc * qyc * qxc + qzs * qys * qxs),
  };
  result.scale = {source.scale, source.scale, source.scale};
  if (std::ranges::any_of(
          result.rotation,
          [](const float value) { return !std::isfinite(value); })) {
    fail("A matching RAC Moby actor rotation overflows EntitySceneV1");
  }
  return result;
}

template <typename Item>
[[nodiscard]] bool preserved_by_authored_id(const std::vector<Item> &base,
                                            const std::vector<Item> &result) {
  auto cursor = result.begin();
  for (const auto &expected : base) {
    cursor = std::lower_bound(cursor, result.end(), expected.authored_id,
                              [](const Item &item, const std::uint32_t id) {
                                return item.authored_id < id;
                              });
    if (cursor == result.end() || cursor->authored_id != expected.authored_id ||
        *cursor != expected) {
      return false;
    }
    ++cursor;
  }
  return true;
}

} // namespace

RacMobyActorSceneCompileResultV1 compile_rac_moby_actor_scene_v1(
    const EntitySceneV1 &base_entity_scene,
    const ActorLibraryV1 &single_model_actor_library,
    const std::span<const RacGameplayMobyInstanceV1> static_mobies,
    const RacMobyActorSceneCompileProfileV1 &profile,
    const RacMobyActorSceneCompileLimitsV1 limits) {
  if (limits.max_source_instances == 0U ||
      static_mobies.size() > limits.max_source_instances ||
      static_mobies.size() > static_cast<std::uint64_t>(
                                 std::numeric_limits<std::uint32_t>::max()) +
                                 1U) {
    fail("The RAC static-Moby table exceeds its explicit source limit");
  }
  validate_base_scene(base_entity_scene, limits.entity_scene);
  validate_single_model_library(single_model_actor_library,
                                profile.model_semantic_key,
                                limits.actor_library);
  validate_profile(profile, limits.entity_scene);

  std::size_t match_count = 0U;
  for (const auto &source : static_mobies) {
    if (source.class_id == profile.source_class_id) {
      if (match_count == std::numeric_limits<std::size_t>::max()) {
        fail("The matching RAC Moby actor count overflows size_t");
      }
      ++match_count;
    }
  }
  if (match_count == 0U) {
    fail("The RAC static-Moby table has no placement selected by the actor "
         "profile");
  }

  require_append_count(base_entity_scene.definitions.size(), match_count,
                       limits.entity_scene.max_definitions,
                       base_entity_scene.definitions.max_size(),
                       "The Moby actor entity-definition count");
  require_append_count(base_entity_scene.transforms.size(), match_count,
                       limits.entity_scene.max_transforms,
                       base_entity_scene.transforms.max_size(),
                       "The Moby actor entity-transform count");
  require_append_count(base_entity_scene.actor_bindings.size(), match_count,
                       limits.entity_scene.max_actor_bindings,
                       base_entity_scene.actor_bindings.max_size(),
                       "The Moby actor entity-binding count");
  const auto added_key_bytes = checked_multiply(
      match_count,
      checked_add(profile.archetype_key.size(),
                  profile.model_semantic_key.size(),
                  "The Moby actor per-instance key bytes"),
      "The Moby actor appended key bytes");
  if (checked_add(entity_key_bytes(base_entity_scene), added_key_bytes,
                  "The resulting Moby actor entity key bytes") >
      limits.entity_scene.max_total_key_bytes) {
    fail("The resulting EntitySceneV1 exceeds its aggregate key-byte limit");
  }

  EntitySceneV1 entity_scene = base_entity_scene;
  entity_scene.definitions.reserve(entity_scene.definitions.size() +
                                   match_count);
  entity_scene.transforms.reserve(entity_scene.transforms.size() + match_count);
  entity_scene.actor_bindings.reserve(entity_scene.actor_bindings.size() +
                                      match_count);
  std::vector<std::uint32_t> authored_ids;
  authored_ids.reserve(match_count);

  for (std::size_t ordinal = 0U; ordinal < static_mobies.size(); ++ordinal) {
    const auto &source = static_mobies[ordinal];
    if (source.class_id != profile.source_class_id) {
      continue;
    }
    if (ordinal > std::numeric_limits<std::uint32_t>::max()) {
      fail("A matching RAC Moby actor ordinal exceeds authored-ID width");
    }
    const auto authored_id = static_cast<std::uint32_t>(ordinal);
    if (contains_authored_id(base_entity_scene.definitions, authored_id)) {
      fail("A RAC Moby actor authored ID collides with the base entity scene");
    }
    entity_scene.definitions.push_back(EntityDefinitionV1{
        authored_id, profile.archetype_key,
        kEntityDefinitionInitiallyEnabledV1, kEntitySceneNoAuthoringGroupIdV1});
    entity_scene.transforms.push_back(
        EntityTransformComponentV1{authored_id, compile_transform(source)});
    entity_scene.actor_bindings.push_back(EntityActorBindingV1{
        authored_id, profile.model_semantic_key, profile.model_to_entity});
    authored_ids.push_back(authored_id);
  }

  try {
    entity_scene = canonicalize_entity_scene_v1(std::move(entity_scene),
                                                limits.entity_scene);
  } catch (const EntitySceneError &error) {
    fail("Cannot canonicalize the Moby actor EntitySceneV1: " +
         std::string(error.what()));
  }
  if (!preserved_by_authored_id(base_entity_scene.definitions,
                                entity_scene.definitions) ||
      !preserved_by_authored_id(base_entity_scene.transforms,
                                entity_scene.transforms) ||
      !preserved_by_authored_id(base_entity_scene.render_bindings,
                                entity_scene.render_bindings) ||
      !preserved_by_authored_id(base_entity_scene.actor_bindings,
                                entity_scene.actor_bindings) ||
      !preserved_by_authored_id(base_entity_scene.player_bindings,
                                entity_scene.player_bindings)) {
    fail("Moby actor compilation changed a base-scene record");
  }
  return RacMobyActorSceneCompileResultV1{std::move(entity_scene),
                                          std::move(authored_ids)};
}

} // namespace openrc
