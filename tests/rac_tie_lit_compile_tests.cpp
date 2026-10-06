#include "openrc/rac_tie_lit_compile.hpp"

#include <array>
#include <bit>
#include <iostream>
#include <stdexcept>

namespace {

void expect(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

openrc::RacTieClassV1 model() {
    openrc::RacTieClassV1 result;
    result.texture_count = 2U;
    result.scale_bits = 0x3f800000U;
    for (std::uint32_t i = 0U; i < 6U; ++i) {
        openrc::RacTieVertexV1 vertex;
        vertex.position = {static_cast<float>(i), 1.0F, -2.0F};
        vertex.quantized_position = {
            static_cast<std::int16_t>(i * 1024U), 1024, -2048};
        vertex.texture_coordinate = {-0.5F, 2.0F};
        vertex.stq = {0xf800U, 8192U, 4096U};
        vertex.source_range = {0U, i < 3U ? 0x10U : 0x18U};
        vertex.quantized_morph_delta = {200, -300, 400};
        vertex.lighting_palette_index_count = i < 3U ? 1U : 3U;
        vertex.lighting_palette_indices = {
            static_cast<std::uint8_t>(i + 10U), 62U, 63U};
        result.vertices.push_back(vertex);
    }
    for (std::uint32_t i = 0U; i < 2U; ++i) {
        openrc::RacTiePrimitiveV1 primitive;
        primitive.local_texture_index = 1U - i;
        primitive.vertex_begin = i * 3U;
        primitive.vertex_count = 3U;
        primitive.triangle_begin = i;
        primitive.triangle_count = 1U;
        result.primitives.push_back(primitive);
        result.triangles.push_back({
            {i * 3U + 2U, i * 3U + 1U, i * 3U}, 1U - i, 0U});
    }
    return result;
}

openrc::RacInstancePaletteV1 palette() {
    openrc::RacInstancePaletteV1 result;
    for (unsigned i = 0U; i < 64U; ++i) {
        result.rgba.push_back({static_cast<std::uint8_t>(i),
            static_cast<std::uint8_t>(i + 64U),
            static_cast<std::uint8_t>(255U - i), 128U});
    }
    return result;
}

void test_lod0_source_ownership_and_order() {
    const auto source = model();
    const auto mesh = openrc::compile_rac_tie_lit_lod0_mesh_v1(
        source, palette(), std::array<std::uint32_t, 2U>{3U, 9U}, 7U);
    expect(mesh.id == 7U && mesh.vertices.size() == 6U &&
        mesh.triangle_indices == std::vector<std::uint32_t>{2U, 1U, 0U, 5U, 4U, 3U},
        "Lit TIE lowering changed source vertex or triangle order");
    expect(mesh.draw_ranges == std::vector<openrc::RenderSceneDrawRangeV1>{
        {9U, 0U, 3U}, {3U, 3U, 3U}},
        "Lit TIE lowering regrouped original material submissions");
    for (unsigned i = 0U; i < 6U; ++i) {
        const auto c = i + 10U;
        expect(mesh.vertices[i].rgba8 ==
            (c | ((c + 64U) << 8U) | ((255U - c) << 16U) | 0x80000000U),
            "Lit TIE lowering changed C0 or original GS alpha scale");
        expect(mesh.vertices[i].x == source.vertices[i].position[0U] &&
            mesh.vertices[i].u == -0.5F && mesh.vertices[i].v == 2.0F,
            "High-LOD0 TIE lowering applied a morph delta or changed UVs");
    }
}

void test_rejections() {
    const auto rejected = [](const auto& mutate) {
        auto source = model();
        auto colors = palette();
        mutate(source, colors);
        try {
            (void)openrc::compile_rac_tie_lit_lod0_mesh_v1(source, colors,
                std::array<std::uint32_t, 2U>{3U, 9U}, 0U);
        } catch (const openrc::RacTieLitCompileError&) {
            return;
        }
        throw std::runtime_error("Malformed lit TIE input was accepted");
    };
    rejected([](auto& source, auto&) {
        source.vertices[0U].lighting_palette_index_count = 0U;
    });
    rejected([](auto& source, auto&) {
        source.vertices[4U].lighting_palette_indices[2U] = 64U;
    });
    rejected([](auto&, auto& colors) { colors.rgba.resize(63U); });
    rejected([](auto& source, auto&) {
        source.primitives[0U].triangle_begin = 1U;
    });
    rejected([](auto& source, auto&) {
        source.triangles[1U].local_texture_index = 1U;
    });
    rejected([](auto& source, auto&) {
        source.triangles[0U].vertex_indices[0U] = 6U;
    });
    rejected([](auto& source, auto&) { source.scale_bits = 0U; });
    rejected([](auto& source, auto&) { source.vertices[0U].stq[2U] = 2048U; });
}

void test_fixed_source_position_rounding() {
    auto source = model();
    source.scale_bits = 0x3f5b946aU;
    // Independently decoded VU multiplication result. Host binary32 round to
    // nearest produces 3f972cee/3f2d431c for the first two coordinates.
    for (const auto vertex : {0U, 3U}) {
        source.vertices[vertex].quantized_position = {1410, 808, 12322};
    }
    const auto mesh = openrc::compile_rac_tie_lit_lod0_mesh_v1(
        source, palette(), std::array<std::uint32_t, 2U>{3U, 9U}, 0U);
    for (const auto vertex : {0U, 3U}) {
        expect(std::bit_cast<std::uint32_t>(mesh.vertices[vertex].x) == 0x3f972cedU &&
            std::bit_cast<std::uint32_t>(mesh.vertices[vertex].y) == 0x3f2d431bU &&
            std::bit_cast<std::uint32_t>(mesh.vertices[vertex].z) == 0x412523f6U,
            "Fixed TIE geometry used host-rounded parser positions");
    }
}

void test_source_lod_gates_and_morphs() {
    const std::array<std::uint32_t,3> thresholds{0x41200000U,0x41a00000U,0x41f00000U};
    for(const auto [depth,lod,t]:std::array<std::array<std::uint32_t,3>,4>{{
        {0x41200000U,0U,0U},{0x41a00000U,0U,0x3f800000U},
        {0x41f00000U,1U,0x3f800000U},{0x41f00001U,2U,0U}}}) {
        const auto selected=openrc::select_rac_tie_lod_v1(thresholds,depth);
        expect(selected.selected_lod==lod&&selected.morph_weight_bits[0]==t,
            "Original TIE threshold equality or endpoint ownership changed");
    }
    const auto after=openrc::select_rac_tie_lod_v1(thresholds,0x41a00001U);
    expect(after.selected_lod==1U&&after.morph_weight_bits[0]!=0U,
        "TIE transition sign-bit gate lost the next representable depth");

    auto source=model();source.scale_bits=0x3f800000U;
    source.vertices[3].quantized_position={1024,2048,-4096};
    source.vertices[3].quantized_morph_delta={512,-256,1024};
    auto colors=palette();colors.rgba[13]={0,10,200,128};
    colors.rgba[62]={255,15,0,127};colors.rgba[63]={0,20,255,128};
    const auto half=openrc::select_rac_tie_lod_v1({0U,0x40000000U,0x40800000U},0x3f800000U);
    auto mesh=openrc::compile_rac_tie_lit_mesh_v1(source,colors,half,
        std::array<std::uint32_t,2>{3U,9U},0U);
    expect(mesh.vertices[3].x==1.25F&&mesh.vertices[3].y==1.875F&&mesh.vertices[3].z==-3.5F&&
        mesh.vertices[3].rgba8==0x7fa30d3fU,
        "Original TIE half morph geometry or ordered packed color differs");
    const auto third=openrc::select_rac_tie_lod_v1({0U,0x40400000U,0x40c00000U},0x3f800000U);
    expect(third.morph_weight_bits==std::array<std::uint32_t,4>{0x3eaaaaabU,0U,0x432aaaabU,0x42aaaaaaU},
        "TIE nondyadic source DIV/MUL/SUB weights changed");
    mesh=openrc::compile_rac_tie_lit_mesh_v1(source,colors,third,
        std::array<std::uint32_t,2>{3U,9U},0U);
    expect(mesh.vertices[3].rgba8==0x7faf0c29U,
        "Original TIE nondyadic ordered ACC color was replaced by byte averaging");

    source.bounding_sphere_bits={0x3f800000U,0x40000000U,0x40400000U,0x40800000U};
    source.scale_bits=0x40000000U;source.lod_threshold_bits=thresholds;
    openrc::RacGameplayTieInstanceV1 instance;
    instance.matrix_bits={0x3f800000U,0U,0U,0U,0U,0x40000000U,0U,0U,
        0U,0U,0x40400000U,0U,0x41200000U,0x41a00000U,0x41f00000U,0U};
    const std::array<std::array<std::uint32_t,4>,4> view{{
        {0x3f800000U,0U,0U,0U},{0U,0x3f800000U,0U,0U},
        {0U,0U,0x3f800000U,0U},{0U,0U,0U,0x3f800000U}}};
    const auto selected=openrc::evaluate_rac_tie_instance_lod_v1(source,instance,
        {0x3f800000U,0x40000000U,0x40400000U},view);
    expect(selected.world_center_bits==std::array<std::uint32_t,3>{0x41400000U,0x41e00000U,0x42400000U}&&
        selected.camera_depth_bits==0x42340000U&&selected.selected_lod==2U,
        "TIE original transformed bounding center or camera depth differs");
}

} // namespace

int main() {
    try {
        test_lod0_source_ownership_and_order();
        test_source_lod_gates_and_morphs();
        test_fixed_source_position_rounding();
        test_rejections();
        std::cout << "OpenRC RAC1 lit TIE LOD0 compile tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC RAC1 lit TIE LOD0 compile tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
