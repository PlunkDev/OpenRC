#pragma once

#include "openrc/destructible_scene_io.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <span>
#include <stdexcept>
#include <string_view>

namespace openrc {

inline constexpr std::string_view kLevelDestructibleSceneCompilePassV1 =
    "compiler/openrc/destructible-scene-v1";

class LevelDestructibleSceneCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Encodes one canonical, source-independent destructible scene and attaches
// it to a base package. Direct source provenance is mandatory; the generated
// compiler provenance is appended here and cannot be supplied by the caller.
[[nodiscard]] LevelPackageV1 attach_destructible_scene_to_level_package_v1(
    LevelPackageV1 base_package, const DestructibleSceneV1 &scene,
    std::span<const LevelPackageProvenanceV1> source_provenance,
    DestructibleSceneIoLimitsV1 destructible_io_limits,
    LevelPackageV1Limits package_limits);

} // namespace openrc
