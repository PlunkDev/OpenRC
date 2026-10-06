#include "openrc/audio_program_cues.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <functional>
#include <iostream>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void rejects(const std::function<void()>& action){try{action();}catch(const openrc::AudioProgramCueError&){return;}
    throw std::runtime_error("Invalid audio cue accepted");}
}
int main()try{
    openrc::AudioProgramCuesV1 bank{"bank","timeline",50,{{5,17,276,300},{8,19,740,1240}}};
    const auto bytes=openrc::encode_audio_program_cues_v1(bank);
    check(openrc::decode_audio_program_cues_v1(bytes)==bank,"Cue codec roundtrip differs");
    using A=openrc::AudioProgramCueActionV1;const auto& cue=bank.cues[0];
    for(const auto sample:{275U,301U}){
        check(openrc::audio_program_cue_action_v1(cue,sample,false)==A::none,"Inactive missing owner admitted");
        check(openrc::audio_program_cue_action_v1(cue,sample,true)==A::stop,"Inactive live owner not stopped");}
    for(const auto sample:{276U,280U,300U}){
        check(openrc::audio_program_cue_action_v1(cue,sample,false)==A::admit,"Inclusive cue admission or readmission failed");
        check(openrc::audio_program_cue_action_v1(cue,sample,true)==A::none,"Live/pending owner was duplicated");}
    for(std::size_t n=0;n<bytes.size();++n)rejects([&]{(void)openrc::decode_audio_program_cues_v1(std::span(bytes).first(n));});
    auto corrupt=bytes;corrupt.back()^=std::byte{1};rejects([&]{(void)openrc::decode_audio_program_cues_v1(corrupt);});
    corrupt=bytes;corrupt[68]=std::byte{255};const auto hash=openrc::prepared_content_sha256_v1(std::span(corrupt).subspan(64));
    std::copy(hash.begin(),hash.end(),corrupt.begin()+32);rejects([&]{(void)openrc::decode_audio_program_cues_v1(corrupt);});
    auto invalid=bank;invalid.cues[1].key=5;rejects([&]{openrc::validate_audio_program_cues_v1(invalid);});
    invalid=bank;invalid.cues[0].first_scene_sample=301;rejects([&]{openrc::validate_audio_program_cues_v1(invalid);});
    std::cout<<"Audio program cues inclusive windows/owner gate/readmission/codec/bounds PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
