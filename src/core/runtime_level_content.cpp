#include "openrc/runtime_level_content.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

  for (const auto &binding : entity_scene.render_bindings) {
    if (binding.render_instance_id >= render_scene.instances.size() ||
        render_scene.instances[binding.render_instance_id].id !=
            binding.render_instance_id) {
      fail("Entity scene render binding references a missing render instance");
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
  return result;
}

} // namespace openrc::game
