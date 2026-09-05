#include "openrc/rac_destructible_scene_compile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
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
  throw RacDestructibleSceneCompileError(message);
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
                          const std::uint64_t additional,
                          const std::uint64_t caller_limit,
                          const std::uint64_t format_limit,
                          const std::size_t host_limit,
                          const char *const description) {
  const auto current_u64 = static_cast<std::uint64_t>(current);
  if (current_u64 > caller_limit || additional > caller_limit - current_u64 ||
      current_u64 > format_limit || additional > format_limit - current_u64 ||
      current > host_limit ||
      additional > static_cast<std::uint64_t>(host_limit - current)) {
    fail(std::string(description) +
         " exceeds its caller, format, or host limit");
  }
}

void validate_base_render_scene(const RenderSceneV1 &scene,
                                const RenderSceneLimitsV1 limits) {
  try {
    validate_render_scene_v1(scene, limits);
  } catch (const RenderSceneError &error) {
    fail("The base RenderSceneV1 is not canonical: " +
         std::string(error.what()));
  }
}

void validate_base_entity_scene(const EntitySceneV1 &scene,
                                const EntitySceneLimitsV1 limits) {
  try {
    validate_entity_scene_v1(scene, limits);
  } catch (const EntitySceneError &error) {
    fail("The base EntitySceneV1 is not canonical: " +
         std::string(error.what()));
  }
}

void validate_base_destructible_scene(const DestructibleSceneV1 &scene,
                                      const DestructibleSceneLimitsV1 limits) {
  try {
    validate_destructible_scene_v1(scene, limits);
  } catch (const DestructibleSceneError &error) {
    fail("The base DestructibleSceneV1 is not canonical: " +
         std::string(error.what()));
  }
}

template <typename Item>
[[nodiscard]] bool contains_authored_id(const std::vector<Item> &items,
                                        const std::uint32_t authored_id) {
  const auto found =
      std::lower_bound(items.begin(), items.end(), authored_id,
                       [](const Item &item, const std::uint32_t id) {
                         return item.authored_id < id;
                       });
  return found != items.end() && found->authored_id == authored_id;
}

void validate_scene_relationships(
    const RenderSceneV1 &render_scene, const EntitySceneV1 &entity_scene,
    const DestructibleSceneV1 &destructible_scene) {
  if (entity_scene.level_id != destructible_scene.level_id) {
    fail("The entity and destructible scenes name different levels");
  }
  for (const auto &binding : entity_scene.render_bindings) {
    if (binding.render_instance_id >= render_scene.instances.size() ||
        render_scene.instances[binding.render_instance_id].id !=
            binding.render_instance_id) {
      fail("An entity render binding references a missing render instance");
    }
  }
  for (const auto &destructible : destructible_scene.destructibles) {
    if (!contains_authored_id(entity_scene.definitions,
                              destructible.authored_id) ||
        !contains_authored_id(entity_scene.transforms,
                              destructible.authored_id)) {
      fail("A destructible references a missing transformed entity");
    }
  }
}

[[nodiscard]] std::vector<DestructibleDropV1>
validate_profile(const RacDestructibleCompileProfileV1 &profile,
                 const EntitySceneLimitsV1 entity_limits,
                 const DestructibleSceneLimitsV1 destructible_limits) {
  EntitySceneV1 entity_probe;
  entity_probe.definitions.push_back(
      EntityDefinitionV1{0U, profile.archetype_key});
  entity_probe.transforms.push_back(EntityTransformComponentV1{0U, {}});
  try {
    static_cast<void>(
        canonicalize_entity_scene_v1(std::move(entity_probe), entity_limits));
  } catch (const EntitySceneError &error) {
    fail("The destructible profile is not valid for EntitySceneV1: " +
         std::string(error.what()));
  }

  DestructibleSceneV1 destructible_probe;
  destructible_probe.destructibles.push_back(
      DestructibleDefinitionV1{0U,
                               profile.max_health,
                               profile.accepted_damage_channels,
                               {},
                               1.0F,
                               0U,
                               profile.drops});
  try {
    auto canonical = canonicalize_destructible_scene_v1(
        std::move(destructible_probe), destructible_limits);
    return std::move(canonical.destructibles.front().drops);
  } catch (const DestructibleSceneError &error) {
    fail("The destructible profile is not valid for DestructibleSceneV1: " +
         std::string(error.what()));
  }
}

struct HitSphereV1 {
  std::array<float, 3U> center{};
  float radius = 0.0F;
};

[[nodiscard]] float checked_scaled_bound(const float source,
                                         const float class_scale,
                                         const char *const description) {
  if (!std::isfinite(source)) {
    fail(std::string("The RAC1 source class has a non-finite ") + description);
  }
  const auto scaled =
      static_cast<double>(source) * static_cast<double>(class_scale) / 1024.0;
  if (!std::isfinite(scaled) ||
      scaled < -static_cast<double>(std::numeric_limits<float>::max()) ||
      scaled > static_cast<double>(std::numeric_limits<float>::max())) {
    fail(std::string("The RAC1 source class ") + description +
         " overflows the neutral float domain");
  }
  return static_cast<float>(scaled);
}

void validate_texture_slot_map(const RacLevelMobyModelV1 &model) {
  if (model.used_texture_slot_count > model.texture_slots.size()) {
    fail("The RAC1 destructible texture-slot count exceeds its fixed map");
  }
  for (std::size_t index = 0U; index < model.texture_slots.size(); ++index) {
    const auto value = model.texture_slots[index];
    if (index < model.used_texture_slot_count) {
      if (value == kUnusedTextureSlot) {
        fail("The RAC1 destructible texture map has an early 0xff sentinel");
      }
    } else if (value != kUnusedTextureSlot) {
      fail("The RAC1 destructible texture map has data after its used slots");
    }
  }
}

[[nodiscard]] HitSphereV1
validate_source_model(const RacLevelMobyModelV1 &model,
                      const RacDestructibleCompileProfileV1 &profile,
                      const RacDestructibleSceneCompileLimitsV1 &limits) {
  if (model.class_id != profile.source_class_id) {
    fail("The RAC1 destructible model class does not match its profile");
  }
  if (model.joint_count != model.source_class.joint_count) {
    fail("The RAC1 destructible model and source-class joint identities "
         "disagree");
  }
  if (model.joint_count != 0U || model.source_class.joint_count != 0U ||
      model.high_lod.requires_bind_transforms) {
    fail("The RAC1 destructible model must be static");
  }
  if (model.high_lod.lod != RacMobyLodV1::high) {
    fail("The RAC1 destructible model must be a high-LOD assembly");
  }
  if (model.high_lod.vertices.empty() || model.high_lod.triangles.empty()) {
    fail("The RAC1 destructible model has no renderable high-LOD geometry");
  }
  if (model.high_lod.vertices.size() > limits.max_source_vertices ||
      model.high_lod.triangles.size() > limits.max_source_triangles) {
    fail("The RAC1 destructible model exceeds an explicit source limit");
  }
  validate_texture_slot_map(model);

  if (!std::isfinite(model.source_class.scale) ||
      !(model.source_class.scale > 0.0F)) {
    fail("The RAC1 destructible source class has an invalid model scale");
  }
  HitSphereV1 result;
  for (std::size_t component = 0U; component < result.center.size();
       ++component) {
    result.center[component] = checked_scaled_bound(
        model.source_class.bounding_sphere[component], model.source_class.scale,
        "bounding-sphere center");
  }
  result.radius =
      checked_scaled_bound(model.source_class.bounding_sphere[3U],
                           model.source_class.scale, "bounding-sphere radius");
  if (!(result.radius > 0.0F)) {
    fail("The scaled RAC1 destructible hit radius must be positive");
  }
  return result;
}

[[nodiscard]] const RacLevelMobyTextureV1 &
resolve_source_texture(const RacLevelMobyTextureBankV1 &bank,
                       const std::uint32_t global_index,
                       const RenderSceneLimitsV1 &limits) {
  if (global_index >= bank.textures.size()) {
    fail("A RAC1 destructible material references a missing decoded texture");
  }
  const auto &texture = bank.textures[global_index];
  if (texture.global_index != global_index) {
    fail("A RAC1 destructible texture global index disagrees with its bank "
         "position");
  }
  if (texture.entry.width <= 0 || texture.entry.height <= 0) {
    fail("A RAC1 destructible texture has invalid dimensions");
  }
  const auto width = static_cast<std::uint64_t>(texture.entry.width);
  const auto height = static_cast<std::uint64_t>(texture.entry.height);
  const auto texels = checked_multiply(
      width, height, "A RAC1 destructible texture texel count");
  const auto rgba_bytes = checked_multiply(
      texels, 4U, "A RAC1 destructible texture RGBA8 byte count");
  if (texture.indices.size() != texels || texture.rgba.size() != rgba_bytes) {
    fail("A RAC1 destructible texture has inconsistent decoded pixel "
         "buffers");
  }
  if (width > limits.max_texture_width || height > limits.max_texture_height ||
      texels > limits.max_texels_per_texture) {
    fail("A RAC1 destructible texture exceeds the neutral dimension limits");
  }
  return texture;
}

struct PreparedMaterialV1 {
  std::optional<std::uint32_t> global_texture_index;
  std::optional<std::uint32_t> local_texture_id;
};

struct PreparedModelV1 {
  std::vector<std::uint32_t> used_vertex_indices;
  std::vector<std::uint32_t> triangle_indices;
  std::vector<RenderSceneDrawRangeV1> draw_ranges;
  std::vector<std::uint32_t> global_texture_indices;
  std::vector<PreparedMaterialV1> materials;
};

[[nodiscard]] std::uint32_t
resolve_material(PreparedModelV1 &prepared, const RacLevelMobyModelV1 &model,
                 const RacLevelMobyTextureBankV1 &texture_bank,
                 const std::int32_t local_texture_index,
                 const RenderSceneLimitsV1 &limits) {
  std::optional<std::uint32_t> global_index;
  if (local_texture_index == -1) {
    global_index = std::nullopt;
  } else {
    if (local_texture_index < -1) {
      fail("A static RAC1 destructible triangle has an invalid negative "
           "texture slot");
    }
    const auto local_slot = static_cast<std::uint32_t>(local_texture_index);
    if (local_slot >= model.used_texture_slot_count ||
        local_slot >= model.texture_slots.size()) {
      fail("A RAC1 destructible triangle references an unavailable local "
           "texture slot");
    }
    global_index = static_cast<std::uint32_t>(model.texture_slots[local_slot]);
    if (*global_index == kUnusedTextureSlot) {
      fail("A RAC1 destructible triangle references an unused texture slot");
    }
  }

  const auto existing =
      std::find_if(prepared.materials.begin(), prepared.materials.end(),
                   [global_index](const PreparedMaterialV1 &material) {
                     return material.global_texture_index == global_index;
                   });
  if (existing != prepared.materials.end()) {
    return static_cast<std::uint32_t>(
        std::distance(prepared.materials.begin(), existing));
  }
  if (prepared.materials.size() >= std::numeric_limits<std::uint32_t>::max() ||
      prepared.materials.size() >= prepared.materials.max_size()) {
    fail("The prepared RAC1 destructible material domain exceeds its host or "
         "format limit");
  }

  std::optional<std::uint32_t> local_texture_id;
  if (global_index) {
    static_cast<void>(
        resolve_source_texture(texture_bank, *global_index, limits));
    if (prepared.global_texture_indices.size() >=
            std::numeric_limits<std::uint32_t>::max() ||
        prepared.global_texture_indices.size() >=
            prepared.global_texture_indices.max_size()) {
      fail("The prepared RAC1 destructible texture domain exceeds its host or "
           "format limit");
    }
    local_texture_id =
        static_cast<std::uint32_t>(prepared.global_texture_indices.size());
    prepared.global_texture_indices.push_back(*global_index);
  }
  const auto material_id =
      static_cast<std::uint32_t>(prepared.materials.size());
  prepared.materials.push_back(
      PreparedMaterialV1{global_index, local_texture_id});
  return material_id;
}

void append_draw_range(PreparedModelV1 &prepared,
                       const std::uint32_t material_id) {
  const auto first_index =
      static_cast<std::uint64_t>(prepared.triangle_indices.size());
  if (!prepared.draw_ranges.empty()) {
    auto &previous = prepared.draw_ranges.back();
    if (previous.material_id == material_id &&
        previous.first_index + previous.index_count == first_index) {
      previous.index_count =
          checked_add(previous.index_count, 3U,
                      "A prepared RAC1 destructible draw-range index count");
      return;
    }
  }
  if (prepared.draw_ranges.size() >= prepared.draw_ranges.max_size()) {
    fail("The prepared RAC1 destructible draw-range count exceeds the host "
         "container");
  }
  prepared.draw_ranges.push_back(
      RenderSceneDrawRangeV1{material_id, first_index, 3U});
}

[[nodiscard]] PreparedModelV1
prepare_model(const RacLevelMobyModelV1 &model,
              const RacLevelMobyTextureBankV1 &texture_bank,
              const RenderSceneLimitsV1 &render_limits) {
  const auto triangle_index_count =
      checked_multiply(model.high_lod.triangles.size(), 3U,
                       "The RAC1 destructible source triangle-index count");
  if (triangle_index_count >
      static_cast<std::uint64_t>(std::vector<std::uint32_t>{}.max_size())) {
    fail("The RAC1 destructible triangle indices exceed the host container");
  }

  for (const auto &vertex : model.high_lod.vertices) {
    for (const auto value : vertex.diagnostic_position) {
      if (!std::isfinite(value)) {
        fail("A RAC1 destructible vertex has a non-finite position");
      }
    }
    for (const auto value : vertex.texture_coordinate) {
      if (!std::isfinite(value)) {
        fail("A RAC1 destructible vertex has a non-finite texture coordinate");
      }
    }
  }

  PreparedModelV1 result;
  result.triangle_indices.reserve(
      static_cast<std::size_t>(triangle_index_count));
  result.draw_ranges.reserve(model.high_lod.triangles.size());
  std::map<std::uint32_t, std::uint32_t> compact_indices;
  for (const auto &triangle : model.high_lod.triangles) {
    const auto material_id = resolve_material(
        result, model, texture_bank, triangle.texture_index, render_limits);
    append_draw_range(result, material_id);
    for (const auto source_index : triangle.vertex_indices) {
      if (source_index >= model.high_lod.vertices.size()) {
        fail("A RAC1 destructible triangle references a missing source "
             "vertex");
      }
      auto compact = compact_indices.find(source_index);
      if (compact == compact_indices.end()) {
        if (result.used_vertex_indices.size() >=
                std::numeric_limits<std::uint32_t>::max() ||
            result.used_vertex_indices.size() >=
                result.used_vertex_indices.max_size()) {
          fail("The compacted RAC1 destructible vertices exceed the neutral "
               "index domain");
        }
        const auto destination =
            static_cast<std::uint32_t>(result.used_vertex_indices.size());
        result.used_vertex_indices.push_back(source_index);
        compact = compact_indices.emplace(source_index, destination).first;
      }
      result.triangle_indices.push_back(compact->second);
    }
  }
  return result;
}

struct RenderTotalsV1 {
  std::uint64_t texture_mips = 0U;
  std::uint64_t rgba8_bytes = 0U;
  std::uint64_t vertices = 0U;
  std::uint64_t triangle_indices = 0U;
  std::uint64_t draw_ranges = 0U;
};

[[nodiscard]] RenderTotalsV1 render_totals(const RenderSceneV1 &scene) {
  RenderTotalsV1 result;
  for (const auto &texture : scene.textures) {
    result.texture_mips =
        checked_add(result.texture_mips, texture.mips.size(),
                    "The base RenderSceneV1 texture-mip count");
    for (const auto &mip : texture.mips) {
      result.rgba8_bytes =
          checked_add(result.rgba8_bytes, mip.rgba8.size(),
                      "The base RenderSceneV1 RGBA8 byte count");
    }
  }
  for (const auto &mesh : scene.meshes) {
    result.vertices = checked_add(result.vertices, mesh.vertices.size(),
                                  "The base RenderSceneV1 vertex count");
    result.triangle_indices =
        checked_add(result.triangle_indices, mesh.triangle_indices.size(),
                    "The base RenderSceneV1 triangle-index count");
    result.draw_ranges =
        checked_add(result.draw_ranges, mesh.draw_ranges.size(),
                    "The base RenderSceneV1 draw-range count");
  }
  return result;
}

void preflight_render_output(const RenderSceneV1 &base,
                             const PreparedModelV1 &prepared,
                             const RacLevelMobyTextureBankV1 &texture_bank,
                             const std::size_t match_count,
                             const RenderSceneLimitsV1 &limits) {
  const auto has_instances = match_count != 0U;
  const auto texture_count =
      has_instances ? prepared.global_texture_indices.size() : 0U;
  const auto material_count = has_instances ? prepared.materials.size() : 0U;
  const auto mesh_count = has_instances ? 1U : 0U;
  const auto vertex_count =
      has_instances ? prepared.used_vertex_indices.size() : 0U;
  const auto index_count =
      has_instances ? prepared.triangle_indices.size() : 0U;
  const auto draw_count = has_instances ? prepared.draw_ranges.size() : 0U;

  require_append_count(base.textures.size(), texture_count, limits.max_textures,
                       UINT32_MAX, base.textures.max_size(),
                       "The destructible render-texture count");
  require_append_count(
      base.materials.size(), material_count, limits.max_materials, UINT32_MAX,
      base.materials.max_size(), "The destructible render-material count");
  require_append_count(base.meshes.size(), mesh_count, limits.max_meshes,
                       UINT32_MAX, base.meshes.max_size(),
                       "The destructible render-mesh count");
  require_append_count(base.instances.size(), match_count, limits.max_instances,
                       UINT32_MAX, base.instances.max_size(),
                       "The destructible render-instance count");

  const auto totals = render_totals(base);
  if (checked_add(totals.texture_mips, texture_count,
                  "The resulting render texture-mip count") >
      limits.max_total_texture_mips) {
    fail("The destructible render textures exceed the aggregate mip limit");
  }
  if (checked_add(totals.vertices, vertex_count,
                  "The resulting render vertex count") > limits.max_vertices) {
    fail("The destructible render mesh exceeds the aggregate vertex limit");
  }
  if (checked_add(totals.triangle_indices, index_count,
                  "The resulting render triangle-index count") >
      limits.max_triangle_indices) {
    fail("The destructible render mesh exceeds the aggregate index limit");
  }
  if (checked_add(totals.draw_ranges, draw_count,
                  "The resulting render draw-range count") >
      limits.max_draw_ranges) {
    fail("The destructible render mesh exceeds the aggregate draw limit");
  }
  if (vertex_count > UINT32_MAX) {
    fail("The destructible render mesh exceeds its local index width");
  }
  if (vertex_count > static_cast<std::uint64_t>(
                         std::vector<RenderSceneVertexV1>{}.max_size())) {
    fail("The destructible render vertices exceed the host mesh container");
  }

  auto rgba8_bytes = totals.rgba8_bytes;
  if (has_instances) {
    for (const auto global_index : prepared.global_texture_indices) {
      const auto &source =
          resolve_source_texture(texture_bank, global_index, limits);
      rgba8_bytes =
          checked_add(rgba8_bytes, source.rgba.size(),
                      "The resulting destructible render RGBA8 byte count");
    }
  }
  if (rgba8_bytes > limits.max_total_rgba8_bytes) {
    fail("The destructible render textures exceed the aggregate RGBA8 "
         "limit");
  }
}

[[nodiscard]] std::uint64_t entity_key_bytes(const EntitySceneV1 &scene) {
  std::uint64_t result = 0U;
  for (const auto &definition : scene.definitions) {
    result = checked_add(result, definition.archetype_key.size(),
                         "The base entity key-byte count");
  }
  for (const auto &binding : scene.actor_bindings) {
    result = checked_add(result, binding.model_key.size(),
                         "The base entity key-byte count");
  }
  return result;
}

struct DestructibleTotalsV1 {
  std::uint64_t drops = 0U;
  std::uint64_t key_bytes = 0U;
};

[[nodiscard]] DestructibleTotalsV1
destructible_totals(const DestructibleSceneV1 &scene) {
  DestructibleTotalsV1 result;
  for (const auto &definition : scene.destructibles) {
    result.drops = checked_add(result.drops, definition.drops.size(),
                               "The base destructible drop count");
    for (const auto &drop : definition.drops) {
      result.key_bytes =
          checked_add(result.key_bytes, drop.item_key.size(),
                      "The base destructible drop-key byte count");
    }
  }
  return result;
}

void preflight_entity_and_destructible_output(
    const EntitySceneV1 &base_entity,
    const DestructibleSceneV1 &base_destructible, const std::size_t match_count,
    const RacDestructibleCompileProfileV1 &profile,
    const std::span<const DestructibleDropV1> canonical_drops,
    const RacDestructibleSceneCompileLimitsV1 &limits) {
  require_append_count(base_entity.definitions.size(), match_count,
                       limits.entity_scene.max_definitions, UINT32_MAX,
                       base_entity.definitions.max_size(),
                       "The destructible entity-definition count");
  require_append_count(base_entity.transforms.size(), match_count,
                       limits.entity_scene.max_transforms, UINT32_MAX,
                       base_entity.transforms.max_size(),
                       "The destructible entity-transform count");
  require_append_count(base_entity.render_bindings.size(), match_count,
                       limits.entity_scene.max_render_bindings, UINT32_MAX,
                       base_entity.render_bindings.max_size(),
                       "The destructible entity render-binding count");
  require_append_count(base_destructible.destructibles.size(), match_count,
                       limits.destructible_scene.max_destructibles, UINT32_MAX,
                       base_destructible.destructibles.max_size(),
                       "The destructible-definition count");

  const auto added_entity_keys =
      checked_multiply(match_count, profile.archetype_key.size(),
                       "The appended destructible entity key-byte count");
  if (checked_add(entity_key_bytes(base_entity), added_entity_keys,
                  "The resulting destructible entity key-byte count") >
      limits.entity_scene.max_total_key_bytes) {
    fail("The resulting EntitySceneV1 exceeds its aggregate key-byte limit");
  }

  std::uint64_t profile_key_bytes = 0U;
  for (const auto &drop : canonical_drops) {
    profile_key_bytes =
        checked_add(profile_key_bytes, drop.item_key.size(),
                    "The destructible profile drop-key byte count");
  }
  const auto added_drops =
      checked_multiply(match_count, canonical_drops.size(),
                       "The appended destructible drop count");
  const auto added_drop_key_bytes =
      checked_multiply(match_count, profile_key_bytes,
                       "The appended destructible drop-key byte count");
  const auto base_totals = destructible_totals(base_destructible);
  if (checked_add(base_totals.drops, added_drops,
                  "The resulting destructible drop count") >
      limits.destructible_scene.max_total_drops) {
    fail("The resulting DestructibleSceneV1 exceeds its aggregate drop "
         "limit");
  }
  if (checked_add(base_totals.key_bytes, added_drop_key_bytes,
                  "The resulting destructible drop-key byte count") >
      limits.destructible_scene.max_total_key_bytes) {
    fail("The resulting DestructibleSceneV1 exceeds its aggregate key-byte "
         "limit");
  }
}

struct CompiledPlacementV1 {
  std::uint32_t authored_id = 0U;
  RenderSceneAffine3x4V1 render_transform;
  game::WorldTransformV1 entity_transform;
};

[[nodiscard]] CompiledPlacementV1
compile_placement(const RacGameplayMobyInstanceV1 &source,
                  const std::uint32_t authored_id) {
  if (!std::isfinite(source.scale) || !(source.scale > 0.0F)) {
    fail("A matching RAC1 destructible placement has an invalid scale");
  }
  for (const auto value : source.position) {
    if (!std::isfinite(value)) {
      fail("A matching RAC1 destructible placement has a non-finite "
           "position");
    }
  }
  for (const auto value : source.rotation) {
    if (!std::isfinite(value)) {
      fail("A matching RAC1 destructible placement has a non-finite "
           "rotation");
    }
  }

  const auto rx = static_cast<double>(source.rotation[0U]);
  const auto ry = static_cast<double>(source.rotation[1U]);
  const auto rz = static_cast<double>(source.rotation[2U]);
  const auto sx = std::sin(rx);
  const auto cx = std::cos(rx);
  const auto sy = std::sin(ry);
  const auto cy = std::cos(ry);
  const auto sz = std::sin(rz);
  const auto cz = std::cos(rz);
  const auto scale = static_cast<double>(source.scale);

  CompiledPlacementV1 result;
  result.authored_id = authored_id;
  // T * S * Rz * Ry * Rx, with uniform S commuting with the rotations.
  result.render_transform.values = {
      static_cast<float>(scale * (cz * cy)),
      static_cast<float>(scale * (cz * sy * sx - sz * cx)),
      static_cast<float>(scale * (cz * sy * cx + sz * sx)),
      source.position[0U],
      static_cast<float>(scale * (sz * cy)),
      static_cast<float>(scale * (sz * sy * sx + cz * cx)),
      static_cast<float>(scale * (sz * sy * cx - cz * sx)),
      source.position[1U],
      static_cast<float>(scale * -sy),
      static_cast<float>(scale * (cy * sx)),
      static_cast<float>(scale * (cy * cx)),
      source.position[2U],
  };

  const auto half_x = rx * 0.5;
  const auto half_y = ry * 0.5;
  const auto half_z = rz * 0.5;
  const auto qxs = std::sin(half_x);
  const auto qxc = std::cos(half_x);
  const auto qys = std::sin(half_y);
  const auto qyc = std::cos(half_y);
  const auto qzs = std::sin(half_z);
  const auto qzc = std::cos(half_z);
  result.entity_transform.position = source.position;
  result.entity_transform.rotation = {
      static_cast<float>(qzc * qyc * qxs - qzs * qys * qxc),
      static_cast<float>(qzc * qys * qxc + qzs * qyc * qxs),
      static_cast<float>(qzs * qyc * qxc - qzc * qys * qxs),
      static_cast<float>(qzc * qyc * qxc + qzs * qys * qxs),
  };
  result.entity_transform.scale = {source.scale, source.scale, source.scale};

  for (const auto value : result.render_transform.values) {
    if (!std::isfinite(value)) {
      fail("A matching RAC1 destructible transform overflows RenderSceneV1");
    }
  }
  for (const auto value : result.entity_transform.rotation) {
    if (!std::isfinite(value)) {
      fail("A matching RAC1 destructible rotation overflows EntitySceneV1");
    }
  }
  return result;
}

[[nodiscard]] RenderSceneV1
append_render_model(const RenderSceneV1 &base,
                    const RacLevelMobyModelV1 &source_model,
                    const RacLevelMobyTextureBankV1 &texture_bank,
                    const PreparedModelV1 &prepared,
                    const std::span<const CompiledPlacementV1> placements,
                    std::vector<std::uint32_t> &instance_ids,
                    const RenderSceneLimitsV1 limits) {
  RenderSceneV1 scene = base;
  if (placements.empty()) {
    try {
      return canonicalize_render_scene_v1(std::move(scene), limits);
    } catch (const RenderSceneError &error) {
      fail("Cannot canonicalize the no-op RenderSceneV1: " +
           std::string(error.what()));
    }
  }

  scene.textures.reserve(scene.textures.size() +
                         prepared.global_texture_indices.size());
  scene.materials.reserve(scene.materials.size() + prepared.materials.size());
  scene.meshes.reserve(scene.meshes.size() + 1U);
  scene.instances.reserve(scene.instances.size() + placements.size());

  const auto first_texture_id =
      static_cast<std::uint32_t>(scene.textures.size());
  for (const auto global_index : prepared.global_texture_indices) {
    const auto &source =
        resolve_source_texture(texture_bank, global_index, limits);
    RenderSceneTextureV1 texture;
    texture.id = static_cast<std::uint32_t>(scene.textures.size());
    texture.color_space = RenderSceneTextureColorSpaceV1::srgb;
    texture.mips.push_back(RenderSceneTextureMipV1{
        static_cast<std::uint32_t>(source.entry.width),
        static_cast<std::uint32_t>(source.entry.height), source.rgba});
    scene.textures.push_back(std::move(texture));
  }

  const auto first_material_id =
      static_cast<std::uint32_t>(scene.materials.size());
  for (const auto &prepared_material : prepared.materials) {
    RenderSceneMaterialV1 material;
    material.id = static_cast<std::uint32_t>(scene.materials.size());
    if (prepared_material.local_texture_id) {
      material.base_color_texture_id =
          first_texture_id + *prepared_material.local_texture_id;
      material.alpha_mode = RenderSceneAlphaModeV1::mask;
      material.alpha_cutoff_rgba8 = 1U;
    }
    material.base_color_rgba8 = kWhiteRgba8;
    material.use_vertex_color = true;
    material.double_sided = true;
    scene.materials.push_back(material);
  }

  RenderSceneMeshV1 mesh;
  mesh.id = static_cast<std::uint32_t>(scene.meshes.size());
  mesh.vertices.reserve(prepared.used_vertex_indices.size());
  for (const auto source_index : prepared.used_vertex_indices) {
    const auto &source = source_model.high_lod.vertices[source_index];
    mesh.vertices.push_back(RenderSceneVertexV1{
        source.diagnostic_position[0U], source.diagnostic_position[1U],
        source.diagnostic_position[2U], source.texture_coordinate[0U],
        source.texture_coordinate[1U], kWhiteRgba8});
  }
  mesh.triangle_indices = prepared.triangle_indices;
  mesh.draw_ranges.reserve(prepared.draw_ranges.size());
  for (const auto &source : prepared.draw_ranges) {
    mesh.draw_ranges.push_back(
        RenderSceneDrawRangeV1{first_material_id + source.material_id,
                               source.first_index, source.index_count});
  }
  const auto mesh_id = mesh.id;
  scene.meshes.push_back(std::move(mesh));

  instance_ids.reserve(placements.size());
  for (const auto &placement : placements) {
    const auto instance_id = static_cast<std::uint32_t>(scene.instances.size());
    scene.instances.push_back(RenderSceneInstanceV1{
        instance_id, mesh_id, placement.render_transform});
    instance_ids.push_back(instance_id);
  }

  try {
    return canonicalize_render_scene_v1(std::move(scene), limits);
  } catch (const RenderSceneError &error) {
    fail("Cannot canonicalize the compiled RenderSceneV1: " +
         std::string(error.what()));
  }
}

template <typename Item>
[[nodiscard]] bool preserved_dense_prefix(const std::vector<Item> &base,
                                          const std::vector<Item> &result) {
  return result.size() >= base.size() &&
         std::equal(base.begin(), base.end(), result.begin());
}

template <typename Item>
[[nodiscard]] bool preserved_by_authored_id(const std::vector<Item> &base,
                                            const std::vector<Item> &result) {
  auto result_cursor = result.begin();
  for (const auto &expected : base) {
    result_cursor =
        std::lower_bound(result_cursor, result.end(), expected.authored_id,
                         [](const Item &item, const std::uint32_t id) {
                           return item.authored_id < id;
                         });
    if (result_cursor == result.end() ||
        result_cursor->authored_id != expected.authored_id ||
        *result_cursor != expected) {
      return false;
    }
    ++result_cursor;
  }
  return true;
}

void require_preserved_records(const RenderSceneV1 &base_render,
                               const RenderSceneV1 &render,
                               const EntitySceneV1 &base_entity,
                               const EntitySceneV1 &entity,
                               const DestructibleSceneV1 &base_destructible,
                               const DestructibleSceneV1 &destructible) {
  if (base_render.schema_version != render.schema_version ||
      !preserved_dense_prefix(base_render.textures, render.textures) ||
      !preserved_dense_prefix(base_render.materials, render.materials) ||
      !preserved_dense_prefix(base_render.meshes, render.meshes) ||
      !preserved_dense_prefix(base_render.instances, render.instances) ||
      base_entity.schema_version != entity.schema_version ||
      base_entity.level_id != entity.level_id ||
      !preserved_by_authored_id(base_entity.definitions, entity.definitions) ||
      !preserved_by_authored_id(base_entity.transforms, entity.transforms) ||
      !preserved_by_authored_id(base_entity.render_bindings,
                                entity.render_bindings) ||
      !preserved_by_authored_id(base_entity.actor_bindings,
                                entity.actor_bindings) ||
      !preserved_by_authored_id(base_entity.player_bindings,
                                entity.player_bindings) ||
      base_destructible.schema_version != destructible.schema_version ||
      base_destructible.level_id != destructible.level_id ||
      !preserved_by_authored_id(base_destructible.destructibles,
                                destructible.destructibles)) {
    fail("RAC1 destructible compilation changed a base-scene record");
  }
}

} // namespace

RacDestructibleSceneCompileResultV1 compile_rac_destructible_scene_v1(
    const RenderSceneV1 &base_render_scene,
    const EntitySceneV1 &base_entity_scene,
    const DestructibleSceneV1 &base_destructible_scene,
    const RacLevelMobyModelV1 &source_model,
    const RacLevelMobyTextureBankV1 &texture_bank,
    const std::span<const RacGameplayMobyInstanceV1> static_mobies,
    const RacDestructibleCompileProfileV1 &profile,
    const RacDestructibleSceneCompileLimitsV1 limits) {
  if (limits.max_source_instances == 0U || limits.max_source_vertices == 0U ||
      limits.max_source_triangles == 0U ||
      static_mobies.size() > limits.max_source_instances ||
      static_mobies.size() > static_cast<std::uint64_t>(UINT32_MAX) + 1U) {
    fail("The RAC1 destructible source limits are zero or exceeded");
  }

  validate_base_render_scene(base_render_scene, limits.render_scene);
  validate_base_entity_scene(base_entity_scene, limits.entity_scene);
  validate_base_destructible_scene(base_destructible_scene,
                                   limits.destructible_scene);
  validate_scene_relationships(base_render_scene, base_entity_scene,
                               base_destructible_scene);
  const auto canonical_drops =
      validate_profile(profile, limits.entity_scene, limits.destructible_scene);
  const auto hit_sphere = validate_source_model(source_model, profile, limits);
  const auto prepared_model =
      prepare_model(source_model, texture_bank, limits.render_scene);

  std::size_t match_count = 0U;
  for (std::size_t ordinal = 0U; ordinal < static_mobies.size(); ++ordinal) {
    if (static_mobies[ordinal].class_id != profile.source_class_id) {
      continue;
    }
    if (ordinal > UINT32_MAX) {
      fail("A matching RAC1 static-moby ordinal exceeds authored-ID width");
    }
    const auto authored_id = static_cast<std::uint32_t>(ordinal);
    if (contains_authored_id(base_entity_scene.definitions, authored_id) ||
        contains_authored_id(base_destructible_scene.destructibles,
                             authored_id)) {
      fail("A RAC1 destructible authored ID collides with a base scene");
    }
    if (match_count == std::numeric_limits<std::size_t>::max()) {
      fail("The matching RAC1 destructible count overflows size_t");
    }
    ++match_count;
  }

  preflight_render_output(base_render_scene, prepared_model, texture_bank,
                          match_count, limits.render_scene);
  preflight_entity_and_destructible_output(base_entity_scene,
                                           base_destructible_scene, match_count,
                                           profile, canonical_drops, limits);
  require_append_count(0U, match_count, UINT32_MAX, UINT32_MAX,
                       std::vector<CompiledPlacementV1>{}.max_size(),
                       "The compiled RAC1 destructible placement count");
  require_append_count(0U, match_count, UINT32_MAX, UINT32_MAX,
                       std::vector<std::uint32_t>{}.max_size(),
                       "The compiled RAC1 destructible authored-ID count");

  std::vector<CompiledPlacementV1> placements;
  std::vector<std::uint32_t> authored_ids;
  placements.reserve(match_count);
  authored_ids.reserve(match_count);
  for (std::size_t ordinal = 0U; ordinal < static_mobies.size(); ++ordinal) {
    const auto &source = static_mobies[ordinal];
    if (source.class_id != profile.source_class_id) {
      continue;
    }
    const auto authored_id = static_cast<std::uint32_t>(ordinal);
    placements.push_back(compile_placement(source, authored_id));
    authored_ids.push_back(authored_id);
  }

  std::vector<std::uint32_t> render_instance_ids;
  auto render_scene = append_render_model(
      base_render_scene, source_model, texture_bank, prepared_model, placements,
      render_instance_ids, limits.render_scene);
  if (render_instance_ids.size() != placements.size()) {
    fail("The compiled render-instance ID table is not parallel to RAC1 "
         "placements");
  }

  EntitySceneV1 entity_scene = base_entity_scene;
  entity_scene.definitions.reserve(entity_scene.definitions.size() +
                                   placements.size());
  entity_scene.transforms.reserve(entity_scene.transforms.size() +
                                  placements.size());
  entity_scene.render_bindings.reserve(entity_scene.render_bindings.size() +
                                       placements.size());
  DestructibleSceneV1 destructible_scene = base_destructible_scene;
  destructible_scene.destructibles.reserve(
      destructible_scene.destructibles.size() + placements.size());

  for (std::size_t index = 0U; index < placements.size(); ++index) {
    const auto &placement = placements[index];
    entity_scene.definitions.push_back(EntityDefinitionV1{
        placement.authored_id, profile.archetype_key,
        kEntityDefinitionInitiallyEnabledV1, kEntitySceneNoAuthoringGroupIdV1});
    entity_scene.transforms.push_back(EntityTransformComponentV1{
        placement.authored_id, placement.entity_transform});
    entity_scene.render_bindings.push_back(EntityRenderBindingV1{
        placement.authored_id, render_instance_ids[index]});
    destructible_scene.destructibles.push_back(DestructibleDefinitionV1{
        placement.authored_id, profile.max_health,
        profile.accepted_damage_channels, hit_sphere.center, hit_sphere.radius,
        0U, canonical_drops});
  }

  try {
    entity_scene = canonicalize_entity_scene_v1(std::move(entity_scene),
                                                limits.entity_scene);
  } catch (const EntitySceneError &error) {
    fail("Cannot canonicalize the compiled EntitySceneV1: " +
         std::string(error.what()));
  }
  try {
    destructible_scene = canonicalize_destructible_scene_v1(
        std::move(destructible_scene), limits.destructible_scene);
  } catch (const DestructibleSceneError &error) {
    fail("Cannot canonicalize the compiled DestructibleSceneV1: " +
         std::string(error.what()));
  }

  validate_scene_relationships(render_scene, entity_scene, destructible_scene);
  require_preserved_records(base_render_scene, render_scene, base_entity_scene,
                            entity_scene, base_destructible_scene,
                            destructible_scene);
  return RacDestructibleSceneCompileResultV1{
      std::move(render_scene), std::move(entity_scene),
      std::move(destructible_scene), std::move(authored_ids),
      std::move(render_instance_ids)};
}

} // namespace openrc
