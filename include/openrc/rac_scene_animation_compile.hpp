#pragma once

#include "openrc/actor_animation.hpp"
#include "openrc/actor_animation_player.hpp"
#include "openrc/rac_moby_bind_pose.hpp"
#include "openrc/rac_ratchet_pose.hpp"
#include "openrc/scene_animation_bank.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

struct RacSceneAnimationActorBindingV1 {
  // Actor ordinal is the identity within a scene. Several actors may use the
  // same class, so binding by class alone would silently merge their motion.
  std::uint32_t actor_index = 0U;
  std::uint32_t expected_class_id = 0U;
  std::string semantic_key;
  std::string rig_key;
  RacMobyBindRigV1 bind_rig;
  float class_scale = 0.0F;
};

struct RacSceneAnimationCompileLimitsV1 {
  SceneAnimationBankLimitsV1 scene;
  RacRatchetSequenceLimitsV1 sequence;
  RacRatchetPoseLimitsV1 pose;
  ActorAnimationLimitsV1 animation;
};

class RacSceneAnimationCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct RacSceneAnimationCameraSampleV1 {
  std::array<float, 3U> position{};
  // The original calls rotations about X, Y, then Z. Its final camera basis
  // permutes/signs matrix rows; these angles are not a ready-made view matrix.
  std::array<float, 3U> rotation_xyz_radians{};
  // Horizontal half-angle tangent, copied to original projection owner+b0.
  // Original owner derives its vertical tangent by multiplying this by
  // 0x3f418937 (PAL selector) or 0x3f466666 (other selector). It is not an angle.
  float projection_parameter = 0.0F;
  bool select_next_actor_frame_on_odd_update = false;
};

struct RacSceneAnimationTickV1 {
  std::uint32_t source_update = 0U;
  std::uint32_t current_frame = 0U;
  std::uint32_t next_frame = 0U;
  float interpolation = 0.0F;
  RacSceneAnimationCameraSampleV1 camera;
  std::vector<std::array<float, 3U>> actor_world_positions;
};

// Evaluates one already-admitted chunk update using original 29a158 and
// 29a618..29a6bc: camera[update], frames update/2 and update/2+1, odd half
// phase, and the camera's odd-update next-frame override. Root translation
// executes two ordered VU MULs and one VU ADD through the existing numerical
// reference. No scene clock, chunk transition, FOV conversion or audio is
// invented here. Out-of-range guard samples are rejected, never clamped.
[[nodiscard]] RacSceneAnimationTickV1 sample_rac_scene_animation_tick_v1(
    std::span<const std::byte> decoded_scene,
    std::uint32_t source_update,
    SceneAnimationBankLimitsV1 limits);

// The original frontend background has a separate confirmed consumer:
// 1eb338 fixes horizontal projection to bits 3f2147ae; 1eb458 ignores the
// camera control byte and uses parity * .5 for every actor. Pose/root decode
// and the neutral skeletal player are shared with the general scene path.
[[nodiscard]] RacSceneAnimationTickV1 sample_rac_frontend_background_tick_v1(
    std::span<const std::byte> decoded_scene,
    std::uint32_t source_update,
    SceneAnimationBankLimitsV1 limits);

struct RacFrontendBackgroundPositionV1 {
  std::uint32_t total_update = 0U;
  std::uint32_t chunk_index = 0U;
  std::uint32_t chunk_update = 0U;
  bool reload_chunk = false;
  bool looped = false;
  bool operator==(const RacFrontendBackgroundPositionV1 &) const = default;
};

// Compiler-side execution of 1eb49c..1eb50c, with 205220/204fc0's observed
// chunk-counter reset. The frontend threshold is always 96, including PAL.
// Initialized counters are zero; the first step samples update 1, and a
// later chunk installation samples update 0. This does not run the frontend
// owner, its asset I/O, input, menu callbacks, or a runtime scene sequencer.
class RacFrontendBackgroundClockV1 final {
public:
  explicit RacFrontendBackgroundClockV1(std::uint32_t source_duration);
  [[nodiscard]] RacFrontendBackgroundPositionV1 step();
private:
  std::uint32_t duration_ = 0U;
  RacFrontendBackgroundPositionV1 position_;
};

struct RacFrontendBackgroundChunksV1 {
  std::uint32_t source_duration = 0U;
  std::vector<std::vector<std::byte>> decoded_chunks;
};

// Original 1eb17c..1eb1c8 source directory in the already-decoded frontend
// WAD: bank = header[4]+header[80], up to 70 (offset, byte-size) rows, stop
// at size zero, compressed bytes = bank+0x800+offset. Bounded source chunks
// are decoded with the existing WAD decoder and parsed as SceneAnimationBank.
[[nodiscard]] RacFrontendBackgroundChunksV1 decode_rac_frontend_background_v1(
    std::span<const std::byte> decoded_frontend_wad,
    SceneAnimationBankLimitsV1 scene_limits,
    std::uint64_t max_total_decoded_bytes);

// Projects an evaluated source selection into the existing neutral player.
// Source phase==1 is represented exactly as next-frame phase==0; the regular
// animation player remains the only skeletal sampler.
[[nodiscard]] ActorAnimationPlaybackStateV1
rac_scene_actor_playback_state_v1(const ActorAnimationClipV1 &clip,
                                 const RacSceneAnimationTickV1 &tick);

// Compiles selected scene actor pose tracks into the existing neutral
// animation resource. The source bank is validated afresh. Ordered bindings
// assign dense clip IDs and pin each clip to the exact rig; offsets, packed
// records and source class IDs never enter ActorAnimationBankV1.
//
// These are skeletal clips, not a complete cutscene: camera samples, external
// root positions, chunk sequencing and audio must be driven by their recovered
// scene owner. In particular, this function does not fabricate a duration or
// assume that normal per-actor advancement supplies the scene clock.
[[nodiscard]] ActorAnimationBankV1 compile_rac_scene_animation_bank_v1(
    std::span<const std::byte> decoded_scene,
    std::span<const RacSceneAnimationActorBindingV1> bindings,
    std::uint32_t source_updates_per_second,
    RacSceneAnimationCompileLimitsV1 limits);

} // namespace openrc
