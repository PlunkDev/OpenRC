#pragma once

#include "openrc/rac_actor_library_compile.hpp"
#include "openrc/rac_scene_animation_compile.hpp"
#include "openrc/actor_animation_io.hpp"
#include "openrc/actor_library_io.hpp"
#include "openrc/scene_timeline.hpp"

namespace openrc {

struct RacFrontendSceneCompileLimitsV1 {
  std::uint64_t max_source_bytes = 0U;
  std::uint32_t max_asset_directory_entries = 0U;
  std::uint32_t max_gs_uploads = 0U;
  std::uint64_t max_decoded_scene_bytes = 0U;
  RacMobyClassLimitsV1 moby_class;
  RacMobyBindPoseLimitsV1 bind_pose;
  RacLevelMobyTextureLimitsV1 textures;
  ActorLibraryLimitsV1 actors;
  // Animation counts/bytes are aggregate over every chunk, not per chunk.
  RacSceneAnimationCompileLimitsV1 animation;
};

struct RacFrontendSceneActorV1 {
  std::uint32_t source_actor_index = 0U;
  std::uint32_t source_class_id = 0U;
  std::string rig_key;
  std::string model_key;
};

// Compiler-only result: the two neutral resources are ready for the existing
// codecs/player/renderer. Original chunk bytes and source identities remain
// here solely so the compiler can lower its recovered owner into a neutral
// timeline. Clip IDs are chunk-major, then source actor ordinal.
struct RacFrontendSceneCompileResultV1 {
  ActorLibraryV1 actor_library;
  ActorAnimationBankV1 actor_animation;
  RacFrontendBackgroundChunksV1 source_background;
  std::vector<RacFrontendSceneActorV1> actors;
  std::vector<std::vector<std::uint32_t>> chunk_actor_clip_ids;
};

class RacFrontendSceneCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Original 1eabe8/203958/203e78 source class/texture binding, followed by the
// existing bind-pose, actor-library and scene-animation compilers. Every actor
// in the first background chunk must have the same identity in later chunks.
// The non-scenic menu objects are not inserted into these background actors.
// No special-material fallback or source-format runtime payload is emitted.
[[nodiscard]] RacFrontendSceneCompileResultV1 compile_rac_frontend_scene_v1(
    std::span<const std::byte> decoded_frontend_wad,
    std::uint32_t source_updates_per_second,
    RacFrontendSceneCompileLimitsV1 limits);

struct RacFrontendTimelineCompileLimitsV1 {
  ActorLibraryIoLimitsV1 actors;
  ActorAnimationIoLimitsV1 animation;
  SceneAnimationBankLimitsV1 scene;
  SceneTimelineLimitsV1 timeline;
};

// Lowers the actual frontend owner clock into one looping neutral timeline.
// Camera basis/frustum and logical display raster are outputs of the separately
// qualified source projection/camera owner. This compositor verifies that all
// source camera records are constant and that position/horizontal tangent
// match the supplied camera; it does not infer a camera or display aspect.
// Hashes pin the exact canonical actor/animation codec payloads. World scale
// is one because the source class scale is already included by the baker.
[[nodiscard]] SceneTimelineV1 compile_rac_frontend_timeline_v1(
    const RacFrontendSceneCompileResultV1 &scene,
    const SceneCameraV1 &qualified_source_camera,
    std::uint32_t display_raster_width, std::uint32_t display_raster_height,
    RacFrontendTimelineCompileLimitsV1 limits);

// The reached class1138 menu-object joint extractor (211808), before consumer
// 20db98 applies class scale, object rotation and object position. Its admitted
// domain is the original five-joint star, fixed root Z half-turn / identity
// children, adjacent frames of one sequence or its exact last->0 endpoints,
// all sparse i16 translations and
// no local scale. Terminal root scales do not enter this extractor. The
// returned five translations are indexed by source joint, not by the four
// consumer selectors: 211548 resolves each selector through model metadata
// to the final joint in its path. Coordinates are source joint units.
// This bounded integer/dyadic path proves the actual VMULA/VMADD results;
// changing rotations, transition snapshots and arbitrary phases are rejected.
[[nodiscard]] std::array<std::array<float, 3U>, 5U>
sample_rac_frontend_menu_joint_translations_v1(
    const RacRatchetSequenceV1 &sequence, const RacMobyBindRigV1 &bind_rig,
    std::uint32_t current_frame, std::uint32_t next_frame, float phase,
    RacRatchetPoseLimitsV1 limits);

} // namespace openrc
