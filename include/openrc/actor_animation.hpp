#pragma once

#include "openrc/prepared_game_v2.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kActorAnimationBankSchemaVersionV1 = 1U;

enum class ActorAnimationWrapModeV1 : std::uint32_t {
  clamp = 0U,
  loop = 1U,
};

// A neutral local joint transform. The terminal scale is applied only after
// hierarchy composition and therefore does not propagate to child joints.
// Finite zero scale is authored data and is deliberately preserved.
struct ActorJointPoseV1 {
  std::array<float, 4U> normalized_rotation_xyzw{0.0F, 0.0F, 0.0F, 1.0F};
  std::array<float, 3U> translation{};
  std::array<float, 3U> local_scale{1.0F, 1.0F, 1.0F};
  std::array<float, 3U> terminal_scale{1.0F, 1.0F, 1.0F};

  [[nodiscard]] bool operator==(const ActorJointPoseV1 &) const = default;
};

struct ActorAnimationFrameV1 {
  // Phase units advanced by one source update while this frame is current.
  // Zero is valid and represents a held frame.
  float phase_rate = 0.0F;
  std::vector<ActorJointPoseV1> joint_poses;

  [[nodiscard]] bool operator==(const ActorAnimationFrameV1 &) const = default;
};

struct ActorAnimationClipV1 {
  // IDs are dense indices local to one bank. Runtime users address clips and
  // rigs by semantic key so package overlays do not depend on local IDs.
  std::uint32_t id = 0U;
  std::string semantic_key;
  std::string rig_key;
  // Pins the exact canonical rig behind rig_key so an overlay cannot silently
  // reinterpret joint indices while retaining the same semantic alias.
  PreparedContentDigestV1 rig_content_sha256{};
  // Exact source cadence (PAL is 50 and NTSC is 60); integer storage avoids
  // introducing timing drift at the package boundary.
  std::uint32_t source_updates_per_second = 0U;
  ActorAnimationWrapModeV1 wrap_mode = ActorAnimationWrapModeV1::clamp;
  std::vector<ActorAnimationFrameV1> frames;

  [[nodiscard]] bool operator==(const ActorAnimationClipV1 &) const = default;
};

struct ActorAnimationBankV1 {
  std::uint32_t schema_version = kActorAnimationBankSchemaVersionV1;
  std::vector<ActorAnimationClipV1> clips;

  [[nodiscard]] bool operator==(const ActorAnimationBankV1 &) const = default;
};

// Counts are aggregate across the bank unless marked as per-clip/per-frame.
// Both semantic_key and rig_key contribute to total semantic-key bytes.
struct ActorAnimationLimitsV1 {
  std::uint32_t max_clips = 0U;
  std::uint32_t max_frames_per_clip = 0U;
  std::uint64_t max_total_frames = 0U;
  std::uint32_t max_joints_per_frame = 0U;
  std::uint64_t max_total_joint_poses = 0U;
  std::uint32_t max_semantic_key_bytes = 0U;
  std::uint64_t max_total_semantic_key_bytes = 0U;
  std::uint32_t max_source_updates_per_second = 0U;
  float max_absolute_component = 0.0F;
  double minimum_quaternion_length = 0.0;

  [[nodiscard]] bool operator==(const ActorAnimationLimitsV1 &) const = default;
};

class ActorAnimationError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Requires the exact V1 schema, dense clip IDs, unique canonical keys, a
// non-zero rig digest, positive source cadence, known wrap modes, non-empty
// frames with one fixed positive joint count per clip, canonical finite
// floats, normalized shortest-neutral quaternions, and every caller limit.
void validate_actor_animation_bank_v1(const ActorAnimationBankV1 &bank,
                                      ActorAnimationLimitsV1 limits);

// Sorts clips by ID, maps signed zero to positive zero, normalizes quaternion
// length and sign deterministically, then performs the same strict validation.
// Duplicate or sparse IDs and duplicate semantic keys are never repaired.
[[nodiscard]] ActorAnimationBankV1
canonicalize_actor_animation_bank_v1(ActorAnimationBankV1 bank,
                                     ActorAnimationLimitsV1 limits);

// Combines independently compiled neutral animation banks into one package
// resource. Clip IDs are reassigned densely in input order; semantic keys
// remain the stable identity and therefore may not repeat. Each input and the
// aggregate result are validated under the caller's same explicit limits.
[[nodiscard]] ActorAnimationBankV1 compose_actor_animation_banks_v1(
    std::span<const ActorAnimationBankV1> banks,
    ActorAnimationLimitsV1 limits);

} // namespace openrc
