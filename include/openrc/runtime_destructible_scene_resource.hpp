#pragma once

#include "openrc/destructible_scene_io.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace openrc::game {

class RuntimeDestructibleSceneResourceError final
    : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Mounts an optional neutral destructible scene. Absence is compatible with
// older packages. Presence makes type/schema, resolved operation, digest,
// payload bounds, canonical body, and package level identity strict.
[[nodiscard]] std::optional<DestructibleSceneV1>
load_optional_runtime_destructible_scene_resource_v1(
    const ResolvedLevelPackageV1 &package,
    std::uint32_t required_content_api_version,
    DestructibleSceneIoLimitsV1 limits);

} // namespace openrc::game
