#include "level_scene_render_compile.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openrc::runtime {
namespace {

constexpr std::uint32_t kMaximumCompiledTextures = 4096U;
constexpr std::uint32_t kMaximumCompiledMaterials =
    kMaximumCompiledTextures + 1U;
constexpr std::uint32_t kMaximumCompiledMeshes = 3U;
constexpr std::uint32_t kMaximumCompiledDrawRanges = 3'000'000U;
constexpr std::uint32_t kMaximumCompiledInstances = 3U;
constexpr std::uint32_t kMaximumTextureDimension = 4096U;
constexpr std::uint64_t kMaximumTexelsPerTexture =
    UINT64_C(16) * 1024U * 1024U;
constexpr std::uint64_t kMaximumCompiledVertices = 3'000'000U;
constexpr std::uint64_t kMaximumCompiledIndices = 9'000'000U;
constexpr std::uint64_t kMaximumCompiledRgbaBytes =
    UINT64_C(256) * 1024U * 1024U;

enum class SceneFamilyV1 : std::uint8_t {
    terrain = 0U,
    moby = 1U,
    tie = 2U,
};

struct SourceMaterialBatchV1 {
    std::uint64_t first_triangle = 0U;
    std::uint64_t index_count = 0U;
    std::optional<std::uint32_t> texture_index;
};

struct SourceFamilyV1 {
    SceneFamilyV1 family = SceneFamilyV1::terrain;
    const char* description = nullptr;
    std::uint64_t first_triangle = 0U;
    std::uint64_t triangle_count = 0U;
    std::vector<SourceMaterialBatchV1> material_batches;
    const RacLevelMobyTextureBankV1* texture_bank = nullptr;
};

struct CompileStateV1 {
    explicit CompileStateV1(const RenderSceneLimitsV1 selected_limits)
        : limits(selected_limits) {}

    RenderSceneV1 scene;
    RenderSceneLimitsV1 limits;
    std::map<std::pair<SceneFamilyV1, std::uint32_t>, std::uint32_t>
        texture_ids;
    std::map<std::uint32_t, std::uint32_t> textured_material_ids;
    std::optional<std::uint32_t> fallback_material_id;
    std::uint64_t total_rgba8_bytes = 0U;
    std::uint64_t total_vertices = 0U;
    std::uint64_t total_indices = 0U;
    std::uint64_t total_draw_ranges = 0U;
};

[[noreturn]] void fail(const std::string& message) {
    throw LevelSceneRenderCompileError(message);
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string(description) + " overflows uint64_t");
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string(description) + " overflows uint64_t");
    }
    return left * right;
}

[[nodiscard]] std::size_t host_size(
    const std::uint64_t value,
    const char* const description) {
    if (value > std::numeric_limits<std::size_t>::max()) {
        fail(std::string(description) + " exceeds host size_t");
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::vector<SourceMaterialBatchV1> terrain_batches(
    const std::vector<SceneMaterialBatchV1>& batches) {
    std::vector<SourceMaterialBatchV1> result;
    result.reserve(batches.size());
    for (const auto& batch : batches) {
        result.push_back(SourceMaterialBatchV1{
            batch.first_triangle, batch.index_count, batch.texture_index});
    }
    return result;
}

[[nodiscard]] std::vector<SourceMaterialBatchV1> environment_batches(
    const std::vector<MobySceneMaterialBatchV1>& batches) {
    std::vector<SourceMaterialBatchV1> result;
    result.reserve(batches.size());
    for (const auto& batch : batches) {
        result.push_back(SourceMaterialBatchV1{
            batch.first_triangle,
            batch.index_count,
            batch.global_texture_index});
    }
    return result;
}

void validate_batch_partition(const SourceFamilyV1& family) {
    if (family.material_batches.empty()) {
        return;
    }

    std::uint64_t expected_triangle = 0U;
    for (const auto& batch : family.material_batches) {
        if (batch.first_triangle != expected_triangle ||
            batch.index_count == 0U || (batch.index_count % 3U) != 0U) {
            fail(std::string("The ") + family.description +
                 " material batches are not a contiguous triangle partition");
        }
        expected_triangle = checked_add(
            expected_triangle,
            batch.index_count / 3U,
            "A recovered material-batch endpoint");
        if (expected_triangle > family.triangle_count) {
            fail(std::string("The ") + family.description +
                 " material batches exceed their geometry family");
        }
    }
    if (expected_triangle != family.triangle_count) {
        fail(std::string("The ") + family.description +
             " material batches do not cover their geometry family exactly");
    }
}

[[nodiscard]] std::vector<SourceFamilyV1> validate_recovered_geometry(
    const LevelSceneRecoveryResultV1& recovered,
    const RenderSceneLimitsV1& limits) {
    if (!recovered.source) {
        fail("The recovered level has no source geometry to compile");
    }
    const auto& source = *recovered.source;
    if (source.triangle_indices.empty() ||
        (source.triangle_indices.size() % 3U) != 0U) {
        fail("The recovered source geometry is empty or not triangle-aligned");
    }
    if (source.vertices.empty() ||
        source.vertices.size() > limits.max_vertices ||
        source.vertices.size() >=
            std::numeric_limits<std::uint32_t>::max()) {
        fail("The recovered source vertex table is empty or exceeds the "
             "RenderSceneV1/32-bit vertex limit");
    }
    if (source.triangle_indices.size() > limits.max_triangle_indices) {
        fail("The recovered source geometry exceeds the RenderSceneV1 index limit");
    }

    const auto total_triangle_count =
        static_cast<std::uint64_t>(source.triangle_indices.size() / 3U);
    if (source.emitted_triangle_count != total_triangle_count) {
        fail("The recovered source triangle counter disagrees with its index buffer");
    }
    if (recovered.terrain_triangle_count == 0U ||
        recovered.terrain_triangle_count > total_triangle_count) {
        fail("The recovered terrain segment is empty or exceeds source geometry");
    }

    const auto moby_present = recovered.moby_first_triangle.has_value();
    if (moby_present != (recovered.moby_triangle_count != 0U)) {
        fail("The recovered Moby segment presence and triangle count disagree");
    }
    if (!moby_present && !recovered.moby_material_batches.empty()) {
        fail("The recovered level has Moby materials without a Moby segment");
    }

    const auto tie_present = recovered.tie_first_triangle.has_value();
    if (tie_present != (recovered.tie_triangle_count != 0U)) {
        fail("The recovered TIE segment presence and triangle count disagree");
    }
    if (!tie_present && !recovered.tie_material_batches.empty()) {
        fail("The recovered level has TIE materials without a TIE segment");
    }

    std::uint64_t expected_triangle = recovered.terrain_triangle_count;
    if (moby_present) {
        if (*recovered.moby_first_triangle != expected_triangle) {
            fail("The recovered Moby segment does not immediately follow terrain");
        }
        expected_triangle = checked_add(
            expected_triangle,
            recovered.moby_triangle_count,
            "The recovered terrain/Moby triangle count");
    }
    if (tie_present) {
        if (*recovered.tie_first_triangle != expected_triangle) {
            fail("The recovered TIE segment does not immediately follow prior geometry");
        }
        expected_triangle = checked_add(
            expected_triangle,
            recovered.tie_triangle_count,
            "The recovered terrain/Moby/TIE triangle count");
    }
    if (expected_triangle != total_triangle_count) {
        fail("The recovered family segments do not cover source geometry exactly");
    }

    for (const auto index : source.triangle_indices) {
        if (index >= source.vertices.size()) {
            fail("A recovered source index references a missing global vertex");
        }
    }

    std::vector<SourceFamilyV1> families;
    families.reserve(3U);
    families.push_back(SourceFamilyV1{
        SceneFamilyV1::terrain,
        "terrain",
        0U,
        recovered.terrain_triangle_count,
        terrain_batches(recovered.terrain_material_batches),
        recovered.tfrag_texture_bank ? &*recovered.tfrag_texture_bank
                                     : nullptr});
    if (moby_present) {
        families.push_back(SourceFamilyV1{
            SceneFamilyV1::moby,
            "Moby",
            *recovered.moby_first_triangle,
            recovered.moby_triangle_count,
            environment_batches(recovered.moby_material_batches),
            recovered.moby_texture_bank ? &*recovered.moby_texture_bank
                                        : nullptr});
    }
    if (tie_present) {
        families.push_back(SourceFamilyV1{
            SceneFamilyV1::tie,
            "TIE",
            *recovered.tie_first_triangle,
            recovered.tie_triangle_count,
            environment_batches(recovered.tie_material_batches),
            recovered.tie_texture_bank ? &*recovered.tie_texture_bank
                                       : nullptr});
    }

    if (families.size() > limits.max_meshes ||
        families.size() > limits.max_instances) {
        fail("The recovered geometry-family count exceeds RenderSceneV1 limits");
    }
    for (const auto& family : families) {
        validate_batch_partition(family);
    }
    return families;
}

[[nodiscard]] std::uint32_t resolve_texture_id(
    CompileStateV1& state,
    const SourceFamilyV1& family,
    const std::uint32_t source_index) {
    const auto key = std::pair{family.family, source_index};
    if (const auto found = state.texture_ids.find(key);
        found != state.texture_ids.end()) {
        return found->second;
    }
    if (family.texture_bank == nullptr) {
        fail(std::string("A textured ") + family.description +
             " batch has no decoded texture bank");
    }
    if (source_index >= family.texture_bank->textures.size()) {
        fail(std::string("A ") + family.description +
             " batch references a missing texture");
    }
    const auto& source_texture = family.texture_bank->textures[source_index];
    if (source_texture.global_index != source_index) {
        fail(std::string("A referenced ") + family.description +
             " texture global index disagrees with its bank position");
    }
    if (source_texture.entry.width <= 0 ||
        source_texture.entry.height <= 0) {
        fail(std::string("A referenced ") + family.description +
             " texture has invalid dimensions");
    }

    const auto width = static_cast<std::uint32_t>(source_texture.entry.width);
    const auto height = static_cast<std::uint32_t>(source_texture.entry.height);
    const auto texel_count = checked_multiply(
        width, height, "A referenced source texture texel count");
    const auto rgba8_bytes = checked_multiply(
        texel_count, 4U, "A referenced source texture RGBA8 byte count");
    if (rgba8_bytes != source_texture.rgba.size()) {
        fail(std::string("A referenced ") + family.description +
             " texture has an inexact RGBA8 payload");
    }
    if (width > state.limits.max_texture_width ||
        height > state.limits.max_texture_height ||
        texel_count > state.limits.max_texels_per_texture) {
        fail("A referenced source texture exceeds RenderSceneV1 dimension limits");
    }
    if (state.scene.textures.size() >= state.limits.max_textures ||
        state.scene.textures.size() >= state.limits.max_total_texture_mips ||
        state.scene.textures.size() >=
            std::numeric_limits<std::uint32_t>::max()) {
        fail("Referenced source textures exceed RenderSceneV1 table limits");
    }
    const auto new_total_rgba8_bytes = checked_add(
        state.total_rgba8_bytes,
        rgba8_bytes,
        "The compiled RenderSceneV1 RGBA8 byte count");
    if (new_total_rgba8_bytes > state.limits.max_total_rgba8_bytes) {
        fail("Referenced source textures exceed the RenderSceneV1 RGBA8 limit");
    }

    const auto id = static_cast<std::uint32_t>(state.scene.textures.size());
    RenderSceneTextureV1 texture;
    texture.id = id;
    texture.color_space = RenderSceneTextureColorSpaceV1::srgb;
    texture.mips.push_back(
        RenderSceneTextureMipV1{width, height, source_texture.rgba});
    state.scene.textures.push_back(std::move(texture));
    state.texture_ids.emplace(key, id);
    state.total_rgba8_bytes = new_total_rgba8_bytes;
    return id;
}

[[nodiscard]] std::uint32_t append_material(
    CompileStateV1& state,
    const std::optional<std::uint32_t> texture_id) {
    if (state.scene.materials.size() >= state.limits.max_materials ||
        state.scene.materials.size() >=
            std::numeric_limits<std::uint32_t>::max()) {
        fail("Compiled materials exceed RenderSceneV1 table limits");
    }
    const auto id = static_cast<std::uint32_t>(state.scene.materials.size());
    RenderSceneMaterialV1 material;
    material.id = id;
    material.base_color_texture_id = texture_id;
    material.base_color_rgba8 = UINT32_C(0xffffffff);
    material.use_vertex_color = true;
    material.double_sided = true;
    if (texture_id) {
        material.alpha_mode = RenderSceneAlphaModeV1::mask;
        // RAC1's current native preview discards only fully transparent texels.
        material.alpha_cutoff_rgba8 = 1U;
    }
    state.scene.materials.push_back(material);
    return id;
}

[[nodiscard]] std::uint32_t resolve_material_id(
    CompileStateV1& state,
    const SourceFamilyV1& family,
    const std::optional<std::uint32_t> source_texture_index) {
    if (!source_texture_index) {
        if (!state.fallback_material_id) {
            state.fallback_material_id = append_material(state, std::nullopt);
        }
        return *state.fallback_material_id;
    }

    const auto texture_id =
        resolve_texture_id(state, family, *source_texture_index);
    if (const auto found = state.textured_material_ids.find(texture_id);
        found != state.textured_material_ids.end()) {
        return found->second;
    }
    const auto material_id = append_material(state, texture_id);
    state.textured_material_ids.emplace(texture_id, material_id);
    return material_id;
}

void append_draw_range(
    CompileStateV1& state,
    RenderSceneMeshV1& mesh,
    const std::uint32_t material_id,
    const std::uint64_t first_index,
    const std::uint64_t index_count) {
    if (!mesh.draw_ranges.empty()) {
        auto& previous = mesh.draw_ranges.back();
        const auto previous_end = checked_add(
            previous.first_index,
            previous.index_count,
            "A compiled draw-range endpoint");
        if (previous.material_id == material_id &&
            previous_end == first_index) {
            previous.index_count = checked_add(
                previous.index_count,
                index_count,
                "A merged compiled draw-range size");
            return;
        }
    }
    if (state.total_draw_ranges >= state.limits.max_draw_ranges) {
        fail("Compiled draw ranges exceed the RenderSceneV1 limit");
    }
    mesh.draw_ranges.push_back(
        RenderSceneDrawRangeV1{material_id, first_index, index_count});
    ++state.total_draw_ranges;
}

void build_draw_ranges(
    CompileStateV1& state,
    RenderSceneMeshV1& mesh,
    const SourceFamilyV1& family) {
    const auto family_index_count = checked_multiply(
        family.triangle_count, 3U, "A geometry-family index count");
    if (family.material_batches.empty()) {
        append_draw_range(
            state,
            mesh,
            resolve_material_id(state, family, std::nullopt),
            0U,
            family_index_count);
        return;
    }

    for (const auto& batch : family.material_batches) {
        append_draw_range(
            state,
            mesh,
            resolve_material_id(state, family, batch.texture_index),
            checked_multiply(
                batch.first_triangle,
                3U,
                "A source material-batch first index"),
            batch.index_count);
    }
}

void append_family_mesh(
    CompileStateV1& state,
    const SceneGeometry3dV1& source,
    const SourceFamilyV1& family,
    const float source_units_per_world_unit) {
    const auto first_index = checked_multiply(
        family.first_triangle, 3U, "A source geometry-family first index");
    const auto index_count = checked_multiply(
        family.triangle_count, 3U, "A source geometry-family index count");
    const auto end_index = checked_add(
        first_index, index_count, "A source geometry-family index endpoint");
    if (end_index > source.triangle_indices.size()) {
        fail("A recovered geometry family lies outside the source index buffer");
    }
    if (state.total_indices > state.limits.max_triangle_indices ||
        index_count > state.limits.max_triangle_indices - state.total_indices) {
        fail("Compiled family indices exceed the RenderSceneV1 limit");
    }

    RenderSceneMeshV1 mesh;
    mesh.id = static_cast<std::uint32_t>(state.scene.meshes.size());
    mesh.triangle_indices.reserve(host_size(
        index_count, "A compiled geometry-family index count"));
    std::vector<std::uint32_t> global_to_local(
        source.vertices.size(), std::numeric_limits<std::uint32_t>::max());

    const auto begin = host_size(first_index, "A source family first index");
    const auto end = host_size(end_index, "A source family end index");
    for (auto cursor = begin; cursor < end; ++cursor) {
        const auto global_index = source.triangle_indices[cursor];
        auto& local_index = global_to_local[global_index];
        if (local_index == std::numeric_limits<std::uint32_t>::max()) {
            if (mesh.vertices.size() >=
                    std::numeric_limits<std::uint32_t>::max() ||
                state.total_vertices >= state.limits.max_vertices) {
                fail("Compiled vertices exceed RenderSceneV1 limits");
            }
            local_index = static_cast<std::uint32_t>(mesh.vertices.size());
            const auto& vertex = source.vertices[global_index];
            mesh.vertices.push_back(RenderSceneVertexV1{
                vertex.x / source_units_per_world_unit,
                vertex.y / source_units_per_world_unit,
                vertex.z / source_units_per_world_unit,
                vertex.u,
                vertex.v,
                vertex.rgba});
            ++state.total_vertices;
        }
        mesh.triangle_indices.push_back(local_index);
    }
    state.total_indices += index_count;
    build_draw_ranges(state, mesh, family);

    RenderSceneInstanceV1 instance;
    instance.id = static_cast<std::uint32_t>(state.scene.instances.size());
    instance.mesh_id = mesh.id;
    state.scene.meshes.push_back(std::move(mesh));
    state.scene.instances.push_back(instance);
}

[[nodiscard]] RenderSceneV1 compile_level_scene_render_impl(
    const LevelSceneRecoveryResultV1& recovered,
    const LevelSceneRenderCompileProfileV1& profile) {
    if (!std::isfinite(profile.source_units_per_world_unit) ||
        profile.source_units_per_world_unit <= 0.0F) {
        fail("The source-units-per-world-unit scale must be finite and positive");
    }

    // This validates every caller limit before any source-dependent allocation.
    validate_render_scene_v1(RenderSceneV1{}, profile.render_scene_limits);
    const auto families = validate_recovered_geometry(
        recovered, profile.render_scene_limits);

    CompileStateV1 state(profile.render_scene_limits);
    for (const auto& family : families) {
        append_family_mesh(
            state,
            *recovered.source,
            family,
            profile.source_units_per_world_unit);
    }
    return canonicalize_render_scene_v1(
        std::move(state.scene), profile.render_scene_limits);
}

} // namespace

LevelSceneRenderCompileProfileV1
make_level_scene_render_compile_profile_v1() {
    LevelSceneRenderCompileProfileV1 result;
    result.source_units_per_world_unit = kSceneBlockUnitsPerWorldUnitV1;
    result.render_scene_limits = RenderSceneLimitsV1{
        kMaximumCompiledTextures,
        1U,
        kMaximumCompiledTextures,
        kMaximumCompiledMaterials,
        kMaximumCompiledMeshes,
        kMaximumCompiledDrawRanges,
        kMaximumCompiledInstances,
        kMaximumTextureDimension,
        kMaximumTextureDimension,
        kMaximumTexelsPerTexture,
        kMaximumCompiledVertices,
        kMaximumCompiledIndices,
        kMaximumCompiledRgbaBytes,
    };
    return result;
}

RenderSceneV1 compile_level_scene_render_v1(
    const LevelSceneRecoveryResultV1& recovered,
    const LevelSceneRenderCompileProfileV1& profile) {
    try {
        return compile_level_scene_render_impl(recovered, profile);
    } catch (const LevelSceneRenderCompileError&) {
        throw;
    } catch (const RenderSceneError& error) {
        throw LevelSceneRenderCompileError(
            std::string("Cannot compile recovered RenderSceneV1: ") +
            error.what());
    }
}

} // namespace openrc::runtime
