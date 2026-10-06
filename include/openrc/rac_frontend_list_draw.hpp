#pragma once

#include "openrc/rac_frontend_new_game.hpp"
#include "openrc/rac_integer_glyph.hpp"

namespace openrc {

// The twelve-byte original list row consumed by 21c1b0. A zero key ends
// iteration; action and secondary text remain distinct source fields.
struct RacFrontendListRowV1 {
  std::int16_t text_key = 0;
  std::int16_t action = 0;
  std::uint32_t target_screen = 0U;
  std::int16_t secondary_text_key = 0;
  std::int16_t color_age = 0;
};

struct RacFrontendListColorObservationV1 {
  std::uint32_t row_index = 0U;
  std::int32_t age = 0;
  std::uint32_t timer_argument_word = 0U;
  std::uint32_t time_scale_bits = 0U;
  std::uint64_t returned_low64 = 0U;
};

struct RacFrontendListDrawInputsV1 {
  // Actual live node+20/+24, set by the enclosing projection owner; these
  // are local RTT dimensions, not positions inferred from file data.
  std::uint32_t width_word = 0U;
  std::uint32_t height_word = 0U;
  std::uint32_t flags = 0U;
  std::uint32_t selected_row = 0U;
  bool focused = false;
  std::span<const RacFrontendListRowV1> rows; // owns reached zero-key row
  const RacTextBankV1 *text_bank = nullptr;
  const RacFontMetricTablesV1 *font_metrics = nullptr;
  // Actual source fallback text, including NUL, required only on lookup miss.
  std::span<const std::byte> missing_text;
  std::array<std::uint32_t, 2U> shadow_x_y_words{}; // 1602b8/1602bc
  std::array<std::uint32_t, 2U> screen_offset_y_x_words{};
  std::array<std::uint32_t, 8U> palette{};
  bool inline_colors_enabled = true; // live15f59c
  bool preserve_palette_zero = false; // live15f5a0
  std::uint32_t timer_argument_word = 0U; // actual1602b4
  std::uint32_t time_scale_bits = 0U; // actual15ee68
  std::span<const RacFrontendListColorObservationV1> color_observations;
  std::span<const RacFrontendTextureEntryV1> textures;
  RacFrontendGsRegionV1 texture_payload;
  std::uint32_t allocator_begin = 0U;
  std::uint32_t max_rows = 1024U;
  std::uint32_t max_text_bytes = 65536U;
  std::uint32_t max_total_text_bytes = 4U * 1024U * 1024U;
  std::uint32_t max_lookup_row_visits = 1048576U;
  std::uint32_t max_total_draws = 131072U;
};

enum class RacFrontendListEffectKindV1 {
  preamble, begin, lookup, measure, timed_color, colors, bind, glyph, end,
  node_flags_write
};
struct RacFrontendListEffectV1 {
  RacFrontendListEffectKindV1 kind = RacFrontendListEffectKindV1::preamble;
  std::uint32_t source_call_pc = 0U;
  std::uint32_t row_index = 0U;
  // key/width/color/enable/TEX0/draw index/new flags, according to kind.
  std::uint64_t value = 0U;
  // Only timed_color may be observed; all other effects execute.
  bool evaluated = true;
};
struct RacFrontendListGlyphV1 {
  std::uint32_t row_index = 0U;
  bool secondary = false;
  bool shadow = false;
  std::vector<std::byte> text;
  RacIntegerGlyphPlanV1 plan;
};
struct RacFrontendListDrawPlanV1 {
  std::uint32_t final_flags = 0U;
  std::uint32_t font_source_id = 0U;
  std::uint32_t returned_word = 0U;
  std::array<std::byte, 96U> preamble_packets{};
  std::vector<RacFrontendListEffectV1> effects;
  std::vector<RacFrontendListGlyphV1> glyph_calls;
  RacFrontendGsBindingsV1 bindings;
  std::array<std::uint32_t, 8U> final_palette{};
  bool final_inline_colors_enabled = true;
  bool batch_ended = false;
};
class RacFrontendListDrawError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Executes the original main-list drawing callback21c1b0, including its
// integer glyph owner1f6668 and exact quad packets1f5800. Reached non-exact
// timed colors require matching explicit observations, never guessed host
// interpolation. Input/action, object projection, incoming GS state and RTT
// compositing are separate owners; this is not a neutral ready-to-play menu.
[[nodiscard]] RacFrontendListDrawPlanV1 execute_rac_frontend_list_draw_v1(
    const RacFrontendListDrawInputsV1 &input);

} // namespace openrc
