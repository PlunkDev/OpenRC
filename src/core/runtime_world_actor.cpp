#include "openrc/runtime_world_actor.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimeWorldActorError(message);
}

template <typename Items>
void require_strict_authored_order(const Items &items,
                                   const char *const description) {
  for (std::size_t index = 1U; index < items.size(); ++index) {
    if (items[index].authored_id <= items[index - 1U].authored_id) {
      fail(std::string("Entity scene ") + description +
           " are not in canonical authored-ID order");
    }
  }
}

template <typename Items>
[[nodiscard]] const typename Items::value_type *
find_authored(const Items &items, const std::uint32_t authored_id) {
  const auto found = std::lower_bound(
      items.begin(), items.end(), authored_id,
      [](const typename Items::value_type &item, const std::uint32_t id) {
        return item.authored_id < id;
      });
  return found != items.end() && found->authored_id == authored_id
             ? &*found
             : nullptr;
}

} // namespace

std::vector<RuntimeWorldActorResolutionV1>
resolve_runtime_world_actors_v1(const RuntimeLevelContentV1 &content) {
  if (!content.actor_library && !content.entity_scene) {
    return {};
  }
  if (!content.actor_library || !content.entity_scene) {
    fail("Runtime actor/entity feature pair is incomplete");
  }

  const auto &library = *content.actor_library;
  const auto &scene = *content.entity_scene;
  if (scene.actor_bindings.size() > kRuntimeWorldActorMaximumInstancesV1) {
    fail("Entity scene exceeds the runtime world-actor instance limit");
  }
  require_strict_authored_order(scene.definitions, "definitions");
  require_strict_authored_order(scene.transforms, "transforms");
  require_strict_authored_order(scene.actor_bindings, "actor bindings");
  require_strict_authored_order(scene.player_bindings, "player bindings");

  if (library.rigs.size() > std::numeric_limits<std::uint32_t>::max() ||
      library.models.size() > std::numeric_limits<std::uint32_t>::max()) {
    fail("Runtime actor library exceeds the neutral asset-ID domain");
  }
  std::unordered_map<std::string_view, std::uint32_t> rig_indices;
  rig_indices.reserve(library.rigs.size());
  for (std::size_t index = 0U; index < library.rigs.size(); ++index) {
    const auto &rig = library.rigs[index];
    if (rig.id != index ||
        !rig_indices.emplace(rig.semantic_key,
                             static_cast<std::uint32_t>(index)).second) {
      fail("Runtime actor library has non-canonical rig identities");
    }
  }
  std::unordered_map<std::string_view, std::uint32_t> model_indices;
  model_indices.reserve(library.models.size());
  std::vector<std::uint32_t> model_rig_indices(library.models.size());
  for (std::size_t index = 0U; index < library.models.size(); ++index) {
    const auto &model = library.models[index];
    const auto rig = rig_indices.find(model.rig_key);
    if (model.id != index || rig == rig_indices.end() ||
        !model_indices.emplace(model.semantic_key,
                               static_cast<std::uint32_t>(index)).second) {
      fail("Runtime actor library has non-canonical model identities");
    }
    model_rig_indices[index] = rig->second;
  }

  std::vector<RuntimeWorldActorResolutionV1> result;
  result.reserve(scene.actor_bindings.size());
  for (const auto &binding : scene.actor_bindings) {
    if (find_authored(scene.player_bindings, binding.authored_id) != nullptr) {
      continue;
    }

    const auto *const definition =
        find_authored(scene.definitions, binding.authored_id);
    if (definition == nullptr) {
      fail("World actor binding references a missing entity definition");
    }
    const auto *const transform =
        find_authored(scene.transforms, binding.authored_id);
    if (transform == nullptr) {
      fail("World actor entity has no world transform");
    }
    const auto model = model_indices.find(binding.model_key);
    if (model == model_indices.end()) {
      fail("World actor binding references a missing actor model");
    }
    const auto model_index = model->second;
    const auto rig_index = model_rig_indices[model_index];

    result.push_back(RuntimeWorldActorResolutionV1{
        binding.authored_id,
        rig_index,
        model_index,
        binding.model_to_entity,
        transform->transform,
        (definition->flags & kEntityDefinitionInitiallyEnabledV1) != 0U,
    });
  }
  return result;
}

const ActorAnimationClipV1 *find_runtime_world_actor_initial_animation_v1(
    const RuntimeLevelContentV1 &content,
    const RuntimeWorldActorResolutionV1 &actor) {
  if (!content.actor_library) {
    fail("World actor initial animation requires an actor library");
  }
  const auto &library = *content.actor_library;
  if (actor.actor_rig_index >= library.rigs.size() ||
      actor.actor_model_index >= library.models.size()) {
    fail("World actor initial animation resolution exceeds its actor library");
  }
  const auto &rig = library.rigs[actor.actor_rig_index];
  const auto &model = library.models[actor.actor_model_index];
  if (model.rig_key != rig.semantic_key) {
    fail("World actor initial animation has an inconsistent model/rig binding");
  }
  if (!content.actor_animation_bank) {
    return nullptr;
  }

  const std::string_view rig_key = rig.semantic_key;
  const ActorAnimationClipV1 *result = nullptr;
  for (const auto &clip : content.actor_animation_bank->clips) {
    const std::string_view clip_key = clip.semantic_key;
    const auto classified_initial = clip_key.find(
        kRuntimeWorldActorInitialAnimationKeySegmentV1);
    if (clip.rig_key != rig_key ||
        classified_initial == std::string_view::npos) {
      continue;
    }
    if (result != nullptr) {
      fail("World actor rig has multiple classified initial animations");
    }
    result = &clip;
  }
  return result;
}

} // namespace openrc::game
