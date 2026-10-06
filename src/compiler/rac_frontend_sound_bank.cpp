#include "openrc/rac_frontend_sound_bank.hpp"
#include "openrc/rac_frontend_sound_pitch.hpp"
#include "openrc/rac_frontend_sound_voice.hpp"

#include <algorithm>
#include <bit>
#include <map>

namespace openrc {
namespace {
void require(bool ok,const char* message){if(!ok)throw RacFrontendSoundCompileError(message);}
AudioScalarProjectionV1 projection(std::int32_t value,std::int32_t low,std::int32_t high,bool wrap=false){
    AudioScalarProjectionV1 result;result.constant=value;result.minimum=low;result.maximum=high;result.wrap=wrap;return result;
}
} // namespace
RacFrontendSoundPreparedBankV1 compile_rac_frontend_ambient_bank_v1(const RacFrontendSoundSourceV1& source,
    const RacFrontendSoundBankV1& bank,std::span<const std::byte> irx){
    require(source.default_effects_volume==1024 && source.default_stereo,"Ambient bank invocation settings are not qualified");
    const auto control=compile_rac_frontend_sound_programs_v1(bank,irx);
    RacFrontendSoundPreparedBankV1 result;result.program=control.control;
    auto& voices=result.voices;voices.observation={200,0,4,1,1};
    voices.program_resource_id="frontend/audio/ambient-program";
    const auto gain_source=make_rac_frontend_sound_gain_source_v1(irx);
    // Per-voice levels for the first family, per-owner levels for the others.
    constexpr std::array<unsigned,4> fixed{65,110,85,95};
    for(unsigned family=0;family<fixed.size();++family){
        voices.gain_resource_ids.push_back("frontend/audio/ambient-gain-"+std::to_string(family));
        result.gains.push_back(compile_rac_frontend_sound_gain_table_v1(gain_source,fixed[family],family!=0,819,0));
    }
    std::map<unsigned,unsigned> blocks;
    for(const auto& tone:control.voices){
        AudioVoiceBindingV1 binding;binding.program_key=tone.program_key;binding.voice_index=tone.voice_index;
        const auto block=blocks.find(tone.block_index);
        if(block==blocks.end()){
            binding.stream_index=static_cast<unsigned>(result.streams.size());blocks.emplace(tone.block_index,binding.stream_index);
            result.streams.push_back(compile_rac_frontend_sound_stream_v1(bank,tone.block_index));
            voices.stream_resource_ids.push_back("frontend/audio/ambient-stream-"+std::to_string(binding.stream_index));
        }else binding.stream_index=block->second;
        auto curve=compile_rac_frontend_sound_pitch_curve_v1(source,tone.source_tone);
        const auto found=std::find_if(voices.phase_curves.begin(),voices.phase_curves.end(),[&](const auto& candidate){
            return candidate.first_control==curve.first_control && candidate.increments==curve.phase_increments;});
        binding.phase_curve_index=static_cast<unsigned>(found-voices.phase_curves.begin());
        if(found==voices.phase_curves.end())voices.phase_curves.push_back({curve.first_control,std::move(curve.phase_increments)});
        binding.phase=projection(0,-32768,32767);binding.phase.terms={{true,4,1,1},{true,6,1,1}};
        const auto& descriptor=bank.bank.descriptors.at(tone.program_key);
        const auto tone_volume=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(tone.source_tone.payload[1]>>8));
        const auto tone_pan=std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(tone.source_tone.payload[2]));
        if(tone.program_key==2){
            require((descriptor.type&0xffffU)==65,"Ambient variable tone gain family differs");binding.gain_table_index=0;
            binding.level=projection(tone_volume,0,127);
            if(tone_volume<0){require(tone_volume>=-4,"Ambient tone requests an unqualified random gain");
                binding.level.constant=0;binding.level.terms={{false,static_cast<unsigned>(-tone_volume-1),1,1}};}
        }else{
            require(tone_volume==110 || tone_volume==85 || tone_volume==95,"Ambient fixed tone gain family differs");
            binding.gain_table_index=tone_volume==110?1:tone_volume==85?2:3;
            binding.level=projection(static_cast<std::int32_t>(descriptor.type&0xffffU),0,127);
            binding.level.terms={{true,7,1,1}};
        }
        // Original EE queue/pump/RPC execution for all five scenic cues
        // qualifies this nonspatial invocation's explicit pan-zero override.
        binding.pan=projection(tone_pan,0,359,true);binding.pan.terms={{true,5,1,1}};
        if(tone_pan<0){require(tone_pan>=-4,"Ambient tone requests an unqualified random pan");
            binding.pan.constant=0;binding.pan.terms.push_back({false,static_cast<unsigned>(-tone_pan-1),360,127});}
        binding.envelope=compile_rac_frontend_sound_envelope_v1(static_cast<std::uint16_t>(tone.source_tone.payload[3]>>16),
            static_cast<std::uint16_t>(tone.source_tone.payload[4]));
        if(!result.streams[binding.stream_index].repeat_end)
            binding.read_ahead=compile_rac_frontend_sound_read_ahead_v1(bank,tone.block_index);
        voices.bindings.push_back(std::move(binding));
    }
    require(result.streams.size()==13 && voices.bindings.size()==26,"Ambient bank reached owner count differs");
    validate_audio_voice_bank_v1(voices);return result;
}
} // namespace openrc
