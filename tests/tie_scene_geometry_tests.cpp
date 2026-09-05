#include "../src/runtime/tie_scene_geometry.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::runtime::TieSceneGeometryLimitsV1 kLimits{
    64U,
    64U,
    4096U,
    12'288U,
    4096U,
    12'288U,
};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] openrc::RacLevelTieModelV1 make_model() {
    openrc::RacLevelTieModelV1 model;
    model.class_id = 7U;
    model.texture_slots.fill(openrc::kRacLevelCoreUnusedTextureSlotV1);
    model.texture_slots[0U] = 3U;
    model.texture_slots[1U] = 4U;
    model.used_texture_slot_count = 2U;
    model.high_lod.texture_count = 2U;
    const auto vertex = [](
                            const std::array<float, 3U> position,
                            const std::array<float, 2U> uv) {
        openrc::RacTieVertexV1 result;
        result.position = position;
        result.texture_coordinate = uv;
        return result;
    };
    model.high_lod.vertices = {
        vertex({1.0F, 2.0F, 3.0F}, {0.0F, 0.0F}),
        vertex({-1.0F, 0.0F, 2.0F}, {1.0F, 0.0F}),
        vertex({0.0F, 1.0F, -2.0F}, {0.0F, 1.0F}),
        vertex({2.0F, -1.0F, 0.0F}, {1.0F, 1.0F}),
    };
    model.high_lod.triangles = {
        {{0U, 1U, 2U}, 0U, 0U},
        {{2U, 1U, 3U}, 1U, 0U},
    };
    return model;
}

[[nodiscard]] openrc::RacGameplayTieInstanceV1 make_placement() {
    openrc::RacGameplayTieInstanceV1 placement;
    placement.class_id = 7U;
    placement.matrix = {
        2.0F, 3.0F, 5.0F, 0.0F,
        7.0F, 11.0F, 13.0F, 0.0F,
        17.0F, 19.0F, 23.0F, 0.0F,
        29.0F, 31.0F, 37.0F, 0.01F,
    };
    for (std::size_t index = 0U; index < placement.matrix.size(); ++index) {
        placement.matrix_bits[index] =
            std::bit_cast<std::uint32_t>(placement.matrix[index]);
    }
    return placement;
}

void test_world_geometry() {
    const std::array models{make_model()};
    const std::array placements{make_placement()};
    const auto result = openrc::runtime::build_tie_scene_geometry_v1(
        models,
        placements,
        openrc::runtime::TieSceneCoordinateDomainV1::world_units,
        kLimits);
    expect(
        result.geometry.has_value() && result.stats.model_count == 1U &&
            result.stats.placement_count == 1U &&
            result.stats.rendered_model_count == 1U &&
            result.stats.rendered_placement_count == 1U &&
            result.geometry->vertices.size() == 4U &&
            result.geometry->triangle_indices ==
                std::vector<std::uint32_t>{0U, 1U, 2U, 2U, 1U, 3U},
        "TIE scene geometry totals or indices are wrong");
    const auto& transformed = result.geometry->vertices[0U];
    expect(
        transformed.x == 96.0F && transformed.y == 113.0F &&
            transformed.z == 137.0F && transformed.u == 0.0F &&
            transformed.v == 0.0F,
        "TIE column-major placement transform is wrong");
    expect(
        result.material_batches ==
            std::vector<openrc::runtime::MobySceneMaterialBatchV1>{
                {0U, 3U, 3U},
                {1U, 3U, 4U},
            },
        "TIE material-slot resolution is wrong");
}

void test_scene_block_scale_and_missing_model() {
    const std::array models{make_model()};
    auto present = make_placement();
    auto missing = present;
    missing.class_id = 99U;
    const std::array placements{present, missing};
    const auto result = openrc::runtime::build_tie_scene_geometry_v1(
        models,
        placements,
        openrc::runtime::TieSceneCoordinateDomainV1::
            scene_block_itof0_units,
        kLimits);
    expect(
        result.geometry && result.geometry->vertices[0U].x == 96.0F * 1024.0F &&
            result.geometry->vertices[0U].y == 113.0F * 1024.0F &&
            result.geometry->vertices[0U].z == 137.0F * 1024.0F &&
            result.stats.rendered_placement_count == 1U &&
            result.stats.missing_model_placement_count == 1U,
        "TIE scene coordinate conversion or missing-model count is wrong");
}

template <typename Mutation>
void expect_rejected(Mutation&& mutation, const std::string& message) {
    auto model = make_model();
    auto placement = make_placement();
    std::invoke(std::forward<Mutation>(mutation), model, placement);
    const std::array models{model};
    const std::array placements{placement};
    try {
        (void)openrc::runtime::build_tie_scene_geometry_v1(
            models,
            placements,
            openrc::runtime::TieSceneCoordinateDomainV1::world_units,
            kLimits);
    } catch (const openrc::runtime::TieSceneGeometryError&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_rejections() {
    expect_rejected(
        [](auto& model, auto&) {
            model.high_lod.triangles[0U].local_texture_index = 2U;
        },
        "an absent TIE texture slot was accepted");
    expect_rejected(
        [](auto&, auto& placement) {
            placement.matrix[4U] = std::bit_cast<float>(0x7fc00000U);
        },
        "a non-finite TIE placement matrix was accepted");
    expect_rejected(
        [](auto& model, auto&) {
            model.high_lod.triangles[0U].vertex_indices[0U] = 99U;
        },
        "a missing TIE model vertex was accepted");

    const std::array models{make_model(), make_model()};
    const std::array placements{make_placement()};
    try {
        (void)openrc::runtime::build_tie_scene_geometry_v1(
            models,
            placements,
            openrc::runtime::TieSceneCoordinateDomainV1::world_units,
            kLimits);
    } catch (const openrc::runtime::TieSceneGeometryError&) {
        return;
    }
    throw std::runtime_error("duplicate TIE model classes were accepted");
}

} // namespace

int main() {
    try {
        test_world_geometry();
        test_scene_block_scale_and_missing_model();
        test_rejections();
        std::cout << "OpenRC TIE scene geometry tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC TIE scene geometry tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
