#include "render_scene_d3d_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace openrc::runtime {
namespace {

// D3D11's largest Texture2D dimension. A lower feature level may impose a
// smaller device-specific limit, which CreateTexture2D reports normally.
constexpr std::uint32_t kMaximumD3d11TextureDimension = 16'384U;
constexpr std::uint32_t kMaximumD3d11TextureMipCount = 15U;
constexpr std::uint64_t kMaximumD3d11BufferBytes =
    std::numeric_limits<std::uint32_t>::max();
constexpr std::uint64_t kProjectedVertexBytes = 28U;
constexpr double kMaximumCameraDistanceFactor = 128.0;

constexpr RenderSceneLimitsV1 kD3d11SceneValidationLimits{
    std::numeric_limits<std::uint32_t>::max(),
    kMaximumD3d11TextureMipCount,
    std::numeric_limits<std::uint32_t>::max(),
    std::numeric_limits<std::uint32_t>::max(),
    std::numeric_limits<std::uint32_t>::max(),
    std::numeric_limits<std::uint32_t>::max(),
    std::numeric_limits<std::uint32_t>::max(),
    kMaximumD3d11TextureDimension,
    kMaximumD3d11TextureDimension,
    std::numeric_limits<std::uint64_t>::max(),
    std::numeric_limits<std::uint64_t>::max(),
    std::numeric_limits<std::uint64_t>::max(),
    std::numeric_limits<std::uint64_t>::max(),
};

[[noreturn]] void fail(const std::string& message) {
    throw RenderSceneD3dDataError(message);
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string(description) + " overflows the host count domain");
    }
    return left + right;
}

[[nodiscard]] float affine_component(
    const RenderSceneAffine3x4V1& transform,
    const std::size_t row,
    const RenderSceneVertexV1& vertex) {
    const auto offset = row * 4U;
    const auto value =
        transform.values[offset] * vertex.x +
        transform.values[offset + 1U] * vertex.y +
        transform.values[offset + 2U] * vertex.z +
        transform.values[offset + 3U];
    if (!std::isfinite(value)) {
        fail("A RenderSceneV1 affine instance produces a non-finite world position");
    }
    return value;
}

[[nodiscard]] double linear_determinant(
    const RenderSceneAffine3x4V1& transform) noexcept {
    const auto& value = transform.values;
    return
        static_cast<double>(value[0U]) *
            (static_cast<double>(value[5U]) * value[10U] -
             static_cast<double>(value[6U]) * value[9U]) -
        static_cast<double>(value[1U]) *
            (static_cast<double>(value[4U]) * value[10U] -
             static_cast<double>(value[6U]) * value[8U]) +
        static_cast<double>(value[2U]) *
            (static_cast<double>(value[4U]) * value[9U] -
             static_cast<double>(value[5U]) * value[8U]);
}

[[nodiscard]] std::array<float, 4U>
unpack_rgba8(const std::uint32_t rgba8) noexcept {
    return {
        static_cast<float>(rgba8 & UINT32_C(0xff)) / 255.0F,
        static_cast<float>((rgba8 >> 8U) & UINT32_C(0xff)) / 255.0F,
        static_cast<float>((rgba8 >> 16U) & UINT32_C(0xff)) / 255.0F,
        static_cast<float>((rgba8 >> 24U) & UINT32_C(0xff)) / 255.0F,
    };
}

} // namespace

RenderSceneD3dDataV1
build_render_scene_d3d_data_v1(const RenderSceneV1& scene,
                               const RenderSceneD3dDataLimitsV1 limits) {
    if (limits.max_flattened_vertices == 0U ||
        limits.max_flattened_triangle_indices == 0U ||
        limits.max_flattened_draws == 0U) {
        fail("RenderScene D3D staging limits must be non-zero");
    }
    try {
        validate_render_scene_v1(scene, kD3d11SceneValidationLimits);
    } catch (const RenderSceneError& error) {
        fail(std::string("Invalid RenderSceneV1 for D3D11: ") + error.what());
    }

    for (const auto& texture : scene.textures) {
        auto dimension = std::max(
            texture.mips.front().width, texture.mips.front().height);
        std::size_t maximum_mip_count = 1U;
        while (dimension > 1U) {
            dimension /= 2U;
            ++maximum_mip_count;
        }
        if (texture.mips.size() > maximum_mip_count) {
            fail("A RenderSceneV1 texture has more mip levels than D3D11 permits for its dimensions");
        }
    }

    RenderSceneD3dDataV1 result;
    result.materials.reserve(scene.materials.size());
    for (const auto& material : scene.materials) {
        result.materials.push_back(RenderSceneD3dMaterialV1{
            material.base_color_texture_id,
            unpack_rgba8(material.base_color_rgba8),
            material.use_vertex_color,
            material.double_sided,
            material.address_u,
            material.address_v,
            material.min_filter,
            material.mag_filter,
            material.mipmap_filter,
            material.alpha_mode == RenderSceneAlphaModeV1::mask
                ? static_cast<float>(material.alpha_cutoff_rgba8) / 255.0F
                : -1.0F,
            material.color_math,material.blend_mode,material.interpolation,
            material.depth_test,material.depth_write,
            material.texture_modulation_denominator,material.blend_denominator,
            material.alpha_failure,
        });
    }
    if (scene.instances.empty()) {
        return result;
    }

    std::uint64_t vertex_count = 0U;
    std::uint64_t index_count = 0U;
    std::uint64_t draw_count = 0U;
    for (const auto& instance : scene.instances) {
        const auto& mesh = scene.meshes[instance.mesh_id];
        vertex_count = checked_add(
            vertex_count, mesh.vertices.size(),
            "The flattened RenderSceneV1 vertex count");
        index_count = checked_add(
            index_count, mesh.triangle_indices.size(),
            "The flattened RenderSceneV1 index count");
        draw_count = checked_add(
            draw_count, mesh.draw_ranges.size(),
            "The flattened RenderSceneV1 draw count");
        if (vertex_count > limits.max_flattened_vertices) {
            fail("The flattened RenderSceneV1 vertex count exceeds the renderer allocation limit");
        }
        if (index_count > limits.max_flattened_triangle_indices) {
            fail("The flattened RenderSceneV1 index count exceeds the renderer allocation limit");
        }
        if (draw_count > limits.max_flattened_draws) {
            fail("The flattened RenderSceneV1 draw count exceeds the renderer allocation limit");
        }
    }

    if (vertex_count > kMaximumD3d11BufferBytes / kProjectedVertexBytes) {
        fail("The flattened RenderSceneV1 vertex buffer exceeds the D3D11 byte-size domain");
    }
    if (index_count >
        kMaximumD3d11BufferBytes / sizeof(std::uint32_t)) {
        fail("The flattened RenderSceneV1 index buffer exceeds the D3D11 byte-size domain");
    }
    if (draw_count > result.draws.max_size()) {
        fail("The flattened RenderSceneV1 draw list exceeds the host container");
    }

    result.vertices.reserve(static_cast<std::size_t>(vertex_count));
    result.vertex_projection_flags.reserve(static_cast<std::size_t>(vertex_count));
    result.triangle_indices.reserve(static_cast<std::size_t>(index_count));
    result.draws.reserve(static_cast<std::size_t>(draw_count));

    bool has_bounds = false;
    for (const auto& instance : scene.instances) {
        const auto& mesh = scene.meshes[instance.mesh_id];
        const auto vertex_base =
            static_cast<std::uint32_t>(result.vertices.size());
        const auto index_base =
            static_cast<std::uint32_t>(result.triangle_indices.size());

        for (const auto& vertex : mesh.vertices) {
            RenderSceneD3dVertexV1 flattened{
                affine_component(instance.local_to_world, 0U, vertex),
                affine_component(instance.local_to_world, 1U, vertex),
                affine_component(instance.local_to_world, 2U, vertex),
                vertex.u,
                vertex.v,
                vertex.rgba8,
            };
            result.vertices.push_back(flattened);
            result.vertex_projection_flags.push_back(static_cast<std::uint8_t>(
                (instance.camera_relative?1U:0U)|(instance.project_to_far_plane?2U:0U)));

            if (!has_bounds) {
                result.minimum_x = flattened.x;
                result.maximum_x = flattened.x;
                result.minimum_y = flattened.y;
                result.maximum_y = flattened.y;
                result.minimum_z = flattened.z;
                result.maximum_z = flattened.z;
                has_bounds = true;
            } else {
                result.minimum_x = std::min(result.minimum_x, flattened.x);
                result.maximum_x = std::max(result.maximum_x, flattened.x);
                result.minimum_y = std::min(result.minimum_y, flattened.y);
                result.maximum_y = std::max(result.maximum_y, flattened.y);
                result.minimum_z = std::min(result.minimum_z, flattened.z);
                result.maximum_z = std::max(result.maximum_z, flattened.z);
            }
        }

        const auto append_index = [&result, vertex_base](
                                      const std::uint32_t index) {
            if (index > std::numeric_limits<std::uint32_t>::max() -
                            vertex_base) {
                fail("A flattened RenderSceneV1 index exceeds 32 bits");
            }
            result.triangle_indices.push_back(vertex_base + index);
        };
        const auto reverses_orientation =
            linear_determinant(instance.local_to_world) < 0.0;
        for (std::size_t first = 0U;
             first < mesh.triangle_indices.size(); first += 3U) {
            append_index(mesh.triangle_indices[first]);
            if (reverses_orientation) {
                append_index(mesh.triangle_indices[first + 2U]);
                append_index(mesh.triangle_indices[first + 1U]);
            } else {
                append_index(mesh.triangle_indices[first + 1U]);
                append_index(mesh.triangle_indices[first + 2U]);
            }
        }

        for (const auto& draw : mesh.draw_ranges) {
            if (draw.first_index >
                    std::numeric_limits<std::uint32_t>::max() - index_base ||
                draw.index_count >
                    std::numeric_limits<std::uint32_t>::max()) {
                fail("A flattened RenderSceneV1 draw exceeds the D3D11 draw domain");
            }
            result.draws.push_back(RenderSceneD3dDrawV1{
                draw.material_id,
                index_base + static_cast<std::uint32_t>(draw.first_index),
                static_cast<std::uint32_t>(draw.index_count),
                instance.id,
            });
        }
    }

    const std::array<double, 3U> camera_target{
        (static_cast<double>(result.minimum_x) + result.maximum_x) * 0.5,
        (static_cast<double>(result.minimum_y) + result.maximum_y) * 0.5,
        (static_cast<double>(result.minimum_z) + result.maximum_z) * 0.5,
    };
    double squared_radius = 0.0;
    for (const auto& vertex : result.vertices) {
        const auto relative_x = static_cast<double>(vertex.x) - camera_target[0U];
        const auto relative_y = static_cast<double>(vertex.y) - camera_target[1U];
        const auto relative_z = static_cast<double>(vertex.z) - camera_target[2U];
        squared_radius = std::max(
            squared_radius,
            relative_x * relative_x + relative_y * relative_y +
                relative_z * relative_z);
    }
    auto camera_radius = std::sqrt(squared_radius);
    if (!(camera_radius > 0.0)) {
        camera_radius = 1.0;
    }
    const auto maximum_float =
        static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(camera_radius) || camera_radius > maximum_float) {
        fail("The RenderSceneV1 radius exceeds the renderer camera numeric domain");
    }
    for (const auto component : camera_target) {
        const auto magnitude = std::abs(component);
        if (!std::isfinite(component) || magnitude > maximum_float ||
            camera_radius * kMaximumCameraDistanceFactor >
                maximum_float - magnitude) {
            fail("The RenderSceneV1 bounds exceed the renderer camera numeric domain");
        }
    }
    result.camera_target = {
        static_cast<float>(camera_target[0U]),
        static_cast<float>(camera_target[1U]),
        static_cast<float>(camera_target[2U]),
    };
    const auto renderer_camera_radius = static_cast<float>(camera_radius);
    result.camera_radius =
        std::isfinite(renderer_camera_radius) && renderer_camera_radius > 0.0F
            ? renderer_camera_radius
            : 1.0F;

    return result;
}

} // namespace openrc::runtime
