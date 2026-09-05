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
#include <string_view>
#include <vector>

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

// Builds one deterministic profile entry for every occupied Ratchet sequence
// slot. Explicitly confirmed semantic mappings remain first, in caller order.
// Every other source sequence receives a zero-padded source-address key and
// the caller-selected non-semantic wrap policy. This preserves the complete
// source animation corpus without inventing gameplay-state names for unknown
// slots.
[[nodiscard]] std::vector<RacRatchetAnimationClipProfileV1>
make_rac_ratchet_complete_animation_profiles_v1(
    const RacLevelCoreIndexV1 &level_core,
    std::span<const RacRatchetAnimationClipProfileV1> confirmed_profiles,
    std::string_view source_sequence_key_prefix,
    ActorAnimationWrapModeV1 unclassified_wrap_mode);

// Compiles explicitly mapped RAC1 Ratchet sequence slots into a neutral,
// source-independent animation bank. Source offsets and packed frame records
// stop at this boundary. Callers may preserve a slot as part of a neutral key
// without exposing RAC source layouts to runtime consumers.
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
