#include "openrc/rac_new_game_flow.hpp"
#include "openrc/rac_frontend_new_game.hpp"

#include <algorithm>
#include <bit>

namespace openrc {
namespace {
using Kind=FrontendSequenceCueKindV1;
[[noreturn]] void fail(const char *s){throw RacFrontendInputError(s);}
RacFrontendInputWriteV1 word(std::uint32_t pc,std::uint32_t address,std::uint32_t value,unsigned width=4) {
  RacFrontendInputWriteV1 w{pc,address,{}};
  for(unsigned i=0;i<width;++i)w.bytes.push_back(static_cast<std::byte>(value>>(8*i)));
  return w;
}
std::uint32_t last_value(std::span<const RacFrontendInputWriteV1> writes,std::uint32_t address,unsigned width) {
  std::array<std::optional<std::byte>,4> found{};
  for(const auto &w:writes)for(unsigned i=0;i<width;++i)
    if(address+i>=w.source_address&&std::uint64_t(address)+i<std::uint64_t(w.source_address)+w.bytes.size())
      found[i]=w.bytes[address+i-w.source_address];
  std::uint32_t value=0;
  for(unsigned i=0;i<width;++i){if(!found[i])fail("Fresh New Game branch lacks a source-written gate");value|=std::uint32_t(std::to_integer<unsigned>(*found[i]))<<(8*i);}
  return value;
}
}
std::vector<game::SessionStateWriteV1> lower_rac_frontend_input_writes_v1(
    std::span<const RacFrontendInputWriteV1> writes,std::span<const RacFrontendStateBindingV1> bindings,
    const SessionStateSchemaV1 &schema,const SessionStateLimitsV1 &state_limits,std::uint64_t max_writes) {
  const auto canonical=canonicalize_session_state_schema_v1(schema,state_limits);
  if(!max_writes||bindings.size()>state_limits.max_views)fail("Frontend state binding limits exceeded");
  for(std::size_t i=0;i<bindings.size();++i) {
    const auto &b=bindings[i];const auto end=std::uint64_t(b.source_begin)+b.byte_count;
    if(!b.source_begin||!b.byte_count||end>0x2000000U)fail("Invalid frontend source state owner");
    const auto view=std::find_if(canonical.views.begin(),canonical.views.end(),[&](const auto &v){return v.key==b.view_key;});
    if(view==canonical.views.end()||view->value_type!=SessionStateValueTypeV1::u8||view->byte_stride!=1U||
        b.first_element>view->element_count||b.byte_count>view->element_count-b.first_element)
      fail("Frontend binding does not match an existing canonical byte view");
    for(std::size_t j=0;j<i;++j) {
      const auto &previous=bindings[j];
      if(std::uint64_t(previous.source_begin)<end&&
          std::uint64_t(b.source_begin)<std::uint64_t(previous.source_begin)+previous.byte_count)
        fail("Overlapping frontend source state owners");
      const auto previous_view=std::find_if(canonical.views.begin(),canonical.views.end(),
          [&](const auto &v){return v.key==previous.view_key;});
      const auto begin=view->byte_offset+b.first_element;
      const auto previous_begin=previous_view->byte_offset+previous.first_element;
      if(view->buffer_key==previous_view->buffer_key&&begin<previous_begin+previous.byte_count&&
          previous_begin<begin+b.byte_count)
        fail("Distinct frontend source owners alias canonical state bytes");
    }
  }
  std::uint64_t count=0;
  for(const auto &w:writes) {
    if(w.bytes.empty()||!w.source_address||std::uint64_t(w.source_address)+w.bytes.size()>0x2000000U||
        w.bytes.size()>max_writes-count)fail("Frontend source writes exceed limits");
    count+=w.bytes.size();
  }
  if(count>state_limits.max_batch_writes)fail("Frontend writes exceed canonical state batch limit");
  std::vector<game::SessionStateWriteV1> result;result.reserve(static_cast<std::size_t>(count));
  for(const auto &w:writes)for(std::size_t i=0;i<w.bytes.size();++i) {
    const auto address=w.source_address+i;
    const auto b=std::find_if(bindings.begin(),bindings.end(),[&](const auto &owner){return address>=owner.source_begin&&address<std::uint64_t(owner.source_begin)+owner.byte_count;});
    if(b==bindings.end())fail("Reached frontend source write has no canonical state owner");
    result.push_back({b->view_key,b->first_element+address-b->source_begin,SessionStateValueTypeV1::u8,std::to_integer<std::uint32_t>(w.bytes[i])});
  }
  return result;
}

FrontendNoSavePlanV1 compile_rac_frontend_no_save_plan_v1(
    const RacFrontendResetTemplateV1 &reset,const RacFrontendNoSaveCompileBindingsV1 &input,
    std::span<const RacFrontendStateBindingV1> bindings,const SessionStateSchemaV1 &schema,
    const SessionStateLimitsV1 &state_limits,FrontendInputLimitsV1 limits) {
  using F=FrontendNoSaveFieldV1;
  if(!input.node_source_address||input.node_source_address>0x1ffffb0U||
      !input.screen_source_address||input.screen_source_address>0x1ffffc4U||
      reset.copies.size()!=267U)
    fail("Live frontend plan requires bounded source node/screen and complete reset");
  for(const auto &copy:reset.copies)
    if(copy.source_pc!=0x20bed0U||!copy.source_address||copy.bytes.empty()||copy.bytes.size()>0x800U||
        std::uint64_t(copy.source_address)+copy.bytes.size()>0x2000000U)
      fail("Live frontend reset contains an invalid destination copy");
  if(last_value(reset.copies,0x13de60U,1)!=0)
    fail("Live frontend reset does not reach the qualified fresh branch");
  FrontendNoSavePlanV1 p;
  p.state_schema_sha256=hash_session_state_schema_v1(schema,state_limits);
  p.save_result_screen_tokens=input.save_result_screen_tokens;
  p.reset_writes=lower_rac_frontend_input_writes_v1(reset.copies,bindings,schema,state_limits,limits.max_reset_writes);
  const auto canonical=canonicalize_session_state_schema_v1(schema,state_limits);
  const auto check_field=[&](const FrontendInputFieldV1 &f,unsigned n) {
    const auto v=std::find_if(canonical.views.begin(),canonical.views.end(),[&](const auto &v){return v.key==f.view_key;});
    if(v==canonical.views.end()||v->value_type!=SessionStateValueTypeV1::u8||v->byte_stride!=1||
        f.first_element>v->element_count||n>v->element_count-f.first_element)
      fail("Live frontend derived field is not an existing canonical byte view");
  };
  const auto bind=[&](F f,std::uint32_t address,unsigned n=4) {
    const std::array source{word(0,address,0,n)};
    const auto bytes=lower_rac_frontend_input_writes_v1(source,bindings,schema,state_limits,n);
    for(std::size_t i=1;i<bytes.size();++i)
      if(bytes[i].view_key!=bytes[0].view_key||bytes[i].element_index!=bytes[0].element_index+i)
        fail("Live frontend field crosses canonical ownership boundaries");
    p.fields[static_cast<std::size_t>(f)]={bytes[0].view_key,bytes[0].element_index};
  };
  check_field(input.focused,1);check_field(input.previous_result,4);
  p.fields[static_cast<std::size_t>(F::focused)]=input.focused;
  p.fields[static_cast<std::size_t>(F::previous_result)]=input.previous_result;
  bind(F::node_flags,input.node_source_address+0x30U);
  bind(F::node_phase,input.node_source_address+0x4cU);
  bind(F::node_selection,input.node_source_address+0x40U);
  bind(F::sound_object,input.node_source_address+0x14U);
  bind(F::previous_screen,0x1d6040U);
  bind(F::parent_screen,input.screen_source_address+0x38U);
  bind(F::cancel_guard,0x1d6094U);bind(F::pending_save,0x1d6098U);bind(F::readiness,0x1d60c4U);
  bind(F::card_mode,0x15efb0U);bind(F::card_status,0x13d46cU);bind(F::card_result,0x13d474U);
  bind(F::card_type,0x13d398U);bind(F::global_pressed,0x13cc04U);
  bind(F::pressed,0x13cbe4U);bind(F::repeated,0x13cbf4U);bind(F::saved_selection,0x15ef34U);
  bind(F::flags,0x15efb4U);bind(F::preserved_a,0x15eef0U);bind(F::preserved_b,0x15eeecU);bind(F::preserved_c,0x15eee8U);
  bind(F::current_screen,0x1d5f78U);bind(F::save_requested,0x13d48cU);
  bind(F::current_level,0x15ee84U);bind(F::target_level,0x15f6e4U);
  bind(F::level_change_requested,0x15f6fcU);bind(F::transition_requested,0x15f690U);
  bind(F::entry_requested,0x13e15aU,2);
  if(p.reset_writes.size()>state_limits.max_batch_writes||
      state_limits.max_batch_writes-p.reset_writes.size()<62U)
    fail("Live frontend batch cannot contain complete reset and ordered live effects");
  (void)encode_frontend_no_save_plan_v1(p,limits);
  return p;
}

RacNewGameFlowV1 compile_rac_new_game_flow_v1(const RacNewGameFlowInputsV1 &input,
    const RacFrontendResetTemplateV1 *reset,const RacNewGameFlowResourcesV1 &resources,
    std::span<const RacFrontendStateBindingV1> bindings,const SessionStateSchemaV1 &schema,
    const SessionStateLimitsV1 &state_limits,FrontendSequenceLimitsV1 limits) {
  RacNewGameFlowV1 out;out.input_effects=execute_rac_frontend_no_save_v1(input.no_save,reset);
  out.input_state_writes=lower_rac_frontend_input_writes_v1(out.input_effects.writes,bindings,schema,state_limits,limits.max_writes);
  if(!out.input_effects.requested_new_game)return out;
  if(last_value(out.input_effects.writes,0x13de60U,1)!=0U||
      last_value(out.input_effects.writes,0x15ee84U,4)!=0U||
      last_value(out.input_effects.writes,0x15f6e4U,4)!=0U||
      last_value(out.input_effects.writes,0x15f690U,4)!=1U)
    fail("Executed reset does not reach the qualified fresh New Game branch");
  auto continuation=compile_rac_new_game_continuation_v1(input,resources,bindings,schema,state_limits,limits);
  continuation.input_effects=std::move(out.input_effects);
  continuation.input_state_writes=std::move(out.input_state_writes);
  FrontendSequenceCueV1 reset_cue;reset_cue.kind=Kind::session_writes;
  reset_cue.writes=continuation.input_state_writes;
  continuation.sequence->cues.insert(continuation.sequence->cues.begin(),std::move(reset_cue));
  for(auto &cue:continuation.source_cues)++cue.neutral_cue;
  continuation.source_cues.insert(continuation.source_cues.begin(),{0,0x224728U,0x209dc0U});
  validate_frontend_sequence_v1(*continuation.sequence,limits);
  return continuation;
}

RacNewGameFlowV1 compile_rac_new_game_continuation_v1(const RacNewGamePresentationInputsV1 &input,
    const RacNewGameFlowResourcesV1 &resources,std::span<const RacFrontendStateBindingV1> bindings,
    const SessionStateSchemaV1 &schema,const SessionStateLimitsV1 &state_limits,FrontendSequenceLimitsV1 limits) {
  RacNewGameFlowV1 out;
  if(input.language_15ee88!=0U&&input.language_15ee88!=2U&&input.language_15ee88!=3U&&
      input.language_15ee88!=4U&&input.language_15ee88!=5U)
    fail("New Game language has no qualified source channel/card mapping");
  if((input.time_scale_bits!=0x3f555555U||input.updates_per_second!=50U)&&
      (input.time_scale_bits!=0x3f800000U||input.updates_per_second!=60U))
    fail("New Game presentation cadence is outside the qualified source clocks");
  out.loading_language_slot=input.language_15ee88?input.language_15ee88-1U:0U;
  out.audio_channel=input.language_15ee88;
  FrontendSequenceV1 p;p.updates_per_second=input.updates_per_second;p.state_schema_sha256=hash_session_state_schema_v1(schema,state_limits);
  for(unsigned i=0;i<3;++i) {
    p.resources.push_back(resources.loading_cards[i]);p.resources.push_back(resources.movies[i]);
    out.movie_toc_offsets[i]=(input.video_selector_15ee80?0x1998U:0x1938U)+8U*i;
  }
  const auto fade_count=std::count_if(resources.fades.begin(),resources.fades.end(),[](const auto &r){return !r.resource_id.empty();});
  if(fade_count!=0&&fade_count!=3)fail("New Game feedback resource bindings are incomplete");
  if(!fade_count)for(const auto &r:resources.fades)
    if(!r.resource_type.empty()||std::any_of(r.payload_sha256.begin(),r.payload_sha256.end(),[](std::byte b){return b!=std::byte{};}))
      fail("Absent New Game feedback binding contains partial metadata");
  if(fade_count)for(const auto &r:resources.fades) {
    if(r.resource_type!="openrc.frame-color-transfers")fail("New Game fade resource has the wrong neutral type");
    p.resources.push_back(r);
  }
  auto add=[&](FrontendSequenceCueV1 cue,std::uint32_t pc,std::uint32_t callee) {
    out.source_cues.push_back({static_cast<std::uint32_t>(p.cues.size()),pc,callee});p.cues.push_back(std::move(cue));
  };
  auto barrier=[&](const char *key,std::uint32_t pc,std::uint32_t callee,std::uint32_t resource=UINT32_MAX) {
    FrontendSequenceCueV1 c;c.consumer_key=key;c.resource_index=resource;add(std::move(c),pc,callee);
  };
  auto write_cue=[&](std::vector<RacFrontendInputWriteV1> writes,std::uint32_t pc) {
    FrontendSequenceCueV1 c;c.kind=Kind::session_writes;
    c.writes=lower_rac_frontend_input_writes_v1(writes,bindings,schema,state_limits,limits.max_writes);add(std::move(c),pc,0);
  };
  auto level=[&](std::uint32_t value,std::uint32_t pc) {
    write_cue({word(pc,0x15ee84U,value)},pc);
  };
  auto timer=[&](std::uint32_t value) {
    const auto result=evaluate_rac_frontend_timer_v1(value,input.time_scale_bits);
    if(!result||result>p.max_loading_updates)fail("New Game timer is outside the bounded positive domain");
    return static_cast<std::uint32_t>(result);
  };
  auto fade=[&](std::uint32_t frames,std::uint32_t pc,std::uint32_t ordinal) {
    FrontendSequenceCueV1 c;c.kind=Kind::fade;c.consumer_key="presentation/fade";c.updates=frames;
    if(fade_count)c.resource_index=6U+ordinal;add(std::move(c),pc,0x1f4e08U);
  };
  barrier("frontend/exit-and-video-restore",0x1e9dc8U,0U);
  write_cue({word(0x1e9e30U,0x13e15aU,1U,2),word(0x1e9e3cU,0x15f6e4U,0U)},0x1e9e30U);
  level(UINT32_MAX,0x1e9e44U);
  // This barrier owns the still-unlowered233308 setup effects and callees,
  // including audio teardown and render configuration. It cannot be skipped.
  barrier("transition/prepare",0x1e9e40U,0x233308U);
  const auto initial_fade=timer(6U);fade(initial_fade,0x233574U,0);
  out.presentation_calls.push_back({0x233574U,0x1f4e08U,1U,{initial_fade,0,0,0,0},UINT32_MAX});
  for(unsigned i=0;i<3;++i) {
    if(i==2U)level(0U,0x2335dcU);
    const std::uint32_t call=std::array<std::uint32_t,3>{0x233594U,0x2335bcU,0x2335f0U}[i];
    // Preparation can perform actual prepared-resource upload/I/O. It occurs
    // before232ef0 starts12f4a8, even when the initial display gate is false.
    barrier("loading/prepare",0x232f44U,0x232b90U,2U*i);
    if(i==2U) {
      FrontendSequenceCueV1 c;c.kind=Kind::start_level_load;c.consumer_key="level/start-load";c.level_id=0;
      add(std::move(c),0x232f54U,0x12f4a8U);
      write_cue({word(0x232f60U,0x15ef48U,0U,2),word(0x232f68U,0x15ef4aU,0U,2)},0x232f60U);
    }
    barrier("loading/begin-presentation",0x232f70U,0x122598U,2U*i);
    FrontendSequenceCueV1 c;c.kind=Kind::loading_overlay;c.consumer_key="presentation/loading-overlay";
    c.resource_index=2U*i;c.updates=timer(i==1U?180U:240U);c.pending_load_extension=20U;
    out.presentation_calls.push_back({call,0x232ef0U,5U,
        {out.loading_language_slot,i==0U?0U:i==1U?2U:3U,i==0U?1U:i==1U?2U:4U,c.updates,i==2U?1U:0U},i==2U?0U:UINT32_MAX});
    add(std::move(c),call,0x232ef0U);
    fade(2U,0x2332c0U,1);
    barrier("media/prepare",0x2329acU,0x23b670U,2U*i+1U);
    c={};c.kind=Kind::media;c.consumer_key="presentation/media";c.resource_index=2U*i+1U;
    if(input.movie_mode!=-1) {
      c.skip.pressed_any_mask=input.movie_mode==2?UINT32_MAX:0x800U;
      c.skip.held_all_mask=(std::uint64_t(0x8000U)<<28U)|0xfU;
    }
    const auto movie_pc=std::array<std::uint32_t,3>{0x23359cU,0x2335c4U,0x2335f8U}[i];
    out.presentation_calls.push_back({movie_pc,0x232920U,1U,{i,0,0,0,0},i==2U?0U:UINT32_MAX});
    add(std::move(c),movie_pc,0x232920U);
    barrier("media/cleanup-before-fade",0x2329bcU,0x122598U);
    fade(4U,0x2329dcU,2);
    barrier("media/cleanup-after-fade",0x2329f0U,0U);
  }
  FrontendSequenceCueV1 wait;wait.kind=Kind::await_level_load;wait.consumer_key="level/await-load";
  add(std::move(wait),0x233a88U,0x120f30U);
  barrier("transition/cleanup",0x233a94U,0x2350a8U);
  barrier("level/admit-prepared-sections",0x12db18U,0x12da38U);
  barrier("level/enter",0x2465f8U,0x2465f8U);
  validate_frontend_sequence_v1(p,limits);out.sequence=std::move(p);return out;
}
} // namespace openrc
