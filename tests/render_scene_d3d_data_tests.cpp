#include "render_scene_d3d_data.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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

template <typename Mutation>
void expect_rejected(Mutation mutation, const std::string& message);

[[nodiscard]] std::vector<std::byte> rgba(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint8_t value) {
    return std::vector<std::byte>(
        static_cast<std::size_t>(width) * height * 4U,
        static_cast<std::byte>(value));
}

[[nodiscard]] openrc::RenderSceneV1 make_scene() {
    openrc::RenderSceneTextureV1 texture0;
    texture0.id = 0U;
    texture0.color_space = openrc::RenderSceneTextureColorSpaceV1::linear;
    texture0.mips = {
        {2U, 2U, rgba(2U, 2U, 0x40U)},
        {1U, 1U, rgba(1U, 1U, 0x80U)},
    };

    openrc::RenderSceneTextureV1 texture1;
    texture1.id = 1U;
    texture1.color_space = openrc::RenderSceneTextureColorSpaceV1::srgb;
    texture1.mips = {{1U, 1U, rgba(1U, 1U, 0xffU)}};

    openrc::RenderSceneMaterialV1 material0;
    material0.id = 0U;
    material0.base_color_texture_id = 0U;
    material0.base_color_rgba8 = UINT32_C(0x80604020);
    material0.use_vertex_color = true;
    material0.double_sided = false;
    material0.address_u = openrc::RenderSceneAddressModeV1::clamp_to_edge;
    material0.address_v = openrc::RenderSceneAddressModeV1::repeat;
    material0.min_filter = openrc::RenderSceneFilterV1::nearest;
    material0.mag_filter = openrc::RenderSceneFilterV1::linear;
    material0.mipmap_filter = openrc::RenderSceneMipmapFilterV1::linear;
    material0.alpha_mode = openrc::RenderSceneAlphaModeV1::mask;
    material0.alpha_cutoff_rgba8 = 128U;

    openrc::RenderSceneMaterialV1 material1;
    material1.id = 1U;
    material1.base_color_texture_id = 1U;
    material1.use_vertex_color = false;
    material1.double_sided = true;
    material1.address_v = openrc::RenderSceneAddressModeV1::clamp_to_edge;

    openrc::RenderSceneMeshV1 mesh;
    mesh.id = 0U;
    mesh.vertices = {
        {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, UINT32_C(0x01020304)},
        {1.0F, 0.0F, 0.0F, 1.0F, 0.0F, UINT32_C(0x11121314)},
        {1.0F, 2.0F, 0.0F, 1.0F, 1.0F, UINT32_C(0x21222324)},
        {0.0F, 2.0F, 1.0F, 0.0F, 1.0F, UINT32_C(0x31323334)},
    };
    mesh.triangle_indices = {0U, 1U, 2U, 0U, 2U, 3U};
    mesh.draw_ranges = {{0U, 0U, 3U}, {1U, 3U, 3U}};

    openrc::RenderSceneInstanceV1 instance0;
    instance0.id = 0U;
    instance0.mesh_id = 0U;
    instance0.local_to_world.values = {
        2.0F, 0.0F, 0.0F, 10.0F,
        0.0F, 3.0F, 0.0F, 20.0F,
        0.0F, 0.0F, 4.0F, 30.0F,
    };

    openrc::RenderSceneInstanceV1 instance1;
    instance1.id = 1U;
    instance1.mesh_id = 0U;
    instance1.local_to_world.values = {
        0.0F, -1.0F, 0.0F, -5.0F,
        1.0F, 0.0F, 0.0F, 7.0F,
        0.0F, 0.0F, 1.0F, -2.0F,
    };

    openrc::RenderSceneV1 scene;
    scene.textures = {std::move(texture0), std::move(texture1)};
    scene.materials = {material0, material1};
    scene.meshes = {std::move(mesh)};
    scene.instances = {instance0, instance1};
    return scene;
}

template <typename Mutation>
void expect_rejected(Mutation mutation, const std::string& message) {
    auto scene = make_scene();
    mutation(scene);
    try {
        (void)openrc::runtime::build_render_scene_d3d_data_v1(scene);
    } catch (const openrc::runtime::RenderSceneD3dDataError&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_affine_instances_indices_draws_and_bounds() {
    const auto flattened =
        openrc::runtime::build_render_scene_d3d_data_v1(make_scene());

    expect(flattened.vertices.size() == 8U,
           "flattening did not expand vertices per instance");
    expect(flattened.triangle_indices ==
               std::vector<std::uint32_t>{
                   0U, 1U, 2U, 0U, 2U, 3U,
                   4U, 5U, 6U, 4U, 6U, 7U},
           "flattening did not remap mesh-local indices globally");
    expect(flattened.draws ==
               std::vector<openrc::runtime::RenderSceneD3dDrawV1>{
                   {0U, 0U, 3U},
                   {1U, 3U, 3U},
                   {0U, 6U, 3U},
                   {1U, 9U, 3U}},
           "flattening did not globalize canonical draw ranges");

    expect(flattened.materials.size() == 2U,
           "material policies were not staged densely");
    const auto& material0 = flattened.materials[0U];
    expect(material0.base_color_texture_id == 0U &&
               material0.base_color == std::array<float, 4U>{
                   32.0F / 255.0F,
                   64.0F / 255.0F,
                   96.0F / 255.0F,
                   128.0F / 255.0F} &&
               material0.use_vertex_color && !material0.double_sided &&
               material0.address_u ==
                   openrc::RenderSceneAddressModeV1::clamp_to_edge &&
               material0.address_v ==
                   openrc::RenderSceneAddressModeV1::repeat &&
               material0.min_filter ==
                   openrc::RenderSceneFilterV1::nearest &&
               material0.mag_filter ==
                   openrc::RenderSceneFilterV1::linear &&
               material0.mipmap_filter ==
                   openrc::RenderSceneMipmapFilterV1::linear &&
               material0.alpha_cutoff == 128.0F / 255.0F,
           "the masked material policy was not staged exactly");
    const auto& material1 = flattened.materials[1U];
    expect(material1.base_color_texture_id == 1U &&
               !material1.use_vertex_color && material1.double_sided &&
               material1.alpha_cutoff == -1.0F,
           "the opaque material policy was not staged exactly");

    const auto& first = flattened.vertices[0U];
    expect(first.x == 10.0F && first.y == 20.0F && first.z == 30.0F &&
               first.u == 0.0F && first.v == 0.0F &&
               first.rgba8 == UINT32_C(0x01020304),
           "the first affine transform or vertex payload is wrong");
    const auto& scaled = flattened.vertices[3U];
    expect(scaled.x == 10.0F && scaled.y == 26.0F && scaled.z == 34.0F,
           "row-major scale/translation was not respected");
    const auto& rotated = flattened.vertices[6U];
    expect(rotated.x == -7.0F && rotated.y == 8.0F && rotated.z == -2.0F,
           "row-major rotation/translation was not respected");

    expect(flattened.minimum_x == -7.0F &&
               flattened.maximum_x == 12.0F &&
               flattened.minimum_y == 7.0F &&
               flattened.maximum_y == 26.0F &&
               flattened.minimum_z == -2.0F &&
               flattened.maximum_z == 34.0F,
           "flattened world bounds are wrong");
}

void test_empty_scene_is_a_valid_blank_resource() {
    const auto flattened = openrc::runtime::build_render_scene_d3d_data_v1({});
    expect(flattened.vertices.empty() &&
               flattened.triangle_indices.empty() &&
               flattened.draws.empty() && flattened.materials.empty(),
           "a canonical empty scene did not remain empty");
}

void test_orientation_reversing_instances_keep_ccw_front_faces() {
    auto scene = make_scene();
    scene.instances[1U].local_to_world.values = {
        -1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
    };
    const auto flattened =
        openrc::runtime::build_render_scene_d3d_data_v1(scene);
    expect(std::vector<std::uint32_t>(
               flattened.triangle_indices.begin() + 6,
               flattened.triangle_indices.end()) ==
               std::vector<std::uint32_t>{4U, 6U, 5U, 4U, 7U, 6U},
           "an orientation-reversing affine instance did not preserve CCW fronts");
}

void test_different_meshes_keep_local_index_domains() {
    auto scene = make_scene();
    openrc::RenderSceneMeshV1 mesh;
    mesh.id = 1U;
    mesh.vertices.assign(
        scene.meshes[0U].vertices.begin(),
        scene.meshes[0U].vertices.begin() + 3);
    mesh.triangle_indices = {2U, 0U, 1U};
    mesh.draw_ranges = {{1U, 0U, 3U}};
    scene.meshes.push_back(std::move(mesh));

    openrc::RenderSceneInstanceV1 instance;
    instance.id = 2U;
    instance.mesh_id = 1U;
    scene.instances.push_back(instance);

    const auto flattened =
        openrc::runtime::build_render_scene_d3d_data_v1(scene);
    expect(flattened.vertices.size() == 11U &&
               flattened.triangle_indices.size() == 15U &&
               std::vector<std::uint32_t>(
                   flattened.triangle_indices.end() - 3,
                   flattened.triangle_indices.end()) ==
                   std::vector<std::uint32_t>{10U, 8U, 9U} &&
               flattened.draws.back() ==
                   openrc::runtime::RenderSceneD3dDrawV1{1U, 12U, 3U},
           "a later mesh escaped its independent local index domain");
}

void test_instance_expansion_has_practical_allocation_limits() {
    const auto scene = make_scene();
    auto limits = openrc::runtime::make_render_scene_d3d_data_limits_v1();

    limits.max_flattened_vertices = 7U;
    try {
        (void)openrc::runtime::build_render_scene_d3d_data_v1(scene, limits);
        throw std::runtime_error(
            "instance-amplified vertices escaped their allocation limit");
    } catch (const openrc::runtime::RenderSceneD3dDataError&) {
    }

    limits = openrc::runtime::make_render_scene_d3d_data_limits_v1();
    limits.max_flattened_triangle_indices = 11U;
    try {
        (void)openrc::runtime::build_render_scene_d3d_data_v1(scene, limits);
        throw std::runtime_error(
            "instance-amplified indices escaped their allocation limit");
    } catch (const openrc::runtime::RenderSceneD3dDataError&) {
    }

    limits = openrc::runtime::make_render_scene_d3d_data_limits_v1();
    limits.max_flattened_draws = 3U;
    try {
        (void)openrc::runtime::build_render_scene_d3d_data_v1(scene, limits);
        throw std::runtime_error(
            "instance-amplified draws escaped their allocation limit");
    } catch (const openrc::runtime::RenderSceneD3dDataError&) {
    }
}

void test_default_draw_work_budget_is_enforced() {
    auto scene = make_scene();
    scene.instances.resize(1U);
    auto& mesh = scene.meshes[0U];
    mesh.triangle_indices.clear();
    mesh.draw_ranges.clear();
    const auto limits =
        openrc::runtime::make_render_scene_d3d_data_limits_v1();
    const auto attempted_draws = limits.max_flattened_draws + 1U;
    for (std::uint64_t draw = 0U; draw < attempted_draws; ++draw) {
        const auto first_index = mesh.triangle_indices.size();
        mesh.triangle_indices.insert(
            mesh.triangle_indices.end(), {0U, 1U, 2U});
        mesh.draw_ranges.push_back(
            {draw == 0U ? 1U : 0U, first_index, 3U});
    }

    try {
        (void)openrc::runtime::build_render_scene_d3d_data_v1(scene);
    } catch (const openrc::runtime::RenderSceneD3dDataError&) {
        return;
    }
    throw std::runtime_error(
        "the default renderer draw-work budget was not enforced");
}

void test_subnormal_scene_extent_keeps_a_usable_camera_radius() {
    auto scene = make_scene();
    scene.instances.resize(1U);
    scene.instances[0U].local_to_world = {};
    for (auto& vertex : scene.meshes[0U].vertices) {
        vertex.x = 0.0F;
        vertex.y = 0.0F;
        vertex.z = 0.0F;
    }
    scene.meshes[0U].vertices.back().x =
        std::numeric_limits<float>::denorm_min();

    const auto flattened =
        openrc::runtime::build_render_scene_d3d_data_v1(scene);
    expect(std::isfinite(flattened.camera_radius) &&
               flattened.camera_radius > 0.0F,
           "a finite subnormal scene extent collapsed the camera radius to zero");
}

void test_invalid_and_non_finite_results_are_rejected() {
    expect_rejected(
        [](auto& scene) { scene.meshes[0U].triangle_indices[0U] = 99U; },
        "an invalid mesh-local index was accepted");
    expect_rejected(
        [](auto& scene) {
            scene.instances[0U].local_to_world.values[0U] =
                std::numeric_limits<float>::max();
            scene.instances[0U].local_to_world.values[3U] =
                std::numeric_limits<float>::max();
        },
        "an affine transform that overflowed a world position was accepted");
    expect_rejected(
        [](auto& scene) {
            for (auto& vertex : scene.meshes[0U].vertices) {
                vertex.x = std::numeric_limits<float>::max();
                vertex.y = 0.0F;
                vertex.z = 0.0F;
            }
            for (auto& instance : scene.instances) {
                instance.local_to_world = {};
            }
        },
        "finite geometry outside the renderer camera domain was accepted");
    expect_rejected(
        [](auto& scene) {
            scene.textures[1U].mips.push_back(
                {1U, 1U, rgba(1U, 1U, 0x7fU)});
        },
        "a texture with more physical mip levels than D3D11 supports was accepted");
}

} // namespace

int main() {
    try {
        test_affine_instances_indices_draws_and_bounds();
        test_empty_scene_is_a_valid_blank_resource();
        test_orientation_reversing_instances_keep_ccw_front_faces();
        test_different_meshes_keep_local_index_domains();
        test_instance_expansion_has_practical_allocation_limits();
        test_default_draw_work_budget_is_enforced();
        test_subnormal_scene_extent_keeps_a_usable_camera_radius();
        test_invalid_and_non_finite_results_are_rejected();
        std::cout << "RenderScene D3D staging tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RenderScene D3D staging tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
