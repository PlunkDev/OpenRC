#include "openrc/audio_voice_bank.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <functional>
#include <iostream>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void rejects(const std::function<void()>& call){try{call();}catch(const openrc::AudioVoiceBankError&){return;}
    throw std::runtime_error("Invalid audio voice bank was accepted");}
openrc::AudioVoiceBankV1 bank(){
    openrc::AudioVoiceBankV1 b;b.program_resource_id="test/program";
    b.observation={200,0,4,1,1};
    b.stream_resource_ids={"test/stream"};b.gain_resource_ids={"test/gain"};b.phase_curves={{-1,{0,123,456}}};
    openrc::AudioVoiceBindingV1 v;v.program_key=17;v.voice_index=2;
    v.level={0,0,127,false,{{false,3,1,1}}};v.pan={12,0,359,true,{{false,2,360,127},{true,5,1,1}}};
    v.phase={0,-1,1,false,{{true,4,1,1},{true,6,1,1}}};
    v.envelope.maximum_level=32767;v.envelope.counter_period=32768;
    for(auto& s:v.envelope.stages){s.counter_increment=s.slow_counter_increment=1;}
    v.read_ahead=openrc::AudioReadAheadV1{56,4,12,4};b.bindings.push_back(v);return b;
}
void rehash(std::vector<std::byte>& bytes){const auto hash=openrc::prepared_content_sha256_v1(std::span(bytes).subspan(64));
    for(unsigned i=0;i<hash.size();++i)bytes[32+i]=std::byte{hash[i]};}
}
int main()try{
    auto value=bank();const auto encoded=openrc::encode_audio_voice_bank_v1(value);
    check(openrc::encode_audio_voice_bank_v1(openrc::decode_audio_voice_bank_v1(encoded))==encoded,"Voice bank canonical roundtrip differs");
    for(std::size_t i=0;i<encoded.size();++i)rejects([&]{(void)openrc::decode_audio_voice_bank_v1(std::span(encoded).first(i));});
    auto corrupt=encoded;corrupt.back()^=std::byte{1};rejects([&]{(void)openrc::decode_audio_voice_bank_v1(corrupt);});
    corrupt=encoded;corrupt[64]=std::byte{255};rehash(corrupt);rejects([&]{(void)openrc::decode_audio_voice_bank_v1(corrupt);});
    corrupt=encoded;corrupt[104]=std::byte{255};rehash(corrupt);rejects([&]{(void)openrc::decode_audio_voice_bank_v1(corrupt);});
    std::array<std::int32_t,8> snapshot{},live{};snapshot[2]=-1;snapshot[3]=77;live[3]=12;live[5]=-20;
    const auto& binding=value.bindings.front();
    check(openrc::evaluate_audio_scalar_projection_v1(binding.level,snapshot,live)==77,"Voice snapshot was replaced by live owner local");
    check(openrc::evaluate_audio_scalar_projection_v1(binding.pan,snapshot,live)==350,"Signed division/wrapped pan projection differs");
    live[4]=INT32_MAX;live[6]=INT32_MAX;
    check(openrc::evaluate_audio_scalar_projection_v1(binding.phase,snapshot,live)==1,"Phase clamp overflowed before saturation");
    live[4]=INT32_MIN;live[6]=INT32_MIN;
    check(openrc::evaluate_audio_scalar_projection_v1(binding.phase,snapshot,live)==-1,"Negative phase clamp overflowed");
    auto projection=binding.level;projection.terms[0].scalar=8;
    rejects([&]{(void)openrc::evaluate_audio_scalar_projection_v1(projection,snapshot,live);});
    projection=binding.level;projection.terms[0].divisor=0;
    rejects([&]{openrc::validate_audio_scalar_projection_v1(projection);});
    value=bank();value.bindings[0].phase.minimum=-2;rejects([&]{openrc::validate_audio_voice_bank_v1(value);});
    value=bank();value.bindings[0].stream_index=1;rejects([&]{openrc::validate_audio_voice_bank_v1(value);});
    value=bank();value.bindings.push_back(value.bindings[0]);rejects([&]{openrc::validate_audio_voice_bank_v1(value);});
    value=bank();value.gain_resource_ids[0]=value.stream_resource_ids[0];rejects([&]{openrc::validate_audio_voice_bank_v1(value);});
    value=bank();value.bindings[0].read_ahead->input_samples=55;rejects([&]{openrc::validate_audio_voice_bank_v1(value);});
    std::cout<<"Audio voice bank codec/handles/snapshot/live/rounding/bounds PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
