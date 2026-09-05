#include "openrc/runtime_level_content.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>

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

[[nodiscard]] bool is_player_entity(const EntitySceneV1 &scene,
                                    const std::uint32_t authored_id) {
  const auto found = std::lower_bound(
      scene.player_bindings.begin(), scene.player_bindings.end(), authored_id,
      [](const PlayerEntityBindingV1 &binding, const std::uint32_t id) {
        return binding.authored_id < id;
      });
  return found != scene.player_bindings.end() &&
         found->authored_id == authored_id;
}

[[nodiscard]] bool is_destructible_entity(
    const DestructibleSceneV1 *const scene,
    const std::uint32_t authored_id) {
  if (scene == nullptr) {
    return false;
  }
  const auto found = std::lower_bound(
      scene->destructibles.begin(), scene->destructibles.end(), authored_id,
      [](const DestructibleDefinitionV1 &definition, const std::uint32_t id) {
        return definition.authored_id < id;
      });
  return found != scene->destructibles.end() &&
         found->authored_id == authored_id;
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

void validate_actor_animation_contract(
    const ActorAnimationBankV1 &animation_bank,
    const ActorLibraryV1 &actor_library) {
  for (const auto &clip : animation_bank.clips) {
    const auto *const rig = find_actor_rig(actor_library, clip.rig_key);
    if (rig == nullptr) {
      fail("Mounted actor animation references a missing actor rig");
    }
    if (is_zero_prepared_digest_v1(clip.rig_content_sha256) ||
        clip.rig_content_sha256 != actor_rig_content_sha256_v1(rig->rig)) {
      fail("Mounted actor animation references a stale actor rig digest");
    }
    for (const auto &frame : clip.frames) {
      if (frame.joint_poses.size() != rig->rig.joints.size()) {
        fail("Mounted actor animation and actor rig joint counts disagree");
      }
    }
  }
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

void validate_actor_behavior_entity_contract(
    const ActorBehaviorSceneV1 &behavior_scene,
    const EntitySceneV1 &entity_scene,
    const ActorLibraryV1 &actor_library,
    const ActorAnimationBankV1 &animation_bank,
    const DestructibleSceneV1 *const destructible_scene,
    const std::uint32_t package_level_id) {
  if (behavior_scene.level_id != package_level_id ||
      entity_scene.level_id != package_level_id) {
    fail("Mounted actor-behavior/entity scenes have inconsistent level "
         "identity");
  }

  struct RigContract {
    const ActorRigAssetV1 *rig = nullptr;
    std::optional<PreparedContentDigestV1> digest;
  };
  struct ClipContract {
    const ActorAnimationClipV1 *clip = nullptr;
    std::optional<PreparedContentDigestV1> digest;
  };
  std::unordered_map<std::string_view, RigContract> rigs;
  std::unordered_map<std::string_view, const ActorModelV1 *> models;
  std::unordered_map<std::string_view, ClipContract> clips;
  rigs.reserve(actor_library.rigs.size());
  models.reserve(actor_library.models.size());
  clips.reserve(animation_bank.clips.size());
  for (const auto &rig : actor_library.rigs) {
    rigs.emplace(rig.semantic_key, RigContract{&rig, std::nullopt});
  }
  for (const auto &model : actor_library.models) {
    models.emplace(model.semantic_key, &model);
  }
  for (const auto &clip : animation_bank.clips) {
    clips.emplace(clip.semantic_key, ClipContract{&clip, std::nullopt});
  }

  for (const auto &program : behavior_scene.programs) {
    const auto rig_entry = rigs.find(program.required_rig_key);
    if (rig_entry == rigs.end()) {
      fail("Actor-behavior program references a missing actor rig");
    }
    auto &rig_contract = rig_entry->second;
    if (!rig_contract.digest) {
      rig_contract.digest = actor_rig_content_sha256_v1(rig_contract.rig->rig);
    }
    if (program.required_rig_sha256 != *rig_contract.digest) {
      fail("Actor-behavior program references a stale actor rig digest");
    }
    const auto model_entry = models.find(program.required_model_key);
    if (model_entry == models.end()) {
      fail("Actor-behavior program references a missing actor model");
    }
    const auto *const required_model = model_entry->second;
    if (required_model->content_sha256 != program.required_model_sha256 ||
        required_model->rig_key != program.required_rig_key) {
      fail("Actor-behavior program references a stale or incompatible actor "
           "model");
    }
    for (const auto &animation : program.animation_imports) {
      const auto clip_entry = clips.find(animation.clip_key);
      if (clip_entry == clips.end()) {
        fail("Actor-behavior program references a missing animation clip");
      }
      auto &clip_contract = clip_entry->second;
      const auto *const clip = clip_contract.clip;
      if (clip->rig_key != program.required_rig_key ||
          clip->rig_content_sha256 != program.required_rig_sha256) {
        fail("Actor-behavior animation import disagrees with its required "
             "rig");
      }
      if (clip->frames.size() != animation.required_frame_count) {
        fail("Actor-behavior animation import disagrees with its required "
             "frame count");
      }
      // Multiple programs and imports can bind one clip. Hash its pose data
      // only on the first use, without depending on unordered-map order.
      if (!clip_contract.digest) {
        clip_contract.digest = actor_animation_clip_content_sha256_v1(*clip);
      }
      if (*clip_contract.digest != animation.required_clip_sha256) {
        fail("Actor-behavior animation import references stale clip "
             "content");
      }
    }
  }

  for (const auto &instance : behavior_scene.instances) {
    const auto *const definition =
        find_definition(entity_scene, instance.authored_id);
    const auto *const actor =
        find_actor_binding(entity_scene, instance.authored_id);
    if (is_player_entity(entity_scene, instance.authored_id)) {
      fail("Actor-behavior instance cannot bind a local player entity");
    }
    const auto *const transform =
        find_entity_transform(entity_scene, instance.authored_id);
    if (definition == nullptr || transform == nullptr || actor == nullptr) {
      fail("Actor-behavior instance does not resolve to a complete actor "
           "entity");
    }
    if (is_destructible_entity(destructible_scene, instance.authored_id)) {
      fail("Actor-behavior instance cannot also use the automatic "
           "destructible runtime");
    }

    const auto &program = behavior_scene.programs[instance.program_id];
    const auto model_entry = models.find(actor->model_key);
    if (model_entry == models.end() ||
        actor->model_key != program.required_model_key ||
        model_entry->second->content_sha256 != program.required_model_sha256 ||
        model_entry->second->rig_key != program.required_rig_key) {
      fail("Actor-behavior instance model disagrees with its exact program "
           "model");
    }

    for (const auto &initial_animation : instance.initial_animations) {
      const auto &animation_import =
          program.animation_imports[initial_animation.animation_import_id];
      if (initial_animation.first_frame_index >=
          animation_import.required_frame_count) {
        fail("Actor-behavior instance has an invalid initial animation "
             "frame");
      }
    }

    std::size_t value_index = 0U;
    for (const auto &field : program.fields) {
      for (std::uint32_t element = 0U; element < field.element_count;
           ++element) {
        static_cast<void>(element);
        const auto &value = instance.initial_values[value_index++];
        const auto *const reference =
            std::get_if<ActorBehaviorEntityReferenceV1>(&value);
        if (reference == nullptr || !reference->authored_id) {
          continue;
        }
        const auto referenced_id = *reference->authored_id;
        if (find_definition(entity_scene, referenced_id) == nullptr) {
          fail("Actor-behavior instance contains a dangling entity "
               "reference");
        }
        if ((field.flags &
             kActorBehaviorEntityReferenceRequiresTransformV1) != 0U &&
            find_entity_transform(entity_scene, referenced_id) == nullptr) {
          fail("Actor-behavior entity reference requires a world transform");
        }
        if ((field.flags & kActorBehaviorEntityReferenceRequiresActorV1) !=
                0U &&
            find_actor_binding(entity_scene, referenced_id) == nullptr) {
          fail("Actor-behavior entity reference requires an actor binding");
        }
        if ((field.flags & kActorBehaviorEntityReferenceRequiresBehaviorV1) !=
            0U) {
          const auto referenced_behavior = std::lower_bound(
              behavior_scene.instances.begin(),
              behavior_scene.instances.end(), referenced_id,
              [](const ActorBehaviorInstanceV1 &candidate,
                 const std::uint32_t id) {
                return candidate.authored_id < id;
              });
          if (referenced_behavior == behavior_scene.instances.end() ||
              referenced_behavior->authored_id != referenced_id) {
            fail("Actor-behavior entity reference requires another behavior "
                 "instance");
          }
        }
      }
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
    result.actor_animation_bank =
        load_optional_runtime_actor_animation_bank_v1(
            package, limits.foundation.required_content_api_version,
            limits.actor_animation);
  } catch (const RuntimeActorAnimationError &error) {
    fail("Cannot mount the runtime actor-animation bank: " +
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

  try {
    result.actor_behavior_scene =
        load_optional_runtime_actor_behavior_scene_resource_v1(
            package, limits.foundation.required_content_api_version,
            limits.actor_behavior_scene);
  } catch (const RuntimeActorBehaviorSceneResourceError &error) {
    fail("Cannot mount the runtime actor-behavior scene: " +
         std::string(error.what()));
  }

  if (result.foundation.level_id != package.level_id ||
      result.foundation.content_api_version != package.content_api_version) {
    fail("Mounted runtime level content has inconsistent package identity");
  }
  if (result.actor_library.has_value() != result.entity_scene.has_value()) {
    fail("Runtime actor/entity feature pair is incomplete");
  }
  if (result.actor_animation_bank && !result.actor_library) {
    fail("Runtime actor-animation bank is present without its actor library");
  }
  if (result.actor_animation_bank) {
    validate_actor_animation_contract(*result.actor_animation_bank,
                                      *result.actor_library);
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
  if (result.actor_behavior_scene &&
      (!result.entity_scene || !result.actor_library ||
       !result.actor_animation_bank)) {
    fail("Runtime actor-behavior scene is present without its complete "
         "actor, animation, and entity feature set");
  }
  if (result.actor_behavior_scene) {
    validate_actor_behavior_entity_contract(
        *result.actor_behavior_scene, *result.entity_scene,
        *result.actor_library, *result.actor_animation_bank,
        result.destructible_scene ? &*result.destructible_scene : nullptr,
        package.level_id);
  }
  return result;
}

std::vector<ActorBehaviorInitialPresentationEnabledV1>
resolve_actor_behavior_initial_presentation_v1(
    const RuntimeLevelContentV1 &content) {
  if (!content.actor_behavior_scene) {
    return {};
  }
  if (!content.entity_scene ||
      content.actor_behavior_scene->level_id != content.foundation.level_id ||
      content.entity_scene->level_id != content.foundation.level_id) {
    fail("Actor-behavior presentation state is disconnected from its entity "
         "scene");
  }

  std::vector<ActorBehaviorInitialPresentationEnabledV1> result;
  result.reserve(content.actor_behavior_scene->instances.size());
  for (const auto &instance : content.actor_behavior_scene->instances) {
    const auto *const definition =
        find_definition(*content.entity_scene, instance.authored_id);
    if (definition == nullptr) {
      fail("Actor-behavior presentation state references a missing entity "
           "definition");
    }
    result.push_back(ActorBehaviorInitialPresentationEnabledV1{
        instance.authored_id,
        (definition->flags & kEntityDefinitionInitiallyEnabledV1) != 0U,
    });
  }
  return result;
}

std::vector<ActorBehaviorEntityCapabilitiesV1>
resolve_actor_behavior_entity_capabilities_v1(
    const RuntimeLevelContentV1 &content) {
  if (!content.actor_behavior_scene) {
    return {};
  }
  // Also validates behavior-instance membership and all three level IDs.
  static_cast<void>(resolve_actor_behavior_initial_presentation_v1(content));
  const auto &entities = *content.entity_scene;
  const auto &instances = content.actor_behavior_scene->instances;
  std::vector<ActorBehaviorEntityCapabilitiesV1> result;
  result.reserve(entities.definitions.size());
  for (const auto &definition : entities.definitions) {
    std::uint32_t flags = 0U;
    if (find_entity_transform(entities, definition.authored_id) != nullptr) {
      flags |= kActorBehaviorEntityReferenceRequiresTransformV1;
    }
    if (find_actor_binding(entities, definition.authored_id) != nullptr) {
      flags |= kActorBehaviorEntityReferenceRequiresActorV1;
    }
    const auto behavior = std::lower_bound(
        instances.begin(), instances.end(), definition.authored_id,
        [](const ActorBehaviorInstanceV1 &instance, const std::uint32_t id) {
          return instance.authored_id < id;
        });
    if (behavior != instances.end() &&
        behavior->authored_id == definition.authored_id) {
      flags |= kActorBehaviorEntityReferenceRequiresBehaviorV1;
    }
    result.push_back({definition.authored_id, flags});
  }
  return result;
}

} // namespace openrc::game
