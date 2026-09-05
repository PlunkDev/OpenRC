#include "d3d11_renderer.hpp"

#include "moby_scene_geometry.hpp"
#include "render_scene_d3d_data.hpp"
#include "scene_geometry.hpp"

#include "generated/render_scene_ps_dxbc.hpp"
#include "generated/source_textured_ps_dxbc.hpp"
#include "generated/source_textured_vs_dxbc.hpp"
#include "generated/wireframe_ps_dxbc.hpp"
#include "generated/wireframe_vs_dxbc.hpp"

#include "openrc/actor_pose.hpp"
#include "openrc/render_scene.hpp"
#include "openrc/rac_level_moby_texture.hpp"
#include "openrc/runtime_level_content.hpp"
#include "openrc/third_person_camera.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace openrc::runtime {
namespace {

using Microsoft::WRL::ComPtr;

struct alignas(16) FitConstants {
    std::array<float, 2U> scale{};
    std::array<float, 2U> offset{};
};

static_assert(sizeof(FitConstants) == 16U);

struct ProjectedSourceVertex {
    std::array<float, 4U> clip_position{};
    std::uint32_t rgba = 0xffffffffU;
    float u = 0.0F;
    float v = 0.0F;
};

static_assert(sizeof(ProjectedSourceVertex) == 28U);

struct alignas(16) RenderSceneMaterialConstants {
    std::array<float, 4U> base_color{};
    float use_vertex_color = 0.0F;
    float alpha_cutoff = -1.0F;
    std::array<float, 2U> padding{};
};

static_assert(sizeof(RenderSceneMaterialConstants) == 32U);

struct GpuSourceMaterialBatch {
    UINT start_index = 0U;
    UINT index_count = 0U;
    std::optional<std::uint32_t> texture_index;
};

struct GpuSourceTextureRegion {
    UINT start_index = 0U;
    UINT index_count = 0U;
    std::vector<GpuSourceMaterialBatch> material_batches;
    std::vector<ComPtr<ID3D11ShaderResourceView>> texture_views;
};

struct RenderSceneSamplerKey {
    RenderSceneAddressModeV1 address_u = RenderSceneAddressModeV1::repeat;
    RenderSceneAddressModeV1 address_v = RenderSceneAddressModeV1::repeat;
    RenderSceneFilterV1 min_filter = RenderSceneFilterV1::linear;
    RenderSceneFilterV1 mag_filter = RenderSceneFilterV1::linear;
    RenderSceneMipmapFilterV1 mipmap_filter =
        RenderSceneMipmapFilterV1::none;

    [[nodiscard]] bool
    operator==(const RenderSceneSamplerKey&) const = default;
};

struct GpuRenderSceneSampler {
    RenderSceneSamplerKey key;
    ComPtr<ID3D11SamplerState> state;
};

constexpr float kPi = 3.14159265358979323846F;
constexpr float kDefaultVerticalFov = kPi / 3.0F;
constexpr float kMaximumPitch = 89.0F * kPi / 180.0F;
constexpr std::uint32_t kGameplayProxyRingSegments = 12U;
constexpr std::uint32_t kGameplayProxyVertexCount =
    2U + 2U * kGameplayProxyRingSegments + 3U;
constexpr std::uint32_t kGameplayProxyIndexCount =
    12U * kGameplayProxyRingSegments + 3U;
constexpr std::uint32_t kMaximumD3d11TextureDimension = 16'384U;
constexpr std::size_t kMaximumD3d11TextureMipCount = 15U;

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    [[nodiscard]] bool operator==(const Vec3&) const = default;
};

struct GameplayPresentation {
    Vec3 eye{};
    Vec3 target{};
    Vec3 up{};
    float vertical_field_of_view_radians = 0.0F;
    float aspect_ratio = 0.0F;
    float near_plane_distance = 0.0F;
    float far_plane_distance = 0.0F;
    Vec3 feet_position{};
    float facing_yaw_radians = 0.0F;
    float capsule_radius = 0.0F;
    float capsule_height = 0.0F;

    [[nodiscard]] bool
    operator==(const GameplayPresentation&) const = default;
};

[[nodiscard]] std::array<float, 4U>
unpack_rgba8(const std::uint32_t rgba8) noexcept {
    return {
        static_cast<float>(rgba8 & UINT32_C(0xff)) / 255.0F,
        static_cast<float>((rgba8 >> 8U) & UINT32_C(0xff)) / 255.0F,
        static_cast<float>((rgba8 >> 16U) & UINT32_C(0xff)) / 255.0F,
        static_cast<float>((rgba8 >> 24U) & UINT32_C(0xff)) / 255.0F,
    };
}

[[nodiscard]] RenderSceneD3dMaterialV1
make_gpu_material(const RenderSceneMaterialV1& material) {
    return RenderSceneD3dMaterialV1{
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
    };
}

[[nodiscard]] ActorAffineTransformV1 compose_actor_transform(
    const ActorAffineTransformV1& parent,
    const ActorAffineTransformV1& local) {
    ActorAffineTransformV1 result;
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            double value = 0.0;
            for (std::size_t inner = 0U; inner < 3U; ++inner) {
                value +=
                    static_cast<double>(parent.values[row * 4U + inner]) *
                    local.values[inner * 4U + column];
            }
            if (!std::isfinite(value) ||
                std::abs(value) > std::numeric_limits<float>::max()) {
                throw std::invalid_argument(
                    "The gameplay actor transform exceeds the renderer numeric domain");
            }
            result.values[row * 4U + column] = static_cast<float>(value);
        }
        double translation = parent.values[row * 4U + 3U];
        for (std::size_t inner = 0U; inner < 3U; ++inner) {
            translation +=
                static_cast<double>(parent.values[row * 4U + inner]) *
                local.values[inner * 4U + 3U];
        }
        if (!std::isfinite(translation) ||
            std::abs(translation) > std::numeric_limits<float>::max()) {
            throw std::invalid_argument(
                "The gameplay actor translation exceeds the renderer numeric domain");
        }
        result.values[row * 4U + 3U] = static_cast<float>(translation);
    }
    return result;
}

[[nodiscard]] double actor_linear_determinant(
    const ActorAffineTransformV1& transform) noexcept {
    const auto& value = transform.values;
    return static_cast<double>(value[0U]) *
               (static_cast<double>(value[5U]) * value[10U] -
                static_cast<double>(value[6U]) * value[9U]) -
           static_cast<double>(value[1U]) *
               (static_cast<double>(value[4U]) * value[10U] -
                static_cast<double>(value[6U]) * value[8U]) +
           static_cast<double>(value[2U]) *
               (static_cast<double>(value[4U]) * value[9U] -
                static_cast<double>(value[5U]) * value[8U]);
}

[[nodiscard]] bool checked_actor_reverses_orientation(
    const ActorAffineTransformV1& transform) {
    if (!std::ranges::all_of(transform.values, [](const float value) {
            return std::isfinite(value);
        })) {
        throw std::invalid_argument(
            "The D3D11 gameplay actor model-to-entity transform is non-finite");
    }
    const auto determinant = actor_linear_determinant(transform);
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <
            game::kRuntimePlayerActorPoseLimitsV1
                .minimum_absolute_linear_determinant) {
        throw std::invalid_argument(
            "The D3D11 gameplay actor model-to-entity transform is singular or ill-conditioned");
    }
    return determinant < 0.0;
}

[[nodiscard]] bool valid_actor_texture_color_space(
    const RenderSceneTextureColorSpaceV1 value) noexcept {
    return value == RenderSceneTextureColorSpaceV1::linear ||
           value == RenderSceneTextureColorSpaceV1::srgb;
}

[[nodiscard]] bool valid_actor_address_mode(
    const RenderSceneAddressModeV1 value) noexcept {
    return value == RenderSceneAddressModeV1::repeat ||
           value == RenderSceneAddressModeV1::clamp_to_edge;
}

[[nodiscard]] bool valid_actor_filter(
    const RenderSceneFilterV1 value) noexcept {
    return value == RenderSceneFilterV1::nearest ||
           value == RenderSceneFilterV1::linear;
}

[[nodiscard]] bool valid_actor_mipmap_filter(
    const RenderSceneMipmapFilterV1 value) noexcept {
    return value == RenderSceneMipmapFilterV1::none ||
           value == RenderSceneMipmapFilterV1::nearest ||
           value == RenderSceneMipmapFilterV1::linear;
}

[[nodiscard]] bool valid_actor_alpha_mode(
    const RenderSceneAlphaModeV1 value) noexcept {
    return value == RenderSceneAlphaModeV1::opaque ||
           value == RenderSceneAlphaModeV1::mask;
}

void preflight_gameplay_actor_texture(
    const RenderSceneTextureV1& texture,
    const std::uint32_t expected_id) {
    if (texture.id != expected_id || texture.mips.empty() ||
        texture.mips.size() > kMaximumD3d11TextureMipCount ||
        !valid_actor_texture_color_space(texture.color_space)) {
        throw std::invalid_argument(
            "The D3D11 gameplay actor has an invalid texture envelope");
    }

    auto expected_width = texture.mips.front().width;
    auto expected_height = texture.mips.front().height;
    for (std::size_t level = 0U; level < texture.mips.size(); ++level) {
        const auto& mip = texture.mips[level];
        if (mip.width == 0U || mip.height == 0U ||
            mip.width > kMaximumD3d11TextureDimension ||
            mip.height > kMaximumD3d11TextureDimension ||
            mip.width != expected_width || mip.height != expected_height) {
            throw std::invalid_argument(
                "The D3D11 gameplay actor texture has invalid mip dimensions");
        }
        const auto rgba_bytes =
            static_cast<std::uint64_t>(mip.width) * mip.height * 4U;
        if (rgba_bytes !=
                static_cast<std::uint64_t>(mip.rgba8.size()) ||
            rgba_bytes > std::numeric_limits<UINT>::max()) {
            throw std::invalid_argument(
                "The D3D11 gameplay actor texture has an invalid mip payload");
        }
        if (mip.width == 1U && mip.height == 1U &&
            level + 1U != texture.mips.size()) {
            throw std::invalid_argument(
                "The D3D11 gameplay actor texture continues past its 1x1 mip");
        }
        expected_width = std::max(UINT32_C(1), mip.width / 2U);
        expected_height = std::max(UINT32_C(1), mip.height / 2U);
    }
}

struct CameraProjection {
    Vec3 eye{};
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
    float tangent_half_vertical = 0.0F;
    float tangent_half_horizontal = 0.0F;
    float depth_scale = 0.0F;
    float depth_offset = 0.0F;
};

[[nodiscard]] Vec3 operator+(const Vec3 a, const Vec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] Vec3 operator-(const Vec3 a, const Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3 operator*(const Vec3 value, const float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] float dot(const Vec3 a, const Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Vec3 cross(const Vec3 a, const Vec3 b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] Vec3 normalized(const Vec3 value) noexcept {
    const auto length_squared =
        static_cast<double>(value.x) * value.x +
        static_cast<double>(value.y) * value.y +
        static_cast<double>(value.z) * value.z;
    if (!(length_squared > 0.0) || !std::isfinite(length_squared)) {
        return {};
    }
    return value * static_cast<float>(1.0 / std::sqrt(length_squared));
}

[[nodiscard]] float squared_length(const Vec3 value) noexcept {
    return dot(value, value);
}

[[nodiscard]] Vec3 as_vec3(const SceneVertex3dV1& vertex) noexcept {
    return {vertex.x, vertex.y, vertex.z};
}

[[nodiscard]] GameplayPresentation checked_gameplay_presentation(
    const game::ThirdPersonCameraViewV1& camera,
    const CollisionVectorV1& feet_position,
    const double facing_yaw_radians,
    const double capsule_radius,
    const double capsule_height) {
    constexpr double kMaximumGameplayCoordinate =
        static_cast<double>(std::numeric_limits<float>::max()) / 16.0;
    const std::array values{
        camera.eye.x,
        camera.eye.y,
        camera.eye.z,
        camera.target.x,
        camera.target.y,
        camera.target.z,
        camera.up.x,
        camera.up.y,
        camera.up.z,
        camera.vertical_field_of_view_radians,
        camera.aspect_ratio,
        camera.near_plane_distance,
        camera.far_plane_distance,
        feet_position.x,
        feet_position.y,
        feet_position.z,
        facing_yaw_radians,
        capsule_radius,
        capsule_height,
    };
    if (!std::ranges::all_of(values, [](const double value) {
            return std::isfinite(value) &&
                   std::abs(value) <= kMaximumGameplayCoordinate;
        })) {
        throw std::invalid_argument(
            "The gameplay presentation contains a non-finite or out-of-range value");
    }
    if (!(camera.vertical_field_of_view_radians > 0.0) ||
        !(camera.vertical_field_of_view_radians <
          static_cast<double>(kPi)) ||
        !(camera.aspect_ratio > 0.0) ||
        !(camera.near_plane_distance > 0.0) ||
        !(camera.far_plane_distance > camera.near_plane_distance) ||
        !(capsule_radius > 0.0) ||
        capsule_height < capsule_radius * 2.0) {
        throw std::invalid_argument(
            "The gameplay presentation has an invalid projection or player envelope");
    }

    const Vec3 eye{static_cast<float>(camera.eye.x),
                   static_cast<float>(camera.eye.y),
                   static_cast<float>(camera.eye.z)};
    const Vec3 target{static_cast<float>(camera.target.x),
                      static_cast<float>(camera.target.y),
                      static_cast<float>(camera.target.z)};
    const Vec3 up{static_cast<float>(camera.up.x),
                  static_cast<float>(camera.up.y),
                  static_cast<float>(camera.up.z)};
    const auto forward = normalized(target - eye);
    const auto right = normalized(cross(forward, up));
    if (!(squared_length(forward) > 0.99F) ||
        !(squared_length(right) > 0.99F)) {
        throw std::invalid_argument(
            "The gameplay camera has a degenerate view basis");
    }

    return GameplayPresentation{
        eye,
        target,
        up,
        static_cast<float>(camera.vertical_field_of_view_radians),
        static_cast<float>(camera.aspect_ratio),
        static_cast<float>(camera.near_plane_distance),
        static_cast<float>(camera.far_plane_distance),
        Vec3{static_cast<float>(feet_position.x),
             static_cast<float>(feet_position.y),
             static_cast<float>(feet_position.z)},
        static_cast<float>(std::remainder(
            facing_yaw_radians, 2.0 * static_cast<double>(kPi))),
        static_cast<float>(capsule_radius),
        static_cast<float>(capsule_height),
    };
}

[[nodiscard]] std::string hresult_message(
    const char* const operation,
    const HRESULT result) {
    std::ostringstream stream;
    stream << operation << " failed (HRESULT 0x" << std::hex
           << std::uppercase << std::setw(8) << std::setfill('0')
           << static_cast<std::uint32_t>(result) << ')';
    return stream.str();
}

void require_success(const HRESULT result, const char* const operation) {
    if (FAILED(result)) {
        throw std::runtime_error(hresult_message(operation, result));
    }
}

[[nodiscard]] UINT checked_buffer_size(
    const std::size_t item_count,
    const std::size_t item_size,
    const char* const description) {
    if (item_count == 0U || item_size == 0U ||
        item_count >
            static_cast<std::size_t>(std::numeric_limits<UINT>::max()) /
                item_size) {
        throw std::runtime_error(
            std::string(description) +
            " is empty or exceeds the D3D11 buffer-size limit");
    }
    return static_cast<UINT>(item_count * item_size);
}

[[nodiscard]] D3D11_TEXTURE_ADDRESS_MODE texture_address_mode(
    const RenderSceneAddressModeV1 mode) {
    switch (mode) {
    case RenderSceneAddressModeV1::repeat:
        return D3D11_TEXTURE_ADDRESS_WRAP;
    case RenderSceneAddressModeV1::clamp_to_edge:
        return D3D11_TEXTURE_ADDRESS_CLAMP;
    }
    throw std::invalid_argument("Unknown RenderSceneV1 texture address mode");
}

[[nodiscard]] D3D11_FILTER texture_filter(
    const RenderSceneFilterV1 min_filter,
    const RenderSceneFilterV1 mag_filter,
    const RenderSceneMipmapFilterV1 mipmap_filter) {
    const auto min_linear = min_filter == RenderSceneFilterV1::linear;
    const auto mag_linear = mag_filter == RenderSceneFilterV1::linear;
    const auto mip_linear =
        mipmap_filter == RenderSceneMipmapFilterV1::linear;

    if (min_linear) {
        if (mag_linear) {
            return mip_linear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR
                              : D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        }
        return mip_linear ? D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR
                          : D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    }
    if (mag_linear) {
        return mip_linear ? D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR
                          : D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    }
    return mip_linear ? D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR
                      : D3D11_FILTER_MIN_MAG_MIP_POINT;
}

} // namespace

struct D3d11Renderer::Implementation {
    explicit Implementation(HWND native_window,
                             const RenderSceneV1& scene)
        : Implementation(native_window, scene, nullptr, nullptr, nullptr) {}

    Implementation(HWND native_window,
                   const RenderSceneV1& scene,
                   const ActorRigV1& player_rig,
                   const ActorModelV1& player_model,
                   const ActorAffineTransformV1& model_to_entity)
        : Implementation(native_window,
                         scene,
                         &player_rig,
                         &player_model,
                         &model_to_entity) {}

    Implementation(HWND native_window,
                   const RenderSceneV1& scene,
                   const ActorRigV1* const player_rig,
                   const ActorModelV1* const player_model,
                   const ActorAffineTransformV1* const model_to_entity)
        : window(native_window), native_scene_mode(true) {
        if (window == nullptr) {
            throw std::invalid_argument(
                "The D3D11 renderer requires a valid window handle");
        }
        const auto actor_inputs =
            static_cast<unsigned>(player_rig != nullptr) +
            static_cast<unsigned>(player_model != nullptr) +
            static_cast<unsigned>(model_to_entity != nullptr);
        if (actor_inputs != 0U && actor_inputs != 3U) {
            throw std::invalid_argument(
                "The D3D11 gameplay actor requires a complete rig, model, and entity transform");
        }

        auto flattened = build_render_scene_d3d_data_v1(scene);
        initialize_render_scene_geometry(flattened);
        render_scene_draws = std::move(flattened.draws);
        render_scene_materials = std::move(flattened.materials);
        if (player_rig != nullptr) {
            initialize_gameplay_actor(
                *player_rig, *player_model, *model_to_entity);
        }

        create_device_and_swap_chain();
        create_pipeline();
        if (has_source_geometry) {
            create_source_geometry_buffers(flattened.triangle_indices);
        }
        if (has_gameplay_actor) {
            create_gameplay_actor_buffers();
        }
        create_render_scene_resources(scene);
        if (has_gameplay_actor) {
            create_gameplay_actor_resources(*player_model);
        }
        create_render_target();

        RECT client_rectangle{};
        if (GetClientRect(window, &client_rectangle) == FALSE) {
            throw std::runtime_error(
                "GetClientRect failed while initializing the D3D11 renderer");
        }
        const auto client_width =
            std::max<LONG>(0, client_rectangle.right - client_rectangle.left);
        const auto client_height =
            std::max<LONG>(0, client_rectangle.bottom - client_rectangle.top);
        set_dimensions(static_cast<std::uint32_t>(client_width),
                       static_cast<std::uint32_t>(client_height));
    }

    explicit Implementation(HWND native_window,
                            const SceneGeometryV1& geometry)
        : Implementation(native_window, geometry, nullptr, nullptr) {}

    Implementation(HWND native_window,
                   const SceneGeometryV1& geometry,
                   const SceneGeometry3dV1& source_geometry)
        : Implementation(native_window, geometry, &source_geometry, nullptr) {}

    Implementation(HWND native_window,
                   const SceneGeometryV1& geometry,
                   const SceneGeometry3dV1& source_geometry,
                   const D3d11SourceTextureSourcesV1& textures)
        : Implementation(
              native_window, geometry, &source_geometry, &textures) {}

    Implementation(HWND native_window,
                   const SceneGeometryV1& geometry,
                   const SceneGeometry3dV1* const source_geometry,
                   const D3d11SourceTextureSourcesV1* const textures)
        : window(native_window),
          minimum_x(geometry.minimum_x),
          maximum_x(geometry.maximum_x),
          minimum_y(geometry.minimum_y),
          maximum_y(geometry.maximum_y) {
        if (window == nullptr) {
            throw std::invalid_argument(
                "The D3D11 renderer requires a valid window handle");
        }
        if (geometry.vertices.empty() ||
            geometry.triangle_indices.empty()) {
            throw std::invalid_argument(
                "The D3D11 renderer requires non-empty triangle geometry");
        }
        if (geometry.triangle_indices.size() >
            static_cast<std::size_t>(std::numeric_limits<UINT>::max())) {
            throw std::runtime_error(
                "The D3D11 index count exceeds the 32-bit draw limit");
        }
        raster_index_count =
            static_cast<UINT>(geometry.triangle_indices.size());
        has_raster_geometry = true;

        if (source_geometry != nullptr) {
            initialize_source_geometry(*source_geometry);
        }
        if (textures != nullptr) {
            if (source_geometry == nullptr) {
                throw std::invalid_argument(
                    "Textured source batches require source-space geometry");
            }
            initialize_terrain_texture_source(
                *source_geometry, textures->terrain);
            for (const auto& object : textures->objects) {
                initialize_object_texture_source(
                    *source_geometry, object);
            }
        }

        create_device_and_swap_chain();
        create_pipeline();
        create_raster_geometry_buffers(geometry);
        if (source_geometry != nullptr) {
            create_source_geometry_buffers(source_geometry->triangle_indices);
        }
        if (textures != nullptr) {
            create_texture_resources(
                textures->terrain.textures,
                texture_regions.front().texture_views);
            for (std::size_t index = 0U;
                 index < textures->objects.size(); ++index) {
                create_texture_resources(
                    textures->objects[index].textures,
                    texture_regions[index + 1U].texture_views);
            }
        }
        create_render_target();

        RECT client_rectangle{};
        if (GetClientRect(window, &client_rectangle) == FALSE) {
            throw std::runtime_error(
                "GetClientRect failed while initializing the D3D11 renderer");
        }
        const auto client_width =
            std::max<LONG>(0, client_rectangle.right - client_rectangle.left);
        const auto client_height =
            std::max<LONG>(0, client_rectangle.bottom - client_rectangle.top);
        set_dimensions(static_cast<std::uint32_t>(client_width),
                       static_cast<std::uint32_t>(client_height));
    }

    void initialize_render_scene_geometry(
        const RenderSceneD3dDataV1& geometry) {
        if (geometry.vertices.empty()) {
            return;
        }
        if (geometry.triangle_indices.empty() || geometry.draws.empty()) {
            throw std::invalid_argument(
                "The flattened RenderSceneV1 has an incomplete draw envelope");
        }

        source_vertices.reserve(geometry.vertices.size());
        for (const auto& vertex : geometry.vertices) {
            source_vertices.push_back(SceneVertex3dV1{
                vertex.x,
                vertex.y,
                vertex.z,
                vertex.rgba8,
                vertex.u,
                vertex.v,
            });
        }
        projected_vertices.resize(source_vertices.size());
        for (std::size_t index = 0U; index < source_vertices.size(); ++index) {
            projected_vertices[index].rgba = source_vertices[index].rgba;
            projected_vertices[index].u = source_vertices[index].u;
            projected_vertices[index].v = source_vertices[index].v;
        }
        source_index_count =
            static_cast<UINT>(geometry.triangle_indices.size());
        has_source_geometry = true;
        show_source_geometry = true;

        orbit_target = {geometry.camera_target[0U],
                        geometry.camera_target[1U],
                        geometry.camera_target[2U]};
        orbit_radius = geometry.camera_radius;
    }

    void initialize_gameplay_actor(
        const ActorRigV1& rig,
        const ActorModelV1& model,
        const ActorAffineTransformV1& model_to_entity) {
        if (model.meshes.empty() || model.materials.empty()) {
            throw std::invalid_argument(
                "The D3D11 gameplay actor has no renderable model data");
        }

        if (model.textures.size() >
                std::numeric_limits<std::uint32_t>::max() ||
            model.materials.size() >
                std::numeric_limits<std::uint32_t>::max() ||
            model.meshes.size() >
                std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error(
                "The D3D11 gameplay actor tables exceed the renderer ID domain");
        }
        // Gameplay entity placement adds only a Z rotation and translation,
        // whose linear determinant is +1. model_to_entity therefore decides
        // whether the flattened model triangles need their winding restored.
        const auto reverses_orientation =
            checked_actor_reverses_orientation(model_to_entity);

        for (std::size_t index = 0U; index < model.textures.size(); ++index) {
            preflight_gameplay_actor_texture(
                model.textures[index], static_cast<std::uint32_t>(index));
        }
        for (std::size_t index = 0U; index < model.materials.size(); ++index) {
            const auto& material = model.materials[index];
            if (material.id != static_cast<std::uint32_t>(index) ||
                !valid_actor_address_mode(material.address_u) ||
                !valid_actor_address_mode(material.address_v) ||
                !valid_actor_filter(material.min_filter) ||
                !valid_actor_filter(material.mag_filter) ||
                !valid_actor_mipmap_filter(material.mipmap_filter) ||
                !valid_actor_alpha_mode(material.alpha_mode) ||
                (material.alpha_mode == RenderSceneAlphaModeV1::opaque &&
                 material.alpha_cutoff_rgba8 != 0U) ||
                (material.alpha_mode == RenderSceneAlphaModeV1::mask &&
                 material.alpha_cutoff_rgba8 == 0U)) {
                throw std::invalid_argument(
                    "The D3D11 gameplay actor has an invalid material policy");
            }
            if (material.base_color_texture_id) {
                const auto texture_id = *material.base_color_texture_id;
                if (static_cast<std::size_t>(texture_id) >=
                        model.textures.size() ||
                    (material.mipmap_filter !=
                         RenderSceneMipmapFilterV1::none &&
                     model.textures[texture_id].mips.size() < 2U)) {
                    throw std::invalid_argument(
                        "The D3D11 gameplay actor material references unavailable texture data");
                }
            }
        }

        std::uint64_t vertex_count = 0U;
        std::uint64_t index_count = 0U;
        std::uint64_t draw_count = 0U;
        for (std::size_t mesh_index = 0U;
             mesh_index < model.meshes.size(); ++mesh_index) {
            const auto& mesh = model.meshes[mesh_index];
            if (mesh.id != static_cast<std::uint32_t>(mesh_index) ||
                mesh.vertices.empty() ||
                mesh.triangle_indices.empty() || mesh.draw_ranges.empty() ||
                (mesh.triangle_indices.size() % 3U) != 0U) {
                throw std::invalid_argument(
                    "The D3D11 gameplay actor has an invalid mesh envelope");
            }
            if (mesh.vertices.size() >
                    game::kRuntimePlayerActorMaximumVerticesV1 -
                        vertex_count ||
                mesh.triangle_indices.size() >
                    game::kRuntimePlayerActorMaximumTriangleIndicesV1 -
                        index_count ||
                mesh.draw_ranges.size() >
                    game::kRuntimePlayerActorMaximumDrawRangesV1 -
                        draw_count) {
                throw std::runtime_error(
                    "The D3D11 gameplay actor exceeds the renderer caps of 1000000 vertices, 3000000 indices, or 65536 draws");
            }
            vertex_count += mesh.vertices.size();
            index_count += mesh.triangle_indices.size();
            draw_count += mesh.draw_ranges.size();

            for (const auto index : mesh.triangle_indices) {
                if (index >= mesh.vertices.size()) {
                    throw std::invalid_argument(
                        "The D3D11 gameplay actor contains an invalid mesh index");
                }
            }
            std::uint64_t expected_first_index = 0U;
            const auto mesh_index_count =
                static_cast<std::uint64_t>(mesh.triangle_indices.size());
            for (const auto& draw : mesh.draw_ranges) {
                if (static_cast<std::size_t>(draw.material_id) >=
                        model.materials.size() ||
                    draw.first_index != expected_first_index ||
                    draw.index_count == 0U ||
                    (draw.index_count % 3U) != 0U ||
                    draw.index_count >
                        mesh_index_count - expected_first_index) {
                    throw std::invalid_argument(
                        "The D3D11 gameplay actor draw ranges are not a complete valid partition");
                }
                expected_first_index += draw.index_count;
            }
            if (expected_first_index != mesh_index_count) {
                throw std::invalid_argument(
                    "The D3D11 gameplay actor draw ranges do not cover their mesh");
            }
        }
        if (vertex_count == 0U || index_count == 0U || draw_count == 0U ||
            vertex_count > std::numeric_limits<std::uint32_t>::max() ||
            index_count > std::numeric_limits<UINT>::max()) {
            throw std::invalid_argument(
                "The D3D11 gameplay actor has an invalid draw envelope");
        }

        gameplay_actor_bind_palette =
            build_actor_bind_pose_palette_v1(
                rig, game::kRuntimePlayerActorPoseLimitsV1);
        gameplay_actor_model_to_entity = model_to_entity;
        gameplay_actor_meshes = model.meshes;
        gameplay_actor_materials.reserve(model.materials.size());
        for (const auto& material : model.materials) {
            gameplay_actor_materials.push_back(make_gpu_material(material));
        }
        gameplay_actor_vertices.resize(static_cast<std::size_t>(vertex_count));
        gameplay_actor_projected_vertices.resize(
            static_cast<std::size_t>(vertex_count));
        gameplay_actor_triangle_indices.reserve(
            static_cast<std::size_t>(index_count));
        gameplay_actor_draws.reserve(static_cast<std::size_t>(draw_count));

        std::uint32_t next_vertex_base = 0U;
        for (const auto& mesh : gameplay_actor_meshes) {
            const auto index_base = static_cast<std::uint32_t>(
                gameplay_actor_triangle_indices.size());
            const auto append_index =
                [this, next_vertex_base](const std::uint32_t index) {
                    gameplay_actor_triangle_indices.push_back(
                        next_vertex_base + index);
                };
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
                gameplay_actor_draws.push_back(RenderSceneD3dDrawV1{
                    draw.material_id,
                    index_base +
                        static_cast<std::uint32_t>(draw.first_index),
                    static_cast<std::uint32_t>(draw.index_count),
                });
            }
            next_vertex_base += static_cast<std::uint32_t>(
                mesh.vertices.size());
        }
        has_gameplay_actor = true;
    }

    void initialize_source_geometry(const SceneGeometry3dV1& geometry) {
        if (geometry.vertices.empty() || geometry.triangle_indices.empty()) {
            throw std::invalid_argument(
                "The D3D11 source-space renderer requires non-empty triangle geometry");
        }
        if (!std::isfinite(geometry.minimum_x) ||
            !std::isfinite(geometry.maximum_x) ||
            !std::isfinite(geometry.minimum_y) ||
            !std::isfinite(geometry.maximum_y) ||
            !std::isfinite(geometry.minimum_z) ||
            !std::isfinite(geometry.maximum_z) ||
            geometry.minimum_x > geometry.maximum_x ||
            geometry.minimum_y > geometry.maximum_y ||
            geometry.minimum_z > geometry.maximum_z) {
            throw std::invalid_argument(
                "The D3D11 source-space geometry has invalid bounds");
        }
        if (geometry.triangle_indices.size() >
            static_cast<std::size_t>(std::numeric_limits<UINT>::max())) {
            throw std::runtime_error(
                "The D3D11 source-space index count exceeds the 32-bit draw limit");
        }
        for (const auto index : geometry.triangle_indices) {
            if (index >= geometry.vertices.size()) {
                throw std::invalid_argument(
                    "The D3D11 source-space geometry contains an invalid index");
            }
        }
        for (const auto& vertex : geometry.vertices) {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
                !std::isfinite(vertex.z) || !std::isfinite(vertex.u) ||
                !std::isfinite(vertex.v)) {
                throw std::invalid_argument(
                    "The D3D11 source-space geometry has a non-finite vertex");
            }
        }

        source_vertices = geometry.vertices;
        projected_vertices.resize(source_vertices.size());
        for (std::size_t index = 0U; index < source_vertices.size(); ++index) {
            projected_vertices[index].rgba = source_vertices[index].rgba;
            projected_vertices[index].u = source_vertices[index].u;
            projected_vertices[index].v = source_vertices[index].v;
        }
        source_index_count =
            static_cast<UINT>(geometry.triangle_indices.size());
        has_source_geometry = true;
        show_source_geometry = true;

        const std::array<double, 3U> target{
            (static_cast<double>(geometry.minimum_x) + geometry.maximum_x) *
                0.5,
            (static_cast<double>(geometry.minimum_y) + geometry.maximum_y) *
                0.5,
            (static_cast<double>(geometry.minimum_z) + geometry.maximum_z) *
                0.5,
        };
        double squared_radius = 0.0;
        for (const auto& vertex : source_vertices) {
            const auto relative_x = static_cast<double>(vertex.x) - target[0U];
            const auto relative_y = static_cast<double>(vertex.y) - target[1U];
            const auto relative_z = static_cast<double>(vertex.z) - target[2U];
            squared_radius = std::max(
                squared_radius,
                relative_x * relative_x + relative_y * relative_y +
                    relative_z * relative_z);
        }
        auto radius = std::sqrt(squared_radius);
        if (!(radius > 0.0)) {
            radius = 1.0;
        }
        constexpr double kMaximumCameraDistanceFactor = 128.0;
        const auto maximum_float =
            static_cast<double>(std::numeric_limits<float>::max());
        for (const auto component : target) {
            const auto magnitude = std::abs(component);
            if (!std::isfinite(component) || magnitude > maximum_float ||
                radius * kMaximumCameraDistanceFactor >
                    maximum_float - magnitude) {
                throw std::invalid_argument(
                    "The D3D11 source-space geometry exceeds the camera numeric domain");
            }
        }
        orbit_target = {static_cast<float>(target[0U]),
                        static_cast<float>(target[1U]),
                        static_cast<float>(target[2U])};
        orbit_radius = static_cast<float>(radius);
        if (!std::isfinite(orbit_radius) || !(orbit_radius > 0.0F)) {
            orbit_radius = 1.0F;
        }
    }

    void initialize_terrain_texture_source(
        const SceneGeometry3dV1& geometry,
        const D3d11TerrainTextureSourceV1& source) {
        const auto triangle_count = static_cast<std::uint64_t>(
            geometry.triangle_indices.size() / 3U);
        if (geometry.triangle_indices.size() % 3U != 0U ||
            source.triangle_count == 0U ||
            source.triangle_count > triangle_count ||
            source.material_batches.empty() || source.textures.empty()) {
            throw std::invalid_argument(
                "The D3D11 terrain texture source has an invalid envelope");
        }
        if (source.triangle_count >
            static_cast<std::uint64_t>(std::numeric_limits<UINT>::max()) / 3U) {
            throw std::runtime_error(
                "The D3D11 terrain geometry exceeds the draw-index domain");
        }

        GpuSourceTextureRegion region{};
        region.start_index = 0U;
        region.index_count = static_cast<UINT>(source.triangle_count * 3U);
        std::uint64_t expected_first_triangle = 0U;
        if (source.material_batches.size() >
            region.material_batches.max_size()) {
            throw std::runtime_error(
                "The D3D11 terrain material batches exceed the host container");
        }
        region.material_batches.reserve(source.material_batches.size());
        for (const auto& batch : source.material_batches) {
            if (batch.first_triangle != expected_first_triangle ||
                batch.index_count == 0U || batch.index_count % 3U != 0U) {
                throw std::invalid_argument(
                    "The D3D11 terrain material batches are not contiguous");
            }
            const auto batch_triangles = batch.index_count / 3U;
            if (batch.first_triangle > source.triangle_count ||
                batch_triangles >
                    source.triangle_count - batch.first_triangle ||
                batch.index_count > std::numeric_limits<UINT>::max()) {
                throw std::invalid_argument(
                    "A D3D11 terrain material batch exceeds source geometry");
            }
            if (batch.texture_index &&
                *batch.texture_index >= source.textures.size()) {
                throw std::invalid_argument(
                    "A D3D11 terrain material references a missing texture");
            }
            region.material_batches.push_back(GpuSourceMaterialBatch{
                static_cast<UINT>(batch.first_triangle * 3U),
                static_cast<UINT>(batch.index_count),
                batch.texture_index});
            expected_first_triangle += batch_triangles;
        }
        if (expected_first_triangle != source.triangle_count) {
            throw std::invalid_argument(
                "The D3D11 terrain materials do not cover the source prefix");
        }
        texture_regions.push_back(std::move(region));
    }

    void initialize_object_texture_source(
        const SceneGeometry3dV1& geometry,
        const D3d11ObjectTextureSourceV1& source) {
        const auto triangle_count = static_cast<std::uint64_t>(
            geometry.triangle_indices.size() / 3U);
        if (geometry.triangle_indices.size() % 3U != 0U ||
            source.first_triangle > triangle_count ||
            source.triangle_count == 0U ||
            source.triangle_count > triangle_count - source.first_triangle ||
            source.material_batches.empty() || source.textures.empty()) {
            throw std::invalid_argument(
                "A D3D11 object texture source has an invalid envelope");
        }
        if (source.first_triangle >
                static_cast<std::uint64_t>(
                    std::numeric_limits<UINT>::max()) /
                    3U ||
            source.triangle_count >
                static_cast<std::uint64_t>(
                    std::numeric_limits<UINT>::max()) /
                    3U) {
            throw std::runtime_error(
                "A D3D11 object texture region exceeds the draw-index domain");
        }

        GpuSourceTextureRegion region{};
        region.start_index = static_cast<UINT>(source.first_triangle * 3U);
        region.index_count = static_cast<UINT>(source.triangle_count * 3U);
        if (!texture_regions.empty()) {
            const auto& previous = texture_regions.back();
            const auto previous_end =
                static_cast<std::uint64_t>(previous.start_index) +
                previous.index_count;
            if (region.start_index < previous_end) {
                throw std::invalid_argument(
                    "D3D11 texture regions overlap or are not ordered");
            }
        }

        std::uint64_t expected_first_triangle = 0U;
        if (source.material_batches.size() >
            region.material_batches.max_size()) {
            throw std::runtime_error(
                "The D3D11 object material batches exceed the host container");
        }
        region.material_batches.reserve(source.material_batches.size());
        for (const auto& batch : source.material_batches) {
            if (batch.first_triangle != expected_first_triangle ||
                batch.index_count == 0U || batch.index_count % 3U != 0U) {
                throw std::invalid_argument(
                    "The D3D11 object material batches are not contiguous");
            }
            const auto batch_triangles = batch.index_count / 3U;
            if (batch.first_triangle > source.triangle_count ||
                batch_triangles >
                    source.triangle_count - batch.first_triangle ||
                batch.index_count > std::numeric_limits<UINT>::max()) {
                throw std::invalid_argument(
                    "A D3D11 object material batch exceeds its texture region");
            }
            if (batch.global_texture_index &&
                *batch.global_texture_index >= source.textures.size()) {
                throw std::invalid_argument(
                    "A D3D11 object material references a missing texture");
            }
            const auto global_first_triangle =
                source.first_triangle + batch.first_triangle;
            region.material_batches.push_back(GpuSourceMaterialBatch{
                static_cast<UINT>(global_first_triangle * 3U),
                static_cast<UINT>(batch.index_count),
                batch.global_texture_index});
            expected_first_triangle += batch_triangles;
        }
        if (expected_first_triangle != source.triangle_count) {
            throw std::invalid_argument(
                "The D3D11 object materials do not cover their texture region");
        }
        texture_regions.push_back(std::move(region));
    }

    void create_device_and_swap_chain() {
        DXGI_SWAP_CHAIN_DESC swap_chain_description{};
        swap_chain_description.BufferDesc.Format =
            DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_chain_description.SampleDesc.Count = 1U;
        swap_chain_description.SampleDesc.Quality = 0U;
        swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_description.BufferCount = 2U;
        swap_chain_description.OutputWindow = window;
        swap_chain_description.Windowed = TRUE;
        swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr std::array<D3D_FEATURE_LEVEL, 3U> kFeatureLevels{
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        constexpr UINT kDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        auto result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            kDeviceFlags,
            kFeatureLevels.data(),
            static_cast<UINT>(kFeatureLevels.size()),
            D3D11_SDK_VERSION,
            &swap_chain_description,
            swap_chain.GetAddressOf(),
            device.GetAddressOf(),
            &feature_level,
            context.GetAddressOf());
        const auto hardware_result = result;
        if (FAILED(result)) {
            swap_chain.Reset();
            context.Reset();
            device.Reset();
            result = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                kDeviceFlags,
                kFeatureLevels.data(),
                static_cast<UINT>(kFeatureLevels.size()),
                D3D11_SDK_VERSION,
                &swap_chain_description,
                swap_chain.GetAddressOf(),
                device.GetAddressOf(),
                &feature_level,
                context.GetAddressOf());
        }
        if (FAILED(result)) {
            throw std::runtime_error(
                hresult_message(
                    "D3D11 hardware-device creation", hardware_result) +
                "; " + hresult_message("D3D11 WARP-device creation", result));
        }
    }

    void create_pipeline() {
        require_success(
            device->CreateVertexShader(
                g_openrc_wireframe_vs,
                sizeof(g_openrc_wireframe_vs),
                nullptr,
                raster_vertex_shader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader");
        require_success(
            device->CreatePixelShader(
                g_openrc_wireframe_ps,
                sizeof(g_openrc_wireframe_ps),
                nullptr,
                wireframe_pixel_shader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader");
        require_success(
            device->CreateVertexShader(
                g_openrc_source_textured_vs,
                sizeof(g_openrc_source_textured_vs),
                nullptr,
                source_vertex_shader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader(source)");
        require_success(
            device->CreatePixelShader(
                g_openrc_source_textured_ps,
                sizeof(g_openrc_source_textured_ps),
                nullptr,
                textured_pixel_shader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader(textured source)");
        require_success(
            device->CreatePixelShader(
                g_openrc_render_scene_ps,
                sizeof(g_openrc_render_scene_ps),
                nullptr,
                render_scene_pixel_shader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader(RenderSceneV1)");

        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2U> kInputElements{{
            {"POSITION",
             0U,
             DXGI_FORMAT_R32G32_FLOAT,
             0U,
             0U,
             D3D11_INPUT_PER_VERTEX_DATA,
             0U},
            {"COLOR",
             0U,
             DXGI_FORMAT_R8G8B8A8_UNORM,
             0U,
             static_cast<UINT>(offsetof(SceneVertexV1, rgba)),
             D3D11_INPUT_PER_VERTEX_DATA,
             0U},
        }};
        require_success(
            device->CreateInputLayout(
                kInputElements.data(),
                static_cast<UINT>(kInputElements.size()),
                g_openrc_wireframe_vs,
                sizeof(g_openrc_wireframe_vs),
                raster_input_layout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout");

        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 3U>
            kSourceInputElements{{
                {"POSITION",
                 0U,
                 DXGI_FORMAT_R32G32B32A32_FLOAT,
                 0U,
                 0U,
                 D3D11_INPUT_PER_VERTEX_DATA,
                 0U},
                {"COLOR",
                 0U,
                 DXGI_FORMAT_R8G8B8A8_UNORM,
                 0U,
                 static_cast<UINT>(offsetof(ProjectedSourceVertex, rgba)),
                 D3D11_INPUT_PER_VERTEX_DATA,
                 0U},
                {"TEXCOORD",
                 0U,
                 DXGI_FORMAT_R32G32_FLOAT,
                 0U,
                 static_cast<UINT>(offsetof(ProjectedSourceVertex, u)),
                 D3D11_INPUT_PER_VERTEX_DATA,
                 0U},
            }};
        require_success(
            device->CreateInputLayout(
                kSourceInputElements.data(),
                static_cast<UINT>(kSourceInputElements.size()),
                g_openrc_source_textured_vs,
                sizeof(g_openrc_source_textured_vs),
                source_input_layout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout(source)");

        D3D11_RASTERIZER_DESC rasterizer_description{};
        rasterizer_description.FillMode = D3D11_FILL_WIREFRAME;
        rasterizer_description.CullMode = D3D11_CULL_NONE;
        rasterizer_description.DepthClipEnable = TRUE;
        rasterizer_description.AntialiasedLineEnable = TRUE;
        require_success(
            device->CreateRasterizerState(
                &rasterizer_description,
                wireframe_rasterizer_state.GetAddressOf()),
            "ID3D11Device::CreateRasterizerState");

        rasterizer_description.FillMode = D3D11_FILL_SOLID;
        rasterizer_description.AntialiasedLineEnable = FALSE;
        require_success(
            device->CreateRasterizerState(
                &rasterizer_description,
                solid_rasterizer_state.GetAddressOf()),
            "ID3D11Device::CreateRasterizerState(solid)");

        // RenderSceneV1 defines counter-clockwise world-space front faces.
        // The viewport's Y flip presents those as clockwise on the D3D render
        // target, so FrontCounterClockwise remains false here.
        rasterizer_description.CullMode = D3D11_CULL_BACK;
        rasterizer_description.FrontCounterClockwise = FALSE;
        require_success(
            device->CreateRasterizerState(
                &rasterizer_description,
                solid_single_sided_rasterizer_state.GetAddressOf()),
            "ID3D11Device::CreateRasterizerState(RenderSceneV1 single-sided)");

        D3D11_DEPTH_STENCIL_DESC depth_description{};
        depth_description.DepthEnable = FALSE;
        depth_description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depth_description.DepthFunc = D3D11_COMPARISON_ALWAYS;
        require_success(
            device->CreateDepthStencilState(
                &depth_description, depth_disabled_state.GetAddressOf()),
            "ID3D11Device::CreateDepthStencilState");

        depth_description.DepthEnable = TRUE;
        depth_description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth_description.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        require_success(
            device->CreateDepthStencilState(
                &depth_description, depth_write_state.GetAddressOf()),
            "ID3D11Device::CreateDepthStencilState(write)");

        depth_description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        require_success(
            device->CreateDepthStencilState(
                &depth_description, depth_read_state.GetAddressOf()),
            "ID3D11Device::CreateDepthStencilState(read)");

        D3D11_BLEND_DESC no_color_description{};
        no_color_description.RenderTarget[0U].RenderTargetWriteMask = 0U;
        require_success(
            device->CreateBlendState(
                &no_color_description, no_color_blend_state.GetAddressOf()),
            "ID3D11Device::CreateBlendState(depth only)");

        D3D11_SAMPLER_DESC sampler_description{};
        sampler_description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_description.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_description.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_description.MaxAnisotropy = 1U;
        sampler_description.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler_description.MinLOD = 0.0F;
        sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
        require_success(
            device->CreateSamplerState(
                &sampler_description, texture_sampler.GetAddressOf()),
            "ID3D11Device::CreateSamplerState(Moby textures)");

        D3D11_BUFFER_DESC constant_description{};
        constant_description.ByteWidth = sizeof(FitConstants);
        constant_description.Usage = D3D11_USAGE_DEFAULT;
        constant_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        require_success(
            device->CreateBuffer(
                &constant_description, nullptr, fit_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(fit constants)");

        constant_description.ByteWidth = sizeof(RenderSceneMaterialConstants);
        require_success(
            device->CreateBuffer(
                &constant_description,
                nullptr,
                render_scene_material_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(RenderSceneV1 material constants)");
    }

    void create_raster_geometry_buffers(const SceneGeometryV1& geometry) {
        D3D11_BUFFER_DESC vertex_description{};
        vertex_description.ByteWidth = checked_buffer_size(
            geometry.vertices.size(), sizeof(SceneVertexV1), "Vertex buffer");
        vertex_description.Usage = D3D11_USAGE_IMMUTABLE;
        vertex_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertex_data{};
        vertex_data.pSysMem = geometry.vertices.data();
        require_success(
            device->CreateBuffer(
                &vertex_description,
                &vertex_data,
                raster_vertex_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(raster vertices)");

        D3D11_BUFFER_DESC index_description{};
        index_description.ByteWidth = checked_buffer_size(
            geometry.triangle_indices.size(),
            sizeof(std::uint32_t),
            "Index buffer");
        index_description.Usage = D3D11_USAGE_IMMUTABLE;
        index_description.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA index_data{};
        index_data.pSysMem = geometry.triangle_indices.data();
        require_success(
            device->CreateBuffer(
                &index_description,
                &index_data,
                raster_index_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(raster indices)");
    }

    void create_source_geometry_buffers(
        const std::span<const std::uint32_t> triangle_indices) {
        D3D11_BUFFER_DESC vertex_description{};
        vertex_description.ByteWidth = checked_buffer_size(
            projected_vertices.size(),
            sizeof(ProjectedSourceVertex),
            "Projected source vertex buffer");
        vertex_description.Usage = D3D11_USAGE_DYNAMIC;
        vertex_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        require_success(
            device->CreateBuffer(
                &vertex_description,
                nullptr,
                source_vertex_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(projected source vertices)");

        D3D11_BUFFER_DESC index_description{};
        index_description.ByteWidth = checked_buffer_size(
            triangle_indices.size(),
            sizeof(std::uint32_t),
            "Source index buffer");
        index_description.Usage = D3D11_USAGE_IMMUTABLE;
        index_description.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA index_data{};
        index_data.pSysMem = triangle_indices.data();
        require_success(
            device->CreateBuffer(
                &index_description,
                &index_data,
                source_index_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(source indices)");
    }

    void create_gameplay_actor_buffers() {
        if (!has_gameplay_actor || gameplay_actor_projected_vertices.empty() ||
            gameplay_actor_triangle_indices.empty()) {
            throw std::logic_error(
                "The D3D11 gameplay actor staging data is incomplete");
        }

        D3D11_BUFFER_DESC vertex_description{};
        vertex_description.ByteWidth = checked_buffer_size(
            gameplay_actor_projected_vertices.size(),
            sizeof(ProjectedSourceVertex),
            "Gameplay actor vertex buffer");
        vertex_description.Usage = D3D11_USAGE_DYNAMIC;
        vertex_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        require_success(
            device->CreateBuffer(
                &vertex_description,
                nullptr,
                gameplay_actor_vertex_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(gameplay actor vertices)");

        D3D11_BUFFER_DESC index_description{};
        index_description.ByteWidth = checked_buffer_size(
            gameplay_actor_triangle_indices.size(),
            sizeof(std::uint32_t),
            "Gameplay actor index buffer");
        index_description.Usage = D3D11_USAGE_IMMUTABLE;
        index_description.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA index_data{};
        index_data.pSysMem = gameplay_actor_triangle_indices.data();
        require_success(
            device->CreateBuffer(
                &index_description,
                &index_data,
                gameplay_actor_index_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(gameplay actor indices)");
    }

    void create_gameplay_proxy_buffers() {
        static_assert(kGameplayProxyVertexCount > 0U);
        static_assert(kGameplayProxyIndexCount > 0U);

        gameplay_proxy_vertices.resize(kGameplayProxyVertexCount);
        gameplay_proxy_projected_vertices.resize(kGameplayProxyVertexCount);
        std::vector<std::uint32_t> indices;
        indices.reserve(kGameplayProxyIndexCount);
        constexpr std::uint32_t kBottomPole = 0U;
        constexpr std::uint32_t kTopPole = 1U;
        constexpr std::uint32_t kLowerRing = 2U;
        constexpr std::uint32_t kUpperRing =
            kLowerRing + kGameplayProxyRingSegments;
        constexpr std::uint32_t kArrow =
            kUpperRing + kGameplayProxyRingSegments;

        for (std::uint32_t index = 0U;
             index < kGameplayProxyRingSegments; ++index) {
            const auto next = (index + 1U) % kGameplayProxyRingSegments;
            indices.insert(indices.end(), {
                kBottomPole,
                kLowerRing + next,
                kLowerRing + index,
                kLowerRing + index,
                kLowerRing + next,
                kUpperRing + next,
                kLowerRing + index,
                kUpperRing + next,
                kUpperRing + index,
                kTopPole,
                kUpperRing + index,
                kUpperRing + next,
            });
        }
        indices.insert(indices.end(), {kArrow, kArrow + 1U, kArrow + 2U});
        if (indices.size() != kGameplayProxyIndexCount) {
            throw std::logic_error(
                "The gameplay debug proxy topology changed unexpectedly");
        }
        gameplay_proxy_index_count = static_cast<UINT>(indices.size());

        D3D11_BUFFER_DESC vertex_description{};
        vertex_description.ByteWidth = checked_buffer_size(
            gameplay_proxy_projected_vertices.size(),
            sizeof(ProjectedSourceVertex),
            "Gameplay debug-proxy vertex buffer");
        vertex_description.Usage = D3D11_USAGE_DYNAMIC;
        vertex_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        require_success(
            device->CreateBuffer(
                &vertex_description,
                nullptr,
                gameplay_proxy_vertex_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(gameplay debug-proxy vertices)");

        D3D11_BUFFER_DESC index_description{};
        index_description.ByteWidth = checked_buffer_size(
            indices.size(), sizeof(std::uint32_t),
            "Gameplay debug-proxy index buffer");
        index_description.Usage = D3D11_USAGE_IMMUTABLE;
        index_description.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA index_data{};
        index_data.pSysMem = indices.data();
        require_success(
            device->CreateBuffer(
                &index_description,
                &index_data,
                gameplay_proxy_index_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(gameplay debug-proxy indices)");
    }

    void rebuild_gameplay_actor_vertices() {
        if (!has_gameplay_actor || !gameplay_presentation ||
            gameplay_actor_vertices.empty() ||
            gameplay_actor_projected_vertices.size() !=
                gameplay_actor_vertices.size()) {
            throw std::logic_error(
                "The gameplay actor has no valid presentation storage");
        }

        const auto& presentation = *gameplay_presentation;
        const auto cosine = std::cos(presentation.facing_yaw_radians);
        const auto sine = std::sin(presentation.facing_yaw_radians);
        ActorAffineTransformV1 entity_to_world;
        entity_to_world.values = {
            cosine,
            -sine,
            0.0F,
            presentation.feet_position.x,
            sine,
            cosine,
            0.0F,
            presentation.feet_position.y,
            0.0F,
            0.0F,
            1.0F,
            presentation.feet_position.z,
        };
        const auto model_to_world = compose_actor_transform(
            entity_to_world, gameplay_actor_model_to_entity);

        std::size_t output_offset = 0U;
        for (const auto& mesh : gameplay_actor_meshes) {
            const auto posed = pose_actor_mesh_vertices_v1(
                mesh,
                gameplay_actor_bind_palette,
                model_to_world,
                game::kRuntimePlayerActorPoseLimitsV1);
            if (posed.size() > gameplay_actor_vertices.size() - output_offset) {
                throw std::logic_error(
                    "The gameplay actor pose exceeded its flattened vertex storage");
            }
            for (const auto& vertex : posed) {
                gameplay_actor_vertices[output_offset] = SceneVertex3dV1{
                    vertex.x,
                    vertex.y,
                    vertex.z,
                    vertex.rgba8,
                    vertex.u,
                    vertex.v,
                };
                gameplay_actor_projected_vertices[output_offset].rgba =
                    vertex.rgba8;
                gameplay_actor_projected_vertices[output_offset].u = vertex.u;
                gameplay_actor_projected_vertices[output_offset].v = vertex.v;
                ++output_offset;
            }
        }
        if (output_offset != gameplay_actor_vertices.size()) {
            throw std::logic_error(
                "The gameplay actor pose did not fill its flattened vertex storage");
        }
    }

    void rebuild_gameplay_proxy_vertices() {
        if (!gameplay_presentation ||
            gameplay_proxy_vertices.size() != kGameplayProxyVertexCount) {
            throw std::logic_error(
                "The gameplay debug proxy has no valid presentation storage");
        }
        const auto& presentation = *gameplay_presentation;
        constexpr std::uint32_t kProxyColor = UINT32_C(0xffff00ff);
        constexpr std::uint32_t kDirectionColor = UINT32_C(0xff00ffff);
        constexpr std::uint32_t kLowerRing = 2U;
        constexpr std::uint32_t kUpperRing =
            kLowerRing + kGameplayProxyRingSegments;
        constexpr std::uint32_t kArrow =
            kUpperRing + kGameplayProxyRingSegments;
        const auto make_vertex = [](const Vec3 position,
                                    const std::uint32_t color) {
            return SceneVertex3dV1{
                position.x, position.y, position.z, color, 0.0F, 0.0F};
        };

        const auto feet = presentation.feet_position;
        gameplay_proxy_vertices[0U] = make_vertex(feet, kProxyColor);
        gameplay_proxy_vertices[1U] = make_vertex(
            {feet.x, feet.y, feet.z + presentation.capsule_height},
            kProxyColor);
        const auto lower_z = feet.z + presentation.capsule_radius;
        const auto upper_z =
            feet.z + presentation.capsule_height -
            presentation.capsule_radius;
        for (std::uint32_t index = 0U;
             index < kGameplayProxyRingSegments; ++index) {
            const auto angle = 2.0F * kPi * static_cast<float>(index) /
                               static_cast<float>(kGameplayProxyRingSegments);
            const auto offset_x = presentation.capsule_radius * std::cos(angle);
            const auto offset_y = presentation.capsule_radius * std::sin(angle);
            gameplay_proxy_vertices[kLowerRing + index] = make_vertex(
                {feet.x + offset_x, feet.y + offset_y, lower_z},
                kProxyColor);
            gameplay_proxy_vertices[kUpperRing + index] = make_vertex(
                {feet.x + offset_x, feet.y + offset_y, upper_z},
                kProxyColor);
        }

        const Vec3 direction{
            std::cos(presentation.facing_yaw_radians),
            std::sin(presentation.facing_yaw_radians),
            0.0F,
        };
        const Vec3 side{-direction.y, direction.x, 0.0F};
        const auto arrow_z = feet.z + presentation.capsule_height * 0.62F;
        const Vec3 arrow_center{feet.x, feet.y, arrow_z};
        const auto arrow_length = presentation.capsule_radius + 0.80F;
        const auto arrow_half_width = presentation.capsule_radius * 0.72F;
        gameplay_proxy_vertices[kArrow] = make_vertex(
            arrow_center + direction * arrow_length, kDirectionColor);
        gameplay_proxy_vertices[kArrow + 1U] = make_vertex(
            arrow_center - direction * (presentation.capsule_radius * 0.15F) +
                side * arrow_half_width,
            kDirectionColor);
        gameplay_proxy_vertices[kArrow + 2U] = make_vertex(
            arrow_center - direction * (presentation.capsule_radius * 0.15F) -
                side * arrow_half_width,
            kDirectionColor);

        for (std::size_t index = 0U;
             index < gameplay_proxy_vertices.size(); ++index) {
            gameplay_proxy_projected_vertices[index].rgba =
                gameplay_proxy_vertices[index].rgba;
            gameplay_proxy_projected_vertices[index].u = 0.0F;
            gameplay_proxy_projected_vertices[index].v = 0.0F;
        }
    }

    void set_gameplay_presentation(
        const game::ThirdPersonCameraViewV1& camera,
        const CollisionVectorV1& feet_position,
        const double facing_yaw_radians,
        const double capsule_radius,
        const double capsule_height) {
        if (!native_scene_mode) {
            throw std::logic_error(
                "Gameplay presentation is only available for prepared native scenes");
        }
        const auto next = checked_gameplay_presentation(
            camera, feet_position, facing_yaw_radians, capsule_radius,
            capsule_height);
        if (gameplay_presentation == next) {
            return;
        }
        gameplay_presentation = next;
        if (has_gameplay_actor) {
            rebuild_gameplay_actor_vertices();
        } else {
            if (!gameplay_proxy_vertex_buffer ||
                !gameplay_proxy_index_buffer) {
                create_gameplay_proxy_buffers();
            }
            rebuild_gameplay_proxy_vertices();
        }
        projected_vertices_dirty = true;
    }

    void create_texture_resources(
        const std::span<const RacLevelMobyTextureV1> textures,
        std::vector<ComPtr<ID3D11ShaderResourceView>>& texture_views) {
        if (textures.empty() ||
            textures.size() >
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max())) {
            throw std::invalid_argument(
                "A D3D11 source texture bank has an invalid envelope");
        }

        texture_views.reserve(textures.size());
        for (std::size_t index = 0U; index < textures.size(); ++index) {
            const auto& texture = textures[index];
            if (texture.global_index != index || texture.entry.width <= 0 ||
                texture.entry.height <= 0) {
                throw std::invalid_argument(
                    "A D3D11 source texture bank has inconsistent indices or dimensions");
            }
            const auto texture_width =
                static_cast<std::uint32_t>(texture.entry.width);
            const auto texture_height =
                static_cast<std::uint32_t>(texture.entry.height);
            const auto expected_rgba_bytes =
                static_cast<std::uint64_t>(texture_width) * texture_height *
                4U;
            if (expected_rgba_bytes != texture.rgba.size()) {
                throw std::invalid_argument(
                    "A D3D11 source texture has an invalid RGBA payload size");
            }

            D3D11_TEXTURE2D_DESC texture_description{};
            texture_description.Width = texture_width;
            texture_description.Height = texture_height;
            texture_description.MipLevels = 1U;
            texture_description.ArraySize = 1U;
            texture_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            texture_description.SampleDesc.Count = 1U;
            texture_description.Usage = D3D11_USAGE_IMMUTABLE;
            texture_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA texture_data{};
            texture_data.pSysMem = texture.rgba.data();
            texture_data.SysMemPitch = texture_width * 4U;
            ComPtr<ID3D11Texture2D> gpu_texture;
            require_success(
                device->CreateTexture2D(
                    &texture_description,
                    &texture_data,
                    gpu_texture.GetAddressOf()),
                "ID3D11Device::CreateTexture2D(source texture)");

            ComPtr<ID3D11ShaderResourceView> texture_view;
            require_success(
                device->CreateShaderResourceView(
                    gpu_texture.Get(), nullptr, texture_view.GetAddressOf()),
                "ID3D11Device::CreateShaderResourceView(source texture)");
            texture_views.push_back(std::move(texture_view));
        }
    }

    void create_neutral_texture_resources(
        const std::span<const RenderSceneTextureV1> textures,
        std::vector<ComPtr<ID3D11ShaderResourceView>>& destination) {
        destination.reserve(textures.size());
        for (const auto& texture : textures) {
            const auto format =
                texture.color_space == RenderSceneTextureColorSpaceV1::srgb
                    ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                    : DXGI_FORMAT_R8G8B8A8_UNORM;

            const auto& base_mip = texture.mips.front();
            D3D11_TEXTURE2D_DESC texture_description{};
            texture_description.Width = base_mip.width;
            texture_description.Height = base_mip.height;
            texture_description.MipLevels =
                static_cast<UINT>(texture.mips.size());
            texture_description.ArraySize = 1U;
            texture_description.Format = format;
            texture_description.SampleDesc.Count = 1U;
            texture_description.Usage = D3D11_USAGE_IMMUTABLE;
            texture_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            std::vector<D3D11_SUBRESOURCE_DATA> mip_data;
            mip_data.reserve(texture.mips.size());
            for (const auto& mip : texture.mips) {
                D3D11_SUBRESOURCE_DATA data{};
                data.pSysMem = mip.rgba8.data();
                data.SysMemPitch = mip.width * 4U;
                data.SysMemSlicePitch =
                    static_cast<UINT>(mip.rgba8.size());
                mip_data.push_back(data);
            }

            ComPtr<ID3D11Texture2D> gpu_texture;
            require_success(
                device->CreateTexture2D(
                    &texture_description,
                    mip_data.data(),
                    gpu_texture.GetAddressOf()),
                "ID3D11Device::CreateTexture2D(RenderSceneV1 texture)");

            D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};
            view_description.Format = format;
            view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view_description.Texture2D.MostDetailedMip = 0U;
            view_description.Texture2D.MipLevels =
                static_cast<UINT>(texture.mips.size());
            ComPtr<ID3D11ShaderResourceView> texture_view;
            require_success(
                device->CreateShaderResourceView(
                    gpu_texture.Get(),
                    &view_description,
                    texture_view.GetAddressOf()),
                "ID3D11Device::CreateShaderResourceView(RenderSceneV1 texture)");
            destination.push_back(std::move(texture_view));
        }
    }

    void create_render_scene_white_texture() {
        constexpr std::array<std::uint8_t, 4U> kWhite{
            0xffU, 0xffU, 0xffU, 0xffU};
        D3D11_TEXTURE2D_DESC texture_description{};
        texture_description.Width = 1U;
        texture_description.Height = 1U;
        texture_description.MipLevels = 1U;
        texture_description.ArraySize = 1U;
        texture_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture_description.SampleDesc.Count = 1U;
        texture_description.Usage = D3D11_USAGE_IMMUTABLE;
        texture_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA texture_data{};
        texture_data.pSysMem = kWhite.data();
        texture_data.SysMemPitch = 4U;
        texture_data.SysMemSlicePitch = 4U;
        ComPtr<ID3D11Texture2D> gpu_texture;
        require_success(
            device->CreateTexture2D(
                &texture_description,
                &texture_data,
                gpu_texture.GetAddressOf()),
            "ID3D11Device::CreateTexture2D(RenderSceneV1 white texture)");
        require_success(
            device->CreateShaderResourceView(
                gpu_texture.Get(),
                nullptr,
                render_scene_white_texture_view.GetAddressOf()),
            "ID3D11Device::CreateShaderResourceView(RenderSceneV1 white texture)");
    }

    [[nodiscard]] ComPtr<ID3D11SamplerState>
    render_scene_sampler(const RenderSceneD3dMaterialV1& material) {
        const RenderSceneSamplerKey key{
            material.address_u,
            material.address_v,
            material.min_filter,
            material.mag_filter,
            material.mipmap_filter,
        };
        const auto existing = std::find_if(
            render_scene_sampler_cache.begin(),
            render_scene_sampler_cache.end(),
            [&key](const GpuRenderSceneSampler& candidate) {
                return candidate.key == key;
            });
        if (existing != render_scene_sampler_cache.end()) {
            return existing->state;
        }

        D3D11_SAMPLER_DESC description{};
        description.Filter = texture_filter(
            material.min_filter,
            material.mag_filter,
            material.mipmap_filter);
        description.AddressU = texture_address_mode(material.address_u);
        description.AddressV = texture_address_mode(material.address_v);
        description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        description.MaxAnisotropy = 1U;
        description.ComparisonFunc = D3D11_COMPARISON_NEVER;
        description.MinLOD = 0.0F;
        description.MaxLOD =
            material.mipmap_filter == RenderSceneMipmapFilterV1::none
                ? 0.0F
                : D3D11_FLOAT32_MAX;

        ComPtr<ID3D11SamplerState> state;
        require_success(
            device->CreateSamplerState(&description, state.GetAddressOf()),
            "ID3D11Device::CreateSamplerState(RenderSceneV1 material)");
        render_scene_sampler_cache.push_back({key, state});
        return state;
    }

    void create_render_scene_resources(const RenderSceneV1& scene) {
        create_neutral_texture_resources(
            scene.textures, render_scene_texture_views);
        create_render_scene_white_texture();
        render_scene_material_samplers.reserve(
            render_scene_materials.size());
        for (const auto& material : render_scene_materials) {
            render_scene_material_samplers.push_back(
                render_scene_sampler(material));
        }
    }

    void create_gameplay_actor_resources(const ActorModelV1& model) {
        create_neutral_texture_resources(
            model.textures, gameplay_actor_texture_views);
        gameplay_actor_material_samplers.reserve(
            gameplay_actor_materials.size());
        for (const auto& material : gameplay_actor_materials) {
            gameplay_actor_material_samplers.push_back(
                render_scene_sampler(material));
        }
    }

    void create_render_target() {
        ComPtr<ID3D11Texture2D> back_buffer;
        require_success(
            swap_chain->GetBuffer(
                0U,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(back_buffer.GetAddressOf())),
            "IDXGISwapChain::GetBuffer");
        require_success(
            device->CreateRenderTargetView(
                back_buffer.Get(), nullptr, render_target.GetAddressOf()),
            "ID3D11Device::CreateRenderTargetView");

        D3D11_TEXTURE2D_DESC back_buffer_description{};
        back_buffer->GetDesc(&back_buffer_description);
        D3D11_TEXTURE2D_DESC depth_description{};
        depth_description.Width = back_buffer_description.Width;
        depth_description.Height = back_buffer_description.Height;
        depth_description.MipLevels = 1U;
        depth_description.ArraySize = 1U;
        depth_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depth_description.SampleDesc = back_buffer_description.SampleDesc;
        depth_description.Usage = D3D11_USAGE_DEFAULT;
        depth_description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        require_success(
            device->CreateTexture2D(
                &depth_description, nullptr, depth_texture.GetAddressOf()),
            "ID3D11Device::CreateTexture2D(depth)");
        require_success(
            device->CreateDepthStencilView(
                depth_texture.Get(), nullptr, depth_view.GetAddressOf()),
            "ID3D11Device::CreateDepthStencilView");
    }

    [[nodiscard]] float aspect_ratio() const noexcept {
        if (width == 0U || height == 0U) {
            return 16.0F / 9.0F;
        }
        return static_cast<float>(width) / static_cast<float>(height);
    }

    void reset_camera() noexcept {
        if (!has_source_geometry) {
            return;
        }
        orbit_yaw = 45.0F * kPi / 180.0F;
        orbit_pitch = 25.0F * kPi / 180.0F;

        const auto half_vertical = kDefaultVerticalFov * 0.5F;
        const auto half_horizontal = std::atan(
            std::tan(half_vertical) * std::max(aspect_ratio(), 0.01F));
        const auto limiting_half_fov =
            std::max(0.01F, std::min(half_vertical, half_horizontal));
        orbit_distance = std::max(
            orbit_radius * 1.05F,
            orbit_radius * 1.10F / std::sin(limiting_half_fov));
        camera_initialized = true;
        projected_vertices_dirty = true;
    }

    void orbit(const float yaw_delta, const float pitch_delta) noexcept {
        if (!has_source_geometry || !std::isfinite(yaw_delta) ||
            !std::isfinite(pitch_delta)) {
            return;
        }
        orbit_yaw = std::remainder(orbit_yaw + yaw_delta, 2.0F * kPi);
        orbit_pitch = std::clamp(
            orbit_pitch + pitch_delta, -kMaximumPitch, kMaximumPitch);
        projected_vertices_dirty = true;
    }

    void zoom(const float wheel_steps) noexcept {
        if (!has_source_geometry || !std::isfinite(wheel_steps)) {
            return;
        }
        const auto scale = std::exp(-0.15F * wheel_steps);
        orbit_distance = std::clamp(
            orbit_distance * scale,
            orbit_radius * 1.05F,
            orbit_radius * 100.0F);
        projected_vertices_dirty = true;
    }

    void toggle_view_mode() noexcept {
        if (has_source_geometry && has_raster_geometry &&
            !native_scene_mode) {
            show_source_geometry = !show_source_geometry;
        }
    }

    [[nodiscard]] bool is_3d_view() const noexcept {
        return native_scene_mode ||
            (has_source_geometry && show_source_geometry);
    }

    [[nodiscard]] CameraProjection make_camera_projection() {
        CameraProjection result;
        float near_plane = 0.0F;
        float far_plane = 0.0F;
        if (gameplay_presentation) {
            const auto& presentation = *gameplay_presentation;
            result.eye = presentation.eye;
            result.forward = normalized(presentation.target - result.eye);
            result.right = normalized(
                cross(result.forward, presentation.up));
            result.up = normalized(cross(result.right, result.forward));
            result.tangent_half_vertical = std::tan(
                presentation.vertical_field_of_view_radians * 0.5F);
            result.tangent_half_horizontal =
                result.tangent_half_vertical * presentation.aspect_ratio;
            near_plane = presentation.near_plane_distance;
            far_plane = presentation.far_plane_distance;
        } else {
            if (!camera_initialized) {
                reset_camera();
            }
            const auto cosine_pitch = std::cos(orbit_pitch);
            const Vec3 target_to_eye{
                cosine_pitch * std::cos(orbit_yaw),
                cosine_pitch * std::sin(orbit_yaw),
                std::sin(orbit_pitch),
            };
            result.eye = orbit_target + target_to_eye * orbit_distance;
            result.forward = normalized(orbit_target - result.eye);
            constexpr Vec3 kDebugUp{0.0F, 0.0F, 1.0F};
            result.right = normalized(cross(result.forward, kDebugUp));
            result.up = normalized(cross(result.right, result.forward));
            result.tangent_half_vertical =
                std::tan(kDefaultVerticalFov * 0.5F);
            result.tangent_half_horizontal =
                result.tangent_half_vertical * aspect_ratio();
            near_plane = std::max(orbit_radius * 0.001F, 0.001F);
            far_plane = std::max(
                near_plane * 2.0F,
                orbit_distance + orbit_radius * 4.0F);
        }

        const auto depth_denominator =
            static_cast<double>(far_plane) - near_plane;
        result.depth_scale = static_cast<float>(
            static_cast<double>(far_plane) / depth_denominator);
        result.depth_offset = static_cast<float>(
            static_cast<double>(near_plane) * far_plane /
            depth_denominator);
        const std::array values{
            squared_length(result.forward),
            squared_length(result.right),
            squared_length(result.up),
            result.tangent_half_vertical,
            result.tangent_half_horizontal,
            result.depth_scale,
            result.depth_offset,
        };
        if (!(near_plane > 0.0F) || !(far_plane > near_plane) ||
            !std::ranges::all_of(values, [](const float value) {
                return std::isfinite(value) && value > 0.0F;
            })) {
            throw std::runtime_error(
                "The D3D11 camera produced an invalid projection basis");
        }
        return result;
    }

    void project_and_upload_vertices(
        const CameraProjection& camera,
        const std::span<const SceneVertex3dV1> vertices,
        const std::span<ProjectedSourceVertex> output,
        ID3D11Buffer* const buffer,
        const char* const map_operation) {
        if (vertices.empty() || vertices.size() != output.size() ||
            buffer == nullptr) {
            throw std::logic_error(
                "The D3D11 projected-vertex storage is inconsistent");
        }
        for (std::size_t index = 0U; index < vertices.size(); ++index) {
            const auto relative = as_vec3(vertices[index]) - camera.eye;
            const auto camera_z = dot(relative, camera.forward);
            output[index].clip_position = {
                dot(relative, camera.right) /
                    camera.tangent_half_horizontal,
                dot(relative, camera.up) /
                    camera.tangent_half_vertical,
                camera.depth_scale * camera_z - camera.depth_offset,
                camera_z,
            };
            for (const auto component : output[index].clip_position) {
                if (!std::isfinite(component)) {
                    throw std::runtime_error(
                        "The D3D11 camera produced a non-finite projected vertex");
                }
            }
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        require_success(
            context->Map(
                buffer, 0U, D3D11_MAP_WRITE_DISCARD, 0U, &mapped),
            map_operation);
        std::memcpy(
            mapped.pData,
            output.data(),
            output.size_bytes());
        context->Unmap(buffer, 0U);
    }

    void update_projected_vertices() {
        if ((!has_source_geometry && !gameplay_presentation) ||
            !projected_vertices_dirty || width == 0U || height == 0U) {
            return;
        }
        const auto camera = make_camera_projection();
        if (has_source_geometry) {
            project_and_upload_vertices(
                camera,
                source_vertices,
                projected_vertices,
                source_vertex_buffer.Get(),
                "ID3D11DeviceContext::Map(projected source vertices)");
        }
        if (gameplay_presentation) {
            if (has_gameplay_actor) {
                project_and_upload_vertices(
                    camera,
                    gameplay_actor_vertices,
                    gameplay_actor_projected_vertices,
                    gameplay_actor_vertex_buffer.Get(),
                    "ID3D11DeviceContext::Map(gameplay actor vertices)");
            } else {
                project_and_upload_vertices(
                    camera,
                    gameplay_proxy_vertices,
                    gameplay_proxy_projected_vertices,
                    gameplay_proxy_vertex_buffer.Get(),
                    "ID3D11DeviceContext::Map(gameplay debug-proxy vertices)");
            }
        }
        projected_vertices_dirty = false;
    }

    void set_dimensions(const std::uint32_t new_width,
                        const std::uint32_t new_height) {
        width = new_width;
        height = new_height;
        if (width == 0U || height == 0U) {
            return;
        }

        constexpr float kPreferredMarginPixels = 32.0F;
        const auto margin = std::min(
            kPreferredMarginPixels,
            0.1F * static_cast<float>(std::min(width, height)));
        const auto drawable_width = std::max(
            1.0F, static_cast<float>(width) - 2.0F * margin);
        const auto drawable_height = std::max(
            1.0F, static_cast<float>(height) - 2.0F * margin);
        const auto scene_width = std::max(1.0F, maximum_x - minimum_x);
        const auto scene_height = std::max(1.0F, maximum_y - minimum_y);
        const auto pixels_per_scene_unit = std::min(
            drawable_width / scene_width, drawable_height / scene_height);
        const auto center_x = (minimum_x + maximum_x) * 0.5F;
        const auto center_y = (minimum_y + maximum_y) * 0.5F;

        fit_constants.scale = {
            2.0F * pixels_per_scene_unit / static_cast<float>(width),
            2.0F * pixels_per_scene_unit / static_cast<float>(height),
        };
        fit_constants.offset = {
            -center_x * fit_constants.scale[0U],
            -center_y * fit_constants.scale[1U],
        };
        if (has_source_geometry || gameplay_presentation) {
            if (!camera_initialized) {
                reset_camera();
            }
            projected_vertices_dirty = true;
        }
    }

    void resize(const std::uint32_t new_width,
                const std::uint32_t new_height) {
        if (new_width == 0U || new_height == 0U) {
            set_dimensions(0U, 0U);
            return;
        }
        if (new_width == width && new_height == height && render_target &&
            depth_texture && depth_view) {
            return;
        }

        context->OMSetRenderTargets(0U, nullptr, nullptr);
        render_target.Reset();
        depth_view.Reset();
        depth_texture.Reset();
        require_success(
            swap_chain->ResizeBuffers(
                0U, new_width, new_height, DXGI_FORMAT_UNKNOWN, 0U),
            "IDXGISwapChain::ResizeBuffers");
        create_render_target();
        set_dimensions(new_width, new_height);
    }

    bool render() {
        if (width == 0U || height == 0U || !render_target || !depth_view) {
            return false;
        }

        const auto render_source = native_scene_mode || is_3d_view();
        if (render_source) {
            update_projected_vertices();
        }

        constexpr std::array<float, 4U> kClearColor{
            5.0F / 255.0F,
            8.0F / 255.0F,
            18.0F / 255.0F,
            1.0F,
        };
        context->ClearRenderTargetView(render_target.Get(), kClearColor.data());
        if (render_source) {
            context->ClearDepthStencilView(
                depth_view.Get(), D3D11_CLEAR_DEPTH, 1.0F, 0U);
        }

        ID3D11RenderTargetView* const render_targets[] = {
            render_target.Get()};
        context->OMSetRenderTargets(
            1U,
            render_targets,
            render_source ? depth_view.Get() : nullptr);

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0F;
        viewport.MaxDepth = 1.0F;
        context->RSSetViewports(1U, &viewport);
        context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        constexpr UINT kOffset = 0U;
        constexpr std::array<float, 4U> kBlendFactor{};
        constexpr UINT kAllSamples = 0xffffffffU;
        ID3D11ShaderResourceView* const no_texture[] = {nullptr};

        if (!render_source) {
            context->UpdateSubresource(
                fit_buffer.Get(), 0U, nullptr, &fit_constants, 0U, 0U);
            constexpr UINT kRasterStride = sizeof(SceneVertexV1);
            ID3D11Buffer* const vertex_buffers[] = {
                raster_vertex_buffer.Get()};
            context->IASetInputLayout(raster_input_layout.Get());
            context->IASetVertexBuffers(
                0U, 1U, vertex_buffers, &kRasterStride, &kOffset);
            context->IASetIndexBuffer(
                raster_index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0U);
            context->RSSetState(wireframe_rasterizer_state.Get());
            context->OMSetDepthStencilState(depth_disabled_state.Get(), 0U);
            context->OMSetBlendState(
                nullptr, kBlendFactor.data(), kAllSamples);
            ID3D11Buffer* const constant_buffers[] = {fit_buffer.Get()};
            context->VSSetShader(raster_vertex_shader.Get(), nullptr, 0U);
            context->VSSetConstantBuffers(0U, 1U, constant_buffers);
            context->PSSetShader(wireframe_pixel_shader.Get(), nullptr, 0U);
            context->PSSetShaderResources(0U, 1U, no_texture);
            context->DrawIndexed(raster_index_count, 0U, 0);
        } else {
            constexpr UINT kSourceStride = sizeof(ProjectedSourceVertex);
            ID3D11Buffer* const vertex_buffers[] = {
                source_vertex_buffer.Get()};
            context->IASetInputLayout(source_input_layout.Get());
            context->IASetVertexBuffers(
                0U, 1U, vertex_buffers, &kSourceStride, &kOffset);
            context->IASetIndexBuffer(
                source_index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0U);
            context->VSSetShader(source_vertex_shader.Get(), nullptr, 0U);
            ID3D11Buffer* const no_constant_buffer[] = {nullptr};
            context->VSSetConstantBuffers(0U, 1U, no_constant_buffer);

            if (native_scene_mode) {
                const auto draw_neutral =
                    [this, &kBlendFactor](
                        const RenderSceneD3dDrawV1& draw,
                        const std::vector<RenderSceneD3dMaterialV1>& materials,
                        const std::vector<ComPtr<ID3D11ShaderResourceView>>&
                            texture_views,
                        const std::vector<ComPtr<ID3D11SamplerState>>&
                            material_samplers) {
                    const auto& material = materials[draw.material_id];
                    RenderSceneMaterialConstants constants{};
                    constants.base_color = material.base_color;
                    constants.use_vertex_color =
                        material.use_vertex_color ? 1.0F : 0.0F;
                    constants.alpha_cutoff = material.alpha_cutoff;
                    context->UpdateSubresource(
                        render_scene_material_buffer.Get(),
                        0U,
                        nullptr,
                        &constants,
                        0U,
                        0U);

                    context->RSSetState(
                        material.double_sided
                            ? solid_rasterizer_state.Get()
                            : solid_single_sided_rasterizer_state.Get());
                    context->OMSetDepthStencilState(
                        depth_write_state.Get(), 0U);
                    context->OMSetBlendState(
                        nullptr, kBlendFactor.data(), kAllSamples);
                    context->PSSetShader(
                        render_scene_pixel_shader.Get(), nullptr, 0U);
                    ID3D11Buffer* const material_buffers[] = {
                        render_scene_material_buffer.Get()};
                    context->PSSetConstantBuffers(
                        0U, 1U, material_buffers);

                    auto* texture_view =
                        material.base_color_texture_id
                            ? texture_views[
                                  *material.base_color_texture_id].Get()
                            : render_scene_white_texture_view.Get();
                    ID3D11ShaderResourceView* const selected_views[] = {
                        texture_view};
                    context->PSSetShaderResources(
                        0U, 1U, selected_views);
                    ID3D11SamplerState* const samplers[] = {
                        material_samplers[draw.material_id].Get()};
                    context->PSSetSamplers(0U, 1U, samplers);
                    context->DrawIndexed(
                        draw.index_count, draw.first_index, 0);
                };

                for (const auto& draw : render_scene_draws) {
                    draw_neutral(
                        draw,
                        render_scene_materials,
                        render_scene_texture_views,
                        render_scene_material_samplers);
                }

                if (has_gameplay_actor && gameplay_presentation) {
                    ID3D11Buffer* const actor_vertex_buffers[] = {
                        gameplay_actor_vertex_buffer.Get()};
                    context->IASetVertexBuffers(
                        0U,
                        1U,
                        actor_vertex_buffers,
                        &kSourceStride,
                        &kOffset);
                    context->IASetIndexBuffer(
                        gameplay_actor_index_buffer.Get(),
                        DXGI_FORMAT_R32_UINT,
                        0U);
                    for (const auto& draw : gameplay_actor_draws) {
                        draw_neutral(
                            draw,
                            gameplay_actor_materials,
                            gameplay_actor_texture_views,
                            gameplay_actor_material_samplers);
                    }
                }
            } else {
            const auto draw_depth_only = [this, &kBlendFactor](
                                             const UINT index_count,
                                             const UINT start_index) {
                if (index_count == 0U) {
                    return;
                }
                context->RSSetState(solid_rasterizer_state.Get());
                context->OMSetDepthStencilState(depth_write_state.Get(), 0U);
                context->OMSetBlendState(
                    no_color_blend_state.Get(),
                    kBlendFactor.data(),
                    kAllSamples);
                context->PSSetShader(nullptr, nullptr, 0U);
                context->DrawIndexed(index_count, start_index, 0);
            };
            const auto draw_wireframe = [this, &kBlendFactor, &no_texture](
                                             const UINT index_count,
                                             const UINT start_index) {
                if (index_count == 0U) {
                    return;
                }
                context->RSSetState(wireframe_rasterizer_state.Get());
                context->OMSetDepthStencilState(depth_read_state.Get(), 0U);
                context->OMSetBlendState(
                    nullptr, kBlendFactor.data(), kAllSamples);
                context->PSSetShader(
                    wireframe_pixel_shader.Get(), nullptr, 0U);
                context->PSSetShaderResources(0U, 1U, no_texture);
                context->DrawIndexed(index_count, start_index, 0);
            };

            if (!texture_regions.empty()) {
                ID3D11SamplerState* const samplers[] = {
                    texture_sampler.Get()};
                context->PSSetSamplers(0U, 1U, samplers);
            }
            const auto draw_material_batches =
                [this,
                 &draw_depth_only,
                 &draw_wireframe,
                 &kBlendFactor](
                    const std::vector<GpuSourceMaterialBatch>& batches,
                    const std::vector<ComPtr<ID3D11ShaderResourceView>>&
                        available_texture_views) {
                for (const auto& batch : batches) {
                    if (!batch.texture_index) {
                        draw_depth_only(batch.index_count, batch.start_index);
                        draw_wireframe(batch.index_count, batch.start_index);
                        continue;
                    }

                    context->RSSetState(solid_rasterizer_state.Get());
                    context->OMSetDepthStencilState(
                        depth_write_state.Get(), 0U);
                    context->OMSetBlendState(
                        nullptr, kBlendFactor.data(), kAllSamples);
                    context->PSSetShader(
                        textured_pixel_shader.Get(), nullptr, 0U);
                    ID3D11ShaderResourceView* const selected_texture_view[] = {
                        available_texture_views[
                            *batch.texture_index].Get()};
                    context->PSSetShaderResources(
                        0U, 1U, selected_texture_view);
                    context->DrawIndexed(
                        batch.index_count, batch.start_index, 0);
                }
            };

            UINT next_uncovered_index = 0U;
            for (const auto& region : texture_regions) {
                if (next_uncovered_index < region.start_index) {
                    const auto gap_index_count =
                        region.start_index - next_uncovered_index;
                    draw_depth_only(
                        gap_index_count, next_uncovered_index);
                    draw_wireframe(
                        gap_index_count, next_uncovered_index);
                }
                draw_material_batches(
                    region.material_batches, region.texture_views);
                next_uncovered_index =
                    region.start_index + region.index_count;
            }
            if (next_uncovered_index < source_index_count) {
                const auto suffix_index_count =
                    source_index_count - next_uncovered_index;
                draw_depth_only(
                    suffix_index_count, next_uncovered_index);
                draw_wireframe(
                    suffix_index_count, next_uncovered_index);
            }
            }
            if (gameplay_presentation && !has_gameplay_actor) {
                constexpr UINT kGameplayProxyStride =
                    sizeof(ProjectedSourceVertex);
                ID3D11Buffer* const gameplay_vertex_buffers[] = {
                    gameplay_proxy_vertex_buffer.Get()};
                context->IASetVertexBuffers(
                    0U,
                    1U,
                    gameplay_vertex_buffers,
                    &kGameplayProxyStride,
                    &kOffset);
                context->IASetIndexBuffer(
                    gameplay_proxy_index_buffer.Get(),
                    DXGI_FORMAT_R32_UINT,
                    0U);
                context->RSSetState(solid_rasterizer_state.Get());
                context->OMSetDepthStencilState(depth_write_state.Get(), 0U);
                context->OMSetBlendState(
                    nullptr, kBlendFactor.data(), kAllSamples);
                context->PSSetShader(
                    wireframe_pixel_shader.Get(), nullptr, 0U);
                context->PSSetShaderResources(0U, 1U, no_texture);
                ID3D11Buffer* const no_proxy_constant_buffer[] = {nullptr};
                context->PSSetConstantBuffers(
                    0U, 1U, no_proxy_constant_buffer);
                context->DrawIndexed(
                    gameplay_proxy_index_count, 0U, 0);
            }
            context->PSSetShaderResources(0U, 1U, no_texture);
            ID3D11Buffer* const no_pixel_constant_buffer[] = {nullptr};
            context->PSSetConstantBuffers(
                0U, 1U, no_pixel_constant_buffer);
            context->OMSetBlendState(
                nullptr, kBlendFactor.data(), kAllSamples);
        }

        const auto result = swap_chain->Present(1U, 0U);
        if (result == DXGI_STATUS_OCCLUDED) {
            return false;
        }
        if (result == DXGI_ERROR_DEVICE_REMOVED ||
            result == DXGI_ERROR_DEVICE_RESET) {
            throw std::runtime_error(
                hresult_message(
                    "The D3D11 device was removed",
                    device->GetDeviceRemovedReason()));
        }
        require_success(result, "IDXGISwapChain::Present");
        return true;
    }

    HWND window = nullptr;
    float minimum_x = 0.0F;
    float maximum_x = 0.0F;
    float minimum_y = 0.0F;
    float maximum_y = 0.0F;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    UINT raster_index_count = 0U;
    UINT source_index_count = 0U;
    UINT gameplay_proxy_index_count = 0U;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;
    FitConstants fit_constants{};
    std::vector<SceneVertex3dV1> source_vertices;
    std::vector<ProjectedSourceVertex> projected_vertices;
    std::vector<ActorSkinnedMeshV1> gameplay_actor_meshes;
    ActorPosePaletteV1 gameplay_actor_bind_palette;
    ActorAffineTransformV1 gameplay_actor_model_to_entity;
    std::vector<std::uint32_t> gameplay_actor_triangle_indices;
    std::vector<RenderSceneD3dDrawV1> gameplay_actor_draws;
    std::vector<RenderSceneD3dMaterialV1> gameplay_actor_materials;
    std::vector<SceneVertex3dV1> gameplay_actor_vertices;
    std::vector<ProjectedSourceVertex> gameplay_actor_projected_vertices;
    std::vector<ComPtr<ID3D11ShaderResourceView>>
        gameplay_actor_texture_views;
    std::vector<ComPtr<ID3D11SamplerState>>
        gameplay_actor_material_samplers;
    std::vector<SceneVertex3dV1> gameplay_proxy_vertices;
    std::vector<ProjectedSourceVertex> gameplay_proxy_projected_vertices;
    std::vector<GpuSourceTextureRegion> texture_regions;
    std::vector<RenderSceneD3dDrawV1> render_scene_draws;
    std::vector<RenderSceneD3dMaterialV1> render_scene_materials;
    std::vector<ComPtr<ID3D11ShaderResourceView>>
        render_scene_texture_views;
    std::vector<ComPtr<ID3D11SamplerState>>
        render_scene_material_samplers;
    std::vector<GpuRenderSceneSampler> render_scene_sampler_cache;
    std::optional<GameplayPresentation> gameplay_presentation;
    Vec3 orbit_target{};
    float orbit_radius = 1.0F;
    float orbit_yaw = 0.0F;
    float orbit_pitch = 0.0F;
    float orbit_distance = 1.0F;
    bool has_source_geometry = false;
    bool has_raster_geometry = false;
    bool show_source_geometry = false;
    bool native_scene_mode = false;
    bool has_gameplay_actor = false;
    bool camera_initialized = false;
    bool projected_vertices_dirty = false;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap_chain;
    ComPtr<ID3D11RenderTargetView> render_target;
    ComPtr<ID3D11Texture2D> depth_texture;
    ComPtr<ID3D11DepthStencilView> depth_view;
    ComPtr<ID3D11VertexShader> raster_vertex_shader;
    ComPtr<ID3D11VertexShader> source_vertex_shader;
    ComPtr<ID3D11PixelShader> wireframe_pixel_shader;
    ComPtr<ID3D11PixelShader> textured_pixel_shader;
    ComPtr<ID3D11PixelShader> render_scene_pixel_shader;
    ComPtr<ID3D11InputLayout> raster_input_layout;
    ComPtr<ID3D11InputLayout> source_input_layout;
    ComPtr<ID3D11RasterizerState> wireframe_rasterizer_state;
    ComPtr<ID3D11RasterizerState> solid_rasterizer_state;
    ComPtr<ID3D11RasterizerState> solid_single_sided_rasterizer_state;
    ComPtr<ID3D11DepthStencilState> depth_disabled_state;
    ComPtr<ID3D11DepthStencilState> depth_write_state;
    ComPtr<ID3D11DepthStencilState> depth_read_state;
    ComPtr<ID3D11BlendState> no_color_blend_state;
    ComPtr<ID3D11SamplerState> texture_sampler;
    ComPtr<ID3D11ShaderResourceView> render_scene_white_texture_view;
    ComPtr<ID3D11Buffer> raster_vertex_buffer;
    ComPtr<ID3D11Buffer> raster_index_buffer;
    ComPtr<ID3D11Buffer> source_vertex_buffer;
    ComPtr<ID3D11Buffer> source_index_buffer;
    ComPtr<ID3D11Buffer> gameplay_actor_vertex_buffer;
    ComPtr<ID3D11Buffer> gameplay_actor_index_buffer;
    ComPtr<ID3D11Buffer> gameplay_proxy_vertex_buffer;
    ComPtr<ID3D11Buffer> gameplay_proxy_index_buffer;
    ComPtr<ID3D11Buffer> fit_buffer;
    ComPtr<ID3D11Buffer> render_scene_material_buffer;
};

D3d11Renderer::D3d11Renderer(HWND window,
                             const openrc::RenderSceneV1& scene)
    : implementation_(std::make_unique<Implementation>(window, scene)) {}

D3d11Renderer::D3d11Renderer(
    HWND window,
    const openrc::RenderSceneV1& scene,
    const openrc::ActorRigV1& player_rig,
    const openrc::ActorModelV1& player_model,
    const openrc::ActorAffineTransformV1& model_to_entity)
    : implementation_(std::make_unique<Implementation>(
          window,
          scene,
          player_rig,
          player_model,
          model_to_entity)) {}

D3d11Renderer::D3d11Renderer(HWND window,
                             const SceneGeometryV1& geometry)
    : implementation_(std::make_unique<Implementation>(window, geometry)) {}

D3d11Renderer::D3d11Renderer(
    HWND window,
    const SceneGeometryV1& raster_geometry,
    const SceneGeometry3dV1& source_geometry)
    : implementation_(std::make_unique<Implementation>(
          window, raster_geometry, source_geometry)) {}

D3d11Renderer::D3d11Renderer(
    HWND window,
    const SceneGeometryV1& raster_geometry,
    const SceneGeometry3dV1& source_geometry,
    const D3d11SourceTextureSourcesV1 textures)
    : implementation_(std::make_unique<Implementation>(
          window, raster_geometry, source_geometry, textures)) {}

D3d11Renderer::~D3d11Renderer() = default;

D3d11Renderer::D3d11Renderer(D3d11Renderer&&) noexcept = default;

D3d11Renderer&
D3d11Renderer::operator=(D3d11Renderer&&) noexcept = default;

void D3d11Renderer::resize(const std::uint32_t width,
                           const std::uint32_t height) {
    implementation_->resize(width, height);
}

bool D3d11Renderer::render() {
    return implementation_->render();
}

void D3d11Renderer::set_gameplay_presentation(
    const game::ThirdPersonCameraViewV1& camera,
    const CollisionVectorV1& feet_position,
    const double facing_yaw_radians,
    const double capsule_radius,
    const double capsule_height) {
    implementation_->set_gameplay_presentation(
        camera,
        feet_position,
        facing_yaw_radians,
        capsule_radius,
        capsule_height);
}

void D3d11Renderer::orbit(
    const float yaw_delta,
    const float pitch_delta) {
    implementation_->orbit(yaw_delta, pitch_delta);
}

void D3d11Renderer::zoom(const float wheel_steps) {
    implementation_->zoom(wheel_steps);
}

void D3d11Renderer::reset_camera() {
    implementation_->reset_camera();
}

void D3d11Renderer::toggle_view_mode() {
    implementation_->toggle_view_mode();
}

bool D3d11Renderer::is_3d_view() const noexcept {
    return implementation_->is_3d_view();
}

} // namespace openrc::runtime
