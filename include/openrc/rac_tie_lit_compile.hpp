#pragma once

#include "openrc/rac_instance_lighting.hpp"
#include "openrc/rac_tie_class.hpp"
#include "openrc/render_scene.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>

namespace openrc {

class RacTieLitCompileError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct RacTieLodSelectionV1 {
    std::array<std::uint32_t,3U> world_center_bits{};
    std::uint32_t camera_depth_bits = 0U;
    std::uint8_t selected_lod = 0U;
    // Original VF27: {t,0,256-256*t,256*t}; fixed endpoints use t=0.
    std::array<std::uint32_t,4U> morph_weight_bits{0U,0U,0x43800000U,0U};
};

// Executes 2375a0/237628..2377e4 sign-bit gates on the original nonnegative
// camera depth. Equality belongs to the nearer table, including its t=1
// transition endpoint. Portal/frustum admission is a separate source owner.
[[nodiscard]] RacTieLodSelectionV1 select_rac_tie_lod_v1(
    std::array<std::uint32_t,3U> source_threshold_bits,
    std::uint32_t source_camera_depth_bits);

// Original loader 1ea41c/1ea480/1ea490 transforms the class bounding center;
// 23724c..237308 then computes max(0,view Z). Inputs are the actual original
// camera position and source view columns, never a host-reconstructed camera.
[[nodiscard]] RacTieLodSelectionV1 evaluate_rac_tie_instance_lod_v1(
    const RacTieClassV1& model,
    const RacGameplayTieInstanceV1& instance,
    const std::array<std::uint32_t,3U>& camera_position_bits,
    const std::array<std::array<std::uint32_t,4U>,4U>& source_view_columns);

// Lower an explicitly selected source table, preserving original fat-vertex
// MUL/ADD position morphs and VU 224979 ordered ACC color arithmetic. Wind
// modes act on placement basis separately. All candidate geometry is emitted;
// this function does not claim portal/frustum admission. Positions are rebuilt
// from signed source integers through source MUL even for fixed/dinky vertices.
// The qualified neutral UV path requires the authored Q lane to equal 4096.
[[nodiscard]] RenderSceneMeshV1 compile_rac_tie_lit_mesh_v1(
    const RacTieClassV1& selected_model,
    const RacInstancePaletteV1& initial_palette,
    const RacTieLodSelectionV1& selection,
    std::span<const std::uint32_t> material_ids,
    std::uint32_t mesh_id);

// Compiler-side lowering of one placed TIE's qualified initial palette into
// neutral model-local geometry. This is explicitly LOD0 at morph factor zero;
// it does not select the original camera-dependent LOD. EE 237770..2377a4
// supplies {0,0,256,0}; VU 224979 selects C0 for both dinky and fat vertices.
// Triangle and primitive order are retained. Material IDs must correspond to
// every class-local slot and use the original encoded GS modulation contract.
[[nodiscard]] RenderSceneMeshV1 compile_rac_tie_lit_lod0_mesh_v1(
    const RacTieClassV1& model,
    const RacInstancePaletteV1& initial_palette,
    std::span<const std::uint32_t> material_ids,
    std::uint32_t mesh_id);

} // namespace openrc
