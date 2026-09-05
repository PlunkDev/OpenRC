#include "openrc/rac_destructible_scene_compile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_near(const float actual, const float expected,
                 const std::string &message) {
  if (!std::isfinite(actual) || !std::isfinite(expected) ||
      std::abs(actual - expected) > 0.00002F) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] openrc::RacLevelMobyTextureV1
make_texture(const std::uint32_t global_index, const std::int16_t width,
             const std::int16_t height, const std::uint8_t marker) {
  openrc::RacLevelMobyTextureV1 result;
  result.global_index = global_index;
  result.entry.width = width;
  result.entry.height = height;
  const auto texels =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  result.indices.resize(texels, std::byte{0U});
  result.rgba.resize(texels * 4U, static_cast<std::byte>(marker));
  return result;
}

[[nodiscard]] openrc::RacMobyModelVertexV1
make_vertex(const std::array<float, 3U> position,
            const std::array<float, 2U> texture_coordinate) {
  openrc::RacMobyModelVertexV1 result;
  result.diagnostic_position = position;
  result.texture_coordinate = texture_coordinate;
  return result;
}

[[nodiscard]] openrc::RacGameplayMobyInstanceV1 make_placement(
    const std::uint32_t class_id, const std::array<float, 3U> position = {},
    const std::array<float, 3U> rotation = {}, const float scale = 1.0F) {
  openrc::RacGameplayMobyInstanceV1 result;
  result.class_id = class_id;
  result.position = position;
  result.rotation = rotation;
  result.scale = scale;
  return result;
}

[[nodiscard]] openrc::RenderSceneV1 make_base_render_scene() {
  openrc::RenderSceneMaterialV1 material;
  material.id = 0U;

  openrc::RenderSceneMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, UINT32_C(0xffffffff)},
      {1.0F, 0.0F, 0.0F, 1.0F, 0.0F, UINT32_C(0xffffffff)},
      {0.0F, 1.0F, 0.0F, 0.0F, 1.0F, UINT32_C(0xffffffff)},
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};

  openrc::RenderSceneInstanceV1 instance;
  instance.id = 0U;
  instance.mesh_id = 0U;

  openrc::RenderSceneV1 result;
  result.materials.push_back(material);
  result.meshes.push_back(std::move(mesh));
  result.instances.push_back(instance);
  return result;
}

[[nodiscard]] openrc::EntitySceneV1 make_base_entity_scene() {
  openrc::EntitySceneV1 result;
  result.level_id = 7U;
  result.definitions.push_back({100U, "base/crate",
                                openrc::kEntityDefinitionInitiallyEnabledV1,
                                openrc::kEntitySceneNoAuthoringGroupIdV1});
  result.transforms.push_back({100U, {}});
  result.render_bindings.push_back({100U, 0U});
  return result;
}

[[nodiscard]] openrc::DestructibleSceneV1 make_base_destructible_scene() {
  openrc::DestructibleSceneV1 result;
  result.level_id = 7U;
  result.destructibles.push_back({100U,
                                  10U,
                                  openrc::game::kDamageChannelEnvironmentV1,
                                  {0.0F, 0.0F, 0.0F},
                                  1.0F,
                                  0U,
                                  {{"base/item", 1U, 0U}}});
  return result;
}

[[nodiscard]] openrc::RacDestructibleSceneCompileLimitsV1 make_limits() {
  openrc::RacDestructibleSceneCompileLimitsV1 result;
  result.max_source_instances = 64U;
  result.max_source_vertices = 64U;
  result.max_source_triangles = 64U;
  result.render_scene = {
      16U, 1U, 16U, 16U, 8U, 16U, 16U, 16U, 16U, 256U, 64U, 192U, 4096U,
  };
  result.entity_scene = {
      16U, 16U, 16U, 16U, 16U, 64U, 64U, 2048U,
  };
  result.destructible_scene = {
      16U, 64U, 8U, 64U, 4096U, 1000U, 1000U,
      1000.0F, 1000.0F,
  };
  return result;
}

struct FixtureV1 {
  openrc::RenderSceneV1 base_render = make_base_render_scene();
  openrc::EntitySceneV1 base_entity = make_base_entity_scene();
  openrc::DestructibleSceneV1 base_destructible =
      make_base_destructible_scene();
  openrc::RacLevelMobyModelV1 model;
  openrc::RacLevelMobyTextureBankV1 texture_bank;
  std::vector<openrc::RacGameplayMobyInstanceV1> placements;
  openrc::RacDestructibleCompileProfileV1 profile;
  openrc::RacDestructibleSceneCompileLimitsV1 limits = make_limits();

  FixtureV1() {
    model.class_id = 42U;
    model.joint_count = 0U;
    model.source_class.joint_count = 0U;
    model.source_class.scale = 2048.0F;
    model.source_class.bounding_sphere = {1.0F, -2.0F, 3.0F, 4.0F};
    model.texture_slots.fill(0xffU);
    model.texture_slots[0U] = 2U;
    model.texture_slots[1U] = 1U;
    model.used_texture_slot_count = 2U;
    model.high_lod.lod = openrc::RacMobyLodV1::high;
    model.high_lod.requires_bind_transforms = false;
    model.high_lod.vertices = {
        make_vertex({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F}),
        make_vertex({1.0F, 0.0F, 0.0F}, {1.0F, 0.0F}),
        make_vertex({0.0F, 1.0F, 0.0F}, {0.0F, 1.0F}),
        make_vertex({0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}),
        make_vertex({99.0F, 99.0F, 99.0F}, {0.5F, 0.5F}),
    };
    model.high_lod.triangles = {
        {{2U, 0U, 1U}, 1, 0U}, {{1U, 3U, 2U}, 1, 0U}, {{0U, 3U, 1U}, -1, 0U},
        {{3U, 2U, 0U}, 0, 0U}, {{2U, 3U, 1U}, 1, 0U},
    };

    texture_bank.textures = {
        make_texture(0U, 1, 1, 0x10U),
        make_texture(1U, 2, 1, 0x20U),
        make_texture(2U, 1, 2, 0x30U),
    };
    placements = {
        make_placement(7U, {90.0F, 90.0F, 90.0F}),
        make_placement(42U, {10.0F, 20.0F, 30.0F}),
        make_placement(42U, {-4.0F, 5.0F, 6.0F}, {0.25F, -0.5F, 1.0F}, 2.0F),
    };
    profile.source_class_id = 42U;
    profile.archetype_key = "props/destructible-crate";
    profile.max_health = 50U;
    profile.accepted_damage_channels = openrc::game::kDamageChannelMeleeV1 |
                                       openrc::game::kDamageChannelExplosiveV1;
    // Deliberately reversed to exercise canonical per-definition drop order.
    profile.drops = {
        {"items/raritanium", 2U, 0U},
        {"items/bolts", 5U, 0U},
    };
  }
};

[[nodiscard]] openrc::RacDestructibleSceneCompileResultV1
compile(const FixtureV1 &fixture) {
  return openrc::compile_rac_destructible_scene_v1(
      fixture.base_render, fixture.base_entity, fixture.base_destructible,
      fixture.model, fixture.texture_bank, fixture.placements, fixture.profile,
      fixture.limits);
}

template <typename Mutation>
void expect_rejected(Mutation &&mutation, const std::string &message) {
  FixtureV1 fixture;
  std::invoke(std::forward<Mutation>(mutation), fixture);
  try {
    static_cast<void>(compile(fixture));
  } catch (const openrc::RacDestructibleSceneCompileError &) {
    return;
  }
  throw std::runtime_error(message);
}

template <typename Item>
[[nodiscard]] const Item &find_authored(const std::vector<Item> &items,
                                        const std::uint32_t authored_id) {
  const auto found =
      std::find_if(items.begin(), items.end(), [authored_id](const Item &item) {
        return item.authored_id == authored_id;
      });
  if (found == items.end()) {
    throw std::runtime_error("missing expected authored record");
  }
  return *found;
}

[[nodiscard]] openrc::RenderSceneAffine3x4V1
matrix_from_entity_transform(const openrc::game::WorldTransformV1 &transform) {
  const auto x = transform.rotation[0U];
  const auto y = transform.rotation[1U];
  const auto z = transform.rotation[2U];
  const auto w = transform.rotation[3U];
  openrc::RenderSceneAffine3x4V1 result;
  result.values = {
      transform.scale[0U] * (1.0F - 2.0F * (y * y + z * z)),
      transform.scale[1U] * (2.0F * (x * y - z * w)),
      transform.scale[2U] * (2.0F * (x * z + y * w)),
      transform.position[0U],
      transform.scale[0U] * (2.0F * (x * y + z * w)),
      transform.scale[1U] * (1.0F - 2.0F * (x * x + z * z)),
      transform.scale[2U] * (2.0F * (y * z - x * w)),
      transform.position[1U],
      transform.scale[0U] * (2.0F * (x * z - y * w)),
      transform.scale[1U] * (2.0F * (y * z + x * w)),
      transform.scale[2U] * (1.0F - 2.0F * (x * x + y * y)),
      transform.position[2U],
  };
  return result;
}

void test_filtered_shared_mesh_ordinals_and_determinism() {
  const FixtureV1 fixture;
  const auto first = compile(fixture);
  const auto second = compile(fixture);
  expect(first == second, "RAC1 destructible compilation is not deterministic");
  expect(first.authored_ids == std::vector<std::uint32_t>{1U, 2U} &&
             first.render_instance_ids == std::vector<std::uint32_t>{1U, 2U},
         "class filtering lost complete-table authored ordinals");

  expect(first.render_scene.meshes.size() == 2U &&
             first.render_scene.instances.size() == 3U &&
             first.render_scene.instances[1U].mesh_id == 1U &&
             first.render_scene.instances[2U].mesh_id == 1U,
         "matching placements did not share exactly one appended mesh");
  expect(first.entity_scene.definitions.size() == 3U &&
             first.entity_scene.transforms.size() == 3U &&
             first.entity_scene.render_bindings.size() == 3U &&
             first.destructible_scene.destructibles.size() == 3U,
         "matching placements did not create parallel neutral records");
  expect(find_authored(first.entity_scene.render_bindings, 1U)
                     .render_instance_id == 1U &&
             find_authored(first.entity_scene.render_bindings, 2U)
                     .render_instance_id == 2U,
         "entity render bindings are not parallel to compiled instances");

  expect(first.render_scene.materials[0U] ==
                 fixture.base_render.materials[0U] &&
             first.render_scene.meshes[0U] == fixture.base_render.meshes[0U] &&
             first.render_scene.instances[0U] ==
                 fixture.base_render.instances[0U] &&
             find_authored(first.entity_scene.definitions, 100U) ==
                 fixture.base_entity.definitions[0U] &&
             find_authored(first.entity_scene.transforms, 100U) ==
                 fixture.base_entity.transforms[0U] &&
             find_authored(first.entity_scene.render_bindings, 100U) ==
                 fixture.base_entity.render_bindings[0U] &&
             find_authored(first.destructible_scene.destructibles, 100U) ==
                 fixture.base_destructible.destructibles[0U],
         "a base scene record changed during layered compilation");
}

void test_compaction_texture_material_and_bounds_mapping() {
  const FixtureV1 fixture;
  const auto result = compile(fixture);
  const auto &mesh = result.render_scene.meshes[1U];
  expect(mesh.vertices.size() == 4U &&
             mesh.triangle_indices ==
                 std::vector<std::uint32_t>{0U, 1U, 2U, 2U, 3U, 0U, 1U, 3U, 2U,
                                            3U, 0U, 1U, 0U, 3U, 2U},
         "triangle order or first-reference vertex compaction changed");
  constexpr std::array<std::uint32_t, 4U> kSourceOrder{2U, 0U, 1U, 3U};
  for (std::size_t index = 0U; index < kSourceOrder.size(); ++index) {
    const auto &actual = mesh.vertices[index];
    const auto &source = fixture.model.high_lod.vertices[kSourceOrder[index]];
    expect(actual.x == source.diagnostic_position[0U] &&
               actual.y == source.diagnostic_position[1U] &&
               actual.z == source.diagnostic_position[2U] &&
               actual.u == source.texture_coordinate[0U] &&
               actual.v == source.texture_coordinate[1U] &&
               actual.rgba8 == UINT32_C(0xffffffff),
           "a compacted model-local vertex changed");
  }

  expect(result.render_scene.textures.size() == 2U &&
             result.render_scene.textures[0U].mips[0U].rgba8.front() ==
                 std::byte{0x20U} &&
             result.render_scene.textures[1U].mips[0U].rgba8.front() ==
                 std::byte{0x30U},
         "only first-used decoded RAC1 textures were not appended densely");
  expect(result.render_scene.materials.size() == 4U &&
             result.render_scene.materials[1U].base_color_texture_id == 0U &&
             !result.render_scene.materials[2U].base_color_texture_id &&
             result.render_scene.materials[3U].base_color_texture_id == 1U &&
             result.render_scene.materials[1U].base_color_rgba8 ==
                 UINT32_C(0xffffffff) &&
             result.render_scene.materials[1U].use_vertex_color &&
             result.render_scene.materials[1U].double_sided &&
             result.render_scene.materials[1U].alpha_mode ==
                 openrc::RenderSceneAlphaModeV1::mask &&
             result.render_scene.materials[1U].alpha_cutoff_rgba8 == 1U &&
             result.render_scene.materials[2U].alpha_mode ==
                 openrc::RenderSceneAlphaModeV1::opaque &&
             result.render_scene.materials[2U].alpha_cutoff_rgba8 == 0U,
         "the appended neutral material policy or texture mapping is wrong");
  expect(mesh.draw_ranges ==
             std::vector<openrc::RenderSceneDrawRangeV1>{
                 {1U, 0U, 6U}, {2U, 6U, 3U}, {3U, 9U, 3U}, {1U, 12U, 3U}},
         "source triangle/material run order was not preserved");

  for (const auto authored_id : result.authored_ids) {
    const auto &definition =
        find_authored(result.destructible_scene.destructibles, authored_id);
    expect(definition.max_health == fixture.profile.max_health &&
               definition.accepted_damage_channels ==
                   fixture.profile.accepted_damage_channels &&
               definition.local_hit_center ==
                   std::array<float, 3U>{2.0F, -4.0F, 6.0F} &&
               definition.hit_radius == 8.0F && definition.flags == 0U &&
               definition.drops ==
                   std::vector<openrc::DestructibleDropV1>{
                       {"items/bolts", 5U, 0U}, {"items/raritanium", 2U, 0U}},
           "profile or scaled RAC1 class bounds were not copied exactly");
  }
}

void test_render_and_entity_transform_equivalence() {
  const FixtureV1 fixture;
  const auto result = compile(fixture);
  for (std::size_t index = 0U; index < result.authored_ids.size(); ++index) {
    const auto authored_id = result.authored_ids[index];
    const auto &entity =
        find_authored(result.entity_scene.transforms, authored_id).transform;
    const auto &render =
        result.render_scene.instances[result.render_instance_ids[index]]
            .local_to_world;
    const auto reconstructed = matrix_from_entity_transform(entity);
    for (std::size_t component = 0U; component < render.values.size();
         ++component) {
      expect_near(reconstructed.values[component], render.values[component],
                  "entity quaternion/scale does not reproduce render affine");
    }
  }

  const auto &identity = result.render_scene.instances[1U].local_to_world;
  expect(identity.values == std::array<float, 12U>{1.0F, 0.0F, 0.0F, 10.0F,
                                                   0.0F, 1.0F, 0.0F, 20.0F,
                                                   0.0F, 0.0F, 1.0F, 30.0F},
         "translation or identity placement transform is wrong");
  const auto &rotated_entity =
      find_authored(result.entity_scene.transforms, 2U).transform;
  expect(rotated_entity.position == std::array<float, 3U>{-4.0F, 5.0F, 6.0F} &&
             rotated_entity.scale == std::array<float, 3U>{2.0F, 2.0F, 2.0F},
         "entity placement position or uniform scale changed");
}

void test_empty_filter_is_a_validated_no_op() {
  FixtureV1 fixture;
  fixture.placements = {make_placement(7U), make_placement(8U)};
  const auto result = compile(fixture);
  expect(result.render_scene == fixture.base_render &&
             result.entity_scene == fixture.base_entity &&
             result.destructible_scene == fixture.base_destructible &&
             result.authored_ids.empty() && result.render_instance_ids.empty(),
         "an empty class filter was not an exact validated no-op");
}

void test_invalid_model_geometry_slots_and_textures_are_rejected() {
  expect_rejected([](auto &fixture) { fixture.model.class_id = 41U; },
                  "a model with the wrong source class identity was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.model.high_lod.lod = openrc::RacMobyLodV1::low;
      },
      "a low-LOD model entered the static destructible adapter");
  expect_rejected(
      [](auto &fixture) {
        fixture.model.high_lod.requires_bind_transforms = true;
      },
      "an animated model entered the static destructible adapter");
  expect_rejected(
      [](auto &fixture) { fixture.model.source_class.joint_count = 1U; },
      "inconsistent static model identity was accepted");
  expect_rejected(
      [](auto &fixture) { fixture.model.high_lod.triangles.clear(); },
      "an empty high-LOD model was accepted");
  expect_rejected(
      [](auto &fixture) { fixture.model.texture_slots[1U] = 0xffU; },
      "an early texture-slot sentinel was accepted");
  expect_rejected([](auto &fixture) { fixture.model.texture_slots[2U] = 0U; },
                  "texture data after the used slot prefix was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.model.high_lod.triangles[0U].texture_index = 2;
      },
      "an unavailable local texture slot was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.model.high_lod.triangles[0U].texture_index = -2;
      },
      "an invalid negative texture slot was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.model.high_lod.triangles[0U].vertex_indices[0U] = 99U;
      },
      "a triangle referencing a missing source vertex was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.model.high_lod.vertices.back().diagnostic_position[0U] =
            std::numeric_limits<float>::quiet_NaN();
      },
      "a non-finite source vertex was accepted");
  expect_rejected([](auto &fixture) { fixture.model.texture_slots[1U] = 9U; },
                  "a missing decoded global texture was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.texture_bank.textures[1U].global_index = 0U;
      },
      "a decoded texture with a displaced global identity was accepted");
  expect_rejected(
      [](auto &fixture) { fixture.texture_bank.textures[1U].entry.width = 0; },
      "a referenced zero-width decoded texture was accepted");
  expect_rejected(
      [](auto &fixture) { fixture.texture_bank.textures[1U].rgba.pop_back(); },
      "an inexact decoded RGBA8 payload was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.texture_bank.textures[1U].indices.pop_back();
      },
      "an inexact decoded index payload was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.model.source_class.bounding_sphere[3U] = 0.0F;
      },
      "a non-positive source bounding sphere was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.model.source_class.scale =
            std::numeric_limits<float>::infinity();
      },
      "a non-finite source class scale was accepted");
}

void test_placements_profiles_relationships_and_collisions_are_rejected() {
  expect_rejected(
      [](auto &fixture) {
        fixture.placements[1U].rotation[0U] =
            std::numeric_limits<float>::quiet_NaN();
      },
      "a non-finite matching placement was accepted");
  expect_rejected([](auto &fixture) { fixture.placements[1U].scale = 0.0F; },
                  "a zero-scale matching placement was accepted");
  expect_rejected(
      [](auto &fixture) { fixture.profile.accepted_damage_channels = 0U; },
      "an empty accepted-damage mask was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.profile.accepted_damage_channels = UINT32_C(0x80000000);
      },
      "an unknown accepted-damage channel was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.profile.drops.push_back({"items/bolts", 1U, 0U});
      },
      "duplicate destructible drop keys were accepted");
  expect_rejected(
      [](auto &fixture) { fixture.profile.archetype_key = "Bad Key"; },
      "a non-canonical entity archetype key was accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.base_entity.definitions.insert(
            fixture.base_entity.definitions.begin(),
            {1U, "base/collision", openrc::kEntityDefinitionInitiallyEnabledV1,
             openrc::kEntitySceneNoAuthoringGroupIdV1});
        fixture.base_entity.transforms.insert(
            fixture.base_entity.transforms.begin(), {1U, {}});
      },
      "a matching complete-table ordinal collision was accepted");
  expect_rejected(
      [](auto &fixture) { fixture.base_destructible.level_id = 8U; },
      "base scenes naming different levels were accepted");
  expect_rejected(
      [](auto &fixture) {
        fixture.base_entity.render_bindings[0U].render_instance_id = 8U;
      },
      "a dangling base entity render binding was accepted");
}

void test_every_limit_domain_is_preflighted() {
  expect_rejected(
      [](auto &fixture) { fixture.limits.max_source_instances = 2U; },
      "the source instance limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.max_source_vertices = 4U; },
      "the source vertex limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.max_source_triangles = 4U; },
      "the source triangle limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.render_scene.max_textures = 1U; },
      "the output texture count limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.render_scene.max_materials = 3U; },
      "the output material count limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.render_scene.max_meshes = 1U; },
      "the output mesh count limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.render_scene.max_instances = 2U; },
      "the output render-instance count limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.render_scene.max_vertices = 6U; },
      "the aggregate render vertex limit was ignored");
  expect_rejected(
      [](auto &fixture) {
        fixture.limits.render_scene.max_triangle_indices = 17U;
      },
      "the aggregate render index limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.render_scene.max_draw_ranges = 4U; },
      "the aggregate render draw-range limit was ignored");
  expect_rejected(
      [](auto &fixture) {
        fixture.limits.render_scene.max_total_texture_mips = 1U;
      },
      "the aggregate render texture-mip limit was ignored");
  expect_rejected(
      [](auto &fixture) {
        fixture.limits.render_scene.max_total_rgba8_bytes = 15U;
      },
      "the aggregate render RGBA8 limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.render_scene.max_texture_width = 1U; },
      "the referenced texture dimension limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.entity_scene.max_definitions = 2U; },
      "the entity definition count limit was ignored");
  expect_rejected(
      [](auto &fixture) {
        const auto base_bytes =
            fixture.base_entity.definitions[0U].archetype_key.size();
        const auto added = fixture.profile.archetype_key.size() * 2U;
        fixture.limits.entity_scene.max_total_key_bytes =
            base_bytes + added - 1U;
      },
      "the aggregate entity key-byte limit was ignored");
  expect_rejected(
      [](auto &fixture) {
        fixture.limits.destructible_scene.max_destructibles = 2U;
      },
      "the destructible definition count limit was ignored");
  expect_rejected(
      [](auto &fixture) {
        fixture.limits.destructible_scene.max_total_drops = 4U;
      },
      "the aggregate destructible drop limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.destructible_scene.max_health = 49U; },
      "the destructible health limit was ignored");
  expect_rejected(
      [](auto &fixture) {
        fixture.limits.destructible_scene.max_drop_amount = 4U;
      },
      "the destructible drop-amount limit was ignored");
  expect_rejected(
      [](auto &fixture) { fixture.limits.max_source_instances = 0U; },
      "a zero explicit source limit was accepted");
}

} // namespace

int main() {
  try {
    test_filtered_shared_mesh_ordinals_and_determinism();
    test_compaction_texture_material_and_bounds_mapping();
    test_render_and_entity_transform_equivalence();
    test_empty_filter_is_a_validated_no_op();
    test_invalid_model_geometry_slots_and_textures_are_rejected();
    test_placements_profiles_relationships_and_collisions_are_rejected();
    test_every_limit_domain_is_preflighted();
    std::cout << "OpenRC RAC destructible-scene compiler tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OpenRC RAC destructible-scene compiler tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
