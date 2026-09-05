#pragma once

#include "openrc/actor_rig.hpp"
#include "openrc/rac_moby_model_geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

struct RacMobyBindRigLimitsV1 {
  std::uint64_t max_input_bytes = 0U;
  std::uint64_t max_joints = 0U;
  // General affine inversion is rejected below this absolute determinant.
  double minimum_absolute_linear_determinant = 0.0;
};

struct RacMobyBindPoseLimitsV1 {
  RacMobyBindRigLimitsV1 rig_limits;
  RacMobyModelGeometryLimitsV1 geometry_limits;
  std::uint64_t max_output_skin_bindings = 0U;
};

struct RacMobyBindRigV1 {
  ActorRigV1 actor_rig;
  // Raw finite XYZ values from the RAC common-translation records, parallel
  // to actor_rig.joints. Their animation meaning is intentionally preserved
  // without equating them to the independently recovered local bind pose.
  std::vector<std::array<float, 3U>> source_common_translations;
};

struct RacMobyBindPoseGeometryV1 {
  RacMobyBindRigV1 bind_rig;
  RacMobyModelGeometryV1 geometry;
  // Parallel to geometry.vertices. Duplicated geometry retains the binding of
  // the earlier packet/local source identified by the model provenance.
  std::vector<ActorSkinBindingV1> vertex_skin_bindings;
};

class RacMobyBindPoseError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Decodes the static bind rig only. V1 requires complete skeleton and common
// translation tables in this class; owner resolution for animation-only or
// borrowed rigs is a separate asset-loading step that is not guessed here.
// This contract intentionally does not interpret RAC sequence/frame payloads
// as animation TRS.
[[nodiscard]] RacMobyBindRigV1 decode_rac_moby_bind_rig_v1(
    std::span<const std::byte> class_bytes, const RacMobyClassV1 &moby,
    RacMobyBindRigLimitsV1 limits);

// Reconstructs one regular LOD plus exact per-vertex skin influences. RAC VU0
// matrix state is carried across packets in the selected LOD and reset between
// calls. Metal packets and animation decoding remain outside this V1 contract.
[[nodiscard]] RacMobyBindPoseGeometryV1 compile_rac_moby_bind_pose_geometry_v1(
    std::span<const std::byte> class_bytes, const RacMobyClassV1 &moby,
    RacMobyLodV1 lod, RacMobyBindPoseLimitsV1 limits);

} // namespace openrc
