#include "openrc/render_scene.hpp"
#include "render_material_policy.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RenderSceneError(message);
}

void validate_limits(const RenderSceneLimitsV1 limits) {
  if (limits.max_textures == 0U ||
      limits.max_mips_per_texture == 0U ||
      limits.max_total_texture_mips == 0U ||
      limits.max_materials == 0U || limits.max_meshes == 0U ||
      limits.max_draw_ranges == 0U || limits.max_instances == 0U ||
      limits.max_texture_width == 0U ||
      limits.max_texture_height == 0U ||
      limits.max_texels_per_texture == 0U || limits.max_vertices == 0U ||
      limits.max_triangle_indices == 0U ||
      limits.max_total_rgba8_bytes == 0U) {
    fail("RenderSceneV1 limits must all be positive");
  }
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left, const std::uint64_t right,
    const char *const description) {
  if (left != 0U &&
      right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left * right;
}

[[nodiscard]] bool valid_color_space(
    const RenderSceneTextureColorSpaceV1 value) noexcept {
  return value == RenderSceneTextureColorSpaceV1::linear ||
         value == RenderSceneTextureColorSpaceV1::srgb;
}

[[nodiscard]] bool
valid_address_mode(const RenderSceneAddressModeV1 value) noexcept {
  return value == RenderSceneAddressModeV1::repeat ||
         value == RenderSceneAddressModeV1::clamp_to_edge;
}

[[nodiscard]] bool valid_filter(const RenderSceneFilterV1 value) noexcept {
  return value == RenderSceneFilterV1::nearest ||
         value == RenderSceneFilterV1::linear;
}

[[nodiscard]] bool
valid_mipmap_filter(const RenderSceneMipmapFilterV1 value) noexcept {
  return value == RenderSceneMipmapFilterV1::none ||
         value == RenderSceneMipmapFilterV1::nearest ||
         value == RenderSceneMipmapFilterV1::linear;
}

[[nodiscard]] bool
valid_alpha_mode(const RenderSceneAlphaModeV1 value) noexcept {
  return value == RenderSceneAlphaModeV1::opaque ||
         value == RenderSceneAlphaModeV1::mask;
}

void validate_canonical_float(const float value,
                              const char *const description) {
  if (!std::isfinite(value)) {
    fail(std::string("RenderSceneV1 has a non-finite ") + description);
  }
  if (value == 0.0F && std::signbit(value)) {
    fail(std::string("RenderSceneV1 has non-canonical signed zero in ") +
         description);
  }
}

[[nodiscard]] float canonical_float(const float value,
                                    const char *const description) {
  if (!std::isfinite(value)) {
    fail(std::string("RenderSceneV1 has a non-finite ") + description);
  }
  return value == 0.0F ? 0.0F : value;
}

template <typename Item>
void require_dense_ids(const std::vector<Item> &items,
                       const char *const description) {
  for (std::size_t index = 0U; index < items.size(); ++index) {
    if (items[index].id != index) {
      fail(std::string("RenderSceneV1 ") + description +
           " IDs are not canonical dense table indices");
    }
  }
}

template <typename Item>
void sort_by_id(std::vector<Item> &items) {
  std::sort(items.begin(), items.end(),
            [](const Item &left, const Item &right) {
              return left.id < right.id;
            });
}

void require_top_level_count(const std::size_t count,
                             const std::uint32_t limit,
                             const char *const description) {
  if (count > limit || count > std::numeric_limits<std::uint32_t>::max()) {
    fail(std::string("RenderSceneV1 ") + description +
         " count exceeds its caller or V1 limit");
  }
}

} // namespace

void validate_render_scene_v1(const RenderSceneV1 &scene,
                              const RenderSceneLimitsV1 limits) {
  validate_limits(limits);
  if (scene.schema_version != kRenderSceneSchemaVersionV1) {
    fail("RenderSceneV1 has an unsupported schema version");
  }

  require_top_level_count(scene.textures.size(), limits.max_textures,
                          "texture");
  require_top_level_count(scene.materials.size(), limits.max_materials,
                          "material");
  require_top_level_count(scene.meshes.size(), limits.max_meshes, "mesh");
  require_top_level_count(scene.instances.size(), limits.max_instances,
                          "instance");
  require_dense_ids(scene.textures, "texture");
  require_dense_ids(scene.materials, "material");
  require_dense_ids(scene.meshes, "mesh");
  require_dense_ids(scene.instances, "instance");

  std::uint64_t total_mips = 0U;
  std::uint64_t total_rgba8_bytes = 0U;
  for (const auto &texture : scene.textures) {
    if (!valid_color_space(texture.color_space)) {
      fail("RenderSceneV1 texture uses an unknown color space");
    }
    if (texture.mips.empty() ||
        texture.mips.size() > limits.max_mips_per_texture ||
        texture.mips.size() > std::numeric_limits<std::uint32_t>::max()) {
      fail("RenderSceneV1 texture has an invalid mip count");
    }
    total_mips = checked_add(total_mips, texture.mips.size(),
                             "The RenderSceneV1 mip count");
    if (total_mips > limits.max_total_texture_mips) {
      fail("RenderSceneV1 total mip count exceeds its caller limit");
    }

    std::uint64_t texture_texels = 0U;
    std::uint32_t expected_width = 0U;
    std::uint32_t expected_height = 0U;
    for (std::size_t level = 0U; level < texture.mips.size(); ++level) {
      const auto &mip = texture.mips[level];
      if (mip.width == 0U || mip.height == 0U ||
          mip.width > limits.max_texture_width ||
          mip.height > limits.max_texture_height) {
        fail("RenderSceneV1 texture mip dimensions exceed caller limits");
      }
      if (level != 0U &&
          (mip.width != expected_width || mip.height != expected_height)) {
        fail("RenderSceneV1 texture mip dimensions do not form a canonical chain");
      }
      const auto texels = checked_multiply(
          mip.width, mip.height, "A RenderSceneV1 texture mip texel count");
      const auto rgba8_bytes = checked_multiply(
          texels, 4U, "A RenderSceneV1 texture mip byte count");
      if (rgba8_bytes != mip.rgba8.size()) {
        fail("RenderSceneV1 texture mip has an inexact RGBA8 byte count");
      }
      texture_texels = checked_add(texture_texels, texels,
                                   "A RenderSceneV1 texture texel count");
      if (texture_texels > limits.max_texels_per_texture) {
        fail("RenderSceneV1 texture texels exceed the per-texture limit");
      }
      total_rgba8_bytes = checked_add(
          total_rgba8_bytes, rgba8_bytes,
          "The RenderSceneV1 aggregate RGBA8 byte count");
      if (total_rgba8_bytes > limits.max_total_rgba8_bytes) {
        fail("RenderSceneV1 RGBA8 bytes exceed their aggregate caller limit");
      }
      if (mip.width == 1U && mip.height == 1U &&
          level + 1U != texture.mips.size()) {
        fail("RenderSceneV1 texture mip chain continues past its terminal 1x1 level");
      }
      expected_width = std::max(UINT32_C(1), mip.width / 2U);
      expected_height = std::max(UINT32_C(1), mip.height / 2U);
    }
  }

  std::vector<bool> used_textures(scene.textures.size(), false);
  std::vector<bool> used_materials(scene.materials.size(), false);
  for (const auto &material : scene.materials) {
    detail::validate_render_material_extension_v1(material,scene.textures);
    if (!valid_address_mode(material.address_u) ||
        !valid_address_mode(material.address_v) ||
        !valid_filter(material.min_filter) ||
        !valid_filter(material.mag_filter) ||
        !valid_mipmap_filter(material.mipmap_filter) ||
        !valid_alpha_mode(material.alpha_mode)) {
      fail("RenderSceneV1 material uses an unknown bounded render policy");
    }
    if (material.alpha_mode == RenderSceneAlphaModeV1::opaque) {
      if (material.alpha_cutoff_rgba8 != 0U) {
        fail("RenderSceneV1 opaque material has a non-zero alpha cutoff");
      }
    } else if (material.alpha_cutoff_rgba8 == 0U) {
      fail("RenderSceneV1 masked material has a zero alpha cutoff");
    }

    if (material.base_color_texture_id.has_value()) {
      const auto texture_id = *material.base_color_texture_id;
      if (texture_id >= scene.textures.size()) {
        fail("RenderSceneV1 material references a missing texture");
      }
      used_textures[texture_id] = true;
      if (material.mipmap_filter != RenderSceneMipmapFilterV1::none &&
          scene.textures[texture_id].mips.size() < 2U) {
        fail("RenderSceneV1 material requests mip filtering from a one-level texture");
      }
    } else if (material.address_u != RenderSceneAddressModeV1::repeat ||
               material.address_v != RenderSceneAddressModeV1::repeat ||
               material.min_filter != RenderSceneFilterV1::linear ||
               material.mag_filter != RenderSceneFilterV1::linear ||
               material.mipmap_filter != RenderSceneMipmapFilterV1::none) {
      fail("RenderSceneV1 untextured material has non-canonical sampler fields");
    }
  }

  std::uint64_t total_vertices = 0U;
  std::uint64_t total_indices = 0U;
  std::uint64_t total_draws = 0U;
  for (const auto &mesh : scene.meshes) {
    if (mesh.vertices.empty() || mesh.triangle_indices.empty() ||
        mesh.draw_ranges.empty()) {
      fail("RenderSceneV1 mesh must have vertices, triangle indices, and draws");
    }
    if (mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
      fail("RenderSceneV1 mesh vertex count exceeds its local index width");
    }
    if ((mesh.triangle_indices.size() % 3U) != 0U) {
      fail("RenderSceneV1 mesh index count is not triangle-aligned");
    }
    total_vertices = checked_add(total_vertices, mesh.vertices.size(),
                                 "The RenderSceneV1 vertex count");
    total_indices = checked_add(total_indices, mesh.triangle_indices.size(),
                                "The RenderSceneV1 index count");
    total_draws = checked_add(total_draws, mesh.draw_ranges.size(),
                              "The RenderSceneV1 draw-range count");
    if (total_vertices > limits.max_vertices) {
      fail("RenderSceneV1 vertices exceed their aggregate caller limit");
    }
    if (total_indices > limits.max_triangle_indices) {
      fail("RenderSceneV1 indices exceed their aggregate caller limit");
    }
    if (total_draws > limits.max_draw_ranges) {
      fail("RenderSceneV1 draws exceed their aggregate caller limit");
    }

    for (const auto &vertex : mesh.vertices) {
      validate_canonical_float(vertex.x, "vertex X");
      validate_canonical_float(vertex.y, "vertex Y");
      validate_canonical_float(vertex.z, "vertex Z");
      validate_canonical_float(vertex.u, "vertex U");
      validate_canonical_float(vertex.v, "vertex V");
    }
    for (const auto index : mesh.triangle_indices) {
      if (index >= mesh.vertices.size()) {
        fail("RenderSceneV1 mesh index references a missing local vertex");
      }
    }

    std::uint64_t expected_first_index = 0U;
    for (const auto &draw : mesh.draw_ranges) {
      if (draw.material_id >= scene.materials.size()) {
        fail("RenderSceneV1 draw references a missing material");
      }
      if (expected_first_index > mesh.triangle_indices.size() ||
          draw.first_index != expected_first_index || draw.index_count == 0U ||
          (draw.index_count % 3U) != 0U ||
          draw.index_count > mesh.triangle_indices.size() -
                                 expected_first_index) {
        fail("RenderSceneV1 draws are not a complete canonical triangle partition");
      }
      used_materials[draw.material_id] = true;
      for(std::uint64_t i=0U;i<draw.index_count;++i) {
        const auto &vertex=mesh.vertices[mesh.triangle_indices[static_cast<std::size_t>(draw.first_index+i)]];
        detail::validate_encoded_material_uv_v1(scene.materials[draw.material_id],scene.textures,vertex.u,vertex.v);
      }
      expected_first_index = checked_add(
          expected_first_index, draw.index_count,
          "A RenderSceneV1 draw-range endpoint");
    }
    if (expected_first_index != mesh.triangle_indices.size()) {
      fail("RenderSceneV1 draws do not cover their mesh index buffer exactly");
    }
  }

  std::vector<bool> used_meshes(scene.meshes.size(), false);
  for (const auto &instance : scene.instances) {
    if (instance.mesh_id >= scene.meshes.size()) {
      fail("RenderSceneV1 instance references a missing mesh");
    }
    used_meshes[instance.mesh_id] = true;
    for (const auto value : instance.local_to_world.values) {
      validate_canonical_float(value, "instance affine matrix");
    }
  }

  if (std::find(used_meshes.begin(), used_meshes.end(), false) !=
      used_meshes.end()) {
    fail("RenderSceneV1 contains an uninstantiated mesh");
  }
  if (std::find(used_materials.begin(), used_materials.end(), false) !=
      used_materials.end()) {
    fail("RenderSceneV1 contains an unused material");
  }
  if (std::find(used_textures.begin(), used_textures.end(), false) !=
      used_textures.end()) {
    fail("RenderSceneV1 contains an unused texture");
  }
}

RenderSceneV1 canonicalize_render_scene_v1(RenderSceneV1 scene,
                                            const RenderSceneLimitsV1 limits) {
  sort_by_id(scene.textures);
  sort_by_id(scene.materials);
  sort_by_id(scene.meshes);
  sort_by_id(scene.instances);

  for (auto &mesh : scene.meshes) {
    for (auto &vertex : mesh.vertices) {
      vertex.x = canonical_float(vertex.x, "vertex X");
      vertex.y = canonical_float(vertex.y, "vertex Y");
      vertex.z = canonical_float(vertex.z, "vertex Z");
      vertex.u = canonical_float(vertex.u, "vertex U");
      vertex.v = canonical_float(vertex.v, "vertex V");
    }
  }
  for (auto &instance : scene.instances) {
    for (auto &value : instance.local_to_world.values) {
      value = canonical_float(value, "instance affine matrix");
    }
  }
  validate_render_scene_v1(scene, limits);
  return scene;
}

} // namespace openrc
