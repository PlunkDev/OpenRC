#pragma once

#include "openrc/actor_pose.hpp"
#include "openrc/rac_moby_bind_pose.hpp"
#include "openrc/rac_ratchet_sequence.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace openrc {

struct RacRatchetPoseLimitsV1 {
  std::uint64_t max_joints = 0U;
  std::uint64_t max_frame_payload_bytes = 0U;
  std::uint64_t max_sparse_scale_records = 0U;
  std::uint64_t max_sparse_translation_records = 0U;
  double minimum_quaternion_length = 0.0;
};

// Compiler-side recovery of one source joint record. The quaternion component
// order and sparse overrides are established by the PAL executable, while the
// distinction between local and terminal scale is retained explicitly.
struct RacRatchetJointPoseV1 {
  std::array<float, 4U> normalized_rotation_xyzw{};
  std::array<float, 3U> translation{};
  std::array<float, 3U> local_scale{1.0F, 1.0F, 1.0F};
  std::array<float, 3U> terminal_scale{1.0F, 1.0F, 1.0F};

  [[nodiscard]] bool operator==(const RacRatchetJointPoseV1 &) const = default;
};

struct RacRatchetPoseV1 {
  std::vector<RacRatchetJointPoseV1> source_joint_poses;

  // Direct G and G * inverse-bind results. Authored zero scale components are
  // preserved, so these matrices may be singular. They intentionally do not
  // pass through build_actor_pose_palette_v1, whose runtime contract requires
  // every transform to be invertible.
  ActorPosePaletteV1 palette;
};

class RacRatchetPoseError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Decodes one structurally regular frame. Primary records are one normalized
// signed-16 XYZW quaternion per rig joint. Sparse translations replace the
// corresponding common translation, with both expressed using
// class_scale / 1024. Sparse scales either participate in hierarchy
// composition or are applied terminally to only that joint's global matrix.
[[nodiscard]] RacRatchetPoseV1 decode_rac_ratchet_regular_pose_v1(
    const RacRatchetSequenceV1 &sequence, std::uint64_t frame_index,
    const RacMobyBindRigV1 &bind_rig, float class_scale,
    RacRatchetPoseLimitsV1 limits);

} // namespace openrc
