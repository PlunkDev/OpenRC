#include "openrc/audio_voice_bank.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <set>

namespace openrc {
namespace {
void require(bool value, const char* message) { if (!value) throw AudioVoiceBankError(message); }
constexpr std::array<std::byte,8> magic{std::byte{'O'},std::byte{'R'},std::byte{'A'},std::byte{'V'},
    std::byte{'B'},std::byte{'N'},std::byte{'K'},std::byte{'1'}};
void put(std::vector<std::byte>& bytes, std::uint64_t value, unsigned width = 4) {
    for (unsigned i=0;i<width;++i) { bytes.push_back(std::byte(value & 255U)); value >>= 8U; }
}
void put_string(std::vector<std::byte>& bytes, const std::string& value) {
    put(bytes,value.size()); for (unsigned char c:value) bytes.push_back(std::byte{c});
}
struct Reader {
    std::span<const std::byte> bytes;
    std::size_t at=0;
    void available(std::uint64_t count) const { require(at<=bytes.size() && count<=bytes.size()-at,
        "Truncated audio voice bank resource"); }
    std::uint64_t get(unsigned width=4) {
        available(width); std::uint64_t value=0;
        for(unsigned i=0;i<width;++i)value|=std::uint64_t{std::to_integer<unsigned>(bytes[at++])}<<(8U*i);
        return value;
    }
    std::uint32_t u32(){return static_cast<std::uint32_t>(get());}
    std::int32_t i32(){return std::bit_cast<std::int32_t>(u32());}
    bool flag(){const auto value=u32();require(value<=1,"Invalid audio voice bank flag");return value!=0;}
    std::string string(std::uint32_t limit){const auto size=u32();require(size<=limit,"Audio resource ID is too long");
        available(size);std::string result;result.reserve(size);
        for(unsigned i=0;i<size;++i)result.push_back(static_cast<char>(std::to_integer<unsigned>(bytes[at++])));
        return result;}
};
std::uint64_t size(const AudioVoiceBankV1& bank) {
    std::uint64_t result=64+24+16+4+bank.program_resource_id.size();
    for(const auto& id:bank.stream_resource_ids)result+=4+id.size();
    for(const auto& id:bank.gain_resource_ids)result+=4+id.size();
    for(const auto& curve:bank.phase_curves)result+=8+curve.increments.size()*2;
    for(const auto& binding:bank.bindings){
        result+=20+136+4+(binding.read_ahead?20:0);
        for(const auto* projection:{&binding.level,&binding.pan,&binding.phase})result+=20+projection->terms.size()*16;
    }
    return result;
}
void put_projection(std::vector<std::byte>& body,const AudioScalarProjectionV1& p) {
    put(body,static_cast<std::uint32_t>(p.constant));put(body,static_cast<std::uint32_t>(p.minimum));
    put(body,static_cast<std::uint32_t>(p.maximum));put(body,p.wrap);put(body,p.terms.size());
    for(const auto& term:p.terms){put(body,term.live);put(body,term.scalar);
        put(body,static_cast<std::uint32_t>(term.multiplier));put(body,term.divisor);}
}
AudioScalarProjectionV1 get_projection(Reader& reader,const AudioVoiceBankLimitsV1& limits) {
    AudioScalarProjectionV1 p;p.constant=reader.i32();p.minimum=reader.i32();p.maximum=reader.i32();p.wrap=reader.flag();
    const auto count=reader.u32();require(count<=limits.max_terms,"Audio scalar term count exceeds its limit");
    reader.available(std::uint64_t{count}*16);p.terms.reserve(count);
    for(unsigned i=0;i<count;++i){AudioScalarTermV1 t;t.live=reader.flag();t.scalar=reader.u32();
        t.multiplier=reader.i32();t.divisor=reader.u32();p.terms.push_back(t);}return p;
}
} // namespace
void validate_audio_scalar_projection_v1(const AudioScalarProjectionV1& p,AudioVoiceBankLimitsV1 limits) {
    require(p.minimum<=p.maximum && p.terms.size()<=limits.max_terms && p.terms.size()<=16,
        "Audio scalar projection domain is invalid");
    for(const auto& term:p.terms)require(term.scalar<audio_program_scalar_count_v1 &&
        std::abs(std::int64_t{term.multiplier})<=65536 && term.divisor && term.divisor<=(1U<<30U),
        "Audio scalar projection term is invalid");
}
std::int32_t evaluate_audio_scalar_projection_v1(const AudioScalarProjectionV1& p,
    const std::array<std::int32_t,audio_program_scalar_count_v1>& snapshot,
    const std::array<std::int32_t,audio_program_scalar_count_v1>& live) {
    validate_audio_scalar_projection_v1(p);
    std::int64_t value=p.constant;
    for(const auto& term:p.terms)value+=std::int64_t{(term.live?live:snapshot)[term.scalar]}*term.multiplier/term.divisor;
    if(p.wrap){const auto width=std::int64_t{p.maximum}-p.minimum+1;
        value=(value-p.minimum)%width;if(value<0)value+=width;value+=p.minimum;}
    else value=std::clamp<std::int64_t>(value,p.minimum,p.maximum);
    return static_cast<std::int32_t>(value);
}
void validate_audio_voice_bank_v1(const AudioVoiceBankV1& bank,AudioVoiceBankLimitsV1 limits) {
    const auto& o=bank.observation;
    require(o.frame_stride && o.frame_stride<=48000 && o.first_frame_offset<o.frame_stride &&
        o.zero_observations_before_completion && o.zero_observations_before_completion<=65536 &&
        o.observations_after_completion_before_retire && o.observations_after_completion_before_retire<=65536 &&
        o.zero_observations_after_release && o.zero_observations_after_release<=65536 &&
        o.schedule_order==AudioVoiceScheduleOrderV1::observe_program_release_start_modulation,
        "Audio voice observation clock or policy is invalid");
    const auto id=[&](const std::string& value){require(!value.empty() && value.size()<=limits.max_id_bytes &&
        std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=33 && c<=126;}),
        "Audio voice bank resource ID is invalid");};
    id(bank.program_resource_id);
    require(!bank.stream_resource_ids.empty() && bank.stream_resource_ids.size()<=limits.max_resources &&
        !bank.gain_resource_ids.empty() && bank.gain_resource_ids.size()<=limits.max_resources &&
        !bank.phase_curves.empty() && bank.phase_curves.size()<=limits.max_curves &&
        !bank.bindings.empty() && bank.bindings.size()<=limits.max_bindings,"Audio voice bank owner counts are invalid");
    std::set<std::string> ids{bank.program_resource_id};
    for(const auto* values:{&bank.stream_resource_ids,&bank.gain_resource_ids})for(const auto& value:*values){
        id(value);require(ids.insert(value).second,"Audio voice bank has duplicate resource handles");}
    for(const auto& curve:bank.phase_curves)require(!curve.increments.empty() && curve.increments.size()<=limits.max_curve_values &&
        std::int64_t{curve.first_control}+static_cast<std::int64_t>(curve.increments.size())-1<=INT32_MAX,
        "Audio phase curve control domain is invalid");
    std::set<std::pair<std::uint32_t,std::uint32_t>> bindings;
    for(const auto& b:bank.bindings){
        require(bindings.emplace(b.program_key,b.voice_index).second && b.stream_index<bank.stream_resource_ids.size() &&
            b.gain_table_index<bank.gain_resource_ids.size() && b.phase_curve_index<bank.phase_curves.size(),
            "Audio voice binding owner is invalid");
        for(const auto* p:{&b.level,&b.pan,&b.phase})validate_audio_scalar_projection_v1(*p,limits);
        require(b.level.minimum>=0 && b.pan.minimum>=0,"Audio gain coordinates must be nonnegative");
        const auto& curve=bank.phase_curves[b.phase_curve_index];
        require(b.phase.minimum>=curve.first_control && std::int64_t{b.phase.maximum}-curve.first_control<
            static_cast<std::int64_t>(curve.increments.size()),"Audio phase projection escapes its prepared curve");
        try{validate_audio_envelope_v1(b.envelope);if(b.read_ahead)validate_audio_read_ahead_v1(*b.read_ahead);}
        catch(const std::runtime_error&){throw AudioVoiceBankError("Audio voice envelope or end policy is invalid");}
    }
    require(size(bank)<=limits.max_bytes,"Audio voice bank exceeds its byte limit");
}
std::vector<std::byte> encode_audio_voice_bank_v1(const AudioVoiceBankV1& bank,AudioVoiceBankLimitsV1 limits) {
    validate_audio_voice_bank_v1(bank,limits);std::vector<std::byte> body;body.reserve(static_cast<std::size_t>(size(bank)-64));
    put(body,bank.stream_resource_ids.size());put(body,bank.gain_resource_ids.size());put(body,bank.phase_curves.size());put(body,bank.bindings.size());
    const auto& o=bank.observation;put(body,o.frame_stride);put(body,o.first_frame_offset);
    put(body,o.zero_observations_before_completion);put(body,o.observations_after_completion_before_retire);
    put(body,o.zero_observations_after_release);put(body,static_cast<std::uint32_t>(o.schedule_order));
    put_string(body,bank.program_resource_id);
    for(const auto* values:{&bank.stream_resource_ids,&bank.gain_resource_ids})for(const auto& value:*values)put_string(body,value);
    for(const auto& curve:bank.phase_curves){put(body,static_cast<std::uint32_t>(curve.first_control));put(body,curve.increments.size());
        for(auto increment:curve.increments)put(body,increment,2);}
    for(const auto& b:bank.bindings){put(body,b.program_key);put(body,b.voice_index);put(body,b.stream_index);put(body,b.gain_table_index);put(body,b.phase_curve_index);
        for(const auto* p:{&b.level,&b.pan,&b.phase})put_projection(body,*p);
        put(body,b.envelope.maximum_level);put(body,b.envelope.counter_period);
        for(const auto& s:b.envelope.stages){put(body,s.counter_increment);put(body,s.slow_counter_increment);put(body,s.slow_above_level);
            put(body,static_cast<std::uint32_t>(s.delta_multiplier));put(body,static_cast<std::uint32_t>(s.delta_bias));
            put(body,s.delta_denominator);put(body,s.target);put(body,s.decreasing);}
        put(body,b.read_ahead.has_value());if(b.read_ahead){const auto& r=*b.read_ahead;put(body,r.input_samples,8);put(body,r.refill_samples);
            put(body,r.refill_when_available_at_most);put(body,r.required_lookahead_samples);}}
    std::vector<std::byte> result(magic.begin(),magic.end());put(result,1);put(result,64);put(result,body.size(),8);put(result,0,8);
    const auto digest=prepared_content_sha256_v1(body);for(auto value:digest)result.push_back(std::byte{value});
    result.insert(result.end(),body.begin(),body.end());require(result.size()==size(bank),"Audio voice bank writer size differs");return result;
}
AudioVoiceBankV1 decode_audio_voice_bank_v1(std::span<const std::byte> bytes,AudioVoiceBankLimitsV1 limits) {
    require(bytes.size()>=64 && bytes.size()<=limits.max_bytes && std::equal(magic.begin(),magic.end(),bytes.begin()),
        "Audio voice bank envelope is invalid");Reader header{bytes,8};
    require(header.get()==1 && header.get()==64 && header.get(8)==bytes.size()-64 && header.get(8)==0,
        "Audio voice bank schema, extent or reserved fields differ");
    const auto body=bytes.subspan(64);const auto hash=prepared_content_sha256_v1(body);
    for(unsigned i=0;i<hash.size();++i)require(bytes[32+i]==hash[i],"Audio voice bank digest differs");
    Reader r{body};const auto streams=r.u32(),gains=r.u32(),curves=r.u32(),bindings=r.u32();
    require(streams && streams<=limits.max_resources && gains && gains<=limits.max_resources && curves && curves<=limits.max_curves &&
        bindings && bindings<=limits.max_bindings,"Audio voice bank declared counts exceed limits");
    r.available((std::uint64_t{streams}+gains)*5+std::uint64_t{curves}*10+std::uint64_t{bindings}*220+29);
    AudioVoiceBankV1 bank;auto& o=bank.observation;o.frame_stride=r.u32();o.first_frame_offset=r.u32();
    o.zero_observations_before_completion=r.u32();o.observations_after_completion_before_retire=r.u32();
    o.zero_observations_after_release=r.u32();o.schedule_order=static_cast<AudioVoiceScheduleOrderV1>(r.u32());bank.program_resource_id=r.string(limits.max_id_bytes);
    for(unsigned i=0;i<streams;++i)bank.stream_resource_ids.push_back(r.string(limits.max_id_bytes));
    for(unsigned i=0;i<gains;++i)bank.gain_resource_ids.push_back(r.string(limits.max_id_bytes));
    for(unsigned i=0;i<curves;++i){AudioPhaseCurveV1 curve;curve.first_control=r.i32();const auto count=r.u32();
        require(count && count<=limits.max_curve_values,"Audio phase curve count exceeds its limit");r.available(std::uint64_t{count}*2);
        curve.increments.reserve(count);for(unsigned j=0;j<count;++j)curve.increments.push_back(static_cast<std::uint16_t>(r.get(2)));
        bank.phase_curves.push_back(std::move(curve));}
    for(unsigned i=0;i<bindings;++i){AudioVoiceBindingV1 b;b.program_key=r.u32();b.voice_index=r.u32();b.stream_index=r.u32();b.gain_table_index=r.u32();b.phase_curve_index=r.u32();
        b.level=get_projection(r,limits);b.pan=get_projection(r,limits);b.phase=get_projection(r,limits);
        b.envelope.maximum_level=r.u32();b.envelope.counter_period=r.u32();
        for(auto& s:b.envelope.stages){s.counter_increment=r.u32();s.slow_counter_increment=r.u32();s.slow_above_level=r.u32();
            s.delta_multiplier=r.i32();s.delta_bias=r.i32();s.delta_denominator=r.u32();s.target=r.u32();s.decreasing=r.flag();}
        if(r.flag()){AudioReadAheadV1 a;a.input_samples=r.get(8);a.refill_samples=r.u32();a.refill_when_available_at_most=r.u32();
            a.required_lookahead_samples=r.u32();b.read_ahead=a;}bank.bindings.push_back(std::move(b));}
    require(r.at==body.size(),"Audio voice bank has an unowned tail");validate_audio_voice_bank_v1(bank,limits);return bank;
}
} // namespace openrc
