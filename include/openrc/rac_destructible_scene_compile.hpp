#pragma once

#include "openrc/destructible_scene.hpp"
#include "openrc/entity_scene.hpp"
#include "openrc/rac_level_moby_assets.hpp"
#include "openrc/render_scene.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

struct RacDestructibleCompileProfileV1 {
  std::uint32_t source_class_id = 0U;
  std::string archetype_key;
  std::uint32_t max_health = 0U;
  game::DamageChannelMaskV1 accepted_damage_channels = 0U;
  std::vector<DestructibleDropV1> drops;

  [[nodiscard]] bool
  operator==(const RacDestructibleCompileProfileV1 &) const = default;
};

struct RacDestructibleSceneCompileLimitsV1 {
  std::uint64_t max_source_instances = 0U;
  std::uint64_t max_source_vertices = 0U;
  std::uint64_t max_source_triangles = 0U;
  RenderSceneLimitsV1 render_scene;
  EntitySceneLimitsV1 entity_scene;
  DestructibleSceneLimitsV1 destructible_scene;

  [[nodiscard]] bool
  operator==(const RacDestructibleSceneCompileLimitsV1 &) const = default;
};

struct RacDestructibleSceneCompileResultV1 {
  RenderSceneV1 render_scene;
  EntitySceneV1 entity_scene;
  DestructibleSceneV1 destructible_scene;

  // Both vectors are parallel to matching source placements in complete
  // static-moby table ordinal order.
  std::vector<std::uint32_t> authored_ids;
  std::vector<std::uint32_t> render_instance_ids;

  [[nodiscard]] bool
  operator==(const RacDestructibleSceneCompileResultV1 &) const = default;
};

class RacDestructibleSceneCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compiler-only RAC1 adapter for one static high-LOD model. The source span
// must be the complete static_mobies table: each matching record's zero-based
// table ordinal becomes its stable neutral authored ID. The model-local mesh,
// used decoded images, and materials are appended once and shared by all
// matching render instances. No RAC source type crosses the three returned
// canonical neutral scene boundaries.
[[nodiscard]] RacDestructibleSceneCompileResultV1
compile_rac_destructible_scene_v1(
    const RenderSceneV1 &base_render_scene,
    const EntitySceneV1 &base_entity_scene,
    const DestructibleSceneV1 &base_destructible_scene,
    const RacLevelMobyModelV1 &source_model,
    const RacLevelMobyTextureBankV1 &texture_bank,
    std::span<const RacGameplayMobyInstanceV1> static_mobies,
    const RacDestructibleCompileProfileV1 &profile,
    RacDestructibleSceneCompileLimitsV1 limits);

} // namespace openrc
