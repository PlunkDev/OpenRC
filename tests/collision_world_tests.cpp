#include "openrc/collision_world.hpp"
#include "openrc/rac_level_collision_compile.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr openrc::CollisionWorldBuildLimitsV1 kWorldLimits{
    1024U,
    1024U,
    1024U,
    8192U,
    openrc::kCollisionDefaultGridCellSizeQ6V1,
};

constexpr openrc::RacLevelCollisionCompileLimitsV1 kCompileLimits{
    128U,
    4096U,
    4096U,
    128U,
    4096U,
    4096U,
    kWorldLimits,
};

constexpr openrc::CollisionQueryLimitsV1 kQueryLimits{
    4096U,
    4096U,
};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_near(
    const double actual,
    const double expected,
    const double tolerance,
    const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

template <typename Callback>
void expect_collision_error(Callback&& callback, const std::string& message) {
    try {
        std::invoke(std::forward<Callback>(callback));
    } catch (const openrc::CollisionWorldError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] std::uint32_t packed_signed(
    const std::int32_t value,
    const std::uint32_t bits) {
    return static_cast<std::uint32_t>(value) &
           ((UINT32_C(1) << bits) - 1U);
}

[[nodiscard]] std::uint32_t pack_vertex(
    const std::int32_t x_sixteenths,
    const std::int32_t y_sixteenths,
    const std::int32_t z_sixty_fourths) {
    return packed_signed(x_sixteenths, 10U) |
           (packed_signed(y_sixteenths, 10U) << 10U) |
           (packed_signed(z_sixty_fourths, 12U) << 20U);
}

[[nodiscard]] openrc::RacLevelCollisionVertexV1 source_vertex(
    const std::int32_t x_sixteenths,
    const std::int32_t y_sixteenths,
    const std::int32_t z_sixty_fourths) {
    openrc::RacLevelCollisionVertexV1 result;
    result.packed_value = pack_vertex(
        x_sixteenths, y_sixteenths, z_sixty_fourths);
    // Deliberately wrong: the neutral compiler must never round-trip through
    // parser convenience floats when exact packed Q6 data is available.
    result.world_position = {9999.0F, 9999.0F, 9999.0F};
    return result;
}

[[nodiscard]] openrc::RacLevelCollisionFaceV1 source_face(
    const std::array<std::uint8_t, 4U> indices,
    const std::uint8_t vertex_count,
    const std::uint8_t raw_surface_type) {
    openrc::RacLevelCollisionFaceV1 result;
    result.packed_vertex_indices = indices;
    result.vertex_count = vertex_count;
    result.surface_type = raw_surface_type;
    return result;
}

[[nodiscard]] openrc::RacLevelCollisionOctantV1 source_octant() {
    openrc::RacLevelCollisionOctantV1 octant;
    octant.grid_x = 1;
    octant.grid_y = 0;
    octant.grid_z = 0;
    octant.vertices = {
        source_vertex(0, 0, 0),
        source_vertex(16, 0, 0),
        source_vertex(16, 16, 0),
        source_vertex(0, 16, 0),
    };
    octant.faces = {
        source_face({0U, 1U, 2U, 3U}, 4U, 0x8dU),
        source_face({0U, 2U, 3U, 0U}, 3U, 0x2cU),
    };
    return octant;
}

[[nodiscard]] openrc::RacLevelCollisionV1 make_source_collision() {
    openrc::RacLevelCollisionV1 source;
    source.octants.push_back(source_octant());

    auto duplicate = source_octant();
    duplicate.faces = {
        source_face({0U, 1U, 2U, 3U}, 4U, 0x8dU),
        // This is a cyclic rotation of the first octant's triangle after
        // source winding reversal and must be deduplicated.
        source_face({3U, 0U, 2U, 0U}, 3U, 0x2cU),
        // Same geometry and kind, but a different raw byte must survive.
        source_face({3U, 0U, 2U, 0U}, 3U, 0x4cU),
    };
    source.octants.push_back(std::move(duplicate));

    openrc::RacLevelCollisionOctantV1 vertex_only;
    vertex_only.grid_x = -9;
    vertex_only.grid_y = -9;
    vertex_only.grid_z = -9;
    vertex_only.vertices.push_back(source_vertex(-1, -1, -1));
    source.octants.push_back(std::move(vertex_only));

    openrc::RacLevelHeroCollisionGroupV1 hero;
    hero.vertices.resize(3U);
    hero.vertices[0U].packed_position = {384U, 128U, 128U};
    hero.vertices[1U].packed_position = {448U, 192U, 128U};
    hero.vertices[2U].packed_position = {384U, 192U, 128U};
    for (auto& vertex : hero.vertices) {
        vertex.world_position = {-9999.0F, -9999.0F, -9999.0F};
    }
    openrc::RacLevelHeroCollisionTriangleV1 triangle;
    triangle.packed_vertex_indices = {0U, 1U, 2U};
    hero.triangles.push_back(triangle);
    source.hero_groups.push_back(std::move(hero));
    return source;
}

[[nodiscard]] openrc::CollisionSurfaceV1 raw_surface(
    const std::uint8_t value) {
    return {
        true,
        value,
        static_cast<std::uint8_t>(value & 0x1fU),
        static_cast<std::uint8_t>(value >> 5U),
    };
}

[[nodiscard]] openrc::CollisionWorldV1 make_flat_world() {
    openrc::CollisionMeshV1 mesh;
    mesh.vertices = {
        {0, 0, 0},
        {64, 0, 0},
        {0, 64, 0},
    };
    mesh.triangles.push_back({
        {0U, 1U, 2U},
        raw_surface(0x81U),
        openrc::CollisionLayerV1::world,
    });
    return openrc::build_collision_world_v1(std::move(mesh), kWorldLimits);
}

void test_exact_compile_winding_surfaces_and_deduplication() {
    const auto source = make_source_collision();
    const auto world = openrc::compile_rac_level_collision_world_v1(
        source, kCompileLimits);
    expect(
        world.schema_version == openrc::kCollisionWorldSchemaVersionV1 &&
            world.mesh.schema_version == openrc::kCollisionMeshSchemaVersionV1,
        "the neutral collision schema version is wrong");
    expect(
        world.mesh.vertices ==
            std::vector<openrc::CollisionPositionQ6V1>{
                {384, 192, 128},
                {448, 192, 128},
                {448, 128, 128},
                {384, 128, 128},
            },
        "packed RAC1 vertices were not converted directly and exactly to Q6");
    expect(
        world.mesh.triangles.size() == 5U,
        "octant duplicates were not removed or distinct surfaces/layers were merged");
    expect(
        world.mesh.triangles[0U].vertex_indices ==
                std::array<std::uint32_t, 3U>{0U, 1U, 2U} &&
            world.mesh.triangles[1U].vertex_indices ==
                std::array<std::uint32_t, 3U>{0U, 2U, 3U} &&
            world.mesh.triangles[2U].vertex_indices ==
                std::array<std::uint32_t, 3U>{0U, 1U, 3U},
        "triangle reversal or quad triangulation is wrong");
    expect(
        world.mesh.triangles[3U].vertex_indices ==
                std::array<std::uint32_t, 3U>{1U, 3U, 0U} &&
            world.mesh.triangles[3U].surface == raw_surface(0x4cU) &&
            world.mesh.triangles[2U].surface == raw_surface(0x2cU),
        "raw surface identity was lost during oriented deduplication");
    expect(
        world.mesh.triangles[0U].surface == raw_surface(0x8dU) &&
            world.mesh.triangles[0U].surface.kind == 0x0dU &&
            world.mesh.triangles[0U].surface.sound_id == 4U,
        "the lossless RAC1 surface-byte split is wrong");
    expect(
        world.mesh.triangles[4U].vertex_indices ==
                std::array<std::uint32_t, 3U>{0U, 1U, 3U} &&
            world.mesh.triangles[4U].layer ==
                openrc::CollisionLayerV1::hero_only &&
            !world.mesh.triangles[4U].surface.has_source_type,
        "hero collision winding, layer, or absent surface provenance is wrong");
    expect(
        world == openrc::compile_rac_level_collision_world_v1(
                     source, kCompileLimits),
        "collision compilation is not deterministic");
}

void test_signed_main_q6_conversion() {
    openrc::RacLevelCollisionV1 source;
    openrc::RacLevelCollisionOctantV1 octant;
    octant.grid_x = -1;
    octant.grid_y = -2;
    octant.grid_z = -3;
    octant.vertices = {
        source_vertex(-16, -16, -64),
        source_vertex(16, -16, -64),
        source_vertex(-16, 16, 64),
    };
    octant.faces.push_back(source_face({0U, 1U, 2U, 0U}, 3U, 0x1fU));
    source.octants.push_back(std::move(octant));
    const auto world = openrc::compile_rac_level_collision_world_v1(
        source, kCompileLimits);
    expect(
        world.mesh.vertices ==
            std::vector<openrc::CollisionPositionQ6V1>{
                {-192, -320, -576},
                {-64, -448, -704},
                {-192, -448, -704},
            },
        "signed packed coordinates or negative octant centers lost exact Q6 values");
}

void test_uniform_grid_and_candidate_query() {
    const auto world = openrc::compile_rac_level_collision_world_v1(
        make_source_collision(), kCompileLimits);
    expect(
        world.grid.cell_size_q6 == 256 && world.grid.cells.size() == 1U &&
            world.grid.cells[0U].coordinate ==
                openrc::CollisionGridCoordinateV1{1, 0, 0} &&
            world.grid.cells[0U].reference_count == 5U,
        "the neutral uniform-grid index is wrong");
    const openrc::CollisionAabbQ6V1 bounds{
        {384, 128, 128},
        {448, 192, 128},
    };
    expect(
        openrc::query_collision_candidates_v1(
            world, bounds, openrc::kCollisionAllLayersMaskV1,
            kQueryLimits) ==
            std::vector<std::uint32_t>{0U, 1U, 2U, 3U, 4U},
        "candidate query order or deduplication is not deterministic");
    expect(
        openrc::query_collision_candidates_v1(
            world, bounds, openrc::kCollisionWorldLayerMaskV1,
            kQueryLimits) ==
            std::vector<std::uint32_t>{0U, 1U, 2U, 3U} &&
            openrc::query_collision_candidates_v1(
                world, bounds, openrc::kCollisionHeroOnlyLayerMaskV1,
                kQueryLimits) == std::vector<std::uint32_t>{4U},
        "candidate layer masks are wrong");

    auto candidate_limit = kQueryLimits;
    candidate_limit.max_candidates = 4U;
    expect_collision_error(
        [&] {
            (void)openrc::query_collision_candidates_v1(
                world, bounds, openrc::kCollisionAllLayersMaskV1,
                candidate_limit);
        },
        "the candidate query ignored its output limit");
    auto cell_limit = kQueryLimits;
    cell_limit.max_cells_to_visit = 1U;
    expect_collision_error(
        [&] {
            (void)openrc::query_collision_candidates_v1(
                world,
                {{0, 0, 0}, {256, 0, 0}},
                openrc::kCollisionAllLayersMaskV1,
                cell_limit);
        },
        "the candidate query ignored its cell-visit limit");
    expect_collision_error(
        [&] {
            (void)openrc::query_collision_candidates_v1(
                world, bounds, UINT8_C(0x80), kQueryLimits);
        },
        "an unknown collision layer mask was accepted");
}

void test_negative_grid_coordinates_and_conversions() {
    openrc::CollisionMeshV1 mesh;
    mesh.vertices = {
        {-1, 0, 0},
        {-1, 64, 0},
        {-64, 0, 0},
    };
    mesh.triangles.push_back({
        {0U, 1U, 2U},
        raw_surface(0U),
        openrc::CollisionLayerV1::world,
    });
    const auto world = openrc::build_collision_world_v1(
        std::move(mesh), kWorldLimits);
    expect(
        world.grid.cells.size() == 1U &&
            world.grid.cells[0U].coordinate ==
                openrc::CollisionGridCoordinateV1{-1, 0, 0},
        "negative Q6 positions were truncated instead of floor-divided");
    expect(
        openrc::collision_world_units_to_q6_v1(-0.5) == -32 &&
            openrc::collision_q6_to_world_units_v1(-32) == -0.5 &&
            openrc::collision_world_position_to_q6_v1({1.5, -0.5, 0.25}) ==
                openrc::CollisionPositionQ6V1{96, -32, 16},
        "public Q6 unit conversion is wrong");
    const auto conservative = openrc::collision_world_aabb_to_q6_v1(
        {-0.001, -0.001, -0.001},
        {0.001, 0.001, 0.001});
    expect(
        conservative == openrc::CollisionAabbQ6V1{{-1, -1, -1}, {1, 1, 1}},
        "world AABB conversion is not conservative");
    expect_collision_error(
        [] {
            (void)openrc::collision_world_units_to_q6_v1(
                std::numeric_limits<double>::quiet_NaN());
        },
        "a non-finite world coordinate was accepted");
}

void test_raycast() {
    const auto world = make_flat_world();
    const auto hit = openrc::raycast_collision_world_v1(
        world,
        {{0.25, 0.25, 2.0}, {0.0, 0.0, -2.0}, 3.0},
        openrc::kCollisionWorldLayerMaskV1,
        kQueryLimits);
    expect(hit.has_value() && hit->triangle_index == 0U,
           "a downward ray missed a floor triangle");
    expect_near(hit->distance, 2.0, 1.0e-12,
                "ray distance is not measured in world units");
    expect_near(hit->position.z, 0.0, 1.0e-12,
                "ray hit position is wrong");
    expect_near(hit->normal.x, 0.0, 1.0e-12,
                "ray normal X is wrong");
    expect_near(hit->normal.y, 0.0, 1.0e-12,
                "ray normal Y is wrong");
    expect_near(hit->normal.z, 1.0, 1.0e-12,
                "ray normal does not follow canonical winding");
    expect(
        !openrc::raycast_collision_world_v1(
             world,
             {{0.25, 0.25, 2.0}, {0.0, 0.0, -1.0}, 3.0},
             openrc::kCollisionHeroOnlyLayerMaskV1,
             kQueryLimits)
             .has_value(),
        "raycast ignored its layer mask");
    expect_collision_error(
        [&] {
            (void)openrc::raycast_collision_world_v1(
                world,
                {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 1.0},
                openrc::kCollisionAllLayersMaskV1,
                kQueryLimits);
        },
        "a zero-length ray direction was accepted");
}

void test_hard_build_and_compile_limits() {
    auto source_limits = kCompileLimits;
    source_limits.max_source_main_vertices = 8U;
    expect_collision_error(
        [&] {
            (void)openrc::compile_rac_level_collision_world_v1(
                make_source_collision(), source_limits);
        },
        "the neutral compiler ignored its source vertex limit");

    auto output_limits = kCompileLimits;
    output_limits.world.max_triangles = 1U;
    expect_collision_error(
        [&] {
            (void)openrc::compile_rac_level_collision_world_v1(
                make_source_collision(), output_limits);
        },
        "the neutral compiler ignored its output triangle limit");

    openrc::CollisionMeshV1 spanning_mesh;
    spanning_mesh.vertices = {
        {0, 0, 0},
        {256, 0, 0},
        {0, 256, 0},
    };
    spanning_mesh.triangles.push_back({
        {0U, 1U, 2U},
        raw_surface(1U),
        openrc::CollisionLayerV1::world,
    });
    const auto spanning_world = openrc::build_collision_world_v1(
        spanning_mesh, kWorldLimits);
    expect(
        spanning_world.grid.cells.size() == 4U &&
            openrc::query_collision_candidates_v1(
                spanning_world,
                {{0, 0, 0}, {256, 256, 0}},
                openrc::kCollisionWorldLayerMaskV1,
                kQueryLimits) == std::vector<std::uint32_t>{0U},
        "a triangle referenced by several grid cells was returned more than once");

    auto grid_cell_limits = kWorldLimits;
    grid_cell_limits.max_grid_cells = 3U;
    expect_collision_error(
        [&] {
            (void)openrc::build_collision_world_v1(
                spanning_mesh, grid_cell_limits);
        },
        "uniform-grid construction ignored its cell limit");
    auto reference_limits = kWorldLimits;
    reference_limits.max_grid_triangle_references = 3U;
    expect_collision_error(
        [&] {
            (void)openrc::build_collision_world_v1(
                spanning_mesh, reference_limits);
        },
        "uniform-grid construction ignored its reference limit");

    openrc::CollisionMeshV1 inconsistent_surface;
    inconsistent_surface.vertices = {
        {0, 0, 0},
        {1, 0, 0},
        {0, 1, 0},
    };
    inconsistent_surface.triangles.push_back({
        {0U, 1U, 2U},
        {true, 0x81U, 0U, 0U},
        openrc::CollisionLayerV1::world,
    });
    expect_collision_error(
        [&] {
            (void)openrc::build_collision_world_v1(
                inconsistent_surface, kWorldLimits);
        },
        "an inconsistent lossless surface split was accepted");
}

} // namespace

int main() {
    try {
        test_exact_compile_winding_surfaces_and_deduplication();
        test_signed_main_q6_conversion();
        test_uniform_grid_and_candidate_query();
        test_negative_grid_coordinates_and_conversions();
        test_raycast();
        test_hard_build_and_compile_limits();
        std::cout << "collision world tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "collision world test failure: " << error.what() << '\n';
        return 1;
    }
}
