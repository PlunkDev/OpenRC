#pragma once

#include "openrc/actor_animation.hpp"
#include "openrc/actor_library.hpp"
#include "openrc/game_world.hpp"

namespace openrc {

// Neutral camera basis and frustum. Display aspect is independent of the
// frustum, so authored non-square pixels need no camera reinterpretation.
struct SceneCameraV1 {
  std::array<float, 3> position{};
  std::array<float, 3> right{1,0,0};
  std::array<float, 3> up{0,0,1};
  std::array<float, 3> forward{0,1,0};
  float tangent_half_horizontal = 0;
  float tangent_half_vertical = 0;
  float near_plane = 0;
  float far_plane = 0;
  bool operator==(const SceneCameraV1 &) const = default;
};

struct SceneTimelineActorV1 {
  // Dense indices are local to the two exact resources pinned below.
  std::uint32_t model_index = 0;
  std::uint32_t rig_index = 0;
  ActorAffineTransformV1 model_to_entity;
  bool operator==(const SceneTimelineActorV1 &) const = default;
};

struct SceneTimelineActorSampleV1 {
  std::uint32_t clip_index = 0;
  std::uint32_t frame_index = 0;
  float phase = 0;
  game::WorldTransformV1 transform;
  bool enabled = true;
  bool operator==(const SceneTimelineActorSampleV1 &) const = default;
};

struct SceneTimelineSampleV1 {
  SceneCameraV1 camera;
  std::vector<SceneTimelineActorSampleV1> actors;
  bool operator==(const SceneTimelineSampleV1 &) const = default;
};

struct SceneTimelineV1 {
  PreparedContentDigestV1 actor_library_sha256{};
  PreparedContentDigestV1 actor_animation_sha256{};
  std::uint32_t updates_per_second = 0;
  std::uint32_t display_aspect_numerator = 0;
  std::uint32_t display_aspect_denominator = 0;
  std::array<float, 4> clear_color{0,0,0,1};
  // A non-looping sequence holds its final sample. The caller owns the
  // following transition. There are no source chunk counters in this schema.
  bool loop = false;
  std::vector<SceneTimelineActorV1> actors;
  std::vector<SceneTimelineSampleV1> samples;
  bool operator==(const SceneTimelineV1 &) const = default;
};

struct SceneTimelineLimitsV1 {
  std::uint64_t max_bytes = 64U*1024U*1024U;
  std::uint32_t max_actors = 256;
  std::uint32_t max_samples = 100'000;
  std::uint64_t max_actor_samples = 1'000'000;
  float max_absolute_component = 1.e6F;
};

class SceneTimelineError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void validate_scene_camera_v1(const SceneCameraV1 &camera,
                              float max_absolute_component = 1.e6F);
void validate_scene_timeline_v1(const SceneTimelineV1 &timeline,
                                SceneTimelineLimitsV1 limits = {});
// Verify dense bindings, rig content and every sampled frame before admission.
void validate_scene_timeline_bindings_v1(const SceneTimelineV1 &timeline,
    const ActorLibraryV1 &library, const ActorAnimationBankV1 &animation);
[[nodiscard]] std::vector<std::byte> encode_scene_timeline_v1(
    const SceneTimelineV1 &timeline, SceneTimelineLimitsV1 limits = {});
[[nodiscard]] SceneTimelineV1 decode_scene_timeline_v1(
    std::span<const std::byte> bytes, SceneTimelineLimitsV1 limits = {});
[[nodiscard]] std::size_t scene_timeline_sample_index_v1(
    const SceneTimelineV1 &timeline, std::uint64_t elapsed_updates);

} // namespace openrc
