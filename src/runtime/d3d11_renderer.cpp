#include "d3d11_renderer.hpp"

#include "render_scene_d3d_data.hpp"

#include "generated/render_scene_ps_dxbc.hpp"
#include "generated/screen_overlay_ps_dxbc.hpp"
#include "generated/encoded_affine_vs_dxbc.hpp"
#include "generated/encoded_vs_dxbc.hpp"
#include "generated/encoded_ps_dxbc.hpp"
#include "generated/encoded_affine_ps_dxbc.hpp"
#include "generated/source_textured_ps_dxbc.hpp"
#include "generated/source_textured_vs_dxbc.hpp"
#include "generated/wireframe_ps_dxbc.hpp"

#include "openrc/actor_pose.hpp"
#include "openrc/render_scene.hpp"
#include "openrc/runtime_level_content.hpp"
#include "openrc/runtime_player_actor.hpp"
#include "openrc/runtime_world_actor.hpp"
#include "openrc/third_person_camera.hpp"
#include "openrc/scene_timeline.hpp"
#include "openrc/screen_overlay.hpp"

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
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace openrc::runtime {
namespace {

using Microsoft::WRL::ComPtr;

struct ProjectedVertex {
    std::array<float, 4U> clip_position{};
    std::uint32_t rgba = 0xffffffffU;
    float u = 0.0F;
    float v = 0.0F;
};

static_assert(sizeof(ProjectedVertex) == 28U);

struct alignas(16) RenderSceneMaterialConstants {
    std::array<float, 4U> base_color{};
    float use_vertex_color = 0.0F;
    float alpha_cutoff = -1.0F;
    std::array<float, 2U> padding{};
};

static_assert(sizeof(RenderSceneMaterialConstants) == 32U);
struct alignas(16) EncodedMaterialConstants {
    std::uint32_t modulation_denominator=255,blend_denominator=255,flags=0,alpha_cutoff=0;
    std::uint32_t texture_width=1,texture_height=1,padding0=0,padding1=0;
};
static_assert(sizeof(EncodedMaterialConstants)==32U);

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
constexpr std::uint32_t kGameplayProxyRingSegments = 12U;
constexpr std::uint32_t kGameplayProxyVertexCount =
    2U + 2U * kGameplayProxyRingSegments + 3U;
constexpr std::uint32_t kGameplayProxyIndexCount =
    12U * kGameplayProxyRingSegments + 3U;
constexpr std::uint32_t kMaximumD3d11TextureDimension = 16'384U;
constexpr std::size_t kMaximumD3d11TextureMipCount = 15U;
constexpr std::uint32_t kMaximumD3d11WorldActorInstances = 65'536U;
constexpr std::uint64_t kMaximumD3d11WorldActorVertices = 3'000'000U;
constexpr std::uint64_t kMaximumD3d11WorldActorTriangleIndices = 9'000'000U;
constexpr std::uint64_t kMaximumD3d11WorldActorDraws = 3'000'000U;
constexpr std::uint64_t kMaximumD3d11WorldActorPoseJoints = 1'000'000U;

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
        material.color_math,material.blend_mode,material.interpolation,
        material.depth_test,material.depth_write,
        material.texture_modulation_denominator,material.blend_denominator,
        material.alpha_failure,
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

[[nodiscard]] ActorAffineTransformV1 actor_transform_from_world(
    const game::WorldTransformV1& transform) {
    const auto finite = [](const float value) {
        return std::isfinite(value);
    };
    if (!std::ranges::all_of(transform.position, finite) ||
        !std::ranges::all_of(transform.rotation, finite) ||
        !std::ranges::all_of(transform.scale, finite) ||
        std::ranges::any_of(transform.scale, [](const float value) {
            return value == 0.0F;
        })) {
        throw std::invalid_argument(
            "The D3D11 world actor has an invalid entity transform");
    }
    const auto x = static_cast<double>(transform.rotation[0U]);
    const auto y = static_cast<double>(transform.rotation[1U]);
    const auto z = static_cast<double>(transform.rotation[2U]);
    const auto w = static_cast<double>(transform.rotation[3U]);
    const auto quaternion_length =
        std::hypot(std::hypot(x, y), std::hypot(z, w));
    if (!std::isfinite(quaternion_length) ||
        std::abs(quaternion_length - 1.0) >
            kEntitySceneQuaternionUnitToleranceV1) {
        throw std::invalid_argument(
            "The D3D11 world actor has a non-unit entity rotation");
    }
    const auto sx = static_cast<double>(transform.scale[0U]);
    const auto sy = static_cast<double>(transform.scale[1U]);
    const auto sz = static_cast<double>(transform.scale[2U]);
    const std::array<double, 12U> values{
        (1.0 - 2.0 * (y * y + z * z)) * sx,
        2.0 * (x * y - z * w) * sy,
        2.0 * (x * z + y * w) * sz,
        transform.position[0U],
        2.0 * (x * y + z * w) * sx,
        (1.0 - 2.0 * (x * x + z * z)) * sy,
        2.0 * (y * z - x * w) * sz,
        transform.position[1U],
        2.0 * (x * z - y * w) * sx,
        2.0 * (y * z + x * w) * sy,
        (1.0 - 2.0 * (x * x + y * y)) * sz,
        transform.position[2U],
    };
    ActorAffineTransformV1 result;
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (!std::isfinite(values[index]) ||
            std::abs(values[index]) > std::numeric_limits<float>::max()) {
            throw std::invalid_argument(
                "The D3D11 world actor entity transform overflows float");
        }
        const auto value = static_cast<float>(values[index]);
        result.values[index] = value == 0.0F ? 0.0F : value;
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

[[nodiscard]] Vec3 as_vec3(
    const RenderSceneD3dVertexV1& vertex) noexcept {
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
    explicit Implementation(HWND native_window) : window(native_window) {
        media_mode = true;
        if (!window) throw std::invalid_argument("Media renderer requires a window");
        create_device_and_swap_chain();
        create_pipeline();
        create_render_target();
        RECT client{};
        if (!GetClientRect(window, &client))
            throw std::runtime_error("Cannot obtain media window dimensions");
        set_dimensions(static_cast<std::uint32_t>(std::max<LONG>(0,client.right-client.left)),
                       static_cast<std::uint32_t>(std::max<LONG>(0,client.bottom-client.top)));
    }

    void set_media_frame(std::uint32_t frame_width, std::uint32_t frame_height,
                         std::span<const std::byte> rgba,
                         std::uint32_t aspect_numerator,
                         std::uint32_t aspect_denominator) {
        if (!frame_width || !frame_height || frame_width>kMaximumD3d11TextureDimension || frame_height>kMaximumD3d11TextureDimension ||
            rgba.size()!=static_cast<std::uint64_t>(frame_width)*frame_height*4U ||
            !aspect_numerator || !aspect_denominator)
            throw std::invalid_argument("Invalid prepared media frame");
        submission_drain_covers_latest_work=false;
        media_mode = true;
        if (!media_texture || media_width!=frame_width || media_height!=frame_height) {
            media_view.Reset(); media_texture.Reset();
            D3D11_TEXTURE2D_DESC description{};
            description.Width=frame_width; description.Height=frame_height;
            description.MipLevels=1; description.ArraySize=1;
            description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count=1;
            description.Usage=D3D11_USAGE_DEFAULT;
            description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            require_success(device->CreateTexture2D(&description,nullptr,media_texture.GetAddressOf()),"Create media texture");
            require_success(device->CreateShaderResourceView(media_texture.Get(),nullptr,media_view.GetAddressOf()),"Create media texture view");
            media_width=frame_width; media_height=frame_height;
        }
        if (!media_vertices) {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth=sizeof(ProjectedVertex)*4U;
            desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            require_success(device->CreateBuffer(&desc,nullptr,media_vertices.GetAddressOf()),"Create media vertices");
            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxLOD=D3D11_FLOAT32_MAX;
            require_success(device->CreateSamplerState(&sampler,media_sampler.GetAddressOf()),"Create media sampler");
        }
        context->UpdateSubresource(media_texture.Get(),0,nullptr,rgba.data(),frame_width*4U,0);
        media_aspect=static_cast<double>(aspect_numerator)/aspect_denominator;
    }

    bool render_media() {
        if (!width || !height || !render_target) return false;
        submission_drain_covers_latest_work=false;
        constexpr float black[4]={0,0,0,1};
        context->ClearRenderTargetView(render_target.Get(),black);
        if (!media_texture) {
            const auto hr=swap_chain->Present(1,0);
            if (hr==DXGI_STATUS_OCCLUDED) return false;
            require_success(hr,"Present initial media background");
            return true;
        }
        auto *target=render_target.Get();
        context->OMSetRenderTargets(1,&target,nullptr);
        context->OMSetDepthStencilState(nullptr,0);
        context->OMSetBlendState(nullptr,nullptr,0xffffffffU);
        context->RSSetState(solid_rasterizer_state.Get());
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
        context->RSSetViewports(1,&viewport);
        auto aspect=static_cast<double>(width)/height;
        float x=1, y=1;
        if (aspect>media_aspect) x=static_cast<float>(media_aspect/aspect);
        else y=static_cast<float>(aspect/media_aspect);
        const std::array<ProjectedVertex,4> vertices{{
            {{-x,y,0,1},0xffffffffU,0,0}, {{x,y,0,1},0xffffffffU,1,0},
            {{-x,-y,0,1},0xffffffffU,0,1}, {{x,-y,0,1},0xffffffffU,1,1}}};
        context->UpdateSubresource(media_vertices.Get(),0,nullptr,vertices.data(),0,0);
        UINT stride=sizeof(ProjectedVertex), offset=0;
        auto *buffer=media_vertices.Get();
        context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        context->IASetInputLayout(projected_input_layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(projected_vertex_shader.Get(),nullptr,0);
        // Decoded video is already display-encoded RGB. The scene shader
        // converts linear light to sRGB and would encode these bytes twice.
        context->PSSetShader(media_pixel_shader.Get(),nullptr,0);
        auto *view=media_view.Get(); auto *sampler=media_sampler.Get();
        context->PSSetShaderResources(0,1,&view);
        context->PSSetSamplers(0,1,&sampler);
        context->Draw(4,0);
        ++media_submitted;
        render_screen_overlay();
        capture_back_buffer();
        view=nullptr; context->PSSetShaderResources(0,1,&view);
        auto hr=swap_chain->Present(1,0);
        if (hr==DXGI_STATUS_OCCLUDED) return false;
        require_success(hr,"Present media frame");
        return true;
    }

    struct ScreenOverlayLayerGpu {
        std::uint32_t canvas_width=0,canvas_height=0,coverage_denominator=0;
        std::vector<std::array<std::uint32_t,2>> image_dimensions;
        std::vector<ScreenOverlayFrameV1> frames;
        std::vector<ComPtr<ID3D11ShaderResourceView>> views;
        std::optional<std::uint32_t> frame_index=0U;
    };

    void set_screen_overlay_layers(const std::span<const ScreenOverlayV1> values,
        const ScreenOverlayLimitsV1 limits) {
        auto remaining_bytes=limits.max_bytes;
        std::uint64_t remaining_images=limits.max_images,remaining_frames=limits.max_frames,
            remaining_draws=limits.max_draws;
        const auto consume=[](std::uint64_t& remaining,const std::uint64_t amount) {
            if(amount>remaining)throw ScreenOverlayError("Screen overlay layers exceed aggregate limits");
            remaining-=amount;
        };
        // Validate the whole admission before allocating or replacing GPU
        // resources, including the bytes retained by frame/draw metadata.
        for(const auto& value:values) {
            validate_screen_overlay_v1(value,limits);
            consume(remaining_bytes,96U);
            consume(remaining_images,value.images.size());
            consume(remaining_frames,value.frames.size());
            for(const auto& image:value.images) {
                consume(remaining_bytes,8U);
                consume(remaining_bytes,image.rgb_coverage.size());
            }
            for(const auto& frame:value.frames) {
                consume(remaining_draws,frame.draws.size());
                consume(remaining_bytes,4U+12U*static_cast<std::uint64_t>(frame.draws.size()));
            }
        }
        submission_drain_covers_latest_work=false;
        std::vector<ScreenOverlayLayerGpu> layers;
        layers.reserve(values.size());
        for(const auto& value:values) {
            ScreenOverlayLayerGpu layer;
            layer.canvas_width=value.canvas_width;layer.canvas_height=value.canvas_height;
            layer.coverage_denominator=value.coverage_denominator;layer.frames=value.frames;
            layer.image_dimensions.reserve(value.images.size());layer.views.reserve(value.images.size());
            for(const auto& image:value.images) {
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width=image.width;desc.Height=image.height;
                desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UINT;
                desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;
                desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA data{image.rgb_coverage.data(),image.width*4U,0};
                ComPtr<ID3D11Texture2D> texture;
                require_success(device->CreateTexture2D(&desc,&data,texture.GetAddressOf()),"Create overlay coverage image");
                ComPtr<ID3D11ShaderResourceView> view;
                require_success(device->CreateShaderResourceView(texture.Get(),nullptr,view.GetAddressOf()),"Create overlay coverage view");
                layer.views.push_back(std::move(view));
                layer.image_dimensions.push_back({image.width,image.height});
            }
            layers.push_back(std::move(layer));
        }
        if(!layers.empty()&&!overlay_vertices) {
            ComPtr<ID3D11Buffer> vertices,constants;
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=4U*sizeof(ProjectedVertex);
            desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            require_success(device->CreateBuffer(&desc,nullptr,vertices.GetAddressOf()),"Create overlay vertices");
            desc.ByteWidth=16;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            require_success(device->CreateBuffer(&desc,nullptr,constants.GetAddressOf()),"Create overlay constants");
            overlay_vertices=std::move(vertices);overlay_constants=std::move(constants);
        }
        overlay_layers=std::move(layers);
    }

    void set_screen_overlay_layer_frames(
        const std::span<const std::optional<std::uint32_t>> frame_indices) {
        if(frame_indices.size()!=overlay_layers.size())
            throw std::invalid_argument("Screen overlay layer frame count differs");
        for(std::size_t i=0;i<frame_indices.size();++i)
            if(frame_indices[i]&&*frame_indices[i]>=overlay_layers[i].frames.size())
                throw std::out_of_range("Screen overlay layer frame is absent");
        for(std::size_t i=0;i<frame_indices.size();++i)
            overlay_layers[i].frame_index=frame_indices[i];
    }

    void render_screen_overlay() {
        if(std::none_of(overlay_layers.begin(),overlay_layers.end(),[](const auto& layer) {
            return layer.frame_index&&!layer.frames[*layer.frame_index].draws.empty();
        }))return;
        ComPtr<ID3D11Texture2D> back;
        require_success(swap_chain->GetBuffer(0,IID_PPV_ARGS(back.GetAddressOf())),"Get overlay destination");
        D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);
        if(!overlay_destination||overlay_destination_width!=desc.Width||overlay_destination_height!=desc.Height) {
            overlay_destination_view.Reset();overlay_destination.Reset();
            desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags=0;desc.MiscFlags=0;
            require_success(device->CreateTexture2D(&desc,nullptr,overlay_destination.GetAddressOf()),"Create overlay destination copy");
            require_success(device->CreateShaderResourceView(overlay_destination.Get(),nullptr,overlay_destination_view.GetAddressOf()),"Create overlay destination view");
            overlay_destination_width=desc.Width;overlay_destination_height=desc.Height;
        }
        const auto aspect=media_mode?media_aspect:scene_aspect;
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
        if(static_cast<double>(width)/height>aspect) {
            viewport.Width=static_cast<float>(height*aspect);
            viewport.TopLeftX=(static_cast<float>(width)-viewport.Width)*0.5F;
        } else {
            viewport.Height=static_cast<float>(width/aspect);
            viewport.TopLeftY=(static_cast<float>(height)-viewport.Height)*0.5F;
        }
        context->RSSetViewports(1,&viewport);
        context->RSSetState(solid_rasterizer_state.Get());
        context->OMSetDepthStencilState(nullptr,0);
        context->OMSetBlendState(nullptr,nullptr,0xffffffffU);
        context->IASetInputLayout(projected_input_layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        auto* vertices=overlay_vertices.Get();UINT stride=sizeof(ProjectedVertex),offset=0;
        context->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
        context->VSSetShader(projected_vertex_shader.Get(),nullptr,0);
        context->PSSetShader(overlay_pixel_shader.Get(),nullptr,0);
        auto* constants=overlay_constants.Get();context->PSSetConstantBuffers(0,1,&constants);
        ID3D11ShaderResourceView* empty[2]={nullptr,nullptr};
        for(const auto& layer:overlay_layers) {
          if(!layer.frame_index)continue;
          for(const auto& draw:layer.frames[*layer.frame_index].draws) {
            const auto& image=layer.image_dimensions[draw.image_id];
            const auto left=static_cast<float>(double(draw.x)*2/layer.canvas_width-1);
            const auto right=static_cast<float>((double(draw.x)+image[0])*2/layer.canvas_width-1);
            const auto top=static_cast<float>(1-double(draw.y)*2/layer.canvas_height);
            const auto bottom=static_cast<float>(1-(double(draw.y)+image[1])*2/layer.canvas_height);
            const std::array<ProjectedVertex,4> quad{{
                {{left,top,0,1},0xffffffffU,0,0},{{right,top,0,1},0xffffffffU,1,0},
                {{left,bottom,0,1},0xffffffffU,0,1},{{right,bottom,0,1},0xffffffffU,1,1}}};
            const std::array<std::uint32_t,4> values{layer.coverage_denominator,image[0],image[1],0};
            context->UpdateSubresource(overlay_vertices.Get(),0,nullptr,quad.data(),0,0);
            context->UpdateSubresource(overlay_constants.Get(),0,nullptr,values.data(),0,0);
            // Preserve each preceding draw in order. A separate copy avoids
            // reading an RTV while it is bound for output.
            context->PSSetShaderResources(0,2,empty);
            context->OMSetRenderTargets(0,nullptr,nullptr);
            context->CopyResource(overlay_destination.Get(),back.Get());
            auto* target=render_target.Get();context->OMSetRenderTargets(1,&target,nullptr);
            ID3D11ShaderResourceView* selected[2]={overlay_destination_view.Get(),layer.views[draw.image_id].Get()};
            context->PSSetShaderResources(0,2,selected);
            context->Draw(4,0);
          }
        }
        context->PSSetShaderResources(0,2,empty);
        ID3D11Buffer* no_constants=nullptr;context->PSSetConstantBuffers(0,1,&no_constants);
    }

    void capture_back_buffer() {
        if (media_capture_requested) {
            ComPtr<ID3D11Texture2D> back;
            require_success(swap_chain->GetBuffer(0,IID_PPV_ARGS(back.GetAddressOf())),"Read media back buffer");
            D3D11_TEXTURE2D_DESC description{};
            back->GetDesc(&description);
            description.Usage=D3D11_USAGE_STAGING;
            description.BindFlags=0; description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            description.MiscFlags=0;
            ComPtr<ID3D11Texture2D> staging;
            require_success(device->CreateTexture2D(&description,nullptr,staging.GetAddressOf()),"Create media readback");
            context->CopyResource(staging.Get(),back.Get());
            media_capture.resize(static_cast<std::size_t>(width)*height*4U);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require_success(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map media readback");
            for(std::uint32_t y=0;y<height;++y)
                std::memcpy(media_capture.data()+static_cast<std::size_t>(y)*width*4U,
                    static_cast<const std::byte*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch,width*4U);
            context->Unmap(staging.Get(),0);
            media_capture_requested=false;
        }
    }

    void ensure_encoded_destination(ID3D11Texture2D* back) {
        D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);
        if(overlay_destination&&overlay_destination_width==desc.Width&&overlay_destination_height==desc.Height) return;
        overlay_destination_view.Reset();overlay_destination.Reset();
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags=0;desc.MiscFlags=0;
        require_success(device->CreateTexture2D(&desc,nullptr,overlay_destination.GetAddressOf()),"Create encoded destination copy");
        require_success(device->CreateShaderResourceView(overlay_destination.Get(),nullptr,overlay_destination_view.GetAddressOf()),"Create encoded destination view");
        overlay_destination_width=desc.Width;overlay_destination_height=desc.Height;
    }

    D3D11_BOX encoded_destination_region(std::span<const ProjectedVertex> vertices,
        std::span<const std::uint32_t> indices,std::uint32_t first,
        const D3D11_VIEWPORT& viewport) const {
        if(first>indices.size()||indices.size()-first<3)
            throw std::logic_error("Encoded draw has no matching CPU index ownership");
        double left=std::numeric_limits<double>::infinity(),top=left;
        double right=-left,bottom=-left;
        unsigned positive_w=0;
        for(unsigned i=0;i<3;++i) {
            if(indices[first+i]>=vertices.size())
                throw std::logic_error("Encoded draw has no matching projected vertex");
            const auto& p=vertices[indices[first+i]].clip_position;
            if(p[3]<=0)continue;
            ++positive_w;
            const auto x=viewport.TopLeftX+(double(p[0])/p[3]+1)*viewport.Width/2;
            const auto y=viewport.TopLeftY+(1-double(p[1])/p[3])*viewport.Height/2;
            left=std::min(left,x);right=std::max(right,x);
            top=std::min(top,y);bottom=std::max(bottom,y);
        }
        if(!positive_w)return {};
        // A triangle crossing the eye plane can grow after clipping. Retain
        // the full framebuffer in that case. With positive W, projected
        // coverage lies in the vertex bounds; a pixel of padding covers the
        // rasterizer's subpixel snap as well as pixel-center conventions.
        if(positive_w!=3)return {0,0,0,width,height,1};
        const auto bound=[](double v,UINT extent){return static_cast<UINT>(std::clamp(v,0.0,double(extent)));};
        return {bound(std::floor(left)-1,width),bound(std::floor(top)-1,height),0,
            bound(std::ceil(right)+1,width),bound(std::ceil(bottom)+1,height),1};
    }

    void draw_encoded(const RenderSceneD3dDrawV1& draw,
        const RenderSceneD3dMaterialV1& material,ID3D11ShaderResourceView* texture,
        std::span<const ProjectedVertex> vertices,std::span<const std::uint32_t> indices,
        const D3D11_VIEWPORT& viewport) {
        EncodedMaterialConstants constants;
        constants.modulation_denominator=material.texture_modulation_denominator;
        constants.blend_denominator=material.blend_denominator;
        const bool blended=material.blend_mode==RenderSceneBlendModeV1::source_over;
        constants.flags=(material.base_color_texture_id?1U:0U)|(blended?2U:0U)|
            (material.mag_filter==RenderSceneFilterV1::linear?4U:0U)|
            (material.address_u==RenderSceneAddressModeV1::repeat?8U:0U)|
            (material.address_v==RenderSceneAddressModeV1::repeat?16U:0U)|
            (material.alpha_cutoff>=0?32U:0U);
        constants.alpha_cutoff=material.alpha_cutoff>=0?static_cast<std::uint32_t>(std::lround(material.alpha_cutoff*255)):0;
        ComPtr<ID3D11Resource> resource;texture->GetResource(resource.GetAddressOf());
        ComPtr<ID3D11Texture2D> image;
        require_success(resource.As(&image),"Read encoded texture dimensions");
        D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);
        constants.texture_width=desc.Width;constants.texture_height=desc.Height;
        context->UpdateSubresource(render_scene_material_buffer.Get(),0,nullptr,&constants,0,0);
        const bool affine=material.interpolation==RenderSceneInterpolationV1::affine;
        context->VSSetShader(affine?encoded_affine_vertex_shader.Get():encoded_vertex_shader.Get(),nullptr,0);
        context->PSSetShader(affine?encoded_affine_pixel_shader.Get():encoded_pixel_shader.Get(),nullptr,0);
        auto* buffer=render_scene_material_buffer.Get();context->PSSetConstantBuffers(0,1,&buffer);
        context->PSSetShaderResources(0,1,&texture);
        const bool keep_failed_rgb=material.alpha_failure==RenderSceneAlphaFailureV1::rgb_only;
        if(!blended&&!keep_failed_rgb) {context->DrawIndexed(draw.index_count,draw.first_index,0);return;}
        ComPtr<ID3D11Texture2D> back;
        if(blended) {
            require_success(swap_chain->GetBuffer(0,IID_PPV_ARGS(back.GetAddressOf())),"Get encoded draw destination");
            ensure_encoded_destination(back.Get());
        }
        // A snapshot once per mesh would read stale pixels for overlapping
        // later primitives. Preserve their order; batching requires proof of
        // disjoint raster regions and must not change this contract.
        ID3D11ShaderResourceView* empty=nullptr;
        auto* target=render_target.Get();auto* destination=overlay_destination_view.Get();
        const auto depth_policy=(material.depth_test==RenderSceneDepthTestV1::always?2U:0U)|
            (material.depth_write?0U:1U);
        for(std::uint32_t index=0;index<draw.index_count;index+=3) {
            if(blended) {
                const auto region=encoded_destination_region(vertices,indices,draw.first_index+index,viewport);
                if(region.left>=region.right||region.top>=region.bottom)continue;
                context->PSSetShaderResources(1,1,&empty);
                context->OMSetRenderTargets(0,nullptr,nullptr);
                context->CopySubresourceRegion(overlay_destination.Get(),0,region.left,region.top,0,back.Get(),0,&region);
                context->OMSetRenderTargets(1,&target,depth_view.Get());
                context->PSSetShaderResources(1,1,&destination);
            }
            if(keep_failed_rgb) {
                // The two predicates are disjoint and share this primitive's
                // destination snapshot. Failed alpha still depth-tests, but
                // cannot occlude later primitives or replace destination A.
                constants.flags|=64U;
                context->UpdateSubresource(render_scene_material_buffer.Get(),0,nullptr,&constants,0,0);
                context->OMSetDepthStencilState(scene_depth_states[depth_policy|1U].Get(),0);
                context->OMSetBlendState(rgb_write_state.Get(),nullptr,0xffffffffU);
                context->DrawIndexed(3,draw.first_index+index,0);
                constants.flags&=~64U;
                context->UpdateSubresource(render_scene_material_buffer.Get(),0,nullptr,&constants,0,0);
                context->OMSetDepthStencilState(scene_depth_states[depth_policy].Get(),0);
                context->OMSetBlendState(nullptr,nullptr,0xffffffffU);
            }
            context->DrawIndexed(3,draw.first_index+index,0);
        }
        context->PSSetShaderResources(1,1,&empty);
    }

    std::uint64_t begin_submission_drain() {
        if(submission_drain_token==UINT64_MAX)
            throw std::overflow_error("GPU submission drain token exhausted");
        ComPtr<ID3D11Query> query;
        const D3D11_QUERY_DESC description{D3D11_QUERY_EVENT,0};
        require_success(device->CreateQuery(&description,query.GetAddressOf()),"Create GPU completion event");
        context->End(query.Get());
        context->Flush();
        submission_drain_query=std::move(query);
        submission_drain_observed_complete=false;
        submission_drain_covers_latest_work=true;
        submission_drain_retirement_consumed=false;
        return ++submission_drain_token;
    }
    bool submission_drain_completed(std::uint64_t token) {
        if(!token||token!=submission_drain_token||!submission_drain_query)
            throw std::invalid_argument("GPU submission drain has a stale or missing token");
        BOOL completed=FALSE;
        const auto status=context->GetData(submission_drain_query.Get(),&completed,sizeof(completed),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(status==S_FALSE)return false;
        require_success(status,"Read GPU completion event");
        submission_drain_observed_complete=completed!=FALSE;
        return completed!=FALSE;
    }
    bool submission_drain_covers_current_work(std::uint64_t token) const {
        if(!token||token!=submission_drain_token||!submission_drain_query)
            throw std::invalid_argument("GPU completion coverage has a stale or missing drain token");
        if(!submission_drain_observed_complete)
            throw std::logic_error("GPU completion coverage requires observed completion");
        return submission_drain_covers_latest_work;
    }

    bool try_retire_submission_drain(std::uint64_t token) {
        if(!submission_drain_covers_current_work(token))return false;
        submission_drain_query.Reset();
        submission_drain_observed_complete=false;
        submission_drain_covers_latest_work=false;
        return true;
    }

    bool try_retire_scene_for_media(std::uint64_t token) {
        if(has_gameplay_actor||gameplay_presentation)
            throw std::logic_error("Cannot retire an active gameplay scene as cinematic media");
        if(!media_mode||!media_texture||!media_view)
            throw std::logic_error("Cinematic retirement requires an admitted frozen media frame");
        if(submission_drain_retirement_consumed)
            throw std::invalid_argument("Cinematic retirement already consumed its GPU drain token");
        if(!submission_drain_covers_current_work(token))return false;

        // The context retains bindings independently of our COM owners.
        // Clear those references after the observed event, then release the
        // cinematic allocations. Both presentation paths bind their complete
        // pipeline again; shared shaders, device and swap chain remain live.
        context->ClearState();
        const auto release=[](auto& values) {
            std::decay_t<decltype(values)>{}.swap(values);
        };
        render_scene_vertex_buffer.Reset();render_scene_index_buffer.Reset();
        release(render_scene_vertices);release(render_scene_projection_flags);
        release(render_scene_projected_vertices);release(render_scene_triangle_indices);
        release(render_scene_draws);release(render_scene_materials);
        release(render_scene_instance_enabled);release(render_scene_instance_submitted);
        release(render_scene_texture_views);release(render_scene_material_samplers);
        release(render_scene_sampler_cache);
        release(world_actor_instances);release(world_actor_models);
        release(overlay_layers);
        overlay_destination_view.Reset();overlay_destination.Reset();
        overlay_destination_width=overlay_destination_height=0;
        overlay_vertices.Reset();overlay_constants.Reset();
        scene_camera.reset();
        has_render_scene_geometry=false;projected_vertices_dirty=false;
        // D3D11 defers destruction even after reference release. This flush
        // retires those objects; it is not the GPU completion proof above.
        context->Flush();
        submission_drain_covers_latest_work=false;
        submission_drain_retirement_consumed=true;
        return true;
    }
    void retire_scene_for_media(std::uint64_t token) {
        if(!try_retire_scene_for_media(token))
            throw std::logic_error("Cinematic retirement requires GPU completion after the latest work");
    }

    void set_scene_actors(const ActorLibraryV1& library,
        std::span<const game::RuntimeWorldActorResolutionV1> actors) {
        if (has_gameplay_actor || gameplay_presentation)
            throw std::logic_error("Cannot replace an active gameplay scene with a cinematic");
        submission_drain_covers_latest_work=false;
        world_actor_instances.clear();
        world_actor_models.clear();
        initialize_world_actors(library,actors);
        create_world_actor_buffers();
        create_world_actor_resources();
        if (!render_scene_white_texture_view) create_render_scene_white_texture();
        media_mode=false;
        projected_vertices_dirty=true;
    }

    void set_scene_geometry(const RenderSceneV1& scene) {
        if (has_gameplay_actor || gameplay_presentation)
            throw std::logic_error("Cannot replace an active gameplay scene with a cinematic");
        auto flattened=build_render_scene_d3d_data_v1(scene);
        submission_drain_covers_latest_work=false;
        // Static scene allocation is independent from cinematic actor ownership.
        // Empty replacement must retire previous buffers and visibility too.
        render_scene_vertex_buffer.Reset();render_scene_index_buffer.Reset();
        render_scene_vertices.clear();render_scene_projected_vertices.clear();
        render_scene_triangle_indices.clear();
        render_scene_projection_flags.clear();
        render_scene_texture_views.clear();render_scene_material_samplers.clear();
        has_render_scene_geometry=false;
        initialize_render_scene_geometry(flattened);
        render_scene_draws=std::move(flattened.draws);
        render_scene_materials=std::move(flattened.materials);
        render_scene_instance_enabled.assign(scene.instances.size(),true);
        render_scene_instance_submitted.assign(scene.instances.size(),false);
        if(has_render_scene_geometry)
            create_render_scene_geometry_buffers(flattened.triangle_indices);
        create_render_scene_resources(scene);
        media_mode=false;projected_vertices_dirty=true;
    }

    void set_gameplay_scene(const RenderSceneV1& scene,const ActorLibraryV1& library,
        const game::RuntimePlayerActorResolutionV1& player,
        std::span<const game::RuntimeWorldActorResolutionV1> actors) {
        if(has_gameplay_actor||gameplay_presentation)
            throw std::logic_error("First gameplay admission already completed");
        if(player.actor_rig_index>=library.rigs.size()||player.actor_model_index>=library.models.size())
            throw std::invalid_argument("Gameplay admission player binding exceeds its prepared library");
        const auto& rig=library.rigs[player.actor_rig_index];
        const auto& model=library.models[player.actor_model_index];
        if(model.rig_key!=rig.semantic_key)
            throw std::invalid_argument("Gameplay admission player model refers to a different rig");
        set_scene_geometry(scene);
        set_scene_actors(library,actors);
        initialize_gameplay_actor(rig.rig,model,player.model_to_entity);
        create_gameplay_actor_buffers();
        create_gameplay_actor_resources(model);
        overlay_layers.clear();
        scene_camera.reset();
        media_mode=false;projected_vertices_dirty=true;
    }

    void set_scene_camera(const SceneCameraV1& camera,
        std::uint32_t aspect_numerator, std::uint32_t aspect_denominator,
        const std::array<float,4>& clear_color) {
        validate_scene_camera_v1(camera);
        if (!aspect_numerator || !aspect_denominator || aspect_numerator>65535 || aspect_denominator>65535 ||
            !std::ranges::all_of(clear_color,[](float v){return std::isfinite(v)&&v>=0&&v<=1;}))
            throw std::invalid_argument("Invalid cinematic aspect or clear color");
        scene_camera=camera;
        scene_aspect=static_cast<double>(aspect_numerator)/aspect_denominator;
        scene_clear_color=clear_color;
        media_mode=false;
        projected_vertices_dirty=true;
    }
    struct WorldActorModelGpu {
        std::uint32_t actor_model_index = 0U;
        std::uint32_t actor_rig_index = 0U;
        std::vector<ActorSkinnedMeshV1> meshes;
        std::vector<std::uint32_t> triangle_indices;
        std::vector<std::uint32_t> reversed_triangle_indices;
        std::vector<RenderSceneD3dDrawV1> draws;
        std::vector<RenderSceneD3dMaterialV1> materials;
        std::vector<RenderSceneTextureV1> textures;
        std::vector<ComPtr<ID3D11ShaderResourceView>> texture_views;
        std::vector<ComPtr<ID3D11SamplerState>> material_samplers;
        ComPtr<ID3D11Buffer> index_buffer;
        ComPtr<ID3D11Buffer> reversed_index_buffer;
        std::size_t vertex_count = 0U;
    };

    struct WorldActorInstanceGpu {
        std::uint32_t authored_id = 0U;
        std::size_t model_gpu_index = 0U;
        ActorPosePaletteV1 pose_palette;
        ActorAffineTransformV1 model_to_entity;
        ActorAffineTransformV1 entity_to_world;
        std::optional<SceneCameraV1> camera;
        std::vector<RenderSceneD3dVertexV1> vertices;
        std::vector<ProjectedVertex> projected_vertices;
        ComPtr<ID3D11Buffer> vertex_buffer;
        bool reverses_orientation = false;
        bool enabled = false;
        bool vertices_dirty = true;
        bool submitted = false;
    };

    explicit Implementation(HWND native_window,
                             const RenderSceneV1& scene)
        : Implementation(native_window, scene, nullptr, nullptr, {}) {}

    Implementation(HWND native_window,
                   const RenderSceneV1& scene,
                   const ActorLibraryV1& actor_library,
                   const game::RuntimePlayerActorResolutionV1& player_actor,
                   const std::span<
                       const game::RuntimeWorldActorResolutionV1> world_actors)
        : Implementation(native_window,
                         scene,
                         &actor_library,
                         &player_actor,
                         world_actors) {}

    Implementation(HWND native_window,
                   const RenderSceneV1& scene,
                   const ActorLibraryV1* const actor_library,
                   const game::RuntimePlayerActorResolutionV1* const
                       player_actor,
                   const std::span<
                       const game::RuntimeWorldActorResolutionV1> world_actors)
        : window(native_window) {
        if (window == nullptr) {
            throw std::invalid_argument(
                "The D3D11 renderer requires a valid window handle");
        }
        if ((actor_library == nullptr) != (player_actor == nullptr) ||
            (actor_library == nullptr && !world_actors.empty())) {
            throw std::invalid_argument(
                "The D3D11 actor path requires a complete library and player resolution");
        }

        auto flattened = build_render_scene_d3d_data_v1(scene);
        initialize_render_scene_geometry(flattened);
        render_scene_draws = std::move(flattened.draws);
        render_scene_materials = std::move(flattened.materials);
        render_scene_instance_enabled.assign(scene.instances.size(), true);
        render_scene_instance_submitted.assign(scene.instances.size(), false);
        const ActorModelV1* player_model = nullptr;
        if (actor_library != nullptr) {
            if (player_actor->actor_rig_index >= actor_library->rigs.size() ||
                player_actor->actor_model_index >=
                    actor_library->models.size()) {
                throw std::invalid_argument(
                    "The D3D11 player actor resolution exceeds its library");
            }
            const auto& player_rig =
                actor_library->rigs[player_actor->actor_rig_index];
            player_model =
                &actor_library->models[player_actor->actor_model_index];
            initialize_gameplay_actor(
                player_rig.rig,
                *player_model,
                player_actor->model_to_entity);
            initialize_world_actors(*actor_library, world_actors);
        }

        create_device_and_swap_chain();
        create_pipeline();
        if (has_render_scene_geometry) {
            create_render_scene_geometry_buffers(flattened.triangle_indices);
        }
        if (has_gameplay_actor) {
            create_gameplay_actor_buffers();
        }
        create_world_actor_buffers();
        create_render_scene_resources(scene);
        if (has_gameplay_actor) {
            create_gameplay_actor_resources(*player_model);
        }
        create_world_actor_resources();
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

        render_scene_vertices = geometry.vertices;
        render_scene_projection_flags=geometry.vertex_projection_flags;
        render_scene_projected_vertices.resize(render_scene_vertices.size());
        for (std::size_t index = 0U;
             index < render_scene_vertices.size(); ++index) {
            render_scene_projected_vertices[index].rgba =
                render_scene_vertices[index].rgba8;
            render_scene_projected_vertices[index].u =
                render_scene_vertices[index].u;
            render_scene_projected_vertices[index].v =
                render_scene_vertices[index].v;
        }
        has_render_scene_geometry = true;
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

        gameplay_actor_pose_palette =
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

    [[nodiscard]] std::size_t initialize_world_actor_model(
        const ActorLibraryV1& library,
        const game::RuntimeWorldActorResolutionV1& actor,
        std::vector<std::optional<std::size_t>>& model_gpu_indices) {
        if (actor.actor_model_index >= library.models.size() ||
            actor.actor_rig_index >= library.rigs.size() ||
            model_gpu_indices.size() != library.models.size()) {
            throw std::invalid_argument(
                "A D3D11 world actor resolution exceeds its actor library");
        }
        const auto existing = model_gpu_indices[actor.actor_model_index];
        if (existing) {
            if (*existing >= world_actor_models.size() ||
                world_actor_models[*existing].actor_rig_index !=
                    actor.actor_rig_index) {
                throw std::invalid_argument(
                    "One D3D11 world actor model resolved to multiple rigs");
            }
            return *existing;
        }
        const auto& model = library.models[actor.actor_model_index];
        const auto& rig = library.rigs[actor.actor_rig_index];
        if (model.id != actor.actor_model_index ||
            rig.id != actor.actor_rig_index ||
            model.rig_key != rig.semantic_key || model.meshes.empty() ||
            model.materials.empty()) {
            throw std::invalid_argument(
                "A D3D11 world actor has an inconsistent model/rig binding");
        }
        if (model.textures.size() >
                std::numeric_limits<std::uint32_t>::max() ||
            model.materials.size() >
                std::numeric_limits<std::uint32_t>::max() ||
            model.meshes.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error(
                "A D3D11 world actor model exceeds the renderer ID domain");
        }
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
                    "A D3D11 world actor has an invalid material policy");
            }
            if (material.base_color_texture_id) {
                const auto texture_id = *material.base_color_texture_id;
                if (texture_id >= model.textures.size() ||
                    (material.mipmap_filter !=
                         RenderSceneMipmapFilterV1::none &&
                     model.textures[texture_id].mips.size() < 2U)) {
                    throw std::invalid_argument(
                        "A D3D11 world actor material references unavailable texture data");
                }
            }
        }

        std::uint64_t vertex_count = 0U;
        std::uint64_t index_count = 0U;
        std::uint64_t draw_count = 0U;
        for (std::size_t mesh_index = 0U;
             mesh_index < model.meshes.size(); ++mesh_index) {
            const auto& mesh = model.meshes[mesh_index];
            if (mesh.id != mesh_index || mesh.vertices.empty() ||
                mesh.triangle_indices.empty() || mesh.draw_ranges.empty() ||
                (mesh.triangle_indices.size() % 3U) != 0U ||
                mesh.vertices.size() >
                    game::kRuntimePlayerActorMaximumVerticesV1 -
                        vertex_count ||
                mesh.triangle_indices.size() >
                    game::kRuntimePlayerActorMaximumTriangleIndicesV1 -
                        index_count ||
                mesh.draw_ranges.size() >
                    game::kRuntimePlayerActorMaximumDrawRangesV1 -
                        draw_count) {
                throw std::invalid_argument(
                    "A D3D11 world actor has an invalid mesh envelope");
            }
            vertex_count += mesh.vertices.size();
            index_count += mesh.triangle_indices.size();
            draw_count += mesh.draw_ranges.size();
            for (const auto index : mesh.triangle_indices) {
                if (index >= mesh.vertices.size()) {
                    throw std::invalid_argument(
                        "A D3D11 world actor contains an invalid mesh index");
                }
            }
            std::uint64_t expected_first_index = 0U;
            for (const auto& draw : mesh.draw_ranges) {
                if (draw.material_id >= model.materials.size() ||
                    draw.first_index != expected_first_index ||
                    draw.index_count == 0U ||
                    (draw.index_count % 3U) != 0U ||
                    draw.index_count >
                        mesh.triangle_indices.size() - expected_first_index) {
                    throw std::invalid_argument(
                        "A D3D11 world actor draw partition is invalid");
                }
                expected_first_index += draw.index_count;
            }
            if (expected_first_index != mesh.triangle_indices.size()) {
                throw std::invalid_argument(
                    "A D3D11 world actor draw partition is incomplete");
            }
        }
        if (vertex_count == 0U || index_count == 0U || draw_count == 0U ||
            vertex_count > std::numeric_limits<std::size_t>::max() ||
            index_count > std::numeric_limits<UINT>::max()) {
            throw std::invalid_argument(
                "A D3D11 world actor has an invalid draw envelope");
        }

        WorldActorModelGpu gpu;
        gpu.actor_model_index = actor.actor_model_index;
        gpu.actor_rig_index = actor.actor_rig_index;
        gpu.vertex_count = static_cast<std::size_t>(vertex_count);
        gpu.meshes = model.meshes;
        gpu.textures = model.textures;
        gpu.materials.reserve(model.materials.size());
        for (const auto& material : model.materials) {
            gpu.materials.push_back(make_gpu_material(material));
        }
        gpu.triangle_indices.reserve(static_cast<std::size_t>(index_count));
        gpu.reversed_triangle_indices.reserve(
            static_cast<std::size_t>(index_count));
        gpu.draws.reserve(static_cast<std::size_t>(draw_count));
        std::uint32_t vertex_base = 0U;
        for (const auto& mesh : model.meshes) {
            const auto index_base = static_cast<std::uint32_t>(
                gpu.triangle_indices.size());
            for (std::size_t first = 0U;
                 first < mesh.triangle_indices.size(); first += 3U) {
                const auto a = vertex_base + mesh.triangle_indices[first];
                const auto b = vertex_base + mesh.triangle_indices[first + 1U];
                const auto c = vertex_base + mesh.triangle_indices[first + 2U];
                gpu.triangle_indices.insert(
                    gpu.triangle_indices.end(), {a, b, c});
                gpu.reversed_triangle_indices.insert(
                    gpu.reversed_triangle_indices.end(), {a, c, b});
            }
            for (const auto& draw : mesh.draw_ranges) {
                gpu.draws.push_back(RenderSceneD3dDrawV1{
                    draw.material_id,
                    index_base + static_cast<std::uint32_t>(draw.first_index),
                    static_cast<std::uint32_t>(draw.index_count),
                });
            }
            vertex_base += static_cast<std::uint32_t>(mesh.vertices.size());
        }
        world_actor_models.push_back(std::move(gpu));
        const auto gpu_index = world_actor_models.size() - 1U;
        model_gpu_indices[actor.actor_model_index] = gpu_index;
        return gpu_index;
    }

    void initialize_world_actors(
        const ActorLibraryV1& library,
        const std::span<const game::RuntimeWorldActorResolutionV1> actors) {
        if (actors.size() > kMaximumD3d11WorldActorInstances) {
            throw std::runtime_error(
                "The D3D11 world actor instance count exceeds its explicit limit");
        }
        std::uint64_t aggregate_vertices = 0U;
        std::uint64_t aggregate_indices = 0U;
        std::uint64_t aggregate_draws = 0U;
        std::uint64_t aggregate_pose_joints = 0U;
        std::optional<std::uint32_t> previous_authored_id;
        std::vector<std::optional<std::size_t>> model_gpu_indices(
            library.models.size());
        world_actor_instances.reserve(actors.size());
        for (const auto& actor : actors) {
            if (previous_authored_id &&
                actor.authored_id <= *previous_authored_id) {
                throw std::invalid_argument(
                    "D3D11 world actors are not in canonical authored-ID order");
            }
            previous_authored_id = actor.authored_id;
            const auto model_gpu_index =
                initialize_world_actor_model(
                    library, actor, model_gpu_indices);
            const auto& model_gpu = world_actor_models[model_gpu_index];
            const auto& rig = library.rigs[model_gpu.actor_rig_index].rig;
            if (model_gpu.vertex_count >
                    kMaximumD3d11WorldActorVertices - aggregate_vertices ||
                model_gpu.triangle_indices.size() >
                    kMaximumD3d11WorldActorTriangleIndices -
                        aggregate_indices ||
                model_gpu.draws.size() >
                    kMaximumD3d11WorldActorDraws - aggregate_draws) {
                throw std::runtime_error(
                    "The D3D11 world actor scene exceeds its aggregate geometry limits");
            }
            if (rig.joints.size() >
                kMaximumD3d11WorldActorPoseJoints -
                    aggregate_pose_joints) {
                throw std::runtime_error(
                    "The D3D11 world actor scene exceeds its aggregate pose-palette limit");
            }
            aggregate_vertices += model_gpu.vertex_count;
            aggregate_indices += model_gpu.triangle_indices.size();
            aggregate_draws += model_gpu.draws.size();
            aggregate_pose_joints += rig.joints.size();

            WorldActorInstanceGpu instance;
            instance.authored_id = actor.authored_id;
            instance.model_gpu_index = model_gpu_index;
            instance.pose_palette = build_actor_bind_pose_palette_v1(
                rig, game::kRuntimePlayerActorPoseLimitsV1);
            instance.model_to_entity = actor.model_to_entity;
            instance.entity_to_world =
                actor_transform_from_world(actor.entity_to_world);
            const auto model_to_world = compose_actor_transform(
                instance.entity_to_world, instance.model_to_entity);
            instance.reverses_orientation =
                checked_actor_reverses_orientation(model_to_world);
            instance.vertices.resize(model_gpu.vertex_count);
            instance.projected_vertices.resize(model_gpu.vertex_count);
            instance.enabled = actor.initially_enabled;
            world_actor_instances.push_back(std::move(instance));
        }
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
        require_success(device->CreatePixelShader(g_openrc_screen_overlay_ps,
            sizeof(g_openrc_screen_overlay_ps),nullptr,overlay_pixel_shader.GetAddressOf()),
            "Create screen overlay shader");
        require_success(device->CreateVertexShader(g_openrc_encoded_vs,sizeof(g_openrc_encoded_vs),
            nullptr,encoded_vertex_shader.GetAddressOf()),"Create encoded vertex shader");
        require_success(device->CreateVertexShader(g_openrc_encoded_affine_vs,sizeof(g_openrc_encoded_affine_vs),
            nullptr,encoded_affine_vertex_shader.GetAddressOf()),"Create encoded affine vertex shader");
        require_success(device->CreatePixelShader(g_openrc_encoded_ps,sizeof(g_openrc_encoded_ps),
            nullptr,encoded_pixel_shader.GetAddressOf()),"Create encoded pixel shader");
        require_success(device->CreatePixelShader(g_openrc_encoded_affine_ps,sizeof(g_openrc_encoded_affine_ps),
            nullptr,encoded_affine_pixel_shader.GetAddressOf()),"Create encoded affine pixel shader");
        require_success(
            device->CreatePixelShader(g_openrc_source_textured_ps,
                sizeof(g_openrc_source_textured_ps),nullptr,
                media_pixel_shader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader(media)");
        require_success(
            device->CreatePixelShader(
                g_openrc_wireframe_ps,
                sizeof(g_openrc_wireframe_ps),
                nullptr,
                gameplay_proxy_pixel_shader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader(gameplay proxy)");
        require_success(
            device->CreateVertexShader(
                g_openrc_source_textured_vs,
                sizeof(g_openrc_source_textured_vs),
                nullptr,
                projected_vertex_shader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader(projected RenderSceneV1)");
        require_success(
            device->CreatePixelShader(
                g_openrc_render_scene_ps,
                sizeof(g_openrc_render_scene_ps),
                nullptr,
                render_scene_pixel_shader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader(RenderSceneV1)");

        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 3U>
            kProjectedInputElements{{
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
                 static_cast<UINT>(offsetof(ProjectedVertex, rgba)),
                 D3D11_INPUT_PER_VERTEX_DATA,
                 0U},
                {"TEXCOORD",
                 0U,
                 DXGI_FORMAT_R32G32_FLOAT,
                 0U,
                 static_cast<UINT>(offsetof(ProjectedVertex, u)),
                 D3D11_INPUT_PER_VERTEX_DATA,
                 0U},
            }};
        require_success(
            device->CreateInputLayout(
                kProjectedInputElements.data(),
                static_cast<UINT>(kProjectedInputElements.size()),
                g_openrc_source_textured_vs,
                sizeof(g_openrc_source_textured_vs),
                projected_input_layout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout(projected RenderSceneV1)");

        D3D11_RASTERIZER_DESC rasterizer_description{};
        rasterizer_description.FillMode = D3D11_FILL_SOLID;
        rasterizer_description.CullMode = D3D11_CULL_NONE;
        rasterizer_description.DepthClipEnable = TRUE;
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
        depth_description.DepthEnable = TRUE;
        depth_description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth_description.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        require_success(
            device->CreateDepthStencilState(
                &depth_description, depth_write_state.GetAddressOf()),
            "ID3D11Device::CreateDepthStencilState(write)");
        for(std::uint32_t policy=0;policy<4;++policy) {
            depth_description.DepthFunc=(policy&2)?D3D11_COMPARISON_ALWAYS:D3D11_COMPARISON_LESS_EQUAL;
            depth_description.DepthWriteMask=(policy&1)?D3D11_DEPTH_WRITE_MASK_ZERO:D3D11_DEPTH_WRITE_MASK_ALL;
            require_success(device->CreateDepthStencilState(&depth_description,scene_depth_states[policy].GetAddressOf()),"Create neutral material depth state");
        }
        D3D11_BLEND_DESC rgb_description{};
        rgb_description.RenderTarget[0].RenderTargetWriteMask=
            D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
        require_success(device->CreateBlendState(&rgb_description,rgb_write_state.GetAddressOf()),
            "Create RGB-only alpha-failure write state");

        D3D11_BUFFER_DESC constant_description{};
        constant_description.ByteWidth = sizeof(RenderSceneMaterialConstants);
        constant_description.Usage = D3D11_USAGE_DEFAULT;
        constant_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        require_success(
            device->CreateBuffer(
                &constant_description,
                nullptr,
                render_scene_material_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(RenderSceneV1 material constants)");
    }

    void create_render_scene_geometry_buffers(
        const std::span<const std::uint32_t> triangle_indices) {
        render_scene_triangle_indices.assign(triangle_indices.begin(),triangle_indices.end());
        D3D11_BUFFER_DESC vertex_description{};
        vertex_description.ByteWidth = checked_buffer_size(
            render_scene_projected_vertices.size(),
            sizeof(ProjectedVertex),
            "Projected RenderSceneV1 vertex buffer");
        vertex_description.Usage = D3D11_USAGE_DYNAMIC;
        vertex_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        require_success(
            device->CreateBuffer(
                &vertex_description,
                nullptr,
                render_scene_vertex_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(projected RenderSceneV1 vertices)");

        D3D11_BUFFER_DESC index_description{};
        index_description.ByteWidth = checked_buffer_size(
            triangle_indices.size(),
            sizeof(std::uint32_t),
            "RenderSceneV1 index buffer");
        index_description.Usage = D3D11_USAGE_IMMUTABLE;
        index_description.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA index_data{};
        index_data.pSysMem = triangle_indices.data();
        require_success(
            device->CreateBuffer(
                &index_description,
                &index_data,
                render_scene_index_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(RenderSceneV1 indices)");
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
            sizeof(ProjectedVertex),
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

    void create_world_actor_buffers() {
        for (auto& model : world_actor_models) {
            if (model.triangle_indices.empty() ||
                model.triangle_indices.size() !=
                    model.reversed_triangle_indices.size()) {
                throw std::logic_error(
                    "A D3D11 world actor model has incomplete index staging");
            }
            const auto create_indices =
                [this](const std::vector<std::uint32_t>& indices,
                       ComPtr<ID3D11Buffer>& output,
                       const char* const operation) {
                    D3D11_BUFFER_DESC description{};
                    description.ByteWidth = checked_buffer_size(
                        indices.size(), sizeof(std::uint32_t), operation);
                    description.Usage = D3D11_USAGE_IMMUTABLE;
                    description.BindFlags = D3D11_BIND_INDEX_BUFFER;
                    D3D11_SUBRESOURCE_DATA data{};
                    data.pSysMem = indices.data();
                    require_success(
                        device->CreateBuffer(
                            &description, &data, output.GetAddressOf()),
                        operation);
                };
            create_indices(
                model.triangle_indices,
                model.index_buffer,
                "ID3D11Device::CreateBuffer(world actor indices)");
            create_indices(
                model.reversed_triangle_indices,
                model.reversed_index_buffer,
                "ID3D11Device::CreateBuffer(reversed world actor indices)");
        }
        for (auto& instance : world_actor_instances) {
            if (instance.projected_vertices.empty()) {
                throw std::logic_error(
                    "A D3D11 world actor has no vertex staging data");
            }
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = checked_buffer_size(
                instance.projected_vertices.size(), sizeof(ProjectedVertex),
                "World actor vertex buffer");
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            require_success(
                device->CreateBuffer(
                    &description, nullptr,
                    instance.vertex_buffer.GetAddressOf()),
                "ID3D11Device::CreateBuffer(world actor vertices)");
        }
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
            sizeof(ProjectedVertex),
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
            const auto posed = pose_actor_mesh_positions_v1(
                mesh,
                gameplay_actor_pose_palette,
                model_to_world,
                game::kRuntimePlayerActorPoseLimitsV1);
            if (posed.size() != mesh.vertices.size() ||
                posed.size() > gameplay_actor_vertices.size() - output_offset) {
                throw std::logic_error(
                    "The gameplay actor pose exceeded its flattened vertex storage");
            }
            for (std::size_t index = 0U; index < posed.size(); ++index) {
                const auto& position = posed[index];
                const auto& source = mesh.vertices[index];
                gameplay_actor_vertices[output_offset] =
                    RenderSceneD3dVertexV1{
                        position.x,
                        position.y,
                        position.z,
                        source.u,
                        source.v,
                        source.rgba8,
                    };
                gameplay_actor_projected_vertices[output_offset].rgba =
                    source.rgba8;
                gameplay_actor_projected_vertices[output_offset].u = source.u;
                gameplay_actor_projected_vertices[output_offset].v = source.v;
                ++output_offset;
            }
        }
        if (output_offset != gameplay_actor_vertices.size()) {
            throw std::logic_error(
                "The gameplay actor pose did not fill its flattened vertex storage");
        }
    }

    void rebuild_world_actor_vertices(WorldActorInstanceGpu& instance) {
        if (instance.model_gpu_index >= world_actor_models.size()) {
            throw std::logic_error(
                "A D3D11 world actor lost its model allocation");
        }
        const auto& model = world_actor_models[instance.model_gpu_index];
        if (instance.vertices.size() != model.vertex_count ||
            instance.projected_vertices.size() != model.vertex_count) {
            throw std::logic_error(
                "A D3D11 world actor has inconsistent vertex storage");
        }
        const auto model_to_world = compose_actor_transform(
            instance.entity_to_world, instance.model_to_entity);
        const auto reverses_orientation =
            checked_actor_reverses_orientation(model_to_world);
        if (reverses_orientation != instance.reverses_orientation) {
            instance.reverses_orientation = reverses_orientation;
        }

        std::size_t output_offset = 0U;
        for (const auto& mesh : model.meshes) {
            const auto posed = pose_actor_mesh_positions_v1(
                mesh,
                instance.pose_palette,
                model_to_world,
                game::kRuntimePlayerActorPoseLimitsV1);
            if (posed.size() != mesh.vertices.size() ||
                posed.size() > instance.vertices.size() - output_offset) {
                throw std::logic_error(
                    "A D3D11 world actor pose exceeded its vertex storage");
            }
            for (std::size_t index = 0U; index < posed.size(); ++index) {
                const auto& position = posed[index];
                const auto& source = mesh.vertices[index];
                instance.vertices[output_offset] = RenderSceneD3dVertexV1{
                    position.x,
                    position.y,
                    position.z,
                    source.u,
                    source.v,
                    source.rgba8,
                };
                instance.projected_vertices[output_offset].rgba =
                    source.rgba8;
                instance.projected_vertices[output_offset].u = source.u;
                instance.projected_vertices[output_offset].v = source.v;
                ++output_offset;
            }
        }
        if (output_offset != instance.vertices.size()) {
            throw std::logic_error(
                "A D3D11 world actor pose did not fill its vertex storage");
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
            return RenderSceneD3dVertexV1{
                position.x, position.y, position.z, 0.0F, 0.0F, color};
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
                gameplay_proxy_vertices[index].rgba8;
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
        const auto next = checked_gameplay_presentation(
            camera, feet_position, facing_yaw_radians, capsule_radius,
            capsule_height);
        if (gameplay_presentation == next) {
            return;
        }
        const auto actor_world_changed =
            !gameplay_presentation ||
            gameplay_presentation->feet_position != next.feet_position ||
            gameplay_presentation->facing_yaw_radians !=
                next.facing_yaw_radians;
        gameplay_presentation = next;
        if (has_gameplay_actor) {
            gameplay_actor_vertices_dirty =
                gameplay_actor_vertices_dirty || actor_world_changed;
        } else {
            if (!gameplay_proxy_vertex_buffer ||
                !gameplay_proxy_index_buffer) {
                create_gameplay_proxy_buffers();
            }
            rebuild_gameplay_proxy_vertices();
        }
        projected_vertices_dirty = true;
    }

    void set_gameplay_actor_pose(const ActorPosePaletteV1& pose) {
        if (!has_gameplay_actor) {
            throw std::logic_error(
                "The D3D11 renderer has no gameplay actor to pose");
        }
        const auto joint_count =
            gameplay_actor_pose_palette.global_joint_transforms.size();
        if (pose.global_joint_transforms.size() != joint_count ||
            pose.skin_transforms.size() != joint_count) {
            throw std::invalid_argument(
                "The D3D11 gameplay actor pose joint count does not match its rig");
        }
        const auto require_finite = [](const auto& transforms) {
            for (const auto& transform : transforms) {
                for (const auto component : transform.values) {
                    if (!std::isfinite(component)) {
                        throw std::invalid_argument(
                            "The D3D11 gameplay actor pose contains a non-finite transform");
                    }
                }
            }
        };
        require_finite(pose.global_joint_transforms);
        require_finite(pose.skin_transforms);

        if (gameplay_actor_pose_palette == pose) {
            return;
        }
        gameplay_actor_pose_palette = pose;
        gameplay_actor_vertices_dirty = true;
        projected_vertices_dirty = true;
    }

    [[nodiscard]] WorldActorInstanceGpu&
    require_world_actor(const std::uint32_t authored_id) {
        const auto found = std::lower_bound(
            world_actor_instances.begin(),
            world_actor_instances.end(),
            authored_id,
            [](const WorldActorInstanceGpu& actor, const std::uint32_t id) {
                return actor.authored_id < id;
            });
        if (found == world_actor_instances.end() ||
            found->authored_id != authored_id) {
            throw std::out_of_range(
                "The D3D11 world actor authored ID is unavailable");
        }
        return *found;
    }

    [[nodiscard]] const WorldActorInstanceGpu&
    require_world_actor(const std::uint32_t authored_id) const {
        const auto found = std::lower_bound(
            world_actor_instances.begin(),
            world_actor_instances.end(),
            authored_id,
            [](const WorldActorInstanceGpu& actor, const std::uint32_t id) {
                return actor.authored_id < id;
            });
        if (found == world_actor_instances.end() ||
            found->authored_id != authored_id) {
            throw std::out_of_range(
                "The D3D11 world actor authored ID is unavailable");
        }
        return *found;
    }

    void set_world_actor_pose(const std::uint32_t authored_id,
                              const ActorPosePaletteV1& pose) {
        auto& actor = require_world_actor(authored_id);
        const auto joint_count =
            actor.pose_palette.global_joint_transforms.size();
        if (pose.global_joint_transforms.size() != joint_count ||
            pose.skin_transforms.size() != joint_count) {
            throw std::invalid_argument(
                "The D3D11 world actor pose joint count does not match its rig");
        }
        const auto require_finite = [](const auto& transforms) {
            for (const auto& transform : transforms) {
                for (const auto component : transform.values) {
                    if (!std::isfinite(component)) {
                        throw std::invalid_argument(
                            "The D3D11 world actor pose contains a non-finite transform");
                    }
                }
            }
        };
        require_finite(pose.global_joint_transforms);
        require_finite(pose.skin_transforms);
        if (actor.pose_palette == pose) {
            return;
        }
        actor.pose_palette = pose;
        actor.vertices_dirty = true;
        projected_vertices_dirty = true;
    }

    void set_world_actor_transform(
        const std::uint32_t authored_id,
        const game::WorldTransformV1& transform) {
        auto& actor = require_world_actor(authored_id);
        const auto next = actor_transform_from_world(transform);
        if (actor.entity_to_world == next) {
            return;
        }
        const auto model_to_world = compose_actor_transform(
            next, actor.model_to_entity);
        const auto reverses_orientation =
            checked_actor_reverses_orientation(model_to_world);
        actor.entity_to_world = next;
        actor.reverses_orientation = reverses_orientation;
        actor.vertices_dirty = true;
        projected_vertices_dirty = true;
    }

    void set_world_actor_enabled(const std::uint32_t authored_id,
                                 const bool enabled) {
        auto& actor = require_world_actor(authored_id);
        if (actor.enabled == enabled) {
            return;
        }
        actor.enabled = enabled;
        projected_vertices_dirty = true;
    }

    void set_world_actor_camera(std::uint32_t authored_id,const SceneCameraV1* camera) {
        if(camera) validate_scene_camera_v1(*camera);
        auto& actor=require_world_actor(authored_id);
        const auto value=camera?std::optional<SceneCameraV1>{*camera}:std::nullopt;
        if(actor.camera==value) return;
        actor.camera=value;projected_vertices_dirty=true;
    }

    [[nodiscard]] bool
    world_actor_enabled(const std::uint32_t authored_id) const {
        return require_world_actor(authored_id).enabled;
    }

    [[nodiscard]] bool last_frame_world_actor_submitted(
        const std::uint32_t authored_id) const {
        return require_world_actor(authored_id).submitted;
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
        if (!render_scene_white_texture_view) create_render_scene_white_texture();
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

    void create_world_actor_resources() {
        for (auto& model : world_actor_models) {
            create_neutral_texture_resources(
                model.textures, model.texture_views);
            model.material_samplers.reserve(model.materials.size());
            for (const auto& material : model.materials) {
                model.material_samplers.push_back(
                    render_scene_sampler(material));
            }
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

    [[nodiscard]] static CameraProjection make_scene_camera_projection(const SceneCameraV1& camera) {
            const auto vector=[](const std::array<float,3>& a){return Vec3{a[0],a[1],a[2]};};
            const auto denominator=static_cast<double>(camera.far_plane)-camera.near_plane;
            return {vector(camera.position),vector(camera.forward),vector(camera.right),vector(camera.up),
                camera.tangent_half_vertical,camera.tangent_half_horizontal,
                static_cast<float>(camera.far_plane/denominator),
                static_cast<float>(static_cast<double>(camera.near_plane)*camera.far_plane/denominator)};
    }

    [[nodiscard]] CameraProjection make_camera_projection() {
        if (scene_camera) {
            return make_scene_camera_projection(*scene_camera);
        }
        if (!gameplay_presentation) {
            throw std::logic_error(
                "The neutral D3D11 renderer requires an explicit gameplay camera");
        }

        CameraProjection result;
        const auto& presentation = *gameplay_presentation;
        result.eye = presentation.eye;
        result.forward = normalized(presentation.target - result.eye);
        result.right = normalized(cross(result.forward, presentation.up));
        result.up = normalized(cross(result.right, result.forward));
        result.tangent_half_vertical = std::tan(
            presentation.vertical_field_of_view_radians * 0.5F);
        result.tangent_half_horizontal =
            result.tangent_half_vertical * presentation.aspect_ratio;
        const auto near_plane = presentation.near_plane_distance;
        const auto far_plane = presentation.far_plane_distance;

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
        const std::span<const RenderSceneD3dVertexV1> vertices,
        const std::span<ProjectedVertex> output,
        ID3D11Buffer* const buffer,
        const char* const map_operation,
        std::span<const std::uint8_t> projection_flags={}) {
        if (vertices.empty() || vertices.size() != output.size() ||
            buffer == nullptr) {
            throw std::logic_error(
                "The D3D11 projected-vertex storage is inconsistent");
        }
        if(!projection_flags.empty()&&projection_flags.size()!=vertices.size())
            throw std::logic_error("The D3D11 per-instance projection flags are inconsistent");
        for (std::size_t index = 0U; index < vertices.size(); ++index) {
            const auto flags=projection_flags.empty()?0U:projection_flags[index];
            const auto relative = as_vec3(vertices[index]) - ((flags&1U)?Vec3{}:camera.eye);
            const auto camera_z = dot(relative, camera.forward);
            output[index].clip_position = {
                dot(relative, camera.right) /
                    camera.tangent_half_horizontal,
                dot(relative, camera.up) /
                    camera.tangent_half_vertical,
                (flags&2U)?camera_z:camera.depth_scale * camera_z - camera.depth_offset,
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
        if (!projected_vertices_dirty || width == 0U || height == 0U) {
            return;
        }
        const auto camera = make_camera_projection();
        if (has_render_scene_geometry) {
            project_and_upload_vertices(
                camera,
                render_scene_vertices,
                render_scene_projected_vertices,
                render_scene_vertex_buffer.Get(),
                "ID3D11DeviceContext::Map(projected RenderSceneV1 vertices)",render_scene_projection_flags);
        }
        for (auto& actor : world_actor_instances) {
            if (!actor.enabled) {
                continue;
            }
            if (actor.vertices_dirty) {
                rebuild_world_actor_vertices(actor);
                actor.vertices_dirty = false;
            }
            project_and_upload_vertices(
                actor.camera?make_scene_camera_projection(*actor.camera):camera,
                actor.vertices,
                actor.projected_vertices,
                actor.vertex_buffer.Get(),
                "ID3D11DeviceContext::Map(world actor vertices)");
        }
        if (gameplay_presentation) {
            if (has_gameplay_actor) {
                if (gameplay_actor_vertices_dirty) {
                    rebuild_gameplay_actor_vertices();
                    gameplay_actor_vertices_dirty = false;
                }
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
        if (gameplay_presentation) {
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

        submission_drain_covers_latest_work=false;
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
        if (media_mode) return render_media();
        std::fill(
            render_scene_instance_submitted.begin(),
            render_scene_instance_submitted.end(),
            false);
        for (auto& actor : world_actor_instances) {
            actor.submitted = false;
        }
        if (width == 0U || height == 0U || !render_target || !depth_view) {
            return false;
        }

        if (!gameplay_presentation && !scene_camera) {
            throw std::logic_error(
                "The neutral D3D11 renderer cannot render without a gameplay presentation");
        }
        submission_drain_covers_latest_work=false;
        update_projected_vertices();

        constexpr std::array<float, 4U> kClearColor{
            5.0F / 255.0F,
            8.0F / 255.0F,
            18.0F / 255.0F,
            1.0F,
        };
        context->ClearRenderTargetView(render_target.Get(),
            scene_camera?scene_clear_color.data():kClearColor.data());
        context->ClearDepthStencilView(
            depth_view.Get(), D3D11_CLEAR_DEPTH, 1.0F, 0U);

        ID3D11RenderTargetView* const render_targets[] = {
            render_target.Get()};
        context->OMSetRenderTargets(
            1U, render_targets, depth_view.Get());

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        if (scene_camera) {
            const auto window_aspect=static_cast<double>(width)/height;
            if (window_aspect>scene_aspect) {
                viewport.Width=static_cast<float>(height*scene_aspect);
                viewport.TopLeftX=(static_cast<float>(width)-viewport.Width)*0.5F;
            } else {
                viewport.Height=static_cast<float>(width/scene_aspect);
                viewport.TopLeftY=(static_cast<float>(height)-viewport.Height)*0.5F;
            }
        }
        viewport.MinDepth = 0.0F;
        viewport.MaxDepth = 1.0F;
        context->RSSetViewports(1U, &viewport);
        context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        constexpr UINT kOffset = 0U;
        constexpr std::array<float, 4U> kBlendFactor{};
        constexpr UINT kAllSamples = 0xffffffffU;
        ID3D11ShaderResourceView* const no_texture[] = {nullptr};

        constexpr UINT kProjectedStride = sizeof(ProjectedVertex);
        ID3D11Buffer* const vertex_buffers[] = {
            render_scene_vertex_buffer.Get()};
        context->IASetInputLayout(projected_input_layout.Get());
        context->IASetVertexBuffers(
            0U, 1U, vertex_buffers, &kProjectedStride, &kOffset);
        context->IASetIndexBuffer(
            render_scene_index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0U);
        context->VSSetShader(projected_vertex_shader.Get(), nullptr, 0U);
        ID3D11Buffer* const no_constant_buffer[] = {nullptr};
        context->VSSetConstantBuffers(0U, 1U, no_constant_buffer);

        const auto draw_neutral =
            [this, &kBlendFactor, &viewport](
                const RenderSceneD3dDrawV1& draw,
                const std::vector<RenderSceneD3dMaterialV1>& materials,
                const std::vector<ComPtr<ID3D11ShaderResourceView>>&
                    texture_views,
                const std::vector<ComPtr<ID3D11SamplerState>>&
                    material_samplers,
                std::span<const ProjectedVertex> projected,
                std::span<const std::uint32_t> indices) {
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
                const auto depth_policy=(material.depth_test==RenderSceneDepthTestV1::always?2U:0U)|
                    (material.depth_write?0U:1U);
                context->OMSetDepthStencilState(scene_depth_states[depth_policy].Get(),0U);
                context->OMSetBlendState(
                    nullptr, kBlendFactor.data(), kAllSamples);
                context->PSSetShader(
                    render_scene_pixel_shader.Get(), nullptr, 0U);
                ID3D11Buffer* const material_buffers[] = {
                    render_scene_material_buffer.Get()};
                context->PSSetConstantBuffers(0U, 1U, material_buffers);

                auto* texture_view =
                    material.base_color_texture_id
                        ? texture_views[*material.base_color_texture_id].Get()
                        : render_scene_white_texture_view.Get();
                if(material.color_math==RenderSceneColorMathV1::encoded_integer) {
                    draw_encoded(draw,material,texture_view,projected,indices,viewport);return;
                }
                context->VSSetShader(projected_vertex_shader.Get(),nullptr,0);
                ID3D11ShaderResourceView* const selected_views[] = {
                    texture_view};
                context->PSSetShaderResources(0U, 1U, selected_views);
                ID3D11SamplerState* const samplers[] = {
                    material_samplers[draw.material_id].Get()};
                context->PSSetSamplers(0U, 1U, samplers);
                context->DrawIndexed(draw.index_count, draw.first_index, 0);
            };

        for (const auto& draw : render_scene_draws) {
            if (draw.instance_id == kRenderSceneD3dNoInstanceIdV1 ||
                static_cast<std::size_t>(draw.instance_id) >=
                    render_scene_instance_enabled.size()) {
                throw std::logic_error(
                    "A prepared render draw lost its instance identity");
            }
            if (!render_scene_instance_enabled[draw.instance_id]) {
                continue;
            }
            draw_neutral(
                draw,
                render_scene_materials,
                render_scene_texture_views,
                render_scene_material_samplers,render_scene_projected_vertices,render_scene_triangle_indices);
            render_scene_instance_submitted[draw.instance_id] = true;
        }

        for (auto& actor : world_actor_instances) {
            if (!actor.enabled) {
                continue;
            }
            if (actor.model_gpu_index >= world_actor_models.size()) {
                throw std::logic_error(
                    "A D3D11 world actor lost its draw model");
            }
            auto& model = world_actor_models[actor.model_gpu_index];
            ID3D11Buffer* const actor_vertex_buffers[] = {
                actor.vertex_buffer.Get()};
            context->IASetVertexBuffers(
                0U,
                1U,
                actor_vertex_buffers,
                &kProjectedStride,
                &kOffset);
            context->IASetIndexBuffer(
                actor.reverses_orientation
                    ? model.reversed_index_buffer.Get()
                    : model.index_buffer.Get(),
                DXGI_FORMAT_R32_UINT,
                0U);
            for (const auto& draw : model.draws) {
                draw_neutral(
                    draw,
                    model.materials,
                    model.texture_views,
                    model.material_samplers,actor.projected_vertices,
                    actor.reverses_orientation?model.reversed_triangle_indices:model.triangle_indices);
                actor.submitted = true;
            }
        }

        if (has_gameplay_actor) {
            ID3D11Buffer* const actor_vertex_buffers[] = {
                gameplay_actor_vertex_buffer.Get()};
            context->IASetVertexBuffers(
                0U,
                1U,
                actor_vertex_buffers,
                &kProjectedStride,
                &kOffset);
            context->IASetIndexBuffer(
                gameplay_actor_index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0U);
            for (const auto& draw : gameplay_actor_draws) {
                draw_neutral(
                    draw,
                    gameplay_actor_materials,
                    gameplay_actor_texture_views,
                    gameplay_actor_material_samplers,gameplay_actor_projected_vertices,gameplay_actor_triangle_indices);
            }
        } else if (gameplay_presentation) {
            context->VSSetShader(projected_vertex_shader.Get(), nullptr, 0U);
            ID3D11Buffer* const gameplay_vertex_buffers[] = {
                gameplay_proxy_vertex_buffer.Get()};
            context->IASetVertexBuffers(
                0U,
                1U,
                gameplay_vertex_buffers,
                &kProjectedStride,
                &kOffset);
            context->IASetIndexBuffer(
                gameplay_proxy_index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0U);
            context->RSSetState(solid_rasterizer_state.Get());
            context->OMSetDepthStencilState(depth_write_state.Get(), 0U);
            context->OMSetBlendState(
                nullptr, kBlendFactor.data(), kAllSamples);
            context->PSSetShader(
                gameplay_proxy_pixel_shader.Get(), nullptr, 0U);
            context->PSSetShaderResources(0U, 1U, no_texture);
            ID3D11Buffer* const no_proxy_constant_buffer[] = {nullptr};
            context->PSSetConstantBuffers(
                0U, 1U, no_proxy_constant_buffer);
            context->DrawIndexed(gameplay_proxy_index_count, 0U, 0);
        }
        context->PSSetShaderResources(0U, 1U, no_texture);
        ID3D11Buffer* const no_pixel_constant_buffer[] = {nullptr};
        context->PSSetConstantBuffers(
            0U, 1U, no_pixel_constant_buffer);
        context->OMSetBlendState(
            nullptr, kBlendFactor.data(), kAllSamples);

        render_screen_overlay();
        capture_back_buffer();
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
    ComPtr<ID3D11Texture2D> media_texture;
    std::vector<ScreenOverlayLayerGpu> overlay_layers;
    std::uint32_t overlay_destination_width=0,overlay_destination_height=0;
    ComPtr<ID3D11Texture2D> overlay_destination;
    ComPtr<ID3D11ShaderResourceView> overlay_destination_view;
    ComPtr<ID3D11Buffer> overlay_vertices,overlay_constants;
    ComPtr<ID3D11PixelShader> overlay_pixel_shader;
    ComPtr<ID3D11ShaderResourceView> media_view;
    ComPtr<ID3D11SamplerState> media_sampler;
    ComPtr<ID3D11Buffer> media_vertices;
    std::uint32_t media_width=0, media_height=0;
    double media_aspect=1;
    std::uint64_t media_submitted=0;
    bool media_capture_requested=false;
    bool media_mode=false;
    std::optional<SceneCameraV1> scene_camera;
    double scene_aspect=1;
    std::array<float,4> scene_clear_color{0,0,0,1};
    std::vector<std::byte> media_capture;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    UINT gameplay_proxy_index_count = 0U;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;
    std::vector<RenderSceneD3dVertexV1> render_scene_vertices;
    std::vector<std::uint8_t> render_scene_projection_flags;
    std::vector<ProjectedVertex> render_scene_projected_vertices;
    std::vector<std::uint32_t> render_scene_triangle_indices;
    std::vector<WorldActorModelGpu> world_actor_models;
    std::vector<WorldActorInstanceGpu> world_actor_instances;
    std::vector<ActorSkinnedMeshV1> gameplay_actor_meshes;
    ActorPosePaletteV1 gameplay_actor_pose_palette;
    ActorAffineTransformV1 gameplay_actor_model_to_entity;
    std::vector<std::uint32_t> gameplay_actor_triangle_indices;
    std::vector<RenderSceneD3dDrawV1> gameplay_actor_draws;
    std::vector<RenderSceneD3dMaterialV1> gameplay_actor_materials;
    std::vector<RenderSceneD3dVertexV1> gameplay_actor_vertices;
    std::vector<ProjectedVertex> gameplay_actor_projected_vertices;
    std::vector<ComPtr<ID3D11ShaderResourceView>>
        gameplay_actor_texture_views;
    std::vector<ComPtr<ID3D11SamplerState>>
        gameplay_actor_material_samplers;
    std::vector<RenderSceneD3dVertexV1> gameplay_proxy_vertices;
    std::vector<ProjectedVertex> gameplay_proxy_projected_vertices;
    std::vector<RenderSceneD3dDrawV1> render_scene_draws;
    std::vector<bool> render_scene_instance_enabled;
    std::vector<bool> render_scene_instance_submitted;
    std::vector<RenderSceneD3dMaterialV1> render_scene_materials;
    std::vector<ComPtr<ID3D11ShaderResourceView>>
        render_scene_texture_views;
    std::vector<ComPtr<ID3D11SamplerState>>
        render_scene_material_samplers;
    std::vector<GpuRenderSceneSampler> render_scene_sampler_cache;
    std::optional<GameplayPresentation> gameplay_presentation;
    bool has_render_scene_geometry = false;
    bool has_gameplay_actor = false;
    bool gameplay_actor_vertices_dirty = false;
    bool projected_vertices_dirty = false;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap_chain;
    ComPtr<ID3D11RenderTargetView> render_target;
    ComPtr<ID3D11Texture2D> depth_texture;
    ComPtr<ID3D11DepthStencilView> depth_view;
    ComPtr<ID3D11VertexShader> projected_vertex_shader;
    ComPtr<ID3D11PixelShader> gameplay_proxy_pixel_shader;
    ComPtr<ID3D11PixelShader> media_pixel_shader;
    ComPtr<ID3D11PixelShader> render_scene_pixel_shader;
    ComPtr<ID3D11InputLayout> projected_input_layout;
    ComPtr<ID3D11RasterizerState> solid_rasterizer_state;
    ComPtr<ID3D11RasterizerState> solid_single_sided_rasterizer_state;
    ComPtr<ID3D11DepthStencilState> depth_write_state;
    std::array<ComPtr<ID3D11DepthStencilState>,4> scene_depth_states;
    ComPtr<ID3D11BlendState> rgb_write_state;
    ComPtr<ID3D11VertexShader> encoded_affine_vertex_shader;
    ComPtr<ID3D11VertexShader> encoded_vertex_shader;
    ComPtr<ID3D11PixelShader> encoded_pixel_shader,encoded_affine_pixel_shader;
    ComPtr<ID3D11ShaderResourceView> render_scene_white_texture_view;
    ComPtr<ID3D11Buffer> render_scene_vertex_buffer;
    ComPtr<ID3D11Buffer> render_scene_index_buffer;
    ComPtr<ID3D11Buffer> gameplay_actor_vertex_buffer;
    ComPtr<ID3D11Buffer> gameplay_actor_index_buffer;
    ComPtr<ID3D11Buffer> gameplay_proxy_vertex_buffer;
    ComPtr<ID3D11Buffer> gameplay_proxy_index_buffer;
    ComPtr<ID3D11Buffer> render_scene_material_buffer;
    ComPtr<ID3D11Query> submission_drain_query;
    std::uint64_t submission_drain_token=0;
    bool submission_drain_observed_complete=false;
    bool submission_drain_covers_latest_work=false;
    bool submission_drain_retirement_consumed=false;
};

D3d11Renderer::D3d11Renderer(HWND window)
    : implementation_(std::make_unique<Implementation>(window)) {}

void D3d11Renderer::set_media_frame(std::uint32_t width, std::uint32_t height,
                                   std::span<const std::byte> rgba,
                                   std::uint32_t aspect_numerator,
                                   std::uint32_t aspect_denominator) {
    implementation_->set_media_frame(width,height,rgba,aspect_numerator,aspect_denominator);
}
std::uint64_t D3d11Renderer::media_frames_submitted() const noexcept {
    return implementation_->media_submitted;
}
std::vector<std::byte> D3d11Renderer::capture_media_frame_rgba() {
    if (!implementation_->media_texture) throw std::logic_error("No media frame to capture");
    implementation_->media_capture_requested=true;
    static_cast<void>(implementation_->render_media());
    return std::move(implementation_->media_capture);
}

std::vector<std::byte> D3d11Renderer::capture_frame_rgba() {
    implementation_->media_capture.clear();
    implementation_->media_capture_requested=true;
    static_cast<void>(implementation_->render());
    return std::move(implementation_->media_capture);
}

std::uint64_t D3d11Renderer::begin_submission_drain() {
    return implementation_->begin_submission_drain();
}
bool D3d11Renderer::submission_drain_completed(std::uint64_t token) {
    return implementation_->submission_drain_completed(token);
}
bool D3d11Renderer::submission_drain_covers_current_work(std::uint64_t token) const {
    return implementation_->submission_drain_covers_current_work(token);
}
bool D3d11Renderer::try_retire_submission_drain(std::uint64_t token) {
    return implementation_->try_retire_submission_drain(token);
}
void D3d11Renderer::retire_scene_for_media(std::uint64_t completed_drain_token) {
    implementation_->retire_scene_for_media(completed_drain_token);
}
bool D3d11Renderer::try_retire_scene_for_media(std::uint64_t completed_drain_token) {
    return implementation_->try_retire_scene_for_media(completed_drain_token);
}

void D3d11Renderer::set_scene_actors(const ActorLibraryV1& library,
    std::span<const game::RuntimeWorldActorResolutionV1> actors) {
    implementation_->set_scene_actors(library,actors);
}

void D3d11Renderer::set_scene_geometry(const RenderSceneV1& scene) {
    implementation_->set_scene_geometry(scene);
}

void D3d11Renderer::set_gameplay_scene(const RenderSceneV1& scene,
    const ActorLibraryV1& library,const game::RuntimePlayerActorResolutionV1& player,
    std::span<const game::RuntimeWorldActorResolutionV1> actors) {
    implementation_->set_gameplay_scene(scene,library,player,actors);
}

void D3d11Renderer::set_scene_camera(const SceneCameraV1& camera,
    std::uint32_t aspect_numerator,std::uint32_t aspect_denominator,
    const std::array<float,4>& clear_color) {
    implementation_->set_scene_camera(camera,aspect_numerator,aspect_denominator,clear_color);
}

D3d11Renderer::D3d11Renderer(HWND window,
                             const openrc::RenderSceneV1& scene)
    : implementation_(std::make_unique<Implementation>(window, scene)) {}

D3d11Renderer::D3d11Renderer(
    HWND window,
    const openrc::RenderSceneV1& scene,
    const openrc::ActorLibraryV1& actor_library,
    const openrc::game::RuntimePlayerActorResolutionV1& player_actor,
    const std::span<const openrc::game::RuntimeWorldActorResolutionV1>
        world_actors)
    : implementation_(std::make_unique<Implementation>(
          window,
          scene,
          actor_library,
          player_actor,
          world_actors)) {}

void D3d11Renderer::set_render_instance_enabled(
    const std::uint32_t instance_id,
    const bool enabled) {
    if (!implementation_ ||
        static_cast<std::size_t>(instance_id) >=
            implementation_->render_scene_instance_enabled.size()) {
        throw std::out_of_range(
            "RenderSceneV1 instance visibility ID is out of range");
    }
    implementation_->render_scene_instance_enabled[instance_id] = enabled;
}

bool D3d11Renderer::render_instance_enabled(
    const std::uint32_t instance_id) const {
    if (!implementation_ ||
        static_cast<std::size_t>(instance_id) >=
            implementation_->render_scene_instance_enabled.size()) {
        throw std::out_of_range(
            "RenderSceneV1 instance visibility ID is out of range");
    }
    return implementation_->render_scene_instance_enabled[instance_id];
}

bool D3d11Renderer::last_frame_render_instance_submitted(
    const std::uint32_t instance_id) const {
    if (!implementation_ ||
        static_cast<std::size_t>(instance_id) >=
            implementation_->render_scene_instance_submitted.size()) {
        throw std::out_of_range(
            "RenderSceneV1 instance submission ID is out of range");
    }
    return implementation_->render_scene_instance_submitted[instance_id];
}

D3d11Renderer::~D3d11Renderer() = default;

D3d11Renderer::D3d11Renderer(D3d11Renderer&&) noexcept = default;

D3d11Renderer&
D3d11Renderer::operator=(D3d11Renderer&&) noexcept = default;

void D3d11Renderer::resize(const std::uint32_t width,
                           const std::uint32_t height) {
    implementation_->resize(width, height);
}

void D3d11Renderer::set_screen_overlay(const ScreenOverlayV1& overlay) {
    implementation_->set_screen_overlay_layers(std::span(&overlay,1U),{});
}
void D3d11Renderer::set_screen_overlay_frame(std::uint32_t frame_index) {
    if(implementation_->overlay_layers.empty()||
        frame_index>=implementation_->overlay_layers[0].frames.size())
        throw std::out_of_range("Screen overlay frame is absent");
    implementation_->overlay_layers[0].frame_index=frame_index;
}
void D3d11Renderer::clear_screen_overlay() {
    implementation_->overlay_layers.clear();
}
void D3d11Renderer::set_screen_overlay_layers(const std::span<const ScreenOverlayV1> layers,
    const ScreenOverlayLimitsV1 limits) {
    implementation_->set_screen_overlay_layers(layers,limits);
}
void D3d11Renderer::set_screen_overlay_layer_frames(
    const std::span<const std::optional<std::uint32_t>> frame_indices) {
    implementation_->set_screen_overlay_layer_frames(frame_indices);
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

void D3d11Renderer::set_gameplay_actor_pose(
    const ActorPosePaletteV1& pose) {
    implementation_->set_gameplay_actor_pose(pose);
}

void D3d11Renderer::set_world_actor_pose(
    const std::uint32_t authored_id,
    const ActorPosePaletteV1& pose) {
    implementation_->set_world_actor_pose(authored_id, pose);
}

void D3d11Renderer::set_world_actor_transform(
    const std::uint32_t authored_id,
    const game::WorldTransformV1& transform) {
    implementation_->set_world_actor_transform(authored_id, transform);
}

void D3d11Renderer::set_world_actor_camera(std::uint32_t authored_id,
    const SceneCameraV1* camera) {
    implementation_->set_world_actor_camera(authored_id,camera);
}

void D3d11Renderer::set_world_actor_enabled(
    const std::uint32_t authored_id,
    const bool enabled) {
    implementation_->set_world_actor_enabled(authored_id, enabled);
}

bool D3d11Renderer::world_actor_enabled(
    const std::uint32_t authored_id) const {
    return implementation_->world_actor_enabled(authored_id);
}

bool D3d11Renderer::last_frame_world_actor_submitted(
    const std::uint32_t authored_id) const {
    return implementation_->last_frame_world_actor_submitted(authored_id);
}

} // namespace openrc::runtime
