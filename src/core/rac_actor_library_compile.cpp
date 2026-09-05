#include "openrc/rac_actor_library_compile.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint8_t kUnusedTextureSlot = 0xffU;
constexpr std::uint32_t kWhiteRgba8 = UINT32_C(0xffffffff);

[[noreturn]] void fail(const std::string &message) {
  throw RacActorLibraryCompileError(message);
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

void validate_slot_map(const RacActorLibraryCompileRequestV1 &request) {
  if (request.used_texture_slot_count > request.texture_slots.size()) {
    fail("The RAC1 actor texture-slot count exceeds its fixed map");
  }
  for (std::size_t index = 0U; index < request.texture_slots.size(); ++index) {
    const auto slot = request.texture_slots[index];
    if (index < request.used_texture_slot_count) {
      if (slot == kUnusedTextureSlot) {
        fail("The RAC1 actor texture map has an early 0xff sentinel");
      }
    } else if (slot != kUnusedTextureSlot) {
      fail("The RAC1 actor texture map has data after its used slots");
    }
  }

  std::map<std::int32_t, bool> seen_special_materials;
  for (const auto &policy : request.special_material_policies) {
    if (policy.source_effect_material_index != -2 &&
        policy.source_effect_material_index != -3) {
      fail("A RAC1 actor special-material policy uses an unproven source "
           "sentinel");
    }
    if (!seen_special_materials
             .emplace(policy.source_effect_material_index, true)
             .second) {
      fail("A RAC1 actor special-material policy is duplicated");
    }
  }
}

[[nodiscard]] const RacLevelMobyTextureV1 &resolve_source_texture(
    const RacActorLibraryCompileRequestV1 &request,
    const std::uint32_t global_index,
    const ActorLibraryLimitsV1 &limits) {
  if (global_index >= request.texture_bank.textures.size()) {
    fail("A RAC1 actor material references a missing decoded texture");
  }
  const auto &texture = request.texture_bank.textures[global_index];
  if (texture.global_index != global_index) {
    fail("A RAC1 actor texture global index disagrees with its bank position");
  }
  if (texture.entry.width <= 0 || texture.entry.height <= 0) {
    fail("A RAC1 actor texture has invalid dimensions");
  }
  const auto width = static_cast<std::uint64_t>(texture.entry.width);
  const auto height = static_cast<std::uint64_t>(texture.entry.height);
  const auto texels = checked_multiply(
      width, height, "The RAC1 actor texture texel count");
  const auto rgba_bytes = checked_multiply(
      texels, 4U, "The RAC1 actor texture RGBA8 byte count");
  if (texture.indices.size() != texels || texture.rgba.size() != rgba_bytes) {
    fail("A RAC1 actor texture has inconsistent decoded pixel buffers");
  }
  if (width > limits.max_texture_width ||
      height > limits.max_texture_height ||
      texels > limits.max_texels_per_texture) {
    fail("A RAC1 actor texture exceeds the neutral dimension limits");
  }
  return texture;
}

struct MaterialState {
  std::map<std::uint32_t, std::uint32_t> texture_ids_by_global_index;
  std::map<std::uint32_t, std::uint32_t> material_ids_by_global_index;
  std::map<std::int32_t, std::uint32_t> special_material_ids_by_source_index;
  std::optional<std::uint32_t> untextured_material_id;
  std::uint64_t total_rgba8_bytes = 0U;
};

[[nodiscard]] std::uint32_t append_texture(
    ActorModelV1 &model, MaterialState &state,
    const RacLevelMobyTextureV1 &source,
    const ActorLibraryLimitsV1 &limits) {
  if (model.textures.size() >= limits.max_textures ||
      model.textures.size() >= limits.max_total_texture_mips ||
      model.textures.size() >= model.textures.max_size() ||
      model.textures.size() >= std::numeric_limits<std::uint32_t>::max()) {
    fail("The neutral actor texture table exceeds its output limits");
  }
  const auto rgba_bytes = static_cast<std::uint64_t>(source.rgba.size());
  if (state.total_rgba8_bytes > limits.max_total_rgba8_bytes ||
      rgba_bytes > limits.max_total_rgba8_bytes -
                       state.total_rgba8_bytes) {
    fail("The neutral actor textures exceed their aggregate RGBA8 limit");
  }
  const auto id = static_cast<std::uint32_t>(model.textures.size());
  RenderSceneTextureV1 texture;
  texture.id = id;
  texture.color_space = RenderSceneTextureColorSpaceV1::srgb;
  texture.mips.push_back(RenderSceneTextureMipV1{
      static_cast<std::uint32_t>(source.entry.width),
      static_cast<std::uint32_t>(source.entry.height), source.rgba});
  model.textures.push_back(std::move(texture));
  state.total_rgba8_bytes += rgba_bytes;
  return id;
}

[[nodiscard]] std::uint32_t append_material(
    ActorModelV1 &model,
    const std::optional<std::uint32_t> texture_id,
    const ActorLibraryLimitsV1 &limits,
    const std::uint32_t base_color_rgba8 = kWhiteRgba8,
    const bool use_vertex_color = true) {
  if (model.materials.size() >= limits.max_materials ||
      model.materials.size() >= model.materials.max_size() ||
      model.materials.size() >= std::numeric_limits<std::uint32_t>::max()) {
    fail("The neutral actor material table exceeds its output limits");
  }
  const auto id = static_cast<std::uint32_t>(model.materials.size());
  RenderSceneMaterialV1 material;
  material.id = id;
  material.base_color_texture_id = texture_id;
  material.base_color_rgba8 = base_color_rgba8;
  material.use_vertex_color = use_vertex_color;
  material.double_sided = true;
  if (texture_id) {
    material.alpha_mode = RenderSceneAlphaModeV1::mask;
    material.alpha_cutoff_rgba8 = 1U;
  }
  model.materials.push_back(material);
  return id;
}

[[nodiscard]] std::uint32_t resolve_material(
    ActorModelV1 &model, MaterialState &state,
    const RacActorLibraryCompileRequestV1 &request,
    const std::int32_t local_texture_index,
    const bool allow_effect_materials,
    const ActorLibraryLimitsV1 &limits) {
  if (local_texture_index == -1) {
    if (!state.untextured_material_id) {
      state.untextured_material_id =
          append_material(model, std::nullopt, limits);
    }
    return *state.untextured_material_id;
  }
  if (local_texture_index < -1) {
    if (!allow_effect_materials) {
      fail("A regular RAC1 actor triangle uses a metal-only effect material");
    }
    const auto existing =
        state.special_material_ids_by_source_index.find(local_texture_index);
    if (existing != state.special_material_ids_by_source_index.end()) {
      return existing->second;
    }
    const auto policy = std::find_if(
        request.special_material_policies.begin(),
        request.special_material_policies.end(),
        [local_texture_index](const RacActorSpecialMaterialPolicyV1 &candidate) {
          return candidate.source_effect_material_index == local_texture_index;
        });
    if (policy == request.special_material_policies.end()) {
      fail("A RAC1 actor effect triangle has no explicit neutral fallback "
           "policy");
    }
    const auto material_id = append_material(
        model, std::nullopt, limits, policy->fallback_base_color_rgba8, false);
    state.special_material_ids_by_source_index.emplace(local_texture_index,
                                                        material_id);
    return material_id;
  }
  const auto local_slot = static_cast<std::uint32_t>(local_texture_index);
  if (local_slot >= request.used_texture_slot_count ||
      local_slot >= request.texture_slots.size()) {
    fail("A RAC1 actor triangle references an unavailable local texture slot");
  }
  const auto global_index =
      static_cast<std::uint32_t>(request.texture_slots[local_slot]);
  if (global_index == kUnusedTextureSlot) {
    fail("A RAC1 actor triangle references an unused texture slot");
  }
  if (const auto found = state.material_ids_by_global_index.find(global_index);
      found != state.material_ids_by_global_index.end()) {
    return found->second;
  }

  const auto &source = resolve_source_texture(request, global_index, limits);
  auto texture_id = state.texture_ids_by_global_index.find(global_index);
  if (texture_id == state.texture_ids_by_global_index.end()) {
    const auto neutral_id = append_texture(model, state, source, limits);
    texture_id = state.texture_ids_by_global_index
                     .emplace(global_index, neutral_id)
                     .first;
  }
  const auto material_id =
      append_material(model, texture_id->second, limits);
  state.material_ids_by_global_index.emplace(global_index, material_id);
  return material_id;
}

void append_draw_range(ActorSkinnedMeshV1 &mesh,
                       const std::uint32_t material_id,
                       std::uint64_t &total_draw_ranges,
                       const ActorLibraryLimitsV1 &limits) {
  const auto first_index =
      static_cast<std::uint64_t>(mesh.triangle_indices.size());
  if (!mesh.draw_ranges.empty()) {
    auto &previous = mesh.draw_ranges.back();
    if (previous.material_id == material_id &&
        previous.first_index + previous.index_count == first_index) {
      previous.index_count += 3U;
      return;
    }
  }
  if (total_draw_ranges >= limits.max_draw_ranges ||
      mesh.draw_ranges.size() >= mesh.draw_ranges.max_size()) {
    fail("The neutral actor draw ranges exceed their output limit");
  }
  mesh.draw_ranges.push_back(
      RenderSceneDrawRangeV1{material_id, first_index, 3U});
  ++total_draw_ranges;
}

[[nodiscard]] ActorSkinnedVertexV1 convert_vertex(
    const RacMobyModelVertexV1 &source,
    const ActorSkinBindingV1 &skin) {
  return ActorSkinnedVertexV1{
      source.diagnostic_position[0U], source.diagnostic_position[1U],
      source.diagnostic_position[2U], source.diagnostic_normal[0U],
      source.diagnostic_normal[1U], source.diagnostic_normal[2U],
      source.texture_coordinate[0U], source.texture_coordinate[1U],
      kWhiteRgba8, skin};
}

void append_mesh(ActorModelV1 &model, MaterialState &material_state,
                 const RacActorLibraryCompileRequestV1 &request,
                 const std::span<const RacMobyModelVertexV1> vertices,
                 const std::span<const RacMobyModelTriangleV1> triangles,
                 const std::span<const ActorSkinBindingV1> skin_bindings,
                 const bool allow_effect_materials,
                 std::uint64_t &total_vertices,
                 std::uint64_t &total_triangle_indices,
                 std::uint64_t &total_draw_ranges,
                 const ActorLibraryLimitsV1 &limits) {
  if (vertices.size() != skin_bindings.size() || vertices.empty() ||
      triangles.empty()) {
    fail("A RAC1 actor mesh has empty or non-parallel geometry and skin "
         "domains");
  }
  if (model.meshes.size() >= limits.max_meshes ||
      model.meshes.size() >= model.meshes.max_size() ||
      model.meshes.size() >= std::numeric_limits<std::uint32_t>::max()) {
    fail("The neutral RAC1 actor mesh table exceeds its output limit");
  }
  ActorSkinnedMeshV1 mesh;
  if (total_triangle_indices > limits.max_triangle_indices ||
      triangles.size() >
          (limits.max_triangle_indices - total_triangle_indices) / 3U ||
      triangles.size() > mesh.triangle_indices.max_size() / 3U) {
    fail("The RAC1 actor triangles exceed the neutral output limit");
  }

  mesh.id = static_cast<std::uint32_t>(model.meshes.size());
  std::map<std::uint32_t, std::uint32_t> compact_indices;
  for (const auto &triangle : triangles) {
    const auto material_id = resolve_material(
        model, material_state, request, triangle.texture_index,
        allow_effect_materials, limits);
    append_draw_range(mesh, material_id, total_draw_ranges, limits);
    for (const auto source_index : triangle.vertex_indices) {
      if (source_index >= vertices.size()) {
        fail("A RAC1 actor triangle references a missing source vertex");
      }
      auto compact = compact_indices.find(source_index);
      if (compact == compact_indices.end()) {
        if (total_vertices >= limits.max_vertices ||
            mesh.vertices.size() >= mesh.vertices.max_size() ||
            mesh.vertices.size() >= std::numeric_limits<std::uint32_t>::max()) {
          fail("The compacted RAC1 actor vertices exceed the neutral output "
               "limit");
        }
        const auto destination =
            static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(
            convert_vertex(vertices[source_index], skin_bindings[source_index]));
        compact = compact_indices.emplace(source_index, destination).first;
        ++total_vertices;
      }
      mesh.triangle_indices.push_back(compact->second);
      ++total_triangle_indices;
    }
  }
  model.meshes.push_back(std::move(mesh));
}

} // namespace

ActorLibraryV1 compile_rac_actor_library_v1(
    const RacActorLibraryCompileRequestV1 &request,
    const ActorLibraryLimitsV1 limits) {
  const auto &bind_pose = request.bind_pose;
  const auto &geometry = bind_pose.geometry;
  if (geometry.lod != RacMobyLodV1::high) {
    fail("The neutral RAC1 actor adapter accepts only high LOD");
  }
  if (!geometry.requires_bind_transforms) {
    fail("The neutral RAC1 actor adapter requires jointed bind-pose geometry");
  }
  if (bind_pose.bind_rig.actor_rig.joints.empty() ||
      bind_pose.bind_rig.source_common_translations.size() !=
          bind_pose.bind_rig.actor_rig.joints.size()) {
    fail("The RAC1 actor bind-rig domains are inconsistent");
  }
  if (limits.max_rigs < 1U || limits.max_models < 1U ||
      limits.max_meshes < 1U ||
      bind_pose.bind_rig.actor_rig.joints.size() >
          limits.max_joints_per_rig ||
      bind_pose.bind_rig.actor_rig.joints.size() > limits.max_total_joints) {
    fail("The RAC1 actor bind rig exceeds the neutral output limits");
  }
  if (geometry.vertices.size() != bind_pose.vertex_skin_bindings.size()) {
    fail("The RAC1 actor geometry and skin-binding domains are not parallel");
  }
  if (geometry.vertices.empty() || geometry.triangles.empty()) {
    fail("The RAC1 actor has no renderable high-LOD geometry");
  }
  validate_slot_map(request);
  const auto required_meshes = request.bind_pose.metal_overlay ? 2U : 1U;
  if (required_meshes > limits.max_meshes) {
    fail("The RAC1 actor mesh count exceeds the neutral output limit");
  }
  if (request.bind_pose.metal_overlay) {
    const auto &metal = *request.bind_pose.metal_overlay;
    if (metal.vertices.size() != metal.vertex_skin_bindings.size() ||
        metal.vertices.empty() || metal.triangles.empty()) {
      fail("The RAC1 actor metal overlay is empty or has non-parallel skin "
           "data");
    }
    for (const auto &triangle : metal.triangles) {
      if (triangle.texture_index != -2 && triangle.texture_index != -3) {
        fail("A RAC1 actor metal overlay uses an unproven effect material");
      }
    }
  }

  ActorLibraryV1 library;
  ActorRigAssetV1 rig;
  rig.id = 0U;
  rig.semantic_key = request.rig_semantic_key;
  rig.rig = bind_pose.bind_rig.actor_rig;
  library.rigs.push_back(std::move(rig));

  ActorModelV1 model;
  model.id = 0U;
  model.semantic_key = request.model_semantic_key;
  model.rig_key = request.rig_semantic_key;
  MaterialState material_state;
  std::uint64_t total_vertices = 0U;
  std::uint64_t total_triangle_indices = 0U;
  std::uint64_t total_draw_ranges = 0U;
  append_mesh(model, material_state, request, geometry.vertices,
              geometry.triangles, bind_pose.vertex_skin_bindings,
              false,
              total_vertices, total_triangle_indices, total_draw_ranges,
              limits);
  if (bind_pose.metal_overlay) {
    const auto &metal = *bind_pose.metal_overlay;
    append_mesh(model, material_state, request, metal.vertices,
                metal.triangles, metal.vertex_skin_bindings, true,
                total_vertices, total_triangle_indices, total_draw_ranges,
                limits);
  }
  library.models.push_back(std::move(model));

  try {
    return canonicalize_actor_library_v1(std::move(library), limits);
  } catch (const ActorLibraryError &error) {
    fail("Neutral ActorLibraryV1 canonicalization failed: " +
         std::string(error.what()));
  }
}

} // namespace openrc
