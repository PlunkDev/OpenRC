#include "scene_geometry.hpp"

#include <algorithm>
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

} // namespace openrc::runtime
