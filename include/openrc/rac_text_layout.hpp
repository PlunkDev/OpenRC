#pragma once

#include "openrc/rac_font_metrics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Compiler-side inputs to the original text layout owner. The original
// descriptor has twelve 16-bit fields; no host typography is substituted.
struct RacTextLayoutBoxV1 {
  std::int16_t top = 0;
  std::int16_t bottom = 0;
  std::int16_t left = 0;
  std::int16_t right = 0;
  std::int16_t anchor_x = 0;
  std::int16_t anchor_y = 0;
  std::int16_t max_width = 0;
  std::int16_t height = 0;
  std::int16_t line_spacing = 0;
  // 1: horizontal centering, 2: vertical centering, 4: measure only,
  // 8: floating glyph owner. Other bits are retained, not assigned meanings.
  std::uint16_t flags = 0;
  std::int16_t subpixel_x = 0;
  std::int16_t subpixel_y = 0;

  [[nodiscard]] bool operator==(const RacTextLayoutBoxV1 &) const = default;
};

struct RacTextClipCallV1 {
  std::int32_t left = 0;
  std::int32_t right_inclusive = 0;
  std::int32_t top = 0;
  std::int32_t bottom_inclusive = 0;

  [[nodiscard]] bool operator==(const RacTextClipCallV1 &) const = default;
};

struct RacTextLineDrawV1 {
  bool floating = false;
  std::int32_t x_base = 0;
  std::int32_t y_base = 0;
  // Floating coordinates are source expressions, NOT host float results:
  // X = ADD.S(CVT.S.W(x_base), MUL.S(CVT.S.W(subpixel_x), 0x3d800000))
  // Y = ADD.S(CVT.S.W(y_base), MUL.S(CVT.S.W(subpixel_y), 0x3d800000))
  // The floating glyph call receives scale bits 0x3f800000. The integer
  // owner ignores both subpixel fields. Numeric execution remains separate.
  std::int16_t subpixel_x = 0;
  std::int16_t subpixel_y = 0;
  // Before EACH call, palette[0] is written with the input color's low word.
  std::uint32_t palette_zero_write = 0U;
  // The chosen palette word is loaded by LW, hence sign-extended to low64.
  std::uint64_t initial_rgbaq = 0U;
  std::uint64_t tex0 = 0U;

  [[nodiscard]] bool operator==(const RacTextLineDrawV1 &) const = default;
};

struct RacTextLayoutLineV1 {
  std::int16_t first_byte = 0;
  std::int16_t last_byte_inclusive = 0;
  std::uint8_t initial_color_index = 0U;
  // Empty lines have last == first-1 and count 0. Vertically culled lines
  // have neither a width nor a glyph call, even in measure-only mode.
  std::optional<std::int32_t> measured_width;
  std::optional<RacTextLineDrawV1> draw;

  [[nodiscard]] bool operator==(const RacTextLayoutLineV1 &) const = default;
};

struct RacTextLayoutRequestV1 {
  RacTextLayoutBoxV1 box;
  std::int64_t byte_limit = -1;
  std::uint64_t rgbaq = 0U;
  std::uint64_t tex0 = 0U;
  std::array<std::uint32_t, 8> palette{};
  bool inline_colors_enabled = false;
  std::uint32_t screen_width = 0U;
  std::uint32_t screen_height = 0U;
};

struct RacTextLayoutPlanV1 {
  RacTextLayoutBoxV1 box;
  // Execution order: entry clip call; set palette-reseed suppression to 1;
  // each line's optional palette write + glyph call; clear suppression to 0;
  // exit clip call. These are call arguments, NOT emitted GS state/packets.
  RacTextClipCallV1 entry_clip;
  std::vector<RacTextLayoutLineV1> lines;
  RacTextClipCallV1 exit_clip;
  std::uint32_t final_palette_zero = 0U;
  std::uint32_t scan_passes = 0U;
  std::int32_t initial_wrap_width = 0;
  // Width used by the accepted scan, before the source's unused final -16.
  std::int32_t accepted_wrap_width = 0;
  bool restored_original_width = false;
};

class RacTextLayoutError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Original width leaf: sum signed byte-3 metrics until NUL or counter ==
// limit. A zero limit reads no text. No control filtering or Unicode mapping.
// The span must include a terminator unless the source limit ends first.
[[nodiscard]] std::int32_t measure_rac_text_width_v1(
    std::span<const std::byte> text, std::int64_t byte_limit,
    const RacFontMetricTableV1 &metrics, std::uint64_t max_byte_visits);

// Exact integer line scanning, retry/color carry, vertical cull and width
// measurement for the original owner. The byte limit is checked at source
// line boundaries, NOT enforced as a span truncation. The caller supplies
// bounded text including its known NUL (bank entries omit this terminator).
// Source stack-array overruns (>32 lines), out-of-span/indexed metric reads,
// and exhausted work budgets fail explicitly. No partial plan is returned.
// This is not a prepared UI resource, glyph renderer, or COP1 qualification.
[[nodiscard]] RacTextLayoutPlanV1 plan_rac_text_layout_v1(
    std::span<const std::byte> text, const RacFontMetricTableV1 &metrics,
    const RacTextLayoutRequestV1 &request, std::uint64_t max_byte_visits);

} // namespace openrc
