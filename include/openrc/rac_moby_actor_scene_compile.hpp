#pragma once

#include "openrc/actor_library.hpp"
#include "openrc/entity_scene.hpp"
#include "openrc/rac_gameplay_bank.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

struct RacMobyActorSceneCompileProfileV1 {
  std::uint32_t source_class_id = 0U;
  std::string model_semantic_key;
  std::string archetype_key;
  ActorAffineTransformV1 model_to_entity;
};

struct RacMobyActorSceneCompileLimitsV1 {
  std::uint64_t max_source_instances = 0U;
  ActorLibraryLimitsV1 actor_library;
  EntitySceneLimitsV1 entity_scene;
};

struct RacMobyActorSceneCompileResultV1 {
  EntitySceneV1 entity_scene;
  // Source static-Moby ordinals, also used as stable authored entity IDs.
  std::vector<std::uint32_t> authored_ids;
};

class RacMobyActorSceneCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Appends every placement of one ordinary animated RAC1 Moby class to a
// neutral entity scene. Geometry and animation remain shared package assets;
// each source placement becomes an actor-bound entity with its exact source
// ordinal, position, Euler-derived rotation, and uniform scale. No AI, health,
// animation-state, or opaque PVar meaning is invented by this presentation
// boundary.
[[nodiscard]] RacMobyActorSceneCompileResultV1
compile_rac_moby_actor_scene_v1(
    const EntitySceneV1 &base_entity_scene,
    const ActorLibraryV1 &single_model_actor_library,
    std::span<const RacGameplayMobyInstanceV1> static_mobies,
    const RacMobyActorSceneCompileProfileV1 &profile,
    RacMobyActorSceneCompileLimitsV1 limits);

} // namespace openrc
