#include "openrc/rac_float_glyph.hpp"

#include <bit>
#include <limits>

namespace openrc {
namespace {

class Builder {
public:
  Builder(const RacFloatGlyphStateV1 &state,
          const RacFloatGlyphLimitsV1 &limits)
      : limits_(limits) {
    result.final_palette = state.palette;
    result.final_rgbaq = state.rgbaq;
    result.palette_zero_written = !state.preserve_palette_zero;
    if (result.palette_zero_written) {
      result.final_palette[0] = static_cast<std::uint32_t>(state.rgbaq);
    }
  }

  RacGlyphValueIdV1 convert(const std::int32_t word) {
    const auto id = next_value();
    append(RacGlyphConvertWordV1{id, word});
    return id;
  }

  RacGlyphValueIdV1 binary(const RacGlyphBinaryOpcodeV1 opcode,
                           const RacGlyphValueIdV1 left,
                           const RacGlyphValueIdV1 right) {
    const auto id = next_value();
    append(RacGlyphBinaryV1{opcode, id, left, right});
    return id;
  }

  void draw(const RacGlyphDrawV1 &call) {
    append(call);
    ++result.draw_count;
  }

  RacFloatGlyphProgramV1 result;

private:
  RacGlyphValueIdV1 next_value() {
    if (result.value_count == std::numeric_limits<std::uint32_t>::max()) {
      throw RacFloatGlyphError("Glyph value ID capacity exceeded");
    }
    return result.value_count++;
  }

  void append(RacGlyphOperationV1 operation) {
    if (result.operations.size() >= limits_.max_operations ||
        result.operations.size() == result.operations.max_size()) {
      throw RacFloatGlyphError("Glyph operation limit exceeded");
    }
    result.operations.push_back(operation);
  }

  const RacFloatGlyphLimitsV1 &limits_;
};

std::uint64_t icon_color(const std::uint64_t color) {
  const auto gray = static_cast<std::uint32_t>(
      ((color & 255U) + ((color >> 8U) & 255U) + ((color >> 16U) & 255U)) / 3U);
  const auto packed = static_cast<std::uint32_t>(color & 0xff000000U) |
                      (gray << 16U) | (gray << 8U) | gray;
  // Original word ADDU sign-extends its result into the outgoing 64-bit GPR.
  return static_cast<std::uint64_t>(
      static_cast<std::int64_t>(std::bit_cast<std::int32_t>(packed)));
}

} // namespace

RacFloatGlyphProgramV1 build_rac_float_glyph_program_v1(
    const std::span<const std::byte> text, const std::int32_t byte_limit,
    const RacFontMetricTableV1 &metrics, const RacFloatGlyphStateV1 &state,
    const RacFloatGlyphLimitsV1 &limits) {
  Builder builder(state, limits);
  auto &result = builder.result;
  constexpr auto mul = RacGlyphBinaryOpcodeV1::multiply_single;
  constexpr auto add = RacGlyphBinaryOpcodeV1::add_single;
  // Original branch delay: this MUL.S executes even for an empty draw.
  const auto scaled16 =
      builder.binary(mul, kRacGlyphInputScaleV1, kRacGlyphSingle16V1);
  const auto limit_word = std::bit_cast<std::uint32_t>(byte_limit);
  if (limit_word == 0U) {
    return result;
  }

  while (true) {
    if (result.consumed_bytes >= text.size()) {
      throw RacFloatGlyphError(
          "Glyph text has no owned terminator before limit");
    }
    const auto byte =
        std::to_integer<std::uint8_t>(text[result.consumed_bytes]);
    if (byte == 0U) {
      result.reached_nul = true;
      break;
    }
    if (result.consumed_bytes >= limits.max_consumed_bytes) {
      throw RacFloatGlyphError("Glyph byte limit exceeded");
    }
    if (byte >= 8U && byte < 16U) {
      if (state.color_controls_enabled) {
        result.final_rgbaq = (result.final_rgbaq & 0xff000000ULL) |
                             (result.final_palette[byte - 8U] & 0x00ffffffU);
      }
    } else {
      if (byte >= metrics.rows.size()) {
        throw RacFloatGlyphError("Glyph byte exceeds recovered metric table");
      }
      const auto &row = metrics.rows[byte];
      if (row.advance_or_accent_x_offset != 0) {
        const auto base_y = builder.convert(row.y_offset);
        const auto scaled_y =
            builder.binary(mul, base_y, kRacGlyphInputScaleV1);
        auto call = RacGlyphDrawV1{};
        call.source_byte_offset = result.consumed_bytes;
        call.source_byte = byte;
        call.metric_row_index = byte;
        call.x = result.final_pen_x;
        call.width = scaled16;
        call.height = scaled16;
        call.atlas_u = row.atlas_u;
        call.atlas_v = row.atlas_v;
        call.rgbaq = result.final_rgbaq;
        call.tex0 = state.tex0;

        if (byte >= 0x80U && byte < 0xa8U) {
          const auto accent_index = static_cast<std::uint16_t>(byte + 0x40U);
          const auto &accent = metrics.rows[accent_index];
          const auto accent_x =
              builder.convert(accent.advance_or_accent_x_offset);
          const auto scaled_x =
              builder.binary(mul, accent_x, kRacGlyphInputScaleV1);
          const auto accent_y = builder.convert(accent.y_offset);
          const auto scaled_accent_y =
              builder.binary(mul, accent_y, kRacGlyphInputScaleV1);
          auto accent_call = call;
          accent_call.kind = RacGlyphDrawKindV1::accent;
          accent_call.metric_row_index = accent_index;
          accent_call.x = builder.binary(add, result.final_pen_x, scaled_x);
          accent_call.y =
              builder.binary(add, kRacGlyphInputYV1, scaled_accent_y);
          accent_call.atlas_u = accent.atlas_u;
          accent_call.atlas_v = accent.atlas_v;
          builder.draw(accent_call);
        }

        if (byte < 32U) {
          call.kind = RacGlyphDrawKindV1::icon;
          call.y = builder.binary(add, kRacGlyphInputYV1, scaled_y);
          call.width =
              builder.binary(mul, kRacGlyphInputScaleV1, kRacGlyphSingle24V1);
          call.height =
              builder.binary(mul, kRacGlyphInputScaleV1, kRacGlyphSingle16V1);
          call.source_width = 24U;
          call.rgbaq = icon_color(result.final_rgbaq);
          builder.draw(call);
        } else if (byte != 32U) {
          call.y = builder.binary(add, kRacGlyphInputYV1, scaled_y);
          builder.draw(call);
        }
        const auto advance = builder.convert(row.advance_or_accent_x_offset);
        const auto scaled_advance =
            builder.binary(mul, advance, kRacGlyphInputScaleV1);
        result.final_pen_x =
            builder.binary(add, result.final_pen_x, scaled_advance);
      }
    }
    ++result.consumed_bytes;
    if (result.consumed_bytes == limit_word) {
      break;
    }
    // Prevent word wrap even if caller permits the largest possible extent.
    if (result.consumed_bytes == std::numeric_limits<std::uint32_t>::max()) {
      throw RacFloatGlyphError("Glyph counter capacity exceeded");
    }
  }
  return result;
}

} // namespace openrc
