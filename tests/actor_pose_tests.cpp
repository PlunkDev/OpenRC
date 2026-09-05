#include "openrc/actor_pose.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::ActorPoseLimitsV1 kLimits{
    64U, 4096U, 1.0e-8, 1.0e-8};

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

template <typename Function>
void expect_pose_error(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const openrc::ActorPoseError &) {
    return;
  }
  fail(message);
}

[[nodiscard]] openrc::ActorAffineTransformV1
translation(const float x, const float y, const float z) {
  openrc::ActorAffineTransformV1 result;
  result.values[3U] = x;
  result.values[7U] = y;
  result.values[11U] = z;
  return result;
}

[[nodiscard]] openrc::ActorAffineTransformV1
rotation_z_90_with_translation(const float x, const float y, const float z) {
  auto result = translation(x, y, z);
  result.values[0U] = 0.0F;
  result.values[1U] = -1.0F;
  result.values[4U] = 1.0F;
  result.values[5U] = 0.0F;
  return result;
}

[[nodiscard]] openrc::ActorRigV1 rig() {
  openrc::ActorRigV1 result;
  result.joints = {
      {-1, translation(2.0F, 0.0F, 0.0F),
       translation(-2.0F, 0.0F, 0.0F)},
      {0, translation(0.0F, 3.0F, 0.0F),
       translation(-2.0F, -3.0F, 0.0F)},
  };
  return result;
}

[[nodiscard]] openrc::ActorSkinBindingV1 binding(
    const std::uint16_t root_weight = 64U,
    const std::uint16_t child_weight = 192U) {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 2U;
  result.joint_indices = {0U, 1U, 0U};
  result.weight_numerators = {root_weight, child_weight, 0U};
  result.weight_sum =
      static_cast<std::uint16_t>(root_weight + child_weight);
  return result;
}

[[nodiscard]] openrc::ActorSkinnedMeshV1 mesh() {
  openrc::ActorSkinnedMeshV1 result;
  result.vertices.resize(1U);
  auto &vertex = result.vertices[0U];
  vertex.x = 1.0F;
  vertex.y = 2.0F;
  vertex.z = 3.0F;
  vertex.nx = 1.0F;
  vertex.ny = 0.0F;
  vertex.nz = 0.0F;
  vertex.u = 0.25F;
  vertex.v = 0.75F;
  vertex.rgba8 = UINT32_C(0xff443322);
  vertex.skin = binding();
  result.triangle_indices = {0U, 0U, 0U};
  result.draw_ranges = {{0U, 0U, 3U}};
  return result;
}

void test_default_affine_is_identity() {
  const openrc::ActorAffineTransformV1 identity;
  const std::vector<float> expected{
      1.0F, 0.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 0.0F,
  };
  expect(std::equal(identity.values.begin(), identity.values.end(),
                    expected.begin(), expected.end()),
         "ActorAffineTransformV1 does not default to identity");
}

void test_bind_palette_is_identity() {
  const auto palette =
      openrc::build_actor_bind_pose_palette_v1(rig(), kLimits);
  expect(palette.global_joint_transforms.size() == 2U &&
             palette.skin_transforms.size() == 2U,
         "bind palette has the wrong joint count");
  expect_near(palette.global_joint_transforms[1U].values[3U], 2.0F,
              "child global bind X is wrong");
  expect_near(palette.global_joint_transforms[1U].values[7U], 3.0F,
              "child global bind Y is wrong");
  for (const auto &skin : palette.skin_transforms) {
    const openrc::ActorAffineTransformV1 identity;
    for (std::size_t index = 0U; index < identity.values.size(); ++index) {
      expect_near(skin.values[index], identity.values[index],
                  "bind-pose skin matrix is not identity");
    }
  }

  const auto posed = openrc::pose_actor_mesh_vertices_v1(
      mesh(), palette, openrc::ActorAffineTransformV1{}, kLimits);
  expect(posed.size() == 1U, "bind pose lost an actor vertex");
  expect_near(posed[0U].x, 1.0F, "bind pose applied the rig twice");
  expect_near(posed[0U].y, 2.0F, "bind pose changed model-space Y");
  expect_near(posed[0U].z, 3.0F, "bind pose changed model-space Z");
}

void test_exact_blend_and_world_transform() {
  const std::vector<openrc::ActorAffineTransformV1> local_pose{
      translation(4.0F, 0.0F, 0.0F),
      translation(0.0F, 4.0F, 0.0F),
  };
  const auto palette =
      openrc::build_actor_pose_palette_v1(rig(), local_pose, kLimits);

  auto origin_mesh = mesh();
  origin_mesh.vertices[0U].x = 0.0F;
  origin_mesh.vertices[0U].y = 0.0F;
  origin_mesh.vertices[0U].z = 0.0F;
  const auto blended = openrc::pose_actor_mesh_vertices_v1(
      origin_mesh, palette, openrc::ActorAffineTransformV1{}, kLimits);
  expect_near(blended[0U].x, 2.0F,
              "weighted skin translation X is wrong");
  expect_near(blended[0U].y, 0.75F,
              "exact 64/192 weighted skin translation Y is wrong");

  const auto world = rotation_z_90_with_translation(10.0F, 0.0F, 1.0F);
  auto equal_motion = local_pose;
  equal_motion[1U] = translation(0.0F, 3.0F, 0.0F);
  const auto equal_palette =
      openrc::build_actor_pose_palette_v1(rig(), equal_motion, kLimits);
  const auto posed = openrc::pose_actor_mesh_vertices_v1(
      mesh(), equal_palette, world, kLimits);
  expect_near(posed[0U].x, 8.0F, "world-space actor X is wrong");
  expect_near(posed[0U].y, 3.0F, "world-space actor Y is wrong");
  expect_near(posed[0U].z, 4.0F, "world-space actor Z is wrong");
  expect_near(posed[0U].nx, 0.0F, "world normal X is wrong");
  expect_near(posed[0U].ny, 1.0F, "world normal Y is wrong");
  expect_near(posed[0U].nz, 0.0F, "world normal Z is wrong");
  expect(posed[0U].u == 0.25F && posed[0U].v == 0.75F &&
             posed[0U].rgba8 == UINT32_C(0xff443322),
         "posing changed immutable vertex attributes");
}

void test_inverse_transpose_normal_and_canonical_zero() {
  const auto palette =
      openrc::build_actor_bind_pose_palette_v1(rig(), kLimits);
  auto source = mesh();
  source.vertices[0U].nx = 1.0F;
  source.vertices[0U].ny = 1.0F;
  source.vertices[0U].nz = -0.0F;
  openrc::ActorAffineTransformV1 scale;
  scale.values[0U] = 2.0F;
  const auto posed = openrc::pose_actor_mesh_vertices_v1(
      source, palette, scale, kLimits);
  expect_near(posed[0U].nx, 0.44721359F,
              "inverse-transpose normal X is wrong");
  expect_near(posed[0U].ny, 0.89442718F,
              "inverse-transpose normal Y is wrong");
  expect(posed[0U].nz == 0.0F && !std::signbit(posed[0U].nz),
         "posed normal retained negative zero");
}

void test_fail_closed_validation() {
  auto invalid_limits = kLimits;
  invalid_limits.minimum_normal_length = 0.0;
  expect_pose_error(
      [&] {
        (void)openrc::build_actor_bind_pose_palette_v1(rig(),
                                                       invalid_limits);
      },
      "pose builder accepted implicit limits");

  expect_pose_error(
      [&] {
        const std::vector<openrc::ActorAffineTransformV1> one_pose(1U);
        (void)openrc::build_actor_pose_palette_v1(rig(), one_pose, kLimits);
      },
      "pose builder accepted a mismatched joint count");

  auto bad_rig = rig();
  bad_rig.joints[1U].parent_index = 1;
  expect_pose_error(
      [&] {
        (void)openrc::build_actor_bind_pose_palette_v1(bad_rig, kLimits);
      },
      "pose builder accepted a forward parent reference");

  auto singular_pose = std::vector<openrc::ActorAffineTransformV1>{
      openrc::ActorAffineTransformV1{}, openrc::ActorAffineTransformV1{}};
  singular_pose[0U].values[0U] = 0.0F;
  expect_pose_error(
      [&] {
        (void)openrc::build_actor_pose_palette_v1(rig(), singular_pose,
                                                  kLimits);
      },
      "pose builder accepted a singular local transform");

  const auto palette =
      openrc::build_actor_bind_pose_palette_v1(rig(), kLimits);
  auto bad_mesh = mesh();
  bad_mesh.vertices[0U].skin.weight_sum = 255U;
  expect_pose_error(
      [&] {
        (void)openrc::pose_actor_mesh_vertices_v1(
            bad_mesh, palette, openrc::ActorAffineTransformV1{}, kLimits);
      },
      "skinner accepted an inexact weight sum");

  bad_mesh = mesh();
  bad_mesh.vertices[0U].nx = 0.0F;
  bad_mesh.vertices[0U].ny = 0.0F;
  bad_mesh.vertices[0U].nz = 0.0F;
  expect_pose_error(
      [&] {
        (void)openrc::pose_actor_mesh_vertices_v1(
            bad_mesh, palette, openrc::ActorAffineTransformV1{}, kLimits);
      },
      "skinner accepted a zero source normal");

  auto too_small = kLimits;
  too_small.max_vertices = 0U;
  expect_pose_error(
      [&] {
        (void)openrc::pose_actor_mesh_vertices_v1(
            mesh(), palette, openrc::ActorAffineTransformV1{}, too_small);
      },
      "skinner accepted a zero vertex limit");

  auto bad_palette = palette;
  bad_palette.global_joint_transforms.pop_back();
  expect_pose_error(
      [&] {
        (void)openrc::pose_actor_mesh_vertices_v1(
            mesh(), bad_palette, openrc::ActorAffineTransformV1{}, kLimits);
      },
      "skinner accepted inconsistent palette tables");

  auto non_finite_world = openrc::ActorAffineTransformV1{};
  non_finite_world.values[3U] = std::numeric_limits<float>::infinity();
  expect_pose_error(
      [&] {
        (void)openrc::pose_actor_mesh_vertices_v1(
            mesh(), palette, non_finite_world, kLimits);
      },
      "skinner accepted a non-finite world transform");
}

} // namespace

int main() {
  try {
    test_default_affine_is_identity();
    test_bind_palette_is_identity();
    test_exact_blend_and_world_transform();
    test_inverse_transpose_normal_and_canonical_zero();
    test_fail_closed_validation();
    std::cout << "Actor pose tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Actor pose tests failed: " << error.what() << '\n';
    return 1;
  }
}
