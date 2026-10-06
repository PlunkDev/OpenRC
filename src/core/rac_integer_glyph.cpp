#include "openrc/rac_integer_glyph.hpp"

#include <bit>
#include <limits>

namespace openrc {
namespace {
[[noreturn]] void fail(const char *message) { throw RacIntegerGlyphError(message); }
std::uint32_t add_signed(std::uint32_t word, std::int8_t offset) noexcept {
  return word + static_cast<std::uint32_t>(static_cast<std::int32_t>(offset));
}
std::uint64_t signed_word(std::uint32_t word) noexcept {
  return static_cast<std::uint64_t>(
      static_cast<std::int64_t>(std::bit_cast<std::int32_t>(word)));
}
std::uint64_t grayscale(std::uint64_t color) noexcept {
  const auto gray = static_cast<std::uint32_t>(
      ((color & 255U) + ((color >> 8U) & 255U) + ((color >> 16U) & 255U)) / 3U);
  return signed_word(static_cast<std::uint32_t>(color & 0xff000000U) |
                     (gray << 16U) | (gray << 8U) | gray);
}
} // namespace

RacIntegerGlyphPlanV1 execute_rac_integer_glyph_v1(
    const std::span<const std::byte> text, const RacFontMetricTableV1 &metrics,
    const RacIntegerGlyphInputsV1 &input, const RacIntegerGlyphLimitsV1 limits) {
  RacIntegerGlyphPlanV1 result;
  result.final_palette = input.palette;
  result.final_rgbaq = input.rgbaq;
  result.final_x_word = input.x_word;
  result.palette_zero_written = !input.preserve_palette_zero;
  if (result.palette_zero_written)
    result.final_palette[0] = static_cast<std::uint32_t>(input.rgbaq);
  if (input.byte_limit == 0) return result;

  const auto draw = [&](RacIntegerGlyphKindV1 kind, std::uint32_t pc,
                        std::uint16_t row_id, std::uint32_t x,
                        std::uint32_t width, std::uint64_t color) {
    if (result.draws.size() >= limits.max_draws)
      fail("Integer glyph draw budget exceeded");
    const auto &row = metrics.rows[row_id];
    RacIntegerGlyphDrawV1 call;
    call.kind = kind;
    call.source_call_pc = pc;
    call.source_byte_offset = result.consumed_bytes;
    call.metric_row_index = row_id;
    call.arguments.rectangle_words = {x, add_signed(input.y_word, row.y_offset),
                                      width, 16U};
    call.arguments.uv_rectangle_words = {row.atlas_u, row.atlas_v, width, 16U};
    call.arguments.screen_offset_reads = input.screen_offset_words;
    call.arguments.rgbaq = color;
    call.arguments.tex0 = input.tex0;
    call.emission = emit_rac_integer_quad_v1(call.arguments);
    result.draws.push_back(call);
  };

  for (;;) {
    if (result.consumed_bytes >= text.size())
      fail("Integer glyph text has no owned terminator before its limit");
    const auto byte = std::to_integer<std::uint8_t>(text[result.consumed_bytes]);
    if (byte == 0U) { result.reached_nul = true; break; }
    if (result.consumed_bytes >= limits.max_consumed_bytes)
      fail("Integer glyph byte budget exceeded");
    if (byte >= 8U && byte < 16U) {
      if (input.inline_colors_enabled)
        result.final_rgbaq = (result.final_rgbaq & 0xff000000U) |
                            (result.final_palette[byte - 8U] & 0xffffffU);
    } else {
      if (byte >= metrics.rows.size())
        fail("Integer glyph byte exceeds the recovered metric table");
      const auto &row = metrics.rows[byte];
      if (row.advance_or_accent_x_offset != 0) {
        if (byte >= 0x80U && byte < 0xa8U) {
          const auto accent = static_cast<std::uint16_t>(byte + 0x40U);
          draw(RacIntegerGlyphKindV1::accent, 0x1f6794U, accent,
               add_signed(result.final_x_word,
                          metrics.rows[accent].advance_or_accent_x_offset),
               16U, result.final_rgbaq);
        }
        if (byte < 32U) {
          draw(RacIntegerGlyphKindV1::icon, 0x1f6838U, byte,
               result.final_x_word, 24U, grayscale(result.final_rgbaq));
        } else if (byte != 32U) {
          draw(RacIntegerGlyphKindV1::ordinary, 0x1f6884U, byte,
               result.final_x_word, 16U, result.final_rgbaq);
        }
        result.final_x_word = add_signed(result.final_x_word,
                                         row.advance_or_accent_x_offset);
      }
    }
    ++result.consumed_bytes;
    if (signed_word(result.consumed_bytes) ==
        static_cast<std::uint64_t>(input.byte_limit)) break;
    if (result.consumed_bytes == std::numeric_limits<std::uint32_t>::max())
      fail("Integer glyph source counter would wrap");
  }
  return result;
}
} // namespace openrc
