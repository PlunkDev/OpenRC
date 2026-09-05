#pragma once

#include "openrc/render_scene.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace openrc::runtime {

// GPU-neutral staging data for the D3D11 RenderSceneV1 path. Mesh-local
// vertices and indices are expanded in canonical instance order so the
// renderer never needs RAC source records or decoder state at run time.
struct RenderSceneD3dVertexV1 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    std::uint32_t rgba8 = UINT32_C(0xffffffff);

    [[nodiscard]] bool
    operator==(const RenderSceneD3dVertexV1&) const = default;
};

static_assert(sizeof(RenderSceneD3dVertexV1) == 24U);

struct RenderSceneD3dDrawV1 {
    std::uint32_t material_id = 0U;
    std::uint32_t first_index = 0U;
    std::uint32_t index_count = 0U;

    [[nodiscard]] bool
    operator==(const RenderSceneD3dDrawV1&) const = default;
};

struct RenderSceneD3dMaterialV1 {
    std::optional<std::uint32_t> base_color_texture_id;
    std::array<float, 4U> base_color{};
    bool use_vertex_color = true;
    bool double_sided = true;
    RenderSceneAddressModeV1 address_u = RenderSceneAddressModeV1::repeat;
    RenderSceneAddressModeV1 address_v = RenderSceneAddressModeV1::repeat;
    RenderSceneFilterV1 min_filter = RenderSceneFilterV1::linear;
    RenderSceneFilterV1 mag_filter = RenderSceneFilterV1::linear;
    RenderSceneMipmapFilterV1 mipmap_filter =
        RenderSceneMipmapFilterV1::none;
    // Opaque materials use -1, so the common shader's clip is disabled.
    float alpha_cutoff = -1.0F;
};

struct RenderSceneD3dDataV1 {
    std::vector<RenderSceneD3dVertexV1> vertices;
    std::vector<std::uint32_t> triangle_indices;
    std::vector<RenderSceneD3dDrawV1> draws;
    std::vector<RenderSceneD3dMaterialV1> materials;
    float minimum_x = 0.0F;
    float maximum_x = 0.0F;
    float minimum_y = 0.0F;
    float maximum_y = 0.0F;
    float minimum_z = 0.0F;
    float maximum_z = 0.0F;
    std::array<float, 3U> camera_target{};
    float camera_radius = 1.0F;
};

class RenderSceneD3dDataError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct RenderSceneD3dDataLimitsV1 {
    std::uint64_t max_flattened_vertices = 0U;
    std::uint64_t max_flattened_triangle_indices = 0U;
    std::uint64_t max_flattened_draws = 0U;

    [[nodiscard]] bool
    operator==(const RenderSceneD3dDataLimitsV1&) const = default;
};

// Practical limits for the shipped renderer. RenderSceneV1 counts unique mesh
// data, while this staging path expands it once per instance; keeping this
// second allocation boundary prevents a compact scene from amplifying into
// multi-gigabyte CPU/GPU buffers.
[[nodiscard]] constexpr RenderSceneD3dDataLimitsV1
make_render_scene_d3d_data_limits_v1() noexcept {
    // The 19 verified base packages peak at 9,939 draws. A power-of-two
    // ceiling leaves substantial mod/headroom without permitting millions of
    // DrawIndexed calls per frame.
    return RenderSceneD3dDataLimitsV1{3'000'000U, 9'000'000U, 16'384U};
}

// Validates the complete neutral scene contract and flattens every affine
// instance. The resulting indices are one global, contiguous triangle list;
// draw ranges remain in canonical instance/mesh order and use global offsets.
[[nodiscard]] RenderSceneD3dDataV1
build_render_scene_d3d_data_v1(
    const RenderSceneV1& scene,
    RenderSceneD3dDataLimitsV1 limits =
        make_render_scene_d3d_data_limits_v1());

} // namespace openrc::runtime
