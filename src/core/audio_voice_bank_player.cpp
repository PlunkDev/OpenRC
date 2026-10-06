#include "openrc/audio_voice_bank_player.hpp"

#include <algorithm>
#include <utility>

namespace openrc {
namespace {
void require(bool condition,const char* message) {if(!condition)throw AudioVoiceBankError(message);}
using Scalars=std::array<std::int32_t,audio_program_scalar_count_v1>;
std::uint32_t sample_rate(const std::vector<AudioVoiceBankStreamV1>& streams) {
    require(!streams.empty(),"Audio bank has no admitted streams");return streams.front().stream.output_sample_rate;
}
void qualify_immediate_stop(const AudioProgramV1& program,const AudioProgramLimitsV1& limits) {
    const auto marker=std::find_if(program.nodes.begin(),program.nodes.end(),[](const auto& node){
        return std::holds_alternative<AudioProgramStopSectionV1>(node.action);});
    if(marker==program.nodes.end() || marker->next==audio_program_end_v1)return;
    struct Frame {std::uint32_t node=0;std::size_t next_child=0;std::uint64_t actions=0,events=0;};
    std::vector<unsigned char> colors(program.nodes.size());
    std::vector<std::uint64_t> actions(program.nodes.size()),events(program.nodes.size());
    std::vector<Frame> pending{{marker->next}};
    while(!pending.empty()) {
        auto& current=pending.back();const auto& node=program.nodes[current.node];
        if(colors[current.node]==0) {
            require(node.delay_ticks==0 && !std::holds_alternative<AudioProgramVoiceV1>(node.action) &&
                !std::holds_alternative<AudioProgramWaitOwnedV1>(node.action),
                "Audio bank stop path must be immediate and cannot admit or wait for voices");
            if(const auto* wait=std::get_if<AudioProgramRandomWaitV1>(&node.action))
                require(wait->inclusive_max==0,"Audio bank stop path cannot contain a random delay");
            colors[current.node]=1;
        }
        const auto* branch=std::get_if<AudioProgramRandomBranchV1>(&node.action);
        const auto* comparison=std::get_if<AudioProgramCompareV1>(&node.action);
        const auto count=std::holds_alternative<AudioProgramStopSectionV1>(node.action)?0U:
            branch?branch->successors.size():comparison?2U:1U;
        if(current.next_child<count) {
            const auto index=current.next_child++;
            const auto child=branch?branch->successors[index]:comparison&&index==1?comparison->true_next:node.next;
            if(child==audio_program_end_v1)continue;
            require(colors[child]!=1,"Audio bank stop path contains a cycle");
            if(colors[child]==0)pending.push_back({child});
            else {current.actions=std::max(current.actions,actions[child]);current.events=std::max(current.events,events[child]);}
            continue;
        }
        const auto index=current.node;
        actions[index]=current.actions+1;
        events[index]=current.events+std::holds_alternative<AudioProgramReleaseV1>(node.action);
        require(actions[index]<=limits.max_actions_per_tick && events[index]<=limits.max_events,
            "Audio bank stop path exceeds the admitted scheduler bounds");
        colors[index]=2;pending.pop_back();
        if(!pending.empty()) {
            pending.back().actions=std::max(pending.back().actions,actions[index]);
            pending.back().events=std::max(pending.back().events,events[index]);
        }
    }
}
}
struct AudioVoiceBankPlayerV1::Implementation {
    struct Voice {
        std::uint64_t instance=0, token=0;
        std::size_t binding=0;
        Scalars snapshot{};
        std::unique_ptr<AudioVoicePlayerV1> pending;
        std::optional<AudioGainTablePlayerV1> gain;
        AudioVoiceControlV1 control;
        std::int32_t level=0,pan=0;
        std::uint32_t zero_remaining=0;
        bool attached=true,release_pending=false;
    };
    AudioVoiceBankV1 bank;
    AudioProgramBankV1 definitions;
    std::vector<AudioVoiceBankStreamV1> streams;
    std::vector<AudioGainTableResourceV1> gains;
    AudioProgramSchedulerV1 scheduler;
    AudioVoiceMixerV1 mixer;
    AudioVoiceMixerLimitsV1 limits;
    std::vector<Voice> voices;
    std::vector<std::uint64_t> instances;
    std::uint64_t frame=0,started=0,completed=0,retired=0;
    bool stopped=false;

    Implementation(AudioVoiceBankV1 b,std::string program_id,AudioProgramBankV1 p,
        std::vector<AudioVoiceBankStreamV1> s,std::vector<AudioVoiceBankGainV1> g,
        std::uint64_t first,AudioVoiceMixerLimitsV1 ml,AudioProgramLimitsV1 pl):
        bank(std::move(b)),definitions(std::move(p)),streams(std::move(s)),
        scheduler(definitions,pl),mixer(sample_rate(streams),ml),limits(ml),frame(first) {
        validate_audio_voice_bank_v1(bank);
        for(const auto& program:definitions.programs)qualify_immediate_stop(program,pl);
        require(program_id==bank.program_resource_id && streams.size()==bank.stream_resource_ids.size() &&
            g.size()==bank.gain_resource_ids.size(),"Audio bank resource handles differ from admitted owners");
        const auto rate=sample_rate(streams);
        require(std::uint64_t{definitions.ticks_per_second}*bank.observation.frame_stride==rate,
            "Audio bank program and PCM observation clocks differ");
        for(std::size_t i=0;i<streams.size();++i) {
            require(streams[i].resource_id==bank.stream_resource_ids[i] && streams[i].stream.output_sample_rate==rate,
                "Audio bank stream handle or sample rate differs");
            validate_audio_stream_v1(streams[i].stream);
        }
        gains.reserve(g.size());
        for(std::size_t i=0;i<g.size();++i) {
            require(g[i].resource_id==bank.gain_resource_ids[i],"Audio bank gain resource handle differs");
            gains.emplace_back(std::move(g[i].table));
        }
        for(const auto& binding:bank.bindings) {
            const auto program=std::find_if(definitions.programs.begin(),definitions.programs.end(),
                [&](const auto& item){return item.key==binding.program_key;});
            require(program!=definitions.programs.end() && binding.voice_index<program->voice_count,
                "Audio voice binding does not name an admitted program voice");
            const auto& table=gains[binding.gain_table_index].definition();
            const auto& stream=streams[binding.stream_index].stream;
            require(std::uint64_t{static_cast<std::uint32_t>(binding.level.maximum)}<table.level_count &&
                std::uint64_t{static_cast<std::uint32_t>(binding.pan.maximum)}<table.pan_count &&
                table.gain_denominator==stream.gain_denominator,
                "Audio voice binding leaves its admitted gain table domain");
            AudioVoicePlayerV1 check(stream,binding.envelope,binding.read_ahead);
            for(const auto increment:bank.phase_curves[binding.phase_curve_index].increments)
                check.validate_control({increment,{0,0}});
        }
        for(const auto& program:definitions.programs)for(std::uint32_t index=0;index<program.voice_count;++index)
            require(std::ranges::any_of(bank.bindings,[&](const auto& b){return b.program_key==program.key&&b.voice_index==index;}),
                "Audio program has a voice without an admitted binding");
        voices.reserve(ml.max_voices);instances.reserve(pl.max_instances);
        const auto& o=bank.observation;
        const auto elapsed=first>o.first_frame_offset?1U+(first-o.first_frame_offset-1U)/o.frame_stride:0U;
        scheduler.advance_idle_to(elapsed);
    }
    void controls(Voice& voice,const Scalars& live,bool initial) {
        const auto& binding=bank.bindings[voice.binding];
        const auto level=evaluate_audio_scalar_projection_v1(binding.level,voice.snapshot,live);
        const auto pan=evaluate_audio_scalar_projection_v1(binding.pan,voice.snapshot,live);
        if(initial||level!=voice.level||pan!=voice.pan) {
            voice.control.channel_gains=voice.gain->step(static_cast<std::uint32_t>(level),static_cast<std::uint32_t>(pan));
            voice.level=level;voice.pan=pan;
        }
        const auto phase=evaluate_audio_scalar_projection_v1(binding.phase,voice.snapshot,live);
        const auto& curve=bank.phase_curves[binding.phase_curve_index];
        voice.control.phase_increment=curve.increments[static_cast<std::size_t>(std::int64_t{phase}-curve.first_control)];
        if(voice.pending)voice.pending->validate_control(voice.control);
        else mixer.set_control(voice.token,voice.control);
    }
    void detach(Voice& voice,std::uint32_t zero_count) {
        voice.attached=false;voice.release_pending=true;voice.zero_remaining=zero_count;
    }
    void consume_events() {
        for(const auto& event:scheduler.take_events()) {
            if(event.kind==AudioProgramEventKindV1::release_owned) {
                for(auto& voice:voices)if(voice.instance==event.instance&&voice.attached)
                    detach(voice,bank.observation.zero_observations_after_release);
                continue;
            }
            require(voices.size()<limits.max_voices,"Audio bank physical and pending voice capacity is exhausted");
            const auto found=std::find_if(bank.bindings.begin(),bank.bindings.end(),[&](const auto& binding){
                return binding.program_key==event.program_key&&binding.voice_index==event.voice_index;});
            require(found!=bank.bindings.end(),"Audio program emitted an unresolved voice");
            Voice voice;voice.instance=event.instance;voice.binding=static_cast<std::size_t>(found-bank.bindings.begin());
            voice.snapshot=event.scalars;voice.zero_remaining=bank.observation.zero_observations_before_completion;
            voice.pending=std::make_unique<AudioVoicePlayerV1>(streams[found->stream_index].stream,found->envelope,found->read_ahead);
            voice.gain.emplace(gains[found->gain_table_index]);controls(voice,event.scalars,true);
            voices.push_back(std::move(voice));
        }
    }
    void observe() {
        for(auto it=voices.begin();it!=voices.end();) {
            auto& voice=*it;
            if(voice.pending||voice.release_pending){++it;continue;}
            const auto physical=mixer.voice_state(voice.token);
            const bool zero=physical.stop_reason!=AudioVoiceStopReasonV1::none||physical.envelope.level==0;
            if(!zero)voice.zero_remaining=voice.attached?bank.observation.zero_observations_before_completion:
                bank.observation.zero_observations_after_release;
            else if(--voice.zero_remaining==0) {
                if(voice.attached) {
                    scheduler.complete_owned(voice.instance);++completed;
                    detach(voice,bank.observation.observations_after_completion_before_retire);
                }else{
                    mixer.stop(voice.token);mixer.retire(voice.token);++retired;it=voices.erase(it);continue;
                }
            }
            ++it;
        }
    }
    void collect_ended_instances() {
        for(auto it=instances.begin();it!=instances.end();) {
            const auto state=scheduler.instance_state(*it);
            if(state.node==audio_program_end_v1&&!state.owned_count) {scheduler.retire(*it);it=instances.erase(it);}
            else ++it;
        }
    }
    void tick() {
        observe();
        // Event snapshots retain pre-modulation controls. Although the graph
        // scheduler computes modulation before returning, no PCM is generated
        // between the following release/start stages and the live updates.
        scheduler.advance_tick();consume_events();
        for(auto& voice:voices)if(voice.release_pending&&!voice.pending) {
            mixer.release(voice.token);voice.release_pending=false;
        }
        for(auto& voice:voices)if(voice.pending) {
            voice.token=mixer.admit(std::move(voice.pending),voice.control);
            // Release before the start commit cancels that start. The reserved
            // physical slot still survives until its off-state zero observation.
            if(voice.release_pending)mixer.stop(voice.token);
            else ++started;
            voice.release_pending=false;
        }
        for(auto& voice:voices)if(voice.attached)controls(voice,scheduler.instance_state(voice.instance).scalars,false);
        collect_ended_instances();
    }
};

AudioVoiceBankPlayerV1::AudioVoiceBankPlayerV1(AudioVoiceBankV1 bank,std::string id,AudioProgramBankV1 program,
    std::vector<AudioVoiceBankStreamV1> streams,std::vector<AudioVoiceBankGainV1> gains,std::uint64_t first,
    AudioVoiceMixerLimitsV1 ml,AudioProgramLimitsV1 pl):implementation_(std::make_unique<Implementation>(
        std::move(bank),std::move(id),std::move(program),std::move(streams),std::move(gains),first,ml,pl)) {}
AudioVoiceBankPlayerV1::~AudioVoiceBankPlayerV1()=default;
std::uint64_t AudioVoiceBankPlayerV1::admit(std::uint32_t key) {
    auto& s=*implementation_;require(!s.stopped,"Stopped audio bank cannot admit a program");
    const auto found=std::find_if(s.definitions.programs.begin(),s.definitions.programs.end(),[&](const auto& p){return p.key==key;});
    require(found!=s.definitions.programs.end(),"Audio bank program key is absent");
    const auto token=s.scheduler.admit(static_cast<std::uint32_t>(found-s.definitions.programs.begin()));
    s.instances.push_back(token);s.consume_events();s.collect_ended_instances();return token;
}
void AudioVoiceBankPlayerV1::stop_instance(std::uint64_t token) {
    auto& s=*implementation_;require(!s.stopped,"Stopped audio bank cannot run another stop path");
    s.scheduler.stop_instance(token);s.consume_events();s.collect_ended_instances();
}
void AudioVoiceBankPlayerV1::stop_all() {
    auto& s=*implementation_;if(s.stopped)return;
    for(const auto token:s.instances){s.scheduler.stop_instance(token);s.consume_events();}
    s.scheduler.stop();s.mixer.stop_all();
    for(const auto& voice:s.voices)if(voice.token){s.mixer.retire(voice.token);++s.retired;}
    s.voices.clear();
    for(const auto token:s.instances) {
        const auto owned=s.scheduler.instance_state(token).owned_count;
        if(owned)s.scheduler.complete_owned(token,owned);
        s.scheduler.retire(token);
    }
    s.instances.clear();s.stopped=true;
}
AudioVoiceBankPlaybackStateV1 AudioVoiceBankPlayerV1::state() const noexcept {
    const auto& s=*implementation_;AudioVoiceBankPlaybackStateV1 result;
    result.next_frame=s.frame;result.program_tick=s.scheduler.tick();result.random_draws=s.scheduler.random_draws();
    result.instances=s.instances.size();result.physical_voices=s.mixer.owned_voices();
    for(const auto& voice:s.voices){result.pending_starts+=bool(voice.pending);result.attached_voices+=voice.attached;}
    result.started=s.started;result.completed=s.completed;result.retired=s.retired;result.stopped=s.stopped;return result;
}
std::optional<AudioProgramInstanceStateV1> AudioVoiceBankPlayerV1::instance_state(std::uint64_t token) const {
    const auto& s=*implementation_;
    if(std::find(s.instances.begin(),s.instances.end(),token)==s.instances.end())return std::nullopt;
    return s.scheduler.instance_state(token);
}
void AudioVoiceBankPlayerV1::render(std::span<std::int16_t> output) {
    auto& s=*implementation_;const auto frames=output.size()/2U;
    require(output.size()%2U==0 && frames<=s.limits.max_render_frames && frames<=UINT64_MAX-s.frame,
        "Audio bank output span exceeds its frame bounds");
    const auto& observation=s.bank.observation;
    for(std::size_t i=0;i<frames;++i,++s.frame) {
        if(!s.stopped&&s.frame%observation.frame_stride==observation.first_frame_offset)s.tick();
        s.mixer.render(output.subspan(i*2U,2U));
    }
}

} // namespace openrc
