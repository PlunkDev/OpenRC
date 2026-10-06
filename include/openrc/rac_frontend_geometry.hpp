#pragma once

#include "openrc/rac_frontend_object.hpp"

namespace openrc {
struct RacFrontendObjectGeometryV1 {
  std::array<RacMobyPostVectorV1,4> corners{};
  std::array<std::uint32_t,2> edge_length_bits{}; // source PVar+40/+44
};
// Original23b5d0 tail: (corner1-corner0),(corner2-corner0), followed by
// separate1f9cb8 VMUL, ADDAx/y, MADDz, VSQRT and VADDq value paths.
[[nodiscard]] RacFrontendObjectGeometryV1 complete_rac_frontend_geometry_v1(
    const std::array<RacMobyPostVectorV1,4> &corners);
// Full geometry-value chain. Execute after pre/stop and before the new post,
// retaining the matrix from the preceding source post just like23b5d0.
[[nodiscard]] RacFrontendObjectGeometryV1 sample_rac_frontend_object_geometry_v1(
    const RacFrontendObjectV1 &object,
    const RacFrontendObjectClassV1 &source_class,
    const RacRatchetSequenceV1 &sequence,
    const RacMobyBindRigV1 &bind_rig,RacRatchetPoseLimitsV1 pose_limits);
} // namespace openrc
