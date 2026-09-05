#include "openrc/rac_level_collision_compile.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace openrc {
namespace {

[[noreturn]] void fail(const char* const message) {
    throw CollisionWorldError(message);
}

void validate_limits(const RacLevelCollisionCompileLimitsV1& limits) {
    if (limits.max_source_octants == 0U ||
        limits.max_source_main_vertices == 0U ||
        limits.max_source_main_faces == 0U ||
        limits.max_source_hero_groups == 0U ||
        limits.max_source_hero_vertices == 0U ||
        limits.max_source_hero_triangles == 0U ||
        limits.world.max_vertices == 0U ||
        limits.world.max_triangles == 0U ||
        limits.world.max_grid_cells == 0U ||
        limits.world.max_grid_triangle_references == 0U ||
        limits.world.grid_cell_size_q6 <= 0) {
        fail("RAC1 collision compile limits must all be positive");
    }
}

void add_bounded(
    std::uint64_t& total,
    const std::uint64_t addition,
    const std::uint64_t limit,
    const char* const message) {
    if (total > limit || addition > limit - total) {
        fail(message);
    }
    total += addition;
}

[[nodiscard]] std::int32_t sign_extend(
    const std::uint32_t value,
    const std::uint32_t bits) noexcept {
    const auto sign_bit = UINT32_C(1) << (bits - 1U);
    const auto mask = (UINT32_C(1) << bits) - 1U;
    const auto narrowed = value & mask;
    if ((narrowed & sign_bit) == 0U) {
        return static_cast<std::int32_t>(narrowed);
    }
    return static_cast<std::int32_t>(narrowed | ~mask);
}

[[nodiscard]] std::int32_t checked_q6(
    const std::int64_t value) {
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max()) {
        fail("A compiled collision coordinate exceeds the Q6 range");
    }
    return static_cast<std::int32_t>(value);
}

[[nodiscard]] CollisionPositionQ6V1 exact_main_position(
    const RacLevelCollisionOctantV1& octant,
    const RacLevelCollisionVertexV1& vertex) {
    const auto center_x =
        (static_cast<std::int64_t>(octant.grid_x) * 4 + 2) *
        kCollisionQ6UnitsPerWorldUnitV1;
    const auto center_y =
        (static_cast<std::int64_t>(octant.grid_y) * 4 + 2) *
        kCollisionQ6UnitsPerWorldUnitV1;
    const auto center_z =
        (static_cast<std::int64_t>(octant.grid_z) * 4 + 2) *
        kCollisionQ6UnitsPerWorldUnitV1;
    const auto packed = vertex.packed_value;
    const auto local_x = static_cast<std::int64_t>(
        sign_extend(packed, 10U)) * 4;
    const auto local_y = static_cast<std::int64_t>(
        sign_extend(packed >> 10U, 10U)) * 4;
    const auto local_z = static_cast<std::int64_t>(
        sign_extend(packed >> 20U, 12U));
    return {
        checked_q6(center_x + local_x),
        checked_q6(center_y + local_y),
        checked_q6(center_z + local_z),
    };
}

[[nodiscard]] CollisionPositionQ6V1 exact_hero_position(
    const RacLevelHeroCollisionVertexV1& vertex) noexcept {
    return {
        static_cast<std::int32_t>(vertex.packed_position[0U]),
        static_cast<std::int32_t>(vertex.packed_position[1U]),
        static_cast<std::int32_t>(vertex.packed_position[2U]),
    };
}

struct PositionLess final {
    [[nodiscard]] bool operator()(
        const CollisionPositionQ6V1& left,
        const CollisionPositionQ6V1& right) const noexcept {
        return std::tie(left.x, left.y, left.z) <
               std::tie(right.x, right.y, right.z);
    }
};

[[nodiscard]] std::array<std::uint32_t, 3U> canonical_cyclic_indices(
    const std::array<std::uint32_t, 3U>& indices) noexcept {
    const std::array<std::uint32_t, 3U> rotation_1{
        indices[1U], indices[2U], indices[0U]};
    const std::array<std::uint32_t, 3U> rotation_2{
        indices[2U], indices[0U], indices[1U]};
    return std::min(indices, std::min(rotation_1, rotation_2));
}

struct TriangleKey final {
    std::array<std::uint32_t, 3U> cyclic_indices{};
    CollisionLayerV1 layer = CollisionLayerV1::world;
    bool has_source_type = false;
    std::uint8_t raw_surface_type = 0U;
};

struct TriangleKeyLess final {
    [[nodiscard]] bool operator()(
        const TriangleKey& left,
        const TriangleKey& right) const noexcept {
        return std::tie(
                   left.cyclic_indices,
                   left.layer,
                   left.has_source_type,
                   left.raw_surface_type) <
               std::tie(
                   right.cyclic_indices,
                   right.layer,
                   right.has_source_type,
                   right.raw_surface_type);
    }
};

class MeshCompiler final {
public:
    explicit MeshCompiler(const CollisionWorldBuildLimitsV1& limits)
        : limits_(limits) {}

    void add_triangle(
        const std::array<CollisionPositionQ6V1, 3U>& positions,
        const CollisionSurfaceV1 surface,
        const CollisionLayerV1 layer) {
        std::array<std::uint32_t, 3U> indices{};
        for (std::size_t index = 0U; index < positions.size(); ++index) {
            indices[index] = intern_vertex(positions[index]);
        }
        const TriangleKey key{
            canonical_cyclic_indices(indices),
            layer,
            surface.has_source_type,
            surface.raw_type,
        };
        if (!triangles_.insert(key).second) {
            return;
        }
        if (mesh_.triangles.size() >= limits_.max_triangles ||
            mesh_.triangles.size() >=
                std::numeric_limits<std::uint32_t>::max() ||
            mesh_.triangles.size() >= mesh_.triangles.max_size()) {
            fail("The compiled collision triangle count exceeds the caller's limit");
        }
        mesh_.triangles.push_back(CollisionTriangleV1{
            indices,
            surface,
            layer,
        });
    }

    [[nodiscard]] CollisionMeshV1 take_mesh() && {
        return std::move(mesh_);
    }

private:
    [[nodiscard]] std::uint32_t intern_vertex(
        const CollisionPositionQ6V1 position) {
        const auto existing = vertices_.find(position);
        if (existing != vertices_.end()) {
            return existing->second;
        }
        if (mesh_.vertices.size() >= limits_.max_vertices ||
            mesh_.vertices.size() >=
                std::numeric_limits<std::uint32_t>::max() ||
            mesh_.vertices.size() >= mesh_.vertices.max_size()) {
            fail("The compiled collision vertex count exceeds the caller's limit");
        }
        const auto index = static_cast<std::uint32_t>(mesh_.vertices.size());
        mesh_.vertices.push_back(position);
        vertices_.emplace(position, index);
        return index;
    }

    const CollisionWorldBuildLimitsV1& limits_;
    CollisionMeshV1 mesh_;
    std::map<CollisionPositionQ6V1, std::uint32_t, PositionLess> vertices_;
    std::set<TriangleKey, TriangleKeyLess> triangles_;
};

[[nodiscard]] CollisionSurfaceV1 source_surface(
    const std::uint8_t raw_type) noexcept {
    return CollisionSurfaceV1{
        true,
        raw_type,
        static_cast<std::uint8_t>(raw_type & UINT8_C(0x1f)),
        static_cast<std::uint8_t>(raw_type >> 5U),
    };
}

} // namespace

CollisionWorldV1 compile_rac_level_collision_world_v1(
    const RacLevelCollisionV1& source,
    const RacLevelCollisionCompileLimitsV1 limits) {
    validate_limits(limits);
    if (source.octants.size() > limits.max_source_octants) {
        fail("The source collision octant count exceeds the caller's limit");
    }
    if (source.hero_groups.size() > limits.max_source_hero_groups) {
        fail("The source hero collision group count exceeds the caller's limit");
    }

    std::uint64_t source_main_vertices = 0U;
    std::uint64_t source_main_faces = 0U;
    std::uint64_t source_hero_vertices = 0U;
    std::uint64_t source_hero_triangles = 0U;
    MeshCompiler compiler(limits.world);

    for (const auto& octant : source.octants) {
        add_bounded(
            source_main_vertices,
            static_cast<std::uint64_t>(octant.vertices.size()),
            limits.max_source_main_vertices,
            "The source collision vertex count exceeds the caller's limit");
        add_bounded(
            source_main_faces,
            static_cast<std::uint64_t>(octant.faces.size()),
            limits.max_source_main_faces,
            "The source collision face count exceeds the caller's limit");
        for (const auto& face : octant.faces) {
            if (face.vertex_count != 3U && face.vertex_count != 4U) {
                fail("A source collision face is neither a triangle nor a quad");
            }
            for (std::uint32_t index = 0U; index < face.vertex_count; ++index) {
                if (face.packed_vertex_indices[index] >= octant.vertices.size()) {
                    fail("A source collision face references a missing vertex");
                }
            }
            const auto position = [&octant, &face](const std::size_t index) {
                return exact_main_position(
                    octant,
                    octant.vertices[face.packed_vertex_indices[index]]);
            };
            const auto surface = source_surface(face.surface_type);
            if (face.vertex_count == 3U) {
                compiler.add_triangle(
                    {position(2U), position(1U), position(0U)},
                    surface,
                    CollisionLayerV1::world);
                continue;
            }
            compiler.add_triangle(
                {position(3U), position(2U), position(1U)},
                surface,
                CollisionLayerV1::world);
            compiler.add_triangle(
                {position(3U), position(1U), position(0U)},
                surface,
                CollisionLayerV1::world);
        }
    }

    for (const auto& group : source.hero_groups) {
        add_bounded(
            source_hero_vertices,
            static_cast<std::uint64_t>(group.vertices.size()),
            limits.max_source_hero_vertices,
            "The source hero collision vertex count exceeds the caller's limit");
        add_bounded(
            source_hero_triangles,
            static_cast<std::uint64_t>(group.triangles.size()),
            limits.max_source_hero_triangles,
            "The source hero collision triangle count exceeds the caller's limit");
        for (const auto& triangle : group.triangles) {
            for (const auto index : triangle.packed_vertex_indices) {
                if (index >= group.vertices.size()) {
                    fail("A source hero collision triangle references a missing vertex");
                }
            }
            const auto position = [&group, &triangle](const std::size_t index) {
                return exact_hero_position(
                    group.vertices[triangle.packed_vertex_indices[index]]);
            };
            compiler.add_triangle(
                {position(2U), position(1U), position(0U)},
                CollisionSurfaceV1{},
                CollisionLayerV1::hero_only);
        }
    }

    return build_collision_world_v1(
        std::move(compiler).take_mesh(),
        limits.world);
}

} // namespace openrc
