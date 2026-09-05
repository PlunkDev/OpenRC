#pragma once

#include "moby_scene_geometry.hpp"
#include "scene_geometry.hpp"

#include "openrc/rac_gameplay_bank.hpp"
#include "openrc/rac_level_moby_assets.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc::runtime {

using TieSceneCoordinateDomainV1 = MobySceneCoordinateDomainV1;
using TieSceneGeometryLimitsV1 = MobySceneGeometryLimitsV1;

struct TieSceneGeometryStatsV1 {
    std::uint64_t model_count = 0U;
    std::uint64_t placement_count = 0U;
    std::uint64_t rendered_model_count = 0U;
    std::uint64_t rendered_placement_count = 0U;
    std::uint64_t missing_model_placement_count = 0U;
    std::uint64_t empty_model_placement_count = 0U;
};

struct TieSceneGeometryV1 {
    std::optional<SceneGeometry3dV1> geometry;
    std::vector<MobySceneMaterialBatchV1> material_batches;
    TieSceneGeometryStatsV1 stats;
    TieSceneCoordinateDomainV1 coordinate_domain =
        TieSceneCoordinateDomainV1::world_units;
};

class TieSceneGeometryError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Instantiates parsed RAC1 TIE high-detail meshes with their complete
// column-major gameplay matrices. Class scale is already present in each
// model-local vertex and is never applied again. The final matrix word (0.01
// on disc) is deliberately irrelevant to XYZ and no homogeneous divide is
// performed.
[[nodiscard]] TieSceneGeometryV1 build_tie_scene_geometry_v1(
    std::span<const RacLevelTieModelV1> models,
    std::span<const RacGameplayTieInstanceV1> placements,
    TieSceneCoordinateDomainV1 coordinate_domain,
    TieSceneGeometryLimitsV1 limits);

} // namespace openrc::runtime
