#include "openrc/frontend_sequence.hpp"
#include "openrc/game_world.hpp"

#include <algorithm>
#include <array>
#include <set>

namespace openrc {
namespace {
using Kind=FrontendSequenceCueKindV1;
using Phase=FrontendSequencePhaseV1;
constexpr std::array<std::byte,8> magic{std::byte{'O'},std::byte{'R'},std::byte{'F'},std::byte{'S'},
    std::byte{'E'},std::byte{'Q'},std::byte{'0'},std::byte{'1'}};
[[noreturn]] void fail(const char *s) {throw FrontendSequenceError(s);}
void key(const std::string &s,const FrontendSequenceLimitsV1 &limits,bool empty=false) {
  if((!empty&&s.empty())||s.size()>limits.max_string_bytes||
      std::any_of(s.begin(),s.end(),[](unsigned char c){return c<33U||c>126U;}))
    fail("Invalid frontend sequence key");
}
void put(std::vector<std::byte>& out,std::uint64_t v,unsigned n=4) {
  for(unsigned i=0;i<n;++i){out.push_back(static_cast<std::byte>(v&255U));v>>=8;}
}
void put_string(std::vector<std::byte>& out,const std::string &s) {
  put(out,s.size());for(unsigned char c:s)out.push_back(static_cast<std::byte>(c));
}
struct Reader {
  std::span<const std::byte> bytes;std::size_t at=0;
  std::span<const std::byte> take(std::size_t n) {
    if(n>bytes.size()-at)fail("Truncated frontend sequence");
    const auto result=bytes.subspan(at,n);at+=n;return result;
  }
  std::uint64_t get(unsigned n=4) {
    const auto data=take(n);std::uint64_t v=0;
    for(unsigned i=0;i<n;++i)v|=std::uint64_t(std::to_integer<unsigned>(data[i]))<<(i*8);
    return v;
  }
  std::uint32_t word(){return static_cast<std::uint32_t>(get());}
  std::string string(const FrontendSequenceLimitsV1 &limits) {
    const auto n=word();if(n>limits.max_string_bytes)fail("Frontend sequence string exceeds limit");
    const auto data=take(n);return std::string(reinterpret_cast<const char*>(data.data()),data.size());
  }
};
}
void validate_frontend_sequence_v1(const FrontendSequenceV1 &p,FrontendSequenceLimitsV1 limits) {
  if(!limits.max_bytes||!limits.max_resources||!limits.max_cues||!limits.max_string_bytes||!limits.max_writes||
      !p.updates_per_second||p.updates_per_second>1000U||!p.max_loading_updates||
      p.max_loading_updates>1048576U||p.resources.size()>limits.max_resources||
      p.cues.empty()||p.cues.size()>limits.max_cues)
    fail("Invalid frontend sequence limits or clock");
  std::uint64_t bytes=104U,writes=0U;std::set<std::string> ids;
  for(const auto &r:p.resources) {
    key(r.resource_id,limits);key(r.resource_type,limits);
    if(!ids.insert(r.resource_id).second)fail("Duplicate frontend sequence resource ID");
    bytes+=40U+r.resource_id.size()+r.resource_type.size();
  }
  for(const auto &c:p.cues) {
    if(static_cast<std::uint32_t>(c.kind)>static_cast<std::uint32_t>(Kind::await_level_load))fail("Unknown frontend sequence cue");
    key(c.consumer_key,limits,c.kind==Kind::session_writes);
    const bool resource=c.kind==Kind::loading_overlay||c.kind==Kind::media;
    const bool timed=c.kind==Kind::fade||c.kind==Kind::loading_overlay;
    const bool level=c.kind==Kind::current_level||c.kind==Kind::start_level_load;
    if((resource?c.resource_index>=p.resources.size():
          (c.kind==Kind::consumer||c.kind==Kind::fade)?(c.resource_index!=UINT32_MAX&&c.resource_index>=p.resources.size()):c.resource_index!=UINT32_MAX)||
        (!timed&&c.updates)||c.updates>p.max_loading_updates||
        (c.kind!=Kind::loading_overlay&&c.pending_load_extension)||
        (c.kind==Kind::loading_overlay&&(!c.pending_load_extension||c.pending_load_extension>p.max_loading_updates))||
        (!level&&c.level_id!=UINT32_MAX)||(c.kind==Kind::start_level_load&&c.level_id==UINT32_MAX)||
        (c.kind!=Kind::media&&(c.skip.pressed_any_mask||c.skip.held_all_mask))||
        (c.kind==Kind::session_writes?(!c.consumer_key.empty()||c.writes.empty()):!c.writes.empty()))
      fail("Noncanonical frontend sequence cue fields");
    if(c.kind==Kind::fade&&c.resource_index!=UINT32_MAX&&
        p.resources[c.resource_index].resource_type!="openrc.frame-color-transfers")
      fail("Frontend fade resource is not a neutral frame color transfer sequence");
    bytes+=40U+c.consumer_key.size();writes+=c.writes.size();
    if(writes>limits.max_writes)fail("Frontend sequence write count exceeds limit");
    for(const auto &w:c.writes) {
      key(w.view_key,limits);
      if(static_cast<std::uint32_t>(w.value_type)>2U||
          (w.value_type==SessionStateValueTypeV1::u8&&w.value_bits>255U)||
          (w.value_type==SessionStateValueTypeV1::u16&&w.value_bits>65535U))
        fail("Invalid frontend sequence state write");
      bytes+=20U+w.view_key.size();
    }
    if(bytes>limits.max_bytes)fail("Frontend sequence exceeds byte limit");
  }
}
std::vector<std::byte> encode_frontend_sequence_v1(const FrontendSequenceV1 &p,FrontendSequenceLimitsV1 limits) {
  validate_frontend_sequence_v1(p,limits);std::vector<std::byte> body;
  put(body,p.updates_per_second);put(body,p.max_loading_updates);
  body.insert(body.end(),p.state_schema_sha256.begin(),p.state_schema_sha256.end());
  for(const auto &r:p.resources) {
    put_string(body,r.resource_id);put_string(body,r.resource_type);
    body.insert(body.end(),r.payload_sha256.begin(),r.payload_sha256.end());
  }
  for(const auto &c:p.cues) {
    put(body,static_cast<std::uint32_t>(c.kind));put_string(body,c.consumer_key);
    put(body,c.resource_index);put(body,c.updates);put(body,c.pending_load_extension);put(body,c.level_id);
    put(body,c.skip.pressed_any_mask);put(body,c.skip.held_all_mask,8);put(body,c.writes.size());
    for(const auto &w:c.writes) {
      put_string(body,w.view_key);put(body,w.element_index,8);
      put(body,static_cast<std::uint32_t>(w.value_type));put(body,w.value_bits);
    }
  }
  std::vector<std::byte> out(magic.begin(),magic.end());put(out,1);put(out,64);put(out,64U+body.size(),8);
  put(out,p.resources.size());put(out,p.cues.size());const auto hash=prepared_content_sha256_v1(body);
  out.insert(out.end(),hash.begin(),hash.end());out.insert(out.end(),body.begin(),body.end());return out;
}
FrontendSequenceV1 decode_frontend_sequence_v1(std::span<const std::byte> bytes,FrontendSequenceLimitsV1 limits) {
  if(bytes.size()<104U||bytes.size()>limits.max_bytes)fail("Frontend sequence envelope exceeds bounds");
  Reader in{bytes};const auto signature=in.take(8);
  if(!std::equal(signature.begin(),signature.end(),magic.begin())||in.get()!=1||in.get()!=64||in.get(8)!=bytes.size())
    fail("Invalid frontend sequence header");
  const auto resource_count=in.word(),cue_count=in.word();
  if(resource_count>limits.max_resources||!cue_count||cue_count>limits.max_cues||
      std::uint64_t(resource_count)*40U+std::uint64_t(cue_count)*40U>bytes.size()-104U)
    fail("Frontend sequence counts exceed limits or available bytes");
  const auto stored=in.take(32);const auto digest=prepared_content_sha256_v1(bytes.subspan(64));
  if(!std::equal(stored.begin(),stored.end(),digest.begin()))fail("Frontend sequence digest mismatch");
  FrontendSequenceV1 p;p.updates_per_second=in.word();p.max_loading_updates=in.word();
  const auto schema=in.take(32);std::copy(schema.begin(),schema.end(),p.state_schema_sha256.begin());
  for(std::uint32_t i=0;i<resource_count;++i) {
    FrontendSequenceResourceV1 r;r.resource_id=in.string(limits);r.resource_type=in.string(limits);
    const auto hash=in.take(32);std::copy(hash.begin(),hash.end(),r.payload_sha256.begin());p.resources.push_back(std::move(r));
  }
  std::uint64_t total_writes=0;
  for(std::uint32_t i=0;i<cue_count;++i) {
    FrontendSequenceCueV1 c;c.kind=static_cast<Kind>(in.word());c.consumer_key=in.string(limits);
    c.resource_index=in.word();c.updates=in.word();c.pending_load_extension=in.word();c.level_id=in.word();
    c.skip.pressed_any_mask=in.word();c.skip.held_all_mask=in.get(8);const auto count=in.word();total_writes+=count;
    if(total_writes>limits.max_writes||std::uint64_t(count)*20U>bytes.size()-in.at)
      fail("Frontend sequence writes exceed limits or available bytes");
    for(std::uint32_t j=0;j<count;++j) {
      game::SessionStateWriteV1 w;w.view_key=in.string(limits);w.element_index=in.get(8);
      w.value_type=static_cast<SessionStateValueTypeV1>(in.word());w.value_bits=in.word();c.writes.push_back(std::move(w));
    }
    p.cues.push_back(std::move(c));
  }
  if(in.at!=bytes.size())fail("Trailing frontend sequence bytes");
  validate_frontend_sequence_v1(p,limits);return p;
}
FrontendSequencePlayerV1::FrontendSequencePlayerV1(FrontendSequenceV1 p,
    std::span<const FrontendSequenceResourceV1> admitted,std::span<const std::string> consumers,FrontendSequenceLimitsV1 limits)
    :program_(std::move(p)),consumers_(consumers.begin(),consumers.end()) {
  validate_frontend_sequence_v1(program_,limits);
  for(const auto &r:program_.resources) {
    const auto found=std::find_if(admitted.begin(),admitted.end(),[&](const auto &a){return a.resource_id==r.resource_id;});
    if(found==admitted.end()||*found!=r||
        std::count_if(admitted.begin(),admitted.end(),[&](const auto &a){return a.resource_id==r.resource_id;})!=1)
      fail("Frontend sequence resource was not independently admitted");
  }
  for(const auto &c:consumers_)key(c,limits);
  std::sort(consumers_.begin(),consumers_.end());
  if(std::adjacent_find(consumers_.begin(),consumers_.end())!=consumers_.end())fail("Duplicate frontend sequence consumer");
  command_.cue_index=0;enter();
}
const FrontendSequenceV1 &FrontendSequencePlayerV1::program() const noexcept{return program_;}
FrontendSequenceCommandV1 FrontendSequencePlayerV1::command() const noexcept{return command_;}
std::vector<std::string> FrontendSequencePlayerV1::missing_consumers() const {
  std::set<std::string> missing;
  for(const auto &c:program_.cues)if(!c.consumer_key.empty()&&
      !std::binary_search(consumers_.begin(),consumers_.end(),c.consumer_key))missing.insert(c.consumer_key);
  return {missing.begin(),missing.end()};
}
void FrontendSequencePlayerV1::enter() {
  command_.frame_index=0;command_.duration=0;command_.poll_level_load=false;
  if(command_.cue_index==program_.cues.size()){command_.phase=load_pending_?Phase::incomplete:Phase::complete;return;}
  const auto &c=program_.cues[command_.cue_index];
  if(!c.consumer_key.empty()&&!std::binary_search(consumers_.begin(),consumers_.end(),c.consumer_key)) {
    command_.phase=Phase::incomplete;return;
  }
  switch(c.kind) {
    case Kind::consumer:case Kind::session_writes:case Kind::current_level:case Kind::start_level_load:
      command_.phase=Phase::consumer;break;
    case Kind::fade:command_.duration=c.updates;command_.phase=Phase::fade_frame;if(!c.updates)next();break;
    case Kind::loading_overlay:command_.duration=c.updates;command_.phase=Phase::loading_gate;break;
    case Kind::media:command_.phase=Phase::media_start;break;
    case Kind::await_level_load:command_.phase=Phase::await_level_load;command_.poll_level_load=true;break;
  }
}
void FrontendSequencePlayerV1::next(){++command_.cue_index;enter();}
void FrontendSequencePlayerV1::apply_session_writes(game::SessionStateV1 &state) {
  if(command_.phase!=Phase::consumer||program_.cues[command_.cue_index].kind!=Kind::session_writes)
    fail("Frontend sequence is not waiting to apply state writes");
  if(state.schema_sha256()!=program_.state_schema_sha256)fail("Frontend sequence state schema mismatch");
  state.apply_batch(program_.cues[command_.cue_index].writes,state.revision());next();
}
void FrontendSequencePlayerV1::apply_session_writes(game::GameSessionV1 &session) {
  if(command_.phase!=Phase::consumer||program_.cues[command_.cue_index].kind!=Kind::session_writes)
    fail("Frontend sequence is not waiting to apply state writes");
  const auto* state=session.persistent_state();
  if(!state||state->schema_sha256()!=program_.state_schema_sha256)
    fail("Frontend sequence session has no matching canonical state");
  session.apply_persistent_state_writes(program_.cues[command_.cue_index].writes,state->revision());
  next();
}
void FrontendSequencePlayerV1::advance(const FrontendSequenceSignalV1 &s) {
  if(command_.phase==Phase::complete||command_.phase==Phase::incomplete)
    fail("Frontend sequence has no executable command");
  const auto &c=program_.cues[command_.cue_index];
  switch(command_.phase) {
    case Phase::consumer:
      if(c.kind==Kind::session_writes)fail("Frontend sequence state writes require the canonical state consumer");
      if(s.consumer_completed){if(c.kind==Kind::start_level_load){
        if(load_pending_)fail("Frontend sequence already has an outstanding level load");
        load_pending_=true;}next();}break;
    case Phase::loading_gate:
      if(!s.loading_presentation_permitted||!command_.duration)next();
      else {command_.phase=Phase::loading_frame;command_.poll_level_load=load_pending_;}break;
    case Phase::loading_frame:
      if(!s.frame_presented)break;
      if(s.level_load_completed.has_value()!=load_pending_)fail("Frontend loading frame has an absent or unexpected load completion result");
      {
        auto duration=command_.duration;
        if(load_pending_&&!*s.level_load_completed) {
          const auto extended=std::uint64_t(command_.frame_index)+c.pending_load_extension;
          if(extended>program_.max_loading_updates)fail("Frontend loading exceeded its bounded update budget");
          duration=std::max(duration,static_cast<std::uint32_t>(extended));
        }
        command_.duration=duration;
        if(load_pending_&&*s.level_load_completed)load_pending_=false;
        ++command_.frame_index;command_.poll_level_load=load_pending_;
        if(command_.frame_index>=duration||!s.loading_presentation_permitted)next();
      }break;
    case Phase::fade_frame:
      if(s.frame_presented&&++command_.frame_index>=command_.duration)next();break;
    case Phase::media_start:if(s.media_started)command_.phase=Phase::media_feed;break;
    case Phase::media_feed:
      if(!s.media_input_available||s.media_decoder_stopped||
          (s.pressed_word&c.skip.pressed_any_mask)||
          (c.skip.held_all_mask&&(s.held_word&c.skip.held_all_mask)==c.skip.held_all_mask))
        command_.phase=Phase::media_drain;
      break;
    case Phase::media_drain:if(s.media_decoder_drained)command_.phase=Phase::media_present_wait;break;
    case Phase::media_present_wait:if(s.media_presentation_drained)next();break;
    case Phase::await_level_load:
      if(s.level_load_completed&&*s.level_load_completed){load_pending_=false;next();}break;
    default:fail("Invalid frontend sequence phase");
  }
}
} // namespace openrc
