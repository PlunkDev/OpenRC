#include "openrc/rac_frontend_list_draw.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace openrc;
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using Kind = RacFrontendListEffectKindV1;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F call) {
  try { call(); } catch(const std::runtime_error &) { return; }
  throw std::runtime_error("Invalid original list draw was accepted");
}
std::vector<std::byte> bytes(const std::string &text) {
  std::vector<std::byte> out; for(unsigned char c:text) out.push_back(static_cast<std::byte>(c)); return out;
}
struct Fixture {
  RacFrontendListDrawInputsV1 in;
  RacFontMetricTablesV1 fonts;
  RacTextBankV1 bank;
  std::array<RacFrontendTextureEntryV1,4> textures;
  std::vector<RacFrontendListRowV1> rows;
  Fixture() {
    in.width_word=120; in.height_word=60; in.flags=4; in.focused=true;
    in.shadow_x_y_words={1,2}; in.screen_offset_y_x_words={0x8000,0x7800};
    in.timer_argument_word=10; in.time_scale_bits=0x3f800000;
    in.palette[1]=0xffaabbcc;
    rows={{101,4,0,102,0},{103,0,0,0,10},{}};
    for(const auto &[key,text]:std::array<std::pair<U32,const char *>,4>{{{101,"AB"},{102,"CD"},{103,"E"},{20308,"LOCK"}}}) {
      RacTextBankEntryV1 row; row.key=key; row.text_bytes=bytes(text); bank.entries.push_back(row);
    }
    for(auto &font:fonts.tables) for(std::size_t i=0;i<font.rows.size();++i)
      font.rows[i]={static_cast<std::uint8_t>(i),16,0,static_cast<std::int8_t>(i>=8 && i<16 ? 0 : 4)};
    for(U32 i=0;i<textures.size();++i) {
      textures[i].source_index=i; textures[i].width=textures[i].height=128;
      textures[i].palette_offset=0; textures[i].pixel_offset=1024;
    }
    in.text_bank=&bank; in.font_metrics=&fonts; in.textures=textures;
    in.texture_payload={0x600000,0x10000}; in.allocator_begin=0x100000;
  }
  RacFrontendListDrawPlanV1 run() { in.rows=rows; return execute_rac_frontend_list_draw_v1(in); }
};
void original_row_order_and_colors() {
  Fixture f; auto out=f.run();
  check(out.returned_word==2 && out.batch_ended && out.font_source_id==3 && out.glyph_calls.size()==6,
        "Original list completion/font/secondary order differs");
  const auto &shadow=out.glyph_calls[0].plan.draws[0].arguments;
  const auto &main=out.glyph_calls[1].plan.draws[0].arguments;
  check(shadow.rectangle_words==std::array<U32,4>{57,14,16,16} &&
        main.rectangle_words==std::array<U32,4>{56,12,16,16} &&
        shadow.rgbaq==0x80000000 && main.rgbaq==0x80ffa888,
        "Original row spacing/shadow/timed color differs");
  check(out.glyph_calls[2].secondary && out.glyph_calls[2].shadow &&
        out.glyph_calls[3].plan.draws[0].arguments.rectangle_words[1]==32 &&
        out.glyph_calls[5].plan.draws[0].arguments.rgbaq==UINT64_C(0xffffffff80303030),
        "Secondary line or inactive action color differs");
  check(out.bindings.bindings.size()==6 && out.bindings.uploads.size()==1 &&
        out.final_inline_colors_enabled && out.final_palette[0]==0x80303030,
        "Original cached font bindings/palette restoration differ");
  std::vector<U32> lookups;
  for(const auto &e:out.effects) if(e.kind==Kind::lookup) lookups.push_back(e.source_call_pc);
  check(lookups==std::vector<U32>{0x21c46c,0x21c5b0,0x21c608,0x21c46c},
        "Secondary source text was not looked up twice");
  f.in.selected_row=1; out=f.run();
  check(out.glyph_calls.back().plan.draws[0].arguments.rgbaq==UINT64_C(0xffffffff80006060),
        "Focused unavailable source row color differs");
}
void flags_and_retry() {
  Fixture f; f.in.flags=0x20000|0x4000|4; f.in.width_word=13;
  auto out=f.run();
  check(out.returned_word==1 && !out.batch_ended && out.glyph_calls.empty() &&
        out.final_flags==(f.in.flags|8) && out.effects.back().kind==Kind::node_flags_write,
        "Font retry fabricated end/draw or lost its flags write");
  f.in.flags|=8; out=f.run();
  check(out.font_source_id==2 && out.returned_word==2 && out.batch_ended,
        "Source small-font retry did not complete");
  f.in.flags=4|8|16|0x40|0x4000|0x80|2; f.in.width_word=120;
  f.bank.entries[0].text_bytes=bytes(std::string("\x09")+"AB"); out=f.run();
  check(out.glyph_calls[1].plan.draws[0].arguments.rectangle_words==std::array<U32,4>{4,7,16,16} &&
        out.glyph_calls[1].plan.draws[0].arguments.rgbaq==UINT64_C(0xffffffff80ffa888) &&
        out.final_inline_colors_enabled,
        "Font override/left precedence/fixed spacing/disabled inline color differs");
  f.in.flags=0x4000; f.rows[0].action=2; out=f.run();
  check(out.glyph_calls[1].text==bytes(std::string("LOCK\0",5)) &&
        out.glyph_calls[1].plan.draws[0].arguments.rectangle_words[0]==56,
        "Action2 replacement changed original maximum-width alignment");
  f.rows={{}}; f.in.flags=0; f.in.inline_colors_enabled=false; out=f.run();
  check(out.batch_ended && out.glyph_calls.empty() && !out.final_inline_colors_enabled,
        "Empty list changed live color controls");
}
void qualified_color_and_bounds() {
  Fixture f; f.rows[0].color_age=1;
  auto out=f.run();
  // Independent integer DIV/MUL/ACC reference: factor3dccccd0 and
  // inverse3f666666 produce truncated RGBA lanes147,176,232,127. The
  // alpha lane intentionally catches a host lerp/constant-alpha substitute.
  check(out.glyph_calls[1].plan.draws[0].arguments.rgbaq==0x7fe8b093,
        "Non-dyadic source timed color did not execute its ordered arithmetic");
  const auto color=std::find_if(out.effects.begin(),out.effects.end(),
    [](const auto &effect){return effect.kind==Kind::timed_color;});
  check(color!=out.effects.end() && color->evaluated && color->value==0x7fe8b093,
        "Executed source timed color was mislabeled as an observed result");
  const std::array<RacFrontendListColorObservationV1,1> observed{{{0,1,10,0x3f800000,0x7fe8b093}}};
  f.in.color_observations=observed;
  rejects([&]{(void)f.run();});
  f.rows[0].color_age=0; rejects([&]{(void)f.run();}); f.in.color_observations={};
  f.in.max_total_draws=1; rejects([&]{(void)f.run();}); f.in.max_total_draws=131072;
  f.in.max_total_text_bytes=2; rejects([&]{(void)f.run();}); f.in.max_total_text_bytes=4194304;
  f.in.max_lookup_row_visits=0; rejects([&]{(void)f.run();}); f.in.max_lookup_row_visits=1048576;
  f.rows.pop_back(); rejects([&]{(void)f.run();}); f.rows.push_back({});
  f.rows[0].text_key=-1; rejects([&]{(void)f.run();});
  const auto fallback=bytes(std::string("MISS\0",5)); f.in.missing_text=fallback; out=f.run();
  check(out.glyph_calls[0].text==fallback,"Original lookup miss did not retain supplied fallback");
}

void source_corpus(const char *path) {
  std::ifstream file(path,std::ios::binary);
  const auto read=[&](unsigned count) { U64 value=0; for(unsigned i=0;i<count;++i) {
    int c=file.get(); check(c>=0,"Truncated list source corpus"); value|=U64(c)<<(i*8U); } return value; };
  check(read(4)==0x3154534c,"Wrong list source corpus"); const auto count=read(4);
  check(count && count<=10000,"Unbounded source list cases"); U64 total_draws=0;
  for(U64 case_id=0;case_id<count;++case_id) {
    Fixture f;
    f.in.width_word=static_cast<U32>(read(4)); f.in.height_word=static_cast<U32>(read(4));
    f.in.flags=static_cast<U32>(read(4)); f.in.selected_row=static_cast<U32>(read(4)); f.in.focused=read(4)!=0;
    for(auto &v:f.in.shadow_x_y_words) v=static_cast<U32>(read(4));
    for(auto &v:f.in.screen_offset_y_x_words) v=static_cast<U32>(read(4));
    for(auto &v:f.in.palette) v=static_cast<U32>(read(4));
    f.in.inline_colors_enabled=read(4)!=0; f.in.preserve_palette_zero=read(4)!=0;
    std::array<std::byte,kRacFontMetricTablesBytesV1> raw{}; for(auto &v:raw) v=static_cast<std::byte>(read(1));
    f.fonts=parse_rac_font_metric_tables_v1(raw);
    const auto row_count=read(4); check(row_count<=1024,"Unbounded source list rows"); f.rows.clear();
    for(U64 i=0;i<row_count;++i) {
      RacFrontendListRowV1 row;
      row.text_key=std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(read(2)));
      row.action=std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(read(2))); row.target_screen=static_cast<U32>(read(4));
      row.secondary_text_key=std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(read(2)));
      row.color_age=std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(read(2))); f.rows.push_back(row);
    }
    f.bank.entries.clear(); const auto banks=read(4); check(banks<=1024,"Unbounded source bank");
    for(U64 i=0;i<banks;++i) {
      RacTextBankEntryV1 row; row.key=static_cast<U32>(read(4)); const auto n=read(4); check(n<=65536,"Unbounded source text");
      for(U64 j=0;j<n;++j) row.text_bytes.push_back(static_cast<std::byte>(read(1))); f.bank.entries.push_back(row);
    }
    const auto out=f.run();
    check(out.final_flags==read(4) && out.returned_word==read(4) && out.batch_ended==(read(4)!=0) &&
          out.final_inline_colors_enabled==(read(4)!=0),"Original list final state differs");
    for(auto v:out.final_palette) check(v==read(4),"Original list final palette differs");
    const auto glyphs=read(4); check(glyphs==out.glyph_calls.size(),"Original list glyph count differs");
    for(const auto &call:out.glyph_calls) {
      const auto n=read(4); check(n==call.plan.draws.size(),"Original list quad count differs"); total_draws+=n;
      for(const auto &draw:call.plan.draws) for(auto byte:draw.emission.packet)
        check(std::to_integer<unsigned>(byte)==read(1),"Original list full quad bytes differ");
    }
    std::vector<const RacFrontendListEffectV1 *> lookups;
    for(const auto &e:out.effects) if(e.kind==Kind::lookup) lookups.push_back(&e);
    check(read(4)==lookups.size(),"Original list lookup count differs");
    for(auto e:lookups) check(e->source_call_pc==read(4) && e->value==read(8),"Original list lookup order differs");
  }
  check(file.get()==EOF,"Trailing list source corpus bytes");
  std::cout<<"list original source: cases="<<count<<" quads="<<total_draws<<'\n';
}
} // namespace
int main(int argc,char **argv) {
  try {
    original_row_order_and_colors(); flags_and_retry(); qualified_color_and_bounds();
    if(argc==2) source_corpus(argv[1]); else check(argc==1,"Unexpected list corpus argument");
    std::cout<<"Original frontend list draw tests passed\n"; return 0;
  } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
