#include "openrc/rac_tie_lit_compile.hpp"
#include "openrc/dvp_vu_numeric.hpp"
#include "openrc/ee_cop1_numeric.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <string>

namespace openrc {
namespace {

[[noreturn]] void fail(const char* const message) {
    throw RacTieLitCompileError(message);
}

[[nodiscard]] std::uint32_t packed_color(
    const std::array<std::uint8_t, 4U>& rgba) noexcept {
    return static_cast<std::uint32_t>(rgba[0U]) |
        (static_cast<std::uint32_t>(rgba[1U]) << 8U) |
        (static_cast<std::uint32_t>(rgba[2U]) << 16U) |
        (static_cast<std::uint32_t>(rgba[3U]) << 24U);
}

using Acc=DvpVuAccumulatorLaneV1;
std::uint32_t mul(std::uint32_t a,std::uint32_t b) {return dvp_vu_mul_bits_v1(a,b).bits;}
std::uint32_t add(std::uint32_t a,std::uint32_t b) {return dvp_vu_add_bits_v1(a,b).bits;}
std::uint32_t sub(std::uint32_t a,std::uint32_t b) {return dvp_vu_sub_bits_v1(a,b).bits;}
Acc mula(std::uint32_t a,std::uint32_t b) {
    const auto r=dvp_vu_mul_bits_v1(a,b);return {r.bits,r.overflow};
}
Acc madda(Acc a,std::uint32_t b,std::uint32_t c) {
    const auto r=dvp_vu_madd_bits_v1(a,b,c).result;return {r.bits,r.overflow};
}
std::uint32_t madd(Acc a,std::uint32_t b,std::uint32_t c) {
    return dvp_vu_madd_bits_v1(a,b,c).result.bits;
}
std::uint32_t integer(std::int16_t value) {
    return ee_cop1_cvt_s_w_bits_v1(static_cast<std::uint32_t>(static_cast<std::int32_t>(value))).bits;
}
std::uint32_t morphed_color(const RacTieVertexV1& vertex,
    const RacInstancePaletteV1& palette,const RacTieLodSelectionV1& selection) {
    if(vertex.lighting_palette_index_count==1U)
        return packed_color(palette.rgba[vertex.lighting_palette_indices[0]]);
    std::uint32_t packed=0U;
    for(unsigned lane=0;lane<4;++lane) {
        const auto c0=0x4b000000U+palette.rgba[vertex.lighting_palette_indices[0]][lane];
        const auto c1=0x4b000000U+palette.rgba[vertex.lighting_palette_indices[1]][lane];
        const auto c2=0x4b000000U+palette.rgba[vertex.lighting_palette_indices[2]][lane];
        const auto average=madd(mula(c1,0x3f000000U),c2,0x3f000000U);
        const auto final=madd(mula(average,selection.morph_weight_bits[3]),c0,
            selection.morph_weight_bits[2]);
        packed|=(final&255U)<<(lane*8U);
    }
    return packed;
}

} // namespace

RacTieLodSelectionV1 select_rac_tie_lod_v1(
    const std::array<std::uint32_t,3U> thresholds,
    const std::uint32_t depth) {
    if((depth>>31U)!=0U||!std::isfinite(std::bit_cast<float>(depth)))
        fail("TIE LOD selection requires the original nonnegative finite depth");
    for(unsigned i=0;i<3;++i)
        if((thresholds[i]>>31U)!=0U||!std::isfinite(std::bit_cast<float>(thresholds[i]))||
            (i!=0U&&thresholds[i]<=thresholds[i-1U]))
            fail("TIE LOD thresholds are not finite increasing source values");
    const std::array delta{sub(thresholds[0],depth),sub(thresholds[1],depth),sub(thresholds[2],depth)};
    RacTieLodSelectionV1 out;out.camera_depth_bits=depth;
    std::uint32_t t=0U;
    if((delta[0]>>31U)==0U)out.selected_lod=0U;
    else if((delta[2]>>31U)!=0U)out.selected_lod=2U;
    else {
        out.selected_lod=(delta[1]>>31U)==0U?0U:1U;
        const auto index=out.selected_lod;
        t=dvp_vu_div_bits_v1(sub(0U,delta[index]),sub(thresholds[index+1U],thresholds[index]));
    }
    const auto weight=mul(0x43800000U,t);
    out.morph_weight_bits={t,0U,sub(0x43800000U,weight),weight};
    return out;
}

RacTieLodSelectionV1 evaluate_rac_tie_instance_lod_v1(
    const RacTieClassV1& model,const RacGameplayTieInstanceV1& instance,
    const std::array<std::uint32_t,3U>& camera,
    const std::array<std::array<std::uint32_t,4U>,4U>& view) {
    std::array<std::uint32_t,3U> center{},relative{};
    for(unsigned lane=0;lane<3;++lane) {
        auto acc=mula(instance.matrix_bits[lane],model.bounding_sphere_bits[0]);
        acc=madda(acc,instance.matrix_bits[4U+lane],model.bounding_sphere_bits[1]);
        acc=madda(acc,instance.matrix_bits[8U+lane],model.bounding_sphere_bits[2]);
        const auto transformed=madd(acc,0U,model.bounding_sphere_bits[3]);
        center[lane]=add(mul(transformed,model.scale_bits),instance.matrix_bits[12U+lane]);
        relative[lane]=sub(center[lane],camera[lane]);
    }
    auto acc=mula(view[0][2],relative[0]);
    acc=madda(acc,view[1][2],relative[1]);
    auto depth=madd(acc,view[2][2],relative[2]);
    if((depth>>31U)!=0U)depth=0U; // VMAX.z VF0.z,view-center.z.
    auto out=select_rac_tie_lod_v1(model.lod_threshold_bits,depth);
    out.world_center_bits=center;return out;
}

RenderSceneMeshV1 compile_rac_tie_lit_mesh_v1(
    const RacTieClassV1& model,
    const RacInstancePaletteV1& initial_palette,
    const RacTieLodSelectionV1& selection,
    const std::span<const std::uint32_t> material_ids,
    const std::uint32_t mesh_id) {
    const auto t=selection.morph_weight_bits[0];
    const auto weight=mul(0x43800000U,t);
    if(selection.selected_lod!=model.selected_lod||selection.selected_lod>2U||t>0x3f800000U||
        selection.morph_weight_bits!=std::array<std::uint32_t,4U>{t,0U,sub(0x43800000U,weight),weight})
        fail("A lit RAC1 TIE mesh disagrees with its source LOD/morph selection");
    if (initial_palette.rgba.size() != 64U) {
        fail("A lit RAC1 TIE mesh requires its complete 64-color palette");
    }
    if (material_ids.size() != model.texture_count) {
        fail("A lit RAC1 TIE mesh requires every class-local material slot");
    }
    if (!std::isfinite(std::bit_cast<float>(model.scale_bits)) ||
        !(std::bit_cast<float>(model.scale_bits) > 0.0F)) {
        fail("A lit RAC1 TIE mesh has an invalid source scale");
    }
    const auto local_scale = mul(model.scale_bits, 0x3a800000U);
    RenderSceneMeshV1 result;
    result.id = mesh_id;
    if (model.vertices.empty() || model.triangles.empty() ||
        model.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        model.vertices.size() > result.vertices.max_size() ||
        model.triangles.size() > result.triangle_indices.max_size() / 3U) {
        fail("A lit RAC1 TIE mesh has an invalid geometry envelope");
    }
    result.vertices.reserve(model.vertices.size());
    for (const auto& source : model.vertices) {
        const auto expected_count = source.source_range.size == 0x10U ? 1U :
            source.source_range.size == 0x18U ? 3U : 0U;
        if (expected_count == 0U ||
            source.lighting_palette_index_count != expected_count) {
            fail("A lit RAC1 TIE vertex lacks its source lighting indices");
        }
        if (source.stq[2U] != 4096U) {
            fail("A lit RAC1 TIE vertex leaves the qualified homogeneous-coordinate path");
        }
        for (std::size_t lane = 0U; lane < expected_count; ++lane) {
            if (source.lighting_palette_indices[lane] >= 64U) {
                fail("A lit RAC1 TIE vertex has an invalid lighting index");
            }
        }
        for (const auto position : source.position) {
            if (!std::isfinite(position)) {
                fail("A lit RAC1 TIE vertex has a non-finite position");
            }
        }
        for (const auto coordinate : source.texture_coordinate) {
            if (!std::isfinite(coordinate)) {
                fail("A lit RAC1 TIE vertex has a non-finite texture coordinate");
            }
        }
        std::array<float,3U> position{};
        for(unsigned axis=0;axis<3;++axis) {
            auto moved=integer(source.quantized_position[axis]);
            if(expected_count==3U) {
                // VU1188..11d0: signed delta*t, then ADD with signed base.
                moved=add(mul(integer(source.quantized_morph_delta[axis]),t),moved);
            }
            // Fixed/dinky vertices need this source multiplication too: the
            // parser's convenient host-float position can round one bit away.
            position[axis]=std::bit_cast<float>(mul(moved,local_scale));
            if(!std::isfinite(position[axis]))fail("A TIE position exceeds neutral finite range");
        }
        result.vertices.push_back({
            position[0U], position[1U], position[2U],
            source.texture_coordinate[0U], source.texture_coordinate[1U],
            morphed_color(source,initial_palette,selection),
        });
    }
    result.triangle_indices.reserve(model.triangles.size() * 3U);
    std::size_t next_triangle = 0U;
    for (const auto& primitive : model.primitives) {
        if (primitive.triangle_begin != next_triangle ||
            primitive.triangle_count > model.triangles.size() - next_triangle ||
            primitive.local_texture_index >= material_ids.size()) {
            fail("A lit RAC1 TIE primitive has an invalid source triangle range");
        }
        const auto first_index = result.triangle_indices.size();
        for (std::size_t index = 0U; index < primitive.triangle_count; ++index) {
            const auto& triangle = model.triangles[next_triangle++];
            if (triangle.local_texture_index != primitive.local_texture_index) {
                fail("A lit RAC1 TIE triangle disagrees with its source material");
            }
            for (const auto vertex : triangle.vertex_indices) {
                if (vertex >= model.vertices.size()) {
                    fail("A lit RAC1 TIE triangle references an absent vertex");
                }
                result.triangle_indices.push_back(vertex);
            }
        }
        if (primitive.triangle_count != 0U) {
            result.draw_ranges.push_back({
                material_ids[primitive.local_texture_index], first_index,
                result.triangle_indices.size() - first_index,
            });
        }
    }
    if (next_triangle != model.triangles.size()) {
        fail("A lit RAC1 TIE mesh has triangles outside its source primitives");
    }
    return result;
}

RenderSceneMeshV1 compile_rac_tie_lit_lod0_mesh_v1(
    const RacTieClassV1& model,const RacInstancePaletteV1& initial_palette,
    const std::span<const std::uint32_t> material_ids,const std::uint32_t mesh_id) {
    return compile_rac_tie_lit_mesh_v1(model,initial_palette,{},material_ids,mesh_id);
}

} // namespace openrc
