#pragma once

#include "openrc/rac_level_moby_assets.hpp"
#include "openrc/render_scene.hpp"
#include "openrc/scene_block_directory.hpp"
#include "openrc/dvp_vu.hpp"

namespace openrc {

struct RacFrontendEnvironmentClassV1 {
  std::uint32_t class_id = 0U;
  std::array<std::uint8_t, 16U> texture_slots{};
  std::array<std::uint8_t, 16U> secondary_texture_slots{};
  std::uint8_t used_texture_slot_count = 0U;
  std::uint64_t source_offset = 0U;
  std::vector<std::byte> source_bytes;
};

struct RacFrontendEnvironmentLimitsV1 {
  std::uint64_t max_source_bytes = UINT64_C(64)*1024U*1024U;
  std::uint32_t max_class_entries = 4096U;
  std::uint32_t max_gs_uploads = 4096U;
  RacGameplayBankLimitsV1 gameplay{UINT64_C(64)*1024U*1024U};
  RacLevelMobyTextureLimitsV1 textures{
      UINT64_C(64)*1024U*1024U, UINT64_C(64)*1024U*1024U,
      UINT64_C(64)*1024U*1024U, 255U, 4096U, 4096U,
      16000000U, 16000000U, UINT64_C(64)*1024U*1024U};
  RacTieClassLimitsV1 tie_class{
      UINT64_C(64)*1024U*1024U, 4096U, 65536U, 3000000U,
      3000000U, 3000000U, 16U};
  SceneBlockDirectoryLimits terrain{
      UINT64_C(64)*1024U*1024U, 4096U, UINT64_C(64)*1024U*1024U};
};

// Compiler-only relocation and original payload ownership. No source bytes,
// source class IDs or GS indices are serialized in neutral resources.
struct RacFrontendEnvironmentAssetsV1 {
  RacGameplayEnvironmentV1 gameplay;
  std::vector<std::byte> gameplay_source;
  SceneBlockDirectoryV1 terrain;
  RacLevelMobyTextureBankV1 terrain_textures;
  RacLevelMobyTextureBankV1 tie_textures;
  RacLevelMobyTextureBankV1 shrub_textures;
  std::vector<RacLevelTieModelV1> tie_models;
  // Matched class envelopes retain source normals/ADGIF/color-index streams
  // required by the original lighting consumer after geometry decoding.
  std::vector<RacFrontendEnvironmentClassV1> tie_classes;
  std::vector<RacFrontendEnvironmentClassV1> shrub_classes;
  std::vector<std::byte> sky_source;
};

class RacFrontendEnvironmentError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

[[nodiscard]] RacFrontendEnvironmentAssetsV1
decode_rac_frontend_environment_assets_v1(
    std::span<const std::byte> decoded_frontend_wad,
    RacFrontendEnvironmentLimitsV1 limits = {});

// Compiler-only live fog state built by 1e9ec8 -> 1f2930 -> 1f3140.
// Distances use original 1/1024 world units. These raw coefficients retain
// COP1 operation order; they are not a neutral renderer fog approximation.
struct RacFrontendFogV1 {
  std::uint32_t rgb8 = 0U;
  std::uint32_t near_distance_bits = 0U;
  std::uint32_t far_distance_bits = 0U;
  std::uint32_t near_factor_bits = 0U;
  std::uint32_t far_factor_bits = 0U;
  std::uint32_t world_gradient_bits = 0U; // 18ce00+220
  std::uint32_t world_offset_bits = 0U; // 18ce00+224
  std::uint32_t projection_scale_bits = 0U; // 18ce00+210, seed VF1.x
  std::uint32_t projection_offset_bits = 0U; // 18ce00+214, seed VF7.w
  std::uint32_t depth_to_w_bits = 0U; // projection matrix +ec
};

[[nodiscard]] RacFrontendFogV1 compile_rac_frontend_fog_v1(
    std::span<const std::byte> gameplay_source);

// The terrain VU emitter adds VF7.w to its already transformed W, clamps
// against VF1.y/z, then FTOI4. XYZF2 takes bits4..11 of that packed lane.
// Callers must supply the original source matrix's W, not neutral clip W.
[[nodiscard]] std::uint8_t evaluate_rac_frontend_terrain_fog_v1(
    const RacFrontendFogV1 &fog, std::uint32_t source_projected_w_bits);

struct RacFrontendSkySpriteSourceV1 {
  DvpVuProgramV1 trigonometry;
  std::array<std::uint32_t,4U> atan_coefficients0{};
  std::array<std::uint32_t,4U> atan_coefficients1{};
  std::array<std::uint32_t,4U> atan_positive_quadrants{};
  std::array<std::uint32_t,4U> billboard_axes{};
};
struct RacFrontendSkySpriteV1 {
  bool stationary = false;
  std::uint16_t phase0 = 0U;
  std::uint16_t phase1 = 0U;
  std::uint32_t rotation_bits = 0U;
  std::uint32_t size_bits = 0U;
  std::uint32_t base_rgba8 = 0U;
  std::uint32_t rgba8 = 0U;
  std::array<std::uint32_t,3U> position_bits{};
};
struct RacFrontendSkySpritesV1 {
  std::array<RacFrontendSkySpriteV1,256U> sprites{};
  // Shared source RNG: incorporate any intervening consumers here before
  // stepping. Advancing this owner alone does not execute the full clock.
  std::uint32_t random_state = 0U;
  std::uint32_t update_count = 0U;
};
[[nodiscard]] RacFrontendSkySpriteSourceV1 make_rac_frontend_sky_sprite_source_v1(
    std::span<const std::byte> resident_elf);
// 22c188 initializes with seed12345, then immediately performs update1.
[[nodiscard]] RacFrontendSkySpritesV1 initialize_rac_frontend_sky_sprites_v1(
    const RacFrontendSkySpriteSourceV1 &source, std::span<const std::byte> sky);
void step_rac_frontend_sky_sprites_v1(const RacFrontendSkySpriteSourceV1 &source,
    RacFrontendSkySpritesV1 &state);

struct RacFrontendSkyBillboardV1 {
  std::uint16_t sprite_ordinal = 0U;
  std::uint32_t rgba8 = 0U;
  // Original packed GS coordinates, including the drawing-environment
  // origin. Four strip vertices have ST=(0,0),(1,0),(0,1),(1,1), Q=1.
  std::array<std::array<std::uint16_t,2U>,4U> xy16{};
  std::uint32_t z24 = 0U;
  std::uint8_t fog = 0U;
  std::uint32_t size_q_bits = 0U;
  std::uint32_t projection_q_bits = 0U;
};
struct RacFrontendSkyBillboardFrameV1 {
  std::array<std::uint32_t,2U> tangent_bits{};
  std::array<std::uint32_t,2U> frustum_bits{};
  std::vector<RacFrontendSkyBillboardV1> billboards;
};
// Compiler lowering of22ceb8/221571 for the original PAL scenic projection.
// Input is the original rotation-only source view, not a neutral view matrix.
// Output retains logical post-E GS vertex values, not a PATH1 timing model.
// Material owner is texture1, modulate/128, additive/128, no fog or depth
// writes; integration and the shared RNG clock remain separate contracts.
[[nodiscard]] RacFrontendSkyBillboardFrameV1 sample_rac_frontend_sky_billboards_v1(
    const RacFrontendSkySpriteSourceV1 &source,
    const RacFrontendSkySpritesV1 &sprites,
    const std::array<std::array<std::uint32_t,4U>,4U> &source_view_columns,
    const RacFrontendFogV1 &fog);

struct RacFrontendTerrainCompileResultV1 {
  RenderSceneV1 render_scene;
  std::uint32_t executed_records = 0U;
  std::uint64_t source_vertices = 0U;
  std::uint64_t source_triangles = 0U;
  std::uint32_t opaque_materials = 0U;
  std::uint64_t opaque_triangles = 0U;
};

// Executes the resident source terrain program through the existing bounded
// SceneBlock pipeline, then lowers recovered pre-projection XYZ/STQ/RGBA into
// the existing RenderScene compiler. Every record must complete normally.
[[nodiscard]] RacFrontendTerrainCompileResultV1
compile_rac_frontend_terrain_v1(
    const RacFrontendEnvironmentAssetsV1 &assets,
    std::span<const std::byte> resident_elf,
    RenderSceneLimitsV1 render_limits);

struct RacFrontendSkyCompileResultV1 {
  RenderSceneV1 render_scene;
  std::uint32_t shell_count = 0U;
  std::uint32_t cluster_count = 0U;
  std::uint64_t source_vertices = 0U;
  std::uint64_t source_triangles = 0U;
};

// Original 22c188 shell order 0,1,2,3; 22cc40/22ca00/22d3f8/22d520
// geometry and the resident packets at 13d1f0/13d260. This compiles the four
// authored shells only. The separate 256 procedurally updated sky sprites
// drawn between shells 1 and 2 are not part of this static resource.
[[nodiscard]] RacFrontendSkyCompileResultV1
compile_rac_frontend_sky_shells_v1(
    std::span<const std::byte> source_sky,
    RenderSceneLimitsV1 render_limits);

} // namespace openrc
