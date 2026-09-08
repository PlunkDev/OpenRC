#include "openrc/rac_text_layout.hpp"

#include <algorithm>
#include <bit>

namespace openrc {
namespace {

std::int32_t word(const std::int64_t value) {
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}

std::int16_t half(const std::int32_t value) {
  return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

std::int32_t arithmetic_half(const std::int32_t value) {
  // Avoid implementation-defined signed right shift and INT_MIN negation.
  const auto raw = static_cast<std::uint32_t>(value);
  return std::bit_cast<std::int32_t>((raw >> 1U) | (raw & 0x80000000U));
}

class TextReader {
public:
  TextReader(const std::span<const std::byte> bytes,
             const RacFontMetricTableV1 &metrics, const std::uint64_t budget)
      : bytes_(bytes), metrics_(metrics), remaining_(budget) {}

  std::uint8_t read(const std::int64_t offset) {
    if (remaining_ == 0U) {
      throw RacTextLayoutError("Text layout byte-visit budget exhausted");
    }
    --remaining_;
    if (offset < 0 || static_cast<std::uint64_t>(offset) >= bytes_.size()) {
      throw RacTextLayoutError("Text layout source byte escaped bounded span");
    }
    return std::to_integer<std::uint8_t>(
        bytes_[static_cast<std::size_t>(offset)]);
  }

  std::int32_t advance(const std::uint8_t glyph) const {
    if (glyph >= metrics_.rows.size()) {
      throw RacTextLayoutError("Text layout source metric index out of range");
    }
    return metrics_.rows[glyph].advance_or_accent_x_offset;
  }

  std::int32_t measure(const std::int32_t start, const std::int64_t limit) {
    if (limit == 0) {
      return 0;
    }
    std::int32_t width = 0;
    std::int32_t count = 0;
    for (;;) {
      const auto glyph = read(static_cast<std::int64_t>(start) + count);
      if (glyph == 0U) {
        return width;
      }
      width = word(static_cast<std::int64_t>(width) + advance(glyph));
      count = word(static_cast<std::int64_t>(count) + 1);
      if (count == limit) {
        return width;
      }
    }
  }

private:
  std::span<const std::byte> bytes_;
  const RacFontMetricTableV1 &metrics_;
  std::uint64_t remaining_;
};

bool break_byte(const std::uint8_t glyph) {
  return glyph == 32U || glyph < 16U;
}

} // namespace

std::int32_t measure_rac_text_width_v1(const std::span<const std::byte> text,
                                       const std::int64_t byte_limit,
                                       const RacFontMetricTableV1 &metrics,
                                       const std::uint64_t max_byte_visits) {
  TextReader reader(text, metrics, max_byte_visits);
  return reader.measure(0, byte_limit);
}

RacTextLayoutPlanV1
plan_rac_text_layout_v1(const std::span<const std::byte> text,
                        const RacFontMetricTableV1 &metrics,
                        const RacTextLayoutRequestV1 &request,
                        const std::uint64_t max_byte_visits) {
  TextReader reader(text, metrics, max_byte_visits);
  RacTextLayoutPlanV1 result;
  result.box = request.box;
  const auto &box = request.box;
  result.entry_clip = {box.left, box.right - 1, box.top, box.bottom - 1};
  result.exit_clip = {
      0, word(static_cast<std::int64_t>(request.screen_width) - 1), 0,
      word(static_cast<std::int64_t>(request.screen_height) - 1)};
  result.final_palette_zero = request.palette[0];
  std::int32_t wrap = box.right - box.anchor_x;
  if ((box.flags & 1U) != 0U) {
    wrap = 2 * std::min(wrap, box.anchor_x - box.left);
  }
  result.initial_wrap_width = wrap;
  std::uint8_t color = 0U;
  std::int32_t baseline_line_count = 0;
  std::int32_t final_scanned_width = 0;
  for (;;) {
    ++result.scan_passes;
    result.lines.clear();
    std::int32_t offset = 0;
    if (request.byte_limit != 0 && reader.read(0) != 0U) {
      for (;;) {
        if (result.lines.size() == 32U) {
          throw RacTextLayoutError(
              "Text layout exceeds original 32-line arrays");
        }
        RacTextLayoutLineV1 line;
        line.first_byte = half(offset);
        line.initial_color_index = color;
        auto last_break = offset;
        std::int32_t scanned_width = 0;
        if (wrap > 0) {
          for (;;) {
            const auto glyph = reader.read(offset);
            if (break_byte(glyph)) {
              last_break = offset;
            }
            if (request.inline_colors_enabled && glyph >= 8U && glyph < 16U) {
              color = static_cast<std::uint8_t>(glyph - 8U);
            }
            if (glyph < 2U) {
              break;
            }
            offset = word(static_cast<std::int64_t>(offset) + 1);
            scanned_width = word(static_cast<std::int64_t>(scanned_width) +
                                 reader.advance(glyph));
            if (scanned_width >= wrap) {
              break;
            }
          }
        }
        line.last_byte_inclusive = half(last_break);
        if (line.last_byte_inclusive == line.first_byte) {
          line.last_byte_inclusive = half(offset);
        }
        offset = line.last_byte_inclusive;
        const auto endpoint = reader.read(offset);
        if (break_byte(endpoint)) {
          line.last_byte_inclusive = half(line.last_byte_inclusive - 1);
        }
        result.lines.push_back(line);
        if (endpoint == 0U) {
          final_scanned_width = scanned_width;
          break;
        }
        offset = word(static_cast<std::int64_t>(offset) + 1);
        if (offset == request.byte_limit || reader.read(offset) == 0U) {
          break;
        }
      }
    }
    result.accepted_wrap_width = wrap;
    const auto line_count = static_cast<std::int32_t>(result.lines.size());
    if (result.restored_original_width || line_count < 2) {
      break;
    }
    if (baseline_line_count == 0) {
      baseline_line_count = line_count;
    }
    if (baseline_line_count < line_count) {
      wrap = result.initial_wrap_width;
      result.restored_original_width = true;
      continue;
    }
    const auto retry = final_scanned_width < wrap / 3;
    wrap -= 16;
    // Source delay slot subtracts 16 even when the accepted scan is kept.
    // Neither inline color nor final_scanned_width is reset on a retry.
    if (!retry) {
      break;
    }
  }

  const auto height = static_cast<std::int32_t>(result.lines.size()) *
                      static_cast<std::int32_t>(box.line_spacing);
  result.box.max_width = 0;
  result.box.height = half(height);
  auto y = static_cast<std::int32_t>(box.anchor_y);
  if ((box.flags & 2U) != 0U) {
    y = word(static_cast<std::int64_t>(y) - arithmetic_half(height));
  }
  for (auto &line : result.lines) {
    const auto next_y = word(static_cast<std::int64_t>(y) + box.line_spacing);
    if (next_y >= box.top && box.bottom >= y) {
      const auto count = static_cast<std::int32_t>(line.last_byte_inclusive) -
                         line.first_byte + 1;
      const auto width = reader.measure(line.first_byte, count);
      line.measured_width = width;
      if (result.box.max_width < width) {
        result.box.max_width = half(width);
      }
      if ((box.flags & 4U) == 0U) {
        RacTextLineDrawV1 draw;
        draw.floating = (box.flags & 8U) != 0U;
        draw.x_base = box.anchor_x;
        if ((box.flags & 1U) != 0U) {
          draw.x_base = word(static_cast<std::int64_t>(draw.x_base) -
                             arithmetic_half(width));
        }
        draw.y_base = y;
        if (draw.floating) {
          draw.subpixel_x = box.subpixel_x;
          draw.subpixel_y = box.subpixel_y;
        }
        draw.palette_zero_write = static_cast<std::uint32_t>(request.rgbaq);
        const auto color_word = line.initial_color_index == 0U
                                    ? draw.palette_zero_write
                                    : request.palette[line.initial_color_index];
        draw.initial_rgbaq = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(std::bit_cast<std::int32_t>(color_word)));
        draw.tex0 = request.tex0;
        line.draw = draw;
        result.final_palette_zero = draw.palette_zero_write;
      }
    }
    y = next_y;
  }
  return result;
}

} // namespace openrc
