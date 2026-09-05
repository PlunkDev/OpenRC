#pragma once

#include "openrc/actor_behavior_scene_io.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <span>
#include <stdexcept>
#include <string_view>

namespace openrc {

inline constexpr std::string_view kLevelActorBehaviorSceneCompilePassV1 =
    "compiler/openrc/actor-behavior-scene-v1";

class LevelActorBehaviorSceneCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Encodes one canonical, source-independent actor-behavior scene and attaches
// it to a base package. Direct source provenance is mandatory; the generated
// compiler provenance is appended here and cannot be supplied by the caller.
[[nodiscard]] LevelPackageV1 attach_actor_behavior_scene_to_level_package_v1(
    LevelPackageV1 base_package, const ActorBehaviorSceneV1 &scene,
    std::span<const LevelPackageProvenanceV1> source_provenance,
    ActorBehaviorSceneIoLimitsV1 actor_behavior_io_limits,
    LevelPackageV1Limits package_limits);

} // namespace openrc
