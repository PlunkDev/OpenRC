#pragma once

#include "openrc/actor_animation.hpp"
#include "openrc/rac_moby_bind_pose.hpp"
#include "openrc/rac_moby_class.hpp"
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

struct RacMobyAnimationClipProfileV1 {
  std::uint32_t clip_id = 0U;
  std::uint32_t source_slot = 0U;
  std::string semantic_key;
  ActorAnimationWrapModeV1 wrap_mode = ActorAnimationWrapModeV1::clamp;
};

struct RacMobyAnimationCompileLimitsV1 {
  RacRatchetSequenceLimitsV1 sequence;
  RacRatchetPoseLimitsV1 pose;
  ActorAnimationLimitsV1 animation;
};

class RacMobyAnimationCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Builds one deterministic profile for every occupied source sequence slot in
// an ordinary RAC1 Moby class. Confirmed semantic mappings remain first, in
// caller order. Every unclassified sequence receives a zero-padded
// source-address key so the complete source corpus can be compiled before its
// gameplay-state meaning has been proven.
[[nodiscard]] std::vector<RacMobyAnimationClipProfileV1>
make_rac_moby_complete_animation_profiles_v1(
    const RacMobyClassV1 &source_class,
    std::span<const RacMobyAnimationClipProfileV1> confirmed_profiles,
    std::string_view source_sequence_key_prefix,
    ActorAnimationWrapModeV1 unclassified_wrap_mode);

// Compiles explicitly mapped animation sequences from one ordinary RAC1 Moby
// class into a neutral animation bank. Ordinary Moby frame offsets are
// class-asset-relative; that source-specific addressing stops at this
// boundary, while neutral clips remain pinned to the exact canonical rig.
[[nodiscard]] ActorAnimationBankV1 compile_rac_moby_animation_bank_v1(
    std::span<const std::byte> class_bytes,
    const RacMobyClassV1 &source_class, const RacMobyBindRigV1 &bind_rig,
    std::string rig_key,
    std::span<const RacMobyAnimationClipProfileV1> profiles,
    std::uint32_t source_updates_per_second,
    RacMobyAnimationCompileLimitsV1 limits);

} // namespace openrc
