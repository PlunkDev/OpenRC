#include "scene_geometry.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callback>
void expect_geometry_error(Callback&& callback, const std::string& message) {
    try {
        callback();
    } catch (const openrc::runtime::SceneGeometryError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] openrc::GifGsVertexV1 make_vertex(
    const std::uint16_t x,
    const std::uint16_t y,
    const std::uint8_t r,
    const std::uint8_t g,
    const std::uint8_t b,
    const std::uint8_t a) {
    openrc::GifGsVertexV1 vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.color.r = r;
    vertex.color.g = g;
    vertex.color.b = b;
    vertex.color.a = a;
    return vertex;
}

[[nodiscard]] openrc::GifGsPrimitiveV1 make_triangle(
    const std::uint64_t a,
    const std::uint64_t b,
    const std::uint64_t c) {
    openrc::GifGsPrimitiveV1 primitive;
    primitive.topology = openrc::GifGsPrimitiveTopologyV1::triangle_strip;
    primitive.vertex_indices = {a, b, c};
    primitive.vertex_count = 3U;
    primitive.emission = openrc::GifGsPrimitiveEmissionV1::emitted;
    return primitive;
}

[[nodiscard]] openrc::SceneBlockSourceVertexV1 make_source_vertex(
    const std::uint64_t gs_vertex_index,
    const std::int32_t x,
    const std::int32_t y,
    const std::int32_t z,
    const std::array<std::uint8_t, 4U> rgba) {
    openrc::SceneBlockSourceVertexV1 vertex;
    vertex.gs_vertex_index = gs_vertex_index;
    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    vertex.rgba = rgba;
    return vertex;
}

[[nodiscard]] openrc::runtime::SceneGeometryV1 make_runtime_raster_triangle(
    const float x_offset,
    const float y_offset) {
    openrc::runtime::SceneGeometryV1 result;
    result.vertices = {
        {x_offset - 4.0F, y_offset + 2.0F, 0xff000001U},
        {x_offset + 1.0F, y_offset + 8.0F, 0xff000002U},
        {x_offset, y_offset - 3.0F, 0xff000003U},
    };
    result.triangle_indices = {0U, 1U, 2U};
    result.minimum_x = x_offset - 4.0F;
    result.maximum_x = x_offset + 1.0F;
    result.minimum_y = y_offset - 3.0F;
    result.maximum_y = y_offset + 8.0F;
    result.emitted_triangle_count = 1U;
    return result;
}

[[nodiscard]] openrc::runtime::SceneGeometry3dV1 make_runtime_source_triangle(
    const float x_offset,
    const float y_offset,
    const float z_offset) {
    openrc::runtime::SceneGeometry3dV1 result;
    result.vertices = {
        {x_offset - 4.0F, y_offset + 2.0F, z_offset + 5.0F, 0xff000001U},
        {x_offset + 1.0F, y_offset + 8.0F, z_offset - 6.0F, 0xff000002U},
        {x_offset, y_offset - 3.0F, z_offset + 7.0F, 0xff000003U},
    };
    result.triangle_indices = {0U, 1U, 2U};
    result.minimum_x = x_offset - 4.0F;
    result.maximum_x = x_offset + 1.0F;
    result.minimum_y = y_offset - 3.0F;
    result.maximum_y = y_offset + 8.0F;
    result.minimum_z = z_offset - 6.0F;
    result.maximum_z = z_offset + 7.0F;
    result.emitted_triangle_count = 1U;
    return result;
}

void test_emitted_triangles_are_remapped_with_raster_offsets() {
    openrc::GifGsDecodeResultV1 decoded;
    decoded.vertices = {
        make_vertex(176U, 352U, 1U, 2U, 3U, 4U),
        make_vertex(336U, 512U, 5U, 6U, 7U, 8U),
        make_vertex(496U, 672U, 9U, 10U, 11U, 12U),
    };
    for (auto& vertex : decoded.vertices) {
        openrc::GifGsXyOffsetStateV1 offset;
        offset.ofx = 16U;
        offset.ofy = 32U;
        vertex.raster.context.xy_offset = offset;
    }
    decoded.primitives.push_back(make_triangle(2U, 0U, 1U));

    const auto geometry =
        openrc::runtime::build_scene_geometry_v1(decoded);
    expect(geometry.vertices.size() == 3U, "unique vertices were not remapped");
    expect(
        geometry.triangle_indices ==
            std::vector<std::uint32_t>{0U, 1U, 2U},
        "decoder-provided primitive order was not preserved");
    expect(geometry.vertices[0U].x == 30.0F &&
               geometry.vertices[0U].y == 40.0F,
           "XYOFFSET was not removed from the first remapped vertex");
    expect(geometry.vertices[1U].x == 10.0F &&
               geometry.vertices[1U].y == 20.0F,
           "GS fixed-point coordinates were not converted to pixels");
    expect(
        geometry.vertices[1U].rgba == 0x04030201U,
        "RGBA bytes were not packed for DXGI_FORMAT_R8G8B8A8_UNORM");
    expect(geometry.minimum_x == 10.0F && geometry.maximum_x == 30.0F &&
               geometry.minimum_y == 20.0F && geometry.maximum_y == 40.0F,
           "geometry bounds are incorrect");
    expect(geometry.emitted_triangle_count == 1U,
           "emitted triangle count is incorrect");
    expect(geometry.vertices_without_complete_xy_offset == 0U,
           "complete XYOFFSET state was reported missing");
    expect(geometry.vertices_with_fallback_color == 0U,
           "known vertex colors unexpectedly used the fallback");
}

void test_missing_state_uses_bounded_debug_fallbacks() {
    openrc::GifGsDecodeResultV1 decoded;
    auto first = make_vertex(16U, 32U, 0U, 0U, 0U, 0U);
    first.color.g.reset();
    decoded.vertices = {
        first,
        make_vertex(32U, 48U, 2U, 3U, 4U, 5U),
        make_vertex(48U, 64U, 3U, 4U, 5U, 6U),
    };
    decoded.primitives.push_back(make_triangle(0U, 1U, 2U));

    const auto geometry =
        openrc::runtime::build_scene_geometry_v1(decoded);
    expect(geometry.vertices[0U].x == 1.0F &&
               geometry.vertices[0U].y == 2.0F,
           "missing XYOFFSET did not fall back to raw GS pixels");
    expect(geometry.vertices[0U].rgba == 0xffffd226U,
           "missing RGB did not select the cyan debug color");
    expect(geometry.vertices_without_complete_xy_offset == 3U,
           "missing XYOFFSET diagnostics are incorrect");
    expect(geometry.vertices_with_fallback_color == 1U,
           "fallback-color diagnostics are incorrect");
}

void test_only_emitted_triangles_are_submitted() {
    openrc::GifGsDecodeResultV1 decoded;
    decoded.vertices = {
        make_vertex(0U, 0U, 1U, 1U, 1U, 1U),
        make_vertex(16U, 0U, 1U, 1U, 1U, 1U),
        make_vertex(0U, 16U, 1U, 1U, 1U, 1U),
    };

    auto suppressed = make_triangle(99U, 99U, 99U);
    suppressed.emission =
        openrc::GifGsPrimitiveEmissionV1::suppressed;
    decoded.primitives.push_back(suppressed);

    auto emitted_line = make_triangle(0U, 1U, 2U);
    emitted_line.topology =
        openrc::GifGsPrimitiveTopologyV1::line_strip;
    emitted_line.vertex_count = 2U;
    decoded.primitives.push_back(emitted_line);
    decoded.primitives.push_back(make_triangle(0U, 1U, 2U));

    const auto geometry =
        openrc::runtime::build_scene_geometry_v1(decoded);
    expect(geometry.emitted_triangle_count == 1U,
           "suppressed or non-triangle geometry was submitted");
    expect(geometry.skipped_non_triangle_count == 1U,
           "emitted non-triangle diagnostics are incorrect");
}

void test_malformed_emitted_geometry_is_rejected() {
    openrc::GifGsDecodeResultV1 missing_vertex;
    missing_vertex.vertices = {
        make_vertex(0U, 0U, 1U, 1U, 1U, 1U),
        make_vertex(16U, 0U, 1U, 1U, 1U, 1U),
    };
    missing_vertex.primitives.push_back(make_triangle(0U, 1U, 2U));
    expect_geometry_error(
        [&] {
            static_cast<void>(
                openrc::runtime::build_scene_geometry_v1(missing_vertex));
        },
        "a missing vertex reference was accepted");

    openrc::GifGsDecodeResultV1 unknown_xy;
    unknown_xy.vertices = {
        make_vertex(0U, 0U, 1U, 1U, 1U, 1U),
        make_vertex(16U, 0U, 1U, 1U, 1U, 1U),
        make_vertex(0U, 16U, 1U, 1U, 1U, 1U),
    };
    unknown_xy.vertices[2U].x.reset();
    unknown_xy.primitives.push_back(make_triangle(0U, 1U, 2U));
    expect_geometry_error(
        [&] {
            static_cast<void>(
                openrc::runtime::build_scene_geometry_v1(unknown_xy));
        },
        "an indeterminate emitted position was accepted");

    openrc::GifGsDecodeResultV1 empty;
    expect_geometry_error(
        [&] {
            static_cast<void>(
                openrc::runtime::build_scene_geometry_v1(empty));
        },
        "an empty GS stream produced runtime geometry");
}

void test_source_geometry_uses_gs_topology_without_screen_coordinates() {
    openrc::GifGsDecodeResultV1 decoded;
    decoded.vertices.resize(4U);
    decoded.primitives.push_back(make_triangle(2U, 0U, 1U));
    auto suppressed = make_triangle(99U, 99U, 99U);
    suppressed.emission = openrc::GifGsPrimitiveEmissionV1::suppressed;
    decoded.primitives.push_back(suppressed);

    openrc::SceneBlockSourceGeometryV1 source;
    source.vertices = {
        make_source_vertex(0U, -20, 10, 30, {1U, 2U, 3U, 4U}),
        make_source_vertex(1U, 40, -50, 60, {5U, 6U, 7U, 8U}),
        make_source_vertex(2U, 70, 80, -90, {9U, 10U, 11U, 12U}),
        make_source_vertex(3U, 1000, 1000, 1000, {13U, 14U, 15U, 16U}),
    };

    const auto geometry =
        openrc::runtime::build_scene_geometry_3d_v1(source, decoded);
    expect(geometry.vertices.size() == 3U,
           "unused source vertices were submitted or topology vertices were lost");
    expect(
        geometry.triangle_indices ==
            std::vector<std::uint32_t>{0U, 1U, 2U},
        "source geometry did not preserve decoder-provided primitive order");
    expect(geometry.vertices[0U].x == 70.0F &&
               geometry.vertices[0U].y == 80.0F &&
               geometry.vertices[0U].z == -90.0F,
           "signed source XYZ was not converted to runtime floats");
    expect(geometry.vertices[1U].rgba == 0x04030201U,
           "source RGBA bytes were not packed for the D3D input layout");
    expect(geometry.minimum_x == -20.0F && geometry.maximum_x == 70.0F &&
               geometry.minimum_y == -50.0F && geometry.maximum_y == 80.0F &&
               geometry.minimum_z == -90.0F && geometry.maximum_z == 60.0F,
           "source-space bounds include unused vertices or are otherwise incorrect");
    expect(geometry.emitted_triangle_count == 1U,
           "source-space emitted triangle count is incorrect");
}

void test_source_geometry_requires_one_to_one_gs_provenance() {
    openrc::GifGsDecodeResultV1 decoded;
    decoded.vertices.resize(3U);
    decoded.primitives.push_back(make_triangle(0U, 1U, 2U));

    openrc::SceneBlockSourceGeometryV1 missing;
    missing.vertices = {
        make_source_vertex(0U, 0, 0, 0, {}),
        make_source_vertex(1U, 1, 0, 0, {}),
    };
    expect_geometry_error(
        [&] {
            static_cast<void>(
                openrc::runtime::build_scene_geometry_3d_v1(missing, decoded));
        },
        "a source list that was not one-to-one with GS vertices was accepted");

    openrc::SceneBlockSourceGeometryV1 reordered;
    reordered.vertices = {
        make_source_vertex(0U, 0, 0, 0, {}),
        make_source_vertex(2U, 1, 0, 0, {}),
        make_source_vertex(1U, 0, 1, 0, {}),
    };
    expect_geometry_error(
        [&] {
            static_cast<void>(openrc::runtime::build_scene_geometry_3d_v1(
                reordered, decoded));
        },
        "a reordered source-to-GS mapping was accepted");
}

void test_raster_geometries_merge_indices_bounds_and_counters() {
    auto first = make_runtime_raster_triangle(0.0F, 0.0F);
    first.skipped_non_triangle_count = 2U;
    first.vertices_without_complete_xy_offset = 1U;
    first.vertices_with_fallback_color = 1U;

    auto second = make_runtime_raster_triangle(20.0F, 30.0F);
    second.triangle_indices = {2U, 0U, 1U};
    second.skipped_non_triangle_count = 3U;
    second.vertices_without_complete_xy_offset = 2U;

    const std::array inputs{first, second};
    const auto merged = openrc::runtime::merge_scene_geometries_v1(
        std::span<const openrc::runtime::SceneGeometryV1>(inputs),
        {6U, 6U});

    expect(merged.vertices.size() == 6U,
           "the raster merge lost or duplicated vertices");
    expect(
        merged.triangle_indices ==
            std::vector<std::uint32_t>{0U, 1U, 2U, 5U, 3U, 4U},
        "the raster merge did not apply the checked vertex-index offset");
    expect(merged.vertices[3U].x == 16.0F &&
               merged.vertices[5U].y == 27.0F,
           "the raster merge changed vertex order or coordinates");
    expect(merged.minimum_x == -4.0F && merged.maximum_x == 21.0F &&
               merged.minimum_y == -3.0F && merged.maximum_y == 38.0F,
           "the raster merge bounds are incorrect");
    expect(merged.emitted_triangle_count == 2U &&
               merged.skipped_non_triangle_count == 5U &&
               merged.vertices_without_complete_xy_offset == 3U &&
               merged.vertices_with_fallback_color == 1U,
           "the raster merge counters were not added exactly");
}

void test_source_geometries_merge_indices_bounds_and_counters() {
    auto first = make_runtime_source_triangle(0.0F, 0.0F, 0.0F);
    first.skipped_non_triangle_count = 4U;
    auto second = make_runtime_source_triangle(20.0F, 30.0F, -40.0F);
    second.triangle_indices = {1U, 2U, 0U};
    second.skipped_non_triangle_count = 5U;

    const std::array inputs{first, second};
    const auto merged = openrc::runtime::merge_scene_geometries_3d_v1(
        std::span<const openrc::runtime::SceneGeometry3dV1>(inputs),
        {6U, 6U});

    expect(merged.vertices.size() == 6U,
           "the source merge lost or duplicated vertices");
    expect(
        merged.triangle_indices ==
            std::vector<std::uint32_t>{0U, 1U, 2U, 4U, 5U, 3U},
        "the source merge did not apply the checked vertex-index offset");
    expect(merged.minimum_x == -4.0F && merged.maximum_x == 21.0F &&
               merged.minimum_y == -3.0F && merged.maximum_y == 38.0F &&
               merged.minimum_z == -46.0F && merged.maximum_z == 7.0F,
           "the source merge bounds are incorrect");
    expect(merged.emitted_triangle_count == 2U &&
               merged.skipped_non_triangle_count == 9U,
           "the source merge counters were not added exactly");
}

void test_geometry_merge_limits_and_malformed_inputs_are_rejected() {
    const auto raster = make_runtime_raster_triangle(0.0F, 0.0F);
    const std::array raster_inputs{raster};
    const auto raster_span =
        std::span<const openrc::runtime::SceneGeometryV1>(raster_inputs);

    expect_geometry_error(
        [&] {
            static_cast<void>(openrc::runtime::merge_scene_geometries_v1(
                raster_span, {2U, 3U}));
        },
        "the raster merge vertex limit was not enforced");
    expect_geometry_error(
        [&] {
            static_cast<void>(openrc::runtime::merge_scene_geometries_v1(
                raster_span, {3U, 2U}));
        },
        "the raster merge index limit was not enforced");
    expect_geometry_error(
        [&] {
            static_cast<void>(openrc::runtime::merge_scene_geometries_v1(
                raster_span, {0U, 3U}));
        },
        "a zero raster merge limit was accepted");
    expect_geometry_error(
        [&] {
            static_cast<void>(openrc::runtime::merge_scene_geometries_v1(
                {}, {3U, 3U}));
        },
        "an empty raster geometry list was accepted");

    auto bad_index = raster;
    bad_index.triangle_indices[2U] = 3U;
    const std::array bad_index_inputs{bad_index};
    expect_geometry_error(
        [&] {
            static_cast<void>(openrc::runtime::merge_scene_geometries_v1(
                std::span<const openrc::runtime::SceneGeometryV1>(
                    bad_index_inputs),
                {3U, 3U}));
        },
        "an out-of-range raster merge index was accepted");

    auto bad_bounds = raster;
    bad_bounds.maximum_x += 1.0F;
    const std::array bad_bounds_inputs{bad_bounds};
    expect_geometry_error(
        [&] {
            static_cast<void>(openrc::runtime::merge_scene_geometries_v1(
                std::span<const openrc::runtime::SceneGeometryV1>(
                    bad_bounds_inputs),
                {3U, 3U}));
        },
        "incorrect raster merge bounds were accepted");

    auto counter_left = raster;
    auto counter_right = raster;
    counter_left.skipped_non_triangle_count =
        std::numeric_limits<std::uint64_t>::max();
    counter_right.skipped_non_triangle_count = 1U;
    const std::array counter_inputs{counter_left, counter_right};
    expect_geometry_error(
        [&] {
            static_cast<void>(openrc::runtime::merge_scene_geometries_v1(
                std::span<const openrc::runtime::SceneGeometryV1>(
                    counter_inputs),
                {6U, 6U}));
        },
        "a raster merge diagnostic-counter overflow was accepted");

    auto source = make_runtime_source_triangle(0.0F, 0.0F, 0.0F);
    source.triangle_indices[1U] = 3U;
    const std::array source_inputs{source};
    expect_geometry_error(
        [&] {
            static_cast<void>(openrc::runtime::merge_scene_geometries_3d_v1(
                std::span<const openrc::runtime::SceneGeometry3dV1>(
                    source_inputs),
                {3U, 3U}));
        },
        "an out-of-range source merge index was accepted");
}

} // namespace

int main() {
    try {
        test_emitted_triangles_are_remapped_with_raster_offsets();
        test_missing_state_uses_bounded_debug_fallbacks();
        test_only_emitted_triangles_are_submitted();
        test_malformed_emitted_geometry_is_rejected();
        test_source_geometry_uses_gs_topology_without_screen_coordinates();
        test_source_geometry_requires_one_to_one_gs_provenance();
        test_raster_geometries_merge_indices_bounds_and_counters();
        test_source_geometries_merge_indices_bounds_and_counters();
        test_geometry_merge_limits_and_malformed_inputs_are_rejected();
        std::cout << "scene geometry tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "scene geometry tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
