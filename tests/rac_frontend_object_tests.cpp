#include "openrc/rac_frontend_object.hpp"

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <bit>
#include <algorithm>

namespace {
using namespace openrc;
using namespace openrc::game;
void check(bool value,const char *message) {if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F &&f) {
  bool rejected=false;try{f();}catch(const std::runtime_error &){rejected=true;}
  check(rejected,"Unsupported object owner was accepted");
}
RacFrontendObjectClassV1 object_class() {
  RacFrontendObjectClassV1 c;
  c.source_bytes=4096;
  c.allocation.class_id_bits=1138;c.allocation.class_table_index=4;
  RacMobyFreshModelV1 model;
  model.reference=0x400000;model.scale_bits=0x3f800000;
  model.byte_0c=2;model.sequence0=RacMobyFreshSequenceV1{0x400100,7,255,0};
  c.allocation.model=model;
  for(unsigned i=0;i<2;++i) {
    RacFrontendObjectAnimationClipV1 clip;
    clip.source_offset=0x80+0x100*i;clip.rate_override_bits=0x3f000000;
    clip.frame_rate_bits.assign(7,0x3f000000);
    for(unsigned j=0;j<7;++j)clip.frame_references.push_back(0x100+0x100*i+16*j);
    c.animations.clips.push_back(clip);
    c.post_headers.push_back({static_cast<std::uint8_t>(i),0x400080+0x100*i,{0,0,0,0}});
  }
  return c;
}
constexpr SessionStateLimitsV1 state_limits{8,16,64,4096,65536,262144,65536,131072,128};
void put(std::vector<std::byte>&out,std::uint32_t v) {
  for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::byte>((v>>(8*i))&255U));
}
void word(std::vector<std::byte>&out,std::size_t at,std::uint32_t value) {
  for(unsigned i=0;i<4;++i)out.at(at+i)=static_cast<std::byte>((value>>(8*i))&255U);
}
void halfword(std::vector<std::byte>&out,std::size_t at,std::uint16_t value) {
  out.at(at)=static_cast<std::byte>(value&255U);out.at(at+1)=static_cast<std::byte>(value>>8U);
}
std::vector<std::byte> geometry_class() {
  std::vector<std::byte> b(0x1c0);
  b[8]=std::byte{5};b[12]=std::byte{1};
  word(b,0x1c,0x180);word(b,0x24,0x44800000);word(b,0x3c,0x3f800000);word(b,0x48,0x60);
  b[0x70]=std::byte{2};b[0x71]=std::byte{255};
  for(unsigned f=0;f<2;++f) {
    const auto at=0x90U+f*0x70U;
    word(b,0x7c+4*f,at);word(b,at,0x3f000000);
    halfword(b,at+6,6);halfword(b,at+8,40);halfword(b,at+10,1);
    halfword(b,at+12,48);halfword(b,at+14,5);
    for(unsigned j=0;j<5;++j) {
      halfword(b,at+0x10+j*8+(j==0?4:6),j==0?0x8000:0x7fff);
      const auto record=at+0x40+j*8;
      halfword(b,record,static_cast<std::uint16_t>(j==0?10+f*2:j*2));
      halfword(b,record+2,static_cast<std::uint16_t>(j==0?20:j*3));
      halfword(b,record+4,static_cast<std::uint16_t>(j==0?30:j*4));
      halfword(b,record+6,static_cast<std::uint16_t>(j));
    }
  }
  word(b,0x180,4);
  const std::array<unsigned,4> joints{2,1,4,3};
  for(unsigned selector=0;selector<4;++selector) {
    const auto at=0x194+8*selector;word(b,0x184+4*selector,at);
    halfword(b,at,2);b[at+5]=static_cast<std::byte>(joints[selector]);
  }
  return b;
}
void geometry() {
  auto bytes=geometry_class();
  const auto decode=[&] {return decode_rac_frontend_object_class_v1(bytes,0x400000,4,0,4096);};
  const auto c=decode();
  check(c.corner_joints==std::array<std::uint8_t,4>{2,1,4,3},
        "Source metadata selectors were mistaken for direct joint indices");
  const auto seq=parse_rac_moby_sequence_v1(bytes,{0x60,0x120},{4096,4096,255,255});
  RacMobyBindRigV1 rig;rig.actor_rig.joints.resize(5);rig.source_common_translations.resize(5);
  for(unsigned j=0;j<5;++j)rig.actor_rig.joints[j].parent_index=j==0?-1:0;
  RacFrontendObjectV1 object;object.source_address=0x500010;
  object.post.model_reference=0x400000;object.post.scale_bits=0x44800000;
  object.post.matrix_bits={RacMobyPostVectorV1{0x40000000,0,0,0},
    RacMobyPostVectorV1{0,0x40400000,0,0},RacMobyPostVectorV1{0,0,0x40800000,0}};
  object.post.position_bits={0x42c80000,0x43480000,0x43960000,0};
  object.animation=set_rac_frontend_object_animation_v1({},c.animations,0,0).state;
  object.animation.phase_bits=0x3f000000;
  const auto points=sample_rac_frontend_object_corners_v1(object,c,seq,rig,{5,1024,5,5,1.e-8});
  for(unsigned i=0;i<4;++i) {
    const auto j=c.corner_joints[i];
    const RacMobyPostVectorV1 expected{
      std::bit_cast<std::uint32_t>(float(122-4*j)),
      std::bit_cast<std::uint32_t>(float(260-9*j)),
      std::bit_cast<std::uint32_t>(float(420+16*j)),0x3f800000};
    check(points[i]==expected,"Source class scale, previous post matrix, selector or position order differs");
  }
  auto wrong_sequence=seq;wrong_sequence.source_range.offset+=16;
  rejects([&]{(void)sample_rac_frontend_object_corners_v1(object,c,wrong_sequence,rig,{5,1024,5,5,1.e-8});});
  object.animation.references.current_frame_reference+=16;
  rejects([&]{(void)sample_rac_frontend_object_corners_v1(object,c,seq,rig,{5,1024,5,5,1.e-8});});
  for(const auto corrupt: {0U,1U,0xfffffff0U}) {
    word(bytes,0x184,corrupt);rejects([&]{(void)decode();});
  }
  bytes=geometry_class();halfword(bytes,0x194,0);rejects([&]{(void)decode();});
  bytes=geometry_class();bytes[0x199]=std::byte{5};rejects([&]{(void)decode();});
  bytes=geometry_class();word(bytes,0x180,0xffffffffU);rejects([&]{(void)decode();});
}
struct Pool {
  SessionStateInitialV1 initial;
  RacMobyAllocateBindingsV1 bindings;
  explicit Pool(unsigned count=14) {
    const std::uint64_t n=count+1;
    initial.schema.identity_key="test.frontend-objects";
    initial.schema.buffers={{"status",n},{"packed",n*8},{"pvar",n*128},{"scalars",8}};
    initial.schema.views={{"status","status",SessionStateValueTypeV1::u8,0,n,1},
      {"packed","packed",SessionStateValueTypeV1::u32,0,n*2,4},
      {"pvar","pvar",SessionStateValueTypeV1::u32,0,n*32,4},
      {"scalars","scalars",SessionStateValueTypeV1::u32,0,2,4}};
    initial.buffers={{"status",std::vector<std::byte>(n)},
      {"packed",std::vector<std::byte>(n*8)},
      {"pvar",std::vector<std::byte>(n*128,std::byte{0xa5})},{"scalars",{}}};
    initial.buffers[0].bytes[0]=std::byte{255};
    put(initial.buffers[3].bytes,7);put(initial.buffers[3].bytes,count);
    bindings.state_schema_sha256=hash_session_state_schema_v1(initial.schema,state_limits);
    bindings.cursor={0x500000,0x500000+256*count};
    bindings.first_status=PlacementStateElementV1{"status",0};
    bindings.first_packed_word=PlacementStateElementV1{"packed",0};
    bindings.first_pvar_word=PlacementStateElementV1{"pvar",0};
    bindings.current_timer_word=PlacementStateElementV1{"scalars",0};
    bindings.current_count_word=PlacementStateElementV1{"scalars",1};
    bindings.all_actor_base_bits=0x4fff00;bindings.pvar_base_bits=0x600000;
  }
};
void admission() {
  Pool pool;SessionStateV1 state(pool.initial,state_limits);auto c=object_class();
  std::array<std::uint32_t,14> seq{};
  const auto objects=admit_rac_frontend_objects_v1(c,seq,{0x3f800000,0x40000000,0x40400000},
    pool.bindings,state,0,{64,32});
  for(unsigned i=0;i<14;++i) {
    const auto &o=objects[i];
    check(o.source_address==0x500000+256*i && o.post.counter_word_bits==((i+1)<<16U)+1U &&
          o.post.header_cache_key==0 && o.post.flags==0 && o.post.live_index_bits==i+1 &&
          o.animation.indices.previous_frame==6 && o.animation.indices.current_frame==6 &&
          o.animation.rate_bits==0x3f000000 && o.callback_reference==0x23b578,
          "Source14-object admission, first post or sequence selection differs");
    check(o.post.position_bits==RacMobyPostVectorV1{0x3f800000,0x40000000,0x40400000,0} &&
          state.read_u8("status",i)==0 && state.read_u32("packed",i*2)==0x0e0e &&
          state.read_u32("packed",i*2+1)==0x00202020,
          "Source camera copy or canonical packed metadata differs");
    for(unsigned word=0;word<32;++word)
      check(state.read_u32("pvar",i*32+word)==0,"Admission did not clear its exact PVar block");
  }
  check(state.read_u8("status",14)==255 && state.read_u32("scalars",1)==0 &&
        state.read_u32("pvar",14*32)==0xa5a5a5a5,"Allocation guard or count differs");
  Pool small(13);SessionStateV1 insufficient(small.initial,state_limits);
  const auto before=insufficient.snapshot();
  rejects([&]{(void)admit_rac_frontend_objects_v1(c,seq,{},small.bindings,insufficient,0,{64,32});});
  check(insufficient.snapshot()==before,"Failed14-object admission leaked partial state");
}
void lifecycle() {
  auto c=object_class();std::array<RacFrontendObjectV1,14> objects;
  for(unsigned i=0;i<14;++i) {
    objects[i].source_address=0x500000+i*256;
    objects[i].animation=set_rac_frontend_object_animation_v1({},c.animations,0,6).state;
    objects[i].animation.speed_bits=0x3f800000;
  }
  RacFrontendObjectScreenV1 main;
  main.source_reference=0x1d4948;main.active_state=46;
  main.node_references[2]=0x1d4a18;
  RacFrontendObjectScreenV1 other;
  other.source_reference=0x1d5008;other.parent_reference=main.source_reference;other.active_state=31;
  other.sequences.fill(1);
  const std::array screens{main,other};
  RacFrontendObjectScreenStateV1 state{2,main.source_reference,main.source_reference,0,0};
  auto entered=step_rac_frontend_object_screen_v1(state,screens,objects,c);
  check(entered.began_transition && !entered.arrived && entered.sound_kind==3 &&
        entered.state.mode==1 && entered.state.current_screen==0 &&
        entered.state.remaining_updates==12 && entered.node_bindings==
          std::vector<RacFrontendObjectNodeBindingV1>{{main.node_references[2],objects[2].source_address}},
        "Original self-request entry must begin its12-update reverse transition");
  state=entered.state;
  check(objects[0].animation.speed_bits==0xbf800000,"Self-request did not reverse source animation");
  for(unsigned tick=0;tick<11;++tick) {
    auto step=step_rac_frontend_object_screen_v1(state,screens,objects,c);
    check(!step.arrived,"Source screen arrived before12updates");state=step.state;
  }
  auto arrived=step_rac_frontend_object_screen_v1(state,screens,objects,c);
  check(arrived.arrived && arrived.state.current_screen==main.source_reference &&
        arrived.state.requested_screen==0 && arrived.state.mode==46,
        "Source12-update arrival differs");
  state=arrived.state;state.requested_screen=other.source_reference;
  auto forward=step_rac_frontend_object_screen_v1(state,screens,objects,c);
  check(forward.sound_kind==4 && objects[0].animation.indices.current_sequence==1 &&
        objects[0].animation.speed_bits==0x3f800000,"Child screen did not select target forward sequence");
  state={31,other.source_reference,main.source_reference,0,0};
  (void)step_rac_frontend_object_screen_v1(state,screens,objects,c);
  check(objects[0].animation.indices.current_sequence==1 && objects[0].animation.speed_bits==0xbf800000,
        "Parent transition did not reverse current sequence");
  auto unhandled=screens;unhandled[1].cleanup_callbacks[0]=0x224010;unhandled[1].node_references[0]=1;
  const auto saved=objects;
  rejects([&]{(void)step_rac_frontend_object_screen_v1(state,unhandled,objects,c);});
  check(saved==objects,"Unexecuted node callback modified object state");
  for(const auto mode: {0U,2U,31U,46U,0xffffffffU}) {
    const RacFrontendObjectScreenStateV1 idle{mode,main.source_reference,0,0,0};
    check(step_rac_frontend_object_screen_v1(idle,screens,objects,c).state==idle,
          "Source non-transition mode acquired an invented request/idle gate");
  }
}

void source_comparison(const char *path) {
  std::ifstream in(path,std::ios::binary|std::ios::ate);
  check(bool(in),"Cannot open source object fixture");
  const auto length=in.tellg();check(length>=8 && length<=1048576,"Object fixture exceeds limit");
  in.seekg(0);std::vector<std::byte> bytes(static_cast<std::size_t>(length));
  in.read(reinterpret_cast<char*>(bytes.data()),length);check(bool(in),"Cannot read source object fixture");
  for(unsigned i=0;i<8;++i)check(bytes[i]==std::byte("FROOBJT1"[i]),"Object fixture magic differs");
  std::size_t cursor=8;
  const auto get=[&]() {
    check(cursor<=bytes.size() && bytes.size()-cursor>=4,"Truncated source object fixture");
    std::uint32_t value=0;
    for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(bytes[cursor++])<<(8*i);
    return value;
  };
  const auto class_bytes=get();check(class_bytes<=bytes.size()-cursor,"Truncated source object class");
  const auto c=decode_rac_frontend_object_class_v1(std::span(bytes).subspan(cursor,class_bytes),
    0x400000,4,0,1048576);
  cursor+=class_bytes;
  const auto screen_count=get();check(screen_count<=16,"Source screen bank exceeds limit");
  std::vector<RacFrontendObjectScreenV1> screens(screen_count);
  for(auto &screen:screens) {
    screen.source_reference=get();screen.parent_reference=get();screen.active_state=get();
    for(auto *array:{&screen.sequences,&screen.node_references,&screen.init_callbacks,&screen.cleanup_callbacks})
      for(auto &value:*array)value=get();
  }
  const auto root=[&]() {
    RacFrontendObjectScreenStateV1 state;
    state.mode=get();state.current_screen=get();state.requested_screen=get();
    state.previous_screen=get();state.remaining_updates=get();return state;
  };
  const auto animation=[&]() {
    RacFrontendObjectAnimationStateV1 state;
    state.indices.previous_frame=static_cast<std::uint8_t>(get());
    state.indices.current_frame=static_cast<std::uint8_t>(get());
    state.indices.previous_sequence=static_cast<std::uint8_t>(get());
    state.indices.current_sequence=static_cast<std::uint8_t>(get());
    state.references.previous_frame_reference=get();state.references.current_frame_reference=get();
    state.references.sound_byte=static_cast<std::uint8_t>(get());
    state.references.trigger_byte=static_cast<std::uint8_t>(get());
    state.phase_bits=get();state.speed_bits=get();state.rate_bits=get();
    state.flags=static_cast<std::uint8_t>(get());state.sound_handle=static_cast<std::uint8_t>(get());return state;
  };
  const auto count=get();check(count<=100,"Source screen cases exceed limit");
  unsigned node_writes=0;
  for(unsigned i=0;i<count;++i) {
    const auto before=root();std::array<RacFrontendObjectV1,14> objects;
    for(unsigned j=0;j<14;++j) {
      objects[j].source_address=0x600000+j*256;objects[j].animation=animation();
    }
    const auto after=root();std::array<RacFrontendObjectAnimationStateV1,14> expected;
    for(auto &state:expected)state=animation();
    const auto sounds=get();check(sounds<=1,"Unexpected source sound count");
    std::optional<std::uint32_t> sound_kind;
    if(sounds) {
      sound_kind=get();check(get()==17 && get()==objects[0].source_address,"Source sound ABI differs");
    }
    const auto nodes=get();check(nodes<=14,"Source node bindings exceed limit");
    std::vector<RacFrontendObjectNodeBindingV1> bindings;
    for(unsigned j=0;j<nodes;++j) {const auto node=get(),object=get();bindings.push_back({node,object});}
    const auto native=step_rac_frontend_object_screen_v1(before,screens,objects,c);
    check(native.state==after && native.sound_kind==sound_kind && native.node_bindings==bindings,
          "Source/native screen stage state, sound or node bindings differ");
    for(unsigned j=0;j<14;++j)check(objects[j].animation==expected[j],"Source/native14-object selection differs");
    node_writes+=nodes;
  }
  check(cursor==bytes.size(),"Trailing source object fixture bytes");
  std::cout<<"frontend_object_screen_source: "<<count<<" cases, "<<node_writes<<" node writes matched\n";
}
void corner_source_comparison(const char *path) {
  std::ifstream in(path,std::ios::binary|std::ios::ate);
  check(bool(in),"Cannot open source corner fixture");
  const auto length=in.tellg();check(length>=8 && length<=1048576,"Corner fixture exceeds limit");
  in.seekg(0);std::vector<std::byte> bytes(static_cast<std::size_t>(length));
  in.read(reinterpret_cast<char*>(bytes.data()),length);check(bool(in),"Cannot read corner fixture");
  for(unsigned i=0;i<8;++i)check(bytes[i]==std::byte("FROCORN1"[i]),"Corner fixture magic differs");
  std::size_t cursor=8;
  const auto get=[&]() {
    check(cursor<=bytes.size() && bytes.size()-cursor>=4,"Truncated source corner fixture");
    std::uint32_t value=0;
    for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(bytes[cursor++])<<(8*i);
    return value;
  };
  const auto size=get();check(size<=bytes.size()-cursor,"Truncated corner source class");
  const auto source=std::span(bytes).subspan(cursor,size);cursor+=size;
  const auto c=decode_rac_frontend_object_class_v1(source,0x400000,4,0,1048576);
  const auto model=parse_rac_moby_class_v1(source,{1048576,false});
  const auto rig=decode_rac_moby_bind_rig_v1(source,model,{1048576,255,1.e-8});
  const auto count=get();check(count<=1024,"Corner source cases exceed limit");
  for(unsigned i=0;i<count;++i) {
    const auto slot=get(),previous=get(),current=get(),phase=get();
    check(slot<c.animations.clips.size(),"Corner source slot is absent");
    const auto &clip=c.animations.clips[slot];
    const auto begin=model.sequence_offsets.at(slot);
    auto end=std::uint64_t(source.size());
    for(auto at:model.sequence_offsets)if(at>begin)end=std::min(end,std::uint64_t(at));
    const auto sequence=parse_rac_moby_sequence_v1(source,{begin,end-begin},{1048576,1048576,255,255});
    RacFrontendObjectV1 object;object.source_address=0x600000;
    object.post.model_reference=0x400000;object.post.scale_bits=model.scale_bits;
    for(unsigned axis=0;axis<3;++axis) {
      object.post.position_bits[axis]=get();object.post.matrix_bits[axis][axis]=0x3f800000;
    }
    object.animation=set_rac_frontend_object_animation_v1({},c.animations,slot,previous).state;
    check(current<clip.frame_references.size(),"Corner source current frame is absent");
    object.animation.indices.current_frame=static_cast<std::uint8_t>(current);
    object.animation.references.current_frame_reference=clip.frame_references[current];
    object.animation.phase_bits=phase;
    std::array<RacMobyPostVectorV1,4> expected;
    for(auto &point:expected)for(auto &v:point)v=get();
    const auto native=sample_rac_frontend_object_corners_v1(object,c,sequence,rig,{255,1048576,255,255,1.e-8});
    check(native==expected,"Source/reference versus native object corner words differ");
  }
  check(cursor==bytes.size(),"Trailing corner source fixture bytes");
  std::cout<<"frontend_object_corners_source: "<<count<<" cases, "<<count*16U<<" corner words matched\n";
}
} // namespace
int main(int argc,char **argv) {
  try {admission();lifecycle();geometry();check(argc<=3,"Expected optional screen and corner source fixtures");
    if(argc>=2)source_comparison(argv[1]);
    if(argc==3)corner_source_comparison(argv[2]);
    std::cout<<"rac_frontend_object_tests: 3 groups passed\n";return 0;}
  catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
