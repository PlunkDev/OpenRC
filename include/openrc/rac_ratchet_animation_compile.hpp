#pragma once

#include "openrc/actor_animation.hpp"
#include "openrc/rac_level_core.hpp"
#include "openrc/rac_moby_bind_pose.hpp"
#include "openrc/rac_ratchet_pose.hpp"
#include "openrc/rac_ratchet_sequence.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace openrc {

struct RacRatchetAnimationClipProfileV1 {
  std::uint32_t clip_id = 0U;
  std::uint32_t source_slot = 0U;
  std::string semantic_key;
  ActorAnimationWrapModeV1 wrap_mode = ActorAnimationWrapModeV1::clamp;
};

struct RacRatchetAnimationCompileLimitsV1 {
  RacRatchetSequenceLimitsV1 sequence;
  RacRatchetPoseLimitsV1 pose;
  ActorAnimationLimitsV1 animation;
};

class RacRatchetAnimationCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compiles explicitly mapped RAC1 Ratchet sequence slots into a neutral,
// source-independent animation bank. Source offsets, packed frame records,
// and RAC slot numbers stop at this boundary; runtime consumers resolve only
// semantic clip and rig keys.
[[nodiscard]] ActorAnimationBankV1 compile_rac_ratchet_animation_bank_v1(
    std::span<const std::byte> decoded_level_core,
    const RacLevelCoreIndexV1 &level_core,
    const RacMobyBindRigV1 &bind_rig,
    float class_scale,
    std::string rig_key,
    std::span<const RacRatchetAnimationClipProfileV1> profiles,
    std::uint32_t source_updates_per_second,
    RacRatchetAnimationCompileLimitsV1 limits);

} // namespace openrc
