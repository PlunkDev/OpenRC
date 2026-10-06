#include "openrc/rac_frontend_list_draw.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace openrc {
namespace {
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using Kind = RacFrontendListEffectKindV1;
std::int32_t s32(U32 value) { return std::bit_cast<std::int32_t>(value); }
U64 sx(U32 value) { return static_cast<U64>(static_cast<std::int64_t>(s32(value))); }
U32 asr1(U32 value) { return (value >> 1U) | (value & 0x80000000U); }
[[noreturn]] void fail(const char *message) { throw RacFrontendListDrawError(message); }
void put(std::span<std::byte> bytes, std::size_t at, U64 value, unsigned count) {
  for (unsigned i = 0U; i < count; ++i) bytes[at + i] = static_cast<std::byte>(value >> (8U * i));
}
void preamble(std::span<std::byte> bytes, U32 reg, U64 value) {
  put(bytes, 0U, 0x10000002U, 4U); put(bytes, 12U, 0x50000002U, 4U);
  put(bytes, 16U, UINT64_C(0x1000000000008001), 8U); put(bytes, 24U, 14U, 8U);
  put(bytes, 32U, value, 8U); put(bytes, 40U, reg, 8U);
}
} // namespace

RacFrontendListDrawPlanV1 execute_rac_frontend_list_draw_v1(
    const RacFrontendListDrawInputsV1 &in) {
  if (!in.font_metrics || !in.text_bank || in.rows.size() > in.max_rows ||
      in.max_rows > 65536U || in.max_text_bytes > 1048576U ||
      in.text_bank->entries.size() > 65536U)
    fail("Invalid bounded original main-list inputs");
  RacFrontendListDrawPlanV1 out;
  out.final_flags = in.flags;
  out.final_palette = in.palette;
  out.final_inline_colors_enabled = in.inline_colors_enabled;
  out.font_source_id = in.flags & 4U ? 3U : 1U;
  U32 font_height = in.flags & 4U ? 14U : 12U;
  if (in.flags & 8U) { out.font_source_id = 2U; font_height = 10U; }
  const auto &metrics = in.font_metrics->tables[out.font_source_id - 1U];
  auto effect = [&](Kind kind, U32 pc, U32 row = 0U, U64 value = 0U, bool evaluated = true) {
    out.effects.push_back({kind, pc, row, value, evaluated});
  };
  preamble(std::span(out.preamble_packets).first(48U), 0x42U, 0x44U);
  preamble(std::span(out.preamble_packets).subspan(48U), 0x47U, 0x2004bU);
  effect(Kind::preamble, 0x21c258U);
  effect(Kind::begin, 0x21c270U);
  U32 row_count = 0U;
  while (row_count < in.rows.size() && in.rows[row_count].text_key != 0) ++row_count;
  if (row_count == in.rows.size()) fail("Main list lacks an owned zero-key terminator");
  const U32 spacing = in.flags & 16U ? font_height + 3U :
      static_cast<U32>(s32(in.height_word) / static_cast<std::int32_t>(row_count + 1U));
  const U32 center = asr1(in.width_word);
  U32 y = spacing - font_height / 2U - 1U;
  U32 max_width = 0U;
  U32 total_text_bytes = 0U;
  U32 lookup_visits = 0U;
  const auto account_text = [&](std::size_t size) {
    if (size > in.max_total_text_bytes - total_text_bytes)
      fail("Main-list aggregate text work/output budget exceeded");
    total_text_bytes += static_cast<U32>(size);
  };
  const auto text = [&](std::int16_t key, U32 pc, U32 row) {
    const auto key_word = static_cast<U32>(static_cast<std::int32_t>(key));
    effect(Kind::lookup, pc, row, sx(key_word));
    const RacTextBankEntryV1 *found = nullptr;
    for (const auto &entry : in.text_bank->entries) {
      if (lookup_visits == in.max_lookup_row_visits)
        fail("Main-list aggregate lookup budget exceeded");
      ++lookup_visits;
      if (entry.key == key_word) { found = &entry; break; }
    }
    std::vector<std::byte> result;
    if (found) {
      if (found->text_bytes.size() >= in.max_text_bytes ||
          std::find(found->text_bytes.begin(), found->text_bytes.end(), std::byte{}) != found->text_bytes.end())
        fail("Main-list bank text exceeds its owned bounds");
      account_text(found->text_bytes.size() + 1U);
      result = found->text_bytes;
      result.push_back(std::byte{});
    } else {
      const auto bounded = in.missing_text.first(std::min<std::size_t>(in.missing_text.size(), in.max_text_bytes));
      const auto terminator = std::find(bounded.begin(), bounded.end(), std::byte{});
      if (terminator == bounded.end())
        fail("Main-list lookup miss has no bounded original fallback text");
      account_text(static_cast<std::size_t>(terminator - in.missing_text.begin()) + 1U);
      result.assign(in.missing_text.begin(), terminator + 1);
    }
    return result;
  };
  const auto measure = [&](const std::vector<std::byte> &bytes, U32 pc, U32 row) {
    const auto width = static_cast<U32>(measure_rac_text_width_v1(bytes, -1, metrics, in.max_text_bytes));
    effect(Kind::measure, pc, row, sx(width));
    return width;
  };
  if (in.flags & 0x4000U) {
    for (U32 row = 0U; row < row_count; ++row) {
      const auto bytes = text(in.rows[row].text_key, 0x21c338U, row);
      const auto width = measure(bytes, 0x21c348U, row);
      if (s32(max_width) < s32(width)) max_width = width;
    }
  }
  if ((in.flags & 0x20000U) && s32(in.width_word) < s32(max_width + 6U) && !(in.flags & 8U)) {
    if (!in.color_observations.empty()) fail("Unused timed-color observation on font retry");
    out.final_flags |= 8U;
    out.returned_word = 1U;
    effect(Kind::node_flags_write, 0x21c3b4U, 0U, out.final_flags);
    // The source returns after begin without a matching end on this branch.
    return out;
  }
  if (s32(in.width_word) < s32(max_width)) max_width = in.width_word;
  std::vector<U32> bindings;
  U32 total_draws = 0U;
  std::size_t observations = 0U;
  const auto colors = [&](bool enabled, U32 pc, U32 row) {
    out.final_inline_colors_enabled = enabled;
    effect(Kind::colors, pc, row, enabled);
  };
  const auto glyph = [&](const std::vector<std::byte> &bytes, U32 x, U32 yy,
                         U64 color, U32 bind_pc, U32 draw_pc, U32 row,
                         bool secondary, bool shadow) {
    if (bindings.empty()) {
      bindings.push_back(out.font_source_id);
      out.bindings = plan_rac_frontend_gs_bindings_v1(
          in.textures, bindings, in.texture_payload, in.allocator_begin, in.max_rows * 4U);
    } else {
      out.bindings.bindings.push_back(out.bindings.bindings.front());
    }
    const auto tex0 = out.bindings.bindings.back().tex0;
    effect(Kind::bind, bind_pc, row, tex0);
    RacIntegerGlyphInputsV1 input;
    input.x_word = x; input.y_word = yy; input.rgbaq = color; input.tex0 = tex0;
    input.palette = out.final_palette;
    input.inline_colors_enabled = out.final_inline_colors_enabled;
    input.preserve_palette_zero = in.preserve_palette_zero;
    input.screen_offset_words = in.screen_offset_y_x_words;
    account_text(bytes.size());
    auto plan = execute_rac_integer_glyph_v1(bytes, metrics, input,
        {in.max_text_bytes, in.max_total_draws - total_draws});
    total_draws += static_cast<U32>(plan.draws.size());
    out.final_palette = plan.final_palette;
    effect(Kind::glyph, draw_pc, row, out.glyph_calls.size());
    out.glyph_calls.push_back({row, secondary, shadow, bytes, std::move(plan)});
  };
  for (U32 row = 0U; row < row_count; ++row) {
    const auto &entry = in.rows[row];
    U64 color = 0U;
    if (in.flags & 2U) color = sx(0x80ffa888U);
    else if (entry.action == 0) color = sx(in.focused && in.selected_row == row ? 0x80006060U : 0x80303030U);
    else {
      const auto timed = plan_rac_frontend_timed_color_v1(
          static_cast<U32>(static_cast<std::int32_t>(entry.color_age)),
          UINT64_MAX, UINT64_MAX, in.timer_argument_word, in.time_scale_bits);
      if (timed.returned_low64) color = *timed.returned_low64;
      else {
        if (observations >= in.color_observations.size())
          fail("Main-list timed color needs a qualified result for its actual non-exact operands");
        const auto &observed = in.color_observations[observations++];
        if (observed.row_index != row || observed.age != entry.color_age ||
            observed.timer_argument_word != in.timer_argument_word ||
            observed.time_scale_bits != in.time_scale_bits)
          fail("Main-list timed-color observation differs from reached source operands");
        color = observed.returned_low64;
      }
      effect(Kind::timed_color, 0x21c44cU, row, color, timed.returned_low64.has_value());
    }
    auto bytes = text(entry.text_key, 0x21c46cU, row);
    if (entry.action == 2) bytes = text(20308, 0x21c490U, row);
    const U32 width = measure(bytes, 0x21c4a4U, row);
    U32 x = center - asr1(width);
    if (in.flags & 0x40U) x = 4U;
    else if (in.flags & 0x4000U) x = center - asr1(max_width);
    colors(false, 0x21c4e4U, row);
    glyph(bytes, x + in.shadow_x_y_words[0], y + in.shadow_x_y_words[1],
          UINT64_C(0x80000000), 0x21c4fcU, 0x21c520U, row, false, true);
    colors(true, 0x21c528U, row);
    if (in.flags & 0x80U) colors(false, 0x21c540U, row);
    glyph(bytes, x, y, color, 0x21c54cU, 0x21c574U, row, false, false);
    y += spacing;
    if (entry.secondary_text_key != 0) {
      colors(false, 0x21c590U, row);
      bytes = text(entry.secondary_text_key, 0x21c5b0U, row);
      glyph(bytes, x + in.shadow_x_y_words[0], y + in.shadow_x_y_words[1],
            UINT64_C(0x80000000), 0x21c5bcU, 0x21c5e0U, row, true, true);
      if (!(in.flags & 0x80U)) colors(true, 0x21c5f8U, row);
      bytes = text(entry.secondary_text_key, 0x21c608U, row);
      glyph(bytes, x, y, color, 0x21c614U, 0x21c63cU, row, true, false);
      y += spacing;
    }
    if (in.flags & 0x80U) colors(true, 0x21c654U, row);
  }
  if (observations != in.color_observations.size()) fail("Unused main-list timed-color observation");
  if (bindings.empty()) out.bindings = plan_rac_frontend_gs_bindings_v1(
      in.textures, bindings, in.texture_payload, in.allocator_begin, in.max_rows * 4U);
  effect(Kind::end, 0x21c680U);
  out.batch_ended = true;
  out.returned_word = 2U;
  return out;
}
} // namespace openrc
