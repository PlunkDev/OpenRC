#include "openrc/rac_float_glyph.hpp"
#include "openrc/rac_text_layout.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void expect(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class F> void expect_error(F operation, const char *message) {
  try {
    operation();
  } catch (const openrc::RacTextLayoutError &) {
    return;
  }
  throw std::runtime_error(message);
}

std::vector<std::byte> text(std::initializer_list<unsigned> values) {
  std::vector<std::byte> result;
  for (const auto value : values) {
    result.push_back(static_cast<std::byte>(value));
  }
  return result;
}

openrc::RacFontMetricTableV1 metrics() {
  openrc::RacFontMetricTableV1 result;
  for (unsigned i = 16; i < 232; ++i) {
    result.rows[i].advance_or_accent_x_offset = 10;
  }
  result.rows[32].advance_or_accent_x_offset = 4;
  return result;
}

openrc::RacTextLayoutRequestV1 request() {
  openrc::RacTextLayoutRequestV1 result;
  result.box = {0, 200, 0, 100, 0, 50, 123, 456, 16, 0, -7, 13};
  result.screen_width = 640;
  result.screen_height = 448;
  result.rgbaq = 0x1234567880112233ULL;
  result.tex0 = 0xfedcba9876543210ULL;
  result.palette = {0xdeadbeefU, 0x80010203U, 2, 3, 4, 5, 6, 7};
  return result;
}

void test_width_leaf() {
  auto font = metrics();
  font.rows[9].advance_or_accent_x_offset = -128;
  const auto bytes = text({65, 9, 66, 0, 255});
  expect(openrc::measure_rac_text_width_v1(bytes, -1, font, 10) == -108,
         "Width leaf filtered a control or lost signed advance");
  expect(openrc::measure_rac_text_width_v1(bytes, 2, font, 2) == -118,
         "Width leaf ignored exact positive limit");
  expect(openrc::measure_rac_text_width_v1({}, 0, font, 0) == 0,
         "Zero count read text");
  expect(openrc::measure_rac_text_width_v1(text({65, 255}), 1, font, 1) == 10,
         "Width leaf read poison beyond limit");
  expect_error(
      [&] { (void)openrc::measure_rac_text_width_v1(bytes, -1, font, 2); },
      "Width leaf ignored budget");
  expect_error(
      [&] {
        (void)openrc::measure_rac_text_width_v1(text({255, 0}), -1, font, 9);
      },
      "Width leaf accepted missing metric row");
}

void test_empty_and_single_line() {
  auto input = request();
  input.byte_limit = 0;
  const auto empty = openrc::plan_rac_text_layout_v1({}, metrics(), input, 0);
  expect(empty.lines.empty() && empty.box.max_width == 0 &&
             empty.box.height == 0,
         "Empty call retained old measurements");
  expect(empty.entry_clip == openrc::RacTextClipCallV1{0, 99, 0, 199} &&
             empty.exit_clip == openrc::RacTextClipCallV1{0, 639, 0, 447},
         "Source clipping call arguments changed");
  expect(empty.final_palette_zero == 0xdeadbeefU,
         "Empty layout changed palette");
  input.byte_limit = -1;
  const auto result = openrc::plan_rac_text_layout_v1(text({65, 66, 32, 67, 0}),
                                                      metrics(), input, 99);
  expect(result.lines.size() == 1 && result.lines[0].first_byte == 0 &&
             result.lines[0].last_byte_inclusive == 3 &&
             result.box.max_width == 34 && result.box.height == 16,
         "Single line source span/measure changed");
  const auto &draw = *result.lines[0].draw;
  expect(draw.x_base == 0 && draw.y_base == 50 && !draw.floating &&
             draw.subpixel_x == 0 && draw.subpixel_y == 0,
         "Integer glyph call consumed floating coordinates");
  expect(draw.initial_rgbaq == 0xffffffff80112233ULL &&
             draw.tex0 == input.tex0 &&
             result.final_palette_zero == 0x80112233U,
         "Source LW color extension/palette reset/TEX0 changed");
}

void test_source_wrap_and_limit_are_not_host_layout() {
  auto input = request();
  input.box.right = 20;
  auto result = openrc::plan_rac_text_layout_v1(text({65, 66, 67, 68, 0}),
                                                metrics(), input, 100);
  expect(result.lines.size() == 2 && result.lines[0].last_byte_inclusive == 2 &&
             result.lines[0].measured_width == 30 &&
             result.lines[1].first_byte == 3,
         "Forced source wrap no longer includes its endpoint byte");
  input.box.right = 100;
  input.byte_limit = 1;
  result = openrc::plan_rac_text_layout_v1(text({65, 66, 67, 0}), metrics(),
                                           input, 100);
  expect(result.lines.size() == 1 && result.box.max_width == 30,
         "Layout treated source boundary limit as a text truncation");
  input.byte_limit = -1;
  result = openrc::plan_rac_text_layout_v1(text({65, 1, 66, 0}), metrics(),
                                           input, 999);
  expect(result.scan_passes == 6 && result.accepted_wrap_width == 20 &&
             !result.restored_original_width && result.lines.size() == 2,
         "Source last-line balancing retry differs");
  input.inline_colors_enabled = true;
  result = openrc::plan_rac_text_layout_v1(text({65, 9, 66, 1, 67, 0}),
                                           metrics(), input, 999);
  expect(result.scan_passes > 1 && result.lines[0].initial_color_index == 1,
         "Retry incorrectly reset source carried inline color");
  expect(result.lines[0].draw->initial_rgbaq == 0xffffffff80010203ULL,
         "Retried first line lost selected signed palette word");
}

void test_visible_measurement_and_float_operands() {
  auto input = request();
  input.box.flags = 0x800b;
  input.box.anchor_x = 50;
  const auto result =
      openrc::plan_rac_text_layout_v1(text({65, 66, 0}), metrics(), input, 100);
  expect(result.box.flags == 0x800b && result.lines[0].draw->floating &&
             result.lines[0].draw->x_base == 40 &&
             result.lines[0].draw->y_base == 42 &&
             result.lines[0].draw->subpixel_x == -7 &&
             result.lines[0].draw->subpixel_y == 13,
         "Source centering or split floating operands changed");
  input.box.flags = 4;
  input.box.anchor_y = 200;
  auto measured =
      openrc::plan_rac_text_layout_v1(text({65, 0}), metrics(), input, 100);
  expect(measured.box.max_width == 10 && !measured.lines[0].draw,
         "Bottom equality must remain visible in measure-only mode");
  input.box.anchor_y = 201;
  measured =
      openrc::plan_rac_text_layout_v1(text({65, 0}), metrics(), input, 100);
  expect(measured.box.max_width == 0 && !measured.lines[0].measured_width &&
             measured.box.height == 16 &&
             measured.final_palette_zero == input.palette[0],
         "Vertically culled source line was measured/drawn or lost height");
  input.box.anchor_y = -16;
  measured =
      openrc::plan_rac_text_layout_v1(text({65, 0}), metrics(), input, 100);
  expect(measured.box.max_width == 10, "Top equality must remain visible");
}

void test_bounds_and_wrapped_outputs() {
  auto input = request();
  input.box.right = 0;
  std::vector<std::byte> bytes(33, std::byte{65});
  bytes.push_back(std::byte{0});
  expect_error(
      [&] {
        (void)openrc::plan_rac_text_layout_v1(bytes, metrics(), input, 999);
      },
      "Source stack-array overflow did not fail explicitly");
  bytes.erase(bytes.begin());
  input.box.line_spacing = 32767;
  const auto result =
      openrc::plan_rac_text_layout_v1(bytes, metrics(), input, 999);
  expect(result.lines.size() == 32 && result.box.height == -32,
         "Height store did not retain source low16 wrapping");
  input = request();
  expect_error(
      [&] {
        (void)openrc::plan_rac_text_layout_v1(text({65}), metrics(), input,
                                              999);
      },
      "Unterminated reached source span did not fail");
  expect_error(
      [&] {
        (void)openrc::plan_rac_text_layout_v1(text({65, 0}), metrics(), input,
                                              0);
      },
      "Layout ignored work budget");
  input.byte_limit = 0;
  expect(openrc::plan_rac_text_layout_v1(text({255}), metrics(), input, 0)
             .lines.empty(),
         "Zero layout limit read poison");
}

void test_signed_source_index_boundary() {
  auto font = metrics();
  font.rows[65].advance_or_accent_x_offset = 0;
  std::vector<std::byte> bytes(32767, std::byte{65});
  bytes.push_back(std::byte{0});
  const auto result =
      openrc::plan_rac_text_layout_v1(bytes, font, request(), 100000);
  expect(result.lines.size() == 1 && result.lines[0].first_byte == 0 &&
             result.lines[0].last_byte_inclusive == 32766,
         "Last valid source signed-halfword span changed");
  bytes.insert(bytes.begin(), std::byte{65});
  expect_error(
      [&] {
        (void)openrc::plan_rac_text_layout_v1(bytes, font, request(), 100000);
      },
      "Wrapped source halfword index was treated as unsigned");
}

void test_layout_to_original_float_glyph_boundary() {
  auto input = request();
  input.box.flags = 8;
  input.inline_colors_enabled = true;
  const auto bytes = text({65, 9, 66, 1, 67, 0});
  const auto font = metrics();
  const auto layout = openrc::plan_rac_text_layout_v1(bytes, font, input, 999);
  unsigned total_glyphs = 0;
  for (const auto &line : layout.lines) {
    expect(line.draw && line.draw->floating,
           "Missing original floating row call");
    const auto &draw = *line.draw;
    openrc::RacFloatGlyphStateV1 state;
    state.rgbaq = draw.initial_rgbaq;
    state.tex0 = draw.tex0;
    state.palette = input.palette;
    state.palette[0] = draw.palette_zero_write;
    state.preserve_palette_zero = true;
    state.color_controls_enabled = input.inline_colors_enabled;
    const auto count = line.last_byte_inclusive - line.first_byte + 1;
    const auto glyphs = openrc::build_rac_float_glyph_program_v1(
        std::span<const std::byte>(bytes).subspan(
            static_cast<std::size_t>(line.first_byte)),
        count, font, state);
    expect(!glyphs.palette_zero_written &&
               glyphs.final_palette[0] == 0x80112233U,
           "Nested glyph call reseeded palette with selected line color");
    expect(glyphs.consumed_bytes == static_cast<unsigned>(count),
           "Nested glyph call escaped exact source line count");
    for (const auto &operation : glyphs.operations) {
      if (const auto *quad = std::get_if<openrc::RacGlyphDrawV1>(&operation)) {
        expect(quad->source_byte != 1 && quad->source_byte != 9,
               "Layout newline/color became a displayed glyph");
        expect(quad->tex0 == input.tex0,
               "Nested glyph call changed texture bits");
      }
    }
    total_glyphs += glyphs.draw_count;
  }
  expect(total_glyphs == 3,
         "Source layout/glyph composition lost ordinary letters");
}

} // namespace

int main() {
  try {
    test_width_leaf();
    test_empty_and_single_line();
    test_source_wrap_and_limit_are_not_host_layout();
    test_visible_measurement_and_float_operands();
    test_bounds_and_wrapped_outputs();
    test_signed_source_index_boundary();
    test_layout_to_original_float_glyph_boundary();
    std::cout << "RAC text layout tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
