#pragma once

#include "openrc/actor_pose.hpp"
#include "openrc/content_api.hpp"
#include "openrc/runtime_actor_library.hpp"
#include "openrc/runtime_entity_scene.hpp"
#include "openrc/runtime_level_foundation.hpp"
#include "openrc/runtime_render_scene.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace openrc::game {

// Stable presentation limits shared by the package-mount preflight and the
// shipped renderer. ActorPoseV1 transforms normals through an inverse-transpose
// matrix, so the determinant and normal floors are part of the compatibility
// boundary rather than renderer-private implementation details.
inline constexpr std::uint32_t kRuntimePlayerActorMaximumJointsV1 = 1024U;
inline constexpr std::uint64_t kRuntimePlayerActorMaximumVerticesV1 =
    1'000'000U;
inline constexpr std::uint64_t kRuntimePlayerActorMaximumTriangleIndicesV1 =
    3'000'000U;
inline constexpr std::uint64_t kRuntimePlayerActorMaximumDrawRangesV1 =
    65'536U;
inline constexpr double
    kRuntimePlayerActorMinimumAbsoluteLinearDeterminantV1 = 1.0e-8;
inline constexpr double kRuntimePlayerActorMinimumNormalLengthV1 = 1.0e-8;
inline constexpr ActorPoseLimitsV1 kRuntimePlayerActorPoseLimitsV1{
    kRuntimePlayerActorMaximumJointsV1,
    kRuntimePlayerActorMaximumVerticesV1,
    kRuntimePlayerActorMinimumAbsoluteLinearDeterminantV1,
    kRuntimePlayerActorMinimumNormalLengthV1,
};

// One source-independent level payload ready to be handed to native gameplay
// and rendering. Additional package resources remain independently mountable;
// combining the mandatory V1 foundation and render scene here prevents each
// frontend from inventing a different compatibility boundary.
struct RuntimeLevelContentV1 {
  RuntimeLevelFoundationV1 foundation;
  RenderSceneV1 render_scene;
  std::optional<ActorLibraryV1> actor_library;
  std::optional<EntitySceneV1> entity_scene;

  [[nodiscard]] bool operator==(const RuntimeLevelContentV1 &) const = default;
};

struct RuntimeLevelContentLimitsV1 {
  RuntimeLevelFoundationLimitsV1 foundation;
  RenderSceneIoLimitsV1 render_scene;
  ActorLibraryIoLimitsV1 actor_library;
  EntitySceneIoLimitsV1 entity_scene;
};

class RuntimeLevelContentError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Stable source-independent compatibility and allocation policy used by the
// shipped runtime and its package smoke tools. Asset compilers may use tighter
// construction limits without narrowing what a compatible mod can provide.
[[nodiscard]] constexpr RuntimeLevelContentLimitsV1
make_runtime_level_content_limits_v1() {
  return RuntimeLevelContentLimitsV1{
      RuntimeLevelFoundationLimitsV1{
          kOpenRcContentApiVersionV1,
          CollisionWorldIoLimitsV1{
              UINT64_C(32) * 1024U * 1024U,
              CollisionWorldBuildLimitsV1{
                  1'000'000U,
                  2'000'000U,
                  1'000'000U,
                  16'000'000U,
                  kCollisionDefaultGridCellSizeQ6V1,
              },
          },
          LevelBootstrapV1Limits{64U * 1024U, 1024U},
      },
      RenderSceneIoLimitsV1{
          UINT64_C(512) * 1024U * 1024U,
          RenderSceneLimitsV1{
              4096U,
              16U,
              65'536U,
              4097U,
              65'536U,
              3'000'000U,
              65'536U,
              4096U,
              4096U,
              UINT64_C(16) * 1024U * 1024U,
              3'000'000U,
              9'000'000U,
              UINT64_C(256) * 1024U * 1024U,
          },
      },
      ActorLibraryIoLimitsV1{
          UINT64_C(512) * 1024U * 1024U,
          ActorLibraryLimitsV1{
              4096U,
              65'536U,
              256U,
              UINT64_C(64) * 1024U * 1024U,
              1024U,
              1'000'000U,
              65'536U,
              16U,
              262'144U,
              65'536U,
              65'536U,
              3'000'000U,
              3'000'000U,
              9'000'000U,
              4096U,
              4096U,
              UINT64_C(16) * 1024U * 1024U,
              UINT64_C(256) * 1024U * 1024U,
          },
      },
      EntitySceneIoLimitsV1{
          UINT64_C(256) * 1024U * 1024U,
          EntitySceneLimitsV1{
              1'000'000U,
              1'000'000U,
              1'000'000U,
              1'000'000U,
              16U,
              256U,
              256U,
              UINT64_C(128) * 1024U * 1024U,
          },
      },
  };
}

// Mounts the mandatory gameplay foundation and neutral render scene from the
// same already-resolved package. ActorLibraryV1 and EntitySceneV1 are an
// optional feature pair: old packages may omit both, but a package may never
// expose only half of the actor/entity contract. The content API policy is
// explicit and shared by all resource loaders.
[[nodiscard]] RuntimeLevelContentV1
load_runtime_level_content_v1(const ResolvedLevelPackageV1 &package,
                              RuntimeLevelContentLimitsV1 limits);

} // namespace openrc::game
