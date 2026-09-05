#include "openrc/rac_collectible_scene_compile.hpp"

#include "openrc/actor_render_bake.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RacCollectibleSceneCompileError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(const std::uint64_t left,
                                             const std::uint64_t right,
                                             const char *const description) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left * right;
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

void validate_base_render_scene(const RenderSceneV1 &scene,
                                const RenderSceneLimitsV1 limits) {
  try {
    validate_render_scene_v1(scene, limits);
  } catch (const RenderSceneError &error) {
    fail("The base RenderSceneV1 is not canonical: " +
         std::string(error.what()));
  }
}

void validate_base_entity_scene(const EntitySceneV1 &scene,
                                const EntitySceneLimitsV1 limits) {
  try {
    validate_entity_scene_v1(scene, limits);
  } catch (const EntitySceneError &error) {
    fail("The base EntitySceneV1 is not canonical: " +
         std::string(error.what()));
  }
}

void validate_base_gameplay_scene(const GameplaySceneV1 &scene,
                                  const GameplaySceneLimitsV1 limits) {
  try {
    validate_gameplay_scene_v1(scene, limits);
  } catch (const GameplaySceneError &error) {
    fail("The base GameplaySceneV1 is not canonical: " +
         std::string(error.what()));
  }
}

void validate_single_model_library(const ActorLibraryV1 &library,
                                   const std::string_view model_key,
                                   const ActorLibraryLimitsV1 limits) {
  try {
    validate_actor_library_v1(library, limits);
  } catch (const ActorLibraryError &error) {
    fail("The collectible ActorLibraryV1 is not canonical: " +
         std::string(error.what()));
  }
  if (library.models.size() != 1U) {
    fail("The collectible ActorLibraryV1 must contain exactly one model");
  }
  if (library.models.front().semantic_key != model_key) {
    fail("The collectible profile does not select the library's sole model");
  }
}

void validate_profile(const RacCollectibleCompileProfileV1 &profile,
                      const EntitySceneLimitsV1 entity_limits,
                      const GameplaySceneLimitsV1 gameplay_limits) {
  EntitySceneV1 entity_probe;
  entity_probe.definitions.push_back(
      EntityDefinitionV1{0U, profile.archetype_key});
  entity_probe.transforms.push_back(EntityTransformComponentV1{0U, {}});
  try {
    static_cast<void>(
        canonicalize_entity_scene_v1(std::move(entity_probe), entity_limits));
  } catch (const EntitySceneError &error) {
    fail("The collectible profile is not valid for EntitySceneV1: " +
         std::string(error.what()));
  }

  GameplaySceneV1 gameplay_probe;
  gameplay_probe.collectibles.push_back(
      GameplayCollectibleV1{0U, profile.item_key, profile.local_center,
                            profile.amount, profile.collection_radius, 0U});
  try {
    static_cast<void>(canonicalize_gameplay_scene_v1(std::move(gameplay_probe),
                                                     gameplay_limits));
  } catch (const GameplaySceneError &error) {
    fail("The collectible profile is not valid for GameplaySceneV1: " +
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

void validate_base_relationships(const RenderSceneV1 &render_scene,
                                 const EntitySceneV1 &entity_scene,
                                 const GameplaySceneV1 &gameplay_scene) {
  if (entity_scene.level_id != gameplay_scene.level_id) {
    fail("The base entity and gameplay scenes name different levels");
  }
  for (const auto &binding : entity_scene.render_bindings) {
    if (binding.render_instance_id >= render_scene.instances.size() ||
        render_scene.instances[binding.render_instance_id].id !=
            binding.render_instance_id) {
      fail("A base entity render binding references a missing render instance");
    }
  }
  for (const auto &collectible : gameplay_scene.collectibles) {
    if (!contains_authored_id(entity_scene.definitions,
                              collectible.authored_id)) {
      fail("A base collectible references a missing entity definition");
    }
    if (!contains_authored_id(entity_scene.transforms,
                              collectible.authored_id)) {
      fail("A base collectible references an entity without a transform");
    }
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

[[nodiscard]] std::uint64_t gameplay_key_bytes(const GameplaySceneV1 &scene) {
  std::uint64_t result = 0U;
  for (const auto &collectible : scene.collectibles) {
    result = checked_add(result, collectible.item_key.size(),
                         "The base gameplay key-byte count");
  }
  return result;
}

void preflight_output_counts(const RenderSceneV1 &render_scene,
                             const EntitySceneV1 &entity_scene,
                             const GameplaySceneV1 &gameplay_scene,
                             const std::size_t match_count,
                             const RacCollectibleCompileProfileV1 &profile,
                             const RacCollectibleSceneCompileLimitsV1 &limits) {
  require_append_count(render_scene.instances.size(), match_count,
                       limits.render_scene.max_instances,
                       render_scene.instances.max_size(),
                       "The collectible render-instance count");
  require_append_count(entity_scene.definitions.size(), match_count,
                       limits.entity_scene.max_definitions,
                       entity_scene.definitions.max_size(),
                       "The collectible entity-definition count");
  require_append_count(entity_scene.transforms.size(), match_count,
                       limits.entity_scene.max_transforms,
                       entity_scene.transforms.max_size(),
                       "The collectible entity-transform count");
  require_append_count(entity_scene.render_bindings.size(), match_count,
                       limits.entity_scene.max_render_bindings,
                       entity_scene.render_bindings.max_size(),
                       "The collectible entity render-binding count");
  require_append_count(gameplay_scene.collectibles.size(), match_count,
                       limits.gameplay_scene.max_collectibles,
                       gameplay_scene.collectibles.max_size(),
                       "The gameplay collectible count");

  const auto additional_entity_keys =
      checked_multiply(match_count, profile.archetype_key.size(),
                       "The appended entity key-byte count");
  if (checked_add(entity_key_bytes(entity_scene), additional_entity_keys,
                  "The resulting entity key-byte count") >
      limits.entity_scene.max_total_key_bytes) {
    fail("The resulting EntitySceneV1 exceeds its aggregate key-byte limit");
  }
  const auto additional_gameplay_keys =
      checked_multiply(match_count, profile.item_key.size(),
                       "The appended gameplay key-byte count");
  if (checked_add(gameplay_key_bytes(gameplay_scene), additional_gameplay_keys,
                  "The resulting gameplay key-byte count") >
      limits.gameplay_scene.max_total_key_bytes) {
    fail("The resulting GameplaySceneV1 exceeds its aggregate key-byte limit");
  }
}

struct CompiledPlacementV1 {
  std::uint32_t authored_id = 0U;
  RenderSceneAffine3x4V1 render_transform;
  game::WorldTransformV1 entity_transform;
};

[[nodiscard]] CompiledPlacementV1
compile_placement(const RacGameplayMobyInstanceV1 &source,
                  const std::uint32_t authored_id) {
  if (!std::isfinite(source.scale) || source.scale <= 0.0F) {
    fail("A matching RAC collectible placement has an invalid scale");
  }
  for (const auto value : source.position) {
    if (!std::isfinite(value)) {
      fail("A matching RAC collectible placement has a non-finite position");
    }
  }
  for (const auto value : source.rotation) {
    if (!std::isfinite(value)) {
      fail("A matching RAC collectible placement has a non-finite rotation");
    }
  }

  const auto rx = static_cast<double>(source.rotation[0U]);
  const auto ry = static_cast<double>(source.rotation[1U]);
  const auto rz = static_cast<double>(source.rotation[2U]);
  const auto sx = std::sin(rx);
  const auto cx = std::cos(rx);
  const auto sy = std::sin(ry);
  const auto cy = std::cos(ry);
  const auto sz = std::sin(rz);
  const auto cz = std::cos(rz);
  const auto scale = static_cast<double>(source.scale);

  CompiledPlacementV1 result;
  result.authored_id = authored_id;
  result.render_transform.values = {
      static_cast<float>(scale * (cz * cy)),
      static_cast<float>(scale * (cz * sy * sx - sz * cx)),
      static_cast<float>(scale * (cz * sy * cx + sz * sx)),
      source.position[0U],
      static_cast<float>(scale * (sz * cy)),
      static_cast<float>(scale * (sz * sy * sx + cz * cx)),
      static_cast<float>(scale * (sz * sy * cx - cz * sx)),
      source.position[1U],
      static_cast<float>(scale * -sy),
      static_cast<float>(scale * (cy * sx)),
      static_cast<float>(scale * (cy * cx)),
      source.position[2U],
  };

  const auto half_x = rx * 0.5;
  const auto half_y = ry * 0.5;
  const auto half_z = rz * 0.5;
  const auto qxs = std::sin(half_x);
  const auto qxc = std::cos(half_x);
  const auto qys = std::sin(half_y);
  const auto qyc = std::cos(half_y);
  const auto qzs = std::sin(half_z);
  const auto qzc = std::cos(half_z);
  result.entity_transform.position = source.position;
  result.entity_transform.rotation = {
      static_cast<float>(qzc * qyc * qxs - qzs * qys * qxc),
      static_cast<float>(qzc * qys * qxc + qzs * qyc * qxs),
      static_cast<float>(qzs * qyc * qxc - qzc * qys * qxs),
      static_cast<float>(qzc * qyc * qxc + qzs * qys * qxs),
  };
  result.entity_transform.scale = {source.scale, source.scale, source.scale};

  for (const auto value : result.render_transform.values) {
    if (!std::isfinite(value)) {
      fail("A matching RAC collectible transform overflows RenderSceneV1");
    }
  }
  for (const auto value : result.entity_transform.rotation) {
    if (!std::isfinite(value)) {
      fail("A matching RAC collectible rotation overflows EntitySceneV1");
    }
  }
  return result;
}

template <typename Item>
[[nodiscard]] bool preserved_dense_prefix(const std::vector<Item> &base,
                                          const std::vector<Item> &result) {
  return result.size() >= base.size() &&
         std::equal(base.begin(), base.end(), result.begin());
}

template <typename Item>
[[nodiscard]] bool preserved_by_authored_id(const std::vector<Item> &base,
                                            const std::vector<Item> &result) {
  auto result_cursor = result.begin();
  for (const auto &expected : base) {
    result_cursor =
        std::lower_bound(result_cursor, result.end(), expected.authored_id,
                         [](const Item &item, const std::uint32_t id) {
                           return item.authored_id < id;
                         });
    if (result_cursor == result.end() ||
        result_cursor->authored_id != expected.authored_id ||
        *result_cursor != expected) {
      return false;
    }
    ++result_cursor;
  }
  return true;
}

void require_preserved_prefixes(const RenderSceneV1 &base_render,
                                const RenderSceneV1 &render,
                                const EntitySceneV1 &base_entity,
                                const EntitySceneV1 &entity,
                                const GameplaySceneV1 &base_gameplay,
                                const GameplaySceneV1 &gameplay) {
  if (!preserved_dense_prefix(base_render.textures, render.textures) ||
      !preserved_dense_prefix(base_render.materials, render.materials) ||
      !preserved_dense_prefix(base_render.meshes, render.meshes) ||
      !preserved_dense_prefix(base_render.instances, render.instances) ||
      !preserved_by_authored_id(base_entity.definitions, entity.definitions) ||
      !preserved_by_authored_id(base_entity.transforms, entity.transforms) ||
      !preserved_by_authored_id(base_entity.render_bindings,
                                entity.render_bindings) ||
      !preserved_by_authored_id(base_entity.actor_bindings,
                                entity.actor_bindings) ||
      !preserved_by_authored_id(base_entity.player_bindings,
                                entity.player_bindings) ||
      !preserved_by_authored_id(base_gameplay.collectibles,
                                gameplay.collectibles)) {
    fail("Collectible compilation changed a base-scene record");
  }
}

} // namespace

RacCollectibleSceneCompileResultV1 compile_rac_collectible_scene_v1(
    const RenderSceneV1 &base_render_scene,
    const EntitySceneV1 &base_entity_scene,
    const GameplaySceneV1 &base_gameplay_scene,
    const ActorLibraryV1 &single_model_actor_library,
    const std::span<const RacGameplayMobyInstanceV1> static_mobies,
    const RacCollectibleCompileProfileV1 &profile,
    const RacCollectibleSceneCompileLimitsV1 limits) {
  if (limits.max_source_instances == 0U ||
      static_mobies.size() > limits.max_source_instances ||
      static_mobies.size() > static_cast<std::uint64_t>(
                                 std::numeric_limits<std::uint32_t>::max()) +
                                 1U) {
    fail("The RAC static-moby table exceeds its explicit source limit");
  }

  validate_base_render_scene(base_render_scene, limits.render_scene);
  validate_base_entity_scene(base_entity_scene, limits.entity_scene);
  validate_base_gameplay_scene(base_gameplay_scene, limits.gameplay_scene);
  validate_base_relationships(base_render_scene, base_entity_scene,
                              base_gameplay_scene);
  validate_single_model_library(single_model_actor_library,
                                profile.model_semantic_key,
                                limits.actor_library);
  validate_profile(profile, limits.entity_scene, limits.gameplay_scene);

  std::size_t match_count = 0U;
  for (const auto &source : static_mobies) {
    if (source.class_id == profile.source_class_id) {
      if (match_count == std::numeric_limits<std::size_t>::max()) {
        fail("The matching RAC collectible count overflows size_t");
      }
      ++match_count;
    }
  }
  preflight_output_counts(base_render_scene, base_entity_scene,
                          base_gameplay_scene, match_count, profile, limits);

  std::vector<CompiledPlacementV1> placements;
  std::vector<RenderSceneAffine3x4V1> render_transforms;
  std::vector<std::uint32_t> authored_ids;
  require_append_count(
      0U, match_count, std::numeric_limits<std::uint32_t>::max(),
      placements.max_size(), "The compiled RAC placement count");
  require_append_count(
      0U, match_count, std::numeric_limits<std::uint32_t>::max(),
      render_transforms.max_size(), "The compiled render-transform count");
  require_append_count(
      0U, match_count, std::numeric_limits<std::uint32_t>::max(),
      authored_ids.max_size(), "The compiled authored-ID count");
  placements.reserve(match_count);
  render_transforms.reserve(match_count);
  authored_ids.reserve(match_count);

  for (std::size_t ordinal = 0U; ordinal < static_mobies.size(); ++ordinal) {
    const auto &source = static_mobies[ordinal];
    if (source.class_id != profile.source_class_id) {
      continue;
    }
    if (ordinal > std::numeric_limits<std::uint32_t>::max()) {
      fail("A matching RAC static-moby ordinal exceeds authored-ID width");
    }
    const auto authored_id = static_cast<std::uint32_t>(ordinal);
    if (contains_authored_id(base_entity_scene.definitions, authored_id)) {
      fail("A RAC collectible authored ID collides with the base entity scene");
    }
    auto placement = compile_placement(source, authored_id);
    render_transforms.push_back(placement.render_transform);
    authored_ids.push_back(authored_id);
    placements.push_back(std::move(placement));
  }

  ActorRenderBakeResultV1 baked;
  try {
    baked = bake_actor_bind_pose_to_render_scene_v1(
        base_render_scene, single_model_actor_library,
        profile.model_semantic_key, render_transforms, limits.actor_library,
        limits.actor_pose, limits.render_scene);
  } catch (const ActorRenderBakeError &error) {
    fail("Cannot bake the collectible model into RenderSceneV1: " +
         std::string(error.what()));
  }
  if (baked.instance_ids.size() != placements.size()) {
    fail("Actor bind-pose baking returned a mismatched instance-ID table");
  }

  EntitySceneV1 entity_scene = base_entity_scene;
  entity_scene.definitions.reserve(entity_scene.definitions.size() +
                                   placements.size());
  entity_scene.transforms.reserve(entity_scene.transforms.size() +
                                  placements.size());
  entity_scene.render_bindings.reserve(entity_scene.render_bindings.size() +
                                       placements.size());
  GameplaySceneV1 gameplay_scene = base_gameplay_scene;
  gameplay_scene.collectibles.reserve(gameplay_scene.collectibles.size() +
                                      placements.size());

  for (std::size_t index = 0U; index < placements.size(); ++index) {
    const auto &placement = placements[index];
    entity_scene.definitions.push_back(EntityDefinitionV1{
        placement.authored_id, profile.archetype_key,
        kEntityDefinitionInitiallyEnabledV1, kEntitySceneNoAuthoringGroupIdV1});
    entity_scene.transforms.push_back(EntityTransformComponentV1{
        placement.authored_id, placement.entity_transform});
    entity_scene.render_bindings.push_back(EntityRenderBindingV1{
        placement.authored_id, baked.instance_ids[index]});
    gameplay_scene.collectibles.push_back(GameplayCollectibleV1{
        placement.authored_id, profile.item_key, profile.local_center,
        profile.amount, profile.collection_radius, 0U});
  }

  try {
    entity_scene = canonicalize_entity_scene_v1(std::move(entity_scene),
                                                limits.entity_scene);
  } catch (const EntitySceneError &error) {
    fail("Cannot canonicalize the compiled EntitySceneV1: " +
         std::string(error.what()));
  }
  try {
    gameplay_scene = canonicalize_gameplay_scene_v1(std::move(gameplay_scene),
                                                    limits.gameplay_scene);
  } catch (const GameplaySceneError &error) {
    fail("Cannot canonicalize the compiled GameplaySceneV1: " +
         std::string(error.what()));
  }

  require_preserved_prefixes(base_render_scene, baked.scene, base_entity_scene,
                             entity_scene, base_gameplay_scene, gameplay_scene);
  return RacCollectibleSceneCompileResultV1{
      std::move(baked.scene), std::move(entity_scene),
      std::move(gameplay_scene), std::move(authored_ids),
      std::move(baked.instance_ids)};
}

} // namespace openrc
