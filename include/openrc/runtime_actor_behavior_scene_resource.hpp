#pragma once

#include "openrc/actor_behavior_scene_io.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace openrc::game {

class RuntimeActorBehaviorSceneResourceError final
    : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Mounts an optional neutral actor-behavior scene. Absence is compatible with
// older packages. Presence makes type/schema, resolved operation, digest,
// payload bounds, canonical body, and package level identity strict.
[[nodiscard]] std::optional<ActorBehaviorSceneV1>
load_optional_runtime_actor_behavior_scene_resource_v1(
    const ResolvedLevelPackageV1 &package,
    std::uint32_t required_content_api_version,
    ActorBehaviorSceneIoLimitsV1 limits);

} // namespace openrc::game
