#include "openrc/rac_frontend_new_game.hpp"
#include "openrc/rac_frontend_owner.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace openrc;
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using Bytes = std::vector<std::byte>;
using NK = RacFrontendNewGameNumericKindV1;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F &&fn) {
  bool caught = false;
  try {
    fn();
  } catch (const std::runtime_error &) {
    caught = true;
  }
  check(caught, "Malformed callback request was accepted");
}
U64 sx(U32 word) {
  return static_cast<U64>(
      static_cast<std::int64_t>(std::bit_cast<std::int32_t>(word)));
}
void put(std::span<std::byte> bytes, std::size_t offset, U64 value,
         std::size_t width = 4U) {
  for (std::size_t i = 0U; i < width; ++i)
    bytes[offset + i] = static_cast<std::byte>(value >> (8U * i));
}
U32 word(std::span<const std::byte> bytes, std::size_t at) {
  U32 value = 0U;
  for (unsigned i = 0U; i < 4U; ++i)
    value |= std::to_integer<U32>(bytes[at + i]) << (i * 8U);
  return value;
}
Bytes bytes(std::string text, bool nul = true) {
  Bytes out;
  for (unsigned char byte : text)
    out.push_back(static_cast<std::byte>(byte));
  if (nul)
    out.push_back(std::byte{0U});
  return out;
}

struct Fixture {
  std::vector<std::pair<U32, Bytes>> storage;
  std::vector<RacFrontendNewGameSourceV1> sources;
  std::vector<RacFrontendNewGameNumericV1> observations;
  RacFontMetricTablesV1 fonts;
  RacTextBankV1 bank;
  std::vector<U32> text_addresses;
  std::array<RacFrontendTextureEntryV1, 4U> textures;
  RacFrontendNewGameInputsV1 in;
  Fixture() {
    in.node_address = 0x200000U;
    put(in.node_bytes, 0x20U, 120U);
    put(in.node_bytes, 0x24U, 80U);
    put(in.node_bytes, 0x30U, 0xfU);
    put(in.node_bytes, 0x34U, 101U);
    put(in.node_bytes, 0x38U, 8U);
    put(in.node_bytes, 0x44U, 10U);
    storage = {{0x160000U, Bytes(1024U)},
               {0x15ee00U, Bytes(2048U)},
               {0x18cbf8U, Bytes(32U)},
               {0x13e600U, Bytes(8U)},
               {0x13d400U, Bytes(768U, std::byte{1U})},
               {0x13e620U, Bytes(128U)},
               {0x13cbe0U, Bytes(4U)},
               {0x199a68U, bytes("MISSING")},
               {0x1a0414U, Bytes(4U)},
               {0x1d5f74U, Bytes(4U)},
               {0x300000U, Bytes(80U)},
               {0x310000U, Bytes(80U)},
               {0x320000U, Bytes(128U)},
               {0x199810U, Bytes(600U)}};
    memory(0x1602b0U, 0x81234567U);
    memory(0x1602b4U, 10U);
    memory(0x15ee68U, 0x3f800000U);
    memory(0x1602b8U, 1U);
    memory(0x1602bcU, 2U);
    memory(0x160358U, 4U);
    memory(0x160368U, 16U);
    memory(0x15f59cU, 1U);
    memory(0x13e600U, 512U);
    memory(0x13e604U, 416U);
    memory(0x1d5f74U, 0x300000U);
    memory(0x300040U, 0x310000U);
    memory(0x310034U, 0x320000U);
    memory(0x310048U, 0x320000U);
    literal(0x160370U, "DEFAULT");
    literal(0x160378U, "");
    literal(0x160380U, "%s %s");
    literal(0x160388U, "NULL");
    for (U32 i = 0U; i < 8U; ++i)
      memory(0x18cbf8U + i * 4U, 0x81000000U + i * 0x111111U);
    entry(101U, "AB");
    entry(102U, "CD");
    entry(20172U, "PRE");
    entry(20308U, "LOCK");
    for (auto &font : fonts.tables)
      for (std::size_t i = 0U; i < font.rows.size(); ++i)
        font.rows[i] = {static_cast<std::uint8_t>(i), 16U, -1,
                        static_cast<std::int8_t>(i < 16U ? 0 : 4)};
    for (U32 i = 0U; i < textures.size(); ++i) {
      textures[i].source_index = i;
      textures[i].width = textures[i].height = 128U;
      textures[i].palette_offset = 0U;
      textures[i].pixel_offset = 1024U;
    }
    in.text_bank = &bank;
    in.font_metrics = &fonts;
    in.textures = textures;
    in.texture_payload = {0x600000U, 0x10000U};
    in.allocator_begin = 0x100000U;
    timer(0x21b36cU, 10U);
    color(13U);
  }
  void memory(U32 address, U32 value, U32 width = 4U) {
    for (auto &[base, data] : storage)
      if (address >= base &&
          static_cast<U64>(address) + width <= base + data.size()) {
        put(data, address - base, value, width);
        return;
      }
    throw std::runtime_error("Test source address unavailable");
  }
  void literal(U32 address, const std::string &text) {
    const auto value = bytes(text);
    for (std::size_t i = 0U; i < value.size(); ++i)
      memory(address + static_cast<U32>(i), std::to_integer<U32>(value[i]), 1U);
  }
  void entry(U32 key, const std::string &text) {
    const auto address =
        0x400000U + static_cast<U32>(bank.entries.size()) * 0x1000U;
    RacTextBankEntryV1 row;
    row.key = key;
    row.text_bytes = bytes(text, false);
    bank.entries.push_back(row);
    text_addresses.push_back(address);
    storage.emplace_back(address, bytes(text));
  }
  void replace_text(std::size_t row, std::string value) {
    bank.entries[row].text_bytes = bytes(value, false);
    for (auto &[base, data] : storage)
      if (base == text_addresses[row])
        data = bytes(value);
  }
  void timer(U32 pc, U32 result) {
    observations.push_back(
        {NK::timer_1f98c0, pc, {10U, 0U, 0U}, 0U, sx(result)});
  }
  void color(U32 timer_value) {
    observations.push_back({NK::timed_color_21c6c0,
                            0x21b828U,
                            {sx(timer_value), 0x80917677ULL, sx(0x80ffa888U)},
                            0U,
                            *plan_rac_frontend_timed_color_v1(timer_value,
                                0x80917677ULL, sx(0x80ffa888U),10U,0x3f800000U).returned_low64});
  }
  RacFrontendNewGamePlanV1 run() {
    sources.clear();
    for (const auto &[address, data] : storage)
      sources.push_back({address, data});
    in.source = sources;
    in.numeric_observations = observations;
    in.relocated_text_addresses = text_addresses;
    return plan_rac_frontend_new_game_v1(in);
  }
};
std::vector<const RacFrontendNewGameLayoutV1 *>
layouts(const RacFrontendNewGamePlanV1 &plan) {
  std::vector<const RacFrontendNewGameLayoutV1 *> result;
  for (const auto &effect : plan.effects)
    if (auto *value = std::get_if<RacFrontendNewGameLayoutV1>(&effect))
      result.push_back(value);
  return result;
}
void basic_flow() {
  Fixture f;
  const auto plan = f.run();
  const auto calls = layouts(plan);
  check(plan.batch_reached && plan.returned_word == 2U &&
            plan.font_source_id == 3U,
        "Original font3/main return not selected");
  check(calls.size() == 3U && calls[0]->source_call_pc == 0x21b854U &&
            calls[1]->source_call_pc == 0x21b94cU &&
            calls[2]->source_call_pc == 0x21b9d4U,
        "Measure/shadow/main order changed");
  check(calls[0]->request.box.flags == 15U && calls[0]->glyph_lines.empty() &&
            calls[1]->request.box.flags == 11U &&
            !calls[1]->request.inline_colors_enabled &&
            calls[2]->request.inline_colors_enabled,
        "Source layout/color modes changed");
  check(calls[1]->request.rgbaq == 0x80000000U &&
            calls[2]->request.rgbaq == 0x80ffa888ULL,
        "Timed-color pack or source shadow zero-extension changed");
  check(calls[2]->glyph_lines.size() == 1U &&
            calls[2]->glyph_lines[0].program.draw_count == 2U,
        "Reached original float glyph owner was not composed");
  check(word(plan.final_node_bytes, 0x44U) == 13U &&
            plan.bindings.bindings.size() == 1U,
        "Source timer write or cold binding missing");
  check(word(plan.preamble_packets, 32U) == 0x44U &&
            word(plan.preamble_packets, 80U) == 0x2004bU,
        "Complete early preamble bytes missing");
  std::size_t mix_at = plan.effects.size(), timed_at = mix_at;
  for (std::size_t i = 0U; i < plan.effects.size(); ++i) {
    if (const auto *mix =
            std::get_if<RacFrontendNewGameHalfMixV1>(&plan.effects[i])) {
      check(mix->source_call_pc == 0x21b814U &&
                mix->left_color == sx(0x81234567U) &&
                mix->right_color == sx(0x80ffa888U) &&
                mix->returned_low64 == 0x80917677ULL,
            "Reached constant-half source mix was not evaluated");
      mix_at = i;
    } else if (const auto *numeric =
                   std::get_if<RacFrontendNewGameNumericV1>(&plan.effects[i])) {
      if (numeric->kind == NK::timed_color_21c6c0)
        timed_at = i;
      check(numeric->evaluated,
            "Exact timer/saturated timed color was not executed");
    }
  }
  check(mix_at + 1U == timed_at,
        "Evaluated source mix no longer directly precedes timed color");
}

void exact_timer_and_timed_color() {
  for (const auto argument : {-8388608, -4000, -1, 0, 1, 10, 8388607}) {
    const auto expected = argument < 0 ? argument + 1 : argument;
    check(evaluate_rac_frontend_timer_v1(static_cast<U32>(argument),
                                          0x3f800000U) ==
              sx(static_cast<U32>(expected)),
          "Source exact timer lost offset, truncation or MFC1 sign extension");
  }
  check(evaluate_rac_frontend_timer_v1(8388608U, 0x3f800000U)==8388608U &&
        evaluate_rac_frontend_timer_v1(10U, 0x3f800001U)==10U,
        "General source timer reference retained an obsolete unit-domain gate");
  check(evaluate_rac_frontend_timer_v1(180U,0x3f555555U)==150U &&
        evaluate_rac_frontend_timer_v1(12U,0x3f555555U)==10U &&
        evaluate_rac_frontend_timer_v1(10U,0x3f555555U)==8U &&
        evaluate_rac_frontend_timer_v1(static_cast<U32>(-180),0x3f555555U)==sx(static_cast<U32>(-149)),
        "Original PAL5/6 timer lost source rounding/truncation");
  // Complete byte-pair domain at nine exact phases. The expectation is a
  // quotient of integer color units; no production numeric helper is called.
  for (U32 left = 0U; left < 256U; ++left)
    for (U32 right = 0U; right < 256U; ++right)
      for (U32 age = 0U; age <= 8U; ++age) {
        const auto plan = plan_rac_frontend_timed_color_v1(
            age, left * 0x01010101ULL, right * 0x01010101ULL, 8U,
            0x3f800000U);
        const auto expected = (left * (8U - age) + right * age) / 8U;
        check(plan.returned_low64 == expected * 0x01010101ULL &&
                  plan.timer_calls == 3U && plan.division_numerator == 8U - age &&
                  plan.division_denominator == 8U,
              "Exact dyadic source color or timed operand order differs");
      }
  const auto sentinel = plan_rac_frontend_timed_color_v1(
      20U, ~U64{0U}, ~U64{0U}, 10U, 0x3f800000U);
  check(sentinel.timer_calls == 1U && !sentinel.division_numerator &&
            sentinel.factor_bits == 0x3f800000U &&
            sentinel.returned_low64 == 0x8020ffffULL,
        "Source over-duration branch or full64 sentinel differs");
  const auto negative = plan_rac_frontend_timed_color_v1(
      0xffffffffU, 0xffffffffULL, ~U64{0U}, 10U, 0x3f800000U);
  check(negative.clamped_age == 0U && negative.factor_bits == 0U &&
            negative.returned_low64 == 0xffffffffULL,
        "Signed clamp or zero-extended non-sentinel color differs");
  const auto non_dyadic = plan_rac_frontend_timed_color_v1(
      3U, 0x80875848U, 0x80ffa888U, 10U, 0x3f800000U);
  check(non_dyadic.timer_calls == 3U && non_dyadic.division_numerator == 7U &&
            non_dyadic.division_denominator == 10U && non_dyadic.factor_bits==0x3e99999aU &&
            non_dyadic.returned_low64==0x7faa705bU,
        "Source non-dyadic DIV/MADD lost its ordered rounding, including alpha127");
  const auto zero = plan_rac_frontend_timed_color_v1(
      0U, 0U, 0U, 0U, 0x3f800000U);
  check(zero.division_numerator == 0U && zero.division_denominator == 0U &&
            zero.returned_low64==0U,
        "Original DIV(0,0) path did not execute through the reference mixer");
  Fixture executable;
  executable.observations.clear();
  const auto callback = executable.run();
  check(callback.returned_word == 2U && callback.timed_color &&
            callback.timed_color->returned_low64 == 0x80ffa888ULL,
        "Callback still requires timer/timed observations for an exact path");
  Fixture missing;
  put(missing.in.node_bytes, 0x30U, 0x80U);
  put(missing.in.node_bytes, 0x34U, 0x320020U);
  missing.memory(0x310040U, 1U);
  missing.memory(0x320020U, 101U);
  missing.observations.clear();
  const auto formerly_pending=missing.run();
  check(formerly_pending.timed_color&&formerly_pending.timed_color->returned_low64.has_value(),
        "Non-dyadic callback still requires a supplied numeric observation");
}

void constant_half_mix_exact_domain() {
  for (U32 a = 0U; a < 256U; ++a)
    for (U32 b = 0U; b < 256U; ++b) {
      const std::array<U32, 4U> left{a, 255U - a, b, 255U - b};
      const std::array<U32, 4U> right{b, 255U - b, a, 255U - a};
      U64 packed_left = 0U, packed_right = 0U, expected = 0U;
      for (unsigned lane = 0U; lane < 4U; ++lane) {
        packed_left |= static_cast<U64>(left[lane]) << (8U * lane);
        packed_right |= static_cast<U64>(right[lane]) << (8U * lane);
        // Independent carry-free identity, not the production sum/shift.
        const auto average =
            (left[lane] & right[lane]) + ((left[lane] ^ right[lane]) >> 1U);
        expected |= static_cast<U64>(average) << (8U * lane);
      }
      check(mix_rac_frontend_color_half_v1(packed_left, packed_right) ==
                expected,
            "Exact half mix failed exhaustive byte-pair domain");
      check(mix_rac_frontend_color_half_v1(
                packed_left | (static_cast<U64>(a * 0x01010101U) << 32U),
                packed_right | (static_cast<U64>(b * 0x01010101U) << 32U)) ==
                expected,
            "Source unpack or return pack retained high-word padding");
    }
  check(
      mix_rac_frontend_color_half_v1(0xffffffffffffffffULL,
                                     0xffffffffffffffffULL) == 0xffffffffULL &&
          mix_rac_frontend_color_half_v1(
              0xff000000ff000000ULL, 0x8000000080000000ULL) == 0xbf000000ULL &&
          mix_rac_frontend_color_half_v1(0xffffffffffffffffULL, 0U) ==
              0x7f7f7f7fULL,
      "Packed result must zero-extend, truncate odd halves and mix raw alpha");
}
void early_returns_and_remap() {
  Fixture f;
  put(f.in.node_bytes, 0x34U, 0U);
  f.observations.resize(1U);
  f.in.font_metrics = nullptr;
  auto plan = f.run();
  check(!plan.batch_reached && plan.returned_word == 1U &&
            layouts(plan).empty(),
        "Zero direct key did not return before begin");
  Fixture map;
  put(map.in.node_bytes, 0x30U, 0x1000U);
  map.memory(0x320000U, 77U, 2U);
  map.observations.clear();
  plan = map.run();
  check(!plan.batch_reached && word(plan.final_node_bytes, 0x34U) == 0xffffU,
        "Missing150-row remap did not take original early return");
  map.memory(0x199810U + 9U * 4U, 102U, 2U);
  map.memory(0x199812U + 9U * 4U, 77U, 2U);
  map.color(13U);
  plan = map.run();
  check(plan.batch_reached && word(plan.final_node_bytes, 0x34U) == 102U &&
            layouts(plan)[0]->text == bytes("CD"),
        "Original remap halfword write/lookup differs");
}
void all_selection_families() {
  for (const U32 flags : {0x20U, 0x40U, 0x80U, 0x100U, 0U}) {
    Fixture f;
    put(f.in.node_bytes, 0x30U, flags);
    put(f.in.node_bytes, 0x34U, 0x320020U);
    f.memory(0x15ee84U, 1U);
    f.memory(0x1a0414U, 1U);
    f.memory(0x320020U, 102U);
    f.observations.clear();
    f.color(13U);
    const auto plan = f.run();
    check(layouts(plan)[0]->text == bytes("CD"),
          "One selector family failed source-key dispatch");
  }
  Fixture f;
  put(f.in.node_bytes, 0x30U, 0x8080U);
  put(f.in.node_bytes, 0x34U, 0x320020U);
  f.memory(0x15ee80U, 1U);
  f.memory(0x320024U, 102U);
  f.observations.clear();
  f.color(13U);
  check(layouts(f.run())[0]->text == bytes("CD"),
        "Alternate pointer word was not selected");
}
void timer_transition_and_font_override() {
  Fixture f;
  put(f.in.node_bytes, 0x30U, 0x98U);
  put(f.in.node_bytes, 0x34U, 0x320020U);
  put(f.in.node_bytes, 0x44U, 2U);
  put(f.in.node_bytes, 0x3cU, 37U);
  f.memory(0x310040U, 1U);
  f.memory(0x320028U, 102U);
  f.observations.clear();
  f.timer(0x21b4d4U, 10U);
  f.color(0U);
  const auto plan = f.run();
  check(plan.font_source_id == 2U && word(plan.final_node_bytes, 0x48U) == 1U &&
            word(plan.final_node_bytes, 0x44U) == 0U &&
            word(plan.final_node_bytes, 0x3cU) == 0U,
        "Source transition countdown/reset or font override changed");
  Fixture retain;
  put(retain.in.node_bytes, 0x30U, 0x80U);
  put(retain.in.node_bytes, 0x34U, 0x320020U);
  retain.memory(0x310040U, 1U);
  retain.memory(0x320020U, 101U);
  retain.memory(0x320028U, 102U);
  retain.observations.clear();
  retain.timer(0x21b4d4U, 10U);
  retain.color(7U);
  check(layouts(retain.run())[0]->text == bytes("AB"),
        "Positive countdown did not retain old selection");
}
void text_fallbacks_and_format() {
  Fixture f;
  put(f.in.node_bytes, 0x34U, 777U);
  check(layouts(f.run())[0]->text == bytes("MISSING"),
        "Missing-key source literal lost");
  Fixture null;
  null.text_addresses[0] = 0U;
  check(layouts(null.run())[0]->text == bytes("NULL"),
        "Actual zero relocated pointer fallback lost");
  Fixture empty;
  put(empty.in.node_bytes, 0x30U, 0x100U);
  empty.memory(0x13d5c8U, 0U, 1U);
  empty.observations.clear();
  empty.timer(0x21b4d4U, 10U);
  // Keep previous selector at -1 to take the empty selection directly.
  put(empty.in.node_bytes, 0x48U, 0xffffffffU);
  empty.observations.clear();
  empty.color(13U);
  check(layouts(empty.run())[0]->text == bytes(""),
        "Unavailable original selection was not empty");
  Fixture fmt;
  put(fmt.in.node_bytes, 0x30U, 0x280U);
  put(fmt.in.node_bytes, 0x34U, 0x320020U);
  fmt.memory(0x320020U, 101U);
  fmt.observations.clear();
  fmt.color(13U);
  check(layouts(fmt.run())[0]->text == bytes("PRE AB"),
        "Original two-string formatting failed");
  fmt.replace_text(0U, std::string(60U, 'A'));
  rejects([&] { (void)fmt.run(); });
}
void scrolling_and_controls() {
  Fixture f;
  put(f.in.node_bytes, 0x24U, 12U);
  put(f.in.node_bytes, 0x30U, 0x2000fU);
  f.replace_text(0U, std::string("A") + char(9) + "B");
  const auto plan = f.run();
  const auto calls = layouts(plan);
  check(calls.size() == 5U && calls[3]->source_call_pc == 0x21ba84U &&
            calls[4]->source_call_pc == 0x21baf8U,
        "Original scrolling duplicate passes missing");
  check(!calls[2]->request.inline_colors_enabled &&
            calls[4]->request.inline_colors_enabled,
        "Main-only suppression incorrectly copied to scrolling repeat");
  check((word(plan.final_node_bytes, 0x30U) & 0x400U) &&
            word(plan.final_node_bytes, 0x3cU) == static_cast<U32>(-93),
        "Original scroll advance/remainder changed");
  Fixture forced;
  put(forced.in.node_bytes, 0x24U, 12U);
  put(forced.in.node_bytes, 0x30U, 0x240fU);
  put(forced.in.node_bytes, 0x3cU, 12U);
  const auto p = forced.run();
  check(layouts(p).size() == 3U &&
            !(word(p.final_node_bytes, 0x30U) & 0x400U) &&
            word(p.final_node_bytes, 0x3cU) == 0U,
        "Source no-scroll flag did not reset scrolling state");
}
void observation_and_ownership_failures() {
  const auto rejected = [](const char *name, auto &&call) {
    try { call(); } catch (const std::runtime_error &) { return; }
    throw std::runtime_error(std::string("Malformed callback accepted: ") + name);
  };
  Fixture f;
  f.observations[0].source_call_pc += 4U;
  rejected("observation call PC", [&] { (void)f.run(); });
  Fixture arg;
  arg.observations[0].arguments[0] ^= 1U;
  rejected("observation arguments", [&] { (void)arg.run(); });
  Fixture mixed;
  mixed.observations.back().arguments[1] |= 0x100000000ULL;
  rejected("packed color argument upper word", [&] { (void)mixed.run(); });
  Fixture obsolete;
  obsolete.observations.insert(obsolete.observations.begin() + 1U,
                               obsolete.observations.back());
  obsolete.observations[1U].source_call_pc = 0x21b814U;
  rejected("obsolete half-mix observation", [&] { (void)obsolete.run(); });
  Fixture unused;
  unused.observations.push_back(unused.observations.back());
  rejected("unused observation", [&] { (void)unused.run(); });
  Fixture timer;
  timer.observations[0].returned_low64 = 0x100000000ULL;
  rejected("timer return upper word", [&] { (void)timer.run(); });
  Fixture timed_color;
  timed_color.observations.back().returned_low64 = 0xffffffff83456789ULL;
  rejected("timed color return upper word", [&] { (void)timed_color.run(); });
  Fixture alias;
  alias.storage.emplace_back(0x1602b0U, Bytes(4U));
  rejected("overlapping source owners", [&] { (void)alias.run(); });
  Fixture mismatch;
  mismatch.bank.entries[0].text_bytes[0] = std::byte{'Z'};
  rejected("text bank ownership mismatch", [&] { (void)mismatch.run(); });
  Fixture effects;
  effects.in.limits.max_effects = 1U;
  rejected("effect limit", [&] { (void)effects.run(); });
  Fixture glyph;
  glyph.in.limits.max_total_glyph_operations = 0U;
  rejected("glyph limit", [&] { (void)glyph.run(); });
  Fixture terminator;
  for (auto &[address, data] : terminator.storage)
    if (address == terminator.text_addresses[0])
      data.pop_back();
  terminator.storage.emplace_back(terminator.text_addresses[0] + 2U, Bytes(1U));
  rejected("split terminator owner", [&] { (void)terminator.run(); });
  Fixture delay;
  put(delay.in.node_bytes, 0x34U, 0U);
  delay.observations.resize(1U);
  std::erase_if(delay.storage,
                [](const auto &region) { return region.first == 0x15ee00U; });
  // Even flag4/key0 must own the flag20 branch's unconditional delay-slot LW.
  rejected("unconditional selection delay-slot owner", [&] { (void)delay.run(); });
  Fixture spacing;
  for (std::size_t i = 0U; i < spacing.storage.size(); ++i)
    if (spacing.storage[i].first == 0x160000U) {
      const auto original = spacing.storage[i].second;
      spacing.storage[i].second.assign(original.begin(),
                                       original.begin() + 0x36aU);
      spacing.storage.emplace_back(
          0x16036cU, Bytes(original.begin() + 0x36cU, original.end()));
      break;
    }
  // The earlier LHU line spacing remains owned, but the unconditional LW
  // after main drawing must not read through an unowned upper halfword.
  rejected("unconditional spacing upper-half owner", [&] { (void)spacing.run(); });
}

void callback_return_drives_original_rtt_owner() {
  for (const auto early : {false, true}) {
    Fixture callback;
    if (early) {
      put(callback.in.node_bytes, 0x34U, 0U);
      callback.observations.resize(1U);
    }
    const auto text_plan = callback.run();
    RacFrontendOwnerInputsV1 input;
    input.node_root_address = 0x300000U;
    auto &slot = input.draw_slots[1U][0U];
    slot.source = {0x410000U, 1U, 1U};
    slot.node_address = callback.in.node_address;
    slot.callback_address = 0x21b298U;
    slot.x_y_width_height = {27U, 31U, 120U, 80U};
    slot.callback_return_word = text_plan.returned_word;
    RacFrontendRttLiveV1 live;
    live.allocator_limit_word = 0x300000U;
    live.depth_base_half = 0x120;
    live.depth_format_half = 0x32U;
    live.clear_color_word = 0x81234567U;
    live.clear_offset_reads = {0x7c00U, 0x7800U, 0x7c00U, 0x7800U};
    live.callback_node_after_setup = callback.in.node_address;
    live.callback_after_setup = 0x21b298U;
    live.restore_command_cursor_word = 0x900000U;
    live.live_draw_environment = 0xa0230000U;
    live.restore_environment_region = {0x230030U, 144U};
    live.restore_width_half = 512;
    live.restore_height_half = 416;
    // Explicit post-restore observation, not the callback's font TEX0 and not
    // an inference that any opaque owner callee preserves its source globals.
    live.composite_tex0 = 0x123456789abcdef0ULL;
    slot.rtt_live = live;
    const auto owner = plan_rac_frontend_owner_v1(input);
    check(owner.rtt_callbacks == 1U && owner.direct_callbacks == 0U &&
              owner.composites == (early ? 0U : 1U),
          "Actual callback return did not control original RTT composite");
    std::size_t setup_at = owner.effects.size(), clear_at = setup_at,
                callback_at = setup_at, restore_at = setup_at,
                quad_at = setup_at;
    for (std::size_t i = 0U; i < owner.effects.size(); ++i) {
      const auto &effect = owner.effects[i];
      if (const auto *setup = std::get_if<RacFrontendRttSetupV1>(&effect)) {
        check(setup->packet.bytes.size() == 240U &&
                  word(setup->packet.bytes, 0U) == 0x1000000eU,
              "Original RTT setup packet was not composed");
        setup_at = i;
      } else if (const auto *packet =
                     std::get_if<RacFrontendOwnerPacketV1>(&effect)) {
        if (packet->source_owner == 0x2017c8U) {
          check(packet->bytes.size() == 64U &&
                    word(packet->bytes, 0U) == 0x10000003U,
                "Original RTT clear packet missing");
          clear_at = i;
        } else if (packet->source_owner == 0x1fb498U) {
          check(packet->bytes.size() == 16U &&
                    word(packet->bytes, 0U) == 0x30000009U &&
                    packet->external_reads ==
                        std::vector<RacFrontendGsRegionV1>{{0x230030U, 144U}},
                "Actual return path lost caller-owned REF9 restore");
          restore_at = i;
        }
      } else if (const auto *call =
                     std::get_if<RacFrontendOwnerCallV1>(&effect)) {
        if (call->kind == RacFrontendOwnerCallKindV1::callback) {
          check(call->source_call_pc == 0x21aa84U &&
                    call->callback_address == 0x21b298U &&
                    call->arguments[0] == callback.in.node_address,
                "Owner did not dispatch the genuine recovered text callback");
          callback_at = i;
        } else if (call->kind ==
                   RacFrontendOwnerCallKindV1::composite_quad_1f5800) {
          check(call->arguments ==
                    std::array<U64, 10U>{27U, 31U, 120U, 80U, 0U, 0U, 120U, 80U,
                                         0x80808080U, *live.composite_tex0},
                "Source callback return2 did not retain full source crop "
                "arguments");
          quad_at = i;
        }
      }
    }
    check(setup_at < clear_at && clear_at < callback_at &&
              callback_at < restore_at && (early || restore_at < quad_at),
          "Actual callback/RTT setup-clear-restore order changed");
    // Only the recovered text callback executes here. Other owner callbacks,
    // camera/projectors and the composite remain explicit named dependencies.
  }
}
} // namespace

int main() {
  try {
    basic_flow();
    constant_half_mix_exact_domain();
    exact_timer_and_timed_color();
    early_returns_and_remap();
    all_selection_families();
    timer_transition_and_font_override();
    text_fallbacks_and_format();
    scrolling_and_controls();
    observation_and_ownership_failures();
    callback_return_drives_original_rtt_owner();
    std::cout << "rac_frontend_new_game_tests:10 groups passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rac_frontend_new_game_tests:" << error.what() << '\n';
    return 1;
  }
}
