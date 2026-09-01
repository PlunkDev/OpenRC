#include "moby_scene_geometry.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::runtime::MobySceneGeometryLimitsV1 kLimits{
    64U, 256U, 4096U, 12'288U, 4096U, 12'288U};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_near(const float actual,
                 const float expected,
                 const std::string& message) {
    if (std::abs(actual - expected) > 0.0001F) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] openrc::RacLevelMobyModelV1 make_model(
    const std::uint32_t class_id,
    const bool animated = false) {
    openrc::RacLevelMobyModelV1 model;
    model.class_id = class_id;
    model.texture_slots.fill(openrc::kRacLevelCoreUnusedTextureSlotV1);
    model.texture_slots[0U] = 17U;
    model.used_texture_slot_count = 1U;
    model.high_lod.lod = openrc::RacMobyLodV1::high;
    model.high_lod.requires_bind_transforms = animated;
    for (const auto& position :
         std::array<std::array<float, 3U>, 4U>{
             std::array<float, 3U>{0.0F, 0.0F, 0.0F},
             std::array<float, 3U>{1.0F, 0.0F, 0.0F},
             std::array<float, 3U>{0.0F, 1.0F, 0.0F},
             std::array<float, 3U>{1000.0F, 1000.0F, 1000.0F}}) {
        openrc::RacMobyModelVertexV1 vertex;
        vertex.diagnostic_position = position;
        vertex.texture_coordinate = {
            position[0U] + 0.125F, position[1U] + 0.25F};
        model.high_lod.vertices.push_back(vertex);
    }
    model.high_lod.triangles.push_back(
        openrc::RacMobyModelTriangleV1{{0U, 1U, 2U}, 0, 0U});
    return model;
}

[[nodiscard]] openrc::RacGameplayMobyInstanceV1 make_placement(
    const std::uint32_t class_id,
    const std::array<float, 3U> position = {},
    const std::array<float, 3U> rotation = {},
    const float scale = 1.0F) {
    openrc::RacGameplayMobyInstanceV1 placement;
    placement.class_id = class_id;
    placement.position = position;
    placement.rotation = rotation;
    placement.scale = scale;
    return placement;
}

template <typename Mutation>
void expect_rejected(Mutation&& mutation, const std::string& message) {
    std::vector models{make_model(42U)};
    std::vector placements{make_placement(42U)};
    auto limits = kLimits;
    std::invoke(
        std::forward<Mutation>(mutation), models, placements, limits);
    try {
        (void)openrc::runtime::build_moby_scene_geometry_v1(
            models, placements,
            openrc::runtime::MobySceneCoordinateDomainV1::world_units,
            limits);
    } catch (const openrc::runtime::MobySceneGeometryError&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_static_instance_transform_and_compaction() {
    const std::vector models{make_model(42U)};
    constexpr float kHalfPi = 1.57079632679489661923F;
    const std::vector placements{
        make_placement(42U, {10.0F, 20.0F, 30.0F}, {}, 2.0F),
        make_placement(42U, {}, {0.0F, 0.0F, kHalfPi}),
        make_placement(99U)};
    const auto result = openrc::runtime::build_moby_scene_geometry_v1(
        models, placements,
        openrc::runtime::MobySceneCoordinateDomainV1::world_units,
        kLimits);

    expect(result.geometry.has_value() &&
               result.coordinate_domain ==
                   openrc::runtime::MobySceneCoordinateDomainV1::world_units &&
               result.stats.model_count == 1U &&
               result.stats.placement_count == 3U &&
               result.stats.rendered_model_count == 1U &&
               result.stats.rendered_placement_count == 2U &&
               result.stats.missing_model_placement_count == 1U,
           "the Moby scene placement summary is wrong");
    const auto& geometry = *result.geometry;
    expect(geometry.vertices.size() == 6U &&
               geometry.triangle_indices ==
                   std::vector<std::uint32_t>{0U, 1U, 2U, 3U, 4U, 5U} &&
               geometry.emitted_triangle_count == 2U &&
               geometry.vertices.front().rgba == UINT32_C(0xff30b0ff),
           "the instantiated Moby scene topology, compaction, or bounds are wrong");
    expect(result.material_batches ==
               std::vector<openrc::runtime::MobySceneMaterialBatchV1>{
                   {0U, 6U, 17U}},
           "equal adjacent Moby materials were not coalesced across placements");
    expect_near(geometry.vertices[0U].u, 0.125F,
                "the first Moby texture U coordinate was lost");
    expect_near(geometry.vertices[0U].v, 0.25F,
                "the first Moby texture V coordinate was lost");
    expect_near(geometry.vertices[4U].u, 1.125F,
                "the instanced Moby texture U coordinate was changed");
    expect_near(geometry.minimum_x, -1.0F,
                "the instantiated Moby minimum X bound is wrong");
    expect_near(geometry.maximum_x, 12.0F,
                "the instantiated Moby maximum X bound is wrong");
    expect_near(geometry.minimum_y, 0.0F,
                "the instantiated Moby minimum Y bound is wrong");
    expect_near(geometry.maximum_y, 22.0F,
                "the instantiated Moby maximum Y bound is wrong");
    expect_near(geometry.minimum_z, 0.0F,
                "the instantiated Moby minimum Z bound is wrong");
    expect_near(geometry.maximum_z, 30.0F,
                "the instantiated Moby maximum Z bound is wrong");
    expect_near(geometry.vertices[0U].x, 10.0F,
                "the translated Moby X coordinate is wrong");
    expect_near(geometry.vertices[1U].x, 12.0F,
                "the scaled Moby X coordinate is wrong");
    expect_near(geometry.vertices[2U].y, 22.0F,
                "the scaled Moby Y coordinate is wrong");
    expect_near(geometry.vertices[4U].x, 0.0F,
                "the rotated Moby X coordinate is wrong");
    expect_near(geometry.vertices[4U].y, 1.0F,
                "the rotated Moby Y coordinate is wrong");
    expect_near(geometry.vertices[5U].x, -1.0F,
                "the rotated Moby second X coordinate is wrong");
}

void test_material_slot_mapping_and_batch_boundaries() {
    auto model = make_model(42U);
    model.texture_slots.fill(openrc::kRacLevelCoreUnusedTextureSlotV1);
    model.texture_slots[0U] = 4U;
    model.texture_slots[1U] = 9U;
    model.used_texture_slot_count = 2U;
    model.high_lod.triangles = {
        openrc::RacMobyModelTriangleV1{{0U, 1U, 2U}, 0, 0U},
        openrc::RacMobyModelTriangleV1{{0U, 2U, 1U}, 0, 0U},
        openrc::RacMobyModelTriangleV1{{1U, 0U, 2U}, 1, 0U},
        openrc::RacMobyModelTriangleV1{{2U, 1U, 0U}, -1, 0U},
        openrc::RacMobyModelTriangleV1{{0U, 1U, 2U}, -1, 0U},
        openrc::RacMobyModelTriangleV1{{0U, 2U, 1U}, 0, 0U},
    };
    const std::vector models{std::move(model)};
    const std::vector placements{make_placement(42U)};
    const auto result = openrc::runtime::build_moby_scene_geometry_v1(
        models, placements,
        openrc::runtime::MobySceneCoordinateDomainV1::world_units,
        kLimits);

    expect(result.geometry.has_value() &&
               result.geometry->triangle_indices.size() == 18U,
           "the material fixture did not produce six triangles");
    expect(
        result.material_batches ==
            std::vector<openrc::runtime::MobySceneMaterialBatchV1>{
                {0U, 6U, 4U},
                {2U, 3U, 9U},
                {3U, 6U, std::nullopt},
                {5U, 3U, 4U},
            },
        "local texture slots were not mapped into contiguous global batches");
}

void test_scene_block_coordinate_domain() {
    const std::vector models{make_model(42U)};
    const std::vector placements{
        make_placement(42U, {10.0F, 20.0F, 30.0F}, {}, 2.0F)};
    const auto result = openrc::runtime::build_moby_scene_geometry_v1(
        models, placements,
        openrc::runtime::MobySceneCoordinateDomainV1::
            scene_block_itof0_units,
        kLimits);
    expect(result.geometry.has_value() &&
               result.coordinate_domain ==
                   openrc::runtime::MobySceneCoordinateDomainV1::
                       scene_block_itof0_units,
           "the SceneBlock Moby coordinate domain was not retained");
    const auto& geometry = *result.geometry;
    expect_near(geometry.vertices[0U].x, 10.0F * 1024.0F,
                "the SceneBlock-domain Moby translation is wrong");
    expect_near(geometry.vertices[1U].x, 12.0F * 1024.0F,
                "the SceneBlock-domain Moby model scale is wrong");
    expect_near(geometry.maximum_y, 22.0F * 1024.0F,
                "the SceneBlock-domain Moby bounds are wrong");
}

void test_invalid_coordinate_domain() {
    const std::vector models{make_model(42U)};
    const std::vector placements{make_placement(42U)};
    try {
        (void)openrc::runtime::build_moby_scene_geometry_v1(
            models, placements,
            static_cast<openrc::runtime::MobySceneCoordinateDomainV1>(0xffU),
            kLimits);
    } catch (const openrc::runtime::MobySceneGeometryError&) {
        return;
    }
    throw std::runtime_error("an invalid Moby coordinate domain was accepted");
}

void test_explicit_skip_policy() {
    auto empty = make_model(44U);
    empty.high_lod.triangles.clear();
    const std::vector models{
        make_model(42U), make_model(43U, true), std::move(empty)};
    const std::vector placements{
        make_placement(43U), make_placement(44U), make_placement(45U)};
    const auto result = openrc::runtime::build_moby_scene_geometry_v1(
        models, placements,
        openrc::runtime::MobySceneCoordinateDomainV1::world_units,
        kLimits);
    expect(!result.geometry && result.stats.rendered_placement_count == 0U &&
               result.stats.animated_model_placement_count == 1U &&
               result.stats.empty_model_placement_count == 1U &&
               result.stats.missing_model_placement_count == 1U,
           "the Moby scene skip policy is wrong");
    expect(result.material_batches.empty(),
           "a skipped Moby scene unexpectedly retained material batches");
}

void test_limits_and_structural_rejections() {
    expect_rejected(
        [](auto&, auto&, auto& limits) { limits = {}; },
        "zero Moby scene limits were accepted");
    expect_rejected(
        [](auto& models, auto&, auto& limits) {
            models.push_back(make_model(43U));
            limits.max_models = 1U;
        },
        "the positive Moby scene model limit was ignored");
    expect_rejected(
        [](auto&, auto& placements, auto& limits) {
            placements.push_back(make_placement(42U));
            limits.max_instances = 1U;
        },
        "the positive Moby scene instance limit was ignored");
    expect_rejected(
        [](auto&, auto&, auto& limits) {
            limits.max_workspace_vertices = 3U;
        },
        "the Moby scene vertex-workspace limit was ignored");
    expect_rejected(
        [](auto&, auto&, auto& limits) {
            limits.max_workspace_triangle_indices = 2U;
        },
        "the Moby scene index-workspace limit was ignored");
    expect_rejected(
        [](auto&, auto&, auto& limits) { limits.max_output_vertices = 2U; },
        "the instantiated Moby vertex limit was ignored");
    expect_rejected(
        [](auto&, auto&, auto& limits) {
            limits.max_output_triangle_indices = 2U;
        },
        "the instantiated Moby index limit was ignored");
    expect_rejected(
        [](auto& models, auto&, auto&) { models.push_back(make_model(42U)); },
        "a duplicate Moby scene class ID was accepted");
    expect_rejected(
        [](auto& models, auto&, auto&) {
            models[0U].high_lod.lod = openrc::RacMobyLodV1::low;
        },
        "a low-LOD assembly was accepted as the high-LOD scene model");
    expect_rejected(
        [](auto& models, auto&, auto&) {
            models[0U].high_lod.triangles[0U].vertex_indices[2U] = 4U;
        },
        "a Moby scene triangle with a missing vertex was accepted");
    expect_rejected(
        [](auto&, auto& placements, auto&) {
            placements[0U].scale =
                std::numeric_limits<float>::quiet_NaN();
        },
        "a non-finite Moby placement transform was accepted");
    expect_rejected(
        [](auto& models, auto&, auto&) {
            models[0U].high_lod.triangles[0U].texture_index = 1;
        },
        "an unavailable local Moby texture slot was accepted");
    expect_rejected(
        [](auto& models, auto&, auto&) {
            models[0U].texture_slots[0U] =
                openrc::kRacLevelCoreUnusedTextureSlotV1;
        },
        "an explicitly unused Moby texture slot was accepted");
    expect_rejected(
        [](auto& models, auto&, auto&) {
            models[0U].high_lod.triangles[0U].texture_index = -2;
        },
        "an invalid negative Moby texture slot was accepted");
    expect_rejected(
        [](auto& models, auto&, auto&) {
            models[0U].high_lod.vertices[0U].texture_coordinate[0U] =
                std::numeric_limits<float>::quiet_NaN();
        },
        "a non-finite Moby texture coordinate was accepted");
}

} // namespace

int main() {
    try {
        test_static_instance_transform_and_compaction();
        test_material_slot_mapping_and_batch_boundaries();
        test_scene_block_coordinate_domain();
        test_invalid_coordinate_domain();
        test_explicit_skip_policy();
        test_limits_and_structural_rejections();
        std::cout << "moby_scene_geometry_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "moby_scene_geometry_tests: " << error.what() << '\n';
        return 1;
    }
}
