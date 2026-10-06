#pragma once

#include "openrc/rac_font_metrics.hpp"
#include "openrc/rac_integer_quad.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Compiler-side execution of the actual integer glyph owner 1f6668, used
// by main-list draw 21c1b0. It is not substituted for floating text owners.
struct RacIntegerGlyphInputsV1 {
  std::uint32_t x_word = 0U;
  std::uint32_t y_word = 0U;
  std::int64_t byte_limit = -1;
  std::uint64_t rgbaq = 0U;
  std::uint64_t tex0 = 0U;
  std::array<std::uint32_t, 8U> palette{};
  bool inline_colors_enabled = true;
  bool preserve_palette_zero = false;
  // 1f5800 only reads these globals, and neither owner writes them. The
  // pair is read Y then X for every emitted quad, not guessed from an ELF.
  std::array<std::uint32_t, 2U> screen_offset_words{};
};

enum class RacIntegerGlyphKindV1 { ordinary, accent, icon };
struct RacIntegerGlyphDrawV1 {
  RacIntegerGlyphKindV1 kind = RacIntegerGlyphKindV1::ordinary;
  std::uint32_t source_call_pc = 0U;
  std::uint32_t source_byte_offset = 0U;
  std::uint16_t metric_row_index = 0U;
  RacIntegerQuadInputsV1 arguments;
  RacIntegerQuadEmissionV1 emission;
};

struct RacIntegerGlyphLimitsV1 {
  std::uint32_t max_consumed_bytes = 65536U;
  std::uint32_t max_draws = 131072U;
};

struct RacIntegerGlyphPlanV1 {
  std::vector<RacIntegerGlyphDrawV1> draws;
  std::array<std::uint32_t, 8U> final_palette{};
  std::uint64_t final_rgbaq = 0U;
  std::uint32_t final_x_word = 0U;
  std::uint32_t consumed_bytes = 0U;
  bool reached_nul = false;
  bool palette_zero_written = false;
};

class RacIntegerGlyphError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Executes source word wrapping, palette/control dispatch, accent-before-base
// ordering, signed metric advances and all actual 1f5800 calls. No COP1/VU
// arithmetic occurs in this owner. Input bytes include the owned terminator
// unless the actual byte limit stops first. Every emitted packet retains raw
// RGBAQ/TEX0; residency, inherited GS state and rasterization stay separate.
[[nodiscard]] RacIntegerGlyphPlanV1 execute_rac_integer_glyph_v1(
    std::span<const std::byte> text, const RacFontMetricTableV1 &metrics,
    const RacIntegerGlyphInputsV1 &input,
    RacIntegerGlyphLimitsV1 limits = {});

} // namespace openrc
