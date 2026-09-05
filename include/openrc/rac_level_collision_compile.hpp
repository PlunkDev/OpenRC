#pragma once

#include "openrc/collision_world.hpp"
#include "openrc/rac_level_collision.hpp"

#include <cstdint>

namespace openrc {

struct RacLevelCollisionCompileLimitsV1 {
    std::uint64_t max_source_octants = 0U;
    std::uint64_t max_source_main_vertices = 0U;
    std::uint64_t max_source_main_faces = 0U;
    std::uint64_t max_source_hero_groups = 0U;
    std::uint64_t max_source_hero_vertices = 0U;
    std::uint64_t max_source_hero_triangles = 0U;
    CollisionWorldBuildLimitsV1 world;
};

// Clean-room adapter from validated RAC1 provenance records to the neutral,
// versioned runtime representation. No source offsets or sparse-tree pointers
// survive this boundary.
[[nodiscard]] CollisionWorldV1 compile_rac_level_collision_world_v1(
    const RacLevelCollisionV1& source,
    RacLevelCollisionCompileLimitsV1 limits);

} // namespace openrc
