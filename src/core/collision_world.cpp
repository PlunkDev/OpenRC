#include "openrc/collision_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const char* const message) {
    throw CollisionWorldError(message);
}

[[nodiscard]] bool valid_layer(const CollisionLayerV1 layer) noexcept {
    return layer == CollisionLayerV1::world ||
           layer == CollisionLayerV1::hero_only;
}

void validate_layer_mask(const CollisionLayerMaskV1 mask) {
    if ((mask & static_cast<CollisionLayerMaskV1>(
                    ~kCollisionAllLayersMaskV1)) != 0U) {
        fail("The collision layer mask contains an unknown layer");
    }
}

void validate_build_limits(const CollisionWorldBuildLimitsV1& limits) {
    if (limits.max_vertices == 0U || limits.max_triangles == 0U ||
        limits.max_grid_cells == 0U ||
        limits.max_grid_triangle_references == 0U ||
        limits.grid_cell_size_q6 <= 0) {
        fail("Collision-world build limits must all be positive");
    }
    if (limits.max_vertices > std::numeric_limits<std::uint32_t>::max() ||
        limits.max_triangles > std::numeric_limits<std::uint32_t>::max()) {
        fail("Collision-world indexed-mesh limits exceed the V1 index width");
    }
}

void validate_query_limits(const CollisionQueryLimitsV1& limits) {
    if (limits.max_cells_to_visit == 0U || limits.max_candidates == 0U) {
        fail("Collision query limits must all be positive");
    }
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const message) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(message);
    }
    return left * right;
}

[[nodiscard]] std::int64_t floor_divide(
    const std::int64_t value,
    const std::int64_t positive_divisor) noexcept {
    auto quotient = value / positive_divisor;
    const auto remainder = value % positive_divisor;
    if (remainder < 0) {
        --quotient;
    }
    return quotient;
}

[[nodiscard]] std::uint64_t inclusive_span(
    const std::int64_t minimum,
    const std::int64_t maximum) {
    if (maximum < minimum) {
        fail("A collision grid coordinate span is inverted");
    }
    return static_cast<std::uint64_t>(maximum - minimum) + 1U;
}

[[nodiscard]] std::uint64_t cell_volume(
    const std::int64_t minimum_x,
    const std::int64_t maximum_x,
    const std::int64_t minimum_y,
    const std::int64_t maximum_y,
    const std::int64_t minimum_z,
    const std::int64_t maximum_z) {
    const auto xy = checked_multiply(
        inclusive_span(minimum_x, maximum_x),
        inclusive_span(minimum_y, maximum_y),
        "A collision grid cell span overflows");
    return checked_multiply(
        xy,
        inclusive_span(minimum_z, maximum_z),
        "A collision grid cell span overflows");
}

struct GridCoordinateLess final {
    [[nodiscard]] bool operator()(
        const CollisionGridCoordinateV1& left,
        const CollisionGridCoordinateV1& right) const noexcept {
        return std::tie(left.x, left.y, left.z) <
               std::tie(right.x, right.y, right.z);
    }
};

[[nodiscard]] bool same_coordinate(
    const CollisionGridCoordinateV1& left,
    const CollisionGridCoordinateV1& right) noexcept {
    return left == right;
}

[[nodiscard]] CollisionGridCoordinateV1 grid_coordinate(
    const std::int64_t x,
    const std::int64_t y,
    const std::int64_t z) {
    if (x < std::numeric_limits<std::int32_t>::min() ||
        x > std::numeric_limits<std::int32_t>::max() ||
        y < std::numeric_limits<std::int32_t>::min() ||
        y > std::numeric_limits<std::int32_t>::max() ||
        z < std::numeric_limits<std::int32_t>::min() ||
        z > std::numeric_limits<std::int32_t>::max()) {
        fail("A collision grid coordinate exceeds the V1 coordinate width");
    }
    return {
        static_cast<std::int32_t>(x),
        static_cast<std::int32_t>(y),
        static_cast<std::int32_t>(z),
    };
}

void validate_bounds(const CollisionAabbQ6V1& bounds) {
    if (bounds.minimum.x > bounds.maximum.x ||
        bounds.minimum.y > bounds.maximum.y ||
        bounds.minimum.z > bounds.maximum.z) {
        fail("A collision query AABB is inverted");
    }
}

[[nodiscard]] std::int32_t rounded_q6(
    const double value,
    const bool round_down) {
    if (!std::isfinite(value)) {
        fail("A collision coordinate is not finite");
    }
    const auto scaled = value *
        static_cast<double>(kCollisionQ6UnitsPerWorldUnitV1);
    if (!std::isfinite(scaled)) {
        fail("A collision coordinate exceeds the Q6 range");
    }
    const auto rounded = round_down ? std::floor(scaled) : std::ceil(scaled);
    if (rounded < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        rounded > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        fail("A collision coordinate exceeds the Q6 range");
    }
    return static_cast<std::int32_t>(rounded);
}

[[nodiscard]] CollisionVectorV1 subtract(
    const CollisionVectorV1 left,
    const CollisionVectorV1 right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] CollisionVectorV1 cross(
    const CollisionVectorV1 left,
    const CollisionVectorV1 right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] double dot(
    const CollisionVectorV1 left,
    const CollisionVectorV1 right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

} // namespace

double collision_q6_to_world_units_v1(const std::int32_t value_q6) noexcept {
    return static_cast<double>(value_q6) /
           static_cast<double>(kCollisionQ6UnitsPerWorldUnitV1);
}

std::int32_t collision_world_units_to_q6_v1(const double value) {
    if (!std::isfinite(value)) {
        fail("A collision coordinate is not finite");
    }
    const auto scaled = value *
        static_cast<double>(kCollisionQ6UnitsPerWorldUnitV1);
    if (!std::isfinite(scaled)) {
        fail("A collision coordinate exceeds the Q6 range");
    }
    const auto rounded = std::round(scaled);
    if (rounded < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        rounded > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        fail("A collision coordinate exceeds the Q6 range");
    }
    return static_cast<std::int32_t>(rounded);
}

CollisionVectorV1 collision_q6_position_to_world_v1(
    const CollisionPositionQ6V1 position) noexcept {
    return {
        collision_q6_to_world_units_v1(position.x),
        collision_q6_to_world_units_v1(position.y),
        collision_q6_to_world_units_v1(position.z),
    };
}

CollisionPositionQ6V1 collision_world_position_to_q6_v1(
    const CollisionVectorV1 position) {
    return {
        collision_world_units_to_q6_v1(position.x),
        collision_world_units_to_q6_v1(position.y),
        collision_world_units_to_q6_v1(position.z),
    };
}

CollisionAabbQ6V1 collision_world_aabb_to_q6_v1(
    const CollisionVectorV1 minimum,
    const CollisionVectorV1 maximum) {
    if (!std::isfinite(minimum.x) || !std::isfinite(minimum.y) ||
        !std::isfinite(minimum.z) || !std::isfinite(maximum.x) ||
        !std::isfinite(maximum.y) || !std::isfinite(maximum.z) ||
        minimum.x > maximum.x || minimum.y > maximum.y ||
        minimum.z > maximum.z) {
        fail("A world-space collision AABB is invalid");
    }
    return {
        {
            rounded_q6(minimum.x, true),
            rounded_q6(minimum.y, true),
            rounded_q6(minimum.z, true),
        },
        {
            rounded_q6(maximum.x, false),
            rounded_q6(maximum.y, false),
            rounded_q6(maximum.z, false),
        },
    };
}

CollisionWorldV1 build_collision_world_v1(
    CollisionMeshV1 mesh,
    const CollisionWorldBuildLimitsV1 limits) {
    validate_build_limits(limits);
    if (mesh.schema_version != kCollisionMeshSchemaVersionV1) {
        fail("The collision mesh has an unsupported schema version");
    }
    if (mesh.vertices.size() > limits.max_vertices ||
        mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail("The collision mesh vertex count exceeds the caller's limit");
    }
    if (mesh.triangles.size() > limits.max_triangles ||
        mesh.triangles.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail("The collision mesh triangle count exceeds the caller's limit");
    }

    using CellReferences = std::vector<std::uint32_t>;
    std::map<CollisionGridCoordinateV1, CellReferences, GridCoordinateLess>
        cells;
    std::uint64_t total_references = 0U;
    const auto cell_size = static_cast<std::int64_t>(
        limits.grid_cell_size_q6);

    for (std::size_t triangle_index = 0U;
         triangle_index < mesh.triangles.size(); ++triangle_index) {
        const auto& triangle = mesh.triangles[triangle_index];
        if (!valid_layer(triangle.layer)) {
            fail("A collision triangle has an unknown layer");
        }
        if (triangle.surface.has_source_type) {
            if (triangle.surface.kind !=
                    (triangle.surface.raw_type & UINT8_C(0x1f)) ||
                triangle.surface.sound_id !=
                    (triangle.surface.raw_type >> 5U)) {
                fail("A collision triangle has an inconsistent raw surface split");
            }
        } else if (triangle.surface.raw_type != 0U ||
                   triangle.surface.kind != 0U ||
                   triangle.surface.sound_id != 0U) {
            fail("A collision triangle without a source surface has surface data");
        }
        for (const auto vertex_index : triangle.vertex_indices) {
            if (vertex_index >= mesh.vertices.size()) {
                fail("A collision triangle references a missing vertex");
            }
        }

        const auto& vertex_0 = mesh.vertices[triangle.vertex_indices[0U]];
        const auto& vertex_1 = mesh.vertices[triangle.vertex_indices[1U]];
        const auto& vertex_2 = mesh.vertices[triangle.vertex_indices[2U]];
        const auto minimum_x = std::min({vertex_0.x, vertex_1.x, vertex_2.x});
        const auto maximum_x = std::max({vertex_0.x, vertex_1.x, vertex_2.x});
        const auto minimum_y = std::min({vertex_0.y, vertex_1.y, vertex_2.y});
        const auto maximum_y = std::max({vertex_0.y, vertex_1.y, vertex_2.y});
        const auto minimum_z = std::min({vertex_0.z, vertex_1.z, vertex_2.z});
        const auto maximum_z = std::max({vertex_0.z, vertex_1.z, vertex_2.z});
        const auto minimum_cell_x = floor_divide(minimum_x, cell_size);
        const auto maximum_cell_x = floor_divide(maximum_x, cell_size);
        const auto minimum_cell_y = floor_divide(minimum_y, cell_size);
        const auto maximum_cell_y = floor_divide(maximum_y, cell_size);
        const auto minimum_cell_z = floor_divide(minimum_z, cell_size);
        const auto maximum_cell_z = floor_divide(maximum_z, cell_size);
        const auto new_references = cell_volume(
            minimum_cell_x, maximum_cell_x,
            minimum_cell_y, maximum_cell_y,
            minimum_cell_z, maximum_cell_z);
        if (total_references > limits.max_grid_triangle_references ||
            new_references >
                limits.max_grid_triangle_references - total_references) {
            fail("The collision grid reference count exceeds the caller's limit");
        }
        total_references += new_references;

        for (auto x = minimum_cell_x; x <= maximum_cell_x; ++x) {
            for (auto y = minimum_cell_y; y <= maximum_cell_y; ++y) {
                for (auto z = minimum_cell_z; z <= maximum_cell_z; ++z) {
                    const auto coordinate = grid_coordinate(x, y, z);
                    auto iterator = cells.find(coordinate);
                    if (iterator == cells.end()) {
                        if (cells.size() >= limits.max_grid_cells) {
                            fail("The collision grid cell count exceeds the caller's limit");
                        }
                        iterator = cells.emplace(coordinate, CellReferences{}).first;
                    }
                    if (iterator->second.size() >= iterator->second.max_size()) {
                        fail("A collision grid cell exceeds its host container limit");
                    }
                    iterator->second.push_back(
                        static_cast<std::uint32_t>(triangle_index));
                }
            }
        }
    }

    CollisionWorldV1 world;
    world.schema_version = kCollisionWorldSchemaVersionV1;
    world.mesh = std::move(mesh);
    world.grid.cell_size_q6 = limits.grid_cell_size_q6;
    if (cells.size() > world.grid.cells.max_size() ||
        total_references > world.grid.triangle_references.max_size()) {
        fail("The collision grid exceeds a host container limit");
    }
    world.grid.cells.reserve(cells.size());
    world.grid.triangle_references.reserve(
        static_cast<std::size_t>(total_references));
    for (auto& [coordinate, references] : cells) {
        if (references.size() > std::numeric_limits<std::uint32_t>::max()) {
            fail("A collision grid cell exceeds the V1 reference-count width");
        }
        world.grid.cells.push_back(CollisionUniformGridCellV1{
            coordinate,
            static_cast<std::uint64_t>(world.grid.triangle_references.size()),
            static_cast<std::uint32_t>(references.size()),
        });
        world.grid.triangle_references.insert(
            world.grid.triangle_references.end(),
            references.begin(), references.end());
    }
    return world;
}

std::vector<std::uint32_t> query_collision_candidates_v1(
    const CollisionWorldV1& world,
    const CollisionAabbQ6V1 bounds,
    const CollisionLayerMaskV1 layer_mask,
    const CollisionQueryLimitsV1 limits) {
    validate_query_limits(limits);
    validate_layer_mask(layer_mask);
    validate_bounds(bounds);
    if (world.schema_version != kCollisionWorldSchemaVersionV1 ||
        world.mesh.schema_version != kCollisionMeshSchemaVersionV1) {
        fail("The collision world has an unsupported schema version");
    }
    if (world.grid.cell_size_q6 <= 0) {
        fail("The collision world has an invalid grid cell size");
    }
    if (layer_mask == 0U) {
        return {};
    }

    const auto cell_size = static_cast<std::int64_t>(world.grid.cell_size_q6);
    const auto minimum_cell_x = floor_divide(bounds.minimum.x, cell_size);
    const auto maximum_cell_x = floor_divide(bounds.maximum.x, cell_size);
    const auto minimum_cell_y = floor_divide(bounds.minimum.y, cell_size);
    const auto maximum_cell_y = floor_divide(bounds.maximum.y, cell_size);
    const auto minimum_cell_z = floor_divide(bounds.minimum.z, cell_size);
    const auto maximum_cell_z = floor_divide(bounds.maximum.z, cell_size);
    const auto cells_to_visit = cell_volume(
        minimum_cell_x, maximum_cell_x,
        minimum_cell_y, maximum_cell_y,
        minimum_cell_z, maximum_cell_z);
    if (cells_to_visit > limits.max_cells_to_visit) {
        fail("The collision query cell count exceeds the caller's limit");
    }

    std::vector<std::uint8_t> seen(world.mesh.triangles.size(), UINT8_C(0));
    std::vector<std::uint32_t> result;
    result.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(
        limits.max_candidates,
        static_cast<std::uint64_t>(world.mesh.triangles.size()))));
    const GridCoordinateLess less;
    for (auto x = minimum_cell_x; x <= maximum_cell_x; ++x) {
        for (auto y = minimum_cell_y; y <= maximum_cell_y; ++y) {
            for (auto z = minimum_cell_z; z <= maximum_cell_z; ++z) {
                const auto coordinate = grid_coordinate(x, y, z);
                const auto iterator = std::lower_bound(
                    world.grid.cells.begin(), world.grid.cells.end(), coordinate,
                    [&less](
                        const CollisionUniformGridCellV1& cell,
                        const CollisionGridCoordinateV1& value) {
                        return less(cell.coordinate, value);
                    });
                if (iterator == world.grid.cells.end() ||
                    !same_coordinate(iterator->coordinate, coordinate)) {
                    continue;
                }
                if (iterator->first_reference >
                        world.grid.triangle_references.size() ||
                    iterator->reference_count >
                        world.grid.triangle_references.size() -
                            iterator->first_reference) {
                    fail("A collision grid cell has an invalid reference range");
                }
                const auto reference_end = iterator->first_reference +
                    iterator->reference_count;
                for (auto reference = iterator->first_reference;
                     reference < reference_end; ++reference) {
                    const auto triangle_index =
                        world.grid.triangle_references[
                            static_cast<std::size_t>(reference)];
                    if (triangle_index >= world.mesh.triangles.size()) {
                        fail("A collision grid cell references a missing triangle");
                    }
                    const auto& triangle = world.mesh.triangles[triangle_index];
                    if (!valid_layer(triangle.layer)) {
                        fail("A collision triangle has an unknown layer");
                    }
                    if (!collision_layer_mask_contains_v1(
                            layer_mask, triangle.layer) ||
                        seen[triangle_index] != 0U) {
                        continue;
                    }
                    if (result.size() >= limits.max_candidates) {
                        fail("The collision query candidate count exceeds the caller's limit");
                    }
                    seen[triangle_index] = UINT8_C(1);
                    result.push_back(triangle_index);
                }
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::optional<CollisionRayHitV1> raycast_collision_world_v1(
    const CollisionWorldV1& world,
    const CollisionRayV1 ray,
    const CollisionLayerMaskV1 layer_mask,
    const CollisionQueryLimitsV1 limits) {
    if (!std::isfinite(ray.origin.x) || !std::isfinite(ray.origin.y) ||
        !std::isfinite(ray.origin.z) || !std::isfinite(ray.direction.x) ||
        !std::isfinite(ray.direction.y) || !std::isfinite(ray.direction.z) ||
        !std::isfinite(ray.max_distance) || ray.max_distance < 0.0) {
        fail("A collision ray is invalid");
    }
    const auto direction_length_squared = dot(ray.direction, ray.direction);
    if (!std::isfinite(direction_length_squared) ||
        direction_length_squared <= 0.0) {
        fail("A collision ray direction has zero or invalid length");
    }
    const auto inverse_direction_length =
        1.0 / std::sqrt(direction_length_squared);
    const CollisionVectorV1 direction{
        ray.direction.x * inverse_direction_length,
        ray.direction.y * inverse_direction_length,
        ray.direction.z * inverse_direction_length,
    };
    const CollisionVectorV1 end{
        ray.origin.x + direction.x * ray.max_distance,
        ray.origin.y + direction.y * ray.max_distance,
        ray.origin.z + direction.z * ray.max_distance,
    };
    const CollisionVectorV1 minimum{
        std::min(ray.origin.x, end.x),
        std::min(ray.origin.y, end.y),
        std::min(ray.origin.z, end.z),
    };
    const CollisionVectorV1 maximum{
        std::max(ray.origin.x, end.x),
        std::max(ray.origin.y, end.y),
        std::max(ray.origin.z, end.z),
    };
    const auto candidates = query_collision_candidates_v1(
        world,
        collision_world_aabb_to_q6_v1(minimum, maximum),
        layer_mask,
        limits);

    constexpr double kIntersectionEpsilon = 1.0e-12;
    std::optional<CollisionRayHitV1> closest;
    for (const auto triangle_index : candidates) {
        const auto& triangle = world.mesh.triangles[triangle_index];
        for (const auto vertex_index : triangle.vertex_indices) {
            if (vertex_index >= world.mesh.vertices.size()) {
                fail("A collision triangle references a missing vertex");
            }
        }
        const auto vertex_0 = collision_q6_position_to_world_v1(
            world.mesh.vertices[triangle.vertex_indices[0U]]);
        const auto vertex_1 = collision_q6_position_to_world_v1(
            world.mesh.vertices[triangle.vertex_indices[1U]]);
        const auto vertex_2 = collision_q6_position_to_world_v1(
            world.mesh.vertices[triangle.vertex_indices[2U]]);
        const auto edge_1 = subtract(vertex_1, vertex_0);
        const auto edge_2 = subtract(vertex_2, vertex_0);
        const auto determinant_vector = cross(direction, edge_2);
        const auto determinant = dot(edge_1, determinant_vector);
        if (std::abs(determinant) <= kIntersectionEpsilon) {
            continue;
        }
        const auto inverse_determinant = 1.0 / determinant;
        const auto origin_delta = subtract(ray.origin, vertex_0);
        const auto barycentric_u =
            dot(origin_delta, determinant_vector) * inverse_determinant;
        if (barycentric_u < -kIntersectionEpsilon ||
            barycentric_u > 1.0 + kIntersectionEpsilon) {
            continue;
        }
        const auto barycentric_vector = cross(origin_delta, edge_1);
        const auto barycentric_v =
            dot(direction, barycentric_vector) * inverse_determinant;
        if (barycentric_v < -kIntersectionEpsilon ||
            barycentric_u + barycentric_v > 1.0 + kIntersectionEpsilon) {
            continue;
        }
        const auto raw_distance =
            dot(edge_2, barycentric_vector) * inverse_determinant;
        if (raw_distance < -kIntersectionEpsilon ||
            raw_distance > ray.max_distance + kIntersectionEpsilon) {
            continue;
        }
        const auto distance = std::clamp(
            raw_distance, 0.0, ray.max_distance);
        if (closest.has_value() && distance >= closest->distance) {
            continue;
        }
        const auto unnormalized_normal = cross(edge_1, edge_2);
        const auto normal_length_squared =
            dot(unnormalized_normal, unnormalized_normal);
        if (normal_length_squared <= kIntersectionEpsilon) {
            continue;
        }
        const auto inverse_normal_length = 1.0 / std::sqrt(normal_length_squared);
        closest = CollisionRayHitV1{
            triangle_index,
            distance,
            {
                ray.origin.x + direction.x * distance,
                ray.origin.y + direction.y * distance,
                ray.origin.z + direction.z * distance,
            },
            {
                unnormalized_normal.x * inverse_normal_length,
                unnormalized_normal.y * inverse_normal_length,
                unnormalized_normal.z * inverse_normal_length,
            },
        };
    }
    return closest;
}

} // namespace openrc
