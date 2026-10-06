#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kRenderSceneSchemaVersionV1 = 1U;

// RenderSceneV1 is deliberately a small static, unlit contract. Coordinates
// are right-handed, Z-up natural world units. Mesh indices are local to their
// mesh; instances supply the only local-to-world transform.
enum class RenderSceneTextureColorSpaceV1 : std::uint8_t {
  linear = 0U,
  srgb = 1U,
};

enum class RenderSceneAddressModeV1 : std::uint8_t {
  repeat = 0U,
  clamp_to_edge = 1U,
};

enum class RenderSceneFilterV1 : std::uint8_t {
  nearest = 0U,
  linear = 1U,
};

enum class RenderSceneMipmapFilterV1 : std::uint8_t {
  none = 0U,
  nearest = 1U,
  linear = 2U,
};

enum class RenderSceneAlphaModeV1 : std::uint8_t {
  opaque = 0U,
  mask = 1U,
};
enum class RenderSceneAlphaFailureV1 : std::uint8_t {
  discard = 0U,
  // Failed encoded alpha tests still blend/write RGB, while preserving the
  // destination alpha and depth. The ordinary depth comparison still applies.
  rgb_only = 1U,
};

enum class RenderSceneColorMathV1 : std::uint8_t {
  linear = 0U,
  encoded_integer = 1U,
};
enum class RenderSceneBlendModeV1 : std::uint8_t {
  opaque = 0U,
  source_over = 1U,
};
enum class RenderSceneInterpolationV1 : std::uint8_t {
  perspective = 0U,
  affine = 1U,
};
enum class RenderSceneDepthTestV1 : std::uint8_t {
  less_equal = 0U,
  always = 1U,
};

// Bytes are tightly packed row-major RGBA8. Mip zero is mandatory; later
// levels, when present, halve each dimension with a floor and a minimum of 1.
struct RenderSceneTextureMipV1 {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::vector<std::byte> rgba8;

  [[nodiscard]] bool
  operator==(const RenderSceneTextureMipV1 &) const = default;
};

struct RenderSceneTextureV1 {
  // IDs are canonical dense zero-based indices. The canonicalizer sorts each
  // top-level table by ID, then requires id == table index.
  std::uint32_t id = 0U;
  RenderSceneTextureColorSpaceV1 color_space =
      RenderSceneTextureColorSpaceV1::linear;
  std::vector<RenderSceneTextureMipV1> mips;

  [[nodiscard]] bool operator==(const RenderSceneTextureV1 &) const = default;
};

struct RenderSceneMaterialV1 {
  std::uint32_t id = 0U;
  std::optional<std::uint32_t> base_color_texture_id;

  // Packed as R in bits 0..7, G in 8..15, B in 16..23, A in 24..31.
  std::uint32_t base_color_rgba8 = UINT32_C(0xffffffff);
  bool use_vertex_color = true;
  bool double_sided = true;

  RenderSceneAddressModeV1 address_u = RenderSceneAddressModeV1::repeat;
  RenderSceneAddressModeV1 address_v = RenderSceneAddressModeV1::repeat;
  RenderSceneFilterV1 min_filter = RenderSceneFilterV1::linear;
  RenderSceneFilterV1 mag_filter = RenderSceneFilterV1::linear;
  RenderSceneMipmapFilterV1 mipmap_filter =
      RenderSceneMipmapFilterV1::none;
  RenderSceneAlphaModeV1 alpha_mode = RenderSceneAlphaModeV1::opaque;

  // Opaque materials require zero. Masked fragments pass when their final
  // alpha is at least alpha_cutoff_rgba8 / 255 and require a non-zero cutoff.
  std::uint8_t alpha_cutoff_rgba8 = 0U;

  // Legacy defaults encode as zero extension bytes. Integer color math
  // consumes encoded texture/vertex bytes without sRGB transfer conversion.
  // Textured RGB is floor(texel*vertex/texture_modulation_denominator);
  // source-over uses blend_denominator for its encoded source alpha.
  RenderSceneColorMathV1 color_math = RenderSceneColorMathV1::linear;
  RenderSceneBlendModeV1 blend_mode = RenderSceneBlendModeV1::opaque;
  // Encoded integer materials interpolate byte-space vertex colors affinely.
  // This policy selects perspective or affine texture-coordinate interpolation.
  RenderSceneInterpolationV1 interpolation = RenderSceneInterpolationV1::perspective;
  RenderSceneDepthTestV1 depth_test = RenderSceneDepthTestV1::less_equal;
  bool depth_write = true;
  std::uint8_t texture_modulation_denominator = 255U;
  std::uint8_t blend_denominator = 255U;
  RenderSceneAlphaFailureV1 alpha_failure = RenderSceneAlphaFailureV1::discard;

  [[nodiscard]] bool operator==(const RenderSceneMaterialV1 &) const = default;
};

struct RenderSceneVertexV1 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float u = 0.0F;
  float v = 0.0F;
  std::uint32_t rgba8 = UINT32_C(0xffffffff);

  [[nodiscard]] bool operator==(const RenderSceneVertexV1 &) const = default;
};

static_assert(sizeof(RenderSceneVertexV1) == 24U);

struct RenderSceneDrawRangeV1 {
  std::uint32_t material_id = 0U;
  // Relative to RenderSceneMeshV1::triangle_indices. Draw ranges are
  // non-empty, triangle-aligned, contiguous, and cover that vector exactly.
  std::uint64_t first_index = 0U;
  std::uint64_t index_count = 0U;

  [[nodiscard]] bool
  operator==(const RenderSceneDrawRangeV1 &) const = default;
};

struct RenderSceneMeshV1 {
  std::uint32_t id = 0U;
  std::vector<RenderSceneVertexV1> vertices;
  std::vector<std::uint32_t> triangle_indices;
  std::vector<RenderSceneDrawRangeV1> draw_ranges;

  [[nodiscard]] bool operator==(const RenderSceneMeshV1 &) const = default;
};

// Row-major affine 3x4 matrix. For local (x,y,z), the first row computes
// world X, the second world Y, and the third world Z; columns 3, 7, and 11 are
// translation. There is no homogeneous divide.
struct RenderSceneAffine3x4V1 {
  std::array<float, 12U> values{
      1.0F, 0.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 0.0F,
  };

  [[nodiscard]] bool
  operator==(const RenderSceneAffine3x4V1 &) const = default;
};

struct RenderSceneInstanceV1 {
  std::uint32_t id = 0U;
  std::uint32_t mesh_id = 0U;
  RenderSceneAffine3x4V1 local_to_world;
  // Translation follows the current camera after the authored affine map.
  // Far-plane projection preserves XY and uses the clip far depth.
  bool camera_relative = false;
  bool project_to_far_plane = false;

  [[nodiscard]] bool operator==(const RenderSceneInstanceV1 &) const = default;
};

struct RenderSceneV1 {
  std::uint32_t schema_version = kRenderSceneSchemaVersionV1;
  std::vector<RenderSceneTextureV1> textures;
  std::vector<RenderSceneMaterialV1> materials;
  std::vector<RenderSceneMeshV1> meshes;
  std::vector<RenderSceneInstanceV1> instances;

  [[nodiscard]] bool operator==(const RenderSceneV1 &) const = default;
};

struct RenderSceneLimitsV1 {
  std::uint32_t max_textures = 0U;
  std::uint32_t max_mips_per_texture = 0U;
  std::uint32_t max_total_texture_mips = 0U;
  std::uint32_t max_materials = 0U;
  std::uint32_t max_meshes = 0U;
  std::uint32_t max_draw_ranges = 0U;
  std::uint32_t max_instances = 0U;
  std::uint32_t max_texture_width = 0U;
  std::uint32_t max_texture_height = 0U;
  std::uint64_t max_texels_per_texture = 0U;
  std::uint64_t max_vertices = 0U;
  std::uint64_t max_triangle_indices = 0U;
  std::uint64_t max_total_rgba8_bytes = 0U;

  [[nodiscard]] bool operator==(const RenderSceneLimitsV1 &) const = default;
};

class RenderSceneError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Requires canonical dense IDs/order, canonical positive zero, complete draw
// coverage, valid references, no orphan resources, and all caller limits.
void validate_render_scene_v1(const RenderSceneV1 &scene,
                              RenderSceneLimitsV1 limits);

// Sorts the four ID-bearing tables and maps every finite signed zero to +0,
// then performs the same strict validation. References remain ID-based.
[[nodiscard]] RenderSceneV1
canonicalize_render_scene_v1(RenderSceneV1 scene,
                             RenderSceneLimitsV1 limits);

} // namespace openrc
