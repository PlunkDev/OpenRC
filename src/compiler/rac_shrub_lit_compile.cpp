#include "openrc/rac_shrub_lit_compile.hpp"
#include "openrc/dvp_vu_numeric.hpp"
#include "openrc/ee_cop1_numeric.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace openrc {
namespace {
[[noreturn]] void fail(const char* const message) {
    throw RacShrubLitCompileError(message);
}
float converted(const std::int16_t value, const std::uint32_t scale_bits) {
    const auto integer=ee_cop1_cvt_s_w_bits_v1(
        static_cast<std::uint32_t>(static_cast<std::int32_t>(value))).bits;
    const auto result=std::bit_cast<float>(dvp_vu_mul_bits_v1(integer,scale_bits).bits);
    if(!std::isfinite(result))fail("Shrub local coordinate exceeds neutral finite range");
    return result;
}
}

RenderSceneMeshV1 compile_rac_shrub_lit_base_mesh_v1(
    const RacShrubClassV1& model,
    const RacInstancePaletteV1& initial_palette,
    const std::span<const std::uint32_t> primitive_material_ids,
    const std::uint32_t mesh_id) {
    if(initial_palette.rgba.size()!=24U)
        fail("A lit shrub mesh requires its complete 24-color palette");
    if(primitive_material_ids.size()!=model.primitives.size())
        fail("A lit shrub mesh requires a material for every source primitive");
    const auto scale=std::bit_cast<float>(model.scale_bits);
    if(!std::isfinite(scale)||!(scale>0.0F))fail("A lit shrub mesh has invalid source scale");
    RenderSceneMeshV1 out;out.id=mesh_id;
    if(model.vertices.empty()||model.vertices.size()>std::numeric_limits<std::uint32_t>::max()||
        model.vertices.size()>out.vertices.max_size())fail("A lit shrub mesh has an invalid vertex envelope");
    const auto local_scale=dvp_vu_mul_bits_v1(model.scale_bits,0x3a800000U).bits;
    out.vertices.reserve(model.vertices.size());
    for(const auto& vertex:model.vertices) {
        const auto normal=vertex.normal_and_stop&0x7fffU;
        if(normal>=24U)fail("A lit shrub vertex has an invalid source palette index");
        if(vertex.sth_words[2]!=4096)
            fail("A lit shrub vertex leaves the qualified homogeneous-coordinate path");
        const auto& rgb=initial_palette.rgba[normal];
        const auto packed=static_cast<std::uint32_t>(rgb[0])|
            (static_cast<std::uint32_t>(rgb[1])<<8U)|
            (static_cast<std::uint32_t>(rgb[2])<<16U)|
            (static_cast<std::uint32_t>(initial_palette.rgba[0][3])<<24U);
        out.vertices.push_back({converted(vertex.position_words[0],local_scale),
            converted(vertex.position_words[1],local_scale),
            converted(vertex.position_words[2],local_scale),
            converted(vertex.sth_words[0],0x39800000U),
            converted(vertex.sth_words[1],0x39800000U),packed});
    }
    for(std::size_t i=0;i<model.primitives.size();++i) {
        const auto& primitive=model.primitives[i];
        if(primitive.material_index>=model.materials.size()||primitive.triangles.empty())
            fail("A lit shrub primitive has an invalid material or triangle envelope");
        if(primitive.triangles.size()>(out.triangle_indices.max_size()-out.triangle_indices.size())/3U)
            fail("A lit shrub mesh exceeds its triangle output limit");
        const auto first=out.triangle_indices.size();
        for(const auto& triangle:primitive.triangles)
            for(const auto vertex:triangle) {
                if(vertex>=out.vertices.size())fail("A lit shrub triangle references an absent vertex");
                out.triangle_indices.push_back(vertex);
            }
        out.draw_ranges.push_back({primitive_material_ids[i],first,out.triangle_indices.size()-first});
    }
    if(out.triangle_indices.empty())fail("A lit shrub mesh has no source triangles");
    return out;
}
} // namespace openrc
