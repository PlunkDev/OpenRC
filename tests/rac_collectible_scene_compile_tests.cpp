#include "openrc/rac_collectible_scene_compile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::ActorLibraryLimitsV1 kActorLimits{
    4U,  4U,  64U, 512U,  16U,     64U, 16U, 4U,    32U,
    16U, 16U, 64U, 4096U, 12'288U, 64U, 64U, 4096U, 1U << 20U,
};
constexpr openrc::ActorPoseLimitsV1 kPoseLimits{16U, 4096U, 1.0e-8, 1.0e-8};
constexpr openrc::RenderSceneLimitsV1 kRenderLimits{
    16U, 4U,  32U,   16U,   16U,     64U,       64U,
    64U, 64U, 4096U, 4096U, 12'288U, 1U << 20U,
};
constexpr openrc::EntitySceneLimitsV1 kEntityLimits{
    64U, 64U, 64U, 16U, 8U, 64U, 64U, 4096U,
};
constexpr openrc::GameplaySceneLimitsV1 kGameplayLimits{
    64U,
    64U,
    4096U,
};
constexpr openrc::RacCollectibleSceneCompileLimitsV1 kLimits{
    128U,          kActorLimits,  kPoseLimits,
    kRenderLimits, kEntityLimits, kGameplayLimits,
};

constexpr float kPi = 3.14159265358979323846F;

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

void expect_near(const float actual, const double expected,
                 const std::string &message) {
  if (!std::isfinite(actual) ||
      std::abs(static_cast<double>(actual) - expected) > 2.0e-5) {
    fail(message + ": got " + std::to_string(actual) + ", expected " +
         std::to_string(expected));
  }
}

template <typename Callback>
void expect_compile_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::RacCollectibleSceneCompileError &) {
    return;
  }
  fail(message);
}

[[nodiscard]] std::vector<std::byte> rgba(const std::uint8_t seed) {
  return {static_cast<std::byte>(seed), static_cast<std::byte>(seed + 1U),
          static_cast<std::byte>(seed + 2U), std::byte{0xff}};
}

[[nodiscard]] openrc::ActorSkinBindingV1 root_skin() {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 1U;
  result.joint_indices = {0U, 0U, 0U};
  result.weight_numerators = {255U, 0U, 0U};
  result.weight_sum = 255U;
  return result;
}

[[nodiscard]] openrc::ActorSkinnedVertexV1 actor_vertex(const float x,
                                                        const float y) {
  openrc::ActorSkinnedVertexV1 result;
  result.x = x;
  result.y = y;
  result.nz = 1.0F;
  result.skin = root_skin();
  return result;
}

[[nodiscard]] openrc::RenderSceneTextureV1 texture(const std::uint32_t id,
                                                   const std::uint8_t seed) {
  openrc::RenderSceneTextureV1 result;
  result.id = id;
  result.color_space = openrc::RenderSceneTextureColorSpaceV1::srgb;
  result.mips.push_back({1U, 1U, rgba(seed)});
  return result;
}

[[nodiscard]] openrc::RenderSceneMaterialV1
material(const std::uint32_t id, const std::uint32_t texture_id) {
  openrc::RenderSceneMaterialV1 result;
  result.id = id;
  result.base_color_texture_id = texture_id;
  return result;
}

[[nodiscard]] openrc::ActorLibraryV1 make_library() {
  openrc::ActorRigAssetV1 rig;
  rig.id = 0U;
  rig.semantic_key = "actors/bolt/rig";
  rig.rig.joints.push_back({-1, {}, {}});

  openrc::ActorSkinnedMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      actor_vertex(0.0F, 0.0F),
      actor_vertex(1.0F, 0.0F),
      actor_vertex(0.0F, 1.0F),
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};

  openrc::ActorModelV1 model;
  model.id = 0U;
  model.semantic_key = "actors/bolt/high";
  model.rig_key = rig.semantic_key;
  model.textures.push_back(texture(0U, 0x10U));
  model.materials.push_back(material(0U, 0U));
  model.meshes.push_back(std::move(mesh));

  openrc::ActorLibraryV1 result;
  result.rigs.push_back(std::move(rig));
  result.models.push_back(std::move(model));
  return openrc::canonicalize_actor_library_v1(std::move(result), kActorLimits);
}

[[nodiscard]] openrc::RenderSceneV1 make_base_render_scene() {
  openrc::RenderSceneMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      {10.0F, 0.0F, 0.0F, 0.0F, 0.0F, UINT32_C(0xff102030)},
      {11.0F, 0.0F, 0.0F, 1.0F, 0.0F, UINT32_C(0xff102030)},
      {10.0F, 1.0F, 0.0F, 0.0F, 1.0F, UINT32_C(0xff102030)},
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};

  openrc::RenderSceneV1 result;
  result.textures.push_back(texture(0U, 0x80U));
  result.materials.push_back(material(0U, 0U));
  result.meshes.push_back(std::move(mesh));
  result.instances.push_back({0U, 0U, {}});
  return result;
}

[[nodiscard]] openrc::EntitySceneV1 make_base_entity_scene() {
  openrc::EntitySceneV1 result;
  result.level_id = 0U;
  result.definitions.push_back({0U, "openrc.player/default",
                                openrc::kEntityDefinitionInitiallyEnabledV1,
                                openrc::kEntitySceneNoAuthoringGroupIdV1});
  result.player_bindings.push_back({0U, 0U});
  return openrc::canonicalize_entity_scene_v1(std::move(result), kEntityLimits);
}

[[nodiscard]] openrc::GameplaySceneV1 make_base_gameplay_scene() {
  openrc::GameplaySceneV1 result;
  result.level_id = 0U;
  return openrc::canonicalize_gameplay_scene_v1(std::move(result),
                                                kGameplayLimits);
}

[[nodiscard]] openrc::RacCollectibleCompileProfileV1 make_profile() {
  return {
      13U,
      "actors/bolt/high",
      "openrc.collectible/bolt",
      "currency/bolt",
      {0.25F, -0.5F, 0.75F},
      1.5F,
      1U,
  };
}

[[nodiscard]] openrc::RacGameplayMobyInstanceV1
placement(const std::uint32_t class_id,
          const std::array<float, 3U> position = {},
          const std::array<float, 3U> rotation = {}, const float scale = 1.0F) {
  openrc::RacGameplayMobyInstanceV1 result;
  result.class_id = class_id;
  result.position = position;
  result.rotation = rotation;
  result.scale = scale;
  return result;
}

[[nodiscard]] openrc::RacCollectibleSceneCompileResultV1
compile(const openrc::RenderSceneV1 &render,
        const openrc::EntitySceneV1 &entities,
        const openrc::GameplaySceneV1 &gameplay,
        const openrc::ActorLibraryV1 &library,
        const std::vector<openrc::RacGameplayMobyInstanceV1> &sources,
        const openrc::RacCollectibleCompileProfileV1 &profile = make_profile(),
        const openrc::RacCollectibleSceneCompileLimitsV1 limits = kLimits) {
  return openrc::compile_rac_collectible_scene_v1(
      render, entities, gameplay, library, sources, profile, limits);
}

void test_filters_in_source_order_and_wires_all_three_scenes() {
  const auto render = make_base_render_scene();
  const auto entities = make_base_entity_scene();
  const auto gameplay = make_base_gameplay_scene();
  const auto library = make_library();
  const auto render_before = render;
  const auto entities_before = entities;
  const auto gameplay_before = gameplay;
  const auto library_before = library;

  auto ignored = placement(99U);
  ignored.scale = 0.0F;
  ignored.position[0U] = std::numeric_limits<float>::infinity();
  const std::vector sources{
      placement(99U),
      placement(13U, {1.0F, 2.0F, 3.0F}),
      ignored,
      placement(13U, {-4.0F, 5.0F, 6.0F}),
  };
  const auto result = compile(render, entities, gameplay, library, sources);

  expect(result.authored_ids == std::vector<std::uint32_t>({1U, 3U}) &&
             result.render_instance_ids == std::vector<std::uint32_t>({1U, 2U}),
         "filtered placements lost static-moby ordinal order");
  expect(result.render_scene.textures.size() == 2U &&
             result.render_scene.materials.size() == 2U &&
             result.render_scene.meshes.size() == 2U &&
             result.render_scene.instances.size() == 3U,
         "multiple collectibles did not share one baked model mesh");
  expect(result.render_scene.textures.front() == render.textures.front() &&
             result.render_scene.materials.front() ==
                 render.materials.front() &&
             result.render_scene.meshes.front() == render.meshes.front() &&
             result.render_scene.instances.front() == render.instances.front(),
         "render-scene base prefix was not preserved");

  expect(result.entity_scene.definitions.size() == 3U &&
             result.entity_scene.definitions[0U] == entities.definitions[0U] &&
             result.entity_scene.definitions[1U].authored_id == 1U &&
             result.entity_scene.definitions[2U].authored_id == 3U &&
             result.entity_scene.definitions[1U].archetype_key ==
                 "openrc.collectible/bolt" &&
             result.entity_scene.definitions[1U].flags ==
                 openrc::kEntityDefinitionInitiallyEnabledV1 &&
             result.entity_scene.definitions[1U].authoring_group_id ==
                 openrc::kEntitySceneNoAuthoringGroupIdV1,
         "collectible definitions have wrong neutral identity or policy");
  expect(result.entity_scene.transforms.size() == 2U &&
             result.entity_scene.transforms[0U].authored_id == 1U &&
             result.entity_scene.transforms[1U].authored_id == 3U &&
             result.entity_scene.render_bindings ==
                 std::vector<openrc::EntityRenderBindingV1>(
                     {{1U, 1U}, {3U, 2U}}) &&
             result.entity_scene.player_bindings == entities.player_bindings,
         "entity components were not wired to baked render IDs");
  expect(result.gameplay_scene.collectibles.size() == 2U &&
             result.gameplay_scene.collectibles[0U].authored_id == 1U &&
             result.gameplay_scene.collectibles[1U].authored_id == 3U &&
             result.gameplay_scene.collectibles[0U].item_key ==
                 "currency/bolt" &&
             result.gameplay_scene.collectibles[0U].local_center ==
                 make_profile().local_center &&
             result.gameplay_scene.collectibles[0U].collection_radius == 1.5F &&
             result.gameplay_scene.collectibles[0U].amount == 1U,
         "gameplay collectibles did not retain neutral profile policy");
  expect(render == render_before && entities == entities_before &&
             gameplay == gameplay_before && library == library_before,
         "successful collectible compilation mutated an input");

  openrc::validate_render_scene_v1(result.render_scene, kRenderLimits);
  openrc::validate_entity_scene_v1(result.entity_scene, kEntityLimits);
  openrc::validate_gameplay_scene_v1(result.gameplay_scene, kGameplayLimits);
  expect(result == compile(render, entities, gameplay, library, sources),
         "collectible compilation is not deterministic");
}

using Matrix3 = std::array<double, 9U>;
using Quaternion = std::array<double, 4U>;

[[nodiscard]] Matrix3 multiply(const Matrix3 &left, const Matrix3 &right) {
  Matrix3 result{};
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      for (std::size_t inner = 0U; inner < 3U; ++inner) {
        result[row * 3U + column] +=
            left[row * 3U + inner] * right[inner * 3U + column];
      }
    }
  }
  return result;
}

[[nodiscard]] Quaternion multiply(const Quaternion &left,
                                  const Quaternion &right) {
  return {
      left[3U] * right[0U] + left[0U] * right[3U] + left[1U] * right[2U] -
          left[2U] * right[1U],
      left[3U] * right[1U] - left[0U] * right[2U] + left[1U] * right[3U] +
          left[2U] * right[0U],
      left[3U] * right[2U] + left[0U] * right[1U] - left[1U] * right[0U] +
          left[2U] * right[3U],
      left[3U] * right[3U] - left[0U] * right[0U] - left[1U] * right[1U] -
          left[2U] * right[2U],
  };
}

void test_exact_t_s_rz_ry_rx_and_xyzw_quaternion() {
  const auto render = make_base_render_scene();
  const auto entities = make_base_entity_scene();
  const auto gameplay = make_base_gameplay_scene();
  const auto library = make_library();
  const std::array<float, 3U> rotation{
      0.25F * kPi,
      -0.2F * kPi,
      0.35F * kPi,
  };
  const std::vector sources{
      placement(99U),
      placement(13U, {7.0F, -8.0F, 9.0F}, rotation, 2.5F),
  };
  const auto result = compile(render, entities, gameplay, library, sources);

  const auto x = static_cast<double>(rotation[0U]);
  const auto y = static_cast<double>(rotation[1U]);
  const auto z = static_cast<double>(rotation[2U]);
  const Matrix3 rx{1.0,          0.0, 0.0,         0.0,        std::cos(x),
                   -std::sin(x), 0.0, std::sin(x), std::cos(x)};
  const Matrix3 ry{std::cos(y), 0.0,          std::sin(y), 0.0,        1.0,
                   0.0,         -std::sin(y), 0.0,         std::cos(y)};
  const Matrix3 rz{std::cos(z), -std::sin(z), 0.0, std::sin(z), std::cos(z),
                   0.0,         0.0,          0.0, 1.0};
  const auto expected_rotation = multiply(multiply(rz, ry), rx);
  const auto &matrix = result.render_scene.instances[1U].local_to_world.values;
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      expect_near(matrix[row * 4U + column],
                  2.5 * expected_rotation[row * 3U + column],
                  "render matrix is not T*S*Rz*Ry*Rx");
    }
  }
  expect_near(matrix[3U], 7.0, "render matrix lost translation X");
  expect_near(matrix[7U], -8.0, "render matrix lost translation Y");
  expect_near(matrix[11U], 9.0, "render matrix lost translation Z");

  const Quaternion qx{std::sin(x * 0.5), 0.0, 0.0, std::cos(x * 0.5)};
  const Quaternion qy{0.0, std::sin(y * 0.5), 0.0, std::cos(y * 0.5)};
  const Quaternion qz{0.0, 0.0, std::sin(z * 0.5), std::cos(z * 0.5)};
  auto expected_quaternion = multiply(multiply(qz, qy), qx);
  if (expected_quaternion[3U] < 0.0) {
    for (auto &value : expected_quaternion) {
      value = -value;
    }
  }
  const auto &world = result.entity_scene.transforms.front().transform;
  for (std::size_t index = 0U; index < 4U; ++index) {
    expect_near(world.rotation[index], expected_quaternion[index],
                "entity quaternion is not XYZW for Rz*Ry*Rx");
  }
  expect(world.position == std::array<float, 3U>({7.0F, -8.0F, 9.0F}) &&
             world.scale == std::array<float, 3U>({2.5F, 2.5F, 2.5F}),
         "entity transform lost RAC position or uniform scale");
}

void test_interleaved_policies_compose_by_source_ordinal() {
  const auto render = make_base_render_scene();
  const auto entities = make_base_entity_scene();
  const auto gameplay = make_base_gameplay_scene();
  const auto library = make_library();
  const std::vector sources{
      placement(99U),
      placement(13U, {1.0F, 0.0F, 0.0F}),
      placement(14U, {2.0F, 0.0F, 0.0F}),
      placement(13U, {3.0F, 0.0F, 0.0F}),
      placement(14U, {4.0F, 0.0F, 0.0F}),
  };

  const auto first = compile(render, entities, gameplay, library, sources);
  auto second_profile = make_profile();
  second_profile.source_class_id = 14U;
  second_profile.archetype_key = "openrc.collectible/second";
  second_profile.item_key = "inventory/second";
  second_profile.amount = 5U;
  const auto second =
      compile(first.render_scene, first.entity_scene, first.gameplay_scene,
              library, sources, second_profile);

  expect(first.authored_ids == std::vector<std::uint32_t>({1U, 3U}) &&
             second.authored_ids == std::vector<std::uint32_t>({2U, 4U}) &&
             second.render_instance_ids == std::vector<std::uint32_t>({3U, 4U}),
         "interleaved source ordinals could not be compiled sequentially");
  expect(second.entity_scene.definitions.size() == 5U,
         "sequential policies produced the wrong entity count");
  for (std::size_t index = 0U; index < 5U; ++index) {
    expect(second.entity_scene.definitions[index].authored_id == index,
           "sequential policies lost canonical authored-ID order");
  }
  expect(second.entity_scene.definitions[1U] ==
                 first.entity_scene.definitions[1U] &&
             second.entity_scene.definitions[3U] ==
                 first.entity_scene.definitions[2U] &&
             second.gameplay_scene.collectibles[0U] ==
                 first.gameplay_scene.collectibles[0U] &&
             second.gameplay_scene.collectibles[2U] ==
                 first.gameplay_scene.collectibles[1U],
         "a later interleaved policy changed earlier records");
  expect(std::equal(first.render_scene.textures.begin(),
                    first.render_scene.textures.end(),
                    second.render_scene.textures.begin()) &&
             std::equal(first.render_scene.materials.begin(),
                        first.render_scene.materials.end(),
                        second.render_scene.materials.begin()) &&
             std::equal(first.render_scene.meshes.begin(),
                        first.render_scene.meshes.end(),
                        second.render_scene.meshes.begin()) &&
             std::equal(first.render_scene.instances.begin(),
                        first.render_scene.instances.end(),
                        second.render_scene.instances.begin()),
         "a later policy changed a dense RenderSceneV1 prefix");
}

void test_collision_and_invalid_matching_placement_are_transactional() {
  const auto render = make_base_render_scene();
  const auto entities = make_base_entity_scene();
  const auto gameplay = make_base_gameplay_scene();
  const auto library = make_library();
  const auto render_before = render;
  const auto entities_before = entities;
  const auto gameplay_before = gameplay;
  const auto library_before = library;

  const std::vector colliding{placement(13U)};
  expect_compile_error(
      [&] {
        static_cast<void>(
            compile(render, entities, gameplay, library, colliding));
      },
      "static-moby ordinal zero collided with the player but was accepted");

  const std::vector invalid{
      placement(99U),
      placement(13U, {}, {}, -1.0F),
  };
  expect_compile_error(
      [&] {
        static_cast<void>(
            compile(render, entities, gameplay, library, invalid));
      },
      "a matching placement with negative scale was accepted");
  expect(render == render_before && entities == entities_before &&
             gameplay == gameplay_before && library == library_before,
         "failed collectible compilation mutated an input resource");
}

void test_base_collectible_requires_an_entity_transform() {
  const auto render = make_base_render_scene();
  const auto entities = make_base_entity_scene();
  auto gameplay = make_base_gameplay_scene();
  gameplay.collectibles.push_back({0U, "currency/bolt", {}, 1U, 1.0F, 0U});
  gameplay = openrc::canonicalize_gameplay_scene_v1(std::move(gameplay),
                                                    kGameplayLimits);
  const auto library = make_library();

  expect_compile_error(
      [&] {
        static_cast<void>(compile(render, entities, gameplay, library, {}));
      },
      "a base collectible attached to the transform-less player was accepted");
}

void test_all_explicit_limits_are_enforced() {
  const auto render = make_base_render_scene();
  const auto entities = make_base_entity_scene();
  const auto gameplay = make_base_gameplay_scene();
  const auto library = make_library();
  const std::vector one_match{placement(99U), placement(13U)};

  auto limits = kLimits;
  limits.max_source_instances = 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(compile(render, entities, gameplay, library,
                                  one_match, make_profile(), limits));
      },
      "source placement limit was ignored");

  limits = kLimits;
  limits.entity_scene.max_definitions = 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(compile(render, entities, gameplay, library,
                                  one_match, make_profile(), limits));
      },
      "entity definition limit was ignored");

  const std::vector two_matches{placement(99U), placement(13U), placement(13U)};
  limits = kLimits;
  limits.gameplay_scene.max_collectibles = 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(compile(render, entities, gameplay, library,
                                  two_matches, make_profile(), limits));
      },
      "gameplay collectible limit was ignored");

  limits = kLimits;
  limits.render_scene.max_instances = 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(compile(render, entities, gameplay, library,
                                  one_match, make_profile(), limits));
      },
      "render instance limit was ignored");

  limits = kLimits;
  limits.actor_pose.max_vertices = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(compile(render, entities, gameplay, library,
                                  one_match, make_profile(), limits));
      },
      "actor pose limit was ignored");

  limits = kLimits;
  limits.actor_library.max_vertices = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(compile(render, entities, gameplay, library,
                                  one_match, make_profile(), limits));
      },
      "actor library limit was ignored");
}

void test_empty_filter_is_an_exact_validated_noop() {
  const auto render = make_base_render_scene();
  const auto entities = make_base_entity_scene();
  const auto gameplay = make_base_gameplay_scene();
  const auto library = make_library();
  auto ignored = placement(77U);
  ignored.scale = 0.0F;
  ignored.rotation[2U] = std::numeric_limits<float>::quiet_NaN();
  const std::vector sources{ignored};

  const auto result = compile(render, entities, gameplay, library, sources);
  expect(result.render_scene == render && result.entity_scene == entities &&
             result.gameplay_scene == gameplay && result.authored_ids.empty() &&
             result.render_instance_ids.empty(),
         "an empty collectible filter was not an exact no-op");

  auto wrong_profile = make_profile();
  wrong_profile.model_semantic_key = "actors/missing/high";
  expect_compile_error(
      [&] {
        static_cast<void>(
            compile(render, entities, gameplay, library, {}, wrong_profile));
      },
      "empty compilation skipped model/profile validation");
}

} // namespace

int main() {
  try {
    test_filters_in_source_order_and_wires_all_three_scenes();
    test_exact_t_s_rz_ry_rx_and_xyzw_quaternion();
    test_interleaved_policies_compose_by_source_ordinal();
    test_collision_and_invalid_matching_placement_are_transactional();
    test_base_collectible_requires_an_entity_transform();
    test_all_explicit_limits_are_enforced();
    test_empty_filter_is_an_exact_validated_noop();
    std::cout << "RAC collectible scene compile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC collectible scene compile tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
