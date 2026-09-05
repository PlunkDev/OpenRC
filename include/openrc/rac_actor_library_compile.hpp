#pragma once

#include "openrc/actor_library.hpp"
#include "openrc/rac_level_moby_texture.hpp"
#include "openrc/rac_moby_bind_pose.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

// RAC metal-family packets refer to negative effect-material sentinels rather
// than class-local texture slots. RenderSceneMaterialV1 cannot yet express the
// original view-dependent effect, so a caller that wants a renderable neutral
// fallback must opt in explicitly for each observed sentinel.
struct RacActorSpecialMaterialPolicyV1 {
  std::int32_t source_effect_material_index = -2;
  std::uint32_t fallback_base_color_rgba8 = UINT32_C(0xffffffff);
};

// Compiler-only RAC1 inputs for one neutral actor model. Source class IDs,
// packet offsets, and level ownership deliberately do not cross this adapter.
// The first used_texture_slot_count entries form the active class-local
// material map; every remaining entry must be the RAC1 0xff sentinel.
struct RacActorLibraryCompileRequestV1 {
  std::string rig_semantic_key;
  std::string model_semantic_key;
  RacMobyBindPoseGeometryV1 bind_pose;
  std::array<std::uint8_t, 16U> texture_slots{
      0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
      0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU};
  std::uint8_t used_texture_slot_count = 0U;
  RacLevelMobyTextureBankV1 texture_bank;
  std::vector<RacActorSpecialMaterialPolicyV1> special_material_policies;
};

class RacActorLibraryCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compiles one high-LOD bind-pose model and its optional independently
// recovered metal overlay into a self-contained neutral actor library. The
// overlay becomes a separate skinned mesh and requires an explicit fallback
// policy for every source effect sentinel; no negative sentinel is guessed to
// be a local texture. Only triangle-referenced vertices and source images are
// retained. Triangle/corner order and exact skin numerators/sums are preserved;
// canonical ActorLibraryV1 IDs, influence order, and content digests are then
// supplied by the neutral canonicalizer under the caller's explicit limits.
[[nodiscard]] ActorLibraryV1 compile_rac_actor_library_v1(
    const RacActorLibraryCompileRequestV1 &request,
    ActorLibraryLimitsV1 limits);

} // namespace openrc
