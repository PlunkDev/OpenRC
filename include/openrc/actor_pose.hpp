#pragma once

#include "openrc/actor_library.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Runtime animation poses use the same general-affine convention as the
// package rig. Keeping the complete global and skin palettes makes the
// parent-space composition independently testable and leaves attachment
// points available without re-walking the hierarchy.
struct ActorPosePaletteV1 {
  std::vector<ActorAffineTransformV1> global_joint_transforms;
  std::vector<ActorAffineTransformV1> skin_transforms;

  [[nodiscard]] bool operator==(const ActorPosePaletteV1 &) const = default;
};

struct ActorPosedVertexV1 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float nx = 0.0F;
  float ny = 0.0F;
  float nz = 1.0F;
  float u = 0.0F;
  float v = 0.0F;
  std::uint32_t rgba8 = UINT32_C(0xffffffff);

  [[nodiscard]] bool operator==(const ActorPosedVertexV1 &) const = default;
};

struct ActorPoseLimitsV1 {
  std::uint32_t max_joints = 0U;
  std::uint64_t max_vertices = 0U;
  double minimum_absolute_linear_determinant = 0.0;
  double minimum_normal_length = 0.0;

  [[nodiscard]] bool operator==(const ActorPoseLimitsV1 &) const = default;
};

class ActorPoseError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Composes a parent-first local pose into model-space globals and computes
// Skin_i = GlobalCurrent_i * InverseBind_i. The pose must contain exactly one
// local transform per rig joint.
[[nodiscard]] ActorPosePaletteV1 build_actor_pose_palette_v1(
    const ActorRigV1 &rig,
    std::span<const ActorAffineTransformV1> local_pose,
    ActorPoseLimitsV1 limits);

// Uses the rig's authored local bind transforms as the current pose. A valid
// rig therefore produces identity skin transforms within floating-point
// precision without applying the bind transform to model-space vertices a
// second time.
[[nodiscard]] ActorPosePaletteV1
build_actor_bind_pose_palette_v1(const ActorRigV1 &rig,
                                 ActorPoseLimitsV1 limits);

// CPU linear-blend skinning for one complete mesh. Positions are blended by
// exact integer source weights, then model_to_world is applied. Normals use
// inverse-transpose linear transforms, are blended, transformed to world
// space, and normalized. Topology and material ranges remain immutable in the
// ActorSkinnedMeshV1 owned by the model.
[[nodiscard]] std::vector<ActorPosedVertexV1> pose_actor_mesh_vertices_v1(
    const ActorSkinnedMeshV1 &mesh, const ActorPosePaletteV1 &palette,
    const ActorAffineTransformV1 &model_to_world,
    ActorPoseLimitsV1 limits);

} // namespace openrc
