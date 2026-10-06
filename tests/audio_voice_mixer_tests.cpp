#include "openrc/audio_voice_mixer.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace openrc;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& call) {
    try {call();} catch(const std::runtime_error&) {return;}
    throw std::runtime_error("Invalid mixer operation was accepted");
}
std::unique_ptr<AudioVoicePlayerV1> voice(std::int16_t value,std::uint32_t rate=48000) {
    AudioStreamV1 stream;stream.output_sample_rate=rate;stream.phase_denominator=4;
    stream.coefficient_denominator=1;stream.gain_denominator=16;
    for(auto& row:stream.coefficients)row={1,0,0,0};
    stream.input_samples.assign(8,value);stream.repeat_begin=0;stream.repeat_end=8;
    AudioEnvelopeV1 envelope;envelope.maximum_level=16;envelope.counter_period=1;
    envelope.stages[0]={1,1,UINT32_MAX,0,16,1,16,false};
    envelope.stages[1]={1,1,UINT32_MAX,0,0,1,16,true};
    envelope.stages[2]={1,1,UINT32_MAX,0,0,1,0,false};
    envelope.stages[3]={1,1,UINT32_MAX,0,-4,1,0,true};
    return std::make_unique<AudioVoicePlayerV1>(std::move(stream),envelope);
}
const AudioVoiceControlV1 full{4,{16,16}}, inverted{4,{-16,-16}};
void sum_and_retirement() {
    AudioVoiceMixerV1 mixer(48000,{3,200});
    const auto a=mixer.admit(voice(30000),full), b=mixer.admit(voice(30000),full),
        c=mixer.admit(voice(30000),inverted);
    std::array<std::int16_t,2> pcm{};mixer.render(pcm);
    check(pcm==std::array<std::int16_t,2>{30000,30000},"Mixer clipped before completing the signed sum");
    rejects([&]{mixer.retire(c);});
    check(mixer.owned_voices()==3,"Premature retirement lost a physical owner");
    mixer.stop(c);mixer.render(pcm);
    check(pcm==std::array<std::int16_t,2>{32767,32767} && mixer.owned_voices()==3,
        "Stopped voice still mixed or was implicitly retired");
    rejects([&]{(void)mixer.admit(voice(1),full);});
    mixer.retire(c);rejects([&]{mixer.release(c);});
    const auto d=mixer.admit(voice(7),full);
    check(d!=c && mixer.owned_voices()==3,"Reused physical slot reused an old token");
    rejects([&]{mixer.set_control(c,full);});
    mixer.stop_all();mixer.render(pcm);
    check(pcm==std::array<std::int16_t,2>{0,0} && mixer.owned_voices()==3,
        "Hard mute failed or incorrectly acknowledged physical retirement");
    for(auto token:{a,b,d})mixer.retire(token);
    check(mixer.owned_voices()==0 && mixer.rendered_frames()==3,"Retirement or output frame accounting differs");
    mixer.render(pcm);check(pcm==std::array<std::int16_t,2>{0,0},"Empty mixer did not emit silence");
}
void release_and_partitions() {
    AudioVoiceMixerV1 mixer(48000), split(48000);
    const auto token=mixer.admit(voice(1600),full), other=split.admit(voice(1600),full);
    std::array<std::int16_t,2> initial{};mixer.render(initial);split.render(initial);
    mixer.release(token);split.release(other);
    std::array<std::int16_t,12> full_output{}, split_output{};
    mixer.render(full_output);
    split.render(std::span{split_output}.first(2));split.render(std::span{split_output}.subspan(2,6));
    split.render(std::span{split_output}.subspan(8));
    check(full_output==std::array<std::int16_t,12>{1200,1200,800,800,400,400,0,0,0,0,0,0} &&
        full_output==split_output && mixer.voice_state(token)==split.voice_state(other),
        "Physical release tail or state depends on the mixer block boundary");
    check(mixer.owned_voices()==1 && mixer.voice_state(token).stop_reason==AudioVoiceStopReasonV1::envelope_stopped,
        "Envelope completion removed its physical owner before caller retirement");
    mixer.retire(token);
}
void rejected_controls() {
    AudioVoiceMixerV1 mixer(48000,{1,8});
    rejects([&]{(void)mixer.admit(voice(100,44100),full);});
    rejects([&]{(void)mixer.admit(voice(100),{4,{17,16}});});
    check(mixer.owned_voices()==0,"Invalid admission installed an owner");
    const auto token=mixer.admit(voice(100),full);const auto before=mixer.voice_state(token);
    rejects([&]{mixer.set_control(token,{4,{16,17}});});
    std::array<std::int16_t,20> pcm;pcm.fill(999);
    rejects([&]{mixer.render(pcm);});
    rejects([&]{mixer.render(std::span{pcm}.first(3));});
    check(mixer.voice_state(token)==before && mixer.rendered_frames()==0 &&
        std::ranges::all_of(pcm,[](auto x){return x==999;}),"Invalid mixer operation partially advanced output");
    mixer.render(std::span{pcm}.first(2));
    check(pcm[0]==100 && pcm[1]==100,"Rejected control replaced the last admitted gain");
}
}
int main()try {
    sum_and_retirement();release_and_partitions();rejected_controls();
    std::cout<<"Audio physical mixer signed sum/final saturation/release tails/explicit retirement/tokens/atomic rejection PASS\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
