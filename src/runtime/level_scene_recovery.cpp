#include "level_scene_recovery.hpp"

#include "openrc/dvp_vu.hpp"
#include "openrc/dvp_vu_execute.hpp"
#include "openrc/scene_block_task.hpp"
#include "openrc/scene_block_task_execute.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace openrc::runtime {
namespace {

constexpr std::uint64_t kMaximumRuntimeBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumAggregateRecords = 4096U;
constexpr std::uint64_t kMaximumAggregateVertices = 3'000'000U;
constexpr std::uint64_t kMaximumAggregateTriangleIndices = 9'000'000U;

void add_aggregate_size(
    std::uint64_t& total,
    const std::uint64_t addition,
    const std::uint64_t limit,
    const char* const message) {
    if (total > limit || addition > limit - total) {
        throw std::runtime_error(message);
    }
    total += addition;
}

void append_terrain_material_batches(
    std::vector<SceneMaterialBatchV1>& destination,
    const std::span<const SceneMaterialBatchV1> source,
    const std::uint64_t triangle_offset) {
    std::uint64_t expected_local_triangle = 0U;
    for (const auto& batch : source) {
        if (batch.first_triangle != expected_local_triangle ||
            batch.index_count == 0U || batch.index_count % 3U != 0U) {
            throw std::runtime_error(
                "A terrain material batch is not contiguous");
        }
        const auto triangle_count = batch.index_count / 3U;
        if (triangle_offset >
                std::numeric_limits<std::uint64_t>::max() -
                    batch.first_triangle ||
            expected_local_triangle >
                std::numeric_limits<std::uint64_t>::max() - triangle_count) {
            throw std::runtime_error(
                "A terrain material batch range overflows");
        }
        const auto first_triangle = triangle_offset + batch.first_triangle;
        if (!destination.empty() &&
            destination.back().texture_index == batch.texture_index &&
            destination.back().first_triangle +
                    destination.back().index_count / 3U ==
                first_triangle) {
            if (destination.back().index_count >
                std::numeric_limits<std::uint64_t>::max() -
                    batch.index_count) {
                throw std::runtime_error(
                    "A merged terrain material batch overflows");
            }
            destination.back().index_count += batch.index_count;
        } else {
            destination.push_back(SceneMaterialBatchV1{
                first_triangle, batch.index_count, batch.texture_index});
        }
        expected_local_triangle += triangle_count;
    }
}

void validate_source_geometry_result(
    const SceneBlockRuntimeExecutionV1& execution,
    const std::uint16_t entrypoint) {
    const auto has_source = execution.source_geometry.has_value();
    const auto status = execution.source_geometry_status;
    if (entrypoint == kSceneBlockSourceGeometryEntrypointV1) {
        if (status ==
                SceneBlockRuntimeSourceGeometryStatusV1::not_attempted ||
            (status ==
                 SceneBlockRuntimeSourceGeometryStatusV1::recovered) !=
                has_source) {
            throw std::runtime_error(
                "The SceneBlock source-geometry result is inconsistent");
        }
        return;
    }
    if (status !=
            SceneBlockRuntimeSourceGeometryStatusV1::not_attempted ||
        has_source) {
        throw std::runtime_error(
            "A non-entry-16 record unexpectedly returned source geometry");
    }
}

[[nodiscard]] bool is_known_coordinate_domain(
    const MobySceneCoordinateDomainV1 domain) noexcept {
    switch (domain) {
    case MobySceneCoordinateDomainV1::world_units:
    case MobySceneCoordinateDomainV1::scene_block_itof0_units:
        return true;
    }
    return false;
}

[[nodiscard]] LevelSceneRecoveryResultV1 load_scene_geometry(
    const LevelSceneRecoveryRequestV1& request,
    const LevelSceneRecoveryLimitsV1& limits,
    const LevelSceneRecoveryProfileV1& profile) {
    const auto assets = load_scene_block_runtime_assets_v1(
        request.disc_image,
        request.boot_executable,
        request.level_id,
        limits.scene_block_load);

    const auto load_one_record = [&](const std::uint64_t record_index) {
        return execute_scene_block_runtime_record_v1(
            assets,
            record_index,
            request.entrypoint,
            profile.frame_input,
            limits.scene_block_execution);
    };

    if (request.record_selection ==
        LevelSceneRecordSelectionV1::all_records) {
        if (assets.directory.entries.empty()) {
            throw std::runtime_error(
                "The selected level has no SceneBlock records");
        }
        if (assets.directory.entries.size() > limits.max_aggregate_records) {
            throw std::runtime_error(
                "The selected level exceeds the full-level record limit");
        }

        std::vector<SceneGeometryV1> raster_batches;
        std::vector<SceneGeometry3dV1> source_batches;
        raster_batches.reserve(assets.directory.entries.size());
        source_batches.reserve(assets.directory.entries.size());

        LevelSceneRecoveryResultV1 result;
        result.total_record_count = assets.directory.entries.size();
        std::uint64_t raster_vertex_count = 0U;
        std::uint64_t raster_index_count = 0U;
        std::uint64_t source_vertex_count = 0U;
        std::uint64_t source_index_count = 0U;
        for (std::size_t record_index = 0U;
             record_index < assets.directory.entries.size();
             ++record_index) {
            const auto execution = load_one_record(record_index);
            switch (execution.gs_status) {
            case SceneBlockRuntimeGsStatusV1::not_attempted:
                throw std::runtime_error(
                    "A full-level SceneBlock record was not executed");
            case SceneBlockRuntimeGsStatusV1::no_events:
                ++result.no_event_record_count;
                continue;
            case SceneBlockRuntimeGsStatusV1::incomplete_stream:
                ++result.incomplete_stream_record_count;
                continue;
            case SceneBlockRuntimeGsStatusV1::decoded:
                break;
            }
            if (!execution.gs) {
                throw std::runtime_error(
                    "A decoded full-level GS stream is unavailable");
            }
            ++result.decoded_record_count;
            validate_source_geometry_result(execution, request.entrypoint);

            const auto has_emitted_triangle = std::ranges::any_of(
                execution.gs->primitives,
                [](const GifGsPrimitiveV1& primitive) {
                    const auto topology = primitive.topology;
                    return primitive.emission ==
                               GifGsPrimitiveEmissionV1::emitted &&
                        primitive.vertex_count == 3U &&
                        (topology ==
                             GifGsPrimitiveTopologyV1::triangle_list ||
                         topology ==
                             GifGsPrimitiveTopologyV1::triangle_strip ||
                         topology ==
                             GifGsPrimitiveTopologyV1::triangle_fan);
                });
            if (!has_emitted_triangle) {
                continue;
            }

            auto raster_geometry = build_scene_geometry_v1(*execution.gs);
            add_aggregate_size(
                raster_vertex_count,
                raster_geometry.vertices.size(),
                limits.aggregate_geometry.max_vertices,
                "The full-level raster geometry exceeds its vertex limit");
            add_aggregate_size(
                raster_index_count,
                raster_geometry.triangle_indices.size(),
                limits.aggregate_geometry.max_triangle_indices,
                "The full-level raster geometry exceeds its index limit");
            raster_batches.push_back(std::move(raster_geometry));
            ++result.raster_record_count;
            if (execution.source_geometry_status ==
                    SceneBlockRuntimeSourceGeometryStatusV1::recovered &&
                execution.source_geometry) {
                auto source_material =
                    build_scene_geometry_3d_material_v1(
                        *execution.source_geometry,
                        *execution.gs);
                auto& source_geometry = source_material.geometry;
                if (source_index_count % 3U != 0U) {
                    throw std::runtime_error(
                        "The aggregate terrain geometry is not a triangle list");
                }
                append_terrain_material_batches(
                    result.terrain_material_batches,
                    source_material.material_batches,
                    source_index_count / 3U);
                add_aggregate_size(
                    result.terrain_textured_triangle_count,
                    source_material.textured_triangle_count,
                    limits.aggregate_geometry.max_triangle_indices / 3U,
                    "The textured terrain triangle count exceeds its limit");
                add_aggregate_size(
                    result.terrain_unresolved_material_triangle_count,
                    source_material.unresolved_material_triangle_count,
                    limits.aggregate_geometry.max_triangle_indices / 3U,
                    "The unresolved terrain material count exceeds its limit");
                add_aggregate_size(
                    result.terrain_single_context_triangle_count,
                    source_material.single_programmed_context_triangle_count,
                    limits.aggregate_geometry.max_triangle_indices / 3U,
                    "The terrain context-policy count exceeds its limit");
                add_aggregate_size(
                    result.terrain_vertices_with_stq,
                    source_material.vertices_with_stq,
                    limits.aggregate_geometry.max_vertices,
                    "The terrain STQ vertex count exceeds its limit");
                add_aggregate_size(
                    result.terrain_vertices_without_stq,
                    source_material.vertices_without_stq,
                    limits.aggregate_geometry.max_vertices,
                    "The missing terrain STQ count exceeds its limit");
                add_aggregate_size(
                    source_vertex_count,
                    source_geometry.vertices.size(),
                    limits.aggregate_geometry.max_vertices,
                    "The full-level source geometry exceeds its vertex limit");
                add_aggregate_size(
                    source_index_count,
                    source_geometry.triangle_indices.size(),
                    limits.aggregate_geometry.max_triangle_indices,
                    "The full-level source geometry exceeds its index limit");
                source_batches.push_back(std::move(source_material.geometry));
                ++result.source_record_count;
            } else if (execution.source_geometry_status ==
                       SceneBlockRuntimeSourceGeometryStatusV1::
                           unavailable_layout) {
                ++result.unavailable_source_record_count;
            }
        }

        if (raster_batches.empty()) {
            throw std::runtime_error(
                "No full-level SceneBlock record produced triangle geometry");
        }
        if (request.entrypoint == kSceneBlockSourceGeometryEntrypointV1 &&
            result.source_record_count +
                    result.unavailable_source_record_count !=
                result.raster_record_count) {
            throw std::runtime_error(
                "The full-level source-geometry record counts are inconsistent");
        }
        result.raster = merge_scene_geometries_v1(
            raster_batches, limits.aggregate_geometry);
        if (!source_batches.empty()) {
            result.source = merge_scene_geometries_3d_v1(
                source_batches, limits.aggregate_geometry);
        }
        return result;
    }

    const auto execution = load_one_record(request.record_index);

    if (!execution.initialization.ready_state) {
        throw std::runtime_error(
            "The SceneBlock task did not finish its initialization entrypoint");
    }
    if (!execution.record) {
        throw std::runtime_error("The SceneBlock record was not executed");
    }
    if (execution.gs_status != SceneBlockRuntimeGsStatusV1::decoded ||
        !execution.gs) {
        switch (execution.gs_status) {
        case SceneBlockRuntimeGsStatusV1::not_attempted:
            throw std::runtime_error("The GS stream was not attempted");
        case SceneBlockRuntimeGsStatusV1::no_events:
            throw std::runtime_error(
                "The selected SceneBlock record produced no XGKICK events");
        case SceneBlockRuntimeGsStatusV1::incomplete_stream:
            throw std::runtime_error(
                "The selected SceneBlock record produced an incomplete GS stream");
        case SceneBlockRuntimeGsStatusV1::decoded:
            break;
        }
        throw std::runtime_error("The decoded GS stream is unavailable");
    }
    validate_source_geometry_result(execution, request.entrypoint);
    if (execution.source_geometry_status ==
        SceneBlockRuntimeSourceGeometryStatusV1::unavailable_layout) {
        throw std::runtime_error(
            "The selected SceneBlock record has no recoverable source geometry: " +
            execution.source_geometry_diagnostic.value_or(
                "no diagnostic was supplied"));
    }
    LevelSceneRecoveryResultV1 result;
    result.raster = build_scene_geometry_v1(*execution.gs);
    result.total_record_count = 1U;
    result.decoded_record_count = 1U;
    result.raster_record_count = 1U;
    if (execution.source_geometry) {
        auto source_material = build_scene_geometry_3d_material_v1(
            *execution.source_geometry, *execution.gs);
        result.terrain_material_batches =
            std::move(source_material.material_batches);
        result.terrain_textured_triangle_count =
            source_material.textured_triangle_count;
        result.terrain_unresolved_material_triangle_count =
            source_material.unresolved_material_triangle_count;
        result.terrain_single_context_triangle_count =
            source_material.single_programmed_context_triangle_count;
        result.terrain_vertices_with_stq = source_material.vertices_with_stq;
        result.terrain_vertices_without_stq =
            source_material.vertices_without_stq;
        result.source = std::move(source_material.geometry);
        result.source_record_count = 1U;
    }
    return result;
}

void attach_static_environment_geometry(
    const LevelSceneRecoveryRequestV1& request,
    const LevelSceneRecoveryLimitsV1& limits,
    const LevelSceneRecoveryProfileV1& profile,
    LevelSceneRecoveryResultV1& geometry) {
    if (request.entrypoint != kSceneBlockSourceGeometryEntrypointV1 ||
        !geometry.source) {
        return;
    }

    auto assets = load_rac_level_moby_assets_v1(
        request.disc_image, request.level_id, limits.level_assets);
    if (geometry.terrain_material_batches.empty() ||
        assets.tfrag_textures.textures.empty()) {
        throw std::runtime_error(
            "The terrain source geometry has no decoded texture materials");
    }
    const auto terrain_triangle_count =
        geometry.source->triangle_indices.size() / 3U;
    geometry.terrain_triangle_count = terrain_triangle_count;
    const auto& final_terrain_batch = geometry.terrain_material_batches.back();
    if (geometry.source->triangle_indices.size() % 3U != 0U ||
        final_terrain_batch.first_triangle +
                final_terrain_batch.index_count / 3U !=
            terrain_triangle_count) {
        throw std::runtime_error(
            "The terrain texture materials do not cover source geometry");
    }
    for (const auto& batch : geometry.terrain_material_batches) {
        if (batch.texture_index &&
            *batch.texture_index >= assets.tfrag_textures.textures.size()) {
            throw std::runtime_error(
                "A terrain material references a missing tfrag texture");
        }
    }
    geometry.tfrag_texture_bank = std::move(assets.tfrag_textures);

    if (request.record_selection !=
        LevelSceneRecordSelectionV1::all_records) {
        return;
    }
    auto moby = build_filtered_moby_scene_geometry_v1(
        assets.models,
        assets.gameplay.static_mobies,
        profile.excluded_moby_class_ids,
        profile.moby_coordinate_domain,
        limits.moby_geometry);
    geometry.moby_model_count = moby.stats.model_count;
    geometry.moby_rendered_model_count = moby.stats.rendered_model_count;
    geometry.moby_placement_count = moby.stats.placement_count;
    geometry.moby_excluded_placement_count =
        moby.stats.excluded_placement_count;
    geometry.moby_rendered_placement_count =
        moby.stats.rendered_placement_count;
    geometry.moby_animated_placement_count =
        moby.stats.animated_model_placement_count;
    geometry.moby_missing_or_empty_placement_count =
        moby.stats.missing_model_placement_count +
        moby.stats.empty_model_placement_count;

    auto tie = build_tie_scene_geometry_v1(
        assets.tie_models,
        assets.gameplay.tie_instances,
        profile.tie_coordinate_domain,
        limits.tie_geometry);
    geometry.tie_model_count = tie.stats.model_count;
    geometry.tie_rendered_model_count = tie.stats.rendered_model_count;
    geometry.tie_placement_count = tie.stats.placement_count;
    geometry.tie_rendered_placement_count =
        tie.stats.rendered_placement_count;
    geometry.tie_missing_or_empty_placement_count =
        tie.stats.missing_model_placement_count +
        tie.stats.empty_model_placement_count;

    std::vector<SceneGeometry3dV1> batches;
    batches.reserve(3U);
    batches.push_back(std::move(*geometry.source));
    auto next_triangle = terrain_triangle_count;
    if (moby.geometry) {
        geometry.moby_first_triangle = next_triangle;
        geometry.moby_triangle_count = moby.geometry->emitted_triangle_count;
        geometry.moby_material_batches = std::move(moby.material_batches);
        geometry.moby_texture_bank = std::move(assets.textures);
        next_triangle += geometry.moby_triangle_count;
        batches.push_back(std::move(*moby.geometry));
    }
    if (tie.geometry) {
        geometry.tie_first_triangle = next_triangle;
        geometry.tie_triangle_count = tie.geometry->emitted_triangle_count;
        geometry.tie_material_batches = std::move(tie.material_batches);
        geometry.tie_texture_bank = std::move(assets.tie_textures);
        batches.push_back(std::move(*tie.geometry));
    }

    if (batches.size() == 1U) {
        geometry.source = std::move(batches.front());
        return;
    }
    geometry.source = merge_scene_geometries_3d_v1(
        batches, limits.aggregate_geometry);
}

} // namespace

LevelSceneRecoveryLimitsV1 make_level_scene_recovery_limits_v1() {
    constexpr DvpVuExecutionLimitsV1 kDvpLimits{
        1'000'000U,
        64U,
        1024U,
        65'536U,
    };
    constexpr RacMobyPacketGeometryLimitsV1 packet_limits{
        kMaximumRuntimeBytes,
        4096U,
        4096U,
        4096U,
        1'000'000U,
        4096U,
        1'000'000U,
    };

    LevelSceneRecoveryLimitsV1 result;
    result.scene_block_load = SceneBlockRuntimeLoadLimitsV1{
        kMaximumRuntimeBytes,
        kMaximumRuntimeBytes,
        kMaximumRuntimeBytes,
        SceneBlockDirectoryLimits{
            kMaximumRuntimeBytes,
            kMaximumAggregateRecords,
            kMaximumRuntimeBytes,
        },
        DvpVuLimits{
            kMaximumRuntimeBytes,
            128U,
            kDvpVu1MicroMemoryBytes,
            128U,
            2U * kDvpVu1InstructionCount,
            8U * kDvpVu1InstructionCount,
        },
    };
    result.scene_block_execution = SceneBlockRuntimeExecutionLimitsV1{
        SceneBlockTaskExecutionLimitsV1{
            SceneBlockTaskBuildLimitsV1{
                kMaximumRuntimeBytes,
                kSceneBlockTaskMaximumDmaReferences,
                kMaximumRuntimeBytes,
                SceneBlockVifLimits{
                    kMaximumRuntimeBytes,
                    1'000'000U,
                    kMaximumRuntimeBytes,
                },
            },
            1'000'000U,
            SceneBlockDvpVuBridgeLimitsV1{1'000'000U},
            kDvpLimits,
        },
        GifGsDecodeLimitsV1{
            65'536U,
            1'000'000U,
            1'000'000U,
            1'000'000U,
            1'000'000U,
            64U,
        },
    };
    result.level_assets = RacLevelMobyAssetLimitsV1{
        kMaximumRuntimeBytes,
        kMaximumRuntimeBytes,
        kMaximumRuntimeBytes,
        4096U,
        65'536U,
        1'000'000U,
        1'000'000U,
        RacLevelCoreLimitsV1{
            kMaximumRuntimeBytes,
            kMaximumRuntimeBytes,
            kMaximumRuntimeBytes,
            4096U,
            255U,
            4096U,
            4096U,
            4096U,
            255U,
            255U,
        },
        RacLevelCollisionLimitsV1{
            kMaximumRuntimeBytes,
            65'536U,
            1'000'000U,
            4'000'000U,
            1'000'000U,
            16'000'000U,
            16'000'000U,
            65'536U,
            4'000'000U,
            4'000'000U,
        },
        RacGameplayBankLimitsV1{kMaximumRuntimeBytes},
        RacMobyClassLimitsV1{kMaximumRuntimeBytes, false},
        RacMobyClassLimitsV1{kMaximumRuntimeBytes, true},
        RacMobyModelGeometryLimitsV1{
            packet_limits,
            4096U,
            1'000'000U,
            1'000'000U,
        },
        RacLevelMobyTextureLimitsV1{
            kMaximumRuntimeBytes,
            kMaximumRuntimeBytes,
            kMaximumRuntimeBytes,
            255U,
            4096U,
            4096U,
            16U * 1024U * 1024U,
            16U * 1024U * 1024U,
            64U * 1024U * 1024U,
        },
        RacTieClassLimitsV1{
            kMaximumRuntimeBytes,
            4096U,
            65'536U,
            kMaximumAggregateVertices,
            kMaximumAggregateVertices,
            kMaximumAggregateTriangleIndices / 3U,
            16U,
        },
        4096U,
        65'536U,
        kMaximumAggregateVertices,
        kMaximumAggregateTriangleIndices / 3U,
    };
    result.aggregate_geometry = SceneGeometryMergeLimitsV1{
        kMaximumAggregateVertices,
        kMaximumAggregateTriangleIndices,
    };
    result.moby_geometry = MobySceneGeometryLimitsV1{
        4096U,
        65'536U,
        kMaximumAggregateVertices,
        kMaximumAggregateTriangleIndices,
        kMaximumAggregateVertices,
        kMaximumAggregateTriangleIndices,
    };
    result.tie_geometry = TieSceneGeometryLimitsV1{
        4096U,
        65'536U,
        kMaximumAggregateVertices,
        kMaximumAggregateTriangleIndices,
        kMaximumAggregateVertices,
        kMaximumAggregateTriangleIndices,
    };
    result.max_aggregate_records = kMaximumAggregateRecords;
    return result;
}

LevelSceneRecoveryProfileV1 make_level_scene_recovery_profile_v1() {
    LevelSceneRecoveryProfileV1 result;
    for (std::size_t row = 0U;
         row < result.frame_input.transform_qwords.size();
         ++row) {
        for (std::size_t lane = 0U;
             lane < result.frame_input.transform_qwords[row].lanes.size();
             ++lane) {
            result.frame_input.transform_qwords[row].lanes[lane] = DvpVuWordV1{
                row == lane ? 0x3f800000U : 0U,
                std::numeric_limits<std::uint32_t>::max(),
            };
        }
    }
    result.moby_coordinate_domain =
        MobySceneCoordinateDomainV1::scene_block_itof0_units;
    result.tie_coordinate_domain =
        TieSceneCoordinateDomainV1::scene_block_itof0_units;
    return result;
}

void validate_level_scene_recovery_request_v1(
    const LevelSceneRecoveryRequestV1& request,
    const LevelSceneRecoveryLimitsV1& limits,
    const LevelSceneRecoveryProfileV1& profile) {
    if (request.disc_image.empty()) {
        throw std::runtime_error(
            "The level-scene recovery disc image path is empty");
    }
    if (request.boot_executable.empty()) {
        throw std::runtime_error(
            "The level-scene recovery boot executable path is empty");
    }
    switch (request.record_selection) {
    case LevelSceneRecordSelectionV1::single_record:
    case LevelSceneRecordSelectionV1::all_records:
        break;
    default:
        throw std::runtime_error(
            "The level-scene record selection is invalid");
    }
    if (limits.max_aggregate_records == 0U ||
        limits.aggregate_geometry.max_vertices == 0U ||
        limits.aggregate_geometry.max_triangle_indices < 3U) {
        throw std::runtime_error(
            "The level-scene aggregate limits must be bounded and non-zero");
    }
    if (!is_known_coordinate_domain(profile.moby_coordinate_domain) ||
        !is_known_coordinate_domain(profile.tie_coordinate_domain)) {
        throw std::runtime_error(
            "The level-scene coordinate-domain profile is invalid");
    }
    if (profile.excluded_moby_class_ids.size() >
        limits.level_assets.level_core.max_moby_classes) {
        throw std::runtime_error(
            "The level-scene Moby exclusion profile exceeds its class limit");
    }
    for (std::size_t index = 1U;
         index < profile.excluded_moby_class_ids.size(); ++index) {
        if (profile.excluded_moby_class_ids[index - 1U] >=
            profile.excluded_moby_class_ids[index]) {
            throw std::runtime_error(
                "The level-scene Moby exclusion profile is duplicate or out "
                "of order");
        }
    }
}

LevelSceneRecoveryResultV1 recover_level_scene_v1(
    const LevelSceneRecoveryRequestV1& request,
    const LevelSceneRecoveryLimitsV1& limits,
    const LevelSceneRecoveryProfileV1& profile) {
    validate_level_scene_recovery_request_v1(request, limits, profile);
    auto result = load_scene_geometry(request, limits, profile);
    attach_static_environment_geometry(request, limits, profile, result);
    return result;
}

} // namespace openrc::runtime
