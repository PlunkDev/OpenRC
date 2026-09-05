#pragma once

#include "openrc/content_api.hpp"
#include "openrc/runtime_level_foundation.hpp"
#include "openrc/runtime_render_scene.hpp"

#include <stdexcept>

namespace openrc::game {

// One source-independent level payload ready to be handed to native gameplay
// and rendering. Additional package resources remain independently mountable;
// combining the mandatory V1 foundation and render scene here prevents each
// frontend from inventing a different compatibility boundary.
struct RuntimeLevelContentV1 {
  RuntimeLevelFoundationV1 foundation;
  RenderSceneV1 render_scene;

  [[nodiscard]] bool operator==(const RuntimeLevelContentV1 &) const = default;
};

struct RuntimeLevelContentLimitsV1 {
  RuntimeLevelFoundationLimitsV1 foundation;
  RenderSceneIoLimitsV1 render_scene;
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
  };
}

// Mounts the mandatory gameplay foundation and neutral render scene from the
// same already-resolved package. The content API policy must be explicit and
// is shared by both resource loaders.
[[nodiscard]] RuntimeLevelContentV1
load_runtime_level_content_v1(const ResolvedLevelPackageV1 &package,
                              RuntimeLevelContentLimitsV1 limits);

} // namespace openrc::game
