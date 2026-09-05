#pragma once

#include "openrc/actor_rig.hpp"
#include "openrc/game_world.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kEntitySceneSchemaVersionV1 = 1U;
inline constexpr double kEntitySceneQuaternionUnitToleranceV1 = 1.0e-5;

inline constexpr std::uint32_t kEntityDefinitionInitiallyEnabledV1 = 1U << 0U;
inline constexpr std::uint32_t kEntityDefinitionKnownFlagsV1 =
    kEntityDefinitionInitiallyEnabledV1;
inline constexpr std::uint32_t kEntitySceneNoAuthoringGroupIdV1 = UINT32_MAX;

// authored_id is a sparse, stable identifier supplied by the level compiler.
// Archetypes use semantic keys so packages and overlays never compete for a
// process-global numeric ID. authoring_group_id is provenance-only metadata;
// UINT32_MAX denotes an absent source group.
struct EntityDefinitionV1 {
  std::uint32_t authored_id = 0U;
  std::string archetype_key;
  std::uint32_t flags = kEntityDefinitionInitiallyEnabledV1;
  std::uint32_t authoring_group_id = kEntitySceneNoAuthoringGroupIdV1;

  [[nodiscard]] bool operator==(const EntityDefinitionV1 &) const = default;
};

struct EntityTransformComponentV1 {
  std::uint32_t authored_id = 0U;
  game::WorldTransformV1 transform;

  [[nodiscard]] bool
  operator==(const EntityTransformComponentV1 &) const = default;
};

struct EntityRenderBindingV1 {
  std::uint32_t authored_id = 0U;
  std::uint32_t render_instance_id = 0U;

  [[nodiscard]] bool operator==(const EntityRenderBindingV1 &) const = default;
};

// Animated models are bound independently of world transforms. The actor model
// is transformed into entity-local space here and an optional transform
// component supplies entity-to-world placement.
struct EntityActorBindingV1 {
  std::uint32_t authored_id = 0U;
  std::string model_key;
  ActorAffineTransformV1 model_to_entity;

  [[nodiscard]] bool operator==(const EntityActorBindingV1 &) const = default;
};

// Player-bound definitions intentionally have no EntityTransformComponentV1.
// LevelBootstrapV1 supplies the initial spawn and PlayerSimulation remains the
// authoritative source of the active runtime pose.
struct PlayerEntityBindingV1 {
  std::uint32_t authored_id = 0U;
  std::uint32_t local_player_slot = 0U;

  [[nodiscard]] bool operator==(const PlayerEntityBindingV1 &) const = default;
};

struct EntitySceneV1 {
  std::uint32_t schema_version = kEntitySceneSchemaVersionV1;
  game::LevelIdV1 level_id = 0U;

  // Every table is canonical only when strictly ascending by authored_id.
  std::vector<EntityDefinitionV1> definitions;
  std::vector<EntityTransformComponentV1> transforms;
  std::vector<EntityRenderBindingV1> render_bindings;
  std::vector<EntityActorBindingV1> actor_bindings;
  std::vector<PlayerEntityBindingV1> player_bindings;

  [[nodiscard]] bool operator==(const EntitySceneV1 &) const = default;
};

struct EntitySceneLimitsV1 {
  std::uint32_t max_definitions = 0U;
  std::uint32_t max_transforms = 0U;
  std::uint32_t max_render_bindings = 0U;
  std::uint32_t max_actor_bindings = 0U;
  std::uint32_t max_player_bindings = 0U;
  std::uint32_t max_archetype_key_bytes = 0U;
  std::uint32_t max_model_key_bytes = 0U;
  std::uint64_t max_total_key_bytes = 0U;

  [[nodiscard]] bool operator==(const EntitySceneLimitsV1 &) const = default;
};

class EntitySceneError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Requires canonical sparse authored-ID order, valid references and flags,
// canonical archetype/model keys, finite positive-zero float encodings,
// non-zero scale, and a unit quaternion within
// kEntitySceneQuaternionUnitToleranceV1 whose first non-zero component in
// W/X/Y/Z order is positive. Each non-player definition has exactly one
// transform; player definitions have none. Actor and render bindings are
// mutually exclusive, and local player slots are unique.
void validate_entity_scene_v1(const EntitySceneV1 &scene,
                              EntitySceneLimitsV1 limits);

// Sorts all authored-ID tables and canonicalizes signed zero and quaternion
// sign before applying the same strict validation. Quaternion magnitude is not
// silently changed: authored rotations must already be unit length within the
// validation tolerance.
[[nodiscard]] EntitySceneV1
canonicalize_entity_scene_v1(EntitySceneV1 scene,
                             EntitySceneLimitsV1 limits);

} // namespace openrc
