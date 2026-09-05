#include "openrc/runtime_player_actor.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimePlayerActorError(message);
}

template <typename Items, typename Predicate>
[[nodiscard]] std::size_t require_unique_index(
    const Items &items, Predicate &&matches, const char *const missing_message,
    const char *const ambiguous_message) {
  std::optional<std::size_t> result;
  for (std::size_t index = 0U; index < items.size(); ++index) {
    if (!matches(items[index])) {
      continue;
    }
    if (result) {
      fail(ambiguous_message);
    }
    result = index;
  }
  if (!result) {
    fail(missing_message);
  }
  return *result;
}

[[nodiscard]] std::uint32_t stable_actor_index(const std::size_t index,
                                               const std::uint32_t asset_id,
                                               const char *const description) {
  if (index > std::numeric_limits<std::uint32_t>::max() || asset_id != index) {
    fail(std::string("Runtime actor library has a non-canonical ") +
         description + " index");
  }
  return static_cast<std::uint32_t>(index);
}

} // namespace

std::optional<RuntimePlayerActorResolutionV1>
resolve_runtime_player_actor_v1(const RuntimeLevelContentV1 &content,
                                const std::uint32_t local_player_slot) {
  if (!content.actor_library && !content.entity_scene) {
    return std::nullopt;
  }
  if (!content.actor_library || !content.entity_scene) {
    fail("Runtime actor/entity feature pair is incomplete");
  }

  const auto &library = *content.actor_library;
  const auto &scene = *content.entity_scene;
  const auto player_binding_index = require_unique_index(
      scene.player_bindings,
      [local_player_slot](const PlayerEntityBindingV1 &binding) {
        return binding.local_player_slot == local_player_slot;
      },
      "Entity scene has no binding for the requested local player slot",
      "Entity scene ambiguously binds the requested local player slot");
  const auto &player_binding = scene.player_bindings[player_binding_index];

  const auto definition_index = require_unique_index(
      scene.definitions,
      [&player_binding](const EntityDefinitionV1 &definition) {
        return definition.authored_id == player_binding.authored_id;
      },
      "Player binding references a missing entity definition",
      "Player binding ambiguously resolves to multiple entity definitions");
  const auto &definition = scene.definitions[definition_index];
  if ((definition.flags & kEntityDefinitionInitiallyEnabledV1) == 0U) {
    fail("Player entity definition is disabled");
  }

  const auto actor_binding_index = require_unique_index(
      scene.actor_bindings,
      [&player_binding](const EntityActorBindingV1 &binding) {
        return binding.authored_id == player_binding.authored_id;
      },
      "Player entity has no actor binding",
      "Player entity ambiguously resolves to multiple actor bindings");
  const auto &actor_binding = scene.actor_bindings[actor_binding_index];

  const auto model_index = require_unique_index(
      library.models,
      [&actor_binding](const ActorModelV1 &model) {
        return model.semantic_key == actor_binding.model_key;
      },
      "Player actor binding references a missing actor model",
      "Player actor binding ambiguously resolves to multiple actor models");
  const auto &model = library.models[model_index];

  const auto rig_index = require_unique_index(
      library.rigs,
      [&model](const ActorRigAssetV1 &rig) {
        return rig.semantic_key == model.rig_key;
      },
      "Player actor model references a missing actor rig",
      "Player actor model ambiguously resolves to multiple actor rigs");
  const auto &rig = library.rigs[rig_index];

  return RuntimePlayerActorResolutionV1{
      player_binding.authored_id,
      stable_actor_index(rig_index, rig.id, "rig"),
      stable_actor_index(model_index, model.id, "model"),
      actor_binding.model_to_entity,
  };
}

} // namespace openrc::game
