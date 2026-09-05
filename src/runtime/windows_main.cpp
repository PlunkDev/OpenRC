#include "d3d11_renderer.hpp"
#include "moby_scene_geometry.hpp"
#include "scene_geometry.hpp"
#include "tie_scene_geometry.hpp"

#include "openrc/dvp_vu.hpp"
#include "openrc/dvp_vu_execute.hpp"
#include "openrc/scene_block_runtime.hpp"
#include "openrc/scene_block_task.hpp"
#include "openrc/scene_block_task_execute.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shellapi.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"PlunkDev.OpenRC.Runtime.Window";
constexpr wchar_t kApplicationName[] = L"OpenRC native level viewer";
constexpr std::uint64_t kMaximumRuntimeBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumAggregateRecords = 4096U;
constexpr std::uint64_t kMaximumAggregateVertices = 3'000'000U;
constexpr std::uint64_t kMaximumAggregateTriangleIndices = 9'000'000U;

struct RuntimeArguments {
    std::filesystem::path disc_image;
    std::filesystem::path boot_executable;
    std::uint32_t level_id = 0U;
    std::uint64_t record_index = 0U;
    std::uint16_t entrypoint = 16U;
    bool all_records = false;
    bool smoke_test = false;
    bool show_help = false;
};

struct WindowState {
    std::unique_ptr<openrc::runtime::D3d11Renderer> renderer;
    std::optional<std::string> fatal_error;
    std::wstring base_title;
    POINT previous_pointer{};
    bool orbit_drag_active = false;
};

struct LoadedSceneGeometry {
    openrc::runtime::SceneGeometryV1 raster;
    std::optional<openrc::runtime::SceneGeometry3dV1> source;
    std::optional<openrc::RacLevelMobyTextureBankV1> tfrag_texture_bank;
    std::vector<openrc::runtime::SceneMaterialBatchV1>
        terrain_material_batches;
    std::uint64_t terrain_triangle_count = 0U;
    std::optional<openrc::RacLevelMobyTextureBankV1> moby_texture_bank;
    std::vector<openrc::runtime::MobySceneMaterialBatchV1>
        moby_material_batches;
    std::optional<std::uint64_t> moby_first_triangle;
    std::optional<openrc::RacLevelMobyTextureBankV1> tie_texture_bank;
    std::vector<openrc::runtime::MobySceneMaterialBatchV1>
        tie_material_batches;
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
    std::vector<openrc::runtime::SceneMaterialBatchV1>& destination,
    const std::span<const openrc::runtime::SceneMaterialBatchV1> source,
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
            destination.push_back(openrc::runtime::SceneMaterialBatchV1{
                first_triangle, batch.index_count, batch.texture_index});
        }
        expected_local_triangle += triangle_count;
    }
}

void validate_source_geometry_result(
    const openrc::SceneBlockRuntimeExecutionV1& execution,
    const std::uint16_t entrypoint) {
    const auto has_source = execution.source_geometry.has_value();
    const auto status = execution.source_geometry_status;
    if (entrypoint == openrc::kSceneBlockSourceGeometryEntrypointV1) {
        if (status ==
                openrc::SceneBlockRuntimeSourceGeometryStatusV1::not_attempted ||
            (status ==
                 openrc::SceneBlockRuntimeSourceGeometryStatusV1::recovered) !=
                has_source) {
            throw std::runtime_error(
                "The SceneBlock source-geometry result is inconsistent");
        }
        return;
    }
    if (status !=
            openrc::SceneBlockRuntimeSourceGeometryStatusV1::not_attempted ||
        has_source) {
        throw std::runtime_error(
            "A non-entry-16 record unexpectedly returned source geometry");
    }
}

[[nodiscard]] std::wstring utf8_to_wide(const std::string_view value) {
    if (value.empty()) {
        return {};
    }
    if (value.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return L"An error message was too long to display.";
    }
    const auto required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return L"OpenRC encountered an error whose text could not be decoded.";
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required);
    return result;
}

[[nodiscard]] std::optional<std::uint64_t>
parse_unsigned_decimal(const std::wstring_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0U;
    for (const auto character : text) {
        if (character < L'0' || character > L'9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint64_t>(character - L'0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) /
                        10U) {
            return std::nullopt;
        }
        value = value * 10U + digit;
    }
    return value;
}

[[nodiscard]] RuntimeArguments parse_arguments() {
    int argument_count = 0;
    auto* const raw_arguments =
        CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (raw_arguments == nullptr) {
        throw std::runtime_error("CommandLineToArgvW failed");
    }
    struct ArgumentArrayDeleter {
        void operator()(wchar_t** value) const noexcept {
            LocalFree(value);
        }
    };
    const std::unique_ptr<wchar_t*, ArgumentArrayDeleter> arguments(
        raw_arguments);

    RuntimeArguments result;
    bool saw_disc_image = false;
    bool saw_boot_executable = false;
    bool saw_level = false;
    bool saw_record = false;
    bool saw_entrypoint = false;

    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view name(raw_arguments[index]);
        if (name == L"--help" || name == L"-h" || name == L"/?") {
            result.show_help = true;
            continue;
        }
        if (name == L"--smoke-test") {
            result.smoke_test = true;
            continue;
        }
        if (index + 1 >= argument_count) {
            throw std::runtime_error(
                "A named runtime argument is missing its value");
        }
        const std::wstring_view value(raw_arguments[++index]);
        if (name == L"--disc-image") {
            if (saw_disc_image || value.empty()) {
                throw std::runtime_error(
                    "--disc-image must be supplied exactly once");
            }
            result.disc_image = std::filesystem::path(value);
            saw_disc_image = true;
        } else if (name == L"--boot-executable") {
            if (saw_boot_executable || value.empty()) {
                throw std::runtime_error(
                    "--boot-executable must be supplied exactly once");
            }
            result.boot_executable = std::filesystem::path(value);
            saw_boot_executable = true;
        } else if (name == L"--level") {
            const auto parsed = parse_unsigned_decimal(value);
            if (saw_level || !parsed ||
                *parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error(
                    "--level must be one unsigned decimal value");
            }
            result.level_id = static_cast<std::uint32_t>(*parsed);
            saw_level = true;
        } else if (name == L"--record") {
            if (saw_record) {
                throw std::runtime_error(
                    "--record must be supplied at most once");
            }
            if (value == L"all") {
                result.all_records = true;
            } else {
                const auto parsed = parse_unsigned_decimal(value);
                if (!parsed) {
                    throw std::runtime_error(
                        "--record must be one unsigned decimal value or all");
                }
                result.record_index = *parsed;
            }
            saw_record = true;
        } else if (name == L"--entry-pair") {
            const auto parsed = parse_unsigned_decimal(value);
            if (saw_entrypoint || !parsed ||
                *parsed > std::numeric_limits<std::uint16_t>::max()) {
                throw std::runtime_error(
                    "--entry-pair must be one unsigned decimal value");
            }
            result.entrypoint = static_cast<std::uint16_t>(*parsed);
            saw_entrypoint = true;
        } else {
            throw std::runtime_error("The runtime received an unknown argument");
        }
    }

    if (!result.show_help && (!saw_disc_image || !saw_boot_executable)) {
        throw std::runtime_error(
            "Both --disc-image and --boot-executable are required");
    }
    return result;
}

[[nodiscard]] openrc::SceneBlockRuntimeLoadLimitsV1 make_load_limits() {
    return openrc::SceneBlockRuntimeLoadLimitsV1{
        kMaximumRuntimeBytes,
        kMaximumRuntimeBytes,
        kMaximumRuntimeBytes,
        openrc::SceneBlockDirectoryLimits{
            kMaximumRuntimeBytes,
            kMaximumAggregateRecords,
            kMaximumRuntimeBytes,
        },
        openrc::DvpVuLimits{
            kMaximumRuntimeBytes,
            128U,
            openrc::kDvpVu1MicroMemoryBytes,
            128U,
            2U * openrc::kDvpVu1InstructionCount,
            8U * openrc::kDvpVu1InstructionCount,
        },
    };
}

[[nodiscard]] openrc::SceneBlockRuntimeExecutionLimitsV1
make_execution_limits() {
    constexpr openrc::DvpVuExecutionLimitsV1 kDvpLimits{
        1'000'000U,
        64U,
        1024U,
        65'536U,
    };
    return openrc::SceneBlockRuntimeExecutionLimitsV1{
        openrc::SceneBlockTaskExecutionLimitsV1{
            openrc::SceneBlockTaskBuildLimitsV1{
                kMaximumRuntimeBytes,
                openrc::kSceneBlockTaskMaximumDmaReferences,
                kMaximumRuntimeBytes,
                openrc::SceneBlockVifLimits{
                    kMaximumRuntimeBytes,
                    1'000'000U,
                    kMaximumRuntimeBytes,
                },
            },
            1'000'000U,
            openrc::SceneBlockDvpVuBridgeLimitsV1{1'000'000U},
            kDvpLimits,
        },
        openrc::GifGsDecodeLimitsV1{
            65'536U,
            1'000'000U,
            1'000'000U,
            1'000'000U,
            1'000'000U,
            64U,
        },
    };
}

[[nodiscard]] openrc::RacLevelMobyAssetLimitsV1
make_moby_asset_limits() {
    constexpr openrc::RacMobyPacketGeometryLimitsV1 packet_limits{
        kMaximumRuntimeBytes,
        4096U,
        4096U,
        4096U,
        1'000'000U,
        4096U,
        1'000'000U,
    };
    return openrc::RacLevelMobyAssetLimitsV1{
        kMaximumRuntimeBytes,
        kMaximumRuntimeBytes,
        kMaximumRuntimeBytes,
        4096U,
        65'536U,
        1'000'000U,
        1'000'000U,
        openrc::RacLevelCoreLimitsV1{
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
        openrc::RacLevelCollisionLimitsV1{
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
        openrc::RacGameplayBankLimitsV1{kMaximumRuntimeBytes},
        openrc::RacMobyClassLimitsV1{kMaximumRuntimeBytes, false},
        openrc::RacMobyClassLimitsV1{kMaximumRuntimeBytes, true},
        openrc::RacMobyModelGeometryLimitsV1{
            packet_limits,
            4096U,
            1'000'000U,
            1'000'000U,
        },
        openrc::RacLevelMobyTextureLimitsV1{
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
        openrc::RacTieClassLimitsV1{
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
}

[[nodiscard]] openrc::SceneBlockTaskFrameInputV1
make_identity_frame_input() {
    openrc::SceneBlockTaskFrameInputV1 result;
    for (std::size_t row = 0U; row < result.transform_qwords.size(); ++row) {
        for (std::size_t lane = 0U;
             lane < result.transform_qwords[row].lanes.size();
             ++lane) {
            result.transform_qwords[row].lanes[lane] = openrc::DvpVuWordV1{
                row == lane ? 0x3f800000U : 0U,
                std::numeric_limits<std::uint32_t>::max(),
            };
        }
    }
    return result;
}

[[nodiscard]] LoadedSceneGeometry
load_scene_geometry(const RuntimeArguments& arguments) {
    const auto assets = openrc::load_scene_block_runtime_assets_v1(
        arguments.disc_image,
        arguments.boot_executable,
        arguments.level_id,
        make_load_limits());

    const auto load_one_record = [&](const std::uint64_t record_index) {
        return openrc::execute_scene_block_runtime_record_v1(
            assets,
            record_index,
            arguments.entrypoint,
            make_identity_frame_input(),
            make_execution_limits());
    };

    if (arguments.all_records) {
        if (assets.directory.entries.empty()) {
            throw std::runtime_error(
                "The selected level has no SceneBlock records");
        }
        if (assets.directory.entries.size() > kMaximumAggregateRecords) {
            throw std::runtime_error(
                "The selected level exceeds the full-level record limit");
        }

        std::vector<openrc::runtime::SceneGeometryV1> raster_batches;
        std::vector<openrc::runtime::SceneGeometry3dV1> source_batches;
        raster_batches.reserve(assets.directory.entries.size());
        source_batches.reserve(assets.directory.entries.size());

        LoadedSceneGeometry result;
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
            case openrc::SceneBlockRuntimeGsStatusV1::not_attempted:
                throw std::runtime_error(
                    "A full-level SceneBlock record was not executed");
            case openrc::SceneBlockRuntimeGsStatusV1::no_events:
                ++result.no_event_record_count;
                continue;
            case openrc::SceneBlockRuntimeGsStatusV1::incomplete_stream:
                ++result.incomplete_stream_record_count;
                continue;
            case openrc::SceneBlockRuntimeGsStatusV1::decoded:
                break;
            }
            if (!execution.gs) {
                throw std::runtime_error(
                    "A decoded full-level GS stream is unavailable");
            }
            ++result.decoded_record_count;
            validate_source_geometry_result(execution, arguments.entrypoint);

            const auto has_emitted_triangle = std::ranges::any_of(
                execution.gs->primitives,
                [](const openrc::GifGsPrimitiveV1& primitive) {
                    const auto topology = primitive.topology;
                    return primitive.emission ==
                               openrc::GifGsPrimitiveEmissionV1::emitted &&
                        primitive.vertex_count == 3U &&
                        (topology ==
                             openrc::GifGsPrimitiveTopologyV1::triangle_list ||
                         topology ==
                             openrc::GifGsPrimitiveTopologyV1::triangle_strip ||
                         topology ==
                             openrc::GifGsPrimitiveTopologyV1::triangle_fan);
                });
            if (!has_emitted_triangle) {
                continue;
            }

            auto raster_geometry =
                openrc::runtime::build_scene_geometry_v1(*execution.gs);
            add_aggregate_size(
                raster_vertex_count,
                raster_geometry.vertices.size(),
                kMaximumAggregateVertices,
                "The full-level raster geometry exceeds its vertex limit");
            add_aggregate_size(
                raster_index_count,
                raster_geometry.triangle_indices.size(),
                kMaximumAggregateTriangleIndices,
                "The full-level raster geometry exceeds its index limit");
            raster_batches.push_back(std::move(raster_geometry));
            ++result.raster_record_count;
            if (execution.source_geometry_status ==
                    openrc::SceneBlockRuntimeSourceGeometryStatusV1::recovered &&
                execution.source_geometry) {
                auto source_material =
                    openrc::runtime::build_scene_geometry_3d_material_v1(
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
                    kMaximumAggregateTriangleIndices / 3U,
                    "The textured terrain triangle count exceeds its limit");
                add_aggregate_size(
                    result.terrain_unresolved_material_triangle_count,
                    source_material.unresolved_material_triangle_count,
                    kMaximumAggregateTriangleIndices / 3U,
                    "The unresolved terrain material count exceeds its limit");
                add_aggregate_size(
                    result.terrain_single_context_triangle_count,
                    source_material.single_programmed_context_triangle_count,
                    kMaximumAggregateTriangleIndices / 3U,
                    "The terrain context-policy count exceeds its limit");
                add_aggregate_size(
                    result.terrain_vertices_with_stq,
                    source_material.vertices_with_stq,
                    kMaximumAggregateVertices,
                    "The terrain STQ vertex count exceeds its limit");
                add_aggregate_size(
                    result.terrain_vertices_without_stq,
                    source_material.vertices_without_stq,
                    kMaximumAggregateVertices,
                    "The missing terrain STQ count exceeds its limit");
                add_aggregate_size(
                    source_vertex_count,
                    source_geometry.vertices.size(),
                    kMaximumAggregateVertices,
                    "The full-level source geometry exceeds its vertex limit");
                add_aggregate_size(
                    source_index_count,
                    source_geometry.triangle_indices.size(),
                    kMaximumAggregateTriangleIndices,
                    "The full-level source geometry exceeds its index limit");
                source_batches.push_back(std::move(source_material.geometry));
                ++result.source_record_count;
            } else if (execution.source_geometry_status ==
                       openrc::SceneBlockRuntimeSourceGeometryStatusV1::
                           unavailable_layout) {
                ++result.unavailable_source_record_count;
            }
        }

        if (raster_batches.empty()) {
            throw std::runtime_error(
                "No full-level SceneBlock record produced triangle geometry");
        }
        if (arguments.entrypoint ==
                openrc::kSceneBlockSourceGeometryEntrypointV1 &&
            result.source_record_count +
                    result.unavailable_source_record_count !=
                result.raster_record_count) {
            throw std::runtime_error(
                "The full-level source-geometry record counts are inconsistent");
        }
        constexpr openrc::runtime::SceneGeometryMergeLimitsV1 merge_limits{
            kMaximumAggregateVertices,
            kMaximumAggregateTriangleIndices,
        };
        result.raster = openrc::runtime::merge_scene_geometries_v1(
            raster_batches, merge_limits);
        if (!source_batches.empty()) {
            result.source = openrc::runtime::merge_scene_geometries_3d_v1(
                source_batches, merge_limits);
        }
        return result;
    }

    const auto execution = load_one_record(arguments.record_index);

    if (!execution.initialization.ready_state) {
        throw std::runtime_error(
            "The SceneBlock task did not finish its initialization entrypoint");
    }
    if (!execution.record) {
        throw std::runtime_error(
            "The SceneBlock record was not executed");
    }
    if (execution.gs_status != openrc::SceneBlockRuntimeGsStatusV1::decoded ||
        !execution.gs) {
        switch (execution.gs_status) {
        case openrc::SceneBlockRuntimeGsStatusV1::not_attempted:
            throw std::runtime_error(
                "The GS stream was not attempted");
        case openrc::SceneBlockRuntimeGsStatusV1::no_events:
            throw std::runtime_error(
                "The selected SceneBlock record produced no XGKICK events");
        case openrc::SceneBlockRuntimeGsStatusV1::incomplete_stream:
            throw std::runtime_error(
                "The selected SceneBlock record produced an incomplete GS stream");
        case openrc::SceneBlockRuntimeGsStatusV1::decoded:
            break;
        }
        throw std::runtime_error("The decoded GS stream is unavailable");
    }
    validate_source_geometry_result(execution, arguments.entrypoint);
    if (execution.source_geometry_status ==
        openrc::SceneBlockRuntimeSourceGeometryStatusV1::unavailable_layout) {
        throw std::runtime_error(
            "The selected SceneBlock record has no recoverable source geometry: " +
            execution.source_geometry_diagnostic.value_or(
                "no diagnostic was supplied"));
    }
    LoadedSceneGeometry result;
    result.raster = openrc::runtime::build_scene_geometry_v1(*execution.gs);
    result.total_record_count = 1U;
    result.decoded_record_count = 1U;
    result.raster_record_count = 1U;
    if (execution.source_geometry) {
        auto source_material =
            openrc::runtime::build_scene_geometry_3d_material_v1(
            *execution.source_geometry,
            *execution.gs);
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
    const RuntimeArguments& arguments,
    LoadedSceneGeometry& geometry) {
    if (arguments.entrypoint !=
            openrc::kSceneBlockSourceGeometryEntrypointV1 ||
        !geometry.source) {
        return;
    }

    auto assets = openrc::load_rac_level_moby_assets_v1(
        arguments.disc_image,
        arguments.level_id,
        make_moby_asset_limits());
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

    if (!arguments.all_records) {
        return;
    }
    constexpr openrc::runtime::MobySceneGeometryLimitsV1 moby_limits{
        4096U,
        65'536U,
        kMaximumAggregateVertices,
        kMaximumAggregateTriangleIndices,
        kMaximumAggregateVertices,
        kMaximumAggregateTriangleIndices,
    };
    auto moby = openrc::runtime::build_moby_scene_geometry_v1(
        assets.models,
        assets.gameplay.static_mobies,
        openrc::runtime::MobySceneCoordinateDomainV1::
            scene_block_itof0_units,
        moby_limits);
    geometry.moby_model_count = moby.stats.model_count;
    geometry.moby_rendered_model_count = moby.stats.rendered_model_count;
    geometry.moby_placement_count = moby.stats.placement_count;
    geometry.moby_rendered_placement_count =
        moby.stats.rendered_placement_count;
    geometry.moby_animated_placement_count =
        moby.stats.animated_model_placement_count;
    geometry.moby_missing_or_empty_placement_count =
        moby.stats.missing_model_placement_count +
        moby.stats.empty_model_placement_count;

    constexpr openrc::runtime::TieSceneGeometryLimitsV1 tie_limits{
        4096U,
        65'536U,
        kMaximumAggregateVertices,
        kMaximumAggregateTriangleIndices,
        kMaximumAggregateVertices,
        kMaximumAggregateTriangleIndices,
    };
    auto tie = openrc::runtime::build_tie_scene_geometry_v1(
        assets.tie_models,
        assets.gameplay.tie_instances,
        openrc::runtime::TieSceneCoordinateDomainV1::
            scene_block_itof0_units,
        tie_limits);
    geometry.tie_model_count = tie.stats.model_count;
    geometry.tie_rendered_model_count = tie.stats.rendered_model_count;
    geometry.tie_placement_count = tie.stats.placement_count;
    geometry.tie_rendered_placement_count =
        tie.stats.rendered_placement_count;
    geometry.tie_missing_or_empty_placement_count =
        tie.stats.missing_model_placement_count +
        tie.stats.empty_model_placement_count;

    std::vector<openrc::runtime::SceneGeometry3dV1> batches;
    batches.reserve(3U);
    batches.push_back(std::move(*geometry.source));
    auto next_triangle = terrain_triangle_count;
    if (moby.geometry) {
        geometry.moby_first_triangle = next_triangle;
        geometry.moby_triangle_count =
            moby.geometry->emitted_triangle_count;
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
    constexpr openrc::runtime::SceneGeometryMergeLimitsV1 merge_limits{
        kMaximumAggregateVertices,
        kMaximumAggregateTriangleIndices,
    };
    geometry.source = openrc::runtime::merge_scene_geometries_3d_v1(
        batches, merge_limits);
}

void refresh_window_title(const HWND window, const WindowState& state) {
    if (!state.renderer) {
        return;
    }
    const auto suffix = state.renderer->is_3d_view()
        ? L" - recovered level 3D (debug orbit)"
        : L" - decoded GS output (2D)";
    SetWindowTextW(window, (state.base_title + suffix).c_str());
}

void remember_window_error(
    const HWND window,
    WindowState& state,
    const std::string& message) {
    if (!state.fatal_error) {
        state.fatal_error = message;
    }
    PostMessageW(window, WM_CLOSE, 0U, 0);
}

LRESULT CALLBACK window_procedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param) {
    auto* state = reinterpret_cast<WindowState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create =
            reinterpret_cast<const CREATESTRUCTW*>(l_param);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (state != nullptr && state->renderer) {
            try {
                state->renderer->resize(
                    static_cast<std::uint32_t>(LOWORD(l_param)),
                    static_cast<std::uint32_t>(HIWORD(l_param)));
                InvalidateRect(window, nullptr, FALSE);
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (state != nullptr && state->renderer) {
            try {
                constexpr float kOrbitStep = 0.0872664626F;
                switch (w_param) {
                case VK_LEFT:
                    state->renderer->orbit(-kOrbitStep, 0.0F);
                    break;
                case VK_RIGHT:
                    state->renderer->orbit(kOrbitStep, 0.0F);
                    break;
                case VK_UP:
                    state->renderer->orbit(0.0F, kOrbitStep);
                    break;
                case VK_DOWN:
                    state->renderer->orbit(0.0F, -kOrbitStep);
                    break;
                case VK_ADD:
                case VK_OEM_PLUS:
                    state->renderer->zoom(1.0F);
                    break;
                case VK_SUBTRACT:
                case VK_OEM_MINUS:
                    state->renderer->zoom(-1.0F);
                    break;
                case 'R':
                    state->renderer->reset_camera();
                    break;
                case VK_TAB:
                    state->renderer->toggle_view_mode();
                    refresh_window_title(window, *state);
                    break;
                default:
                    return DefWindowProcW(window, message, w_param, l_param);
                }
                InvalidateRect(window, nullptr, FALSE);
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state != nullptr && state->renderer) {
            try {
                const auto delta = static_cast<float>(
                    GET_WHEEL_DELTA_WPARAM(w_param));
                state->renderer->zoom(delta / WHEEL_DELTA);
                InvalidateRect(window, nullptr, FALSE);
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (state != nullptr && state->renderer &&
            state->renderer->is_3d_view()) {
            state->previous_pointer = {
                static_cast<LONG>(GET_X_LPARAM(l_param)),
                static_cast<LONG>(GET_Y_LPARAM(l_param)),
            };
            state->orbit_drag_active = true;
            SetCapture(window);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state != nullptr && state->renderer &&
            state->orbit_drag_active && (w_param & MK_LBUTTON) != 0U) {
            const POINT current{
                static_cast<LONG>(GET_X_LPARAM(l_param)),
                static_cast<LONG>(GET_Y_LPARAM(l_param)),
            };
            const auto delta_x = current.x - state->previous_pointer.x;
            const auto delta_y = current.y - state->previous_pointer.y;
            state->previous_pointer = current;
            try {
                state->renderer->orbit(
                    static_cast<float>(delta_x) * 0.008F,
                    static_cast<float>(-delta_y) * 0.008F);
                InvalidateRect(window, nullptr, FALSE);
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (state != nullptr && state->orbit_drag_active) {
            state->orbit_drag_active = false;
            if (GetCapture() == window) {
                ReleaseCapture();
            }
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (state != nullptr) {
            state->orbit_drag_active = false;
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        if (state != nullptr && state->renderer) {
            try {
                state->renderer->render();
            } catch (const std::exception& error) {
                remember_window_error(window, *state, error.what());
            }
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

[[nodiscard]] std::wstring make_window_title(
    const RuntimeArguments& arguments,
    const LoadedSceneGeometry& geometry) {
    if (arguments.all_records) {
        if (arguments.entrypoint !=
            openrc::kSceneBlockSourceGeometryEntrypointV1) {
            return std::wstring(L"OpenRC - Level ") +
                std::to_wstring(arguments.level_id) +
                L", all records - GS " +
                std::to_wstring(geometry.raster_record_count) + L"/" +
                std::to_wstring(geometry.total_record_count) + L", " +
                std::to_wstring(geometry.raster.emitted_triangle_count) +
                L" triangles";
        }
        const auto non_drawing_record_count =
            geometry.total_record_count - geometry.raster_record_count;
        const auto source_triangle_count = geometry.terrain_triangle_count;
        return std::wstring(L"OpenRC - Level ") +
            std::to_wstring(arguments.level_id) +
            L", all records - source " +
            std::to_wstring(geometry.source_record_count) + L", GS-only " +
            std::to_wstring(geometry.unavailable_source_record_count) +
            L", non-drawing " +
            std::to_wstring(non_drawing_record_count) + L", triangles " +
            std::to_wstring(source_triangle_count) + L" source + " +
            std::to_wstring(geometry.moby_triangle_count) +
            L" static-Moby + " +
            std::to_wstring(geometry.tie_triangle_count) +
            L" TIE, terrain-textured " +
            std::to_wstring(geometry.terrain_textured_triangle_count) + L"/" +
            std::to_wstring(source_triangle_count) + L", placements " +
            std::to_wstring(geometry.moby_rendered_placement_count) + L"/" +
            std::to_wstring(geometry.moby_placement_count) + L" Moby + " +
            std::to_wstring(geometry.tie_rendered_placement_count) + L"/" +
            std::to_wstring(geometry.tie_placement_count) + L" TIE, " +
            std::to_wstring(geometry.raster.emitted_triangle_count) + L" GS";
    }
    return std::wstring(L"OpenRC - Level ") +
        std::to_wstring(arguments.level_id) + L", record " +
        std::to_wstring(arguments.record_index) + L", entry " +
        std::to_wstring(arguments.entrypoint) + L" - " +
        std::to_wstring(geometry.raster.emitted_triangle_count) +
        L" triangles, terrain-textured " +
        std::to_wstring(geometry.terrain_textured_triangle_count) + L"/" +
        std::to_wstring(geometry.source
                            ? geometry.source->emitted_triangle_count
                            : 0U);
}

[[nodiscard]] HWND create_runtime_window(
    const HINSTANCE instance,
    WindowState& state,
    const std::wstring& title) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&window_class) == 0U) {
        throw std::runtime_error("RegisterClassExW failed");
    }

    constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
    RECT rectangle{0, 0, 1280, 720};
    if (AdjustWindowRectEx(&rectangle, kWindowStyle, FALSE, 0U) == FALSE) {
        throw std::runtime_error("AdjustWindowRectEx failed");
    }
    const auto window = CreateWindowExW(
        0U,
        kWindowClassName,
        title.c_str(),
        kWindowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top,
        nullptr,
        nullptr,
        instance,
        &state);
    if (window == nullptr) {
        throw std::runtime_error("CreateWindowExW failed");
    }
    return window;
}

constexpr wchar_t kUsageText[] =
    L"OpenRC native level viewer\n\n"
    L"Required:\n"
    L"  --disc-image <disc.iso>\n"
    L"  --boot-executable <prepared ELF>\n\n"
    L"Optional:\n"
    L"  --level <0..18>\n"
    L"  --record <index|all>\n"
    L"  --entry-pair <6,8,10,14,16,20>\n\n"
    L"Developer verification:\n"
    L"  --smoke-test - render one hidden frame and exit\n\n"
    L"Using --record all independently executes and merges every supported "
    L"record in the selected level.\n\n"
    L"Recovered 3D debug view (entry 16):\n"
    L"  drag or arrow keys - orbit\n"
    L"  mouse wheel or +/- - zoom\n"
    L"  R - reset camera\n"
    L"  Tab - compare decoded GS output\n";

} // namespace

int WINAPI wWinMain(
    const HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    const int show_command) {
    bool suppress_error_ui = false;
    try {
        const auto arguments = parse_arguments();
        suppress_error_ui = arguments.smoke_test;
        if (arguments.show_help) {
            MessageBoxW(
                nullptr,
                kUsageText,
                kApplicationName,
                MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        auto geometry = load_scene_geometry(arguments);
        attach_static_environment_geometry(arguments, geometry);
        WindowState state;
        state.base_title = make_window_title(arguments, geometry);
        const auto window = create_runtime_window(
            instance,
            state,
            state.base_title);
        try {
            if (geometry.source) {
                if (geometry.tfrag_texture_bank &&
                    !geometry.terrain_material_batches.empty()) {
                    std::vector<
                        openrc::runtime::D3d11ObjectTextureSourceV1>
                        object_texture_sources;
                    object_texture_sources.reserve(2U);
                    if (geometry.moby_texture_bank &&
                        geometry.moby_first_triangle) {
                        object_texture_sources.push_back({
                            *geometry.moby_first_triangle,
                            geometry.moby_triangle_count,
                            geometry.moby_material_batches,
                            geometry.moby_texture_bank->textures,
                        });
                    }
                    if (geometry.tie_texture_bank &&
                        geometry.tie_first_triangle) {
                        object_texture_sources.push_back({
                            *geometry.tie_first_triangle,
                            geometry.tie_triangle_count,
                            geometry.tie_material_batches,
                            geometry.tie_texture_bank->textures,
                        });
                    }
                    openrc::runtime::D3d11SourceTextureSourcesV1 textures{
                        {
                            geometry.terrain_triangle_count,
                            geometry.terrain_material_batches,
                            geometry.tfrag_texture_bank->textures,
                        },
                        object_texture_sources,
                    };
                    state.renderer =
                        std::make_unique<openrc::runtime::D3d11Renderer>(
                            window,
                            geometry.raster,
                            *geometry.source,
                            textures);
                } else {
                    state.renderer =
                        std::make_unique<openrc::runtime::D3d11Renderer>(
                            window, geometry.raster, *geometry.source);
                }
            } else {
                state.renderer =
                    std::make_unique<openrc::runtime::D3d11Renderer>(
                        window, geometry.raster);
            }
            refresh_window_title(window, state);
        } catch (...) {
            DestroyWindow(window);
            throw;
        }

        if (arguments.smoke_test) {
            state.renderer->render();
            DestroyWindow(window);
            return 0;
        }

        ShowWindow(window, show_command == 0 ? SW_SHOWNORMAL : show_command);
        UpdateWindow(window);

        MSG message{};
        for (;;) {
            const auto result = GetMessageW(&message, nullptr, 0U, 0U);
            if (result == -1) {
                throw std::runtime_error("GetMessageW failed");
            }
            if (result == 0) {
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (state.fatal_error) {
            throw std::runtime_error(*state.fatal_error);
        }
        return static_cast<int>(message.wParam);
    } catch (const std::exception& error) {
        if (suppress_error_ui) {
            OutputDebugStringW(utf8_to_wide(error.what()).c_str());
            return 1;
        }
        MessageBoxW(
            nullptr,
            utf8_to_wide(error.what()).c_str(),
            kApplicationName,
            MB_OK | MB_ICONERROR);
        return 1;
    }
}
