#include "moby_scene_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace openrc::runtime {
namespace {

constexpr std::uint32_t kMissingIndex =
    std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kMobyDiagnosticColor =
    255U | (176U << 8U) | (48U << 16U) | (255U << 24U);

[[noreturn]] void fail(const std::string& message) {
    throw MobySceneGeometryError(message);
}

void require_limit(const std::uint64_t value,
                   const std::uint64_t limit,
                   const char* const description) {
    if (value > limit) {
        fail(std::string(description) + " exceeds the caller's limit");
    }
}

void require_append_capacity(const std::uint64_t current,
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

[[nodiscard]] std::optional<std::uint32_t> resolve_global_texture_index(
    const RacLevelMobyModelV1& model,
    const std::int32_t local_texture_index) {
    if (local_texture_index == -1) {
        return std::nullopt;
    }
    if (local_texture_index < -1) {
        fail("A Moby scene triangle has an invalid negative texture slot");
    }
    if (model.used_texture_slot_count > model.texture_slots.size()) {
        fail("A Moby scene model has too many texture slots");
    }
    const auto slot = static_cast<std::uint32_t>(local_texture_index);
    if (slot >= model.used_texture_slot_count ||
        slot >= model.texture_slots.size()) {
        fail("A Moby scene triangle references an unavailable texture slot");
    }
    const auto global_texture_index = model.texture_slots[slot];
    if (global_texture_index == kRacLevelCoreUnusedTextureSlotV1) {
        fail("A Moby scene triangle references an unused texture slot");
    }
    return global_texture_index;
}

void append_material_batch(
    std::vector<MobySceneMaterialBatchV1>& batches,
    const std::uint64_t first_triangle,
    const std::uint64_t index_count,
    const std::optional<std::uint32_t> global_texture_index) {
    if (index_count == 0U || index_count % 3U != 0U) {
        fail("A Moby scene material batch has an invalid index count");
    }
    if (first_triangle >
        std::numeric_limits<std::uint64_t>::max() - index_count / 3U) {
        fail("A Moby scene material batch range overflows");
    }
    if (batches.empty()) {
        if (first_triangle != 0U) {
            fail("The first Moby scene material batch does not begin at zero");
        }
    } else {
        auto& previous = batches.back();
        if (previous.index_count == 0U ||
            previous.index_count % 3U != 0U ||
            previous.first_triangle >
                std::numeric_limits<std::uint64_t>::max() -
                    previous.index_count / 3U) {
            fail("The preceding Moby scene material batch is invalid");
        }
        const auto expected_first =
            previous.first_triangle + previous.index_count / 3U;
        if (first_triangle != expected_first) {
            fail("Moby scene material batches are not contiguous");
        }
        if (previous.global_texture_index == global_texture_index) {
            if (index_count >
                std::numeric_limits<std::uint64_t>::max() -
                    previous.index_count) {
                fail("A Moby scene material batch index count overflows");
            }
            previous.index_count += index_count;
            return;
        }
    }
    if (batches.size() >= batches.max_size()) {
        fail("The Moby scene material batch count exceeds the host container");
    }
    batches.push_back(MobySceneMaterialBatchV1{
        first_triangle, index_count, global_texture_index});
}

[[nodiscard]] PreparedModelV1 prepare_model(
    const RacLevelMobyModelV1& model,
    const MobySceneGeometryLimitsV1 limits) {
    PreparedModelV1 result;
    if (model.high_lod.lod != RacMobyLodV1::high) {
        fail("A Moby scene model is not a high-LOD assembly");
    }
    if (model.high_lod.requires_bind_transforms ||
        model.high_lod.triangles.empty()) {
        return result;
    }
    require_limit(model.high_lod.vertices.size(),
                  limits.max_workspace_vertices,
                  "A Moby scene model's vertex workspace");
    require_limit(model.high_lod.triangles.size(),
                  limits.max_workspace_triangle_indices / 3U,
                  "A Moby scene model's triangle workspace");
    if (model.high_lod.vertices.empty() ||
        model.high_lod.vertices.size() >= kMissingIndex) {
        fail("A Moby scene model has an invalid vertex domain");
    }
    if (model.high_lod.triangles.size() >
        result.compact_triangle_indices.max_size() / 3U) {
        fail("A Moby scene model exceeds the host index container");
    }

    std::vector<std::uint32_t> remap(
        model.high_lod.vertices.size(), kMissingIndex);
    result.compact_triangle_indices.reserve(
        model.high_lod.triangles.size() * 3U);
    result.material_batches.reserve(model.high_lod.triangles.size());
    for (const auto& triangle : model.high_lod.triangles) {
        const auto first_triangle =
            result.compact_triangle_indices.size() / 3U;
        const auto global_texture_index =
            resolve_global_texture_index(model, triangle.texture_index);
        for (const auto source_index : triangle.vertex_indices) {
            if (source_index >= model.high_lod.vertices.size()) {
                fail("A Moby scene triangle references a missing vertex");
            }
            auto& compact_index = remap[source_index];
            if (compact_index == kMissingIndex) {
                if (result.used_vertex_indices.size() >= kMissingIndex) {
                    fail("A compacted Moby scene model exceeds 32-bit indices");
                }
                compact_index = static_cast<std::uint32_t>(
                    result.used_vertex_indices.size());
                result.used_vertex_indices.push_back(source_index);
            }
            result.compact_triangle_indices.push_back(compact_index);
        }
        append_material_batch(
            result.material_batches, first_triangle, 3U,
            global_texture_index);
    }
    return result;
}

[[nodiscard]] std::array<float, 3U> transform_position(
    const std::array<float, 3U>& position,
    const RacGameplayMobyInstanceV1& placement,
    const float output_scale) {
    const auto sin_x = std::sin(placement.rotation[0U]);
    const auto cos_x = std::cos(placement.rotation[0U]);
    const auto sin_y = std::sin(placement.rotation[1U]);
    const auto cos_y = std::cos(placement.rotation[1U]);
    const auto sin_z = std::sin(placement.rotation[2U]);
    const auto cos_z = std::cos(placement.rotation[2U]);

    const auto rx_x = position[0U];
    const auto rx_y = cos_x * position[1U] - sin_x * position[2U];
    const auto rx_z = sin_x * position[1U] + cos_x * position[2U];
    const auto ry_x = cos_y * rx_x + sin_y * rx_z;
    const auto ry_y = rx_y;
    const auto ry_z = -sin_y * rx_x + cos_y * rx_z;
    const auto rz_x = cos_z * ry_x - sin_z * ry_y;
    const auto rz_y = sin_z * ry_x + cos_z * ry_y;

    const std::array<float, 3U> result{
        output_scale * (placement.position[0U] + placement.scale * rz_x),
        output_scale * (placement.position[1U] + placement.scale * rz_y),
        output_scale * (placement.position[2U] + placement.scale * ry_z)};
    if (!std::isfinite(result[0U]) || !std::isfinite(result[1U]) ||
        !std::isfinite(result[2U])) {
        fail("A transformed Moby scene vertex is non-finite");
    }
    return result;
}

[[nodiscard]] float coordinate_scale_for(
    const MobySceneCoordinateDomainV1 coordinate_domain) {
    switch (coordinate_domain) {
    case MobySceneCoordinateDomainV1::world_units:
        return 1.0F;
    case MobySceneCoordinateDomainV1::scene_block_itof0_units:
        return kSceneBlockUnitsPerWorldUnitV1;
    }
    fail("The Moby scene coordinate domain is invalid");
}

} // namespace

MobySceneGeometryV1 build_moby_scene_geometry_v1(
    const std::span<const RacLevelMobyModelV1> models,
    const std::span<const RacGameplayMobyInstanceV1> placements,
    const MobySceneCoordinateDomainV1 coordinate_domain,
    const MobySceneGeometryLimitsV1 limits) {
    if (limits.max_models == 0U || limits.max_instances == 0U ||
        limits.max_workspace_vertices == 0U ||
        limits.max_workspace_triangle_indices == 0U ||
        limits.max_output_vertices == 0U ||
        limits.max_output_triangle_indices == 0U) {
        fail("Moby scene geometry limits must all be non-zero");
    }
    require_limit(models.size(), limits.max_models,
                  "The Moby scene model count");
    require_limit(placements.size(), limits.max_instances,
                  "The Moby scene placement count");

    const auto coordinate_scale = coordinate_scale_for(coordinate_domain);
    MobySceneGeometryV1 result;
    result.coordinate_domain = coordinate_domain;
    result.stats.model_count = models.size();
    result.stats.placement_count = placements.size();

    std::unordered_map<std::uint32_t, std::size_t> class_to_model;
    if (models.size() > class_to_model.max_size()) {
        fail("The Moby scene model count exceeds the host lookup container");
    }
    class_to_model.reserve(models.size());
    for (std::size_t index = 0U; index < models.size(); ++index) {
        if (models[index].high_lod.lod != RacMobyLodV1::high) {
            fail("A Moby scene model is not a high-LOD assembly");
        }
        if (!class_to_model.emplace(models[index].class_id, index).second) {
            fail("The Moby scene model list contains a duplicate class ID");
        }
    }
    std::vector<std::optional<PreparedModelV1>> prepared(models.size());
    std::uint64_t prepared_vertex_count = 0U;
    std::uint64_t prepared_index_count = 0U;

    SceneGeometry3dV1 geometry;
    std::vector<bool> rendered_models(models.size(), false);
    bool has_bounds = false;
    for (const auto& placement : placements) {
        if (!std::isfinite(placement.scale) || placement.scale <= 0.0F ||
            std::ranges::any_of(
                placement.position,
                [](const float value) { return !std::isfinite(value); }) ||
            std::ranges::any_of(
                placement.rotation,
                [](const float value) { return !std::isfinite(value); })) {
            fail("A Moby scene placement has an invalid transform");
        }

        const auto found = class_to_model.find(placement.class_id);
        if (found == class_to_model.end()) {
            ++result.stats.missing_model_placement_count;
            continue;
        }
        const auto model_index = found->second;
        const auto& source = models[model_index].high_lod;
        if (source.requires_bind_transforms) {
            ++result.stats.animated_model_placement_count;
            continue;
        }
        if (!prepared[model_index]) {
            auto candidate = prepare_model(models[model_index], limits);
            require_append_capacity(
                prepared_vertex_count, candidate.used_vertex_indices.size(),
                limits.max_workspace_vertices,
                std::numeric_limits<std::size_t>::max(),
                "The prepared Moby scene vertex workspace");
            require_append_capacity(
                prepared_index_count,
                candidate.compact_triangle_indices.size(),
                limits.max_workspace_triangle_indices,
                std::numeric_limits<std::size_t>::max(),
                "The prepared Moby scene index workspace");
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
            geometry.vertices.size(), compact.used_vertex_indices.size(),
            limits.max_output_vertices, geometry.vertices.max_size(),
            "The instantiated Moby scene vertex count");
        require_append_capacity(
            geometry.triangle_indices.size(),
            compact.compact_triangle_indices.size(),
            limits.max_output_triangle_indices,
            geometry.triangle_indices.max_size(),
            "The instantiated Moby scene index count");
        const auto vertex_begin =
            static_cast<std::uint32_t>(geometry.vertices.size());
        const auto first_triangle = geometry.triangle_indices.size() / 3U;
        for (const auto source_index : compact.used_vertex_indices) {
            const auto& source_vertex = source.vertices[source_index];
            if (!std::isfinite(source_vertex.texture_coordinate[0U]) ||
                !std::isfinite(source_vertex.texture_coordinate[1U])) {
                fail("A Moby scene vertex has non-finite texture coordinates");
            }
            const auto transformed = transform_position(
                source_vertex.diagnostic_position, placement,
                coordinate_scale);
            const SceneVertex3dV1 vertex{
                transformed[0U], transformed[1U], transformed[2U],
                kMobyDiagnosticColor,
                source_vertex.texture_coordinate[0U],
                source_vertex.texture_coordinate[1U]};
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
        for (const auto compact_index : compact.compact_triangle_indices) {
            geometry.triangle_indices.push_back(vertex_begin + compact_index);
        }
        for (const auto& batch : compact.material_batches) {
            if (batch.first_triangle >
                std::numeric_limits<std::uint64_t>::max() - first_triangle) {
                fail("An instantiated Moby material batch offset overflows");
            }
            append_material_batch(
                result.material_batches,
                first_triangle + batch.first_triangle,
                batch.index_count,
                batch.global_texture_index);
        }
        ++result.stats.rendered_placement_count;
        rendered_models[model_index] = true;
    }

    result.stats.rendered_model_count = static_cast<std::uint64_t>(
        std::ranges::count(rendered_models, true));
    if (has_bounds && !geometry.triangle_indices.empty()) {
        geometry.emitted_triangle_count =
            geometry.triangle_indices.size() / 3U;
        if (result.material_batches.empty()) {
            fail("Instantiated Moby geometry has no material batches");
        }
        const auto& final_batch = result.material_batches.back();
        if (final_batch.first_triangle + final_batch.index_count / 3U !=
            geometry.emitted_triangle_count) {
            fail("Instantiated Moby material batches do not cover the geometry");
        }
        result.geometry = std::move(geometry);
    }
    return result;
}

} // namespace openrc::runtime
