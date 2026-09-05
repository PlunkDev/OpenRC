#pragma once

#include "moby_scene_geometry.hpp"
#include "scene_geometry.hpp"
#include "tie_scene_geometry.hpp"

#include "openrc/rac_level_moby_assets.hpp"
#include "openrc/scene_block_runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace openrc::runtime {

enum class LevelSceneRecordSelectionV1 : std::uint8_t {
    single_record,
    all_records,
};

// Compiler-side inputs used to recover one native scene from the original
// game data. The caller owns all source-selection policy; this adapter never
// searches for an ISO, executable, level, or SceneBlock entrypoint.
struct LevelSceneRecoveryRequestV1 {
    std::filesystem::path disc_image;
    std::filesystem::path boot_executable;
    std::uint32_t level_id = 0U;
    LevelSceneRecordSelectionV1 record_selection =
        LevelSceneRecordSelectionV1::single_record;
    std::uint64_t record_index = 0U;
    std::uint16_t entrypoint = 16U;
};

// Every parser, executor, intermediate batch, and final merge is bounded by
// an explicit caller-provided limit. No recovery limit is read from window or
// renderer state.
struct LevelSceneRecoveryLimitsV1 {
    SceneBlockRuntimeLoadLimitsV1 scene_block_load;
    SceneBlockRuntimeExecutionLimitsV1 scene_block_execution;
    RacLevelMobyAssetLimitsV1 level_assets;
    SceneGeometryMergeLimitsV1 aggregate_geometry;
    MobySceneGeometryLimitsV1 moby_geometry;
    TieSceneGeometryLimitsV1 tie_geometry;
    std::uint64_t max_aggregate_records = 0U;
};

// Execution inputs which describe the recovered SceneBlock frame and the
// coordinate domains used when attaching static environment geometry.
struct LevelSceneRecoveryProfileV1 {
    SceneBlockTaskFrameInputV1 frame_input;
    MobySceneCoordinateDomainV1 moby_coordinate_domain =
        MobySceneCoordinateDomainV1::world_units;
    TieSceneCoordinateDomainV1 tie_coordinate_domain =
        TieSceneCoordinateDomainV1::world_units;
    // Strictly ascending compiler policy. These source classes are omitted
    // from the flattened Moby family so independently addressable entity
    // resources can own their presentation without double-rendering.
    std::vector<std::uint32_t> excluded_moby_class_ids;
};

struct LevelSceneRecoveryResultV1 {
    SceneGeometryV1 raster;
    std::optional<SceneGeometry3dV1> source;
    std::optional<RacLevelMobyTextureBankV1> tfrag_texture_bank;
    std::vector<SceneMaterialBatchV1> terrain_material_batches;
    std::uint64_t terrain_triangle_count = 0U;
    std::optional<RacLevelMobyTextureBankV1> moby_texture_bank;
    std::vector<MobySceneMaterialBatchV1> moby_material_batches;
    std::optional<std::uint64_t> moby_first_triangle;
    std::optional<RacLevelMobyTextureBankV1> tie_texture_bank;
    std::vector<MobySceneMaterialBatchV1> tie_material_batches;
    std::optional<std::uint64_t> tie_first_triangle;
    std::uint64_t total_record_count = 0U;
    std::uint64_t decoded_record_count = 0U;
    std::uint64_t raster_record_count = 0U;
    std::uint64_t source_record_count = 0U;
    std::uint64_t no_event_record_count = 0U;
    std::uint64_t incomplete_stream_record_count = 0U;
    std::uint64_t unavailable_source_record_count = 0U;
    std::uint64_t terrain_textured_triangle_count = 0U;
    std::uint64_t terrain_unresolved_material_triangle_count = 0U;
    std::uint64_t terrain_single_context_triangle_count = 0U;
    std::uint64_t terrain_vertices_with_stq = 0U;
    std::uint64_t terrain_vertices_without_stq = 0U;
    std::uint64_t moby_model_count = 0U;
    std::uint64_t moby_rendered_model_count = 0U;
    std::uint64_t moby_placement_count = 0U;
    std::uint64_t moby_excluded_placement_count = 0U;
    std::uint64_t moby_rendered_placement_count = 0U;
    std::uint64_t moby_animated_placement_count = 0U;
    std::uint64_t moby_missing_or_empty_placement_count = 0U;
    std::uint64_t moby_triangle_count = 0U;
    std::uint64_t tie_model_count = 0U;
    std::uint64_t tie_rendered_model_count = 0U;
    std::uint64_t tie_placement_count = 0U;
    std::uint64_t tie_rendered_placement_count = 0U;
    std::uint64_t tie_missing_or_empty_placement_count = 0U;
    std::uint64_t tie_triangle_count = 0U;
};

// Returns the exact bounded profile formerly embedded in windows_main.cpp.
[[nodiscard]] LevelSceneRecoveryLimitsV1
make_level_scene_recovery_limits_v1();

// Returns the identity SceneBlock frame and source-coordinate domains used by
// the current native viewer.
[[nodiscard]] LevelSceneRecoveryProfileV1
make_level_scene_recovery_profile_v1();

// Performs validation which is independent of the source files. This is also
// useful to compiler frontends that want to reject malformed jobs before I/O.
void validate_level_scene_recovery_request_v1(
    const LevelSceneRecoveryRequestV1& request,
    const LevelSceneRecoveryLimitsV1& limits,
    const LevelSceneRecoveryProfileV1& profile);

// Reads the explicitly selected original inputs, reconstructs terrain and GS
// geometry, and for an all-record entry-16 request attaches static Moby/TIE
// geometry. The returned value contains no Windows or D3D objects.
[[nodiscard]] LevelSceneRecoveryResultV1 recover_level_scene_v1(
    const LevelSceneRecoveryRequestV1& request,
    const LevelSceneRecoveryLimitsV1& limits,
    const LevelSceneRecoveryProfileV1& profile);

} // namespace openrc::runtime
