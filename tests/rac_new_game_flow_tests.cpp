#include "openrc/rac_new_game_flow.hpp"
#include "openrc/rac_startup.hpp"
#include "openrc/hash.hpp"

#include <algorithm>
#include <iostream>
#include <fstream>

namespace {
using namespace openrc;using K=FrontendSequenceCueKindV1;
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
template<class F>void rejects(F &&f){try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error("Invalid source flow was accepted");}
struct Fixture {
  SessionStateLimitsV1 limits{1,1,64,256,0x200000U,0x200000U,0x200000U,0x200000U,1048576};
  SessionStateSchemaV1 schema{"fixture/state",{{"owned",0x200000U}},{{"owned-bytes","owned",SessionStateValueTypeV1::u8,0,0x200000U,1}}};
  std::vector<RacFrontendStateBindingV1> bindings{{0x100000U,0x200000U,"owned-bytes",0}};
  RacFrontendResetTemplateV1 reset;
  RacNewGameFlowInputsV1 input;
  RacNewGameFlowResourcesV1 resources;
  Fixture() {
    // Synthetic destination effects, not copied original game data. The
    // existing reset-parser target independently verifies all267 real copies.
    for(unsigned i=0;i<266;++i)reset.copies.push_back({0x20bed0U,0x200000U+i,{static_cast<std::byte>(i)}});
    reset.copies.push_back({0x20bed0U,0x13de60U,{std::byte{0}}});
    auto &n=input.no_save;n.focused=true;n.node_address=0x1d5008;n.ui_mode_15efb0=1;
    n.card_type=2;n.card_e4=UINT32_MAX;n.root_154=11;n.pressed=0x20;
    n.preserved_eef0_eeec_eee8={0xabcdef01,0x23456789,0xdeadbeef};
    for(unsigned i=0;i<3;++i) {
      std::array<std::byte,1> bytes{static_cast<std::byte>(i)};
      resources.loading_cards[i]={"card/"+std::to_string(i),"fixture/loading",prepared_content_sha256_v1(bytes)};
      bytes[0]|=std::byte{0x80};resources.movies[i]={"movie/"+std::to_string(i),"fixture/media",prepared_content_sha256_v1(bytes)};
    }
  }
  RacNewGameFlowV1 run(){return compile_rac_new_game_flow_v1(input,&reset,resources,bindings,schema,limits);}
};
void sequence_order() {
  for(auto language:{0U,2U,3U,4U,5U})for(bool pal:{false,true}) {
    Fixture f;f.input.language_15ee88=language;f.input.time_scale_bits=pal?0x3f555555U:0x3f800000U;
    f.input.updates_per_second=pal?50U:60U;f.input.video_selector_15ee80=pal?1U:0U;
    for(unsigned i=0;i<3;++i) {
      const auto payload=encode_frame_color_transfer_sequence_v1(compile_rac_frontend_fade_v1(i==0?(pal?5U:6U):i==1?2U:4U,f.input.updates_per_second));
      f.resources.fades[i]={"fade/"+std::to_string(i),"openrc.frame-color-transfers",prepared_content_sha256_v1(payload)};
    }
    const auto out=f.run();check(out.sequence.has_value()&&out.input_effects.requested_new_game,"No-save source input did not request its sequence");
    check(out.audio_channel==language&&out.loading_language_slot==(language?language-1:0),"Source card/audio language domains were conflated");
    const auto &p=*out.sequence;std::vector<unsigned> cards,movies,fades,levels;unsigned load=0,third_card=0,level_zero=0;
    for(unsigned i=0;i<p.cues.size();++i) {
      const auto &c=p.cues[i];
      if(c.kind==K::loading_overlay){cards.push_back(c.updates);check(c.resource_index==2U*(cards.size()-1U),"Authored loading resource order changed");if(cards.size()==3U)third_card=i;}
      if(c.kind==K::media)movies.push_back(c.resource_index);
      if(c.kind==K::fade){check(c.resource_index==(fades.empty()?6U:(fades.size()%2?7U:8U)),"Source fade binds the wrong prepared transfer sequence");fades.push_back(c.updates);}
      check(c.kind!=K::current_level,"Source level assignment invented a second host consumer");
      if(i&&c.kind==K::session_writes&&c.writes.size()==4&&c.writes[0].element_index==0x5ee84) {
        std::uint32_t value=0;for(unsigned lane=0;lane<4;++lane)value|=c.writes[lane].value_bits<<(8*lane);
        levels.push_back(value);if(!value)level_zero=i;
      }
      if(c.kind==K::start_level_load){check(load==0&&c.level_id==0,"Repeated or wrong level load");load=i;}
    }
    check(cards==(pal?std::vector<unsigned>{200,150,200}:std::vector<unsigned>{240,180,240}),"Original card timers changed");
    check(movies==std::vector<unsigned>{1,3,5}&&fades==std::vector<unsigned>{pal?5U:6U,2,4,2,4,2,4},"Three-film/fade source order changed");
    check(levels==std::vector<unsigned>{UINT32_MAX,0}&&level_zero<load&&load<third_card,"Veldin source assignment/load order changed");
    check(out.movie_toc_offsets==std::array<std::uint32_t,3>{pal?0x1998U:0x1938U,pal?0x19a0U:0x1940U,pal?0x19a8U:0x1948U},"Source movie selection differs");
    check(encode_frontend_sequence_v1(decode_frontend_sequence_v1(encode_frontend_sequence_v1(p)))==encode_frontend_sequence_v1(p),"Compiled sequence roundtrip differs");
  }
}
void source_input_and_ownership() {
  Fixture partial;partial.resources.fades[0]={"fade/initial","openrc.frame-color-transfers",{}};
  rejects([&]{(void)partial.run();});partial.resources.fades[0].resource_id.clear();rejects([&]{(void)partial.run();});
  Fixture f;f.input.no_save.pressed=0;
  auto out=f.run();check(!out.sequence&& !out.input_state_writes.empty(),"No-action input invented a cutscene sequence or dropped current effects");
  f.input.no_save.focused=false;out=f.run();check(!out.sequence&&out.input_state_writes.empty(),"Unfocused callback produced effects");
  f.input.no_save.focused=true;f.input.no_save.pressed=0x40;rejects([&]{(void)f.run();});
  f.input.no_save.pressed=0x20;f.reset.copies.back().bytes[0]=std::byte{1};rejects([&]{(void)f.run();});
  f.reset.copies.back().bytes[0]=std::byte{0};f.bindings[0].byte_count=1;rejects([&]{(void)f.run();});
  f.bindings[0].byte_count=0x200000;f.input.language_15ee88=1;rejects([&]{(void)f.run();});
  f.input.language_15ee88=0;f.bindings.push_back(f.bindings[0]);rejects([&]{(void)f.run();});
  Fixture mapped;const auto good=mapped.run();
  game::SessionStateV1 state(SessionStateInitialV1{mapped.schema,{{"owned",std::vector<std::byte>(0x200000,std::byte{0xa5})}}},mapped.limits);
  FrontendSequencePlayerV1 player(*good.sequence,good.sequence->resources,{});
  player.apply_session_writes(state);
  for(const auto &w:good.input_state_writes) {
    // The final source bytes are checked by replay into a separate synthetic
    // expected buffer below; overlapping writes intentionally retain order.
    check(w.value_type==SessionStateValueTypeV1::u8,"Write lowering manufactured a different canonical width");
  }
  std::vector<std::byte> expected(0x200000,std::byte{0xa5});
  for(const auto &w:good.input_effects.writes)std::copy(w.bytes.begin(),w.bytes.end(),expected.begin()+w.source_address-0x100000U);
  const auto actual=state.buffer_bytes("owned");check(std::equal(actual.begin(),actual.end(),expected.begin()),"Native state lowering lost source bytes or write order");
  check(player.command().phase==FrontendSequencePhaseV1::incomplete,"Unknown cleanup barrier was treated as success");
  const auto live_continuation=compile_rac_new_game_continuation_v1(mapped.input,mapped.resources,mapped.bindings,mapped.schema,mapped.limits);
  check(live_continuation.sequence&&live_continuation.input_state_writes.empty()&&
      live_continuation.input_effects.writes.empty(),"Static continuation manufactured a future input snapshot");
  auto complete=*good.sequence;complete.cues.erase(complete.cues.begin());
  check(encode_frontend_sequence_v1(complete)==encode_frontend_sequence_v1(*live_continuation.sequence),
      "Live continuation differs from actual source presentation tail");
}
void skip_gates() {
  constexpr std::uint64_t chord=(std::uint64_t(0x8000)<<28)|0xfU;
  for(auto mode:{-1,0,1,2,3}) {
    Fixture f;f.input.movie_mode=mode;const auto out=f.run();
    const auto it=std::find_if(out.sequence->cues.begin(),out.sequence->cues.end(),[](const auto &c){return c.kind==K::media;});
    for(unsigned pressed=0;pressed<65536U;++pressed)for(auto held:{std::uint64_t{0},chord-1U,chord})for(auto level:{-1,0}) {
      const bool neutral=(pressed&it->skip.pressed_any_mask)||(it->skip.held_all_mask&&(held&it->skip.held_all_mask)==it->skip.held_all_mask);
      check(neutral==rac_movie_skip_requested_v1({mode,0,0,level,pressed,held}),"Lowered skip gate differs from actual source gate");
    }
  }
}
void source_fixture(const char *path) {
  std::ifstream file(path,std::ios::binary|std::ios::ate);check(bool(file)&&file.tellg()==2652,"Invalid source flow fixture envelope");
  std::vector<std::byte> bytes(2652);file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());check(bool(file),"Cannot read source flow fixture");
  check(hex_digest(prepared_content_sha256_v1(bytes))=="59fcfde702e182fcb0c591c8923b8a04abf731f8cbb9e76f8ef941f51d0fb2ab","Source flow fixture SHA differs");
  check(std::string(reinterpret_cast<const char*>(bytes.data()),8)=="NGFLOW01","Invalid source flow signature");
  std::size_t at=8;auto word=[&](){check(bytes.size()-at>=4,"Truncated source flow fixture");std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::to_integer<std::uint32_t>(bytes[at++])<<(8*i);return v;};
  const auto count=word();check(count==10,"Source flow case count differs");unsigned calls=0;
  for(unsigned i=0;i<count;++i) {
    Fixture f;f.input.language_15ee88=word();f.input.time_scale_bits=word();f.input.updates_per_second=f.input.time_scale_bits==0x3f800000U?60:50;
    const auto out=f.run();const auto n=word();check(n==out.presentation_calls.size(),"Source presentation call count differs");
    for(const auto &c:out.presentation_calls) {
      check(word()==c.source_pc&&word()==c.source_callee&&word()==c.argument_count,"Source call boundary differs");
      for(auto value:c.arguments)check(word()==value,"Source presentation call argument differs");
      check(word()==c.current_level_word,"Source level assignment order differs");++calls;
    }
  }
  check(at==bytes.size(),"Trailing source flow bytes");std::cout<<count<<" source cases / "<<calls<<" original presentation calls matched\n";
}
}
int main(int argc,char **argv){try{sequence_order();source_input_and_ownership();skip_gates();if(argc>1)source_fixture(argv[1]);std::cout<<"3 New Game flow groups passed\n";return 0;}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
