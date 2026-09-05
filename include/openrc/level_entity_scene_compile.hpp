#pragma once

#include "openrc/entity_scene_io.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <span>
#include <stdexcept>
#include <string_view>

namespace openrc {

// Stable logical compiler-pass identity. This is provenance, not a host
// executable name or version-dependent build path.
inline constexpr std::string_view kLevelEntitySceneCompilePassV1 =
    "compiler/openrc/entity-scene-v1";

class LevelEntitySceneCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Encodes one neutral EntitySceneV1 and attaches it to an existing canonical
// base package for the same level. Every supplied record must identify real
// source bytes (iso_range or prepared_resource); the compiler-pass record is
// added here. The returned package and entity payload have both survived
// canonical parsing.
[[nodiscard]] LevelPackageV1 attach_entity_scene_to_level_package_v1(
    LevelPackageV1 base_package, const EntitySceneV1 &scene,
    std::span<const LevelPackageProvenanceV1> source_provenance,
    EntitySceneIoLimitsV1 entity_scene_limits,
    LevelPackageV1Limits package_limits);

} // namespace openrc
