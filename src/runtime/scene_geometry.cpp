#include "scene_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace openrc::runtime {
namespace {

constexpr std::uint32_t kMissingVertexIndex =
    std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kFallbackColor =
    38U | (210U << 8U) | (255U << 16U) | (255U << 24U);

[[nodiscard]] bool is_triangle_topology(
    const GifGsPrimitiveTopologyV1 topology) noexcept {
    return topology == GifGsPrimitiveTopologyV1::triangle_list ||
           topology == GifGsPrimitiveTopologyV1::triangle_strip ||
           topology == GifGsPrimitiveTopologyV1::triangle_fan;
}

[[nodiscard]] std::uint32_t pack_color(
    const GifGsVertexV1& vertex,
    bool& used_fallback) noexcept {
    if (!vertex.color.r || !vertex.color.g || !vertex.color.b) {
        used_fallback = true;
        return kFallbackColor;
    }

    used_fallback = false;
    const auto alpha = vertex.color.a.value_or(255U);
    return static_cast<std::uint32_t>(*vertex.color.r) |
           (static_cast<std::uint32_t>(*vertex.color.g) << 8U) |
           (static_cast<std::uint32_t>(*vertex.color.b) << 16U) |
           (static_cast<std::uint32_t>(alpha) << 24U);
}

[[nodiscard]] std::uint32_t pack_color(
    const std::array<std::uint8_t, 4U>& rgba) noexcept {
    return static_cast<std::uint32_t>(rgba[0U]) |
           (static_cast<std::uint32_t>(rgba[1U]) << 8U) |
           (static_cast<std::uint32_t>(rgba[2U]) << 16U) |
           (static_cast<std::uint32_t>(rgba[3U]) << 24U);
}

[[nodiscard]] std::uint64_t checked_counter_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const message) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw SceneGeometryError(message);
    }
    return left + right;
}

void validate_merge_limits(const SceneGeometryMergeLimitsV1 limits) {
    if (limits.max_vertices == 0U || limits.max_triangle_indices == 0U) {
        throw SceneGeometryError(
            "Scene geometry merge limits must be non-zero");
    }
}

struct Bounds2d {
    float minimum_x = 0.0F;
    float maximum_x = 0.0F;
    float minimum_y = 0.0F;
    float maximum_y = 0.0F;
};

struct Bounds3d {
    float minimum_x = 0.0F;
    float maximum_x = 0.0F;
    float minimum_y = 0.0F;
    float maximum_y = 0.0F;
    float minimum_z = 0.0F;
    float maximum_z = 0.0F;
};

[[nodiscard]] Bounds2d validate_geometry(
    const SceneGeometryV1& geometry) {
    if (geometry.vertices.empty() || geometry.triangle_indices.empty()) {
        throw SceneGeometryError(
            "A raster geometry merge input is empty");
    }
    if (geometry.triangle_indices.size() % 3U != 0U ||
        geometry.emitted_triangle_count !=
            geometry.triangle_indices.size() / 3U) {
        throw SceneGeometryError(
            "A raster geometry merge input has inconsistent triangle counts");
    }
    if (geometry.vertices_without_complete_xy_offset >
            geometry.vertices.size() ||
        geometry.vertices_with_fallback_color > geometry.vertices.size()) {
        throw SceneGeometryError(
            "A raster geometry merge input has inconsistent vertex diagnostics");
    }
    for (const auto index : geometry.triangle_indices) {
        if (index >= geometry.vertices.size()) {
            throw SceneGeometryError(
                "A raster geometry merge input contains an invalid vertex index");
        }
    }

    const auto& first = geometry.vertices.front();
    if (!std::isfinite(first.x) || !std::isfinite(first.y)) {
        throw SceneGeometryError(
            "A raster geometry merge input contains a non-finite vertex");
    }
    Bounds2d bounds{first.x, first.x, first.y, first.y};
    for (const auto& vertex : geometry.vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y)) {
            throw SceneGeometryError(
                "A raster geometry merge input contains a non-finite vertex");
        }
        bounds.minimum_x = std::min(bounds.minimum_x, vertex.x);
        bounds.maximum_x = std::max(bounds.maximum_x, vertex.x);
        bounds.minimum_y = std::min(bounds.minimum_y, vertex.y);
        bounds.maximum_y = std::max(bounds.maximum_y, vertex.y);
    }
    if (geometry.minimum_x != bounds.minimum_x ||
        geometry.maximum_x != bounds.maximum_x ||
        geometry.minimum_y != bounds.minimum_y ||
        geometry.maximum_y != bounds.maximum_y) {
        throw SceneGeometryError(
            "A raster geometry merge input has incorrect bounds");
    }
    return bounds;
}

[[nodiscard]] Bounds3d validate_geometry(
    const SceneGeometry3dV1& geometry) {
    if (geometry.vertices.empty() || geometry.triangle_indices.empty()) {
        throw SceneGeometryError(
            "A source geometry merge input is empty");
    }
    if (geometry.triangle_indices.size() % 3U != 0U ||
        geometry.emitted_triangle_count !=
            geometry.triangle_indices.size() / 3U) {
        throw SceneGeometryError(
            "A source geometry merge input has inconsistent triangle counts");
    }
    for (const auto index : geometry.triangle_indices) {
        if (index >= geometry.vertices.size()) {
            throw SceneGeometryError(
                "A source geometry merge input contains an invalid vertex index");
        }
    }

    const auto& first = geometry.vertices.front();
    if (!std::isfinite(first.x) || !std::isfinite(first.y) ||
        !std::isfinite(first.z)) {
        throw SceneGeometryError(
            "A source geometry merge input contains a non-finite vertex");
    }
    Bounds3d bounds{
        first.x, first.x, first.y, first.y, first.z, first.z};
    for (const auto& vertex : geometry.vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z)) {
            throw SceneGeometryError(
                "A source geometry merge input contains a non-finite vertex");
        }
        bounds.minimum_x = std::min(bounds.minimum_x, vertex.x);
        bounds.maximum_x = std::max(bounds.maximum_x, vertex.x);
        bounds.minimum_y = std::min(bounds.minimum_y, vertex.y);
        bounds.maximum_y = std::max(bounds.maximum_y, vertex.y);
        bounds.minimum_z = std::min(bounds.minimum_z, vertex.z);
        bounds.maximum_z = std::max(bounds.maximum_z, vertex.z);
    }
    if (geometry.minimum_x != bounds.minimum_x ||
        geometry.maximum_x != bounds.maximum_x ||
        geometry.minimum_y != bounds.minimum_y ||
        geometry.maximum_y != bounds.maximum_y ||
        geometry.minimum_z != bounds.minimum_z ||
        geometry.maximum_z != bounds.maximum_z) {
        throw SceneGeometryError(
            "A source geometry merge input has incorrect bounds");
    }
    return bounds;
}

} // namespace

SceneGeometryV1 build_scene_geometry_v1(
    const GifGsDecodeResultV1& decoded) {
    if (decoded.vertices.size() >=
        static_cast<std::size_t>(kMissingVertexIndex)) {
        throw SceneGeometryError(
            "The decoded GS stream has too many vertices for a 32-bit index buffer");
    }

    SceneGeometryV1 result;
    std::vector<std::uint32_t> remapped_indices(
        decoded.vertices.size(), kMissingVertexIndex);
    bool has_bounds = false;

    const auto add_vertex = [&](const std::uint64_t source_index) {
        if (source_index >= decoded.vertices.size()) {
            throw SceneGeometryError(
                "An emitted GS primitive references a missing vertex");
        }
        const auto source_offset = static_cast<std::size_t>(source_index);
        if (remapped_indices[source_offset] != kMissingVertexIndex) {
            return remapped_indices[source_offset];
        }
        if (result.vertices.size() >=
            static_cast<std::size_t>(kMissingVertexIndex)) {
            throw SceneGeometryError(
                "The runtime geometry exceeds the 32-bit vertex-index limit");
        }

        const auto& source = decoded.vertices[source_offset];
        if (!source.x || !source.y) {
            throw SceneGeometryError(
                "An emitted GS primitive has indeterminate XY coordinates");
        }

        auto x_fixed = static_cast<std::int32_t>(*source.x);
        auto y_fixed = static_cast<std::int32_t>(*source.y);
        bool has_x_offset = false;
        bool has_y_offset = false;
        if (source.raster.context.xy_offset) {
            const auto& offset = *source.raster.context.xy_offset;
            if (offset.ofx) {
                x_fixed -= static_cast<std::int32_t>(*offset.ofx);
                has_x_offset = true;
            }
            if (offset.ofy) {
                y_fixed -= static_cast<std::int32_t>(*offset.ofy);
                has_y_offset = true;
            }
        }
        if (!has_x_offset || !has_y_offset) {
            ++result.vertices_without_complete_xy_offset;
        }

        SceneVertexV1 vertex;
        vertex.x = static_cast<float>(x_fixed) / 16.0F;
        vertex.y = static_cast<float>(y_fixed) / 16.0F;
        bool used_fallback_color = false;
        vertex.rgba = pack_color(source, used_fallback_color);
        if (used_fallback_color) {
            ++result.vertices_with_fallback_color;
        }

        const auto runtime_index =
            static_cast<std::uint32_t>(result.vertices.size());
        result.vertices.push_back(vertex);
        remapped_indices[source_offset] = runtime_index;

        if (!has_bounds) {
            result.minimum_x = vertex.x;
            result.maximum_x = vertex.x;
            result.minimum_y = vertex.y;
            result.maximum_y = vertex.y;
            has_bounds = true;
        } else {
            result.minimum_x = std::min(result.minimum_x, vertex.x);
            result.maximum_x = std::max(result.maximum_x, vertex.x);
            result.minimum_y = std::min(result.minimum_y, vertex.y);
            result.maximum_y = std::max(result.maximum_y, vertex.y);
        }
        return runtime_index;
    };

    for (const auto& primitive : decoded.primitives) {
        if (primitive.emission != GifGsPrimitiveEmissionV1::emitted) {
            continue;
        }
        if (!is_triangle_topology(primitive.topology) ||
            primitive.vertex_count != 3U) {
            ++result.skipped_non_triangle_count;
            continue;
        }
        for (std::size_t index = 0U; index < 3U; ++index) {
            result.triangle_indices.push_back(
                add_vertex(primitive.vertex_indices[index]));
        }
        ++result.emitted_triangle_count;
    }

    if (result.triangle_indices.empty() || !has_bounds) {
        throw SceneGeometryError(
            "The decoded GS stream contains no emitted triangle geometry");
    }
    return result;
}

SceneGeometry3dV1 build_scene_geometry_3d_v1(
    const SceneBlockSourceGeometryV1& source,
    const GifGsDecodeResultV1& decoded) {
    if (source.vertices.size() != decoded.vertices.size()) {
        throw SceneGeometryError(
            "The recovered source vertices are not one-to-one with the GS vertices");
    }
    if (source.vertices.size() >=
        static_cast<std::size_t>(kMissingVertexIndex)) {
        throw SceneGeometryError(
            "The recovered source geometry has too many vertices for a 32-bit index buffer");
    }
    for (std::size_t index = 0U; index < source.vertices.size(); ++index) {
        if (source.vertices[index].gs_vertex_index != index) {
            throw SceneGeometryError(
                "The recovered source vertex order does not match the GS stream");
        }
    }

    SceneGeometry3dV1 result;
    std::vector<std::uint32_t> remapped_indices(
        source.vertices.size(), kMissingVertexIndex);
    bool has_bounds = false;

    const auto add_vertex = [&](const std::uint64_t source_index) {
        if (source_index >= source.vertices.size()) {
            throw SceneGeometryError(
                "An emitted GS primitive references a missing source vertex");
        }
        const auto source_offset = static_cast<std::size_t>(source_index);
        if (remapped_indices[source_offset] != kMissingVertexIndex) {
            return remapped_indices[source_offset];
        }
        if (result.vertices.size() >=
            static_cast<std::size_t>(kMissingVertexIndex)) {
            throw SceneGeometryError(
                "The runtime source geometry exceeds the 32-bit vertex-index limit");
        }

        const auto& recovered = source.vertices[source_offset];
        const SceneVertex3dV1 vertex{
            static_cast<float>(recovered.x),
            static_cast<float>(recovered.y),
            static_cast<float>(recovered.z),
            pack_color(recovered.rgba),
        };
        const auto runtime_index =
            static_cast<std::uint32_t>(result.vertices.size());
        result.vertices.push_back(vertex);
        remapped_indices[source_offset] = runtime_index;

        if (!has_bounds) {
            result.minimum_x = vertex.x;
            result.maximum_x = vertex.x;
            result.minimum_y = vertex.y;
            result.maximum_y = vertex.y;
            result.minimum_z = vertex.z;
            result.maximum_z = vertex.z;
            has_bounds = true;
        } else {
            result.minimum_x = std::min(result.minimum_x, vertex.x);
            result.maximum_x = std::max(result.maximum_x, vertex.x);
            result.minimum_y = std::min(result.minimum_y, vertex.y);
            result.maximum_y = std::max(result.maximum_y, vertex.y);
            result.minimum_z = std::min(result.minimum_z, vertex.z);
            result.maximum_z = std::max(result.maximum_z, vertex.z);
        }
        return runtime_index;
    };

    for (const auto& primitive : decoded.primitives) {
        if (primitive.emission != GifGsPrimitiveEmissionV1::emitted) {
            continue;
        }
        if (!is_triangle_topology(primitive.topology) ||
            primitive.vertex_count != 3U) {
            ++result.skipped_non_triangle_count;
            continue;
        }
        for (std::size_t index = 0U; index < 3U; ++index) {
            result.triangle_indices.push_back(
                add_vertex(primitive.vertex_indices[index]));
        }
        ++result.emitted_triangle_count;
    }

    if (result.triangle_indices.empty() || !has_bounds) {
        throw SceneGeometryError(
            "The decoded GS stream contains no emitted source-space triangle geometry");
    }
    return result;
}

SceneGeometryV1 merge_scene_geometries_v1(
    const std::span<const SceneGeometryV1> geometries,
    const SceneGeometryMergeLimitsV1 limits) {
    validate_merge_limits(limits);
    if (geometries.empty()) {
        throw SceneGeometryError(
            "No raster geometries were supplied for merging");
    }

    std::uint64_t total_vertices = 0U;
    std::uint64_t total_indices = 0U;
    std::uint64_t emitted_triangles = 0U;
    std::uint64_t skipped_non_triangles = 0U;
    std::uint64_t vertices_without_xy_offset = 0U;
    std::uint64_t vertices_with_fallback_color = 0U;
    Bounds2d aggregate_bounds{};
    bool has_bounds = false;

    for (const auto& geometry : geometries) {
        total_vertices = checked_counter_add(
            total_vertices,
            static_cast<std::uint64_t>(geometry.vertices.size()),
            "The merged raster vertex count overflows");
        total_indices = checked_counter_add(
            total_indices,
            static_cast<std::uint64_t>(geometry.triangle_indices.size()),
            "The merged raster index count overflows");
        if (total_vertices > limits.max_vertices) {
            throw SceneGeometryError(
                "The merged raster geometry exceeds its vertex limit");
        }
        if (total_indices > limits.max_triangle_indices) {
            throw SceneGeometryError(
                "The merged raster geometry exceeds its index limit");
        }
        if (total_vertices >= kMissingVertexIndex) {
            throw SceneGeometryError(
                "The merged raster geometry exceeds the 32-bit vertex-index range");
        }
        const auto bounds = validate_geometry(geometry);

        emitted_triangles = checked_counter_add(
            emitted_triangles,
            geometry.emitted_triangle_count,
            "The merged raster emitted-triangle count overflows");
        skipped_non_triangles = checked_counter_add(
            skipped_non_triangles,
            geometry.skipped_non_triangle_count,
            "The merged raster skipped-primitive count overflows");
        vertices_without_xy_offset = checked_counter_add(
            vertices_without_xy_offset,
            geometry.vertices_without_complete_xy_offset,
            "The merged raster missing-XYOFFSET count overflows");
        vertices_with_fallback_color = checked_counter_add(
            vertices_with_fallback_color,
            geometry.vertices_with_fallback_color,
            "The merged raster fallback-color count overflows");

        if (!has_bounds) {
            aggregate_bounds = bounds;
            has_bounds = true;
        } else {
            aggregate_bounds.minimum_x = std::min(
                aggregate_bounds.minimum_x, bounds.minimum_x);
            aggregate_bounds.maximum_x = std::max(
                aggregate_bounds.maximum_x, bounds.maximum_x);
            aggregate_bounds.minimum_y = std::min(
                aggregate_bounds.minimum_y, bounds.minimum_y);
            aggregate_bounds.maximum_y = std::max(
                aggregate_bounds.maximum_y, bounds.maximum_y);
        }
    }

    SceneGeometryV1 result;
    if (total_vertices > result.vertices.max_size() ||
        total_indices > result.triangle_indices.max_size()) {
        throw SceneGeometryError(
            "The merged raster geometry exceeds a host container limit");
    }
    result.vertices.reserve(static_cast<std::size_t>(total_vertices));
    result.triangle_indices.reserve(static_cast<std::size_t>(total_indices));
    for (const auto& geometry : geometries) {
        const auto vertex_offset =
            static_cast<std::uint64_t>(result.vertices.size());
        result.vertices.insert(
            result.vertices.end(), geometry.vertices.begin(), geometry.vertices.end());
        for (const auto index : geometry.triangle_indices) {
            const auto shifted_index = checked_counter_add(
                vertex_offset,
                index,
                "A merged raster vertex-index offset overflows");
            if (shifted_index >= kMissingVertexIndex) {
                throw SceneGeometryError(
                    "A merged raster vertex index exceeds the 32-bit range");
            }
            result.triangle_indices.push_back(
                static_cast<std::uint32_t>(shifted_index));
        }
    }

    result.minimum_x = aggregate_bounds.minimum_x;
    result.maximum_x = aggregate_bounds.maximum_x;
    result.minimum_y = aggregate_bounds.minimum_y;
    result.maximum_y = aggregate_bounds.maximum_y;
    result.emitted_triangle_count = emitted_triangles;
    result.skipped_non_triangle_count = skipped_non_triangles;
    result.vertices_without_complete_xy_offset = vertices_without_xy_offset;
    result.vertices_with_fallback_color = vertices_with_fallback_color;
    return result;
}

SceneGeometry3dV1 merge_scene_geometries_3d_v1(
    const std::span<const SceneGeometry3dV1> geometries,
    const SceneGeometryMergeLimitsV1 limits) {
    validate_merge_limits(limits);
    if (geometries.empty()) {
        throw SceneGeometryError(
            "No source geometries were supplied for merging");
    }

    std::uint64_t total_vertices = 0U;
    std::uint64_t total_indices = 0U;
    std::uint64_t emitted_triangles = 0U;
    std::uint64_t skipped_non_triangles = 0U;
    Bounds3d aggregate_bounds{};
    bool has_bounds = false;

    for (const auto& geometry : geometries) {
        total_vertices = checked_counter_add(
            total_vertices,
            static_cast<std::uint64_t>(geometry.vertices.size()),
            "The merged source vertex count overflows");
        total_indices = checked_counter_add(
            total_indices,
            static_cast<std::uint64_t>(geometry.triangle_indices.size()),
            "The merged source index count overflows");
        if (total_vertices > limits.max_vertices) {
            throw SceneGeometryError(
                "The merged source geometry exceeds its vertex limit");
        }
        if (total_indices > limits.max_triangle_indices) {
            throw SceneGeometryError(
                "The merged source geometry exceeds its index limit");
        }
        if (total_vertices >= kMissingVertexIndex) {
            throw SceneGeometryError(
                "The merged source geometry exceeds the 32-bit vertex-index range");
        }
        const auto bounds = validate_geometry(geometry);

        emitted_triangles = checked_counter_add(
            emitted_triangles,
            geometry.emitted_triangle_count,
            "The merged source emitted-triangle count overflows");
        skipped_non_triangles = checked_counter_add(
            skipped_non_triangles,
            geometry.skipped_non_triangle_count,
            "The merged source skipped-primitive count overflows");

        if (!has_bounds) {
            aggregate_bounds = bounds;
            has_bounds = true;
        } else {
            aggregate_bounds.minimum_x = std::min(
                aggregate_bounds.minimum_x, bounds.minimum_x);
            aggregate_bounds.maximum_x = std::max(
                aggregate_bounds.maximum_x, bounds.maximum_x);
            aggregate_bounds.minimum_y = std::min(
                aggregate_bounds.minimum_y, bounds.minimum_y);
            aggregate_bounds.maximum_y = std::max(
                aggregate_bounds.maximum_y, bounds.maximum_y);
            aggregate_bounds.minimum_z = std::min(
                aggregate_bounds.minimum_z, bounds.minimum_z);
            aggregate_bounds.maximum_z = std::max(
                aggregate_bounds.maximum_z, bounds.maximum_z);
        }
    }

    SceneGeometry3dV1 result;
    if (total_vertices > result.vertices.max_size() ||
        total_indices > result.triangle_indices.max_size()) {
        throw SceneGeometryError(
            "The merged source geometry exceeds a host container limit");
    }
    result.vertices.reserve(static_cast<std::size_t>(total_vertices));
    result.triangle_indices.reserve(static_cast<std::size_t>(total_indices));
    for (const auto& geometry : geometries) {
        const auto vertex_offset =
            static_cast<std::uint64_t>(result.vertices.size());
        result.vertices.insert(
            result.vertices.end(), geometry.vertices.begin(), geometry.vertices.end());
        for (const auto index : geometry.triangle_indices) {
            const auto shifted_index = checked_counter_add(
                vertex_offset,
                index,
                "A merged source vertex-index offset overflows");
            if (shifted_index >= kMissingVertexIndex) {
                throw SceneGeometryError(
                    "A merged source vertex index exceeds the 32-bit range");
            }
            result.triangle_indices.push_back(
                static_cast<std::uint32_t>(shifted_index));
        }
    }

    result.minimum_x = aggregate_bounds.minimum_x;
    result.maximum_x = aggregate_bounds.maximum_x;
    result.minimum_y = aggregate_bounds.minimum_y;
    result.maximum_y = aggregate_bounds.maximum_y;
    result.minimum_z = aggregate_bounds.minimum_z;
    result.maximum_z = aggregate_bounds.maximum_z;
    result.emitted_triangle_count = emitted_triangles;
    result.skipped_non_triangle_count = skipped_non_triangles;
    return result;
}

} // namespace openrc::runtime
