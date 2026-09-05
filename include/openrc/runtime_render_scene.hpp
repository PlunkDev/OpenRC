#pragma once

#include "openrc/prepared_game_v2.hpp"
#include "openrc/render_scene_io.hpp"

#include <cstdint>
#include <stdexcept>

namespace openrc::game {

class RuntimeRenderSceneError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Mounts the one neutral render-scene resource from an already resolved level
// package. The content API version is an explicit runtime compatibility policy;
// zero never means "accept any version". Unrelated resources are deliberately
// ignored so future native package schemas can be mounted independently.
[[nodiscard]] RenderSceneV1 load_runtime_render_scene_v1(
    const ResolvedLevelPackageV1 &package,
    std::uint32_t required_content_api_version,
    RenderSceneIoLimitsV1 limits);

} // namespace openrc::game
