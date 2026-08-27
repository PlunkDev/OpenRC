#pragma once

#include "openrc/gif_gs.hpp"
#include "openrc/scene_block_geometry.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc::runtime {

struct SceneVertexV1 {
    float x = 0.0F;
    float y = 0.0F;
    std::uint32_t rgba = 0xffffffffU;
};

static_assert(sizeof(SceneVertexV1) == 12U);

struct SceneGeometryV1 {
    std::vector<SceneVertexV1> vertices;
    std::vector<std::uint32_t> triangle_indices;
    float minimum_x = 0.0F;
    float maximum_x = 0.0F;
    float minimum_y = 0.0F;
    float maximum_y = 0.0F;
    std::uint64_t emitted_triangle_count = 0U;
    std::uint64_t skipped_non_triangle_count = 0U;
    std::uint64_t vertices_without_complete_xy_offset = 0U;
    std::uint64_t vertices_with_fallback_color = 0U;
};

struct SceneVertex3dV1 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    std::uint32_t rgba = 0xffffffffU;
};

static_assert(sizeof(SceneVertex3dV1) == 16U);

// Source-space geometry used by the diagnostic orbit view. These coordinates
// are the signed integer inputs consumed by VITOF0 in the recovered VU path;
// they are deliberately not labelled as the game's world-space convention.
struct SceneGeometry3dV1 {
    std::vector<SceneVertex3dV1> vertices;
    std::vector<std::uint32_t> triangle_indices;
    float minimum_x = 0.0F;
    float maximum_x = 0.0F;
    float minimum_y = 0.0F;
    float maximum_y = 0.0F;
    float minimum_z = 0.0F;
    float maximum_z = 0.0F;
    std::uint64_t emitted_triangle_count = 0U;
    std::uint64_t skipped_non_triangle_count = 0U;
};

class SceneGeometryError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct SceneGeometryMergeLimitsV1 {
    std::uint64_t max_vertices = 0U;
    std::uint64_t max_triangle_indices = 0U;
};

// Converts already-decoded, emitted GS primitives to the one static triangle
// batch consumed by the first D3D11 preview. Coordinates retain the decoder's
// per-vertex raster snapshot and are expressed in GS pixels.
[[nodiscard]] SceneGeometryV1
build_scene_geometry_v1(const GifGsDecodeResultV1& decoded);

// Applies the GS decoder's emitted-triangle topology to the independently
// recovered source vertices. The source list must remain one-to-one and in the
// same order as decoded.vertices; no relationship is guessed from coordinates.
[[nodiscard]] SceneGeometry3dV1 build_scene_geometry_3d_v1(
    const SceneBlockSourceGeometryV1& source,
    const GifGsDecodeResultV1& decoded);

// Merges complete triangle batches without changing the index order inside
// any input batch. Every input is validated before the result is allocated;
// index offsets, counters, host sizes, and caller-provided aggregate limits
// are checked rather than allowed to wrap.
[[nodiscard]] SceneGeometryV1 merge_scene_geometries_v1(
    std::span<const SceneGeometryV1> geometries,
    SceneGeometryMergeLimitsV1 limits);

[[nodiscard]] SceneGeometry3dV1 merge_scene_geometries_3d_v1(
    std::span<const SceneGeometry3dV1> geometries,
    SceneGeometryMergeLimitsV1 limits);

} // namespace openrc::runtime
