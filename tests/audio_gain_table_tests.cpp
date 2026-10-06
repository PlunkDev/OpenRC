#include "openrc/audio_gain_table.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <utility>

namespace {
using namespace openrc;
void check(bool good,const char* message) { if(!good)throw std::runtime_error(message); }
template<class F>void rejects(F&& call) {
    try{call();}catch(const AudioGainTableError&){return;}
    throw std::runtime_error("Invalid audio gain table accepted");
}
AudioGainTableV1 example() {
    AudioGainTableV1 table;table.state_count=3;table.level_count=2;table.pan_count=2;
    table.initial_state=1;table.gain_denominator=127;
    table.cells={{{10,-10},1},{{20,-20},2},{{30,-30},0},{{40,-40},1},
                 {{-1,2},2},{{3,-4},0},{{-5,-6},1},{{7,8},0},
                 {{9,-9},2},{{-11,11},1},{{12,0},0},{{0,-12},2}};
    return table;
}
void put(std::vector<std::byte>& bytes,std::size_t at,std::uint64_t value,unsigned width=4) {
    for(unsigned i=0;i<width;++i){bytes[at+i]=static_cast<std::byte>(value&255U);value>>=8;}
}
void rehash(std::vector<std::byte>& bytes) {
    const auto digest=prepared_content_sha256_v1(std::span(bytes).subspan(64));
    std::copy(digest.begin(),digest.end(),bytes.begin()+32);
}
void canonical_codec() {
    const auto table=example(), decoded=decode_audio_gain_table_v1(encode_audio_gain_table_v1(table));
    const auto bytes=encode_audio_gain_table_v1(table);
    check(decoded==table && encode_audio_gain_table_v1(decoded)==bytes,"Gain table canonical roundtrip differs");
    check(bytes.size()==240 && bytes[100]==std::byte{246} && bytes[103]==std::byte{255},
        "Gain table signed cells are not canonical little endian");
    auto exact=AudioGainTableLimitsV1{};
    exact.max_states=3;exact.max_levels=2;exact.max_pans=2;exact.max_cells=12;exact.max_bytes=240;
    check(decode_audio_gain_table_v1(bytes,exact)==table,"Exact gain table limits reject their endpoint");
    auto extreme=table;extreme.gain_denominator=1U<<30U;extreme.cells[0].gains={-1073741824,1073741824};
    check(decode_audio_gain_table_v1(encode_audio_gain_table_v1(extreme))==extreme,"Maximum signed gains changed in codec");
}
void shared_state_machine() {
    auto mutable_definition=example();
    AudioGainTableResourceV1 resource(mutable_definition);
    auto shared=resource;
    check(&resource.definition()==&shared.definition(),"Resource copy duplicated immutable cells");
    mutable_definition.cells[4].gains={123,123};
    AudioGainTablePlayerV1 first(resource), second(shared);
    check(first.state()==1 && first.gain_denominator()==127,"Player lost initial state or gain denominator");
    constexpr std::array<std::array<std::uint32_t,2>,6> inputs{{{0,0},{1,1},{0,1},{1,1},{1,0},{0,1}}};
    constexpr std::array<std::array<std::int32_t,2>,6> outputs{{{-1,2},{0,-12},{-11,11},{7,8},{30,-30},{20,-20}}};
    constexpr std::array<std::uint32_t,6> states{2,2,1,0,0,2};
    for(std::size_t i=0;i<inputs.size();++i) {
        check(first.step(inputs[i][0],inputs[i][1])==outputs[i] && first.state()==states[i],
            "State/level/pan addressing or retained history differs");
        check(second.state()==1,"Independent voice state changed through shared resource");
    }
    check(second.step(1,0)==std::array<std::int32_t,2>{-5,-6} && second.state()==1,
        "Second player lost its own trajectory");
    first.reset();check(first.state()==1 && first.step(0,0)==outputs[0],"Reset lost explicit initial state");
    for(const auto coordinate:std::array<std::array<std::uint32_t,2>,4>{{{2,0},{0,2},{UINT32_MAX,0},{0,UINT32_MAX}}}) {
        const auto before=first.state();
        rejects([&]{(void)first.step(coordinate[0],coordinate[1]);});
        check(first.state()==before,"Invalid table coordinates partly mutated retained state");
    }
    AudioGainTablePlayerV1 owned{AudioGainTableResourceV1(example())};
    check(owned.step(0,0)==outputs[0],"Player did not retain a temporary resource's lifetime");
}
void malformed_codec() {
    const auto bytes=encode_audio_gain_table_v1(example());
    for(std::size_t size=0;size<bytes.size();++size)
        rejects([&]{(void)decode_audio_gain_table_v1(std::span(bytes).first(size));});
    for(const auto field:{0U,8U,12U,16U,24U,32U,239U}) {
        auto bad=bytes;bad[field]^=std::byte{1};rejects([&]{(void)decode_audio_gain_table_v1(bad);});
    }
    for(const auto field:{64U,68U,72U,80U}) {
        auto bad=bytes;put(bad,field,0);rehash(bad);
        rejects([&]{(void)decode_audio_gain_table_v1(bad);});
    }
    for(const auto [field,value]:std::array<std::pair<unsigned,std::uint64_t>,8>{{
        {76,3},{80,(1U<<30U)+1U},{84,1},{88,11},{88,UINT64_MAX},
        {96,128},{100,0x80000000U},{236,3}}}) {
        auto bad=bytes;put(bad,field,value,field==88?8:4);rehash(bad);
        rejects([&]{(void)decode_audio_gain_table_v1(bad);});
    }
    for(const auto extra:{1U,12U}) {
        auto bad=bytes;bad.resize(bytes.size()+extra);put(bad,16,bad.size(),8);rehash(bad);
        rejects([&]{(void)decode_audio_gain_table_v1(bad);});
    }
    auto wide=AudioGainTableLimitsV1{};
    wide.max_bytes=UINT64_MAX;wide.max_cells=UINT64_MAX;
    wide.max_states=UINT32_MAX;wide.max_levels=UINT32_MAX;wide.max_pans=UINT32_MAX;
    auto bad=bytes;for(const auto field:{64U,68U,72U})put(bad,field,UINT32_MAX);
    put(bad,88,UINT64_MAX,8);rehash(bad);
    rejects([&]{(void)decode_audio_gain_table_v1(bad,wide);});
}
void admission_bounds() {
    const auto table=example();
    for(unsigned which=0;which<5;++which) {
        auto limits=AudioGainTableLimitsV1{};
        if(which==0)limits.max_states=2;
        if(which==1)limits.max_levels=1;
        if(which==2)limits.max_pans=1;
        if(which==3)limits.max_cells=11;
        if(which==4)limits.max_bytes=239;
        rejects([&]{validate_audio_gain_table_v1(table,limits);});
    }
    auto invalid=table;invalid.cells.pop_back();
    rejects([&]{AudioGainTableResourceV1 resource(invalid);});
    for(const auto gain:{-128,128,INT32_MIN,INT32_MAX}) {
        invalid=table;invalid.cells.back().gains[1]=gain;
        rejects([&]{AudioGainTableResourceV1 resource(invalid);});
    }
    invalid=table;invalid.cells.back().next_state=3;
    rejects([&]{AudioGainTableResourceV1 resource(invalid);});
    auto tiny=AudioGainTableV1{1,1,1,0,1,{{{-1,1},0}}};
    AudioGainTablePlayerV1 player{AudioGainTableResourceV1(tiny)};
    check(player.step(0,0)==std::array<std::int32_t,2>{-1,1} && player.state()==0,
        "Single-state signed table domain is not admitted");
}
} // namespace
int main()try {
    canonical_codec();shared_state_machine();malformed_codec();admission_bounds();
    std::cout<<"4 neutral gain table groups PASS: canonical signed codec, shared immutable resource/independent states, atomic steps, untrusted partitions and numeric limits\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
