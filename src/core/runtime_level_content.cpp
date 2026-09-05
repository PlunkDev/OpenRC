#include "openrc/runtime_level_content.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimeLevelContentError(message);
}

[[nodiscard]] const EntityActorBindingV1 *
find_actor_binding(const EntitySceneV1 &scene,
                   const std::uint32_t authored_id) {
  const auto found = std::lower_bound(
      scene.actor_bindings.begin(), scene.actor_bindings.end(), authored_id,
      [](const EntityActorBindingV1 &binding, const std::uint32_t id) {
        return binding.authored_id < id;
      });
  return found != scene.actor_bindings.end() &&
                 found->authored_id == authored_id
             ? &*found
             : nullptr;
}

[[nodiscard]] const EntityDefinitionV1 *
find_definition(const EntitySceneV1 &scene, const std::uint32_t authored_id) {
  const auto found = std::lower_bound(
      scene.definitions.begin(), scene.definitions.end(), authored_id,
      [](const EntityDefinitionV1 &definition, const std::uint32_t id) {
        return definition.authored_id < id;
      });
  return found != scene.definitions.end() && found->authored_id == authored_id
             ? &*found
             : nullptr;
}

[[nodiscard]] const EntityTransformComponentV1 *
find_entity_transform(const EntitySceneV1 &scene,
                      const std::uint32_t authored_id) {
  const auto found = std::lower_bound(
      scene.transforms.begin(), scene.transforms.end(), authored_id,
      [](const EntityTransformComponentV1 &component,
         const std::uint32_t id) { return component.authored_id < id; });
  return found != scene.transforms.end() && found->authored_id == authored_id
             ? &*found
             : nullptr;
}

[[nodiscard]] bool approximately_equal(const float actual,
                                       const double expected) noexcept {
  constexpr double kToleranceFactor =
      64.0 * static_cast<double>(std::numeric_limits<float>::epsilon());
  const auto scale = std::max(
      {1.0, std::abs(static_cast<double>(actual)), std::abs(expected)});
  return std::abs(static_cast<double>(actual) - expected) <=
         kToleranceFactor * scale;
}

[[nodiscard]] bool render_transform_matches_entity(
    const RenderSceneAffine3x4V1 &render,
    const game::WorldTransformV1 &entity) noexcept {
  const auto x = static_cast<double>(entity.rotation[0U]);
  const auto y = static_cast<double>(entity.rotation[1U]);
  const auto z = static_cast<double>(entity.rotation[2U]);
  const auto w = static_cast<double>(entity.rotation[3U]);
  const auto sx = static_cast<double>(entity.scale[0U]);
  const auto sy = static_cast<double>(entity.scale[1U]);
  const auto sz = static_cast<double>(entity.scale[2U]);
  const std::array<double, 12U> expected{
      (1.0 - 2.0 * (y * y + z * z)) * sx,
      2.0 * (x * y - z * w) * sy,
      2.0 * (x * z + y * w) * sz,
      static_cast<double>(entity.position[0U]),
      2.0 * (x * y + z * w) * sx,
      (1.0 - 2.0 * (x * x + z * z)) * sy,
      2.0 * (y * z - x * w) * sz,
      static_cast<double>(entity.position[1U]),
      2.0 * (x * z - y * w) * sx,
      2.0 * (y * z + x * w) * sy,
      (1.0 - 2.0 * (x * x + y * y)) * sz,
      static_cast<double>(entity.position[2U]),
  };
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    if (!approximately_equal(render.values[index], expected[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] const ActorModelV1 *
find_actor_model(const ActorLibraryV1 &library,
                 const std::string_view semantic_key) {
  const auto found = std::find_if(
      library.models.begin(), library.models.end(),
      [semantic_key](const ActorModelV1 &model) {
        return model.semantic_key == semantic_key;
      });
  return found == library.models.end() ? nullptr : &*found;
}

[[nodiscard]] const ActorRigAssetV1 *
find_actor_rig(const ActorLibraryV1 &library,
               const std::string_view semantic_key) {
  const auto found =
      std::find_if(library.rigs.begin(), library.rigs.end(),
                   [semantic_key](const ActorRigAssetV1 &rig) {
                     return rig.semantic_key == semantic_key;
                   });
  return found == library.rigs.end() ? nullptr : &*found;
}

[[nodiscard]] double actor_linear_determinant(
    const ActorAffineTransformV1 &transform) noexcept {
  const auto &value = transform.values;
  return static_cast<double>(value[0U]) *
             (static_cast<double>(value[5U]) * value[10U] -
              static_cast<double>(value[6U]) * value[9U]) -
         static_cast<double>(value[1U]) *
             (static_cast<double>(value[4U]) * value[10U] -
              static_cast<double>(value[6U]) * value[8U]) +
         static_cast<double>(value[2U]) *
             (static_cast<double>(value[4U]) * value[9U] -
              static_cast<double>(value[5U]) * value[8U]);
}

void add_player_actor_count(std::uint64_t &total, const std::size_t count,
                            const std::uint64_t maximum,
                            const char *const description) {
  if (total > maximum || count > maximum - total) {
    fail(std::string("Mounted player actor exceeds its ") + description +
         " presentation limit");
  }
  total += static_cast<std::uint64_t>(count);
}

void preflight_player_actor(const ActorLibraryV1 &library,
                            const EntityActorBindingV1 &binding) {
  const auto *const model = find_actor_model(library, binding.model_key);
  if (model == nullptr) {
    fail("Local player actor binding references a missing actor model");
  }
  const auto *const rig = find_actor_rig(library, model->rig_key);
  if (rig == nullptr) {
    fail("Local player actor model references a missing actor rig");
  }
  if (rig->rig.joints.size() > kRuntimePlayerActorMaximumJointsV1) {
    fail("Mounted player actor exceeds its joint presentation limit");
  }

  std::uint64_t vertices = 0U;
  std::uint64_t indices = 0U;
  std::uint64_t draws = 0U;
  for (const auto &mesh : model->meshes) {
    add_player_actor_count(vertices, mesh.vertices.size(),
                           kRuntimePlayerActorMaximumVerticesV1, "vertex");
    add_player_actor_count(indices, mesh.triangle_indices.size(),
                           kRuntimePlayerActorMaximumTriangleIndicesV1,
                           "triangle-index");
    add_player_actor_count(draws, mesh.draw_ranges.size(),
                           kRuntimePlayerActorMaximumDrawRangesV1,
                           "draw-range");
  }

  try {
    const auto palette = build_actor_bind_pose_palette_v1(
        rig->rig, kRuntimePlayerActorPoseLimitsV1);
    for (const auto &mesh : model->meshes) {
      static_cast<void>(pose_actor_mesh_vertices_v1(
          mesh, palette, binding.model_to_entity,
          kRuntimePlayerActorPoseLimitsV1));
    }
  } catch (const ActorPoseError &error) {
    fail("Mounted local player actor failed presentation preflight: " +
         std::string(error.what()));
  }
}

void validate_actor_entity_contract(const ActorLibraryV1 &actor_library,
                                    const EntitySceneV1 &entity_scene,
                                    const RenderSceneV1 &render_scene,
                                    const std::uint32_t package_level_id) {
  if (entity_scene.level_id != package_level_id) {
    fail("Mounted entity scene belongs to a different package level");
  }

  std::unordered_set<std::string_view> model_keys;
  model_keys.reserve(actor_library.models.size());
  for (const auto &model : actor_library.models) {
    if (!model_keys.emplace(model.semantic_key).second) {
      fail("Mounted actor library repeats a semantic model key");
    }
  }
  for (const auto &binding : entity_scene.actor_bindings) {
    if (!model_keys.contains(binding.model_key)) {
      fail("Entity scene actor binding references a missing actor model");
    }
  }

  std::unordered_set<std::uint32_t> bound_render_instances;
  bound_render_instances.reserve(entity_scene.render_bindings.size());
  for (const auto &binding : entity_scene.render_bindings) {
    if (binding.render_instance_id >= render_scene.instances.size() ||
        render_scene.instances[binding.render_instance_id].id !=
            binding.render_instance_id) {
      fail("Entity scene render binding references a missing render instance");
    }
    if (!bound_render_instances.emplace(binding.render_instance_id).second) {
      fail("Entity scene assigns one render instance to multiple entities");
    }
    const auto *const transform =
        find_entity_transform(entity_scene, binding.authored_id);
    if (transform == nullptr ||
        !render_transform_matches_entity(
            render_scene.instances[binding.render_instance_id].local_to_world,
            transform->transform)) {
      fail("Entity scene render binding disagrees with its authored transform");
    }
  }

  std::size_t slot_zero_count = 0U;
  const EntityActorBindingV1 *slot_zero_actor = nullptr;
  for (const auto &player : entity_scene.player_bindings) {
    const auto *definition =
        find_definition(entity_scene, player.authored_id);
    if (definition == nullptr) {
      fail("Entity scene player binding does not resolve to a definition");
    }
    if ((definition->flags & kEntityDefinitionInitiallyEnabledV1) == 0U) {
      fail("Entity scene player definition is disabled");
    }
    const auto *actor = find_actor_binding(entity_scene, player.authored_id);
    if (actor == nullptr) {
      fail("Entity scene player binding does not resolve to an actor binding");
    }
    const auto determinant = actor_linear_determinant(actor->model_to_entity);
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <
            kRuntimePlayerActorMinimumAbsoluteLinearDeterminantV1) {
      fail("Entity scene player actor has a singular or ill-conditioned "
           "model-to-entity transform");
    }
    if (player.local_player_slot == 0U) {
      ++slot_zero_count;
      slot_zero_actor = actor;
    }
  }
  if (slot_zero_count != 1U || slot_zero_actor == nullptr) {
    fail("Entity scene must bind local player slot 0 exactly once");
  }
  preflight_player_actor(actor_library, *slot_zero_actor);
}

void validate_gameplay_entity_contract(const GameplaySceneV1 &gameplay_scene,
                                       const EntitySceneV1 &entity_scene,
                                       const std::uint32_t package_level_id) {
  if (gameplay_scene.level_id != package_level_id ||
      entity_scene.level_id != package_level_id) {
    fail("Mounted gameplay/entity scenes have inconsistent level identity");
  }
  for (const auto &collectible : gameplay_scene.collectibles) {
    if (find_definition(entity_scene, collectible.authored_id) == nullptr) {
      fail("Gameplay scene collectible references a missing entity definition");
    }
    if (find_entity_transform(entity_scene, collectible.authored_id) == nullptr) {
      fail("Gameplay scene collectible references an entity without a transform");
    }
  }
}

void validate_destructible_entity_contract(
    const DestructibleSceneV1 &destructible_scene,
    const EntitySceneV1 &entity_scene,
    const GameplaySceneV1 *const gameplay_scene,
    const std::uint32_t package_level_id) {
  if (destructible_scene.level_id != package_level_id ||
      entity_scene.level_id != package_level_id ||
      (gameplay_scene != nullptr &&
       gameplay_scene->level_id != package_level_id)) {
    fail("Mounted destructible/entity/gameplay scenes have inconsistent level "
         "identity");
  }

  for (const auto &destructible : destructible_scene.destructibles) {
    if (find_definition(entity_scene, destructible.authored_id) == nullptr) {
      fail("Destructible scene references a missing entity definition");
    }
    if (find_entity_transform(entity_scene, destructible.authored_id) ==
        nullptr) {
      fail("Destructible scene references an entity without a transform");
    }
    if (gameplay_scene == nullptr) {
      continue;
    }
    const auto collectible = std::lower_bound(
        gameplay_scene->collectibles.begin(),
        gameplay_scene->collectibles.end(), destructible.authored_id,
        [](const GameplayCollectibleV1 &candidate, const std::uint32_t id) {
          return candidate.authored_id < id;
        });
    if (collectible != gameplay_scene->collectibles.end() &&
        collectible->authored_id == destructible.authored_id) {
      fail("One entity cannot be both a gameplay collectible and a "
           "destructible");
    }
  }
}

} // namespace

RuntimeLevelContentV1
load_runtime_level_content_v1(const ResolvedLevelPackageV1 &package,
                              const RuntimeLevelContentLimitsV1 limits) {
  if (limits.foundation.required_content_api_version == 0U) {
    fail("Runtime level content API policy must be explicit");
  }

  RuntimeLevelContentV1 result;
  try {
    result.foundation =
        load_runtime_level_foundation_v1(package, limits.foundation);
  } catch (const RuntimeLevelFoundationError &error) {
    fail("Cannot mount the runtime level foundation: " +
         std::string(error.what()));
  }

  try {
    result.render_scene = load_runtime_render_scene_v1(
        package, limits.foundation.required_content_api_version,
        limits.render_scene);
  } catch (const RuntimeRenderSceneError &error) {
    fail("Cannot mount the runtime render scene: " + std::string(error.what()));
  }

  try {
    result.actor_library = load_optional_runtime_actor_library_v1(
        package, limits.foundation.required_content_api_version,
        limits.actor_library);
  } catch (const RuntimeActorLibraryError &error) {
    fail("Cannot mount the runtime actor library: " +
         std::string(error.what()));
  }

  try {
    result.entity_scene = load_optional_runtime_entity_scene_v1(
        package, limits.foundation.required_content_api_version,
        limits.entity_scene);
  } catch (const RuntimeEntitySceneError &error) {
    fail("Cannot mount the runtime entity scene: " +
         std::string(error.what()));
  }

  try {
    result.gameplay_scene = load_optional_runtime_gameplay_scene_resource_v1(
        package, limits.foundation.required_content_api_version,
        limits.gameplay_scene);
  } catch (const RuntimeGameplaySceneResourceError &error) {
    fail("Cannot mount the runtime gameplay scene: " +
         std::string(error.what()));
  }

  try {
    result.destructible_scene =
        load_optional_runtime_destructible_scene_resource_v1(
            package, limits.foundation.required_content_api_version,
            limits.destructible_scene);
  } catch (const RuntimeDestructibleSceneResourceError &error) {
    fail("Cannot mount the runtime destructible scene: " +
         std::string(error.what()));
  }

  if (result.foundation.level_id != package.level_id ||
      result.foundation.content_api_version != package.content_api_version) {
    fail("Mounted runtime level content has inconsistent package identity");
  }
  if (result.actor_library.has_value() != result.entity_scene.has_value()) {
    fail("Runtime actor/entity feature pair is incomplete");
  }
  if (result.actor_library && result.entity_scene) {
    validate_actor_entity_contract(*result.actor_library, *result.entity_scene,
                                   result.render_scene, package.level_id);
  }
  if (result.gameplay_scene && !result.entity_scene) {
    fail("Runtime gameplay scene is present without its entity scene");
  }
  if (result.gameplay_scene) {
    validate_gameplay_entity_contract(*result.gameplay_scene,
                                      *result.entity_scene,
                                      package.level_id);
  }
  if (result.destructible_scene && !result.entity_scene) {
    fail("Runtime destructible scene is present without its entity scene");
  }
  if (result.destructible_scene) {
    validate_destructible_entity_contract(
        *result.destructible_scene, *result.entity_scene,
        result.gameplay_scene ? &*result.gameplay_scene : nullptr,
        package.level_id);
  }
  return result;
}

} // namespace openrc::game
