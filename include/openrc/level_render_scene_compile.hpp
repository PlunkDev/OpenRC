#pragma once

#include "openrc/prepared_game_v2.hpp"
#include "openrc/render_scene_io.hpp"

#include <span>
#include <stdexcept>
#include <string_view>

namespace openrc {

// Stable logical compiler-pass identity. This is provenance, not a host
// executable name or version-dependent build path.
inline constexpr std::string_view kLevelRenderSceneCompilePassV1 =
    "compiler/openrc/render-scene-v1";

class LevelRenderSceneCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Encodes one neutral RenderSceneV1 and attaches it to an existing canonical
// base package. Every supplied record must identify real source bytes
// (iso_range or prepared_resource); the compiler-pass record is added here.
// The returned package is re-encoded and parsed so callers receive canonical
// resource/provenance order and verified payload digests.
[[nodiscard]] LevelPackageV1 attach_render_scene_to_level_package_v1(
    LevelPackageV1 base_package, const RenderSceneV1 &scene,
    std::span<const LevelPackageProvenanceV1> source_provenance,
    RenderSceneIoLimitsV1 render_scene_limits,
    LevelPackageV1Limits package_limits);

} // namespace openrc
