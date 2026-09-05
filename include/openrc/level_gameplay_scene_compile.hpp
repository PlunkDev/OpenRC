#pragma once

#include "openrc/gameplay_scene_io.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <span>
#include <stdexcept>
#include <string_view>

namespace openrc {

inline constexpr std::string_view kLevelGameplaySceneCompilePassV1 =
    "compiler/openrc/gameplay-scene-v1";

class LevelGameplaySceneCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Encodes one canonical, source-independent gameplay scene and attaches it to
// a base level package. Source provenance may name original byte ranges or
// prepared resources from earlier compiler passes; generated provenance is
// added here and cannot be supplied by the caller.
[[nodiscard]] LevelPackageV1 attach_gameplay_scene_to_level_package_v1(
    LevelPackageV1 base_package, const GameplaySceneV1 &scene,
    std::span<const LevelPackageProvenanceV1> source_provenance,
    GameplaySceneIoLimitsV1 gameplay_io_limits,
    LevelPackageV1Limits package_limits);

} // namespace openrc
