#pragma once

// Structural publication fixture only. Deliberately synthetic triangles,
// glyph pixels,PCM and state bytes are not source asset or gameplay evidence.
const std::vector<openrc::LevelPackageResourceV1> &frontend_flow_fixture() {
  static const auto resources=[] {
    using namespace openrc;
    const auto image=source_provenance(LevelPackageProvenanceKindV1::iso_range,"rac1/disc-image",kSourceImageBytes,kSourceImageSha256);
    const auto boot=source_provenance(LevelPackageProvenanceKindV1::prepared_resource,"rac1/boot-executable",kBootExecutableBytes,kBootExecutableSha256);
    const LevelPackageProvenanceV1 wad{LevelPackageProvenanceKindV1::iso_range,"rac1/global-toc/14e8",
      UINT64_C(14602)*2048U,2101U*2048U,digest_of("synthetic-frontend-wad")};
    const std::vector<LevelPackageProvenanceV1> menu_provenance{image,boot,wad,generated_provenance("compiler/rac-frontend-menu-resources-v1")};
    std::vector<LevelPackageResourceV1> out;
    const auto add=[&](const std::string &id,const char *type,std::vector<std::byte> bytes,
        const std::vector<LevelPackageProvenanceV1> &p) {out.push_back(make_resource(id,type,1,std::move(bytes),p));};
    auto library=make_actor_library(1);library.rigs[0].semantic_key="frontend/menu/rig";
    library.models[0].semantic_key="frontend/menu/model";library.models[0].rig_key="frontend/menu/rig";
    auto actor_bytes=encode_actor_library_v1(library,rac_frontend_menu_actor_io_limits_v1());
    library=decode_actor_library_v1(actor_bytes,rac_frontend_menu_actor_io_limits_v1());
    ActorAnimationBankV1 bank;
    for(unsigned i=0;i<14;++i) {
      ActorAnimationClipV1 clip;clip.id=i;clip.semantic_key="frontend/menu/slot/"+std::string(i<10?"0":"")+std::to_string(i);
      clip.rig_key=library.rigs[0].semantic_key;clip.rig_content_sha256=library.rigs[0].content_sha256;clip.source_updates_per_second=50;
      clip.frames.resize(1);clip.frames[0].phase_rate=0.5F;clip.frames[0].joint_poses.resize(1);bank.clips.push_back(std::move(clip));
    }
    auto animation_bytes=encode_actor_animation_bank_v1(bank,rac_frontend_menu_animation_io_limits_v1());
    SceneTimelineV1 timeline;timeline.updates_per_second=50;timeline.display_aspect_numerator=512;timeline.display_aspect_denominator=512;
    timeline.actor_library_sha256=prepared_content_sha256_v1(actor_bytes);timeline.actor_animation_sha256=prepared_content_sha256_v1(animation_bytes);
    timeline.actors.resize(14);timeline.samples.resize(13);
    for(auto &sample:timeline.samples) {
      sample.camera.tangent_half_horizontal=0.63F;sample.camera.tangent_half_vertical=0.48F;
      sample.camera.near_plane=0.1F;sample.camera.far_plane=1000;sample.actors.resize(14);
      for(unsigned i=0;i<14;++i){sample.actors[i].clip_index=i;sample.actors[i].enabled=i!=6;}
    }
    add("frontend/menu/actors","openrc.actor-library",std::move(actor_bytes),menu_provenance);
    add("frontend/menu/animation","openrc.actor-animation-bank",std::move(animation_bytes),menu_provenance);
    auto p=menu_provenance;p.push_back(prepared_resource_provenance(out[0]));p.push_back(prepared_resource_provenance(out[1]));
    add("frontend/menu/timeline","openrc.scene-timeline",encode_scene_timeline_v1(timeline),p);
    const auto overlay=[] {
      ScreenOverlayV1 result;result.canvas_width=512;result.canvas_height=448;result.updates_per_second=50;result.coverage_denominator=128;return result;
    };
    const auto raster=[](unsigned width,unsigned height,unsigned rgb,unsigned coverage) {
      ScreenOverlayImageV1 image{width,height,std::vector<std::byte>(std::size_t(width)*height*4U)};
      for(std::size_t i=0;i<image.rgb_coverage.size();++i)image.rgb_coverage[i]=static_cast<std::byte>(i%4==3?coverage:rgb);return image;
    };
    auto lists=overlay();lists.images.push_back(raster(1,1,255,128));lists.frames.resize(20);
    for(unsigned i=12;i<20;++i)lists.frames[i].draws={{0,100,100},{0,100,150},{0,100,200}};
    add("frontend/menu/lists","openrc.screen-overlay",encode_screen_overlay_v1(lists),menu_provenance);
    constexpr std::array<const char*,9> ids{"frontend/dialog/backdrop","frontend/dialog/body/empty","frontend/dialog/body/absent",
      "frontend/dialog/body/without-game","frontend/dialog/body/unavailable","frontend/dialog/prompts/new","frontend/dialog/prompts/existing",
      "frontend/dialog/prompts/without-game","frontend/dialog/prompts/unavailable"};
    constexpr std::array<unsigned,9> draws{1,7,200,128,64,15,15,15,14};
    for(unsigned i=0;i<9;++i) {
      auto layer=overlay();
      if(!i) {layer.images.push_back(raster(512,448,0,48));layer.frames.resize(1);layer.frames[0].draws={{0,0,0}};}
      else {
        constexpr std::array<int,4> tops{204,124,140,172};constexpr std::array<unsigned,4> heights{40,200,168,104};
        layer.images.push_back(raster(i<=4?320:1,i<=4?heights[i-1]:1,4,i<=4?80:128));
        layer.images.push_back(raster(i<=4?320:1,i<=4?heights[i-1]:1,4,0));
        layer.images.push_back(raster(1,1,255,128));layer.images.push_back(raster(1,1,255,0));
        layer.frames.resize(26);
        for(unsigned f=0;f<26;++f) {
          layer.frames[f].draws.push_back({f==25?1U:0U,i<=4?96:256,i<=4?tops[i-1]:200});
          for(unsigned d=1;d<draws[i];++d)layer.frames[f].draws.push_back({f==25?3U:2U,200,200});
        }
      }
      add(ids[i],"openrc.screen-overlay",encode_screen_overlay_v1(layer,rac_frontend_menu_overlay_limits_v1()),menu_provenance);
    }
    for(unsigned i=0;i<5;++i) {
      const AudioClipV1 clip{48000,2,{static_cast<std::int16_t>(i+1),static_cast<std::int16_t>(-(int(i)+1))}};
      p={image,boot,generated_provenance("compiler/rac-frontend-sound-resources-v1-effects1024-stereo"),
        {LevelPackageProvenanceKindV1::iso_range,"rac1/frontend-sound-bank",25000ULL*2048,4096,digest_of("synthetic-menu-bank")},
        {LevelPackageProvenanceKindV1::iso_range,"rac1/iop-module-bundle",26000ULL*2048,4096,digest_of("synthetic-menu-modules")}};
      add("frontend/audio/variant-"+std::to_string(i),"openrc.audio-clip",encode_audio_clip_v1(clip),p);
    }
    RacNewGameFlowResourcesV1 bindings;
    std::vector<LevelPackageProvenanceV1> refs;
    for(unsigned i=0;i<3;++i) {
      LoadingPresentationV1 loading;loading.library=overlay();loading.library.loop_begin=0;loading.library.frames.resize(600);
      loading.library.images={raster(128,64,16,128),raster(512,64,255,128)};
      for(auto &frame:loading.library.frames)for(unsigned tile=0;tile<4;++tile)frame.draws.push_back({0,static_cast<int>(tile*128),0});
      loading.fade_in_updates=32;loading.fade_out_updates=16;loading.bands.push_back({1,0,i==1?192:178,0,0,0});
      if(i!=1)loading.bands.push_back({1,0,224,65,64,96});
      p={image,boot,generated_provenance("compiler/rac-new-game-resources-v1"),
        {LevelPackageProvenanceKindV1::iso_range,"disc/loading-cards",20000ULL*2048,2048,digest_of("synthetic-loading-wad")}};
      add("new-game/loading-"+std::to_string(i),"openrc.loading-presentation",encode_loading_presentation_v1(loading),p);
      refs.push_back(prepared_resource_provenance(out.back()));bindings.loading_cards[i]={out.back().resource_id,out.back().type_id,out.back().payload_sha256};
      MediaClipV1 clip;clip.width=512;clip.height=416;clip.frame_rate_numerator=25;clip.frame_rate_denominator=1;
      clip.display_aspect_numerator=4;clip.display_aspect_denominator=3;clip.audio_sample_rate=48000;clip.audio_channels=2;
      clip.audio_start_time=0;clip.audio={0,0};clip.video={{0,0,{std::byte{0},std::byte{0},std::byte{1},std::byte{0xb3},
          std::byte{0x20},std::byte{1},std::byte{0xa0},std::byte{0x13}}}};
      p={image,boot,generated_provenance("compiler/rac-new-game-resources-v1"),
        {LevelPackageProvenanceKindV1::iso_range,"disc/new-game-movies",(30000ULL+i*1000ULL)*2048,2048,digest_of("synthetic-movie-"+std::to_string(i))}};
      add("new-game/movie-"+std::to_string(i),"openrc.media-clip",encode_media_clip_v1(clip),p);
      refs.push_back(prepared_resource_provenance(out.back()));bindings.movies[i]={out.back().resource_id,out.back().type_id,out.back().payload_sha256};
    }
    const std::array fade_durations{5U,2U,4U};
    for(unsigned i=0;i<fade_durations.size();++i) {
      const auto value=compile_rac_frontend_fade_v1(fade_durations[i],50);
      p={image,boot,generated_provenance("compiler/rac-new-game-resources-v1")};
      add("new-game/fade-"+std::to_string(fade_durations[i]),"openrc.frame-color-transfers",encode_frame_color_transfer_sequence_v1(value),p);
      refs.push_back(prepared_resource_provenance(out.back()));bindings.fades[i]={out.back().resource_id,out.back().type_id,out.back().payload_sha256};
    }
    SessionStateInitialV1 initial;initial.schema.identity_key="openrc.frontend-session/v1";
    const auto owner=[&](const std::string &key,unsigned size=4,unsigned value=0) {
      if(std::any_of(initial.schema.buffers.begin(),initial.schema.buffers.end(),[&](const auto &b){return b.key==key;}))return;
      initial.schema.buffers.push_back({key,size});initial.schema.views.push_back({key+"/bytes",key,SessionStateValueTypeV1::u8,0,size,1});
      std::vector<std::byte> bytes(size);for(unsigned i=0;i<std::min(4U,size);++i)bytes[i]=static_cast<std::byte>(value>>(8*i));
      initial.buffers.push_back({key,std::move(bytes)});
    };
    for(unsigned i=0;i<267;++i)owner(i?"progress/primary/field-"+std::to_string(i):"rac1.progress/encoded-level",i==14?20:4);
    constexpr std::array<const char*,kFrontendNoSaveFieldCountV1> fields{
      "frontend/no-save/focused","frontend/no-save/node-flags","frontend/no-save/node-phase","frontend/no-save/node-selection",
      "frontend/no-save/sound-object","frontend/previous-screen","frontend/previous-result","frontend/no-save/parent-screen",
      "frontend/cancel-guard","frontend/pending-save","frontend/readiness","frontend/card-mode","frontend/card-status",
      "frontend/card-result","frontend/card-type","input/global-pressed","input/pressed","input/repeated",
      "frontend/saved-selection","frontend/flags","progress/primary/field-36","progress/primary/field-35",
      "progress/primary/field-34","frontend/requested-screen","frontend/save-requested","rac1.progress/encoded-level",
      "session/target-level","session/level-change-requested","session/transition-requested","session/entry-requested"};
    for(const auto *key:fields)owner(key);
    owner("frontend/config/dialog-duration",4,25);owner("frontend/config/dialog-delay",4,8);owner("frontend/config/title-fade-counter",4,50);
    owner("frontend/config/startup-video-selector",4,1);
    for(const auto &[key,width]:std::array{
        std::pair{"display/video-selector",4U},std::pair{"display/saved-video-selector",1U},
        std::pair{"loading/selector",2U},std::pair{"collision/query-flags",4U},
        std::pair{"audio/group5-volume",4U},std::pair{"audio/reverb-depth",4U},
        std::pair{"audio/reverb-mode",1U},std::pair{"audio/reverb-delay",1U},
        std::pair{"audio/reverb-feedback",1U},std::pair{"audio/reverb-flags",1U}})owner(key,width);
    initial.schema.views.push_back({"loading/selector-first-unlocked/bytes","progress/primary/field-14",SessionStateValueTypeV1::u8,8,1,1});
    initial.schema.views.push_back({"loading/selector-second-unlocked/bytes","progress/primary/field-14",SessionStateValueTypeV1::u8,14,1,1});
    owner("session/load-count-a",2);owner("session/load-count-b",2);
    while(initial.schema.buffers.size()<358)owner("fixture/frontend/owner-"+std::to_string(initial.schema.buffers.size()));
    std::uint64_t bytes=0;for(const auto &b:initial.schema.buffers)bytes+=b.byte_count;
    auto &padding=initial.schema.buffers[266];padding.byte_count+=57690-bytes;
    initial.schema.views[266].element_count=padding.byte_count;initial.buffers[266].bytes.resize(static_cast<std::size_t>(padding.byte_count));
    while(initial.schema.views.size()<461)initial.schema.views.push_back({"fixture/alias-"+std::to_string(initial.schema.views.size()),
      "rac1.progress/encoded-level",SessionStateValueTypeV1::u8,0,4,1});
    owner("rac1.level/selector-cache",17);owner("rac1.level/alternate-bits",256);owner("rac1.level/saved-state",3168);
    initial.schema.views.push_back({"rac1.level/selector-cache/selectors","rac1.level/selector-cache",SessionStateValueTypeV1::u8,1,16,1});
    initial.schema.views.push_back({"rac1.level/alternate-bits/words","rac1.level/alternate-bits",SessionStateValueTypeV1::u32,0,64,4});
    initial.schema.views.push_back({"rac1.level/saved-state/suppression","rac1.level/saved-state",SessionStateValueTypeV1::u8,0x454,2047,1});
    const auto limits=frontend_session_state_limits_v1();initial=canonicalize_session_state_initial_v1(initial,limits);
    FrontendNoSavePlanV1 plan;plan.state_schema_sha256=hash_session_state_schema_v1(initial.schema,limits);plan.save_result_screen_tokens={3,4};
    for(unsigned i=0;i<fields.size();++i)plan.fields[i]={std::string(fields[i])+"/bytes",0};
    for(const auto &b:initial.schema.buffers)if(b.key.starts_with("progress/")||b.key=="rac1.progress/encoded-level")
      for(std::uint64_t at=0;at<b.byte_count;++at)plan.reset_writes.push_back({b.key+"/bytes",at,SessionStateValueTypeV1::u8,0});
    p={image,boot,generated_provenance("openrc.rac-frontend-state-compile.v1")};
    add("frontend/session-state","openrc.session-state",encode_session_state_initial_v1(initial,{4U*1024U*1024U,limits}),p);
    const auto state_reference=prepared_resource_provenance(out.back());
    p.push_back(prepared_resource_provenance(out.back()));
    add("frontend/no-save-input","openrc.frontend-no-save-input",encode_frontend_no_save_plan_v1(plan),p);
    const std::array<RacFrontendStateBindingV1,5> state_bindings{{
      {0x13e15a,2,"session/entry-requested/bytes",0},{0x15f6e4,4,"session/target-level/bytes",0},
      {0x15ee84,4,"rac1.progress/encoded-level/bytes",0},{0x15ef48,2,"session/load-count-a/bytes",0},{0x15ef4a,2,"session/load-count-b/bytes",0}}};
    const auto sequence=compile_rac_new_game_continuation_v1({},bindings,state_bindings,initial.schema,limits);
    p.insert(p.end(),refs.begin(),refs.end());
    add("frontend/new-game-sequence","openrc.frontend-sequence",encode_frontend_sequence_v1(*sequence.sequence),p);
    StateInstallationV1 installation;installation.state_schema_sha256=plan.state_schema_sha256;
    for(const auto& [key,count]:std::array{std::pair{"session/transition-requested/bytes",4U},
        std::pair{"session/target-level/bytes",4U},std::pair{"session/level-change-requested/bytes",4U},
        std::pair{"rac1.level/selector-cache/bytes",17U},std::pair{"collision/query-flags/bytes",4U},
        std::pair{"rac1.level/alternate-bits/bytes",256U},std::pair{"rac1.level/saved-state/bytes",3168U}})
      for(unsigned byte=0;byte<count;++byte)installation.writes.push_back({key,byte,SessionStateValueTypeV1::u8,0});
    p={image,boot,state_reference,generated_provenance("compiler/rac-initial-level-installation-v1"),
      {LevelPackageProvenanceKindV1::iso_range,"rac1/initial-level-overlay",3862566912ULL,1651060U,digest_of("synthetic-installed-state")}};
    add("new-game/level-installation","openrc.state-installation",encode_state_installation_v1(installation),p);
    return out;
  }();
  return resources;
}

// Numeric shape fixture only; these samples, curves and graphs are synthetic.
void append_frontend_ambient_fixture(openrc::LevelPackageV1& package) {
  using namespace openrc;
  const auto locate=[&](const std::string& id)->const LevelPackageResourceV1* {
    const auto found=std::find_if(package.resources.begin(),package.resources.end(),[&](const auto& r){return r.resource_id==id;});
    return found==package.resources.end()?nullptr:&*found;
  };
  auto sources=locate("frontend/audio/variant-0")->provenance;
  for(auto& p:sources)if(p.kind==LevelPackageProvenanceKindV1::generated)
    p.source_locator="compiler/rac-frontend-ambient-resources-v1-effects1024-stereo";
  const auto add=[&](const std::string& id,const char* type,std::vector<std::byte> bytes,
      const std::vector<LevelPackageProvenanceV1>& provenance) {
    package.resources.push_back(make_resource(id,type,1,std::move(bytes),provenance));
  };
  AudioProgramBankV1 programs;programs.ticks_per_second=240;programs.modulation_tick_divisor=2;
  programs.random.words.assign(250,1);programs.random.forward_tap=103;
  AudioVoiceBankV1 bank;bank.program_resource_id="frontend/audio/ambient-program";bank.observation={200,0,4,1,1};
  for(unsigned i=0;i<11;++i)bank.phase_curves.push_back({-32768,std::vector<std::uint16_t>(65536,4096)});
  for(unsigned i=0;i<13;++i) {
    const auto id="frontend/audio/ambient-stream-"+std::to_string(i);bank.stream_resource_ids.push_back(id);
    AudioStreamV1 stream;stream.output_sample_rate=48000;stream.phase_denominator=4096;
    stream.coefficient_denominator=32768;stream.gain_denominator=32768;
    for(auto& row:stream.coefficients)row={32767,0,0,0};stream.input_samples.assign(16,1000);
    if(i==3 || i==4){stream.repeat_begin=4;stream.repeat_end=16;}
    add(id,"openrc.audio-stream",encode_audio_stream_v1(stream),sources);
  }
  for(unsigned i=0;i<4;++i) {
    const auto id="frontend/audio/ambient-gain-"+std::to_string(i);bank.gain_resource_ids.push_back(id);
    AudioGainTableV1 gain;gain.state_count=6;gain.level_count=128;gain.pan_count=360;gain.gain_denominator=32768;
    gain.cells.assign(6U*128U*360U,{{32768,32768},0});
    add(id,"openrc.audio-gain-table",encode_audio_gain_table_v1(gain),sources);
  }
  constexpr std::array<unsigned,5> keys{2,3,8,4,9},counts{12,1,1,11,1};
  for(unsigned p=0;p<keys.size();++p) {
    AudioProgramV1 program;program.key=keys[p];program.voice_count=counts[p];
    for(unsigned voice=0;voice<counts[p];++voice) {
      program.nodes.push_back({0,voice+1,AudioProgramVoiceV1{voice}});
      const auto ordinal=static_cast<unsigned>(bank.bindings.size());
      AudioVoiceBindingV1 binding;binding.program_key=keys[p];binding.voice_index=voice;
      binding.stream_index=ordinal%13;binding.gain_table_index=ordinal%4;binding.phase_curve_index=ordinal%11;
      binding.level.maximum=127;binding.pan.maximum=359;binding.pan.wrap=true;
      binding.phase.minimum=-32768;binding.phase.maximum=32767;
      binding.envelope.maximum_level=32767;binding.envelope.counter_period=32768;
      for(auto& stage:binding.envelope.stages) {
        stage.counter_increment=32768;stage.slow_counter_increment=32768;stage.delta_denominator=1;
      }
      binding.envelope.stages[0].delta_bias=32767;binding.envelope.stages[0].target=32767;
      binding.envelope.stages[1].decreasing=true;binding.envelope.stages[1].target=32767;
      binding.envelope.stages[3].decreasing=true;binding.envelope.stages[3].delta_bias=-32767;
      if(binding.stream_index!=3 && binding.stream_index!=4)binding.read_ahead=AudioReadAheadV1{16,4,12,4};
      bank.bindings.push_back(std::move(binding));
    }
    program.nodes.push_back({0,counts[p]+1,AudioProgramWaitOwnedV1{}});
    program.nodes.push_back({0,counts[p]+2,AudioProgramStopSectionV1{}});
    program.nodes.push_back({0,audio_program_end_v1,AudioProgramReleaseV1{}});
    programs.programs.push_back(std::move(program));
  }
  add(bank.program_resource_id,"openrc.audio-program",encode_audio_program_bank_v1(programs),sources);
  auto references=sources;references.push_back(prepared_resource_provenance(*locate(bank.program_resource_id)));
  for(const auto* ids:{&bank.stream_resource_ids,&bank.gain_resource_ids})for(const auto& id:*ids)
    references.push_back(prepared_resource_provenance(*locate(id)));
  add("frontend/audio/ambient-bank","openrc.audio-voice-bank",encode_audio_voice_bank_v1(bank),references);
  AudioProgramCuesV1 cues{"frontend/audio/ambient-bank","frontend/background/timeline",50,
    {{0,2,0,99999999},{1,3,0,99999999},{2,8,0,99999999},{3,4,740,1240},{4,9,276,300}}};
  references=sources;references.push_back(prepared_resource_provenance(*locate(cues.voice_bank_resource_id)));
  if(const auto* timeline=locate(cues.timeline_resource_id))references.push_back(prepared_resource_provenance(*timeline));
  add("frontend/audio/ambient-cues","openrc.audio-program-cues",encode_audio_program_cues_v1(cues),references);
}
