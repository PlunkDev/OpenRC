#include "openrc/audio_program_cues.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <array>
#include <set>

namespace openrc {
namespace {
void require(bool v,const char* message){if(!v)throw AudioProgramCueError(message);}
constexpr std::array<std::byte,8> magic{std::byte{'O'},std::byte{'R'},std::byte{'A'},std::byte{'U'},
    std::byte{'C'},std::byte{'U'},std::byte{'E'},std::byte{'1'}};
void put(std::vector<std::byte>& out,std::uint64_t value,unsigned bytes=4){
    for(unsigned i=0;i<bytes;++i){out.push_back(std::byte(value&255U));value>>=8;}}
void string(std::vector<std::byte>& out,const std::string& value){put(out,value.size());for(unsigned char c:value)out.push_back(std::byte{c});}
struct Reader {
    std::span<const std::byte> bytes;std::size_t at=0;
    std::uint64_t get(unsigned size=4){require(at<=bytes.size() && size<=bytes.size()-at,"Truncated audio cue resource");
        std::uint64_t value=0;for(unsigned i=0;i<size;++i)value|=std::uint64_t{std::to_integer<unsigned>(bytes[at++])}<<(i*8U);return value;}
    std::string string(std::uint32_t limit){const auto size=get();require(size<=limit && at<=bytes.size() && size<=bytes.size()-at,"Audio cue handle extent is invalid");
        std::string result;result.reserve(static_cast<std::size_t>(size));for(std::uint64_t i=0;i<size;++i)result.push_back(static_cast<char>(std::to_integer<unsigned>(bytes[at++])));return result;}
};
}
AudioProgramCueActionV1 audio_program_cue_action_v1(const AudioProgramCueV1& cue,std::uint64_t sample,bool valid){
    require(cue.first_scene_sample<=cue.last_scene_sample,"Audio cue window is inverted");
    const bool active=sample>=cue.first_scene_sample && sample<=cue.last_scene_sample;
    return active?(valid?AudioProgramCueActionV1::none:AudioProgramCueActionV1::admit):
        (valid?AudioProgramCueActionV1::stop:AudioProgramCueActionV1::none);
}
void validate_audio_program_cues_v1(const AudioProgramCuesV1& value,AudioProgramCueLimitsV1 limits){
    const auto id=[&](const std::string& s){require(!s.empty() && s.size()<=limits.max_id_bytes &&
        std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;}),"Audio cue resource handle is invalid");};
    id(value.voice_bank_resource_id);id(value.timeline_resource_id);
    require(value.voice_bank_resource_id!=value.timeline_resource_id && value.updates_per_second && value.updates_per_second<=1000 &&
        !value.cues.empty() && value.cues.size()<=limits.max_cues,"Audio cue owner domain is invalid");
    std::set<std::uint32_t> keys;
    for(const auto& cue:value.cues)require(keys.insert(cue.key).second && cue.first_scene_sample<=cue.last_scene_sample,
        "Audio cue key or inclusive window is invalid");
    require(80U+value.voice_bank_resource_id.size()+value.timeline_resource_id.size()+std::uint64_t{value.cues.size()}*24U<=limits.max_bytes,
        "Audio cue resource exceeds its byte limit");
}
std::vector<std::byte> encode_audio_program_cues_v1(const AudioProgramCuesV1& value,AudioProgramCueLimitsV1 limits){
    validate_audio_program_cues_v1(value,limits);std::vector<std::byte> body;
    put(body,value.updates_per_second);put(body,value.cues.size());string(body,value.voice_bank_resource_id);string(body,value.timeline_resource_id);
    for(const auto& cue:value.cues){put(body,cue.key);put(body,cue.program_key);put(body,cue.first_scene_sample,8);put(body,cue.last_scene_sample,8);}
    std::vector<std::byte> out(magic.begin(),magic.end());put(out,1);put(out,64);put(out,body.size(),8);put(out,0,8);
    const auto digest=prepared_content_sha256_v1(body);out.insert(out.end(),digest.begin(),digest.end());out.insert(out.end(),body.begin(),body.end());return out;
}
AudioProgramCuesV1 decode_audio_program_cues_v1(std::span<const std::byte> bytes,AudioProgramCueLimitsV1 limits){
    require(bytes.size()>=80 && bytes.size()<=limits.max_bytes && std::equal(magic.begin(),magic.end(),bytes.begin()),"Audio cue envelope is invalid");
    Reader h{bytes,8};require(h.get()==1 && h.get()==64 && h.get(8)==bytes.size()-64 && h.get(8)==0,"Audio cue header differs");
    const auto body=bytes.subspan(64);const auto digest=prepared_content_sha256_v1(body);
    require(std::equal(digest.begin(),digest.end(),bytes.begin()+32),"Audio cue digest differs");Reader r{body};
    AudioProgramCuesV1 value;value.updates_per_second=static_cast<std::uint32_t>(r.get());const auto count=r.get();
    require(count && count<=limits.max_cues && count<=(body.size()-8)/24,"Audio cue declared count exceeds its owner");
    value.voice_bank_resource_id=r.string(limits.max_id_bytes);value.timeline_resource_id=r.string(limits.max_id_bytes);
    require(r.at<=body.size() && body.size()-r.at==count*24,"Audio cue rows do not partition the body");
    value.cues.reserve(static_cast<std::size_t>(count));
    for(std::uint64_t i=0;i<count;++i){AudioProgramCueV1 cue;cue.key=static_cast<std::uint32_t>(r.get());cue.program_key=static_cast<std::uint32_t>(r.get());
        cue.first_scene_sample=r.get(8);cue.last_scene_sample=r.get(8);value.cues.push_back(cue);}
    validate_audio_program_cues_v1(value,limits);return value;
}
} // namespace openrc
