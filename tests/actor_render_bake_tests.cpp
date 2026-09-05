#include "openrc/actor_render_bake.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <span>
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

constexpr openrc::RenderSceneLimitsV1 kSceneLimits{
    16U, 4U,  32U,   16U,   16U,     64U,       64U,
    64U, 64U, 4096U, 4096U, 12'288U, 1U << 20U,
};

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

void expect_near(const float actual, const float expected,
                 const std::string &message) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > 1.0e-5F) {
    fail(message + ": got " + std::to_string(actual) + ", expected " +
         std::to_string(expected));
  }
}

template <typename Callback>
void expect_bake_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::ActorRenderBakeError &) {
    return;
  }
  fail(message);
}

[[nodiscard]] std::vector<std::byte> rgba(const std::uint8_t seed) {
  return {static_cast<std::byte>(seed), static_cast<std::byte>(seed + 1U),
          static_cast<std::byte>(seed + 2U), std::byte{0xff}};
}

[[nodiscard]] openrc::ActorAffineTransformV1
actor_translation(const float x, const float y, const float z) {
  openrc::ActorAffineTransformV1 result;
  result.values[3U] = x;
  result.values[7U] = y;
  result.values[11U] = z;
  return result;
}

[[nodiscard]] openrc::RenderSceneAffine3x4V1
instance_translation(const float x, const float y, const float z) {
  openrc::RenderSceneAffine3x4V1 result;
  result.values[3U] = x;
  result.values[7U] = y;
  result.values[11U] = z;
  return result;
}

[[nodiscard]] openrc::ActorSkinBindingV1 root_skin() {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 1U;
  result.joint_indices = {0U, 0U, 0U};
  result.weight_numerators = {255U, 0U, 0U};
  result.weight_sum = 255U;
  return result;
}

[[nodiscard]] openrc::ActorSkinnedVertexV1
actor_vertex(const float x, const float y, const float u, const float v,
             const std::uint32_t color) {
  openrc::ActorSkinnedVertexV1 result;
  result.x = x;
  result.y = y;
  result.z = 0.0F;
  result.nx = 0.0F;
  result.ny = 0.0F;
  result.nz = 1.0F;
  result.u = u;
  result.v = v;
  result.rgba8 = color;
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
  result.alpha_mode = openrc::RenderSceneAlphaModeV1::mask;
  result.alpha_cutoff_rgba8 = 1U;
  return result;
}

[[nodiscard]] openrc::ActorSkinnedMeshV1
actor_mesh(const std::uint32_t id, const std::uint32_t material_id,
           const float x_offset, const bool reverse_winding) {
  openrc::ActorSkinnedMeshV1 result;
  result.id = id;
  result.vertices = {
      actor_vertex(x_offset, 0.0F, 0.0F, 0.0F, UINT32_C(0xff112233) + id),
      actor_vertex(x_offset + 1.0F, 0.0F, 1.0F, 0.0F,
                   UINT32_C(0xff445566) + id),
      actor_vertex(x_offset, 1.0F, 0.0F, 1.0F, UINT32_C(0xff778899) + id),
  };
  result.triangle_indices = reverse_winding
                                ? std::vector<std::uint32_t>{0U, 2U, 1U}
                                : std::vector<std::uint32_t>{0U, 1U, 2U};
  result.draw_ranges = {{material_id, 0U, 3U}};
  return result;
}

[[nodiscard]] openrc::ActorLibraryV1
make_library(const bool multiple_meshes = false) {
  openrc::ActorRigAssetV1 rig;
  rig.id = 0U;
  rig.semantic_key = "actors/bolt/rig";
  rig.rig.joints.push_back({-1, actor_translation(2.0F, 0.0F, 0.0F),
                            actor_translation(-2.0F, 0.0F, 0.0F)});

  openrc::ActorModelV1 model;
  model.id = 0U;
  model.semantic_key = "actors/bolt/high";
  model.rig_key = rig.semantic_key;
  model.textures.push_back(texture(0U, 0x10U));
  model.materials.push_back(material(0U, 0U));
  model.meshes.push_back(actor_mesh(0U, 0U, 0.0F, false));
  if (multiple_meshes) {
    model.textures.push_back(texture(1U, 0x20U));
    model.materials.push_back(material(1U, 1U));
    model.meshes.push_back(actor_mesh(1U, 1U, 10.0F, true));
  }

  openrc::ActorLibraryV1 result;
  result.rigs.push_back(std::move(rig));
  result.models.push_back(std::move(model));
  return openrc::canonicalize_actor_library_v1(std::move(result), kActorLimits);
}

[[nodiscard]] openrc::RenderSceneV1 make_base_scene() {
  openrc::RenderSceneMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      {20.0F, 0.0F, 0.0F, 0.0F, 0.0F, UINT32_C(0xffabcdef)},
      {21.0F, 0.0F, 0.0F, 1.0F, 0.0F, UINT32_C(0xffabcdef)},
      {20.0F, 1.0F, 0.0F, 0.0F, 1.0F, UINT32_C(0xffabcdef)},
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};

  openrc::RenderSceneV1 result;
  result.textures.push_back(texture(0U, 0x80U));
  result.materials.push_back(material(0U, 0U));
  result.meshes.push_back(std::move(mesh));
  result.instances.push_back({0U, 0U, instance_translation(0.0F, 0.0F, 0.0F)});
  return result;
}

void test_one_and_multiple_instances_stay_as_instances() {
  const openrc::RenderSceneV1 empty_base;
  const auto library = make_library();
  const auto base_before = empty_base;
  const auto library_before = library;
  const std::array one_transform{instance_translation(7.0F, -0.0F, 9.0F)};
  const auto one = openrc::bake_actor_bind_pose_to_render_scene_v1(
      empty_base, library, "actors/bolt/high", one_transform, kActorLimits,
      kPoseLimits, kSceneLimits);

  expect(one.scene.textures.size() == 1U && one.scene.materials.size() == 1U &&
             one.scene.meshes.size() == 1U &&
             one.scene.instances.size() == 1U &&
             one.instance_ids == std::vector<std::uint32_t>{0U},
         "one actor instance produced the wrong scene tables");
  expect(one.scene.meshes[0U].vertices.size() == 3U &&
             one.scene.meshes[0U].triangle_indices ==
                 std::vector<std::uint32_t>({0U, 1U, 2U}),
         "bind-pose baking changed single-mesh topology");
  expect_near(one.scene.meshes[0U].vertices[1U].x, 1.0F,
              "bind-pose baking applied the rig twice");
  expect_near(one.scene.instances[0U].local_to_world.values[3U], 7.0F,
              "instance translation was not preserved");
  expect(one.scene.instances[0U].local_to_world.values[7U] == 0.0F &&
             !std::signbit(one.scene.instances[0U].local_to_world.values[7U]),
         "new instance signed zero was not canonicalized");
  expect(empty_base == base_before && library == library_before,
         "successful baking mutated an input resource");

  const std::array transforms{
      instance_translation(3.0F, 4.0F, 5.0F),
      instance_translation(-2.0F, 8.0F, 1.0F),
  };
  const auto multiple = openrc::bake_actor_bind_pose_to_render_scene_v1(
      empty_base, library, "actors/bolt/high", transforms, kActorLimits,
      kPoseLimits, kSceneLimits);
  expect(multiple.scene.meshes.size() == 1U &&
             multiple.scene.instances.size() == 2U &&
             multiple.instance_ids == std::vector<std::uint32_t>({0U, 1U}) &&
             multiple.scene.instances[0U].mesh_id == 0U &&
             multiple.scene.instances[1U].mesh_id == 0U &&
             multiple.scene.instances[0U].local_to_world == transforms[0U] &&
             multiple.scene.instances[1U].local_to_world == transforms[1U],
         "multiple actor transforms were not preserved as parallel instances");
}

void test_remaps_nonempty_base_and_merges_all_meshes() {
  const auto base = make_base_scene();
  const auto library = make_library(true);
  const auto base_before = base;
  const auto library_before = library;
  const std::array transforms{instance_translation(100.0F, 200.0F, 300.0F)};
  const auto baked = openrc::bake_actor_bind_pose_to_render_scene_v1(
      base, library, "actors/bolt/high", transforms, kActorLimits, kPoseLimits,
      kSceneLimits);

  expect(baked.scene.textures.size() == 3U &&
             baked.scene.materials.size() == 3U &&
             baked.scene.meshes.size() == 2U &&
             baked.scene.instances.size() == 2U &&
             baked.instance_ids == std::vector<std::uint32_t>{1U},
         "nonempty-base append produced the wrong dense table sizes");
  expect(baked.scene.textures[0U] == base.textures[0U] &&
             baked.scene.materials[0U] == base.materials[0U] &&
             baked.scene.meshes[0U] == base.meshes[0U] &&
             baked.scene.instances[0U] == base.instances[0U],
         "actor baking changed a pre-existing scene prefix");
  expect(baked.scene.materials[1U].base_color_texture_id == 1U &&
             baked.scene.materials[2U].base_color_texture_id == 2U,
         "actor texture IDs were not remapped by the append offset");

  const auto &mesh = baked.scene.meshes[1U];
  expect(mesh.vertices.size() == 6U &&
             mesh.triangle_indices ==
                 std::vector<std::uint32_t>({0U, 1U, 2U, 3U, 5U, 4U}) &&
             mesh.draw_ranges == std::vector<openrc::RenderSceneDrawRangeV1>(
                                     {{1U, 0U, 3U}, {2U, 3U, 3U}}),
         "multi-mesh merge changed winding or draw partitions");
  expect_near(mesh.vertices[3U].x, 10.0F,
              "multi-mesh merge changed the second mesh vertex domain");
  expect_near(mesh.vertices[0U].x, 0.0F,
              "an instance transform was pre-applied to actor vertices");
  expect(baked.scene.instances[1U].id == 1U &&
             baked.scene.instances[1U].mesh_id == 1U &&
             baked.scene.instances[1U].local_to_world == transforms[0U],
         "the appended actor instance references the wrong merged mesh");
  expect(base == base_before && library == library_before,
         "nonempty-base baking mutated an input resource");
}

void test_missing_key_empty_noop_and_transactional_failures() {
  const auto base = make_base_scene();
  const auto library = make_library();
  const auto base_before = base;
  const auto library_before = library;
  const std::array transforms{instance_translation(1.0F, 2.0F, 3.0F)};

  expect_bake_error(
      [&] {
        static_cast<void>(openrc::bake_actor_bind_pose_to_render_scene_v1(
            base, library, "actors/missing/high", transforms, kActorLimits,
            kPoseLimits, kSceneLimits));
      },
      "actor baking accepted a missing model semantic key");
  expect(base == base_before && library == library_before,
         "failed model resolution mutated an input resource");

  const auto noop = openrc::bake_actor_bind_pose_to_render_scene_v1(
      base, library, "actors/bolt/high", {}, kActorLimits, kPoseLimits,
      kSceneLimits);
  expect(noop.scene == base && noop.instance_ids.empty(),
         "an empty transform span was not an exact validated no-op");
}

void test_every_operation_obeys_explicit_limits() {
  const openrc::RenderSceneV1 base;
  const auto library = make_library();
  const std::array one_transform{instance_translation(1.0F, 2.0F, 3.0F)};
  const std::array two_transforms{
      instance_translation(1.0F, 2.0F, 3.0F),
      instance_translation(4.0F, 5.0F, 6.0F),
  };

  auto scene_limits = kSceneLimits;
  scene_limits.max_instances = 1U;
  expect_bake_error(
      [&] {
        static_cast<void>(openrc::bake_actor_bind_pose_to_render_scene_v1(
            base, library, "actors/bolt/high", two_transforms, kActorLimits,
            kPoseLimits, scene_limits));
      },
      "actor baking ignored the RenderSceneV1 instance limit");

  scene_limits = kSceneLimits;
  scene_limits.max_vertices = 2U;
  expect_bake_error(
      [&] {
        static_cast<void>(openrc::bake_actor_bind_pose_to_render_scene_v1(
            base, library, "actors/bolt/high", one_transform, kActorLimits,
            kPoseLimits, scene_limits));
      },
      "actor baking ignored the RenderSceneV1 aggregate vertex limit");

  auto pose_limits = kPoseLimits;
  pose_limits.max_vertices = 2U;
  expect_bake_error(
      [&] {
        static_cast<void>(openrc::bake_actor_bind_pose_to_render_scene_v1(
            base, library, "actors/bolt/high", one_transform, kActorLimits,
            pose_limits, kSceneLimits));
      },
      "actor baking ignored the ActorPoseV1 per-mesh vertex limit");

  auto actor_limits = kActorLimits;
  actor_limits.max_vertices = 2U;
  expect_bake_error(
      [&] {
        static_cast<void>(openrc::bake_actor_bind_pose_to_render_scene_v1(
            base, library, "actors/bolt/high", one_transform, actor_limits,
            kPoseLimits, kSceneLimits));
      },
      "actor baking ignored the ActorLibraryV1 validation limits");
}

void test_result_is_deterministic_and_canonical() {
  const auto base = make_base_scene();
  const auto library = make_library(true);
  const std::array transforms{
      instance_translation(-0.0F, 2.0F, 3.0F),
      instance_translation(4.0F, 5.0F, 6.0F),
  };
  const auto first = openrc::bake_actor_bind_pose_to_render_scene_v1(
      base, library, "actors/bolt/high", transforms, kActorLimits, kPoseLimits,
      kSceneLimits);
  const auto second = openrc::bake_actor_bind_pose_to_render_scene_v1(
      base, library, "actors/bolt/high", transforms, kActorLimits, kPoseLimits,
      kSceneLimits);

  expect(first == second,
         "identical actor baking inputs produced different canonical results");
  openrc::validate_render_scene_v1(first.scene, kSceneLimits);
  expect(first.scene.instances[1U].local_to_world.values[3U] == 0.0F &&
             !std::signbit(first.scene.instances[1U].local_to_world.values[3U]),
         "deterministic result retained a non-canonical signed zero");
}

} // namespace

int main() {
  try {
    test_one_and_multiple_instances_stay_as_instances();
    test_remaps_nonempty_base_and_merges_all_meshes();
    test_missing_key_empty_noop_and_transactional_failures();
    test_every_operation_obeys_explicit_limits();
    test_result_is_deterministic_and_canonical();
    std::cout << "Actor render bake tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Actor render bake tests failed: " << error.what() << '\n';
    return 1;
  }
}
