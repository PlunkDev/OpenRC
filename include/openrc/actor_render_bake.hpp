#pragma once

#include "openrc/actor_library.hpp"
#include "openrc/actor_pose.hpp"
#include "openrc/render_scene.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

struct ActorRenderBakeResultV1 {
  RenderSceneV1 scene;

  // Parallel to the caller's transform span. Each value is the dense
  // RenderSceneInstanceV1 ID assigned to that transform.
  std::vector<std::uint32_t> instance_ids;

  [[nodiscard]] bool
  operator==(const ActorRenderBakeResultV1 &) const = default;
};

class ActorRenderBakeError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Appends one neutral actor model, frozen in its authored bind pose, to an
// already canonical static scene. All source meshes are merged into one new
// RenderSceneMeshV1 while their vertex/index order and draw partitions remain
// intact. The supplied transforms become independent instances of that mesh;
// vertices are never pre-transformed by an instance matrix.
//
// Both inputs are validated as canonical under their explicit limits. An
// empty transform span is a validated no-op. Otherwise the result is fully
// canonical, existing scene resources are preserved exactly, and
// instance_ids is parallel to instance_transforms.
[[nodiscard]] ActorRenderBakeResultV1 bake_actor_bind_pose_to_render_scene_v1(
    const RenderSceneV1 &base_scene, const ActorLibraryV1 &actor_library,
    std::string_view model_semantic_key,
    std::span<const RenderSceneAffine3x4V1> instance_transforms,
    ActorLibraryLimitsV1 actor_library_limits,
    ActorPoseLimitsV1 actor_pose_limits,
    RenderSceneLimitsV1 render_scene_limits);

} // namespace openrc
