#include "openrc/frontend_input.hpp"
#include "openrc/rac_new_game_flow.hpp"
#include "openrc/rac_frontend_state.hpp"
#include "openrc/frontend_menu.hpp"
#include "openrc/rac_frontend_title.hpp"
#include "openrc/hash.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace {
using namespace openrc;
using F=FrontendNoSaveFieldV1;
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
template<class Fn>void rejects(Fn &&fn){try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error("Invalid neutral frontend input was accepted");}
void neutral_title() {
  ScreenOverlayV1 title;title.canvas_width=512;title.canvas_height=448;title.updates_per_second=50;
  title.coverage_denominator=128;title.loop_begin=100;title.images.resize(192);
  RacFrontendTitleStateV1 authored;
  for(unsigned tick=0;tick<160;++tick) {
    authored=step_rac_frontend_title_v1(authored,0,0,0x3f555555).state;ScreenOverlayFrameV1 frame;
    if(authored.logo_alpha)frame.draws.push_back({authored.logo_alpha-1,236,16});
    if(authored.prompt_alpha)frame.draws.push_back({63+authored.prompt_alpha,160,368});
    title.frames.push_back(std::move(frame));
  }
  SessionStateInitialV1 initial;initial.schema.identity_key="test/title";
  constexpr std::array keys{"frontend/mode","frontend/title/counter","frontend/title/logo-alpha",
      "frontend/title/prompt-alpha","frontend/config/title-fade-counter"};
  for(const auto *key:keys) {
    initial.schema.buffers.push_back({key,4});initial.schema.views.push_back({std::string(key)+"/bytes",key,SessionStateValueTypeV1::u8,0,4,1});
    initial.buffers.push_back({key,std::vector<std::byte>(4)});
  }
  const auto limits=frontend_session_state_limits_v1();initial=canonicalize_session_state_initial_v1(initial,limits);
  game::SessionStateV1 state(initial,limits);
  const auto set=[&](const std::array<std::uint32_t,5> &values) {
    std::vector<game::SessionStateWriteV1> writes;
    for(unsigned i=0;i<5;++i)for(unsigned b=0;b<4;++b)writes.push_back({std::string(keys[i])+"/bytes",b,SessionStateValueTypeV1::u8,(values[i]>>(8*b))&255U});
    state.apply_batch(writes,state.revision());
  };
  const auto read=[&](const char *key) {
    std::uint32_t out=0;for(unsigned b=0;b<4;++b)out|=std::uint32_t(state.read_u8(std::string(key)+"/bytes",b))<<(8*b);return out;
  };
  unsigned cases=0;
  for(unsigned mode:{0U,3U})for(unsigned counter:{0U,24U,25U,26U,99U,100U,101U,158U,159U,160U,1000U,0xffffffffU})
    for(unsigned logo:{0U,1U,15U,16U,63U,64U})for(unsigned prompt:{0U,1U,15U,16U,127U,128U}) {
      set({mode,counter,logo,prompt,50});const auto expected=step_rac_frontend_title_v1({counter,logo,prompt},mode,0,0x3f555555).state;
      const auto result=evaluate_frontend_title_v1(title,state,state.revision());state.apply_batch(result.writes,result.expected_revision);
      check(!result.unsupported&&read(keys[1])==expected.counter&&read(keys[2])==expected.logo_alpha&&read(keys[3])==expected.prompt_alpha,
          "Neutral canonical title state differs from original qualified title owner");
      const auto frame=frontend_title_presentation_v1(state);
      check(frame.draws.size()==unsigned(expected.logo_alpha!=0)+unsigned(expected.prompt_alpha!=0),"Neutral title presentation lost current alpha slices");
      if(expected.logo_alpha)check(frame.draws.front()==ScreenOverlayDrawV1{expected.logo_alpha-1,236,16},"Title logo uses a different prepared alpha slice");
      if(expected.prompt_alpha)check(frame.draws.back()==ScreenOverlayDrawV1{63+expected.prompt_alpha,160,368},"Title prompt uses a different prepared alpha slice");
      ++cases;
    }
  set({4,50,17,33,50});const auto snapshot=state.snapshot();const auto held=evaluate_frontend_title_v1(title,state,state.revision());
  check(held.writes.empty()&&state.snapshot()==snapshot&&frontend_title_presentation_v1(state).draws.size()==2,"Dialog mode did not hold original title overlays");
  set({0,100,64,96,50});auto broken=title;broken.frames[100].draws.pop_back();
  rejects([&]{(void)evaluate_frontend_title_v1(broken,state,state.revision());});
  std::cout<<cases<<" neutral title updates matched source reference; mode4 holds and alpha slices passed\n";
}
struct Fixture {
  SessionStateLimitsV1 limits{1,16,64,256,4096,4096,4096,4096,1048576};
  SessionStateSchemaV1 schema{"fixture/live-input",{{"state",4096}},{{"bytes","state",SessionStateValueTypeV1::u8,0,4096,1}}};
  std::vector<RacFrontendStateBindingV1> bindings;
  RacFrontendResetTemplateV1 reset;
  FrontendNoSavePlanV1 plan;
  Fixture() {
    std::uint32_t offset=0;
    for(const auto range:std::array<std::pair<std::uint32_t,std::uint32_t>,8>{{
        {0x13cb00,0x120},{0x13d390,0x100},{0x13de60,1},{0x13e15a,2},
        {0x15ee80,0x880},{0x1d5008,0x130},{0x1d5f70,0x158},{0x200000,0x110}}}) {
      bindings.push_back({range.first,range.second,"bytes",offset});offset+=range.second;
    }
    for(unsigned i=0;i<263;++i)reset.copies.push_back({0x20bed0,0x200000+i,{static_cast<std::byte>(i)}});
    reset.copies.push_back({0x20bed0,0x13de60,{std::byte{0}}});
    for(auto address:{0x15eef0U,0x15eeecU,0x15eee8U})reset.copies.push_back({0x20bed0,address,std::vector<std::byte>(4,std::byte{0xa5})});
    const RacFrontendNoSaveCompileBindingsV1 input{0x1d50e8,0x1d5008,{"bytes",3700},{"bytes",3704},{11,12}};
    plan=compile_rac_frontend_no_save_plan_v1(reset,input,bindings,schema,limits);
    plan=decode_frontend_no_save_plan_v1(encode_frontend_no_save_plan_v1(plan));
  }
  game::SessionStateV1 state()const{return game::SessionStateV1(SessionStateInitialV1{schema,{{"state",std::vector<std::byte>(4096,std::byte{0x5a})}}},limits);}
  std::vector<game::SessionStateWriteV1> field(F f,std::uint32_t value)const {
    const auto &ref=plan.fields[static_cast<std::size_t>(f)];
    const unsigned width=f==F::focused?1:f==F::entry_requested?2:4;
    std::vector<game::SessionStateWriteV1> out;
    for(unsigned i=0;i<width;++i)out.push_back({ref.view_key,ref.first_element+i,SessionStateValueTypeV1::u8,(value>>(8*i))&255U});
    return out;
  }
  void set(game::SessionStateV1 &state,F f,std::uint32_t value)const{state.apply_batch(field(f,value),state.revision());}
  std::uint32_t read(const game::SessionStateV1 &s,F f)const {
    const auto &r=plan.fields[static_cast<std::size_t>(f)];std::uint32_t v=0;
    for(unsigned i=0;i<4;++i)v|=std::uint32_t(s.read_u8(r.view_key,r.first_element+i))<<(8*i);
    return v;
  }
  static RacFrontendNoSaveInputsV1 input() {
    RacFrontendNoSaveInputsV1 i;i.focused=true;i.node_address=0x1d50e8;i.parent_screen=0x1d4948;
    i.ui_mode_15efb0=1;i.card_type=2;i.card_e4=UINT32_MAX;i.root_154=11;i.pressed=0x20;
    i.preserved_eef0_eeec_eee8={0x12345678,0x9abcdef0,0x7f800000};return i;
  }
  void seed(game::SessionStateV1 &state,const RacFrontendNoSaveInputsV1 &i)const {
    const std::array values{
      std::pair{F::focused,std::uint32_t(i.focused)},std::pair{F::node_flags,i.node_flags},
      std::pair{F::node_phase,i.node_phase},std::pair{F::node_selection,i.node_selection},
      std::pair{F::sound_object,i.sound_object},std::pair{F::previous_screen,i.previous_screen==0x1d51b8?11U:i.previous_screen==0x1d5318?12U:0U},
      std::pair{F::previous_result,i.previous_result},std::pair{F::parent_screen,i.parent_screen?7U:0U},
      std::pair{F::cancel_guard,i.root_124},std::pair{F::pending_save,i.root_128},std::pair{F::readiness,i.root_154},
      std::pair{F::card_mode,i.ui_mode_15efb0},std::pair{F::card_status,i.card_dc},std::pair{F::card_result,i.card_e4},
      std::pair{F::card_type,i.card_type},std::pair{F::global_pressed,i.global_pressed},std::pair{F::pressed,i.pressed},
      std::pair{F::repeated,i.repeated},std::pair{F::saved_selection,i.saved_selection_15ef34},std::pair{F::flags,i.flags_15efb4},
      std::pair{F::preserved_a,i.preserved_eef0_eeec_eee8[0]},std::pair{F::preserved_b,i.preserved_eef0_eeec_eee8[1]},
      std::pair{F::preserved_c,i.preserved_eef0_eeec_eee8[2]}};
    std::vector<game::SessionStateWriteV1> writes;
    for(const auto &[f,value]:values){const auto w=field(f,value);writes.insert(writes.end(),w.begin(),w.end());}
    state.apply_batch(writes,state.revision());
  }
  bool compare(const RacFrontendNoSaveInputsV1 &input)const {
    auto actual=state();seed(actual,input);auto expected=actual;
    RacFrontendInputResultV1 source;bool unsupported=false;
    try{source=execute_rac_frontend_no_save_v1(input,&reset);}catch(const RacFrontendInputError&){unsupported=true;}
    const auto result=execute_frontend_no_save_v1(plan,actual,actual.revision());
    check((result.unsupported!=FrontendNoSaveUnsupportedV1::none)==unsupported,"Neutral unsupported gate differs from recovered source");
    if(!unsupported) {
      // Pointer-valued source screen writes become neutral screen handles.
      for(auto &w:source.writes)if(w.source_address==0x1d5f78) {
        const auto token=input.parent_screen?7U:0U;
        for(unsigned i=0;i<4;++i)w.bytes[i]=static_cast<std::byte>(token>>(8*i));
      }
      const auto writes=lower_rac_frontend_input_writes_v1(source.writes,bindings,schema,limits);
      expected.apply_batch(writes,expected.revision());
      check(result.return_word==source.return_word&&result.requested_new_game==source.requested_new_game,
          "Neutral callback result differs from recovered source");
      check(result.selection_sound_requested==!source.sounds.empty(),"Neutral sound request differs from recovered source");
      if(!source.sounds.empty())check(result.sound_object_token==source.sounds[0].arguments[2],"Neutral sound handle differs");
    }
    check(actual.snapshot()==expected.snapshot(),"Live neutral state bytes or atomic revision differ from source effects");
    return !unsupported&&result.requested_new_game;
  }
};
void differential() {
  Fixture f;unsigned cases=0,requests=0;
  for(auto flags:{0U,1U,0x2000U})for(auto mode:{0U,1U,16U})for(auto guard:{0U,1U})
  for(auto pressed:{0U,0x20U,0x40U,0x60U,0x1000U,0x4000U,0x5000U,0x4020U})
  for(auto global:{0U,0x10U,0x810U})for(auto selection:{0U,2U,4U,UINT32_MAX}) {
    auto input=Fixture::input();input.node_flags=flags;input.ui_mode_15efb0=mode;input.root_124=guard;
    input.pressed=pressed;input.repeated=pressed^0x20U;input.global_pressed=global;
    input.saved_selection_15ef34=selection;input.node_selection=2;input.sound_object=23;
    requests+=f.compare(input);++cases;
  }
  for(unsigned arm=0;arm<13;++arm) {
    auto i=Fixture::input();
    switch(arm) {
      case 0:i.focused=false;break;case 1:i.node_phase=1;break;
      case 2:i.previous_screen=0x1d51b8;i.previous_result=1;break;
      case 3:i.previous_screen=0x1d5318;i.previous_result=1;break;
      case 4:i.root_128=1;break;case 5:i.card_dc=3;break;case 6:i.card_e4=0;break;
      case 7:i.root_154=10;break;case 8:i.root_154=UINT32_MAX;break;case 9:i.card_type=1;break;
      case 10:i.global_pressed=0x10;i.parent_screen=0;break;
      case 11:i.card_dc=UINT32_MAX;break;case 12:i.node_phase=2;i.previous_screen=0x1d5318;i.previous_result=1;break;
    }
    requests+=f.compare(i);++cases;
  }
  check(requests>0,"Differential corpus never reached fresh reset");
  std::cout<<cases<<" live input/source cases / "<<requests<<" fresh resets matched\n";
}
void live_state_and_boundaries() {
  Fixture f;auto s=f.state();f.seed(s,Fixture::input());
  f.set(s,F::pressed,0);auto result=execute_frontend_no_save_v1(f.plan,s,s.revision());
  check(!result.requested_new_game,"Plan replayed a precompiled future button press");
  f.set(s,F::pressed,0x20);f.set(s,F::preserved_a,0xdeadbeef);f.set(s,F::preserved_b,0x01234567);
  result=execute_frontend_no_save_v1(f.plan,s,s.revision());
  check(result.requested_new_game&&f.read(s,F::preserved_a)==0xdeadbeef&&f.read(s,F::preserved_b)==0x01234567,
      "Reset replayed prepared settings over actual live preserved values");
  const auto snapshot=s.snapshot();
  rejects([&]{(void)execute_frontend_no_save_v1(f.plan,s,s.revision()-1);});check(s.snapshot()==snapshot,"Stale call mutated state");
  auto broken=f.plan;broken.fields[0].first_element=4096;
  rejects([&]{(void)execute_frontend_no_save_v1(broken,s,s.revision());});check(s.snapshot()==snapshot,"Invalid view mutated state");
  broken=f.plan;broken.state_schema_sha256[0]^=std::byte{1};
  rejects([&]{(void)execute_frontend_no_save_v1(broken,s,s.revision());});
  auto bytes=encode_frontend_no_save_plan_v1(f.plan);
  check(encode_frontend_no_save_plan_v1(decode_frontend_no_save_plan_v1(bytes))==bytes,"Neutral input codec is not canonical");
  bytes.back()^=std::byte{1};rejects([&]{(void)decode_frontend_no_save_plan_v1(bytes);});
  bytes=encode_frontend_no_save_plan_v1(f.plan);bytes.resize(bytes.size()-1);rejects([&]{(void)decode_frontend_no_save_plan_v1(bytes);});
  broken=f.plan;broken.reset_writes.back().element_index=4096;
  rejects([&]{(void)execute_frontend_no_save_v1(broken,s,s.revision());});check(s.snapshot()==snapshot,"Invalid reset mutated live state");
  auto alias=f.bindings;alias[1].first_element=0;
  rejects([&]{(void)lower_rac_frontend_input_writes_v1({},alias,f.schema,f.limits);});
}
void transition_owner_fixture(const char *path,const RacFrontendStateCompilationV1 &compiled) {
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  check(bool(file)&&file.tellg()==97718,"Invalid transition owner fixture extent");
  std::vector<std::byte> data(97718);file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),data.size());
  check(hex_digest(prepared_content_sha256_v1(data))==
      "e61124437a95a6a0d7159236f6e0be94e7aded1c684be731205d6a6cc2f7f06a","Transition owner fixture hash differs");
  check(std::string(reinterpret_cast<const char*>(data.data()),8)=="FRONTOW1","Transition owner fixture magic differs");
  std::size_t at=8;
  const auto word=[&]() {check(at<=data.size()&&data.size()-at>=4,"Truncated transition owner fixture");std::uint32_t value=0;
    for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(data[at++])<<(8*i);return value;};
  const auto lower=[&](const std::vector<RacFrontendInputWriteV1> &writes) {
    return lower_rac_frontend_input_writes_v1(writes,compiled.source_bindings,compiled.initial.schema,compiled.state_limits);
  };
  const auto cases=word();check(cases==346,"Transition owner fixture case count differs");
  for(unsigned c=0;c<cases;++c) {
    const auto operation=word(),selector=word(),reached=word(),restore=word(),fields=word();
    std::vector<RacFrontendInputWriteV1> initial;
    for(unsigned f=0;f<fields;++f) {
      const auto address=word(),width=word(),value=word();check(width>0&&width<=4,"Invalid transition owner width");
      RacFrontendInputWriteV1 write{0,address,{}};
      for(unsigned i=0;i<width;++i)write.bytes.push_back(static_cast<std::byte>(value>>(8*i)));
      initial.push_back(std::move(write));
    }
    auto state=game::SessionStateV1(compiled.initial,compiled.state_limits);auto setup=lower(initial);
    for(unsigned i=0;i<4;++i)setup.push_back({"frontend/config/startup-video-selector/bytes",i,SessionStateValueTypeV1::u8,(selector>>(8*i))&255U});
    state.apply_batch(setup,state.revision());auto expected=state;
    std::vector<RacFrontendInputWriteV1> effects;const auto count=word();
    for(unsigned e=0;e<count;++e) {
      const auto pc=word(),address=word(),width=word();check(width<=data.size()-at,"Truncated transition owner write");
      effects.push_back({pc,address,std::vector<std::byte>(data.begin()+at,data.begin()+at+width)});at+=width;
    }
    FrontendMenuEvaluationV1 result;
    if(operation==0)result=evaluate_frontend_platform_bootstrap_v1(state,state.revision());
    else if(operation==1)result=evaluate_frontend_exit_v1(state,state.revision());
    else {check(operation==2,"Unknown transition owner operation");result=evaluate_frontend_transition_prefix_v1(state,state.revision());}
    check(!result.unsupported&&result.frontend_exit_reached==bool(reached),"Original frontend exit gate differs");
    check(result.restore_display_selector==(restore==UINT32_MAX?std::nullopt:std::optional<std::uint32_t>(restore)),
        "Frontend display restoration request differs from original calls");
    const auto source=lower(effects);
    check(result.writes.size()==source.size()&&std::equal(result.writes.begin(),result.writes.end(),source.begin(),[](const auto &a,const auto &b){
        return a.view_key==b.view_key&&a.element_index==b.element_index&&a.value_type==b.value_type&&a.value_bits==b.value_bits;
      }),"Transition owner ordered writes differ from original instructions");
    state.apply_batch(result.writes,result.expected_revision);expected.apply_batch(source,expected.revision());
    check(state.snapshot()==expected.snapshot(),"Transition owner changed unowned state or lost aliases");
    rejects([&]{(void)evaluate_frontend_transition_prefix_v1(state,state.revision()+1);});
  }
  check(at==data.size(),"Trailing transition owner fixture bytes");
  std::cout<<cases<<" original startup/exit/prepare owner cases matched, including preserved live audio fields\n";
}
void card_sequence_fixture(const char *path,const RacFrontendStateCompilationV1 &compiled) {
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  check(bool(file)&&file.tellg()==64064,"Invalid sequential card source fixture extent");
  std::vector<std::byte> data(64064);file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),data.size());
  check(hex_digest(prepared_content_sha256_v1(data))==
      "e9422b2b4cb4a013c552ee543dfc130dfdb8816ed96b4ee3e276eb16f4669031","Sequential card fixture hash differs");
  check(std::string(reinterpret_cast<const char*>(data.data()),8)=="CARDSEQ1","Sequential card fixture magic differs");
  std::size_t at=8;unsigned operations=0;
  const auto word=[&]() {check(data.size()-at>=4,"Truncated sequential card fixture");std::uint32_t value=0;
    for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(data[at++])<<(8*i);return value;};
  const auto lower=[&](std::vector<RacFrontendInputWriteV1> writes) {
    for(auto &write:writes)if(write.source_address==0x193418||write.source_address==0x1d5f74) {
      check(write.bytes.size()==4,"Truncated sequential screen token");std::uint32_t value=0;
      for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(write.bytes[i])<<(8*i);
      if(value==0x1d4948)value=1;else if(value==0x1d5008)value=2;else check(value==0,"Unbound sequential screen reference");
      for(unsigned i=0;i<4;++i)write.bytes[i]=static_cast<std::byte>(value>>(8*i));
    }
    return lower_rac_frontend_input_writes_v1(writes,compiled.source_bindings,compiled.initial.schema,compiled.state_limits);
  };
  const auto flows=word();check(flows==5,"Sequential source flow count differs");
  for(unsigned flow=0;flow<flows;++flow) {
    const auto completion=std::bit_cast<std::int32_t>(word());(void)word();const auto first_visible=word(),owners=word();
    std::vector<RacFrontendInputWriteV1> initial;
    for(unsigned i=0;i<owners;++i) {
      const auto address=word(),value=word();RacFrontendInputWriteV1 write{0,address,{}};
      for(unsigned lane=0;lane<4;++lane)write.bytes.push_back(static_cast<std::byte>(value>>(8*lane)));
      initial.push_back(std::move(write));
    }
    auto actual=game::SessionStateV1(compiled.initial,compiled.state_limits);
    auto setup=lower(std::move(initial));setup.push_back({"frontend/main/focused/bytes",0,SessionStateValueTypeV1::u8,1});
    actual.apply_batch(setup,actual.revision());auto expected=actual;
    const auto records=word();std::uint32_t visible=UINT32_MAX,dialog_tick=0;
    for(unsigned record=0;record<records;++record) {
      const auto operation=word(),signal=word(),pressed=word(),effects=word();
      if(record) {
        std::vector<game::SessionStateWriteV1> input;
        for(unsigned lane=0;lane<4;++lane) {
          input.push_back({"input/global-pressed/bytes",lane,SessionStateValueTypeV1::u8,0});
          input.push_back({"input/pressed/bytes",lane,SessionStateValueTypeV1::u8,(pressed>>(8*lane))&255U});
        }
        actual.apply_batch(input,actual.revision());expected.apply_batch(input,expected.revision());
      }
      std::vector<RacFrontendInputWriteV1> source;
      for(unsigned i=0;i<effects;++i) {
        const auto pc=word(),address=word(),bytes=word();check(bytes<=data.size()-at,"Truncated sequential source write");
        source.push_back({pc,address,std::vector<std::byte>(data.begin()+at,data.begin()+at+bytes)});at+=bytes;
      }
      FrontendMenuEvaluationV1 result;
      if(operation==0) {
        FrontendCardSignalV1 host;
        if(signal==1)host.poll=FrontendCardPollStatusV1::no_request;
        if(signal==3){host.poll=FrontendCardPollStatusV1::completed_absent;host.completed_result=completion;host.completed_command_token=1;}
        if(signal==4)host.request_accepted=true;
        if(signal==3) {
          auto invalid=host;invalid.completed_result.reset();
          rejects([&]{(void)evaluate_frontend_absent_card_v1(actual,actual.revision(),invalid);});
          invalid.completed_result=0;rejects([&]{(void)evaluate_frontend_absent_card_v1(actual,actual.revision(),invalid);});
        }
        result=evaluate_frontend_absent_card_v1(actual,actual.revision(),host);
      } else {
        check(operation==1||operation==2,"Unknown sequential source operation");
        result=evaluate_frontend_menu_v1(operation==2?FrontendMenuOperationV1::action4_input:
            FrontendMenuOperationV1::dialog3_update,compiled.no_save,actual,actual.revision());
      }
      check(!result.unsupported,"Original sequential card branch rejected");
      actual.apply_batch(result.writes,result.expected_revision);expected.apply_batch(lower(std::move(source)),expected.revision());
      check(actual.snapshot()==expected.snapshot(),"Live sequential card/menu state differs from original source writes");
      if(operation==1) {
        if(evaluate_frontend_dialog_presentation_v1(actual).panel&&visible==UINT32_MAX)visible=dialog_tick;
        ++dialog_tick;
      }
      ++operations;
    }
    check(visible==first_visible,"Sequential dialog visibility differs from the original busy/delay gates");
  }
  check(at==data.size()&&operations==725,"Sequential source fixture coverage differs");
  std::cout<<"5 sequential card/menu flows /725 operations matched (71999 original instructions), explicit completion and busy release passed\n";
}
void card_fixture(const char *path,const RacFrontendStateCompilationV1 &compiled) {
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  check(bool(file)&&file.tellg()==24600,"Invalid absent-card source fixture extent");
  std::vector<std::byte> data(24600);file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),data.size());
  check(hex_digest(prepared_content_sha256_v1(data))=="450922278ac5c10c4c1632b68051358f3a8c1bfb92bb7f91375bc5d8837a4eb2","Absent-card source fixture hash differs");
  check(std::string(reinterpret_cast<const char*>(data.data()),8)=="CARDABS1","Invalid absent-card source signature");
  std::size_t at=8;
  const auto word=[&](){check(data.size()-at>=4,"Truncated card source word");std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::to_integer<std::uint32_t>(data[at++])<<(8*i);return v;};
  const auto source_write=[](std::uint32_t address,std::uint32_t value) {
    RacFrontendInputWriteV1 w{0,address,{}};for(unsigned i=0;i<4;++i)w.bytes.push_back(static_cast<std::byte>(value>>(8*i)));return w;
  };
  const auto lower=[&](std::span<const RacFrontendInputWriteV1> writes) {
    return lower_rac_frontend_input_writes_v1(writes,compiled.source_bindings,compiled.initial.schema,compiled.state_limits);
  };
  const auto count=word();unsigned requests=0;
  for(unsigned c=0;c<count;++c) {
    const auto operation=word(),signal=word(),reset=word(),n=word();
    auto actual=game::SessionStateV1(compiled.initial,compiled.state_limits);
    std::vector<RacFrontendInputWriteV1> source_setup;
    for(unsigned i=0;i<n;++i) {
      const auto address=word();auto value=word();
      if(address==0x193418&&value==0x1d5008)value=2;
      source_setup.push_back(source_write(address,value));
    }
    auto setup=lower(source_setup);
    for(const auto &[key,value]:std::array<std::pair<const char*,std::uint32_t>,2>{{{"frontend/config/dialog-duration/bytes",30},{"frontend/config/dialog-delay/bytes",10}}})
      for(unsigned lane=0;lane<4;++lane)setup.push_back({key,lane,SessionStateValueTypeV1::u8,(value>>(8*lane))&255U});
    actual.apply_batch(setup,actual.revision());auto expected=actual;
    std::vector<game::SessionStateWriteV1> expected_writes;
    const auto writes=word();
    for(unsigned i=0;i<writes;++i) {
      const auto pc=word(),address=word(),bytes=word();
      if(pc==0x209dc0&&address==0&&bytes==0) {
        // This source fixture stops at the explicit reset boundary. Complete
        // reset copy bytes and save/restore ordering have independent original
        // instruction proof in rac-frontend-input-source.bin.
        const auto &p=compiled.no_save;
        expected_writes.insert(expected_writes.end(),p.reset_writes.begin(),p.reset_writes.end());
        for(auto field:{F::preserved_a,F::preserved_b,F::preserved_c,F::current_level}) {
          const auto &ref=p.fields[static_cast<std::size_t>(field)];
          for(unsigned lane=0;lane<4;++lane)expected_writes.push_back({ref.view_key,ref.first_element+lane,
              SessionStateValueTypeV1::u8,field==F::current_level?0U:actual.read_u8(ref.view_key,ref.first_element+lane)});
        }
      } else {
        check(bytes<=data.size()-at,"Truncated absent-card source effect");
        const std::array source{RacFrontendInputWriteV1{pc,address,std::vector<std::byte>(data.begin()+at,data.begin()+at+bytes)}};at+=bytes;
        const auto lowered=lower(source);expected_writes.insert(expected_writes.end(),lowered.begin(),lowered.end());
      }
    }
    FrontendMenuEvaluationV1 result;
    if(operation==0) {
      FrontendCardSignalV1 host;
      if(signal==1)host.poll=FrontendCardPollStatusV1::no_request;
      if(signal==2)host.poll=FrontendCardPollStatusV1::pending;
      if(signal==3){host.poll=FrontendCardPollStatusV1::completed_absent;host.completed_result=-1;}
      if(signal==4)host.request_accepted=true;
      if(signal==5)host.request_accepted=false;
      result=evaluate_frontend_absent_card_v1(actual,actual.revision(),host);
    } else result=evaluate_frontend_menu_v1(FrontendMenuOperationV1::dialog3_update,compiled.no_save,actual,actual.revision());
    check(!result.unsupported,"Reached original absent-card branch was rejected");
    check(result.requested_new_game==bool(reset),"Original absent-card confirmation precedence differs");requests+=result.requested_new_game;
    actual.apply_batch(result.writes,result.expected_revision);expected.apply_batch(expected_writes,expected.revision());
    if(actual.snapshot()!=expected.snapshot()) {
      for(const auto &buffer:actual.schema().buffers) {
        const auto left=actual.buffer_bytes(buffer.key),right=expected.buffer_bytes(buffer.key);
        if(!std::equal(left.begin(),left.end(),right.begin()))std::cerr<<"Source card case "<<c<<" differs in "<<buffer.key<<'\n';
      }
      check(false,"Neutral card/dialog state differs from original executed effects");
    }
  }
  check(at==data.size(),"Trailing source card bytes");
  std::cout<<count<<" original absent-card/menu cases / "<<requests<<" fresh requests matched\n";
}
void dialog_presentation_fixture(const char *path,const RacFrontendStateCompilationV1 &compiled) {
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  check(bool(file)&&file.tellg()==16140,"Invalid dialog presentation source fixture extent");
  std::vector<std::byte> data(16140);file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),data.size());
  check(hex_digest(prepared_content_sha256_v1(data))=="6f74685a16d9ad6bb8587a5a644140ba425c2ac971b8e5071fba4e2ebf1e9f42",
      "Dialog presentation source fixture hash differs");
  check(std::string(reinterpret_cast<const char*>(data.data()),8)=="DIALOGP1","Invalid dialog presentation signature");
  std::size_t at=8;
  const auto word=[&](){check(data.size()-at>=4,"Truncated dialog presentation word");std::uint32_t v=0;
    for(unsigned i=0;i<4;++i)v|=std::to_integer<std::uint32_t>(data[at++])<<(8*i);return v;};
  constexpr std::array keys{"frontend/dialog/draw-delay","frontend/dialog/kind","frontend/card-busy","frontend/dialog/age",
      "frontend/card-mode","frontend/new-game-context","frontend/existing-game-context","frontend/root-dialog-result"};
  const auto message_key=[](FrontendDialogMessageV1 value) {
    switch(value) {
      case FrontendDialogMessageV1::empty:return 0U;
      case FrontendDialogMessageV1::absent_fresh:case FrontendDialogMessageV1::absent_existing:return 20395U;
      case FrontendDialogMessageV1::absent_without_game:return 20396U;
      case FrontendDialogMessageV1::unavailable:return 20391U;
    }
    throw std::runtime_error("Unknown neutral dialog message");
  };
  const auto prompt_key=[](FrontendDialogPromptV1 value) {
    switch(value) {
      case FrontendDialogPromptV1::none:return 0U;
      case FrontendDialogPromptV1::continue_without_save:return 21075U;
      case FrontendDialogPromptV1::continue_existing_game:return 21076U;
      case FrontendDialogPromptV1::cancel:return 21072U;
      case FrontendDialogPromptV1::back:return 20392U;
    }
    throw std::runtime_error("Unknown neutral dialog prompt");
  };
  const auto count=word();unsigned panels=0;
  for(unsigned c=0;c<count;++c) {
    auto state=game::SessionStateV1(compiled.initial,compiled.state_limits);
    std::vector<game::SessionStateWriteV1> setup;
    const auto set=[&](const std::string &key,std::uint32_t value) {
      for(unsigned i=0;i<4;++i)setup.push_back({key+"/bytes",i,SessionStateValueTypeV1::u8,(value>>(8*i))&255U});
    };
    for(const auto *key:keys)set(key,word());
    set("frontend/mode",4);set("frontend/dialog/duration",7);set("frontend/dialog/fade",11);
    const auto coverage=word(),panel=word(),message=word(),left=word(),right=word(),center=word();
    state.apply_batch(setup,state.revision());const auto before=state.snapshot();const auto revision=state.revision();
    const auto result=evaluate_frontend_dialog_presentation_v1(state);
    check(!result.unsupported&&result.backdrop&&result.backdrop_coverage==coverage&&result.coverage_denominator==128&&
        result.panel==bool(panel),"Original dialog draw gate differs");
    check(message_key(result.message)==message&&prompt_key(result.left)==left&&prompt_key(result.right)==right&&
        prompt_key(result.center)==center,"Original dialog message or prompt selection differs");
    if(result.panel) {
      ++panels;check(result.body_remaining==7&&result.prompt_remaining==11&&result.prepared_duration==25,
          "Dialog presentation combined the separate original opacity counters");
    }
    check(state.revision()==revision&&state.snapshot()==before,"Read-only dialog selection changed session state");
  }
  check(at==data.size(),"Trailing dialog presentation bytes");
  std::cout<<count<<" original dialog presentation cases / "<<panels<<" panels matched\n";
}
void main_entry_fixture(const char *path,const RacFrontendStateCompilationV1 &compiled) {
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  check(bool(file)&&file.tellg()==197332,"Invalid main entry source fixture extent");
  std::vector<std::byte> data(197332);file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),data.size());
  check(hex_digest(prepared_content_sha256_v1(data))=="eb49f53e674e98c94f9356d678f4b08b0fa0484cd8500820327b74c5f59fe0f6",
      "Main entry source fixture hash differs");
  check(std::string(reinterpret_cast<const char*>(data.data()),8)=="FROOBJT1","Invalid main entry source signature");
  std::size_t at=8;
  const auto word=[&](){check(data.size()-at>=4,"Truncated main entry word");std::uint32_t v=0;
    for(unsigned i=0;i<4;++i)v|=std::to_integer<std::uint32_t>(data[at++])<<(8*i);return v;};
  const auto skip=[&](std::size_t count){check(count<=data.size()-at,"Truncated main entry body");at+=count;};
  skip(word());const auto screens=word();check(screens==2,"Main fixture screen count differs");skip(screens*(3+4*14)*4);
  constexpr std::array keys{"frontend/root-phase","frontend/current-screen","frontend/requested-screen",
      "frontend/previous-screen","frontend/transition-remaining"};
  const auto token=[](std::uint32_t value){if(!value)return 0U;if(value==0x1d4948)return 1U;if(value==0x1d5008)return 2U;
    throw std::runtime_error("Unknown source screen in main fixture");};
  const auto read_state=[](const game::SessionStateV1 &state,const char *key) {
    std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(state.read_u8(std::string(key)+"/bytes",i))<<(8*i);return value;
  };
  const auto count=word();unsigned arrivals=0;
  for(unsigned c=0;c<count;++c) {
    std::array<std::uint32_t,5> before,after;
    for(auto &v:before)v=word();skip(14*13*4);for(auto &v:after)v=word();skip(14*13*4);
    const auto sounds=word();skip(sounds*3*4);const auto bindings=word();skip(bindings*2*4);
    for(unsigned i=1;i<4;++i){before[i]=token(before[i]);after[i]=token(after[i]);}
    auto state=game::SessionStateV1(compiled.initial,compiled.state_limits);std::vector<game::SessionStateWriteV1> setup;
    const auto set=[&](const std::string &key,std::uint32_t value) {
      for(unsigned i=0;i<4;++i)setup.push_back({key+"/bytes",i,SessionStateValueTypeV1::u8,(value>>(8*i))&255U});
    };
    for(unsigned i=0;i<keys.size();++i)set(keys[i],c==0?(i==0?45U:0U):before[i]);set("frontend/mode",3);
    state.apply_batch(setup,state.revision());std::optional<FrontendMainResourcesReadyV1> ready;
    if(c==0) {
      const auto snapshot=state.snapshot();
      for(unsigned retry=0;retry<3;++retry) {
        const auto waiting=evaluate_frontend_main_entry_v1(state,state.revision());
        check(waiting.awaiting_resources&&!waiting.began_main_entry&&waiting.writes.empty()&&state.snapshot()==snapshot,
            "Main entry synthesized resource completion from polling");
      }
      ready.emplace();for(unsigned i=0;i<14;++i)ready->actor_tokens[i]=100+i;
    }
    const auto result=evaluate_frontend_main_entry_v1(state,state.revision(),ready);
    if(c==14) {check(result.unsupported&&result.writes.empty(),"Unclosed noninitial screen transition was accepted");continue;}
    check(!result.unsupported&&!result.awaiting_resources,"Original main transition gate was rejected");
    state.apply_batch(result.writes,result.expected_revision);
    for(unsigned i=0;i<keys.size();++i)check(read_state(state,keys[i])==after[i],"Neutral main entry differs from original source writes");
    check(result.sound_requested==bool(sounds),"Original main transition sound boundary differs");
    if(c==0)check(result.began_main_entry&&result.sound_variant==3&&result.sound_object_token==100&&
        read_state(state,"frontend/main/sound-object")==102,"Main entry lost actual admitted object bindings");
    arrivals+=result.main_entry_complete;
    if(result.main_entry_complete)check(state.read_u8("frontend/main/focused/bytes",0)==1&&
        state.read_u8("frontend/no-save/focused/bytes",0)==0,"Main focus did not derive from the committed screen");
  }
  check(at==data.size()&&arrivals==4,"Main source fixture coverage differs");
  std::cout<<count-1<<" original main entry cases / "<<arrivals<<" completed gates matched; unsupported screen remained explicit\n";
}
void actual_sources(const char *image,const char *elf_path,const char *fixture,const char *draw_fixture,const char *entry_fixture,
    const char *sequence_fixture,const char *transition_fixture) {
  std::ifstream file(elf_path,std::ios::binary|std::ios::ate);
  check(bool(file)&&file.tellg()>0&&file.tellg()<=32*1024*1024,"Invalid source state ELF extent");
  std::vector<std::byte> elf(static_cast<std::size_t>(file.tellg()));file.seekg(0);
  file.read(reinterpret_cast<char*>(elf.data()),static_cast<std::streamsize>(elf.size()));check(bool(file),"Cannot read source state ELF");
  RacNewGameFlowResourcesV1 resources;
  for(unsigned i=0;i<3;++i) {
    const std::array<std::byte,1> payload{static_cast<std::byte>(i)};
    resources.loading_cards[i]={"loading/"+std::to_string(i),"openrc.loading-presentation",prepared_content_sha256_v1(payload)};
    resources.movies[i]={"movie/"+std::to_string(i),"openrc.media-clip",prepared_content_sha256_v1(payload)};
  }
  const auto compiled=compile_rac_frontend_state_v1(image,elf,{},resources);
  check(compiled.resources.size()==3&&compiled.reset_template.copies.size()==267,"Complete frontend state package shape differs");
  const auto initial=decode_session_state_initial_v1(compiled.resources[0].payload,{4U*1024U*1024U,compiled.state_limits});
  check(initial==compiled.initial,"Complete frontend state resource roundtrip differs");
  auto actual=game::SessionStateV1(initial,compiled.state_limits);
  const auto &plan=compiled.no_save;
  // Controlled live callback input for comparison; this does not claim the
  // actual card lifecycle reached readiness or perform runtime menu entry.
  const auto i=Fixture::input();
  const std::array values{
      std::pair{F::focused,1U},std::pair{F::node_flags,0U},std::pair{F::node_phase,0U},
      std::pair{F::node_selection,0U},std::pair{F::sound_object,0U},std::pair{F::previous_screen,0U},
      std::pair{F::previous_result,0U},std::pair{F::parent_screen,1U},std::pair{F::cancel_guard,0U},
      std::pair{F::pending_save,0U},std::pair{F::readiness,11U},std::pair{F::card_mode,1U},
      std::pair{F::card_status,0U},std::pair{F::card_result,UINT32_MAX},std::pair{F::card_type,2U},
      std::pair{F::global_pressed,0U},std::pair{F::pressed,0x20U},std::pair{F::repeated,0U},
      std::pair{F::saved_selection,0U},std::pair{F::flags,0U},
      std::pair{F::preserved_a,i.preserved_eef0_eeec_eee8[0]},
      std::pair{F::preserved_b,i.preserved_eef0_eeec_eee8[1]},
      std::pair{F::preserved_c,i.preserved_eef0_eeec_eee8[2]}};
  std::vector<game::SessionStateWriteV1> setup;
  for(const auto &[f,value]:values) {
    const auto &ref=plan.fields[static_cast<std::size_t>(f)];
    for(unsigned lane=0;lane<(f==F::focused?1U:4U);++lane)
      setup.push_back({ref.view_key,ref.first_element+lane,SessionStateValueTypeV1::u8,(value>>(8*lane))&255U});
  }
  actual.apply_batch(setup,actual.revision());auto expected=actual;
  const auto source=execute_rac_frontend_no_save_v1(i,&compiled.reset_template);
  const auto writes=lower_rac_frontend_input_writes_v1(source.writes,compiled.source_bindings,initial.schema,compiled.state_limits);
  expected.apply_batch(writes,expected.revision());
  const auto evaluated=evaluate_frontend_no_save_v1(plan,actual,actual.revision());
  check(evaluated.result.requested_new_game,"Actual-source neutral plan omitted fresh request");
  actual.apply_batch(evaluated.writes,evaluated.expected_revision);
  check(actual.snapshot()==expected.snapshot(),"Complete named live reset differs from actual descriptor effects");
  check(actual.read_u32("rac1.progress/encoded-source-level",0)==0,"Current-level typed alias lost canonical reset write");
  for(unsigned row=0;row<20;++row) {
    const auto prefix="rac1.progress/level/"+std::to_string(row)+"/";
    const auto keys=actual.read_u16(prefix+"registration-keys",0);
    const auto value=actual.read_u32(prefix+"registration-words",0);
    check(keys==(value&65535U),"Progress admission aliases lost shared reset storage");
  }
  std::uint64_t total=0;for(const auto &b:initial.buffers)total+=b.bytes.size();
  std::cout<<"Actual-source named state: "<<initial.schema.buffers.size()<<" buffers / "<<initial.schema.views.size()
      <<" views / "<<total<<" bytes / "<<compiled.reset_template.copies.size()<<" reset copies; live reset and resources matched\n";
  if(fixture)card_fixture(fixture,compiled);
  if(draw_fixture)dialog_presentation_fixture(draw_fixture,compiled);
  if(entry_fixture)main_entry_fixture(entry_fixture,compiled);
  if(sequence_fixture)card_sequence_fixture(sequence_fixture,compiled);
  if(transition_fixture)transition_owner_fixture(transition_fixture,compiled);
}
} // namespace
int main(int argc,char **argv){try{differential();live_state_and_boundaries();neutral_title();if(argc>=3)actual_sources(argv[1],argv[2],argc>3?argv[3]:nullptr,argc>4?argv[4]:nullptr,argc>5?argv[5]:nullptr,argc>6?argv[6]:nullptr,argc>7?argv[7]:nullptr);std::cout<<"3 neutral live input groups passed\n";return 0;}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
