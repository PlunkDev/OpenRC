#include "openrc/frontend_sequence.hpp"
#include "openrc/game_world.hpp"
#include "openrc/rac_frontend_loading.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace openrc;
using K=FrontendSequenceCueKindV1;
using P=FrontendSequencePhaseV1;
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
template<class F>void rejects(F &&f){try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error("Invalid neutral sequence was accepted");}
FrontendSequenceResourceV1 resource() {
  const std::array<std::byte,3> payload{std::byte{7},std::byte{11},std::byte{31}};
  return {"fixture/presentation","fixture/presentation-v1",prepared_content_sha256_v1(payload)};
}
FrontendSequenceCueV1 cue(K kind,const char *key) {FrontendSequenceCueV1 c;c.kind=kind;c.consumer_key=key;return c;}
FrontendSequenceV1 base() {FrontendSequenceV1 p;p.updates_per_second=50;p.resources={resource()};return p;}
const std::vector<std::string> consumers{"show","load","wait","fade","finish"};
void codec() {
  auto p=base();auto c=cue(K::media,"show");c.resource_index=0;c.skip={0x800U,0x8000000000fULL};p.cues={c};
  const auto bytes=encode_frontend_sequence_v1(p);
  check(encode_frontend_sequence_v1(decode_frontend_sequence_v1(bytes))==bytes,"Sequence codec is not canonical");
  for(std::size_t n=0;n<bytes.size();++n)rejects([&]{(void)decode_frontend_sequence_v1(std::span(bytes).first(n));});
  auto bad=bytes;bad.back()^=std::byte{1};rejects([&]{(void)decode_frontend_sequence_v1(bad);});
  auto limits=FrontendSequenceLimitsV1{};limits.max_bytes=bytes.size()-1U;
  rejects([&]{(void)encode_frontend_sequence_v1(p,limits);});
  auto catalog=p.resources;catalog[0].payload_sha256[0]^=std::byte{1};
  rejects([&]{FrontendSequencePlayerV1 player(p,catalog,consumers);});
  catalog=p.resources;catalog.push_back(catalog[0]);rejects([&]{FrontendSequencePlayerV1 player(p,catalog,consumers);});
  FrontendSequencePlayerV1 incomplete(p,p.resources,{});
  check(incomplete.command().phase==P::incomplete&&incomplete.missing_consumers()==std::vector<std::string>{"show"},"Missing real consumer was reported as executable");
  rejects([&]{incomplete.advance({.consumer_completed=true,.media_started=true});});
  p.cues[0].updates=1;rejects([&]{validate_frontend_sequence_v1(p);});
  auto fade=cue(K::fade,"fade");fade.updates=2;fade.resource_index=0;p.cues={fade};
  rejects([&]{validate_frontend_sequence_v1(p);});
  p.resources[0].resource_type="openrc.frame-color-transfers";
  const auto bound=encode_frontend_sequence_v1(p);
  check(decode_frontend_sequence_v1(bound).cues[0].resource_index==0,"Prepared fade binding lost in neutral codec");
  catalog=p.resources;catalog[0].payload_sha256[0]^=std::byte{1};
  rejects([&]{FrontendSequencePlayerV1 player(p,catalog,consumers);});
  p.cues[0].resource_index=1;rejects([&]{validate_frontend_sequence_v1(p);});
}
void media_and_fade() {
  auto p=base();auto movie=cue(K::media,"show");movie.resource_index=0;movie.skip={0x800U,0x8000000000fULL};
  auto fade=cue(K::fade,"fade");fade.updates=2;p.cues={movie,fade};
  FrontendSequencePlayerV1 player(p,p.resources,consumers);
  player.advance({.media_input_available=false,.media_presentation_drained=true});
  check(player.command().phase==P::media_start,"Unstarted media accepted EOF");
  player.advance({.media_started=true});player.advance({.pressed_word=0x40});
  check(player.command().phase==P::media_feed,"Non-skip input ended a film");
  player.advance({.pressed_word=0x800});check(player.command().phase==P::media_drain,"Start did not request drain");
  player.advance({.media_presentation_drained=true});check(player.command().phase==P::media_drain,"Presentation signal bypassed decoder drain");
  player.advance({.media_decoder_drained=true});check(player.command().phase==P::media_present_wait,"Decoder drain skipped presentation wait");
  player.advance({.media_presentation_drained=true});check(player.command().phase==P::fade_frame,"Film did not enter its ordered fade");
  player.advance({.pressed_word=UINT32_MAX});check(player.command().frame_index==0,"Input skipped timed fade");
  player.advance({.frame_presented=true});check(player.command().phase==P::fade_frame,"Fade completed one frame early");
  player.advance({.frame_presented=true});check(player.command().phase==P::complete,"Presented fade did not complete");
}
void loading_clock() {
  // Differential check against the existing original232ef0 clock, whose
  // independent432-case raw instruction fixture is separately qualified.
  for(bool initial_gate:{false,true})for(unsigned finish:{0U,1U,199U,211U}) {
    auto p=base();auto start=cue(K::start_level_load,"load");start.level_id=0;
    auto card=cue(K::loading_overlay,"show");card.resource_index=0;card.updates=200;card.pending_load_extension=20;
    p.cues={start,card,cue(K::await_level_load,"wait")};
    FrontendSequencePlayerV1 player(p,p.resources,consumers);player.advance({.consumer_completed=true});
    auto source=begin_rac_frontend_loading_card_v1(3,4,200,true,0,initial_gate?0U:3U,UINT32_MAX).state;
    player.advance({.loading_presentation_permitted=initial_gate});
    while(source.render_pending) {
      const auto command=player.command();check(command.phase==P::loading_frame&&command.frame_index==source.frame&&
          command.duration==source.duration&&command.poll_level_load==source.load_pending,"Neutral loading clock diverged from source");
      const bool complete=source.frame>=finish;const auto poll=source.load_pending?std::optional<bool>{complete}:std::nullopt;
      player.advance({.frame_presented=true,.loading_presentation_permitted=true,.level_load_completed=poll});
      source=advance_rac_frontend_loading_card_v1(source,0,UINT32_MAX,poll?std::optional<std::uint32_t>{*poll?7U:0U}:std::nullopt);
    }
    check(player.command().phase==P::await_level_load,"Loading gate bypassed the final real I/O wait");
    player.advance({.consumer_completed=true});check(player.command().phase==P::await_level_load,"Generic acknowledgement completed level I/O");
    player.advance({.level_load_completed=true});check(player.command().phase==P::complete,"Actual level completion did not finish wait");
  }
  auto p=base();auto start=cue(K::start_level_load,"load");start.level_id=0;p.cues={start};
  FrontendSequencePlayerV1 outstanding(p,p.resources,consumers);outstanding.advance({.consumer_completed=true});
  check(outstanding.command().phase==P::incomplete,"Outstanding level load was claimed complete");
}
void state_ownership() {
  const SessionStateLimitsV1 limits{1,1,64,256,16,16,16,16,16};
  const SessionStateSchemaV1 schema{"fixture/state",{{"bytes",4}},{{"view","bytes",SessionStateValueTypeV1::u8,0,4,1}}};
  game::SessionStateV1 state(SessionStateInitialV1{schema,{{"bytes",std::vector<std::byte>(4,std::byte{0xa5})}}},limits);
  auto p=base();p.state_schema_sha256=hash_session_state_schema_v1(schema,limits);
  auto c=cue(K::session_writes,"");c.writes={{"view",1,SessionStateValueTypeV1::u8,3},{"view",1,SessionStateValueTypeV1::u8,7}};p.cues={c};
  FrontendSequencePlayerV1 player(p,p.resources,{});
  rejects([&]{player.advance({.consumer_completed=true});});
  player.apply_session_writes(state);check(state.read_u8("view",1)==7&&state.read_u8("view",0)==0xa5&&state.revision()==1,
      "Canonical state write order, untouched bytes or revision changed");
  check(player.command().phase==P::complete,"State application did not advance sequence");
  game::GameSessionV1 session(17U,SessionStateInitialV1{schema,{{"bytes",std::vector<std::byte>(4,std::byte{0xa5})}}},limits);
  const auto* allocation=session.persistent_state()->buffer_bytes("bytes").data();
  FrontendSequencePlayerV1 owned(p,p.resources,{});
  owned.apply_session_writes(session);
  check(session.persistent_state()->snapshot()==state.snapshot()&&
      session.persistent_state()->buffer_bytes("bytes").data()==allocation&&owned.command().phase==P::complete,
      "Sequence replaced canonical GameSession state instead of committing its writes");
  FrontendSequencePlayerV1 missing_state(p,p.resources,{});game::GameSessionV1 empty;
  rejects([&]{missing_state.apply_session_writes(empty);});
  check(missing_state.command().phase==P::consumer,"Missing session state advanced the sequence");
  p.state_schema_sha256[0]^=std::byte{1};FrontendSequencePlayerV1 mismatched(p,p.resources,{});
  rejects([&]{mismatched.apply_session_writes(state);});check(state.revision()==1,"Mismatched state was mutated");
}
}
int main(){try{codec();media_and_fade();loading_clock();state_ownership();std::cout<<"4 frontend sequence groups passed\n";return 0;}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
