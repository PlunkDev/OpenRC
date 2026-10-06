#include "openrc/rac_shrub_lit_compile.hpp"
#include <bit>
#include <iostream>

namespace {
void check(const bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
openrc::RacShrubClassV1 model() {
    openrc::RacShrubClassV1 out;out.scale_bits=std::bit_cast<std::uint32_t>(0.5F);
    out.mode_bits=4U; // Wind changes the instance basis, never these local bytes.
    out.materials.resize(1U);
    for(std::uint16_t i=0;i<3;++i) {
        openrc::RacShrubVertexV1 vertex;
        vertex.position_words={1024,-2048,512};vertex.sth_words={-2048,8192,4096};
        vertex.normal_and_stop=static_cast<std::uint16_t>(0x8000U+i);
        out.vertices.push_back(vertex);
    }
    openrc::RacShrubPrimitiveV1 p;p.material_index=0U;p.triangles={{{2U,1U,0U}}};
    out.primitives.push_back(p);p.triangles={{{0U,1U,2U}}};out.primitives.push_back(p);
    return out;
}
openrc::RacInstancePaletteV1 palette() {
    openrc::RacInstancePaletteV1 out;
    for(unsigned i=0;i<24;++i)out.rgba.push_back({static_cast<std::uint8_t>(i),64U,255U,
        static_cast<std::uint8_t>(128U-i)});
    return out;
}
}
int main()try {
    const auto mesh=openrc::compile_rac_shrub_lit_base_mesh_v1(model(),palette(),
        std::array<std::uint32_t,2>{9U,3U},7U);
    check(mesh.id==7U&&mesh.vertices.size()==3U&&mesh.vertices[2].rgba8==0x80ff4002U,
        "Shrub RGB palette index, stop bit or per-instance alpha differs");
    check(mesh.vertices[0].x==0.5F&&mesh.vertices[0].y==-1.0F&&mesh.vertices[0].z==0.25F&&
        mesh.vertices[0].u==-0.5F&&mesh.vertices[0].v==2.0F,"Shrub source coordinate conversion differs");
    check(mesh.triangle_indices==std::vector<std::uint32_t>{2,1,0,0,1,2}&&
        mesh.draw_ranges==std::vector<openrc::RenderSceneDrawRangeV1>{{9,0,3},{3,3,3}},
        "Shrub source primitive/material order changed");
    for(unsigned mutation=0;mutation<3;++mutation) {
        auto source=model();auto colors=palette();
        if(mutation==0)source.vertices[0].normal_and_stop=24U;
        if(mutation==1)colors.rgba.resize(23U);
        if(mutation==2)source.primitives[0].triangles[0][0]=3U;
        bool rejected=false;
        try{(void)openrc::compile_rac_shrub_lit_base_mesh_v1(source,colors,
            std::array<std::uint32_t,2>{9U,3U},7U);}
        catch(const openrc::RacShrubLitCompileError&){rejected=true;}
        check(rejected,"Malformed shrub lighting input was accepted");
    }
    std::cout<<"Shrub lit base mesh source binding and ordering passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
