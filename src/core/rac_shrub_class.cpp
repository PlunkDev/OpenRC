#include "openrc/rac_shrub_class.hpp"
#include "openrc/scene_block_vif.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string>

namespace openrc {
namespace {
[[noreturn]] void fail(const char* what) {throw RacShrubClassError(what);}
void range(std::span<const std::byte> b,std::uint64_t p,std::uint64_t n) {
  if(p>b.size()||n>b.size()-p) fail("Shrub source range exceeds class envelope");
}
std::uint64_t word(std::span<const std::byte> b,std::uint64_t p,unsigned n=4) {
  range(b,p,n);std::uint64_t out=0;
  for(unsigned i=0;i<n;++i) out|=std::uint64_t(std::to_integer<unsigned>(b[p+i]))<<(i*8);
  return out;
}
std::int16_t i16(std::span<const std::byte> b,std::uint64_t p) {
  return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word(b,p,2)));
}
enum class Kind {tag, material, vertex};
struct Event {std::uint32_t offset=0,index=0;Kind kind{};};
}
RacShrubClassV1 parse_rac_shrub_class_v1(std::span<const std::byte> bytes,
    RacShrubClassLimitsV1 limits) {
  if(bytes.size()<0x40||bytes.size()>limits.max_input_bytes||!limits.max_packets||
      !limits.max_vertices||!limits.max_triangles) fail("Invalid shrub class envelope or limits");
  RacShrubClassV1 out;
  for(unsigned i=0;i<4;++i) out.bounding_sphere_bits[i]=static_cast<std::uint32_t>(word(bytes,i*4));
  out.scale_bits=static_cast<std::uint32_t>(word(bytes,0x20));
  out.mip_distance_bits=static_cast<std::uint32_t>(word(bytes,0x10));
  out.class_id=static_cast<std::uint16_t>(word(bytes,0x24,2));
  out.mode_bits=static_cast<std::uint16_t>(word(bytes,0x14,2));
  const auto scale=std::bit_cast<float>(out.scale_bits);
  if(!std::isfinite(scale)||!(scale>0)) fail("Invalid shrub model scale");
  for(auto bits:out.bounding_sphere_bits)
    if(!std::isfinite(std::bit_cast<float>(bits))) fail("Nonfinite shrub bounding sphere");
  out.packet_count=static_cast<std::uint32_t>(word(bytes,0x28,2));
  if(!out.packet_count||out.packet_count>limits.max_packets) fail("Shrub packet count exceeds limits");
  range(bytes,0x40,std::uint64_t(out.packet_count)*8);
  const auto normal_at=word(bytes,0x2c);
  range(bytes,normal_at,24*8);
  for(unsigned n=0;n<24;++n)
    for(unsigned a=0;a<4;++a) out.normals[n][a]=i16(bytes,normal_at+n*8+a*2);
  const auto billboard=word(bytes,0x1c);
  if(billboard) {
    range(bytes,billboard,64);
    out.billboard_source.assign(bytes.begin()+billboard,bytes.begin()+billboard+64);
  }
  std::uint64_t triangles=0;
  for(std::uint32_t pi=0;pi<out.packet_count;++pi) {
    const auto at=word(bytes,0x40+std::uint64_t(pi)*8);
    const auto size=word(bytes,0x44+std::uint64_t(pi)*8);
    range(bytes,at,size);
    if(!size) fail("Empty shrub VIF packet");
    const auto packet=bytes.subspan(at,size);
    SceneBlockVifStreamV1 stream;
    try {stream=parse_scene_block_vif_stream_v1(packet,{limits.max_input_bytes,4096,limits.max_input_bytes});}
    catch(const SceneBlockVifError& e) {throw RacShrubClassError(std::string("Shrub VIF: ")+e.what());}
    std::vector<SceneBlockVifCommandV1> unpacks;
    for(const auto& cmd:stream.commands) {
      if(cmd.output_vector_count) {
        if(!cmd.use_tops||cmd.unsigned_data||cmd.input_vector_count!=cmd.output_vector_count)
          fail("Shrub VIF uses unsupported unpack addressing");
        unpacks.push_back(cmd);
      } else if(cmd.opcode==SceneBlockVifOpcode::stmod && cmd.immediate!=0)
        fail("Shrub VIF uses nonzero STMOD");
      else if(cmd.opcode==SceneBlockVifOpcode::stcycl && cmd.immediate!=0x404)
        fail("Shrub VIF uses a noncontiguous cycle");
      else if(cmd.opcode!=SceneBlockVifOpcode::nop&&cmd.opcode!=SceneBlockVifOpcode::stmod&&
              cmd.opcode!=SceneBlockVifOpcode::stcycl) fail("Unsupported shrub VIF control");
    }
    if(unpacks.size()!=3||unpacks[0].opcode!=SceneBlockVifOpcode::unpack_v4_32||
       unpacks[1].opcode!=SceneBlockVifOpcode::unpack_v4_16||
       unpacks[2].opcode!=SceneBlockVifOpcode::unpack_v4_16||unpacks[0].destination_address!=0)
      fail("Shrub packet does not contain its three source arrays");
    const auto h=unpacks[0].payload_range.offset;
    const auto nt=word(packet,h),ng=word(packet,h+4),nv=word(packet,h+8),vo=word(packet,h+12);
    if(!nv||!ng||nv>limits.max_vertices-out.vertices.size()||nt>256||ng>256||
       1+ng+nt*4!=unpacks[0].output_vector_count||nv!=unpacks[1].output_vector_count||
       nv!=unpacks[2].output_vector_count||vo!=1+ng+nt*4||
       unpacks[1].destination_address!=vo||unpacks[2].destination_address!=vo+nv)
      fail("Shrub VIF counts or destinations disagree with packet header");
    const auto p=unpacks[1].payload_range.offset,a=unpacks[2].payload_range.offset;
    const auto base=static_cast<std::uint32_t>(out.vertices.size());
    std::vector<Event> events;
    for(std::uint32_t n=0;n<ng;++n)
      events.push_back({static_cast<std::uint32_t>(word(packet,h+16+n*16+12)),n,Kind::tag});
    for(std::uint32_t n=0;n<nt;++n)
      events.push_back({static_cast<std::uint32_t>(word(packet,h+16+ng*16+n*64+12)),n,Kind::material});
    for(std::uint32_t n=0;n<nv;++n) {
      RacShrubVertexV1 v;
      for(unsigned axis=0;axis<3;++axis) {
        v.position_words[axis]=i16(packet,p+n*8+axis*2);
        v.sth_words[axis]=i16(packet,a+n*8+axis*2);
      }
      v.normal_and_stop=static_cast<std::uint16_t>(word(packet,a+n*8+6,2));
      if((v.normal_and_stop&0x7fff)>=24) fail("Shrub vertex normal index exceeds source table");
      v.position_source_offset=at+p+n*8;v.attributes_source_offset=at+a+n*8;
      out.vertices.push_back(v);
      const auto offset=i16(packet,p+n*8+6);
      if(offset<0) fail("Negative shrub GS output address");
      events.push_back({static_cast<std::uint32_t>(offset),n,Kind::vertex});
    }
    std::stable_sort(events.begin(),events.end(),[](const Event& x,const Event& y){return x.offset<y.offset;});
    std::uint32_t cursor=0,material=UINT32_MAX,remaining=0;
    RacShrubPrimitiveV1* primitive=nullptr;
    const Event* previous=nullptr;
    for(const auto& event:events) {
      if(previous&&event.offset==previous->offset&&event.kind==Kind::vertex&&previous->kind==Kind::vertex) {
        const auto& x=out.vertices[base+event.index];const auto& y=out.vertices[base+previous->index];
        if(x.position_words!=y.position_words||x.sth_words!=y.sth_words||
           (x.normal_and_stop&0x7fff)!=(y.normal_and_stop&0x7fff))
          fail("Shrub padding overwrites a vertex with different data");
        ++out.duplicate_padding_writes;continue;
      }
      if(event.offset!=cursor) fail("Shrub GS writes overlap or leave a hole");
      if(event.kind==Kind::material) {
        if(remaining) fail("Shrub material interrupts a primitive");
        const auto m=h+16+ng*16+std::uint64_t(event.index)*64;
        if(word(packet,m+8)!=0x14||word(packet,m+24,8)!=8||
           word(packet,m+40,8)!=0x34||word(packet,m+56,8)!=6)
          fail("Shrub material register destinations differ");
        RacShrubMaterialV1 state;
        state.tex1=word(packet,m,8);state.clamp=word(packet,m+16,8);
        state.miptbp1=word(packet,m+32,8);state.tex0=word(packet,m+48,8);
        state.local_texture_index=static_cast<std::uint32_t>(state.tex0);
        if(state.local_texture_index>=16) fail("Shrub local texture slot exceeds directory");
        const auto found=std::find(out.materials.begin(),out.materials.end(),state);
        material=static_cast<std::uint32_t>(found-out.materials.begin());
        if(found==out.materials.end()) out.materials.push_back(state);
        cursor+=5;
      } else if(event.kind==Kind::tag) {
        if(remaining||material==UINT32_MAX) fail("Shrub primitive lacks a completed material binding");
        const auto t=h+16+event.index*16;
        const auto low=word(packet,t,8),high=word(packet,t+8);
        const auto kind=(low>>47)&7;
        if(((low>>58)&3)!=0||((low>>60)&15)!=3||!(low&(UINT64_C(1)<<46))||
           high!=0x412||(kind!=3&&kind!=4)) fail("Unsupported shrub packed primitive tag");
        remaining=static_cast<std::uint32_t>(low&0x7fff);
        if(remaining<3||remaining>nv||(kind==3&&remaining%3)) fail("Invalid shrub primitive vertex count");
        out.primitives.push_back({pi,material,low,high,{},{}});
        primitive=&out.primitives.back();++cursor;
      } else {
        if(!primitive||!remaining) fail("Shrub vertex has no primitive consumer");
        primitive->vertex_indices.push_back(base+event.index);--remaining;cursor+=3;
        const auto n=primitive->vertex_indices.size();
        const auto kind=(primitive->tag_low>>47)&7;
        if(n>=3&&(kind==4||n%3==0)) {
          if(++triangles>limits.max_triangles) fail("Shrub triangle count exceeds limits");
          auto tri=std::array{primitive->vertex_indices[n-3],primitive->vertex_indices[n-2],primitive->vertex_indices[n-1]};
          if(kind==4&&n%2==0) std::swap(tri[0],tri[1]);
          primitive->triangles.push_back(tri);
        }
      }
      previous=&event;
    }
    if(remaining) fail("Shrub packet ends before its primitive");
  }
  return out;
}
} // namespace openrc
