#include "level_scene_render_compile.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callback>
void expect_compile_error(Callback&& callback, const std::string& message) {
    try {
        std::invoke(std::forward<Callback>(callback));
    } catch (const openrc::runtime::LevelSceneRenderCompileError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] openrc::RacLevelMobyTextureV1 make_texture(
    const std::uint32_t global_index,
    const std::int16_t width,
    const std::int16_t height,
    const std::byte value) {
    openrc::RacLevelMobyTextureV1 result;
    result.global_index = global_index;
    result.entry.width = width;
    result.entry.height = height;
    if (width > 0 && height > 0) {
        const auto texels = static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height);
        result.indices.assign(texels, std::byte{0U});
        result.rgba.assign(texels * 4U, value);
    }
    return result;
}

[[nodiscard]] openrc::RacLevelMobyTextureBankV1 make_bank(
    const std::uint32_t count,
    const std::byte first_value) {
    openrc::RacLevelMobyTextureBankV1 result;
    result.textures.reserve(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        result.textures.push_back(make_texture(
            index,
            static_cast<std::int16_t>(index + 1U),
            1,
            std::byte{static_cast<unsigned char>(
                std::to_integer<unsigned int>(first_value) + index)}));
    }
    return result;
}

[[nodiscard]] openrc::runtime::LevelSceneRecoveryResultV1
make_three_family_recovery() {
    openrc::runtime::SceneGeometry3dV1 source;
    source.vertices = {
        {0.0F, 0.0F, 0.0F, UINT32_C(0xff000001), 0.0F, 0.0F},
        {1024.0F, 0.0F, 0.0F, UINT32_C(0xff000002), 1.0F, 0.0F},
        {0.0F, 2048.0F, 0.0F, UINT32_C(0xff000003), 0.0F, 1.0F},
        {1024.0F, 2048.0F, 1024.0F, UINT32_C(0xff000004), 1.0F, 1.0F},
        {-1024.0F, 0.0F, 0.0F, UINT32_C(0xff000005), -1.0F, 0.0F},
        {0.0F, -1024.0F, 0.0F, UINT32_C(0xff000006), 0.0F, -1.0F},
        {2048.0F, 1024.0F, -1024.0F, UINT32_C(0xff000007), 2.0F, 1.0F},
        {-2048.0F, -1024.0F, 1024.0F, UINT32_C(0xff000008), -2.0F, -1.0F},
    };
    source.triangle_indices = {
        4U, 2U, 0U,
        2U, 1U, 0U,
        5U, 1U, 3U,
        7U, 6U, 0U,
    };
    source.emitted_triangle_count = 4U;

    openrc::runtime::LevelSceneRecoveryResultV1 result;
    result.source = std::move(source);
    result.terrain_triangle_count = 2U;
    result.terrain_material_batches = {
        {0U, 3U, 1U},
        {1U, 3U, std::nullopt},
    };
    result.tfrag_texture_bank = make_bank(3U, std::byte{0x10});

    result.moby_first_triangle = 2U;
    result.moby_triangle_count = 1U;
    result.moby_material_batches = {{0U, 3U, 2U}};
    result.moby_texture_bank = make_bank(3U, std::byte{0x20});

    result.tie_first_triangle = 3U;
    result.tie_triangle_count = 1U;
    result.tie_material_batches = {{0U, 3U, 0U}};
    result.tie_texture_bank = make_bank(2U, std::byte{0x30});
    return result;
}

[[nodiscard]] openrc::runtime::LevelSceneRecoveryResultV1
make_fallback_recovery() {
    openrc::runtime::SceneGeometry3dV1 source;
    source.vertices = {
        {-1024.0F, 0.0F, 0.0F, UINT32_C(0xff102030), 0.0F, 0.0F},
        {1024.0F, 0.0F, 0.0F, UINT32_C(0xff405060), 1.0F, 0.0F},
        {0.0F, 1024.0F, 0.0F, UINT32_C(0xff708090), 0.5F, 1.0F},
    };
    source.triangle_indices = {2U, 0U, 1U};
    source.emitted_triangle_count = 1U;

    openrc::runtime::LevelSceneRecoveryResultV1 result;
    result.source = std::move(source);
    result.terrain_triangle_count = 1U;
    return result;
}

[[nodiscard]] openrc::runtime::LevelSceneRenderCompileProfileV1 profile() {
    return openrc::runtime::make_level_scene_render_compile_profile_v1();
}

void test_compiles_three_dense_family_meshes() {
    const auto scene = openrc::runtime::compile_level_scene_render_v1(
        make_three_family_recovery(), profile());

    expect(scene.schema_version == openrc::kRenderSceneSchemaVersionV1,
           "compiled render-scene schema is wrong");
    expect(scene.textures.size() == 3U,
           "compiler did not keep exactly the three referenced textures");
    expect(scene.materials.size() == 4U,
           "compiler did not create three textured materials plus fallback");
    expect(scene.meshes.size() == 3U && scene.instances.size() == 3U,
           "compiler did not preserve the three geometry families");

    for (std::size_t index = 0U; index < 3U; ++index) {
        expect(scene.textures[index].id == index,
               "compiled texture IDs are not dense");
        expect(scene.meshes[index].id == index,
               "compiled mesh IDs are not dense");
        expect(scene.instances[index].id == index &&
                   scene.instances[index].mesh_id == index,
               "compiled instance IDs/references are not dense");
        expect(scene.instances[index].local_to_world ==
                   openrc::RenderSceneAffine3x4V1{},
               "a baked family instance is not identity");
    }
    for (std::size_t index = 0U; index < scene.materials.size(); ++index) {
        expect(scene.materials[index].id == index,
               "compiled material IDs are not dense");
        expect(scene.materials[index].use_vertex_color &&
                   scene.materials[index].double_sided,
               "compiled material discarded vertex color or sidedness policy");
    }

    expect(scene.textures[0U].mips[0U].rgba8.front() == std::byte{0x11} &&
               scene.textures[1U].mips[0U].rgba8.front() == std::byte{0x22} &&
               scene.textures[2U].mips[0U].rgba8.front() == std::byte{0x30},
           "compiler selected a texture from the wrong family bank");
    for (const auto& texture : scene.textures) {
        expect(texture.color_space ==
                   openrc::RenderSceneTextureColorSpaceV1::srgb &&
                   texture.mips.size() == 1U,
               "compiled source texture policy is wrong");
    }

    expect(scene.materials[0U].base_color_texture_id == 0U &&
               !scene.materials[1U].base_color_texture_id &&
               scene.materials[2U].base_color_texture_id == 1U &&
               scene.materials[3U].base_color_texture_id == 2U,
           "compiled material-to-texture mapping is wrong");
    expect(scene.materials[0U].alpha_mode ==
                   openrc::RenderSceneAlphaModeV1::mask &&
               scene.materials[0U].alpha_cutoff_rgba8 == 1U &&
               scene.materials[1U].alpha_mode ==
                   openrc::RenderSceneAlphaModeV1::opaque &&
               scene.materials[1U].alpha_cutoff_rgba8 == 0U,
           "compiled alpha policies are wrong");

    const auto& terrain = scene.meshes[0U];
    expect(terrain.vertices.size() == 4U &&
               terrain.triangle_indices ==
                   std::vector<std::uint32_t>{0U, 1U, 2U, 1U, 3U, 2U},
           "terrain global-to-local vertex remap is not first-use dense");
    expect(terrain.vertices[0U].x == -1.0F &&
               terrain.vertices[0U].y == 0.0F &&
               terrain.vertices[0U].rgba8 == UINT32_C(0xff000005) &&
               terrain.vertices[0U].u == -1.0F,
           "terrain vertex scale or recovered attributes changed");
    expect(terrain.draw_ranges ==
               std::vector<openrc::RenderSceneDrawRangeV1>{
                   {0U, 0U, 3U}, {1U, 3U, 3U}},
           "terrain material draws do not cover its mesh exactly");
    expect(scene.meshes[1U].triangle_indices ==
                   std::vector<std::uint32_t>{0U, 1U, 2U} &&
               scene.meshes[1U].draw_ranges ==
                   std::vector<openrc::RenderSceneDrawRangeV1>{{2U, 0U, 3U}} &&
               scene.meshes[2U].triangle_indices ==
                   std::vector<std::uint32_t>{0U, 1U, 2U} &&
               scene.meshes[2U].draw_ranges ==
                   std::vector<openrc::RenderSceneDrawRangeV1>{{3U, 0U, 3U}},
           "Moby/TIE mesh or draw remapping is wrong");
}

void test_is_deterministic_and_omits_unused_textures() {
    const auto input = make_three_family_recovery();
    const auto first =
        openrc::runtime::compile_level_scene_render_v1(input, profile());
    const auto second =
        openrc::runtime::compile_level_scene_render_v1(input, profile());
    expect(first == second, "identical recovery input compiled differently");

    auto changed_unused = input;
    changed_unused.tfrag_texture_bank->textures[0U].rgba[0U] =
        std::byte{0xee};
    changed_unused.moby_texture_bank->textures[0U].rgba[0U] =
        std::byte{0xdd};
    changed_unused.tie_texture_bank->textures[1U].rgba[0U] =
        std::byte{0xcc};
    const auto without_unused = openrc::runtime::compile_level_scene_render_v1(
        changed_unused, profile());
    expect(first == without_unused,
           "unused source textures leaked into compiled RenderSceneV1");
}

void test_synthesizes_and_merges_explicit_fallbacks() {
    const auto absent_batches =
        openrc::runtime::compile_level_scene_render_v1(
            make_fallback_recovery(), profile());
    expect(absent_batches.textures.empty() &&
               absent_batches.materials.size() == 1U &&
               absent_batches.meshes[0U].draw_ranges ==
                   std::vector<openrc::RenderSceneDrawRangeV1>{{0U, 0U, 3U}},
           "absent material batches did not create the canonical fallback");

    auto explicit_fallback = make_fallback_recovery();
    explicit_fallback.terrain_material_batches = {
        {0U, 3U, std::nullopt}};
    const auto explicit_scene =
        openrc::runtime::compile_level_scene_render_v1(
            explicit_fallback, profile());
    expect(absent_batches == explicit_scene,
           "explicit null material and absent material batches differ");

    auto adjacent = make_three_family_recovery();
    adjacent.moby_first_triangle.reset();
    adjacent.moby_triangle_count = 0U;
    adjacent.moby_material_batches.clear();
    adjacent.tie_first_triangle.reset();
    adjacent.tie_triangle_count = 0U;
    adjacent.tie_material_batches.clear();
    adjacent.source->triangle_indices.resize(6U);
    adjacent.source->emitted_triangle_count = 2U;
    adjacent.terrain_material_batches = {{0U, 3U, 1U}, {1U, 3U, 1U}};
    const auto merged = openrc::runtime::compile_level_scene_render_v1(
        adjacent, profile());
    expect(merged.meshes[0U].draw_ranges ==
               std::vector<openrc::RenderSceneDrawRangeV1>{{0U, 0U, 6U}},
           "adjacent equal materials were not merged deterministically");
}

void test_rejects_malformed_geometry_and_segmentation() {
    auto missing_source = make_three_family_recovery();
    missing_source.source.reset();
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                missing_source, profile()));
        },
        "compiler accepted missing source geometry");

    auto misaligned = make_three_family_recovery();
    misaligned.source->triangle_indices.pop_back();
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                misaligned, profile()));
        },
        "compiler accepted a non-triangle index buffer");

    auto stale_counter = make_three_family_recovery();
    --stale_counter.source->emitted_triangle_count;
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                stale_counter, profile()));
        },
        "compiler accepted a stale emitted-triangle counter");

    auto bad_boundary = make_three_family_recovery();
    bad_boundary.moby_first_triangle = 1U;
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                bad_boundary, profile()));
        },
        "compiler accepted overlapping terrain/Moby segments");

    auto missing_segment = make_three_family_recovery();
    missing_segment.tie_first_triangle.reset();
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                missing_segment, profile()));
        },
        "compiler accepted a TIE count without a TIE segment");

    auto batch_gap = make_three_family_recovery();
    batch_gap.terrain_material_batches[1U].first_triangle = 0U;
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                batch_gap, profile()));
        },
        "compiler accepted overlapping terrain material batches");

    auto bad_vertex_index = make_three_family_recovery();
    bad_vertex_index.source->triangle_indices[0U] = 999U;
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                bad_vertex_index, profile()));
        },
        "compiler accepted a missing global source vertex");
}

void test_rejects_missing_or_malformed_referenced_textures() {
    auto missing_bank = make_three_family_recovery();
    missing_bank.tfrag_texture_bank.reset();
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                missing_bank, profile()));
        },
        "compiler accepted a textured family without its bank");

    auto missing_texture = make_three_family_recovery();
    missing_texture.terrain_material_batches[0U].texture_index = 99U;
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                missing_texture, profile()));
        },
        "compiler accepted a source texture index outside its bank");

    auto bad_global_index = make_three_family_recovery();
    bad_global_index.tfrag_texture_bank->textures[1U].global_index = 0U;
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                bad_global_index, profile()));
        },
        "compiler accepted a non-canonical source texture global index");

    auto bad_dimensions = make_three_family_recovery();
    bad_dimensions.moby_texture_bank->textures[2U].entry.width = 0;
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                bad_dimensions, profile()));
        },
        "compiler accepted invalid source texture dimensions");

    auto bad_rgba = make_three_family_recovery();
    bad_rgba.tie_texture_bank->textures[0U].rgba.pop_back();
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                bad_rgba, profile()));
        },
        "compiler accepted an inexact source texture RGBA payload");
}

void test_enforces_profile_and_render_scene_limits() {
    auto invalid_scale = profile();
    invalid_scale.source_units_per_world_unit =
        std::numeric_limits<float>::quiet_NaN();
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                make_three_family_recovery(), invalid_scale));
        },
        "compiler accepted a non-finite coordinate scale");

    auto mesh_limit = profile();
    mesh_limit.render_scene_limits.max_meshes = 2U;
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                make_three_family_recovery(), mesh_limit));
        },
        "compiler ignored its mesh limit");

    auto vertex_limit = profile();
    vertex_limit.render_scene_limits.max_vertices = 3U;
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                make_three_family_recovery(), vertex_limit));
        },
        "compiler ignored its aggregate vertex limit");

    auto texture_limit = profile();
    texture_limit.render_scene_limits.max_total_rgba8_bytes = 7U;
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                make_three_family_recovery(), texture_limit));
        },
        "compiler ignored its RGBA8 byte limit");

    auto invalid_limits = profile();
    invalid_limits.render_scene_limits.max_draw_ranges = 0U;
    expect_compile_error(
        [&] {
            static_cast<void>(openrc::runtime::compile_level_scene_render_v1(
                make_three_family_recovery(), invalid_limits));
        },
        "compiler accepted an invalid zero caller limit");
}

} // namespace

int main() {
    try {
        test_compiles_three_dense_family_meshes();
        test_is_deterministic_and_omits_unused_textures();
        test_synthesizes_and_merges_explicit_fallbacks();
        test_rejects_malformed_geometry_and_segmentation();
        test_rejects_missing_or_malformed_referenced_textures();
        test_enforces_profile_and_render_scene_limits();
        std::cout << "level_scene_render_compile_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "level_scene_render_compile_tests: " << error.what()
                  << '\n';
        return 1;
    }
}
