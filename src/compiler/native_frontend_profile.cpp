#include "openrc/native_frontend_profile.hpp"
#include "openrc/rac_frontend_menu_resources.hpp"
#include "openrc/rac_frontend_state.hpp"
#include "openrc/loading_presentation.hpp"
#include "openrc/media_clip.hpp"
#include "openrc/rac_startup.hpp"
#include "openrc/audio_clip.hpp"
#include "openrc/audio_program_cues.hpp"
#include "openrc/audio_voice_bank_player.hpp"
#include "openrc/state_installation.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace openrc {
namespace {
using Resource=LevelPackageResourceV1;
using Provenance=LevelPackageProvenanceV1;
using PKind=LevelPackageProvenanceKindV1;
[[noreturn]] void fail(){throw std::runtime_error("Native frontend resource profile differs");}
void require(bool value){if(!value)fail();}
const Resource &resource(const LevelPackageV1 &package,const std::string &id,const char *type) {
  const Resource *found=nullptr;
  for(const auto &r:package.resources)if(r.resource_id==id){require(!found);found=&r;}
  require(found&&found->type_id==type&&found->schema_version==1&&
      found->operation==LevelPackageResourceOperationV1::upsert&&found->flags==kLevelPackageResourceOverlayReplaceableV1&&
      !found->payload.empty()&&found->payload.size()<=128U*1024U*1024U&&
      found->payload_sha256==prepared_content_sha256_v1(found->payload));
  return *found;
}
bool same(const Provenance &a,const Provenance &b) {
  return a.kind==b.kind&&a.source_locator==b.source_locator&&a.source_offset==b.source_offset&&
      a.source_bytes==b.source_bytes&&a.source_sha256==b.source_sha256;
}
void provenance(const Resource &r,std::vector<Provenance> expected) {
  require(r.provenance.size()==expected.size());
  for(const auto &p:r.provenance) {
    const auto found=std::find_if(expected.begin(),expected.end(),[&](const auto &e){return same(e,p);});
    require(found!=expected.end());expected.erase(found);
  }
}
Provenance reference(const Resource &r){return {PKind::prepared_resource,r.resource_id,0,r.payload.size(),r.payload_sha256};}
Provenance source_range(const Resource &r,const char *id,std::uint64_t image_bytes,bool sectors) {
  const Provenance *found=nullptr;
  for(const auto &p:r.provenance)if(p.kind==PKind::iso_range&&p.source_locator==id){require(!found);found=&p;}
  require(found&&found->source_offset>=1506ULL*2048&&found->source_offset%2048==0&&found->source_offset<image_bytes&&
      found->source_bytes>0&&found->source_bytes<=128U*1024U*1024U&&found->source_bytes<=image_bytes-found->source_offset&&
      (!sectors||found->source_bytes%2048==0)&&!is_zero_prepared_digest_v1(found->source_sha256));
  return *found;
}
void overlay_profile(const ScreenOverlayV1 &p,std::uint32_t frames) {
  require(p.canvas_width==512&&p.canvas_height==448&&p.updates_per_second==50&&p.coverage_denominator==128&&
      p.loop_begin==UINT32_MAX&&p.frames.size()==frames);
}
std::uint32_t state_word(const game::SessionStateV1 &state,const std::string &key) {
  std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(state.read_u8(key+"/bytes",i))<<(8*i);return value;
}
void ambient_profile(const LevelPackageV1& package,const Provenance& image,const Provenance& boot,
    const Provenance& source_bank,const Provenance& source_modules) {
  const Provenance pass{PKind::generated,"compiler/rac-frontend-ambient-resources-v1-effects1024-stereo",0,0,{}};
  const std::vector<Provenance> sources{image,boot,pass,source_bank,source_modules};
  const auto& program_resource=resource(package,"frontend/audio/ambient-program","openrc.audio-program");
  const auto& bank_resource=resource(package,"frontend/audio/ambient-bank","openrc.audio-voice-bank");
  const auto& cue_resource=resource(package,"frontend/audio/ambient-cues","openrc.audio-program-cues");
  const auto& timeline_resource=resource(package,"frontend/background/timeline","openrc.scene-timeline");
  provenance(program_resource,sources);
  auto program=decode_audio_program_bank_v1(program_resource.payload);
  auto bank=decode_audio_voice_bank_v1(bank_resource.payload);
  const auto cues=decode_audio_program_cues_v1(cue_resource.payload);
  const auto timeline=decode_scene_timeline_v1(timeline_resource.payload);
  require(program.ticks_per_second==240 && program.modulation_tick_divisor==2 &&
      program.random.words.size()==250 && program.random.index==0 && program.random.forward_tap==103 &&
      program.programs.size()==5 && bank.program_resource_id==program_resource.resource_id &&
      bank.stream_resource_ids.size()==13 && bank.gain_resource_ids.size()==4 &&
      bank.phase_curves.size()==11 && bank.bindings.size()==26);
  const auto& observation=bank.observation;
  require(observation.frame_stride==200 && observation.first_frame_offset==0 &&
      observation.zero_observations_before_completion==4 &&
      observation.observations_after_completion_before_retire==1 &&
      observation.zero_observations_after_release==1 &&
      observation.schedule_order==AudioVoiceScheduleOrderV1::observe_program_release_start_modulation);
  constexpr std::array<std::uint32_t,5> keys{2,3,8,4,9},voice_counts{12,1,1,11,1};
  constexpr std::array<std::uint64_t,5> starts{0,0,0,740,276},ends{99999999,99999999,99999999,1240,300};
  require(cues.voice_bank_resource_id==bank_resource.resource_id && cues.timeline_resource_id==timeline_resource.resource_id &&
      cues.updates_per_second==50 && cues.updates_per_second==timeline.updates_per_second &&
      cues.cues.size()==5 && timeline.loop && timeline.samples.size()==1398);
  for(unsigned i=0;i<keys.size();++i) {
    require(program.programs[i].key==keys[i] && program.programs[i].voice_count==voice_counts[i] &&
        cues.cues[i]==AudioProgramCueV1{i,keys[i],starts[i],ends[i]});
    require(std::ranges::any_of(program.programs[i].nodes,[](const auto& node){
      return std::holds_alternative<AudioProgramStopSectionV1>(node.action);}));
  }
  for(const auto& curve:bank.phase_curves)require(curve.first_control==-32768 && curve.increments.size()==65536);
  std::vector<Provenance> bank_sources=sources;bank_sources.push_back(reference(program_resource));
  std::vector<AudioVoiceBankStreamV1> streams;
  for(unsigned i=0;i<13;++i) {
    const auto id="frontend/audio/ambient-stream-"+std::to_string(i);
    require(bank.stream_resource_ids[i]==id);
    const auto& r=resource(package,id,"openrc.audio-stream");provenance(r,sources);bank_sources.push_back(reference(r));
    auto stream=decode_audio_stream_v1(r.payload);
    require(stream.output_sample_rate==48000 && stream.phase_denominator==4096 &&
        stream.coefficient_denominator==32768 && stream.gain_denominator==32768 &&
        (stream.repeat_end!=0)==(i==3 || i==4));
    streams.push_back({id,std::move(stream)});
  }
  std::vector<AudioVoiceBankGainV1> gains;
  for(unsigned i=0;i<4;++i) {
    const auto id="frontend/audio/ambient-gain-"+std::to_string(i);
    require(bank.gain_resource_ids[i]==id);
    const auto& r=resource(package,id,"openrc.audio-gain-table");provenance(r,sources);bank_sources.push_back(reference(r));
    auto gain=decode_audio_gain_table_v1(r.payload);
    require(gain.state_count==6 && gain.level_count==128 && gain.pan_count==360 &&
        gain.initial_state==0 && gain.gain_denominator==32768);
    gains.push_back({id,std::move(gain)});
  }
  provenance(bank_resource,std::move(bank_sources));
  auto cue_sources=sources;cue_sources.push_back(reference(bank_resource));cue_sources.push_back(reference(timeline_resource));
  provenance(cue_resource,std::move(cue_sources));
  for(const auto& binding:bank.bindings) {
    require(binding.level.minimum==0 && binding.level.maximum==127 && !binding.level.wrap &&
        binding.pan.minimum==0 && binding.pan.maximum==359 && binding.pan.wrap &&
        binding.phase.minimum==-32768 && binding.phase.maximum==32767 && !binding.phase.wrap &&
        binding.envelope.maximum_level==32767 && binding.envelope.counter_period==32768);
    const auto& stream=streams[binding.stream_index].stream;
    require(binding.read_ahead.has_value()==(stream.repeat_end==0));
    if(binding.read_ahead)require(binding.read_ahead->refill_samples==4 &&
        binding.read_ahead->refill_when_available_at_most==12 && binding.read_ahead->required_lookahead_samples==4);
  }
  // The neutral owner validates every program voice, resource and arithmetic
  // domain, including bounded stop tails. No admission, render or device work.
  const AudioVoiceBankPlayerV1 admitted(std::move(bank),program_resource.resource_id,
      std::move(program),std::move(streams),std::move(gains));
}
}

bool exact_native_menu_flow_profile_v1(const LevelPackageV1 &package,
    std::uint64_t image_bytes,const PreparedContentDigestV1 &image_digest,
    std::uint64_t boot_bytes,const PreparedContentDigestV1 &boot_digest) {
  try {
    require(package.level_id==kPreparedGameSharedPackageIdV2&&package.resources.size()<=64&&image_bytes&&boot_bytes&&
        !is_zero_prepared_digest_v1(image_digest)&&!is_zero_prepared_digest_v1(boot_digest));
    const Provenance image{PKind::iso_range,"rac1/disc-image",0,image_bytes,image_digest};
    const Provenance boot{PKind::prepared_resource,"rac1/boot-executable",0,boot_bytes,boot_digest};
    const Provenance menu_pass{PKind::generated,"compiler/rac-frontend-menu-resources-v1",0,0,{}};
    const Provenance game_pass{PKind::generated,"compiler/rac-new-game-resources-v1",0,0,{}};
    const Provenance state_pass{PKind::generated,"openrc.rac-frontend-state-compile.v1",0,0,{}};
    const auto &actors=resource(package,"frontend/menu/actors","openrc.actor-library");
    const auto &animation=resource(package,"frontend/menu/animation","openrc.actor-animation-bank");
    const auto &timeline=resource(package,"frontend/menu/timeline","openrc.scene-timeline");
    const auto &lists=resource(package,"frontend/menu/lists","openrc.screen-overlay");
    const auto wad=source_range(actors,"rac1/global-toc/14e8",image_bytes,true);
    for(const auto *r:{&actors,&animation,&lists})provenance(*r,{image,boot,menu_pass,wad});
    provenance(timeline,{image,boot,menu_pass,wad,reference(actors),reference(animation)});
    // The startup scenic bank and main/dialog assets must share one WAD owner.
    for(const auto &r:package.resources)if(r.resource_id=="frontend/background/actors")
      require(same(wad,source_range(r,"rac1/global-toc/14e8",image_bytes,true)));
    const auto library=decode_actor_library_v1(actors.payload,rac_frontend_menu_actor_io_limits_v1());
    const auto bank=decode_actor_animation_bank_v1(animation.payload,rac_frontend_menu_animation_io_limits_v1());
    const auto entry=decode_scene_timeline_v1(timeline.payload);
    require(library.models.size()==1&&library.rigs.size()==1&&library.rigs[0].semantic_key=="frontend/menu/rig"&&
        library.models[0].semantic_key=="frontend/menu/model"&&bank.clips.size()==14&&entry.actors.size()==14&&
        entry.samples.size()==13&&!entry.loop&&entry.updates_per_second==50&&entry.display_aspect_numerator==512&&
        entry.display_aspect_denominator==512&&entry.actor_library_sha256==actors.payload_sha256&&
        entry.actor_animation_sha256==animation.payload_sha256);
    validate_scene_timeline_bindings_v1(entry,library,bank);
    for(unsigned i=0;i<14;++i) {
      const auto key="frontend/menu/slot/"+std::string(i<10?"0":"")+std::to_string(i);
      require(bank.clips[i].semantic_key==key&&bank.clips[i].rig_key=="frontend/menu/rig"&&
          bank.clips[i].source_updates_per_second==50&&bank.clips[i].wrap_mode==ActorAnimationWrapModeV1::clamp&&
          entry.actors[i].rig_index==0&&entry.actors[i].model_index==0);
      for(const auto &sample:entry.samples)require(sample.actors[i].enabled==(i!=6)&&sample.actors[i].clip_index==i);
    }
    const auto labels=decode_screen_overlay_v1(lists.payload);overlay_profile(labels,20);
    for(unsigned i=0;i<20;++i)require(labels.frames[i].draws.size()==(i<12?0U:3U));
    constexpr std::array<const char*,9> dialog_ids{"frontend/dialog/backdrop","frontend/dialog/body/empty",
      "frontend/dialog/body/absent","frontend/dialog/body/without-game","frontend/dialog/body/unavailable",
      "frontend/dialog/prompts/new","frontend/dialog/prompts/existing","frontend/dialog/prompts/without-game","frontend/dialog/prompts/unavailable"};
    unsigned dialog_draws=0;
    for(unsigned i=0;i<dialog_ids.size();++i) {
      const auto &r=resource(package,dialog_ids[i],"openrc.screen-overlay");provenance(r,{image,boot,menu_pass,wad});
      const auto layer=decode_screen_overlay_v1(r.payload,rac_frontend_menu_overlay_limits_v1());overlay_profile(layer,i?26:1);
      dialog_draws+=static_cast<unsigned>(layer.frames[0].draws.size());
      if(!i) {
        require(layer.frames[0].draws.size()==1);const auto &d=layer.frames[0].draws[0];const auto &p=layer.images[d.image_id];
        require(d.x==0&&d.y==0&&p.width==512&&p.height==448);
        for(std::size_t at=0;at<p.rgb_coverage.size();++at)require(p.rgb_coverage[at]==static_cast<std::byte>(at%4==3?48:0));
      } else {
        require(!layer.frames[0].draws.empty());
        for(const auto &frame:layer.frames)require(frame.draws.size()==layer.frames[0].draws.size());
        for(const auto &d:layer.frames.back().draws) {
          const auto &pixels=layer.images[d.image_id].rgb_coverage;
          for(std::size_t at=3;at<pixels.size();at+=4)require(pixels[at]==std::byte{});
        }
        if(i<=4) {
          constexpr std::array<int,4> tops{204,124,140,172};constexpr std::array<unsigned,4> heights{40,200,168,104};
          require(layer.frames[0].draws.size()>=(i==1?7U:8U));const auto &d=layer.frames[0].draws[0];const auto &p=layer.images[d.image_id];
          require(d.x==96&&d.y==tops[i-1]&&p.width==320&&p.height==heights[i-1]);
          for(std::size_t at=0;at<p.rgb_coverage.size();++at)require(p.rgb_coverage[at]==static_cast<std::byte>(at%4==3?80:4));
        }
      }
    }
    require(dialog_draws==459);
    const Provenance audio_pass{PKind::generated,"compiler/rac-frontend-sound-resources-v1-effects1024-stereo",0,0,{}};
    std::optional<Provenance> sound_bank,sound_modules;
    for(unsigned i=0;i<5;++i) {
      const auto &r=resource(package,"frontend/audio/variant-"+std::to_string(i),"openrc.audio-clip");
      const auto bank=source_range(r,"rac1/frontend-sound-bank",image_bytes,true);
      const auto modules=source_range(r,"rac1/iop-module-bundle",image_bytes,true);
      require(bank.source_bytes<=16U*1024U*1024U&&modules.source_bytes<=16U*1024U*1024U&&
          (!sound_bank||same(*sound_bank,bank))&&(!sound_modules||same(*sound_modules,modules)));
      sound_bank=bank;sound_modules=modules;provenance(r,{image,boot,audio_pass,bank,modules});
      const auto clip=decode_audio_clip_v1(r.payload);
      require(clip.sample_rate==48000&&clip.channels==2&&!clip.samples.empty());
    }
    ambient_profile(package,image,boot,*sound_bank,*sound_modules);
    RacNewGameFlowResourcesV1 bindings;
    std::vector<const Resource*> flow_resources;std::array<Provenance,3> movies;
    std::optional<Provenance> card_source;
    for(unsigned i=0;i<3;++i) {
      const auto &card=resource(package,"new-game/loading-"+std::to_string(i),"openrc.loading-presentation");
      const auto &movie=resource(package,"new-game/movie-"+std::to_string(i),"openrc.media-clip");
      const auto cards=source_range(card,"disc/loading-cards",image_bytes,true);
      require(!card_source||same(*card_source,cards));card_source=cards;
      movies[i]=source_range(movie,"disc/new-game-movies",image_bytes,false);
      provenance(card,{image,boot,game_pass,cards});provenance(movie,{image,boot,game_pass,movies[i]});
      const auto loading=decode_loading_presentation_v1(card.payload);const auto &p=loading.library;
      require(p.canvas_width==512&&p.canvas_height==448&&p.updates_per_second==50&&p.coverage_denominator==128&&
          p.loop_begin==0&&p.frames.size()==600&&loading.fade_in_updates==32&&loading.fade_out_updates==16&&
          loading.bands.size()==(i==1?1U:2U));
      for(const auto &frame:p.frames) {
        require(frame.draws.size()==4);
        for(unsigned tile=0;tile<4;++tile) {
          const auto &d=frame.draws[tile];require(d.x==static_cast<int>(tile*128)&&d.y==0&&d.image_id==frame.draws[0].image_id&&
              p.images[d.image_id].width==128&&p.images[d.image_id].height==64);
        }
      }
      for(unsigned band=0;band<loading.bands.size();++band) {
        const auto &b=loading.bands[band];require(p.images[b.label_image].width==512&&p.images[b.label_image].height==64&&
            b.x==0&&b.y==(band?224:(i==1?192:178))&&b.reveal_update==(band?65U:0U)&&
            b.reveal_ramp_origin==(band?64U:0U)&&b.reveal_ramp_end==(band?96U:0U));
      }
      const auto media=decode_media_clip_v1(movie.payload);
      require(media.width==512&&media.height==416&&media.frame_rate_numerator==25&&media.frame_rate_denominator==1&&
          media.display_aspect_numerator==4&&media.display_aspect_denominator==3&&
          media.audio_sample_rate==48000&&media.audio_channels==2&&!media.audio.empty()&&!media.video.empty());
      bindings.loading_cards[i]={card.resource_id,card.type_id,card.payload_sha256};
      bindings.movies[i]={movie.resource_id,movie.type_id,movie.payload_sha256};
      flow_resources.push_back(&card);flow_resources.push_back(&movie);
      for(unsigned j=0;j<i;++j)require(movies[i].source_offset>=movies[j].source_offset+movies[j].source_bytes||
          movies[j].source_offset>=movies[i].source_offset+movies[i].source_bytes);
    }
    const std::array fade_durations{5U,2U,4U};
    for(unsigned i=0;i<fade_durations.size();++i) {
      const auto &fade=resource(package,"new-game/fade-"+std::to_string(fade_durations[i]),"openrc.frame-color-transfers");
      provenance(fade,{image,boot,game_pass});
      const auto value=decode_frame_color_transfer_sequence_v1(fade.payload);
      require(value==compile_rac_frontend_fade_v1(fade_durations[i],50));
      bindings.fades[i]={fade.resource_id,fade.type_id,fade.payload_sha256};flow_resources.push_back(&fade);
    }
    const auto &state_resource=resource(package,"frontend/session-state","openrc.session-state");
    const auto &input_resource=resource(package,"frontend/no-save-input","openrc.frontend-no-save-input");
    const auto &sequence_resource=resource(package,"frontend/new-game-sequence","openrc.frontend-sequence");
    provenance(state_resource,{image,boot,state_pass});provenance(input_resource,{image,boot,state_pass,reference(state_resource)});
    std::vector<Provenance> sequence_provenance{image,boot,state_pass,reference(state_resource)};
    for(const auto *r:flow_resources)sequence_provenance.push_back(reference(*r));provenance(sequence_resource,std::move(sequence_provenance));
    const auto limits=frontend_session_state_limits_v1();
    const auto initial=decode_session_state_initial_v1(state_resource.payload,{4U*1024U*1024U,limits});
    require(initial.schema.identity_key=="openrc.frontend-session/v1"&&initial.schema.buffers.size()==361&&initial.schema.views.size()==467);
    std::uint64_t total=0;std::map<std::string,std::uint64_t> reset_owners;
    for(const auto &b:initial.schema.buffers) {
      total+=b.byte_count;
      if(b.key.starts_with("progress/primary/")||b.key.starts_with("progress/level/")||b.key=="rac1.progress/encoded-level")
        reset_owners.emplace(b.key+"/bytes",b.byte_count);
    }
    require(total==61131&&reset_owners.size()==267);
    game::SessionStateV1 state(initial,limits);
    require(state_word(state,"frontend/config/dialog-duration")==25&&state_word(state,"frontend/config/dialog-delay")==8&&
        state_word(state,"frontend/config/title-fade-counter")==50&&state_word(state,"frontend/config/startup-video-selector")==1);
    constexpr std::array transition_fields{
      std::pair{"display/video-selector",4U},std::pair{"display/saved-video-selector",1U},
      std::pair{"loading/selector",2U},std::pair{"collision/query-flags",4U},
      std::pair{"audio/group5-volume",4U},std::pair{"audio/reverb-depth",4U},
      std::pair{"audio/reverb-mode",1U},std::pair{"audio/reverb-delay",1U},
      std::pair{"audio/reverb-feedback",1U},std::pair{"audio/reverb-flags",1U}};
    for(const auto &[key,width]:transition_fields) {
      const auto view=std::find_if(initial.schema.views.begin(),initial.schema.views.end(),
          [&](const auto &v){return v.key==std::string(key)+"/bytes";});
      require(view!=initial.schema.views.end()&&view->buffer_key==key&&view->value_type==SessionStateValueTypeV1::u8&&
          view->byte_offset==0&&view->byte_stride==1&&view->element_count==width);
      for(unsigned i=0;i<width;++i)require(state.read_u8(view->key,i)==0);
    }
    for(const auto &[key,offset]:std::array{std::pair{"loading/selector-first-unlocked/bytes",8U},
        std::pair{"loading/selector-second-unlocked/bytes",14U}}) {
      const auto view=std::find_if(initial.schema.views.begin(),initial.schema.views.end(),[&](const auto &v){return v.key==key;});
      require(view!=initial.schema.views.end()&&view->buffer_key=="progress/primary/field-14"&&
          view->value_type==SessionStateValueTypeV1::u8&&view->byte_offset==offset&&view->element_count==1&&view->byte_stride==1);
    }
    for(const auto& [key,count]:std::array{std::pair{"rac1.level/selector-cache",17U},
        std::pair{"rac1.level/alternate-bits",256U},std::pair{"rac1.level/saved-state",3168U}}) {
      require(state.buffer_bytes(key).size()==count);
      const auto view=std::ranges::find(initial.schema.views,std::string(key)+"/bytes",&SessionStateViewV1::key);
      require(view!=initial.schema.views.end()&&*view==SessionStateViewV1{std::string(key)+"/bytes",key,
          SessionStateValueTypeV1::u8,0U,count,1U});
    }
    for(const auto& expected:std::array<SessionStateViewV1,3>{{
        {"rac1.level/selector-cache/selectors","rac1.level/selector-cache",SessionStateValueTypeV1::u8,1U,16U,1U},
        {"rac1.level/alternate-bits/words","rac1.level/alternate-bits",SessionStateValueTypeV1::u32,0U,64U,4U},
        {"rac1.level/saved-state/suppression","rac1.level/saved-state",SessionStateValueTypeV1::u8,0x454U,2047U,1U}}}) {
      const auto view=std::ranges::find(initial.schema.views,expected.key,&SessionStateViewV1::key);
      require(view!=initial.schema.views.end()&&*view==expected);
    }
    const auto& installation_resource=resource(package,"new-game/level-installation","openrc.state-installation");
    const auto installation_source=source_range(installation_resource,"rac1/initial-level-overlay",image_bytes,false);
    require(installation_source.source_offset==3862566912ULL&&installation_source.source_bytes==1651060U);
    provenance(installation_resource,{image,boot,
        {PKind::generated,"compiler/rac-initial-level-installation-v1",0U,0U,{}},
        installation_source,reference(state_resource)});
    const auto installation=decode_state_installation_v1(installation_resource.payload);
    require(installation.level_id==0U&&installation.state_schema_sha256==state.schema_sha256()&&installation.writes.size()==3457U);
    std::size_t install_index=0U;
    // Semantic overlay bindings are compiler-owned. In particular these are
    // not the old frontend addresses, and the saved display byte is retired.
    for(const auto& [key,count]:std::array{std::pair{"session/transition-requested/bytes",4U},
        std::pair{"session/target-level/bytes",4U},std::pair{"session/level-change-requested/bytes",4U},
        std::pair{"rac1.level/selector-cache/bytes",17U},std::pair{"collision/query-flags/bytes",4U},
        std::pair{"rac1.level/alternate-bits/bytes",256U},std::pair{"rac1.level/saved-state/bytes",3168U}})
      for(unsigned byte=0;byte<count;++byte) {
        const auto& write=installation.writes.at(install_index++);
        require(write.view_key==key&&write.element_index==byte&&write.value_type==SessionStateValueTypeV1::u8&&write.value_bits==0U);
      }
    auto installed=state;
    execute_state_installation_v1(installation,0U,installed,state.revision());
    const auto plan=decode_frontend_no_save_plan_v1(input_resource.payload);validate_frontend_no_save_plan_v1(plan,state);
    require(plan.save_result_screen_tokens==std::array<std::uint32_t,2>{3,4});
    constexpr std::array<const char*,kFrontendNoSaveFieldCountV1> field_keys{
      "frontend/no-save/focused","frontend/no-save/node-flags","frontend/no-save/node-phase","frontend/no-save/node-selection",
      "frontend/no-save/sound-object","frontend/previous-screen","frontend/previous-result","frontend/no-save/parent-screen",
      "frontend/cancel-guard","frontend/pending-save","frontend/readiness","frontend/card-mode","frontend/card-status",
      "frontend/card-result","frontend/card-type","input/global-pressed","input/pressed","input/repeated",
      "frontend/saved-selection","frontend/flags","progress/primary/field-36","progress/primary/field-35",
      "progress/primary/field-34","frontend/requested-screen","frontend/save-requested","rac1.progress/encoded-level",
      "session/target-level","session/level-change-requested","session/transition-requested","session/entry-requested"};
    for(unsigned i=0;i<field_keys.size();++i)require(plan.fields[i].view_key==std::string(field_keys[i])+"/bytes"&&plan.fields[i].first_element==0);
    std::map<std::string,std::uint64_t> reset_counts;
    for(const auto &w:plan.reset_writes) {
      require(w.value_type==SessionStateValueTypeV1::u8&&reset_owners.contains(w.view_key)&&w.element_index==reset_counts[w.view_key]++);
    }
    require(reset_counts==reset_owners);
    // Recreate only the static compiler continuation with these admitted
    // resource digests. No input/reset is executed and no live value guessed.
    const std::array<RacFrontendStateBindingV1,5> state_bindings{{
      {0x13e15a,2,"session/entry-requested/bytes",0},{0x15f6e4,4,"session/target-level/bytes",0},
      {0x15ee84,4,"rac1.progress/encoded-level/bytes",0},{0x15ef48,2,"session/load-count-a/bytes",0},
      {0x15ef4a,2,"session/load-count-b/bytes",0}}};
    const auto expected=compile_rac_new_game_continuation_v1({},bindings,state_bindings,initial.schema,limits);
    require(expected.sequence.has_value()&&sequence_resource.payload==encode_frontend_sequence_v1(*expected.sequence));
    const auto decoded=decode_frontend_sequence_v1(sequence_resource.payload);
    require(decoded.state_schema_sha256==state.schema_sha256()&&decoded.cues.size()==39&&decoded.resources.size()==9);
    return true;
  } catch(const std::exception &) {return false;}
}
} // namespace openrc
