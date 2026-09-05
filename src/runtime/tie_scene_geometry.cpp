#include "tie_scene_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace openrc::runtime {
namespace {

constexpr std::uint32_t kMissingIndex =
    std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kTieDiagnosticColor = 0xffffffffU;

[[noreturn]] void fail(const std::string& message) {
    throw TieSceneGeometryError(message);
}

void require_limit(
    const std::uint64_t value,
    const std::uint64_t limit,
    const char* const description) {
    if (value > limit) {
        fail(std::string(description) + " exceeds the caller's limit");
    }
}

void require_append_capacity(
    const std::uint64_t current,
    const std::uint64_t additional,
    const std::uint64_t limit,
    const std::size_t host_limit,
    const char* const description) {
    if (current > limit || additional > limit - current ||
        current > static_cast<std::uint64_t>(host_limit) ||
        additional > static_cast<std::uint64_t>(host_limit) - current ||
        current >= kMissingIndex || additional >= kMissingIndex - current) {
        fail(std::string(description) + " exceeds its bounded output capacity");
    }
}

struct PreparedModelV1 {
    std::vector<std::uint32_t> used_vertex_indices;
    std::vector<std::uint32_t> compact_triangle_indices;
    std::vector<MobySceneMaterialBatchV1> material_batches;
};

[[nodiscard]] std::uint32_t resolve_global_texture_index(
    const RacLevelTieModelV1& model,
    const std::uint32_t local_texture_index) {
    if (model.used_texture_slot_count > model.texture_slots.size() ||
        model.high_lod.texture_count > model.used_texture_slot_count ||
        local_texture_index >= model.high_lod.texture_count ||
        local_texture_index >= model.used_texture_slot_count ||
        local_texture_index >= model.texture_slots.size()) {
        fail("A TIE scene triangle references an unavailable texture slot");
    }
    const auto global_texture_index = model.texture_slots[local_texture_index];
    if (global_texture_index == kRacLevelCoreUnusedTextureSlotV1) {
        fail("A TIE scene triangle references an unused texture slot");
    }
    return global_texture_index;
}

void append_material_batch(
    std::vector<MobySceneMaterialBatchV1>& batches,
    const std::uint64_t first_triangle,
    const std::uint64_t index_count,
    const std::uint32_t global_texture_index) {
    if (index_count == 0U || index_count % 3U != 0U) {
        fail("A TIE scene material batch has an invalid index count");
    }
    if (first_triangle >
        std::numeric_limits<std::uint64_t>::max() - index_count / 3U) {
        fail("A TIE scene material batch range overflows");
    }
    if (batches.empty()) {
        if (first_triangle != 0U) {
            fail("The first TIE scene material batch does not begin at zero");
        }
    } else {
        auto& previous = batches.back();
        if (previous.index_count == 0U || previous.index_count % 3U != 0U ||
            previous.first_triangle >
                std::numeric_limits<std::uint64_t>::max() -
                    previous.index_count / 3U) {
            fail("The preceding TIE scene material batch is invalid");
        }
        const auto expected_first =
            previous.first_triangle + previous.index_count / 3U;
        if (first_triangle != expected_first) {
            fail("TIE scene material batches are not contiguous");
        }
        if (previous.global_texture_index == global_texture_index) {
            if (index_count >
                std::numeric_limits<std::uint64_t>::max() -
                    previous.index_count) {
                fail("A TIE scene material batch index count overflows");
            }
            previous.index_count += index_count;
            return;
        }
    }
    if (batches.size() >= batches.max_size()) {
        fail("The TIE scene material batch count exceeds the host container");
    }
    batches.push_back(MobySceneMaterialBatchV1{
        first_triangle,
        index_count,
        global_texture_index,
    });
}

[[nodiscard]] PreparedModelV1 prepare_model(
    const RacLevelTieModelV1& model,
    const TieSceneGeometryLimitsV1 limits) {
    PreparedModelV1 result;
    if (model.high_lod.triangles.empty()) {
        return result;
    }
    require_limit(
        model.high_lod.vertices.size(),
        limits.max_workspace_vertices,
        "A TIE scene model's vertex workspace");
    require_limit(
        model.high_lod.triangles.size(),
        limits.max_workspace_triangle_indices / 3U,
        "A TIE scene model's triangle workspace");
    if (model.high_lod.vertices.empty() ||
        model.high_lod.vertices.size() >= kMissingIndex ||
        model.high_lod.triangles.size() >
            result.compact_triangle_indices.max_size() / 3U) {
        fail("A TIE scene model has an invalid geometry domain");
    }

    std::vector<std::uint32_t> remap(
        model.high_lod.vertices.size(), kMissingIndex);
    result.compact_triangle_indices.reserve(
        model.high_lod.triangles.size() * 3U);
    result.material_batches.reserve(model.high_lod.triangles.size());
    for (const auto& triangle : model.high_lod.triangles) {
        const auto first_triangle =
            result.compact_triangle_indices.size() / 3U;
        const auto global_texture_index = resolve_global_texture_index(
            model, triangle.local_texture_index);
        for (const auto source_index : triangle.vertex_indices) {
            if (source_index >= model.high_lod.vertices.size()) {
                fail("A TIE scene triangle references a missing vertex");
            }
            auto& compact_index = remap[source_index];
            if (compact_index == kMissingIndex) {
                if (result.used_vertex_indices.size() >= kMissingIndex) {
                    fail("A compacted TIE scene model exceeds 32-bit indices");
                }
                compact_index = static_cast<std::uint32_t>(
                    result.used_vertex_indices.size());
                result.used_vertex_indices.push_back(source_index);
            }
            result.compact_triangle_indices.push_back(compact_index);
        }
        append_material_batch(
            result.material_batches,
            first_triangle,
            3U,
            global_texture_index);
    }
    return result;
}

[[nodiscard]] std::array<float, 3U> transform_position(
    const std::array<float, 3U>& position,
    const RacGameplayTieInstanceV1& placement,
    const float output_scale) {
    const auto& matrix = placement.matrix;
    const std::array<float, 3U> transformed{
        output_scale *
            (matrix[0U] * position[0U] + matrix[4U] * position[1U] +
             matrix[8U] * position[2U] + matrix[12U]),
        output_scale *
            (matrix[1U] * position[0U] + matrix[5U] * position[1U] +
             matrix[9U] * position[2U] + matrix[13U]),
        output_scale *
            (matrix[2U] * position[0U] + matrix[6U] * position[1U] +
             matrix[10U] * position[2U] + matrix[14U]),
    };
    if (std::ranges::any_of(
            transformed,
            [](const float value) { return !std::isfinite(value); })) {
        fail("A transformed TIE scene vertex is non-finite");
    }
    return transformed;
}

[[nodiscard]] float coordinate_scale_for(
    const TieSceneCoordinateDomainV1 coordinate_domain) {
    switch (coordinate_domain) {
    case TieSceneCoordinateDomainV1::world_units:
        return 1.0F;
    case TieSceneCoordinateDomainV1::scene_block_itof0_units:
        return kSceneBlockUnitsPerWorldUnitV1;
    }
    fail("The TIE scene coordinate domain is invalid");
}

} // namespace

TieSceneGeometryV1 build_tie_scene_geometry_v1(
    const std::span<const RacLevelTieModelV1> models,
    const std::span<const RacGameplayTieInstanceV1> placements,
    const TieSceneCoordinateDomainV1 coordinate_domain,
    const TieSceneGeometryLimitsV1 limits) {
    if (limits.max_models == 0U || limits.max_instances == 0U ||
        limits.max_workspace_vertices == 0U ||
        limits.max_workspace_triangle_indices == 0U ||
        limits.max_output_vertices == 0U ||
        limits.max_output_triangle_indices == 0U) {
        fail("TIE scene geometry limits must all be non-zero");
    }
    require_limit(models.size(), limits.max_models, "The TIE scene model count");
    require_limit(
        placements.size(), limits.max_instances, "The TIE scene placement count");

    const auto coordinate_scale = coordinate_scale_for(coordinate_domain);
    TieSceneGeometryV1 result;
    result.coordinate_domain = coordinate_domain;
    result.stats.model_count = models.size();
    result.stats.placement_count = placements.size();

    std::unordered_map<std::uint32_t, std::size_t> class_to_model;
    if (models.size() > class_to_model.max_size()) {
        fail("The TIE scene model count exceeds the host lookup container");
    }
    class_to_model.reserve(models.size());
    for (std::size_t index = 0U; index < models.size(); ++index) {
        if (!class_to_model.emplace(models[index].class_id, index).second) {
            fail("The TIE scene model list contains a duplicate class ID");
        }
    }
    std::vector<std::optional<PreparedModelV1>> prepared(models.size());
    std::uint64_t prepared_vertex_count = 0U;
    std::uint64_t prepared_index_count = 0U;

    SceneGeometry3dV1 geometry;
    std::map<std::uint32_t, std::vector<std::uint32_t>> material_indices;
    std::uint64_t output_index_count = 0U;
    std::vector<bool> rendered_models(models.size(), false);
    bool has_bounds = false;
    for (const auto& placement : placements) {
        if (std::ranges::any_of(
                placement.matrix,
                [](const float value) { return !std::isfinite(value); })) {
            fail("A TIE scene placement has a non-finite transform matrix");
        }
        const auto found = class_to_model.find(placement.class_id);
        if (found == class_to_model.end()) {
            ++result.stats.missing_model_placement_count;
            continue;
        }
        const auto model_index = found->second;
        const auto& source = models[model_index].high_lod;
        if (!prepared[model_index]) {
            auto candidate = prepare_model(models[model_index], limits);
            require_append_capacity(
                prepared_vertex_count,
                candidate.used_vertex_indices.size(),
                limits.max_workspace_vertices,
                std::numeric_limits<std::size_t>::max(),
                "The prepared TIE scene vertex workspace");
            require_append_capacity(
                prepared_index_count,
                candidate.compact_triangle_indices.size(),
                limits.max_workspace_triangle_indices,
                std::numeric_limits<std::size_t>::max(),
                "The prepared TIE scene index workspace");
            prepared_vertex_count += candidate.used_vertex_indices.size();
            prepared_index_count += candidate.compact_triangle_indices.size();
            prepared[model_index] = std::move(candidate);
        }
        const auto& compact = *prepared[model_index];
        if (compact.compact_triangle_indices.empty()) {
            ++result.stats.empty_model_placement_count;
            continue;
        }
        require_append_capacity(
            geometry.vertices.size(),
            compact.used_vertex_indices.size(),
            limits.max_output_vertices,
            geometry.vertices.max_size(),
            "The instantiated TIE scene vertex count");
        require_append_capacity(
            output_index_count,
            compact.compact_triangle_indices.size(),
            limits.max_output_triangle_indices,
            geometry.triangle_indices.max_size(),
            "The instantiated TIE scene index count");
        const auto vertex_begin =
            static_cast<std::uint32_t>(geometry.vertices.size());
        for (const auto source_index : compact.used_vertex_indices) {
            const auto& source_vertex = source.vertices[source_index];
            if (std::ranges::any_of(
                    source_vertex.position,
                    [](const float value) { return !std::isfinite(value); }) ||
                std::ranges::any_of(
                    source_vertex.texture_coordinate,
                    [](const float value) { return !std::isfinite(value); })) {
                fail("A TIE scene source vertex is non-finite");
            }
            const auto transformed = transform_position(
                source_vertex.position, placement, coordinate_scale);
            const SceneVertex3dV1 vertex{
                transformed[0U],
                transformed[1U],
                transformed[2U],
                kTieDiagnosticColor,
                source_vertex.texture_coordinate[0U],
                source_vertex.texture_coordinate[1U],
            };
            geometry.vertices.push_back(vertex);
            if (!has_bounds) {
                geometry.minimum_x = vertex.x;
                geometry.maximum_x = vertex.x;
                geometry.minimum_y = vertex.y;
                geometry.maximum_y = vertex.y;
                geometry.minimum_z = vertex.z;
                geometry.maximum_z = vertex.z;
                has_bounds = true;
            } else {
                geometry.minimum_x = std::min(geometry.minimum_x, vertex.x);
                geometry.maximum_x = std::max(geometry.maximum_x, vertex.x);
                geometry.minimum_y = std::min(geometry.minimum_y, vertex.y);
                geometry.maximum_y = std::max(geometry.maximum_y, vertex.y);
                geometry.minimum_z = std::min(geometry.minimum_z, vertex.z);
                geometry.maximum_z = std::max(geometry.maximum_z, vertex.z);
            }
        }
        for (const auto& batch : compact.material_batches) {
            if (!batch.global_texture_index) {
                fail("An instantiated TIE material batch lacks a texture");
            }
            if (batch.first_triangle >
                    compact.compact_triangle_indices.size() / 3U ||
                batch.index_count % 3U != 0U ||
                batch.index_count >
                    compact.compact_triangle_indices.size() -
                        batch.first_triangle * 3U) {
                fail("A prepared TIE material batch exceeds its index buffer");
            }
            auto& bucket = material_indices[*batch.global_texture_index];
            require_append_capacity(
                bucket.size(),
                batch.index_count,
                limits.max_output_triangle_indices,
                bucket.max_size(),
                "A grouped TIE material index buffer");
            const auto begin = static_cast<std::size_t>(
                batch.first_triangle * 3U);
            const auto end = begin + static_cast<std::size_t>(batch.index_count);
            for (auto index = begin; index < end; ++index) {
                bucket.push_back(
                    vertex_begin + compact.compact_triangle_indices[index]);
            }
        }
        output_index_count += compact.compact_triangle_indices.size();
        ++result.stats.rendered_placement_count;
        rendered_models[model_index] = true;
    }

    result.stats.rendered_model_count = static_cast<std::uint64_t>(
        std::ranges::count(rendered_models, true));
    if (has_bounds && output_index_count != 0U) {
        if (output_index_count > geometry.triangle_indices.max_size()) {
            fail("The grouped TIE index buffer exceeds its host container");
        }
        geometry.triangle_indices.reserve(
            static_cast<std::size_t>(output_index_count));
        for (auto& [texture_index, indices] : material_indices) {
            const auto first_triangle = geometry.triangle_indices.size() / 3U;
            geometry.triangle_indices.insert(
                geometry.triangle_indices.end(), indices.begin(), indices.end());
            append_material_batch(
                result.material_batches,
                first_triangle,
                indices.size(),
                texture_index);
        }
        if (geometry.triangle_indices.size() != output_index_count) {
            fail("The grouped TIE material buffers lost triangle indices");
        }
        geometry.emitted_triangle_count = geometry.triangle_indices.size() / 3U;
        if (result.material_batches.empty()) {
            fail("Instantiated TIE geometry has no material batches");
        }
        const auto& final_batch = result.material_batches.back();
        if (final_batch.first_triangle + final_batch.index_count / 3U !=
            geometry.emitted_triangle_count) {
            fail("Instantiated TIE material batches do not cover the geometry");
        }
        result.geometry = std::move(geometry);
    }
    return result;
}

} // namespace openrc::runtime
