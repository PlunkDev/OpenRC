#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kCollisionMeshSchemaVersionV1 = 1U;
inline constexpr std::uint32_t kCollisionWorldSchemaVersionV1 = 1U;
inline constexpr std::int32_t kCollisionQ6UnitsPerWorldUnitV1 = 64;
inline constexpr std::int32_t kCollisionDefaultGridCellSizeQ6V1 =
    4 * kCollisionQ6UnitsPerWorldUnitV1;

// Neutral runtime coordinates are right-handed and Z-up. One integer step is
// exactly 1/64 of a natural world unit.
struct CollisionPositionQ6V1 {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;

    [[nodiscard]] bool operator==(const CollisionPositionQ6V1&) const = default;
};

struct CollisionVectorV1 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    [[nodiscard]] bool operator==(const CollisionVectorV1&) const = default;
};

struct CollisionAabbQ6V1 {
    CollisionPositionQ6V1 minimum;
    CollisionPositionQ6V1 maximum;

    [[nodiscard]] bool operator==(const CollisionAabbQ6V1&) const = default;
};

enum class CollisionLayerV1 : std::uint8_t {
    world = 0U,
    hero_only = 1U,
};

using CollisionLayerMaskV1 = std::uint8_t;

[[nodiscard]] constexpr CollisionLayerMaskV1 collision_layer_mask_v1(
    const CollisionLayerV1 layer) noexcept {
    if (layer == CollisionLayerV1::world) {
        return UINT8_C(1);
    }
    if (layer == CollisionLayerV1::hero_only) {
        return UINT8_C(2);
    }
    return UINT8_C(0);
}

inline constexpr CollisionLayerMaskV1 kCollisionWorldLayerMaskV1 =
    collision_layer_mask_v1(CollisionLayerV1::world);
inline constexpr CollisionLayerMaskV1 kCollisionHeroOnlyLayerMaskV1 =
    collision_layer_mask_v1(CollisionLayerV1::hero_only);
inline constexpr CollisionLayerMaskV1 kCollisionAllLayersMaskV1 =
    kCollisionWorldLayerMaskV1 | kCollisionHeroOnlyLayerMaskV1;

[[nodiscard]] constexpr bool collision_layer_mask_contains_v1(
    const CollisionLayerMaskV1 mask,
    const CollisionLayerV1 layer) noexcept {
    return (mask & collision_layer_mask_v1(layer)) != 0U;
}

// Some source formats carry a raw surface byte while other layers do not, so
// callers must inspect has_source_type before interpreting raw_type. Kind and
// sound are only a lossless bit split; this type deliberately assigns no
// gameplay meaning to either value.
struct CollisionSurfaceV1 {
    bool has_source_type = false;
    std::uint8_t raw_type = 0U;
    std::uint8_t kind = 0U;
    std::uint8_t sound_id = 0U;

    [[nodiscard]] bool operator==(const CollisionSurfaceV1&) const = default;
};

struct CollisionTriangleV1 {
    std::array<std::uint32_t, 3U> vertex_indices{};
    CollisionSurfaceV1 surface;
    CollisionLayerV1 layer = CollisionLayerV1::world;

    [[nodiscard]] bool operator==(const CollisionTriangleV1&) const = default;
};

struct CollisionMeshV1 {
    std::uint32_t schema_version = kCollisionMeshSchemaVersionV1;
    std::vector<CollisionPositionQ6V1> vertices;
    std::vector<CollisionTriangleV1> triangles;

    [[nodiscard]] bool operator==(const CollisionMeshV1&) const = default;
};

struct CollisionGridCoordinateV1 {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;

    [[nodiscard]] bool operator==(const CollisionGridCoordinateV1&) const = default;
};

struct CollisionUniformGridCellV1 {
    CollisionGridCoordinateV1 coordinate;
    std::uint64_t first_reference = 0U;
    std::uint32_t reference_count = 0U;

    [[nodiscard]] bool operator==(
        const CollisionUniformGridCellV1&) const = default;
};

struct CollisionUniformGridV1 {
    std::int32_t cell_size_q6 = kCollisionDefaultGridCellSizeQ6V1;
    // Cells are sorted lexicographically by (x, y, z). Each cell references
    // a sorted sequence of triangle indices in triangle_references.
    std::vector<CollisionUniformGridCellV1> cells;
    std::vector<std::uint32_t> triangle_references;

    [[nodiscard]] bool operator==(const CollisionUniformGridV1&) const = default;
};

struct CollisionWorldV1 {
    std::uint32_t schema_version = kCollisionWorldSchemaVersionV1;
    CollisionMeshV1 mesh;
    CollisionUniformGridV1 grid;

    [[nodiscard]] bool operator==(const CollisionWorldV1&) const = default;
};

struct CollisionWorldBuildLimitsV1 {
    std::uint64_t max_vertices = 0U;
    std::uint64_t max_triangles = 0U;
    std::uint64_t max_grid_cells = 0U;
    std::uint64_t max_grid_triangle_references = 0U;
    std::int32_t grid_cell_size_q6 = 0;

    [[nodiscard]] bool operator==(
        const CollisionWorldBuildLimitsV1&) const = default;
};

struct CollisionQueryLimitsV1 {
    std::uint64_t max_cells_to_visit = 0U;
    std::uint64_t max_candidates = 0U;

    [[nodiscard]] bool operator==(
        const CollisionQueryLimitsV1&) const = default;
};

struct CollisionRayV1 {
    CollisionVectorV1 origin;
    // The direction need not be normalized. A hit distance is always returned
    // in world units along the normalized direction.
    CollisionVectorV1 direction;
    double max_distance = 0.0;
};

struct CollisionRayHitV1 {
    std::uint32_t triangle_index = 0U;
    double distance = 0.0;
    CollisionVectorV1 position;
    // Geometric normal produced by the canonical triangle winding.
    CollisionVectorV1 normal;

    [[nodiscard]] bool operator==(const CollisionRayHitV1&) const = default;
};

class CollisionWorldError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] double collision_q6_to_world_units_v1(
    std::int32_t value_q6) noexcept;

// Converts to the nearest Q6 value. Non-finite and out-of-range coordinates
// are rejected instead of being silently clamped.
[[nodiscard]] std::int32_t collision_world_units_to_q6_v1(double value);

[[nodiscard]] CollisionVectorV1 collision_q6_position_to_world_v1(
    CollisionPositionQ6V1 position) noexcept;

[[nodiscard]] CollisionPositionQ6V1 collision_world_position_to_q6_v1(
    CollisionVectorV1 position);

// This conversion is conservative: minima are rounded down and maxima up.
[[nodiscard]] CollisionAabbQ6V1 collision_world_aabb_to_q6_v1(
    CollisionVectorV1 minimum,
    CollisionVectorV1 maximum);

// Validates a neutral mesh and builds a source-independent uniform grid.
[[nodiscard]] CollisionWorldV1 build_collision_world_v1(
    CollisionMeshV1 mesh,
    CollisionWorldBuildLimitsV1 limits);

// Returns unique triangle indices in ascending order. Bounds are inclusive.
[[nodiscard]] std::vector<std::uint32_t> query_collision_candidates_v1(
    const CollisionWorldV1& world,
    CollisionAabbQ6V1 bounds,
    CollisionLayerMaskV1 layer_mask,
    CollisionQueryLimitsV1 limits);

[[nodiscard]] std::optional<CollisionRayHitV1> raycast_collision_world_v1(
    const CollisionWorldV1& world,
    CollisionRayV1 ray,
    CollisionLayerMaskV1 layer_mask,
    CollisionQueryLimitsV1 limits);

} // namespace openrc
