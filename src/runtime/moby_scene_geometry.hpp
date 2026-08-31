#pragma once

#include "scene_geometry.hpp"

#include "openrc/rac_gameplay_bank.hpp"
#include "openrc/rac_level_moby_assets.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>

namespace openrc::runtime {

enum class MobySceneCoordinateDomainV1 : std::uint8_t {
    world_units,
    scene_block_itof0_units,
};

inline constexpr float kSceneBlockUnitsPerWorldUnitV1 = 1024.0F;

struct MobySceneGeometryLimitsV1 {
    std::uint64_t max_models = 0U;
    std::uint64_t max_instances = 0U;
    std::uint64_t max_workspace_vertices = 0U;
    std::uint64_t max_workspace_triangle_indices = 0U;
    std::uint64_t max_output_vertices = 0U;
    std::uint64_t max_output_triangle_indices = 0U;
};

struct MobySceneGeometryStatsV1 {
    std::uint64_t model_count = 0U;
    std::uint64_t placement_count = 0U;
    std::uint64_t rendered_model_count = 0U;
    std::uint64_t rendered_placement_count = 0U;
    std::uint64_t missing_model_placement_count = 0U;
    std::uint64_t animated_model_placement_count = 0U;
    std::uint64_t empty_model_placement_count = 0U;
};

struct MobySceneGeometryV1 {
    std::optional<SceneGeometry3dV1> geometry;
    MobySceneGeometryStatsV1 stats;
    MobySceneCoordinateDomainV1 coordinate_domain =
        MobySceneCoordinateDomainV1::world_units;
};

class MobySceneGeometryError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Builds a diagnostic triangle batch for static RAC1 Moby placements. The
// transform follows the verified instance convention T * S * Rz * Ry * Rx.
// Class scale is already present in the model-local coordinates and is not
// applied again. Callers must explicitly select world units or the current
// SceneBlock viewer's raw ITOF0-input unit domain (1024 units per world unit).
// Animated, absent, and empty models are counted and skipped rather than
// rendered with guessed bind transforms.
[[nodiscard]] MobySceneGeometryV1 build_moby_scene_geometry_v1(
    std::span<const RacLevelMobyModelV1> models,
    std::span<const RacGameplayMobyInstanceV1> placements,
    MobySceneCoordinateDomainV1 coordinate_domain,
    MobySceneGeometryLimitsV1 limits);

} // namespace openrc::runtime
