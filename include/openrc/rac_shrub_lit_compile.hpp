#pragma once

#include "openrc/rac_instance_lighting.hpp"
#include "openrc/rac_shrub_class.hpp"
#include "openrc/render_scene.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>

namespace openrc {
class RacShrubLitCompileError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};

// Compiler-side base geometry for the original full-mesh shrub path. Every
// primitive receives an explicit material binding in source submission order;
// the caller must resolve its original tag state as well as material registers.
// Overlay912339 selects RGB by (normal_and_stop&0x7fff), but takes every
// vertex's alpha from palette[0].w. ST are signed ITOF12 source values;
// the qualified source H=4096 path is required explicitly.
//
// This does not select between full mesh and source billboards. Wind mode&6
// acts on the instance basis (22a608..22a784), so this same base mesh remains
// valid, but its placement must supply the source deformed basis at that tick.
// No wind state or placement transform is generated or silently frozen here.
[[nodiscard]] RenderSceneMeshV1 compile_rac_shrub_lit_base_mesh_v1(
    const RacShrubClassV1& model,
    const RacInstancePaletteV1& initial_palette,
    std::span<const std::uint32_t> primitive_material_ids,
    std::uint32_t mesh_id);
} // namespace openrc
