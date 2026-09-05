#pragma once

#include "openrc/actor_library.hpp"
#include "openrc/actor_pose.hpp"
#include "openrc/entity_scene.hpp"
#include "openrc/gameplay_scene.hpp"
#include "openrc/rac_gameplay_bank.hpp"
#include "openrc/render_scene.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

struct RacCollectibleCompileProfileV1 {
  std::uint32_t source_class_id = 0U;
  std::string model_semantic_key;
  std::string archetype_key;
  std::string item_key;
  std::array<float, 3U> local_center{};
  float collection_radius = 0.0F;
  std::uint32_t amount = 0U;

  [[nodiscard]] bool
  operator==(const RacCollectibleCompileProfileV1 &) const = default;
};

struct RacCollectibleSceneCompileLimitsV1 {
  std::uint64_t max_source_instances = 0U;
  ActorLibraryLimitsV1 actor_library;
  ActorPoseLimitsV1 actor_pose;
  RenderSceneLimitsV1 render_scene;
  EntitySceneLimitsV1 entity_scene;
  GameplaySceneLimitsV1 gameplay_scene;

  [[nodiscard]] bool
  operator==(const RacCollectibleSceneCompileLimitsV1 &) const = default;
};

struct RacCollectibleSceneCompileResultV1 {
  RenderSceneV1 render_scene;
  EntitySceneV1 entity_scene;
  GameplaySceneV1 gameplay_scene;

  // Both vectors are parallel to matching source placements in static-moby
  // ordinal order.
  std::vector<std::uint32_t> authored_ids;
  std::vector<std::uint32_t> render_instance_ids;

  [[nodiscard]] bool
  operator==(const RacCollectibleSceneCompileResultV1 &) const = default;
};

class RacCollectibleSceneCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compiler-only RAC placement adapter. The source span must be the complete
// static_mobies table: its zero-based record ordinal becomes the stable neutral
// authored ID. Matching placements are appended as bind-pose render instances,
// static render-bound entities, and overlap collectibles. Source formats end at
// this boundary; all three returned scenes are canonical neutral resources.
[[nodiscard]] RacCollectibleSceneCompileResultV1
compile_rac_collectible_scene_v1(
    const RenderSceneV1 &base_render_scene,
    const EntitySceneV1 &base_entity_scene,
    const GameplaySceneV1 &base_gameplay_scene,
    const ActorLibraryV1 &single_model_actor_library,
    std::span<const RacGameplayMobyInstanceV1> static_mobies,
    const RacCollectibleCompileProfileV1 &profile,
    RacCollectibleSceneCompileLimitsV1 limits);

} // namespace openrc
