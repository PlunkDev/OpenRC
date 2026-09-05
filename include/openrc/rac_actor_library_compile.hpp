#pragma once

#include "openrc/actor_library.hpp"
#include "openrc/rac_level_moby_texture.hpp"
#include "openrc/rac_moby_bind_pose.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace openrc {

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
};

class RacActorLibraryCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compiles one regular high-LOD bind-pose model into a self-contained neutral
// actor library. Only triangle-referenced vertices and source images are
// retained. Triangle/corner order and exact skin numerators/sums are preserved;
// canonical ActorLibraryV1 IDs, influence order, and content digests are then
// supplied by the neutral canonicalizer under the caller's explicit limits.
[[nodiscard]] ActorLibraryV1 compile_rac_actor_library_v1(
    const RacActorLibraryCompileRequestV1 &request,
    ActorLibraryLimitsV1 limits);

} // namespace openrc
