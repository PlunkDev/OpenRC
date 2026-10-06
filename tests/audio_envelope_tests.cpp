#include "openrc/audio_envelope.hpp"

#include <functional>
#include <iostream>

namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
void rejects(const std::function<void()>& call) {
    try{call();}catch(const openrc::AudioEnvelopeError&){return;}
    throw std::runtime_error("Invalid envelope was accepted");
}
openrc::AudioEnvelopeV1 envelope() {
    openrc::AudioEnvelopeV1 value;value.maximum_level=16;value.counter_period=4;
    value.stages[0]={4,4,UINT32_MAX,0,3,1,16,false};
    value.stages[1]={1,1,UINT32_MAX,0,-2,1,8,true};
    value.stages[2]={1,1,UINT32_MAX,0,0,1,0,false};
    value.stages[3]={2,2,UINT32_MAX,-1,0,4,0,true};
    return value;
}
}
int main()try {
    auto definition=envelope();
    openrc::AudioEnvelopePlayerV1 player(definition);
    for(const auto value:{3U,6U,9U,12U,15U,16U})check(player.advance_frame()==value,"Envelope attack trajectory differs");
    check(player.state().stage==1,"Attack did not enter the next stage");
    for(unsigned i=0;i<16;++i)(void)player.advance_frame();
    check(player.state().level==8 && player.state().stage==2,"Decay did not reach its explicit target");
    for(unsigned i=0;i<9;++i)check(player.advance_frame()==8,"Sustain did not retain its level");
    player.release();
    check(player.state().counter==0 && player.state().level==8 && player.state().stage==3,
        "Release did not preserve level/reset only the counter");
    for(const auto value:{8U,6U,6U,4U,4U,3U,3U,2U,2U,1U,1U,0U})
        check(player.advance_frame()==value,"Negative affine envelope delta did not floor");
    check(player.state().stopped,"Release completion did not stop the envelope");
    const auto stopped=player.state();player.release();(void)player.advance_frame();
    check(player.state()==stopped,"Completed envelope continued to mutate");
    definition.stages[0].slow_above_level=8;definition.stages[0].slow_counter_increment=1;
    openrc::AudioEnvelopePlayerV1 slow(definition);
    for(const auto value:{3U,6U,9U,9U,9U,9U,12U})check(slow.advance_frame()==value,"Threshold rate change lost counter phase");
    slow.release();
    check(slow.advance_frame()==12 && slow.advance_frame()==9,"Early release restarted from a fabricated peak");
    definition=envelope();definition.stages[1].target=17;
    openrc::AudioEnvelopePlayerV1 transition(definition);
    for(unsigned i=0;i<7;++i)(void)transition.advance_frame();
    check(transition.state().stage==2 && transition.state().counter==1,
        "Target transition incorrectly reset the fractional stage counter");
    definition=envelope();definition.maximum_level=0;
    rejects([&]{openrc::AudioEnvelopePlayerV1 invalid(definition);});
    definition=envelope();definition.stages[3].delta_denominator=0;
    rejects([&]{openrc::AudioEnvelopePlayerV1 invalid(definition);});
    definition=envelope();definition.stages[0].slow_counter_increment=0;
    rejects([&]{openrc::AudioEnvelopePlayerV1 invalid(definition);});
    std::cout<<"Audio envelope exact stages/threshold/floor/early release/counter ownership PASS\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
