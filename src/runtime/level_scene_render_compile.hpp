#pragma once

#include "level_scene_recovery.hpp"

#include "openrc/render_scene.hpp"

#include <stdexcept>

namespace openrc::runtime {

// Compiler policy for translating recovered RAC1 source geometry into the
// source-independent RenderSceneV1 package contract. The scale is explicit
// because LevelSceneRecoveryResultV1 deliberately keeps the SceneBlock
// VITOF0-input coordinate domain neutral.
struct LevelSceneRenderCompileProfileV1 {
    float source_units_per_world_unit = 0.0F;
    RenderSceneLimitsV1 render_scene_limits;

    [[nodiscard]] bool operator==(
        const LevelSceneRenderCompileProfileV1&) const = default;
};

class LevelSceneRenderCompileError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Returns the bounded production profile for the current recovery path. RAC1
// SceneBlock coordinates use 1024 source units per natural world unit.
[[nodiscard]] LevelSceneRenderCompileProfileV1
make_level_scene_render_compile_profile_v1();

// Bakes the recovered terrain, static Moby, and TIE families into at most
// three identity-instanced meshes. Only referenced source textures cross the
// package boundary. Vertex colors remain enabled because the recovery path
// preserves meaningful GS/Moby/TIE tint and alpha alongside RGBA textures.
[[nodiscard]] RenderSceneV1 compile_level_scene_render_v1(
    const LevelSceneRecoveryResultV1& recovered,
    const LevelSceneRenderCompileProfileV1& profile);

} // namespace openrc::runtime
