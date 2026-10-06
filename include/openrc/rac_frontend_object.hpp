#pragma once

#include "openrc/rac_frontend_object_animation.hpp"
#include "openrc/rac_moby_allocate.hpp"
#include "openrc/rac_moby_post.hpp"
#include "openrc/rac_moby_bind_pose.hpp"
#include "openrc/rac_ratchet_pose.hpp"

namespace openrc {

struct RacFrontendObjectClassV1 {
  RacMobyAllocateClassV1 allocation;
  RacFrontendObjectAnimationBankV1 animations;
  std::vector<RacMobyPostSequenceHeaderV1> post_headers;
  std::uint32_t source_bytes = 0;
  // 160fd0 selectors 0,1,2,3 resolve through the actual model+1c path
  // directory in211548. These are path-tail joint indices, not selectors.
  std::array<std::uint8_t,4> corner_joints{};
};

// Actual bound class1138 source data, plus the original class table/callback
// lookup values supplied by the loader. No callback is guessed from class ID.
[[nodiscard]] RacFrontendObjectClassV1 decode_rac_frontend_object_class_v1(
    std::span<const std::byte> bytes, std::uint32_t source_reference,
    std::uint8_t source_class_table_index,
    std::uint32_t source_registered_callback, std::uint64_t max_input_bytes);

struct RacFrontendObjectV1 {
  // The allocator result records construction, not a second mutable actor.
  RacMobyAllocatedFreshV1 admission;
  std::uint32_t source_address = 0;
  RacMobyPostActorV1 post;
  RacFrontendObjectAnimationStateV1 animation;
  std::uint8_t byte_30 = 0xff;
  std::uint8_t byte_31 = 1;
  std::uint16_t half_32 = 0xff;
  std::uint64_t packed_38 = UINT64_C(0x0020202000000e0e);
  std::uint32_t callback_reference = 0x23b578;
  bool operator==(const RacFrontendObjectV1 &) const = default;
};

// Original226720(1138) ->20d348 ->20d440 ->20ed48, then219ffc..21a07c,
// for every one of14 slots. Reuses the existing allocator/constructor and
// executed zero-rotation post; insufficient source capacity rejects. Caller
// owns the existing staged canonical SessionState and full pool bindings.
// Successful work installs real source metadata/PVars; failure is atomic.
// Graphics buffer setup226d50 and camera219c08 have separate owners.
[[nodiscard]] std::array<RacFrontendObjectV1,14>
admit_rac_frontend_objects_v1(
    const RacFrontendObjectClassV1 &source_class,
    const std::array<std::uint32_t,14> &screen_sequences,
    const std::array<std::uint32_t,3> &source_camera_position,
    const RacMobyAllocateBindingsV1 &allocation_bindings,
    game::SessionStateV1 &staged_state, std::uint64_t expected_revision,
    RacMobyAllocateLimitsV1 allocation_limits);

// Execute source pre/stop followed by the real post. Geometry callback
//23b5d0 must consume the returned pre/stop selection BEFORE this post when
// composing a complete update; it reads the matrix from the preceding post.
// These separate calls retain that important source ordering.
void advance_rac_frontend_object_animation_v1(
    RacFrontendObjectV1 &object, const RacFrontendObjectClassV1 &source_class);
void update_rac_frontend_object_post_v1(
    RacFrontendObjectV1 &object, const RacFrontendObjectClassV1 &source_class);

// Executed211548/211808 ->20db98, with the exact source-unit joint sampler,
// ordered class-scale MUL, VU MUL,1f9ec0 and position ADD. Call after pre/stop
// and before post: object.post.matrix_bits belongs to the preceding update.
// This returns precisely the four corner qwords written to PVar+00..+30.
// The later23b5d0 edge lengths at+40/+44 require a separate qualified VSQRT
// owner and are not reported as completed by this bounded corner consumer.
[[nodiscard]] std::array<RacMobyPostVectorV1,4>
sample_rac_frontend_object_corners_v1(
    const RacFrontendObjectV1 &object,
    const RacFrontendObjectClassV1 &source_class,
    const RacRatchetSequenceV1 &sequence,
    const RacMobyBindRigV1 &bind_rig, RacRatchetPoseLimitsV1 pose_limits);

struct RacFrontendObjectScreenV1 {
  std::uint32_t source_reference = 0;
  std::uint32_t parent_reference = 0;
  std::uint32_t active_state = 0;
  std::array<std::uint32_t,14> sequences{};
  std::array<std::uint32_t,14> node_references{};
  std::array<std::uint32_t,14> init_callbacks{};
  std::array<std::uint32_t,14> cleanup_callbacks{};
};

struct RacFrontendObjectScreenStateV1 {
  std::uint32_t mode = 2;
  std::uint32_t current_screen = 0;
  std::uint32_t requested_screen = 0;
  std::uint32_t previous_screen = 0;
  std::uint32_t remaining_updates = 0;
  bool operator==(const RacFrontendObjectScreenStateV1 &) const = default;
};
struct RacFrontendObjectNodeBindingV1 {
  std::uint32_t node_reference = 0;
  std::uint32_t object_reference = 0;
  bool operator==(const RacFrontendObjectNodeBindingV1 &) const = default;
};
struct RacFrontendObjectScreenStepV1 {
  RacFrontendObjectScreenStateV1 state;
  std::vector<RacFrontendObjectNodeBindingV1> node_bindings;
  // Real22ed80 arguments0 (3=same screen,4=different),1=17; argument2 is
  // object0. Audio submission is external to this integer screen transition.
  std::optional<std::uint32_t> sound_kind;
  bool arrived = false;
  bool began_transition = false;
};

// Original21a2f4..21a500 transition stage. Executes the actual12-update
// gate,14sequence setters, reverse/forward direction and node+14 binding.
// Initial219e90 sets current=requested=main, so its first owner update also
// takes this real transition. Reached nonzero cleanup/init callbacks fail
// explicitly until their own owners are integrated; actual main nodes use0.
// Input callbacks, owner camera/card bookkeeping and14object pre/post remain
// separate executed stages, never represented by caller-observed results.
[[nodiscard]] RacFrontendObjectScreenStepV1 step_rac_frontend_object_screen_v1(
    const RacFrontendObjectScreenStateV1 &state,
    std::span<const RacFrontendObjectScreenV1> screens,
    std::array<RacFrontendObjectV1,14> &objects,
    const RacFrontendObjectClassV1 &source_class);

class RacFrontendObjectError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

} // namespace openrc
