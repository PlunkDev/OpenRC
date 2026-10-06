#include "openrc/audio_voice_bank_player.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace openrc;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& call){try{call();}catch(const std::runtime_error&){return;}
    throw std::runtime_error("Invalid bank playback operation was accepted");}
struct Fixture {
    AudioVoiceBankV1 bank;
    AudioProgramBankV1 program;
    std::vector<AudioVoiceBankStreamV1> streams;
    std::vector<AudioVoiceBankGainV1> gains;
    Fixture(bool loop=false) {
        bank.program_resource_id="program";bank.stream_resource_ids={"stream"};bank.gain_resource_ids={"gain"};
        bank.observation={200,0,4,1,1};bank.phase_curves.push_back({0,{4}});
        AudioVoiceBindingV1 binding;binding.program_key=17;
        binding.envelope.maximum_level=16;binding.envelope.counter_period=1;
        binding.envelope.stages[0]={1,1,UINT32_MAX,0,16,1,16,false};
        binding.envelope.stages[1]={1,1,UINT32_MAX,0,0,1,16,true};
        binding.envelope.stages[2]={1,1,UINT32_MAX,0,0,1,0,false};
        binding.envelope.stages[3]={1,1,UINT32_MAX,0,-4,1,0,true};
        bank.bindings.push_back(binding);
        program.ticks_per_second=240;program.modulation_tick_divisor=2;program.random={{1,2,3},0,1};
        AudioProgramV1 definition;definition.key=17;definition.voice_count=1;
        definition.nodes={{0,1,AudioProgramVoiceV1{0}},{0,2,AudioProgramWaitOwnedV1{}},
            {0,3,AudioProgramStopSectionV1{}},{0,audio_program_end_v1,AudioProgramReleaseV1{}}};
        program.programs.push_back(std::move(definition));
        AudioStreamV1 stream;stream.output_sample_rate=48000;stream.phase_denominator=4;
        stream.coefficient_denominator=1;stream.gain_denominator=16;
        for(auto& row:stream.coefficients)row={1,0,0,0};
        stream.input_samples.assign(8,1600);if(loop)stream.repeat_end=8;
        streams.push_back({"stream",std::move(stream)});
        AudioGainTableV1 gain;gain.state_count=1;gain.level_count=1;gain.pan_count=1;gain.gain_denominator=16;
        gain.cells={{{16,16},0}};gains.push_back({"gain",std::move(gain)});
    }
    std::unique_ptr<AudioVoiceBankPlayerV1> player(std::uint64_t first=0) const {
        return std::make_unique<AudioVoiceBankPlayerV1>(bank,"program",program,streams,gains,first);
    }
};
void natural_observation() {
    auto player=Fixture().player();const auto id=player->admit(17);
    check(player->state().pending_starts==1 && player->state().physical_voices==0,
        "Program admission started physical PCM before its commit");
    std::vector<std::int16_t> output(1600,-1);player->render(output);
    check(std::all_of(output.begin(),output.begin()+10,[](auto x){return x==1600;}) &&
        std::all_of(output.begin()+10,output.end(),[](auto x){return x==0;}),"Finite bank PCM differs");
    check(player->state().attached_voices==1 && player->instance_state(id)->owned_count==1 &&
        player->state().completed==0,"Natural completion happened before four zero observations");
    std::array<std::int16_t,2> one{};player->render(one);
    check(player->state().completed==1 && player->state().attached_voices==0 &&
        player->state().physical_voices==1 && !player->instance_state(id),
        "Fourth zero failed to finish the logical program while preserving its physical slot");
    output.resize(400);player->render(output);
    check(player->state().next_frame==1001 && player->state().physical_voices==0 && player->state().retired==1,
        "Completed voice did not retire at its next off-state zero observation");
}
void pending_cancel_and_detached_release() {
    auto cancelled=Fixture(true).player();const auto id=cancelled->admit(17);cancelled->stop_instance(id);
    std::array<std::int16_t,2> one;one.fill(99);cancelled->render(one);
    check(one==std::array<std::int16_t,2>{0,0} && cancelled->state().started==0 &&
        cancelled->state().physical_voices==1 && cancelled->state().attached_voices==0,
        "Release before start restarted PCM or prematurely reused its physical slot");
    std::vector<std::int16_t> output(400);cancelled->render(output);
    check(cancelled->state().retired==1 && cancelled->state().physical_voices==0 && cancelled->state().completed==0,
        "Cancelled start did not retire at first off-zero or incorrectly delivered a natural callback");
    auto tail=Fixture(true).player();const auto playing=tail->admit(17);tail->render(one);tail->stop_instance(playing);
    check(!tail->instance_state(playing) && tail->state().physical_voices==1,
        "Logical stop did not preserve its detached physical tail");
    output.resize(398);tail->render(output); // Through frame199: release has not committed.
    check(std::ranges::all_of(output,[](auto x){return x==1600;}),"Release committed before its prepared observation grid");
    std::array<std::int16_t,8> release{};tail->render(release);
    check(release==std::array<std::int16_t,8>{1200,1200,800,800,400,400,0,0},"Detached release tail differs");
    output.resize(394);tail->render(output); // Includes observation400.
    check(tail->state().physical_voices==0 && tail->state().completed==0,
        "Detached tail retained its physical slot or completed a retired logical owner");
}
void phase_partition_and_stop_all() {
    Fixture fixture(true);auto whole=fixture.player(4097U*200U),parts=fixture.player(4097U*200U);
    check(whole->state().program_tick==4097 && whole->state().random_draws==0,"Idle clock reset its phase or drew random values");
    const auto a=whole->admit(17),b=parts->admit(17);
    std::array<std::int16_t,802> expected{},actual{};whole->render(expected);
    parts->render(std::span{actual}.first(2));
    check(parts->state().program_tick==4098,"Admission reset the retained observation parity");
    parts->render(std::span{actual}.subspan(2,396));parts->render(std::span{actual}.subspan(398));
    check(actual==expected && whole->state()==parts->state() && whole->instance_state(a)==parts->instance_state(b),
        "Bank PCM or control state depends on output chunk boundaries");
    whole->stop_instance(a); // Detached voice must also be included in hard stop.
    (void)whole->admit(17);  // A pending start must not survive hard stop either.
    whole->stop_all();whole->stop_all();
    check(whole->state().stopped && whole->state().instances==0 && whole->state().physical_voices==0 &&
        whole->state().pending_starts==0,"Stop-all retained a program, pending start or detached voice");
    whole->render(expected);check(std::ranges::all_of(expected,[](auto x){return x==0;}),"Stopped bank generated PCM");
    rejects([&]{(void)whole->admit(17);});
}
void cross_resource_validation() {
    Fixture fixture;fixture.streams[0].resource_id="other";rejects([&]{(void)fixture.player();});
    fixture=Fixture();fixture.bank.bindings[0].level.maximum=1;rejects([&]{(void)fixture.player();});
    fixture=Fixture();fixture.program.programs[0].voice_count=2;rejects([&]{(void)fixture.player();});
    fixture=Fixture();fixture.gains[0].table.gain_denominator=17;rejects([&]{(void)fixture.player();});
    fixture=Fixture();fixture.program.ticks_per_second=239;rejects([&]{(void)fixture.player();});
    auto player=Fixture().player();std::array<std::int16_t,3> odd{1,2,3};const auto before=player->state();
    rejects([&]{player->render(odd);});
    check(player->state()==before && odd==std::array<std::int16_t,3>{1,2,3},"Invalid output advanced the bank");
}
void stop_path_admission() {
    Fixture fixture(true);fixture.program.programs[0].nodes[3].delay_ticks=1;
    rejects([&]{(void)fixture.player();});
    fixture=Fixture(true);fixture.program.programs[0].nodes[3].action=AudioProgramRandomWaitV1{1};
    rejects([&]{(void)fixture.player();});
    fixture=Fixture(true);fixture.program.programs[0].nodes[3].action=AudioProgramWaitOwnedV1{};
    rejects([&]{(void)fixture.player();});
    fixture=Fixture(true);fixture.program.programs[0].nodes[3].action=AudioProgramVoiceV1{0};
    rejects([&]{(void)fixture.player();});
    fixture=Fixture(true);fixture.program.programs[0].nodes[3].next=3;
    rejects([&]{(void)fixture.player();});
    fixture=Fixture(true);
    auto& nodes=fixture.program.programs[0].nodes;
    nodes[3]={0,4,AudioProgramCompareV1{0,AudioProgramComparisonV1::equal,0,5}};
    nodes.push_back({0,audio_program_end_v1,AudioProgramReleaseV1{}});
    nodes.push_back({1,audio_program_end_v1,AudioProgramReleaseV1{}});
    rejects([&]{(void)fixture.player();}); // Both possible branches are qualified.
    nodes[5].delay_ticks=0;auto player=fixture.player();(void)player->admit(17);player->stop_all();
    check(player->state().stopped&&player->state().instances==0,"Immediate conditional stop path failed");
    AudioProgramLimitsV1 limits;limits.max_actions_per_tick=1;
    rejects([&]{AudioVoiceBankPlayerV1 p(fixture.bank,"program",fixture.program,fixture.streams,fixture.gains,0,{},limits);});
    fixture=Fixture(true);limits={};limits.max_events=1;
    AudioVoiceBankPlayerV1 bounded(fixture.bank,"program",fixture.program,fixture.streams,fixture.gains,0,{},limits);
    (void)bounded.admit(17);(void)bounded.admit(17);bounded.stop_all();
    check(bounded.state().stopped&&bounded.state().instances==0,
        "Stop-all failed to consume each bounded immediate stop event before the next owner");
}
}
int main()try {
    natural_observation();pending_cancel_and_detached_release();phase_partition_and_stop_all();cross_resource_validation();stop_path_admission();
    std::cout<<"Audio bank physical/logical observations/pending cancel/detached release/global phase/chunking/stop-all/resource admission PASS\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
