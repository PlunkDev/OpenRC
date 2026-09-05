#include "openrc/actor_pose.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw ActorPoseError(message);
}

void validate_limits(const ActorPoseLimitsV1 &limits) {
  if (limits.max_joints == 0U || limits.max_vertices == 0U ||
      !std::isfinite(limits.minimum_absolute_linear_determinant) ||
      limits.minimum_absolute_linear_determinant <= 0.0 ||
      !std::isfinite(limits.minimum_normal_length) ||
      limits.minimum_normal_length <= 0.0) {
    fail("Actor pose limits must be finite, positive, and explicit");
  }
}

[[nodiscard]] double finite_value(const double value,
                                  const char *const description) {
  if (!std::isfinite(value) ||
      value > static_cast<double>(std::numeric_limits<float>::max()) ||
      value < -static_cast<double>(std::numeric_limits<float>::max())) {
    fail(std::string("Actor pose produced a non-finite ") + description);
  }
  return value;
}

[[nodiscard]] float canonical_float(const double value,
                                    const char *const description) {
  const auto checked = finite_value(value, description);
  const auto result = static_cast<float>(checked);
  if (!std::isfinite(result)) {
    fail(std::string("Actor pose produced an out-of-range ") + description);
  }
  return result == 0.0F ? 0.0F : result;
}

void validate_transform(const ActorAffineTransformV1 &transform,
                        const char *const description) {
  for (const auto value : transform.values) {
    if (!std::isfinite(value)) {
      fail(std::string(description) + " contains a non-finite value");
    }
  }
}

[[nodiscard]] double
linear_determinant(const ActorAffineTransformV1 &transform) noexcept {
  const auto &m = transform.values;
  return static_cast<double>(m[0U]) *
             (static_cast<double>(m[5U]) * m[10U] -
              static_cast<double>(m[6U]) * m[9U]) -
         static_cast<double>(m[1U]) *
             (static_cast<double>(m[4U]) * m[10U] -
              static_cast<double>(m[6U]) * m[8U]) +
         static_cast<double>(m[2U]) *
             (static_cast<double>(m[4U]) * m[9U] -
              static_cast<double>(m[5U]) * m[8U]);
}

void require_invertible(const ActorAffineTransformV1 &transform,
                        const ActorPoseLimitsV1 &limits,
                        const char *const description) {
  validate_transform(transform, description);
  const auto determinant = linear_determinant(transform);
  if (!std::isfinite(determinant) ||
      std::abs(determinant) < limits.minimum_absolute_linear_determinant) {
    fail(std::string(description) + " has a singular linear transform");
  }
}

[[nodiscard]] ActorAffineTransformV1 compose(
    const ActorAffineTransformV1 &parent,
    const ActorAffineTransformV1 &local) {
  ActorAffineTransformV1 result;
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      double value = 0.0;
      for (std::size_t inner = 0U; inner < 3U; ++inner) {
        value += static_cast<double>(parent.values[row * 4U + inner]) *
                 local.values[inner * 4U + column];
      }
      result.values[row * 4U + column] =
          canonical_float(value, "composed linear transform");
    }
    double translation = parent.values[row * 4U + 3U];
    for (std::size_t inner = 0U; inner < 3U; ++inner) {
      translation +=
          static_cast<double>(parent.values[row * 4U + inner]) *
          local.values[inner * 4U + 3U];
    }
    result.values[row * 4U + 3U] =
        canonical_float(translation, "composed translation");
  }
  return result;
}

[[nodiscard]] std::array<double, 3U>
transform_position(const ActorAffineTransformV1 &transform,
                   const std::array<double, 3U> &position) {
  std::array<double, 3U> result{};
  for (std::size_t row = 0U; row < 3U; ++row) {
    auto value = static_cast<double>(transform.values[row * 4U + 3U]);
    for (std::size_t column = 0U; column < 3U; ++column) {
      value += static_cast<double>(transform.values[row * 4U + column]) *
               position[column];
    }
    result[row] = finite_value(value, "transformed position");
  }
  return result;
}

[[nodiscard]] std::array<double, 3U> transform_normal(
    const ActorAffineTransformV1 &transform,
    const std::array<double, 3U> &normal,
    const ActorPoseLimitsV1 &limits) {
  const auto &m = transform.values;
  const auto determinant = linear_determinant(transform);
  if (!std::isfinite(determinant) ||
      std::abs(determinant) < limits.minimum_absolute_linear_determinant) {
    fail("Actor normal transform is singular");
  }

  // Cofactor matrix / determinant is inverse-transpose for the row-major
  // linear block when points and normals are treated as column vectors.
  const std::array<double, 9U> inverse_transpose{
      (static_cast<double>(m[5U]) * m[10U] -
       static_cast<double>(m[6U]) * m[9U]) /
          determinant,
      (static_cast<double>(m[6U]) * m[8U] -
       static_cast<double>(m[4U]) * m[10U]) /
          determinant,
      (static_cast<double>(m[4U]) * m[9U] -
       static_cast<double>(m[5U]) * m[8U]) /
          determinant,
      (static_cast<double>(m[2U]) * m[9U] -
       static_cast<double>(m[1U]) * m[10U]) /
          determinant,
      (static_cast<double>(m[0U]) * m[10U] -
       static_cast<double>(m[2U]) * m[8U]) /
          determinant,
      (static_cast<double>(m[1U]) * m[8U] -
       static_cast<double>(m[0U]) * m[9U]) /
          determinant,
      (static_cast<double>(m[1U]) * m[6U] -
       static_cast<double>(m[2U]) * m[5U]) /
          determinant,
      (static_cast<double>(m[2U]) * m[4U] -
       static_cast<double>(m[0U]) * m[6U]) /
          determinant,
      (static_cast<double>(m[0U]) * m[5U] -
       static_cast<double>(m[1U]) * m[4U]) /
          determinant,
  };

  std::array<double, 3U> result{};
  for (std::size_t row = 0U; row < 3U; ++row) {
    double value = 0.0;
    for (std::size_t column = 0U; column < 3U; ++column) {
      value += inverse_transpose[row * 3U + column] * normal[column];
    }
    result[row] = finite_value(value, "transformed normal");
  }
  return result;
}

void validate_binding(const ActorSkinBindingV1 &skin,
                      const std::size_t joint_count) {
  if (skin.influence_count == 0U ||
      skin.influence_count > kActorMaximumSkinInfluencesV1 ||
      skin.weight_sum == 0U) {
    fail("Actor vertex has an invalid skin envelope");
  }
  std::uint32_t sum = 0U;
  for (std::size_t index = 0U; index < skin.joint_indices.size(); ++index) {
    if (index < skin.influence_count) {
      if (skin.joint_indices[index] >= joint_count ||
          skin.weight_numerators[index] == 0U) {
        fail("Actor vertex references an invalid skin influence");
      }
      sum += skin.weight_numerators[index];
    } else if (skin.joint_indices[index] != 0U ||
               skin.weight_numerators[index] != 0U) {
      fail("Actor vertex has non-zero unused skin influences");
    }
  }
  if (sum != skin.weight_sum) {
    fail("Actor vertex skin weights do not equal their exact source sum");
  }
}

} // namespace

ActorPosePaletteV1 build_actor_pose_palette_v1(
    const ActorRigV1 &rig,
    const std::span<const ActorAffineTransformV1> local_pose,
    const ActorPoseLimitsV1 limits) {
  validate_limits(limits);
  if (rig.joints.empty() || rig.joints.size() > limits.max_joints ||
      local_pose.size() != rig.joints.size()) {
    fail("Actor rig and local pose have incompatible joint counts");
  }

  ActorPosePaletteV1 result;
  result.global_joint_transforms.reserve(rig.joints.size());
  result.skin_transforms.reserve(rig.joints.size());
  for (std::size_t index = 0U; index < rig.joints.size(); ++index) {
    const auto &joint = rig.joints[index];
    const auto &local = local_pose[index];
    if ((index == 0U && joint.parent_index != -1) ||
        (index != 0U &&
         (joint.parent_index < 0 ||
          static_cast<std::size_t>(joint.parent_index) >= index))) {
      fail("Actor rig is not one canonical parent-first hierarchy");
    }
    require_invertible(local, limits, "Actor local-pose transform");
    require_invertible(joint.inverse_bind_transform, limits,
                       "Actor inverse-bind transform");

    auto global = local;
    if (joint.parent_index >= 0) {
      global = compose(
          result.global_joint_transforms[static_cast<std::size_t>(
              joint.parent_index)],
          local);
    }
    require_invertible(global, limits, "Actor global-pose transform");
    auto skin = compose(global, joint.inverse_bind_transform);
    require_invertible(skin, limits, "Actor skin transform");
    result.global_joint_transforms.push_back(std::move(global));
    result.skin_transforms.push_back(std::move(skin));
  }
  return result;
}

ActorPosePaletteV1
build_actor_bind_pose_palette_v1(const ActorRigV1 &rig,
                                 const ActorPoseLimitsV1 limits) {
  validate_limits(limits);
  if (rig.joints.empty() || rig.joints.size() > limits.max_joints) {
    fail("Actor bind rig has an invalid joint count");
  }
  std::vector<ActorAffineTransformV1> locals;
  locals.reserve(rig.joints.size());
  for (const auto &joint : rig.joints) {
    locals.push_back(joint.local_bind_transform);
  }
  return build_actor_pose_palette_v1(rig, locals, limits);
}

std::vector<ActorPosedVertexV1> pose_actor_mesh_vertices_v1(
    const ActorSkinnedMeshV1 &mesh, const ActorPosePaletteV1 &palette,
    const ActorAffineTransformV1 &model_to_world,
    const ActorPoseLimitsV1 limits) {
  validate_limits(limits);
  if (mesh.vertices.size() > limits.max_vertices) {
    fail("Actor mesh exceeds the pose vertex limit");
  }
  if (palette.global_joint_transforms.empty() ||
      palette.global_joint_transforms.size() !=
          palette.skin_transforms.size() ||
      palette.skin_transforms.size() > limits.max_joints) {
    fail("Actor skin palette has an invalid joint domain");
  }
  for (const auto &transform : palette.global_joint_transforms) {
    require_invertible(transform, limits, "Actor global-palette transform");
  }
  for (const auto &transform : palette.skin_transforms) {
    require_invertible(transform, limits, "Actor skin-palette transform");
  }
  require_invertible(model_to_world, limits, "Actor model-to-world transform");

  std::vector<ActorPosedVertexV1> result;
  result.reserve(mesh.vertices.size());
  for (const auto &vertex : mesh.vertices) {
    const std::array<double, 3U> source_position{
        vertex.x, vertex.y, vertex.z};
    const std::array<double, 3U> source_normal{
        vertex.nx, vertex.ny, vertex.nz};
    for (const auto value : source_position) {
      static_cast<void>(finite_value(value, "source position"));
    }
    for (const auto value : source_normal) {
      static_cast<void>(finite_value(value, "source normal"));
    }
    if (!std::isfinite(vertex.u) || !std::isfinite(vertex.v)) {
      fail("Actor vertex has non-finite texture coordinates");
    }
    validate_binding(vertex.skin, palette.skin_transforms.size());

    std::array<double, 3U> skinned_position{};
    std::array<double, 3U> skinned_normal{};
    for (std::size_t influence = 0U;
         influence < vertex.skin.influence_count; ++influence) {
      const auto joint = vertex.skin.joint_indices[influence];
      const auto weight =
          static_cast<double>(vertex.skin.weight_numerators[influence]) /
          static_cast<double>(vertex.skin.weight_sum);
      const auto position = transform_position(
          palette.skin_transforms[joint], source_position);
      const auto normal = transform_normal(
          palette.skin_transforms[joint], source_normal, limits);
      for (std::size_t axis = 0U; axis < 3U; ++axis) {
        skinned_position[axis] += weight * position[axis];
        skinned_normal[axis] += weight * normal[axis];
      }
    }

    const auto world_position =
        transform_position(model_to_world, skinned_position);
    auto world_normal =
        transform_normal(model_to_world, skinned_normal, limits);
    const auto normal_length = std::sqrt(
        world_normal[0U] * world_normal[0U] +
        world_normal[1U] * world_normal[1U] +
        world_normal[2U] * world_normal[2U]);
    if (!std::isfinite(normal_length) ||
        normal_length < limits.minimum_normal_length) {
      fail("Actor pose produced a zero-length world normal");
    }
    for (auto &component : world_normal) {
      component /= normal_length;
    }

    result.push_back(ActorPosedVertexV1{
        canonical_float(world_position[0U], "world X"),
        canonical_float(world_position[1U], "world Y"),
        canonical_float(world_position[2U], "world Z"),
        canonical_float(world_normal[0U], "world normal X"),
        canonical_float(world_normal[1U], "world normal Y"),
        canonical_float(world_normal[2U], "world normal Z"),
        vertex.u == 0.0F ? 0.0F : vertex.u,
        vertex.v == 0.0F ? 0.0F : vertex.v,
        vertex.rgba8});
  }
  return result;
}

std::vector<ActorPosedPositionV1> pose_actor_mesh_positions_v1(
    const ActorSkinnedMeshV1 &mesh, const ActorPosePaletteV1 &palette,
    const ActorAffineTransformV1 &model_to_world,
    const ActorPoseLimitsV1 limits) {
  validate_limits(limits);
  if (mesh.vertices.size() > limits.max_vertices) {
    fail("Actor mesh exceeds the pose vertex limit");
  }
  if (palette.global_joint_transforms.empty() ||
      palette.global_joint_transforms.size() !=
          palette.skin_transforms.size() ||
      palette.skin_transforms.size() > limits.max_joints) {
    fail("Actor skin palette has an invalid joint domain");
  }
  for (const auto &transform : palette.global_joint_transforms) {
    validate_transform(transform, "Actor global-palette transform");
  }
  for (const auto &transform : palette.skin_transforms) {
    validate_transform(transform, "Actor skin-palette transform");
  }
  require_invertible(model_to_world, limits, "Actor model-to-world transform");

  std::vector<ActorPosedPositionV1> result;
  result.reserve(mesh.vertices.size());
  for (const auto &vertex : mesh.vertices) {
    const std::array<double, 3U> source_position{
        vertex.x, vertex.y, vertex.z};
    for (const auto value : source_position) {
      static_cast<void>(finite_value(value, "source position"));
    }
    validate_binding(vertex.skin, palette.skin_transforms.size());

    std::array<double, 3U> skinned_position{};
    for (std::size_t influence = 0U;
         influence < vertex.skin.influence_count; ++influence) {
      const auto joint = vertex.skin.joint_indices[influence];
      const auto weight =
          static_cast<double>(vertex.skin.weight_numerators[influence]) /
          static_cast<double>(vertex.skin.weight_sum);
      const auto position = transform_position(
          palette.skin_transforms[joint], source_position);
      for (std::size_t axis = 0U; axis < 3U; ++axis) {
        skinned_position[axis] += weight * position[axis];
      }
    }

    const auto world_position =
        transform_position(model_to_world, skinned_position);
    result.push_back(ActorPosedPositionV1{
        canonical_float(world_position[0U], "world X"),
        canonical_float(world_position[1U], "world Y"),
        canonical_float(world_position[2U], "world Z")});
  }
  return result;
}

} // namespace openrc
