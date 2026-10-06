#include "openrc/frontend_menu.hpp"
#include <bit>

namespace openrc {
namespace {
[[noreturn]] void fail(const char *s){throw FrontendInputError(s);}
std::int32_t signed_word(std::uint32_t v){return std::bit_cast<std::int32_t>(v);}
struct Access {
  const game::SessionStateV1 &state;
  FrontendMenuEvaluationV1 out;
  Access(const game::SessionStateV1 &s,std::uint64_t revision):state(s) {
    if(s.revision()!=revision)fail("Stale frontend menu state revision");
    out.expected_revision=revision;
  }
  std::uint32_t read(const std::string &key,unsigned n=4)const {
    const auto view=key+"/bytes";std::uint32_t value=0;
    for(unsigned i=0;i<n;++i) {
      auto byte=state.read_u8(view,i);
      for(auto w=out.writes.rbegin();w!=out.writes.rend();++w)
        if(w->view_key==view&&w->element_index==i){byte=static_cast<std::uint8_t>(w->value_bits);break;}
      value|=std::uint32_t(byte)<<(8*i);
    }
    return value;
  }
  void write(const std::string &key,std::uint32_t value,unsigned n=4) {
    const auto view=key+"/bytes";
    for(unsigned i=0;i<n;++i) {
      (void)state.read_u8(view,i);
      out.writes.push_back({view,i,SessionStateValueTypeV1::u8,(value>>(8*i))&255U});
    }
  }
  FrontendMenuEvaluationV1 unsupported(){out.writes.clear();out.unsupported=true;return std::move(out);}
  void reset(const FrontendNoSavePlanV1 &plan) {
    validate_frontend_no_save_plan_v1(plan,state);
    constexpr std::array fields{FrontendNoSaveFieldV1::preserved_a,FrontendNoSaveFieldV1::preserved_b,FrontendNoSaveFieldV1::preserved_c};
    std::array<std::uint32_t,3> values{};
    for(unsigned f=0;f<3;++f) {
      const auto &ref=plan.fields[static_cast<std::size_t>(fields[f])];
      for(unsigned i=0;i<4;++i)values[f]|=std::uint32_t(state.read_u8(ref.view_key,ref.first_element+i))<<(8*i);
    }
    out.writes.insert(out.writes.end(),plan.reset_writes.begin(),plan.reset_writes.end());
    for(unsigned f=0;f<3;++f) {
      const auto &ref=plan.fields[static_cast<std::size_t>(fields[f])];
      for(unsigned i=0;i<4;++i)out.writes.push_back({ref.view_key,ref.first_element+i,SessionStateValueTypeV1::u8,(values[f]>>(8*i))&255U});
    }
    const auto &ref=plan.fields[static_cast<std::size_t>(FrontendNoSaveFieldV1::current_level)];
    for(unsigned i=0;i<4;++i)out.writes.push_back({ref.view_key,ref.first_element+i,SessionStateValueTypeV1::u8,0});
  }
  void request_game() {
    write("session/target-level",0);write("session/level-change-requested",1);
    write("session/transition-requested",1);write("session/entry-requested",1,2);
    out.requested_new_game=true;
  }
};
} // namespace

FrontendMenuEvaluationV1 evaluate_frontend_platform_bootstrap_v1(
    const game::SessionStateV1 &state,std::uint64_t revision) {
  Access a(state,revision);
  const auto selector=a.read("frontend/config/startup-video-selector");
  if(selector>1)fail("Frontend startup display profile is unsupported");
  a.write("display/video-selector",selector);
  a.write("audio/group5-volume",a.read("progress/primary/field-36"));
  a.write("display/saved-video-selector",selector,1);
  a.write("collision/query-flags",0);
  return std::move(a.out);
}

FrontendMenuEvaluationV1 evaluate_frontend_exit_v1(
    const game::SessionStateV1 &state,std::uint64_t revision) {
  Access a(state,revision);
  if(!a.read("session/transition-requested"))return std::move(a.out);
  const auto live=a.read("display/video-selector"),saved=a.read("display/saved-video-selector",1);
  if(live>1||saved>1)return a.unsupported();
  a.out.frontend_exit_reached=true;
  if(live!=saved) {
    a.write("display/video-selector",saved);a.out.restore_display_selector=saved;
  }
  return std::move(a.out);
}

FrontendMenuEvaluationV1 evaluate_frontend_transition_prefix_v1(
    const game::SessionStateV1 &state,std::uint64_t revision) {
  Access a(state,revision);
  a.write("collision/query-flags",a.read("collision/query-flags")|0x80000000U);
  a.write("frontend/mode",6);a.write("loading/selector",0,2);
  const auto target=signed_word(a.read("session/target-level"));
  if(a.read("loading/selector-first-unlocked",1)||target>=8)a.write("loading/selector",1,2);
  if(a.read("loading/selector-second-unlocked",1)||target>=14)a.write("loading/selector",2,2);
  return std::move(a.out);
}

FrontendMenuEvaluationV1 evaluate_frontend_menu_v1(FrontendMenuOperationV1 operation,
    const FrontendNoSavePlanV1 &reset,const game::SessionStateV1 &state,std::uint64_t revision) {
  if(reset.state_schema_sha256!=state.schema_sha256())fail("Frontend menu state schema mismatch");
  Access a(state,revision);
  switch(operation) {
    case FrontendMenuOperationV1::bootstrap:
      a.write("frontend/new-game-context",1);
      a.write("frontend/title/counter",0);a.write("frontend/title/logo-alpha",0);a.write("frontend/title/prompt-alpha",0);break;
    case FrontendMenuOperationV1::title_input:
      if(a.read("frontend/mode")!=0||!(a.read("input/pressed")&0x840U))break;
      a.write("frontend/root-phase",45);a.write("frontend/root-entry-counter",0);
      a.write("frontend/mode",3);a.write("frontend/root-age",0);a.write("frontend/root-fade",0);
      a.out.requested_menu=true;break;
    case FrontendMenuOperationV1::action4_input: {
      const auto focused=a.read("frontend/main/focused",1);
      if(focused>1)fail("Frontend main focus is not boolean");
      if(!focused)break;
      if(a.read("frontend/main/node-flags")!=4)return a.unsupported();
      const auto pressed=a.read("input/global-pressed");
      if(pressed&0xd00U){a.out.return_word=-1;break;}
      if(pressed&0x10U) {
        const auto parent=a.read("frontend/main/parent-screen");
        if(parent){a.write("frontend/requested-screen",parent);break;}
        if(!a.read("frontend/cancel-guard")){a.out.return_word=-1;break;}
      }
      const auto flags=a.read("frontend/flags");
      if(!(pressed&0x40U)||(flags&1U))break;
      const auto target=a.read("frontend/main/target-screen");
      if(!target||a.read("frontend/mode")!=3)return a.unsupported();
      a.out.sound_requested=true;a.out.sound_variant=0;a.out.sound_object_token=a.read("frontend/main/sound-object");
      a.write("frontend/root-dialog-result",0);a.write("frontend/flags",(flags|2U)&~4U);
      a.write("frontend/dialog/auxiliary",0);a.write("frontend/dialog/target-screen",target);
      a.write("frontend/dialog/previous-mode",3);a.write("frontend/mode",4);a.write("frontend/dialog/kind",3);
      a.write("frontend/dialog/draw-delay",0);a.write("frontend/dialog/age",UINT32_MAX);
      const auto duration=a.read("frontend/config/dialog-duration");
      a.write("frontend/dialog/duration",duration);a.write("frontend/card-busy",3);
      a.write("frontend/dialog/fade",duration);a.write("frontend/dialog/draw-delay",a.read("frontend/config/dialog-delay"));
      a.out.entered_dialog=true;break;
    }
    case FrontendMenuOperationV1::dialog3_update: {
      if(a.read("frontend/dialog/kind")!=3||a.read("frontend/mode")!=4)return a.unsupported();
      a.out.return_word=1;
      auto value=a.read("frontend/dialog/duration");if(value)a.write("frontend/dialog/duration",value-1);
      value=a.read("frontend/dialog/draw-delay");if(value)a.write("frontend/dialog/draw-delay",value-1);
      if(a.read("frontend/card-busy")==1)break;
      const auto age=a.read("frontend/dialog/age")+1U;a.write("frontend/dialog/age",age);
      value=a.read("frontend/dialog/fade");
      if(signed_word(a.read("frontend/config/dialog-duration"))<signed_word(age)&&value)a.write("frontend/dialog/fade",value-1);
      const auto mode=a.read("frontend/card-mode");
      if(mode==1||mode==16) {
        const auto target=a.read("frontend/dialog/target-screen"),previous=a.read("frontend/dialog/previous-mode");
        if(!target||previous!=3)return a.unsupported();
        a.write("frontend/requested-screen",target);a.write("frontend/mode",previous);a.out.requested_menu=true;break;
      }
      if(mode==0||mode==7||mode==8||mode==10||mode==11||mode==14||mode==15||mode==22||mode>24)break;
      if(mode!=4&&mode!=3&&mode!=5&&mode!=23&&mode!=24)return a.unsupported();
      if(a.read("frontend/dialog/fade"))break;
      const auto pressed=a.read("input/pressed");
      const bool fresh=mode==23||mode==24||
          (mode==4&&a.read("frontend/new-game-context")&&a.read("frontend/root-dialog-result")==0);
      if(mode==4&&!a.read("frontend/new-game-context")&&a.read("frontend/existing-game-context")&&(pressed&0x20U))
        return a.unsupported();
      if(fresh&&(pressed&0x40U)) {
        a.reset(reset);a.write("frontend/flags",(a.read("frontend/flags")&~6U)|((mode==23||mode==24)?0x20U:0U));
        a.request_game();
        if(mode==23||mode==24)break;
      }
      if(pressed&0x10U) {
        auto flags=a.read("frontend/flags")|0x20U;
        if(mode!=23&&mode!=24)flags&=~6U;
        a.write("frontend/flags",flags);a.write("frontend/mode",a.read("frontend/dialog/previous-mode"));
      }
      break;
    }
    case FrontendMenuOperationV1::menu_counters: {
      const auto increment=a.read("frontend/root-entry-counter")+1U;
      a.write("frontend/root-entry-counter",signed_word(increment)<32001?increment:32000U);
      const auto cooldown=a.read("frontend/root-cooldown");if(cooldown)a.write("frontend/root-cooldown",cooldown-1);
      a.write("frontend/readiness",signed_word(a.read("frontend/card-status"))<3&&signed_word(a.read("frontend/card-result"))<0?
          a.read("frontend/readiness")+1U:0U);break;
    }
    default:fail("Unknown frontend menu callback");
  }
  return std::move(a.out);
}

FrontendMenuEvaluationV1 evaluate_frontend_title_v1(const ScreenOverlayV1 &title,
    const game::SessionStateV1 &state,std::uint64_t revision) {
  Access a(state,revision);const auto mode=a.read("frontend/mode");
  if(mode!=0&&mode!=3)return std::move(a.out);
  if(title.canvas_width!=512||title.canvas_height!=448||title.updates_per_second!=50||title.coverage_denominator!=128||
      title.loop_begin!=100||title.frames.size()!=160||title.images.size()!=192)
    fail("Title prepared pulse/alpha library has a different profile");
  auto logo=a.read("frontend/title/logo-alpha"),prompt=a.read("frontend/title/prompt-alpha");
  if(logo>64||prompt>128)fail("Title alpha leaves its admitted prepared slice range");
  if(mode==3) {
    a.write("frontend/title/counter",a.read("frontend/config/title-fade-counter"));
    // Preserve the original subtract-before-clamp write order too.
    a.write("frontend/title/logo-alpha",logo-16);if(logo<16)a.write("frontend/title/logo-alpha",0);
    a.write("frontend/title/prompt-alpha",prompt-16);if(prompt<16)a.write("frontend/title/prompt-alpha",0);
    return std::move(a.out);
  }
  const auto counter=a.read("frontend/title/counter")+1U;
  a.write("frontend/title/counter",counter);
  if(signed_word(counter)>25) {
    a.write("frontend/title/logo-alpha",++logo);if(logo>64)a.write("frontend/title/logo-alpha",64);
  }
  if(signed_word(counter)>100) {
    const auto &frame=title.frames[100U+(counter-101U)%60U];
    if(frame.draws.size()!=2||frame.draws[1].image_id<64||frame.draws[1].image_id>191||
        frame.draws[1].x!=160||frame.draws[1].y!=368)
      fail("Title pulse sample lost its prepared prompt binding");
    a.write("frontend/title/prompt-alpha",frame.draws[1].image_id-63U);
  }
  return std::move(a.out);
}

ScreenOverlayFrameV1 frontend_title_presentation_v1(const game::SessionStateV1 &state) {
  Access a(state,state.revision());ScreenOverlayFrameV1 frame;
  const auto logo=a.read("frontend/title/logo-alpha"),prompt=a.read("frontend/title/prompt-alpha");
  if(logo>64||prompt>128)fail("Title draw alpha leaves its admitted prepared slice range");
  if(logo)frame.draws.push_back({logo-1U,236,16});
  if(prompt)frame.draws.push_back({63U+prompt,160,368});
  return frame;
}

FrontendCardCommandV1 frontend_card_command_v1(const game::SessionStateV1 &state) {
  Access a(state,state.revision());
  if(a.read("frontend/card/sync-pending"))return FrontendCardCommandV1::poll;
  switch(a.read("frontend/card-status")) {
    case 0:return FrontendCardCommandV1::request_status;
    case 1:case 2:return FrontendCardCommandV1::none;
    default:return FrontendCardCommandV1::unsupported;
  }
}

FrontendMenuEvaluationV1 evaluate_frontend_absent_card_v1(const game::SessionStateV1 &state,
    std::uint64_t revision,const FrontendCardSignalV1 &signal) {
  Access a(state,revision);const auto command=frontend_card_command_v1(state);
  if(command==FrontendCardCommandV1::unsupported)return a.unsupported();
  const bool completed=command==FrontendCardCommandV1::poll&&signal.poll==FrontendCardPollStatusV1::completed_absent;
  if(completed!=signal.completed_result.has_value()||(completed&&*signal.completed_result>=0))
    fail("Completed unavailable-card query requires its actual negative result");
  if(command==FrontendCardCommandV1::poll) {
    if(!signal.poll||signal.request_accepted)fail("Card poll requires the actual host completion result");
    if(*signal.poll!=FrontendCardPollStatusV1::no_request&&*signal.poll!=FrontendCardPollStatusV1::pending&&
        *signal.poll!=FrontendCardPollStatusV1::completed_absent)fail("Unknown card poll completion result");
    if(*signal.poll==FrontendCardPollStatusV1::completed_absent) {
      a.write("frontend/card-type",0);a.write("frontend/card/free-blocks",0);a.write("frontend/card/formatted",0);
      a.write("frontend/card/io-command",signal.completed_command_token);
      a.write("frontend/card/io-result",std::bit_cast<std::uint32_t>(*signal.completed_result));
    }
    a.write("frontend/card/sync-pending",*signal.poll==FrontendCardPollStatusV1::pending?1U:0U);
  } else {
    if(signal.poll||(command!=FrontendCardCommandV1::request_status&&signal.request_accepted))fail("Unexpected card operation result");
    a.write("frontend/card/sync-pending",1);
    const auto phase=a.read("frontend/card-status");
    if(phase==0) {
      if(!signal.request_accepted)fail("Card query requires the actual host request result");
      if(signed_word(a.read("frontend/card/index"))<0)a.write("frontend/card/index",0);
      if(a.read("frontend/card/index")!=0||a.read("frontend/card/port")!=0||a.read("frontend/card/slot")!=0)
        return a.unsupported();
      if(*signal.request_accepted)a.write("frontend/card-status",1);
    } else if(phase==1) {
      const auto result=a.read("frontend/card/io-result");
      if(a.read("frontend/card/index")!=0||signed_word(result)>=0||a.read("frontend/card-type")!=0)
        return a.unsupported();
      a.write("frontend/card/slot-error",0xfffffffcU);a.write("frontend/card/slot-result",result);
      a.write("frontend/card/slot-change",UINT32_MAX);a.write("frontend/card/slot-scan",0);
      if(a.read("frontend/card-busy")==1)a.write("frontend/card-busy",result==UINT32_MAX?2U:0U);
      a.write("frontend/card/index",1);a.write("frontend/card-status",2);a.write("frontend/card/sync-pending",0);
    } else {
      if(signed_word(a.read("frontend/card-result"))>=0)return a.unsupported();
      const auto index=a.read("frontend/card/index")+1U;const auto busy=a.read("frontend/card-busy");
      a.write("frontend/card/index",index);if(busy==3)a.write("frontend/card-busy",1);
      if(signed_word(index)>=11||a.read("frontend/card-busy")==1){a.write("frontend/card-status",0);a.write("frontend/card/index",0);}
      a.write("frontend/card/sync-pending",0);
    }
  }
  // Actual209070 owner follows209e68, and reads the preceding writes.
  if(a.read("frontend/card/error")||a.read("frontend/card/slot-result"))a.write("frontend/card/sticky-result",1);
  auto flags=a.read("frontend/flags");const auto previous=a.read("frontend/card-mode");
  if(flags&(0x80U|0x100U))return a.unsupported();
  switch(a.read("frontend/card-mode")) {
    case 0:a.write("frontend/card-mode",3);a.write("frontend/save-requested",0);a.write("frontend/card/slot-result",a.read("frontend/card/io-result"));break;
    case 3:
      for(const auto *key:{"frontend/card/slot-summary","frontend/card-result","frontend/card/request-argument",
          "frontend/card/entry-0","frontend/card/entry-1","frontend/card/entry-2","frontend/card/entry-3"})a.write(key,UINT32_MAX);
      if(a.read("frontend/card/index")!=0)return a.unsupported();
      if(a.read("frontend/card-busy")==2)a.write("frontend/card-busy",0);
      a.write("frontend/card-mode",4);break;
    case 4:
      a.write("frontend/flags",flags&~0x20U);
      if(a.read("frontend/card-type")==2)return a.unsupported();
      break;
    default:return a.unsupported();
  }
  a.write("frontend/card/mode-age",a.read("frontend/card/mode-age")+1U);
  if(a.read("frontend/card-mode")!=previous)a.write("frontend/card/mode-age",0);
  return std::move(a.out);
}

FrontendDialogPresentationV1 evaluate_frontend_dialog_presentation_v1(const game::SessionStateV1 &state) {
  Access a(state,state.revision());FrontendDialogPresentationV1 out;
  if(a.read("frontend/mode")!=4)return out;
  out.backdrop=true;out.backdrop_coverage=48;
  if(a.read("frontend/dialog/draw-delay"))return out;
  if(a.read("frontend/dialog/kind")!=3){out.unsupported=true;return out;}
  if(a.read("frontend/card-busy")||signed_word(a.read("frontend/dialog/age"))<=0)return out;
  const auto card=a.read("frontend/card-mode");
  if(card==3||card==4) {
    const auto result=a.read("frontend/root-dialog-result");
    if(a.read("frontend/new-game-context")&&!result) {
      out.message=FrontendDialogMessageV1::absent_fresh;
      out.left=FrontendDialogPromptV1::continue_without_save;
      out.right=FrontendDialogPromptV1::cancel;
    } else if(a.read("frontend/existing-game-context")) {
      out.message=FrontendDialogMessageV1::absent_existing;
      out.left=FrontendDialogPromptV1::continue_existing_game;
      out.right=FrontendDialogPromptV1::cancel;
    } else {
      out.message=result?FrontendDialogMessageV1::unavailable:FrontendDialogMessageV1::absent_without_game;
      out.center=FrontendDialogPromptV1::back;
    }
  } else if(card!=0) {out.unsupported=true;return out;}
  out.prepared_duration=a.read("frontend/config/dialog-duration");
  out.body_remaining=a.read("frontend/dialog/duration");out.prompt_remaining=a.read("frontend/dialog/fade");
  if(!out.prepared_duration||out.body_remaining>out.prepared_duration||out.prompt_remaining>out.prepared_duration) {
    out.unsupported=true;return out;
  }
  out.panel=true;return out;
}

FrontendMenuEvaluationV1 evaluate_frontend_main_entry_v1(const game::SessionStateV1 &state,
    std::uint64_t revision,const std::optional<FrontendMainResourcesReadyV1> &ready) {
  Access a(state,revision);
  if(a.read("frontend/mode")!=3)return a.unsupported();
  const auto phase=a.read("frontend/root-phase");
  if(phase==0||phase==45) {
    if(a.read("frontend/current-screen")||a.read("frontend/requested-screen")||(a.read("frontend/flags")&1U))
      return a.unsupported();
    if(!ready){a.out.awaiting_resources=true;return std::move(a.out);}
    for(unsigned i=0;i<ready->actor_tokens.size();++i) {
      if(!ready->actor_tokens[i])fail("Main entry requires all actual admitted actor handles");
      for(unsigned j=0;j<i;++j)if(ready->actor_tokens[i]==ready->actor_tokens[j])fail("Main actor handles alias different admitted objects");
    }
    // Initial219e90 establishes main=current=requested; the same update's
    //21a380 stage begins the real reverse same-screen transition.
    a.write("frontend/root-phase",2);a.write("frontend/requested-screen",1);
    a.write("frontend/cancel-guard",0);a.write("frontend/current-screen",1);
    a.write("frontend/main/sound-object",ready->actor_tokens[2]);
    a.write("frontend/root-phase",1);a.write("frontend/transition-remaining",12);
    a.write("frontend/previous-screen",1);a.write("frontend/current-screen",0);
    a.write("frontend/main/focused",0,1);a.write("frontend/no-save/focused",0,1);
    a.out.sound_requested=true;a.out.sound_variant=3;a.out.sound_object_token=ready->actor_tokens[0];
    a.out.began_main_entry=true;return std::move(a.out);
  }
  if(ready)fail("Main resource completion supplied after its admission stage");
  if(phase==1) {
    if(a.read("frontend/current-screen")||a.read("frontend/requested-screen")!=1||a.read("frontend/previous-screen")!=1)
      return a.unsupported();
    const auto before=a.read("frontend/transition-remaining");
    const auto remaining=signed_word(before)<1?0U:before-1U;
    a.write("frontend/transition-remaining",remaining);
    if(remaining)return std::move(a.out);
    a.write("frontend/requested-screen",0);a.write("frontend/current-screen",1);
    a.write("frontend/root-phase",46);a.write("frontend/main/focused",1,1);a.write("frontend/no-save/focused",0,1);
    a.out.main_entry_complete=true;return std::move(a.out);
  }
  if(phase!=46||a.read("frontend/current-screen")!=1||a.read("frontend/requested-screen"))return a.unsupported();
  return std::move(a.out);
}
} // namespace openrc
