#include "openrc/actor_render_bake.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw ActorRenderBakeError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(const std::uint64_t left,
                                             const std::uint64_t right,
                                             const char *const description) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left * right;
}

void require_append_count(const std::size_t current,
                          const std::size_t additional,
                          const std::uint32_t caller_limit,
                          const std::size_t host_limit,
                          const char *const description) {
  const auto current_u64 = static_cast<std::uint64_t>(current);
  const auto additional_u64 = static_cast<std::uint64_t>(additional);
  const auto result = checked_add(current_u64, additional_u64, description);
  if (result > caller_limit ||
      result > std::numeric_limits<std::uint32_t>::max() ||
      current > host_limit || additional > host_limit - current) {
    fail(std::string(description) +
         " exceeds its caller, format, or host limit");
  }
}

void require_aggregate_append(const std::uint64_t current,
                              const std::uint64_t additional,
                              const std::uint64_t caller_limit,
                              const char *const description) {
  if (checked_add(current, additional, description) > caller_limit) {
    fail(std::string(description) + " exceeds its caller limit");
  }
}

[[nodiscard]] std::size_t require_host_size(const std::uint64_t value,
                                            const std::size_t host_limit,
                                            const char *const description) {
  if (value > std::numeric_limits<std::size_t>::max()) {
    fail(std::string(description) + " exceeds size_t");
  }
  const auto result = static_cast<std::size_t>(value);
  if (result > host_limit) {
    fail(std::string(description) + " exceeds the host container limit");
  }
  return result;
}

void validate_base_scene(const RenderSceneV1 &scene,
                         const RenderSceneLimitsV1 limits) {
  try {
    validate_render_scene_v1(scene, limits);
  } catch (const RenderSceneError &error) {
    fail("The base RenderSceneV1 is not canonical: " +
         std::string(error.what()));
  }
}

void validate_library(const ActorLibraryV1 &library,
                      const ActorLibraryLimitsV1 limits) {
  try {
    validate_actor_library_v1(library, limits);
  } catch (const ActorLibraryError &error) {
    fail("The ActorLibraryV1 is not canonical: " + std::string(error.what()));
  }
}

[[nodiscard]] const ActorModelV1 &
resolve_model(const ActorLibraryV1 &library,
              const std::string_view semantic_key) {
  const ActorModelV1 *result = nullptr;
  for (const auto &model : library.models) {
    if (model.semantic_key != semantic_key) {
      continue;
    }
    if (result != nullptr) {
      fail("ActorLibraryV1 repeats the requested model semantic key");
    }
    result = &model;
  }
  if (result == nullptr) {
    fail("ActorLibraryV1 does not contain the requested model semantic key");
  }
  return *result;
}

[[nodiscard]] const ActorRigAssetV1 &resolve_rig(const ActorLibraryV1 &library,
                                                 const ActorModelV1 &model) {
  const ActorRigAssetV1 *result = nullptr;
  for (const auto &rig : library.rigs) {
    if (rig.semantic_key != model.rig_key) {
      continue;
    }
    if (result != nullptr) {
      fail("ActorLibraryV1 repeats the requested model's rig semantic key");
    }
    result = &rig;
  }
  if (result == nullptr) {
    fail("The requested actor model references a missing rig semantic key");
  }
  return *result;
}

[[nodiscard]] ActorPosePaletteV1 bind_palette(const ActorRigAssetV1 &rig,
                                              const ActorPoseLimitsV1 limits) {
  try {
    return build_actor_bind_pose_palette_v1(rig.rig, limits);
  } catch (const ActorPoseError &error) {
    fail("Cannot build the actor bind-pose palette: " +
         std::string(error.what()));
  }
}

[[nodiscard]] std::vector<ActorPosedVertexV1>
pose_mesh(const ActorSkinnedMeshV1 &mesh, const ActorPosePaletteV1 &palette,
          const ActorPoseLimitsV1 limits) {
  try {
    // Bind-pose vertices stay in model space. Instance transforms remain the
    // sole model-to-world operation in the returned RenderSceneV1.
    return pose_actor_mesh_vertices_v1(mesh, palette, ActorAffineTransformV1{},
                                       limits);
  } catch (const ActorPoseError &error) {
    fail("Cannot freeze an actor mesh in bind pose: " +
         std::string(error.what()));
  }
}

struct SceneAggregateCountsV1 {
  std::uint64_t mips = 0U;
  std::uint64_t vertices = 0U;
  std::uint64_t triangle_indices = 0U;
  std::uint64_t draw_ranges = 0U;
  std::uint64_t rgba8_bytes = 0U;
};

[[nodiscard]] SceneAggregateCountsV1
scene_aggregate_counts(const RenderSceneV1 &scene) {
  SceneAggregateCountsV1 result;
  for (const auto &texture : scene.textures) {
    result.mips = checked_add(result.mips, texture.mips.size(),
                              "The base scene mip count");
    for (const auto &mip : texture.mips) {
      result.rgba8_bytes = checked_add(result.rgba8_bytes, mip.rgba8.size(),
                                       "The base scene RGBA8 byte count");
    }
  }
  for (const auto &mesh : scene.meshes) {
    result.vertices = checked_add(result.vertices, mesh.vertices.size(),
                                  "The base scene vertex count");
    result.triangle_indices =
        checked_add(result.triangle_indices, mesh.triangle_indices.size(),
                    "The base scene triangle-index count");
    result.draw_ranges =
        checked_add(result.draw_ranges, mesh.draw_ranges.size(),
                    "The base scene draw-range count");
  }
  return result;
}

[[nodiscard]] SceneAggregateCountsV1
actor_aggregate_counts(const ActorModelV1 &model,
                       const RenderSceneLimitsV1 limits) {
  SceneAggregateCountsV1 result;
  for (const auto &texture : model.textures) {
    if (texture.mips.size() > limits.max_mips_per_texture) {
      fail("An actor texture exceeds the RenderSceneV1 per-texture mip limit");
    }
    result.mips = checked_add(result.mips, texture.mips.size(),
                              "The baked actor mip count");
    std::uint64_t texture_texels = 0U;
    for (const auto &mip : texture.mips) {
      if (mip.width > limits.max_texture_width ||
          mip.height > limits.max_texture_height) {
        fail("An actor texture exceeds the RenderSceneV1 dimension limits");
      }
      texture_texels =
          checked_add(texture_texels,
                      checked_multiply(mip.width, mip.height,
                                       "A baked actor mip texel count"),
                      "A baked actor texture texel count");
      result.rgba8_bytes = checked_add(result.rgba8_bytes, mip.rgba8.size(),
                                       "The baked actor RGBA8 byte count");
    }
    if (texture_texels > limits.max_texels_per_texture) {
      fail("An actor texture exceeds the RenderSceneV1 texel limit");
    }
  }
  for (const auto &mesh : model.meshes) {
    result.vertices = checked_add(result.vertices, mesh.vertices.size(),
                                  "The baked actor vertex count");
    result.triangle_indices =
        checked_add(result.triangle_indices, mesh.triangle_indices.size(),
                    "The baked actor triangle-index count");
    result.draw_ranges =
        checked_add(result.draw_ranges, mesh.draw_ranges.size(),
                    "The baked actor draw-range count");
  }
  if (result.vertices > std::numeric_limits<std::uint32_t>::max()) {
    fail("The merged actor mesh exceeds its local 32-bit vertex domain");
  }
  return result;
}

void preflight_append(const RenderSceneV1 &base_scene,
                      const ActorModelV1 &model,
                      const std::span<const RenderSceneAffine3x4V1> transforms,
                      const RenderSceneLimitsV1 limits) {
  require_append_count(base_scene.textures.size(), model.textures.size(),
                       limits.max_textures, base_scene.textures.max_size(),
                       "The baked RenderSceneV1 texture count");
  require_append_count(base_scene.materials.size(), model.materials.size(),
                       limits.max_materials, base_scene.materials.max_size(),
                       "The baked RenderSceneV1 material count");
  require_append_count(base_scene.meshes.size(), 1U, limits.max_meshes,
                       base_scene.meshes.max_size(),
                       "The baked RenderSceneV1 mesh count");
  require_append_count(base_scene.instances.size(), transforms.size(),
                       limits.max_instances, base_scene.instances.max_size(),
                       "The baked RenderSceneV1 instance count");

  const auto base_counts = scene_aggregate_counts(base_scene);
  const auto actor_counts = actor_aggregate_counts(model, limits);
  require_aggregate_append(base_counts.mips, actor_counts.mips,
                           limits.max_total_texture_mips,
                           "The baked RenderSceneV1 mip count");
  require_aggregate_append(base_counts.vertices, actor_counts.vertices,
                           limits.max_vertices,
                           "The baked RenderSceneV1 vertex count");
  require_aggregate_append(base_counts.triangle_indices,
                           actor_counts.triangle_indices,
                           limits.max_triangle_indices,
                           "The baked RenderSceneV1 triangle-index count");
  require_aggregate_append(base_counts.draw_ranges, actor_counts.draw_ranges,
                           limits.max_draw_ranges,
                           "The baked RenderSceneV1 draw-range count");
  require_aggregate_append(base_counts.rgba8_bytes, actor_counts.rgba8_bytes,
                           limits.max_total_rgba8_bytes,
                           "The baked RenderSceneV1 RGBA8 byte count");

  for (const auto &transform : transforms) {
    for (const auto value : transform.values) {
      if (!std::isfinite(value)) {
        fail("A baked actor instance transform contains a non-finite value");
      }
    }
  }
}

void append_textures(RenderSceneV1 &scene, const ActorModelV1 &model,
                     const std::uint32_t texture_offset) {
  scene.textures.reserve(scene.textures.size() + model.textures.size());
  for (const auto &source : model.textures) {
    auto texture = source;
    texture.id = texture_offset + source.id;
    scene.textures.push_back(std::move(texture));
  }
}

void append_materials(RenderSceneV1 &scene, const ActorModelV1 &model,
                      const std::uint32_t texture_offset,
                      const std::uint32_t material_offset) {
  scene.materials.reserve(scene.materials.size() + model.materials.size());
  for (const auto &source : model.materials) {
    auto material = source;
    material.id = material_offset + source.id;
    if (material.base_color_texture_id) {
      material.base_color_texture_id =
          texture_offset + *material.base_color_texture_id;
    }
    scene.materials.push_back(std::move(material));
  }
}

[[nodiscard]] RenderSceneMeshV1 merge_bind_pose_meshes(
    const ActorModelV1 &model, const ActorPosePaletteV1 &palette,
    const ActorPoseLimitsV1 pose_limits, const std::uint32_t mesh_id,
    const std::uint32_t material_offset,
    const SceneAggregateCountsV1 actor_counts) {
  RenderSceneMeshV1 result;
  result.id = mesh_id;
  result.vertices.reserve(require_host_size(actor_counts.vertices,
                                            result.vertices.max_size(),
                                            "The merged actor vertex count"));
  result.triangle_indices.reserve(require_host_size(
      actor_counts.triangle_indices, result.triangle_indices.max_size(),
      "The merged actor triangle-index count"));
  result.draw_ranges.reserve(
      require_host_size(actor_counts.draw_ranges, result.draw_ranges.max_size(),
                        "The merged actor draw-range count"));

  for (const auto &source_mesh : model.meshes) {
    const auto posed = pose_mesh(source_mesh, palette, pose_limits);
    const auto vertex_offset =
        static_cast<std::uint32_t>(result.vertices.size());
    const auto index_offset =
        static_cast<std::uint64_t>(result.triangle_indices.size());

    for (const auto &vertex : posed) {
      result.vertices.push_back(RenderSceneVertexV1{
          vertex.x, vertex.y, vertex.z, vertex.u, vertex.v, vertex.rgba8});
    }
    for (const auto source_index : source_mesh.triangle_indices) {
      const auto destination = checked_add(vertex_offset, source_index,
                                           "A merged actor vertex index");
      if (destination > std::numeric_limits<std::uint32_t>::max()) {
        fail("A merged actor vertex index exceeds uint32_t");
      }
      result.triangle_indices.push_back(
          static_cast<std::uint32_t>(destination));
    }
    for (const auto &source_draw : source_mesh.draw_ranges) {
      const auto material_id =
          checked_add(material_offset, source_draw.material_id,
                      "A merged actor draw material ID");
      if (material_id > std::numeric_limits<std::uint32_t>::max()) {
        fail("A merged actor draw material ID exceeds uint32_t");
      }
      result.draw_ranges.push_back(RenderSceneDrawRangeV1{
          static_cast<std::uint32_t>(material_id),
          checked_add(index_offset, source_draw.first_index,
                      "A merged actor draw first index"),
          source_draw.index_count});
    }
  }
  return result;
}

void require_preserved_prefix(const RenderSceneV1 &base,
                              const RenderSceneV1 &result) {
  const auto preserved = [](const auto &expected, const auto &actual) {
    return actual.size() >= expected.size() &&
           std::equal(expected.begin(), expected.end(), actual.begin());
  };
  if (!preserved(base.textures, result.textures) ||
      !preserved(base.materials, result.materials) ||
      !preserved(base.meshes, result.meshes) ||
      !preserved(base.instances, result.instances)) {
    fail("Actor bind-pose baking changed an existing scene resource");
  }
}

} // namespace

ActorRenderBakeResultV1 bake_actor_bind_pose_to_render_scene_v1(
    const RenderSceneV1 &base_scene, const ActorLibraryV1 &actor_library,
    const std::string_view model_semantic_key,
    const std::span<const RenderSceneAffine3x4V1> instance_transforms,
    const ActorLibraryLimitsV1 actor_library_limits,
    const ActorPoseLimitsV1 actor_pose_limits,
    const RenderSceneLimitsV1 render_scene_limits) {
  validate_base_scene(base_scene, render_scene_limits);
  validate_library(actor_library, actor_library_limits);
  const auto &model = resolve_model(actor_library, model_semantic_key);
  const auto &rig = resolve_rig(actor_library, model);
  const auto palette = bind_palette(rig, actor_pose_limits);

  if (instance_transforms.empty()) {
    return ActorRenderBakeResultV1{base_scene, {}};
  }
  preflight_append(base_scene, model, instance_transforms, render_scene_limits);
  const auto actor_counts = actor_aggregate_counts(model, render_scene_limits);

  RenderSceneV1 scene = base_scene;
  const auto texture_offset = static_cast<std::uint32_t>(scene.textures.size());
  const auto material_offset =
      static_cast<std::uint32_t>(scene.materials.size());
  const auto mesh_id = static_cast<std::uint32_t>(scene.meshes.size());
  const auto first_instance_id =
      static_cast<std::uint32_t>(scene.instances.size());

  append_textures(scene, model, texture_offset);
  append_materials(scene, model, texture_offset, material_offset);
  scene.meshes.push_back(merge_bind_pose_meshes(model, palette,
                                                actor_pose_limits, mesh_id,
                                                material_offset, actor_counts));

  std::vector<std::uint32_t> instance_ids;
  instance_ids.reserve(instance_transforms.size());
  scene.instances.reserve(scene.instances.size() + instance_transforms.size());
  for (std::size_t index = 0U; index < instance_transforms.size(); ++index) {
    const auto id =
        checked_add(first_instance_id, index, "A baked actor instance ID");
    if (id > std::numeric_limits<std::uint32_t>::max()) {
      fail("A baked actor instance ID exceeds uint32_t");
    }
    const auto instance_id = static_cast<std::uint32_t>(id);
    scene.instances.push_back(RenderSceneInstanceV1{
        instance_id, mesh_id, instance_transforms[index]});
    instance_ids.push_back(instance_id);
  }

  RenderSceneV1 canonical;
  try {
    canonical =
        canonicalize_render_scene_v1(std::move(scene), render_scene_limits);
  } catch (const RenderSceneError &error) {
    fail("Cannot canonicalize the baked RenderSceneV1: " +
         std::string(error.what()));
  }
  require_preserved_prefix(base_scene, canonical);
  return ActorRenderBakeResultV1{std::move(canonical), std::move(instance_ids)};
}

} // namespace openrc
