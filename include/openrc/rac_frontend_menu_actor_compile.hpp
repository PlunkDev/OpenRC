#pragma once

#include "openrc/rac_actor_library_compile.hpp"
#include "openrc/rac_frontend_main_compile.hpp"
#include "openrc/rac_moby_animation_compile.hpp"
#include "openrc/actor_animation_io.hpp"
#include "openrc/actor_library_io.hpp"
#include "openrc/scene_timeline.hpp"

namespace openrc {

struct RacFrontendMenuActorCompileLimitsV1 {
  RacMobyClassLimitsV1 source_class;
  RacMobyBindPoseLimitsV1 bind_pose;
  ActorLibraryLimitsV1 actors;
  RacMobyAnimationCompileLimitsV1 animation;
};
struct RacFrontendMenuActorCompileResultV1 {
  ActorLibraryV1 actor_library;
  ActorAnimationBankV1 actor_animation;
  // These identities remain compiler-only. One neutral model/rig is shared
  // by14 instances; source sequence identity is not a neutral clip ID.
  std::array<std::uint32_t,14U> source_sequences{};
  std::array<std::uint32_t,14U> slot_clip_ids{};
  std::uint32_t source_class_scale_bits=0U;
};
struct RacFrontendMenuActorTimelineLimitsV1 {
  ActorLibraryIoLimitsV1 actors;
  ActorAnimationIoLimitsV1 animation;
  SceneTimelineLimitsV1 timeline;
};

// Source21a6cc ->20e180 ->212658 is the ordinary Moby model path.
// Reuses the complete high-LOD actor baker and ordinary animation compiler;
// no menu-specific mesh, placeholder texture or material fallback is made.
// Class scale/source1024 conversion is already baked into both resources.
[[nodiscard]] RacFrontendMenuActorCompileResultV1
compile_rac_frontend_menu_actor_v1(
    std::span<const std::byte> class1138,
    const RacLevelMobyTextureBankV1 &texture_bank,
    const std::array<std::uint8_t,16U> &texture_slots,
    std::uint8_t used_texture_slot_count,
    const std::array<std::uint32_t,14U> &source_sequences,
    std::uint32_t updates_per_second,
    RacFrontendMenuActorCompileLimitsV1 limits);

// The existing source lifecycle provides13 samples after pre/corners/post.
// Exact endpoints are represented by their selected neutral frame; .5 uses
// the two actual adjacent source frames. A final moving sample, a transition
// between sequences, or nonzero source object rotation rejects. The result
// uses the qualified menu camera,512:512 display and holds its final sample.
[[nodiscard]] SceneTimelineV1 compile_rac_frontend_menu_actor_timeline_v1(
    const RacFrontendMenuActorCompileResultV1 &compiled,
    const std::array<RacFrontendMainGeometryFrameV1,13U> &entry,
    RacFrontendMenuActorTimelineLimitsV1 limits);

class RacFrontendMenuActorCompileError final: public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
} // namespace openrc
