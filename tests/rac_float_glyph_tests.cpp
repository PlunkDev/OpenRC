#include "openrc/rac_float_glyph.hpp"

#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace openrc;
using Bytes = std::vector<std::byte>;

void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class F> void expect_error(F action, const char *message) {
  try {
    action();
  } catch (const RacFloatGlyphError &) {
    return;
  }
  throw std::runtime_error(message);
}

Bytes bytes(std::initializer_list<unsigned> values) {
  Bytes out;
  for (auto value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

// Deliberately synthetic, including nonzero control rows and signed extremes.
RacFontMetricTableV1 metrics() {
  RacFontMetricTableV1 result;
  for (std::size_t i = 0; i < result.rows.size(); ++i) {
    result.rows[i] = {
        static_cast<std::uint8_t>(i * 17U), static_cast<std::uint8_t>(i * 13U),
        static_cast<std::int8_t>(static_cast<int>(i % 255U) - 128),
        static_cast<std::int8_t>(static_cast<int>(i % 127U) + 1)};
  }
  return result;
}

RacFloatGlyphStateV1 state() {
  RacFloatGlyphStateV1 result;
  result.rgbaq = 0xa5f0123480060c13ULL;
  result.tex0 = 0x91f023456789abcdULL;
  result.palette = {0xcafef00dU, 0xa0112233U, 0xb0223344U, 0xc0334455U,
                    0xd0445566U, 0xe0556677U, 0xf0667788U, 0x88778899U};
  return result;
}

std::vector<RacGlyphDrawV1> draws(const RacFloatGlyphProgramV1 &program) {
  std::vector<RacGlyphDrawV1> result;
  std::uint32_t next = 5U;
  for (const auto &operation : program.operations) {
    if (const auto *convert = std::get_if<RacGlyphConvertWordV1>(&operation)) {
      expect(convert->result == next++, "Nonsequential conversion value ID");
    } else if (const auto *binary = std::get_if<RacGlyphBinaryV1>(&operation)) {
      expect(binary->left < next && binary->right < next,
             "Arithmetic uses an undefined value");
      expect(binary->result == next++, "Nonsequential arithmetic value ID");
    } else {
      const auto &draw = std::get<RacGlyphDrawV1>(operation);
      expect(draw.x < next && draw.y < next && draw.width < next &&
                 draw.height < next,
             "Draw uses an undefined value");
      result.push_back(draw);
    }
  }
  expect(next == program.value_count && result.size() == program.draw_count,
         "Program value/draw totals disagree");
  return result;
}

void test_empty_and_palette_seed() {
  const auto input = state();
  const auto zero = build_rac_float_glyph_program_v1({}, 0, metrics(), input);
  expect(zero.operations.size() == 1 && zero.value_count == 6 &&
             !zero.reached_nul && zero.consumed_bytes == 0,
         "Zero limit read text or skipped the source MUL delay slot");
  expect(std::get<RacGlyphBinaryV1>(zero.operations[0]) ==
             RacGlyphBinaryV1{RacGlyphBinaryOpcodeV1::multiply_single, 5, 2, 3},
         "Initial scale*16 operation changed");
  expect(zero.palette_zero_written && zero.final_palette[0] == 0x80060c13U &&
             zero.final_rgbaq == input.rgbaq && zero.final_pen_x == 0,
         "Early return lost original palette seed or raw color");
  const auto nul =
      build_rac_float_glyph_program_v1(bytes({0, 255}), -1, metrics(), input);
  expect(nul.operations == zero.operations && nul.reached_nul,
         "Initial NUL reached poison or changed numeric operations");
  auto preserved = input;
  preserved.preserve_palette_zero = true;
  const auto result =
      build_rac_float_glyph_program_v1({}, 0, metrics(), preserved);
  expect(!result.palette_zero_written && result.final_palette == input.palette,
         "Preserved palette zero changed");
}

void test_ordinary_order_and_signed_metrics() {
  auto table = metrics();
  table.rows[65] = {255, 254, -128, -127};
  const auto input = state();
  const auto result =
      build_rac_float_glyph_program_v1(bytes({65, 0}), -1, table, input);
  const auto calls = draws(result);
  expect(result.operations.size() == 8 && calls.size() == 1 &&
             result.consumed_bytes == 1 && result.reached_nul,
         "Ordinary dispatch count changed");
  const std::vector<RacGlyphOperationV1> expected = {
      RacGlyphBinaryV1{RacGlyphBinaryOpcodeV1::multiply_single, 5, 2, 3},
      RacGlyphConvertWordV1{6, -128},
      RacGlyphBinaryV1{RacGlyphBinaryOpcodeV1::multiply_single, 7, 6, 2},
      RacGlyphBinaryV1{RacGlyphBinaryOpcodeV1::add_single, 8, 1, 7},
      calls[0],
      RacGlyphConvertWordV1{9, -127},
      RacGlyphBinaryV1{RacGlyphBinaryOpcodeV1::multiply_single, 10, 9, 2},
      RacGlyphBinaryV1{RacGlyphBinaryOpcodeV1::add_single, 11, 0, 10}};
  expect(result.operations == expected && result.final_pen_x == 11,
         "Ordinary source draw/advance ordering changed");
  expect(calls[0].x == 0 && calls[0].y == 8 && calls[0].width == 5 &&
             calls[0].height == 5 && calls[0].atlas_u == 255 &&
             calls[0].atlas_v == 254 && calls[0].rgbaq == input.rgbaq &&
             calls[0].tex0 == input.tex0,
         "Ordinary raw emitter arguments changed");
}

void test_palette_controls() {
  for (bool enabled : {false, true}) {
    for (bool preserve : {false, true}) {
      auto input = state();
      input.color_controls_enabled = enabled;
      input.preserve_palette_zero = preserve;
      Bytes text;
      for (unsigned control = 8; control < 16; ++control) {
        text.push_back(static_cast<std::byte>(control));
        text.push_back(std::byte{65});
      }
      text.push_back(std::byte{0});
      const auto result =
          build_rac_float_glyph_program_v1(text, -1, metrics(), input);
      const auto calls = draws(result);
      expect(result.consumed_bytes == 16 && calls.size() == 8 &&
                 result.operations.size() == 57,
             "Color controls drew or advanced the pen");
      for (unsigned i = 0; i < 8; ++i) {
        const auto expected = enabled
                                  ? (input.rgbaq & 0xff000000ULL) |
                                        (result.final_palette[i] & 0x00ffffffU)
                                  : input.rgbaq;
        expect(calls[i].rgbaq == expected &&
                   calls[i].source_byte_offset == i * 2 + 1,
               "Control color high32, palette ownership or byte accounting "
               "changed");
      }
      expect(result.final_rgbaq == calls.back().rgbaq,
             "Final control color differs from source state");
    }
  }
}

void test_icons_word_sign_extension() {
  for (const std::uint64_t color :
       {0xaabbccdd80060c13ULL, 0xff1122337f060c13ULL, 0x99887766fffeffffULL}) {
    auto input = state();
    input.rgbaq = color;
    const auto result = build_rac_float_glyph_program_v1(bytes({1, 65, 0}), -1,
                                                         metrics(), input);
    const auto calls = draws(result);
    const auto gray = static_cast<std::uint32_t>(
        ((color & 255) + ((color >> 8) & 255) + ((color >> 16) & 255)) / 3);
    const auto packed =
        static_cast<std::uint32_t>(color & 0xff000000ULL) | (gray * 0x10101U);
    const auto expanded = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(std::bit_cast<std::int32_t>(packed)));
    expect(calls.size() == 2 && calls[0].kind == RacGlyphDrawKindV1::icon &&
               calls[0].source_width == 24 && calls[0].source_height == 16 &&
               calls[0].rgbaq == expanded && calls[1].rgbaq == color &&
               result.final_rgbaq == color,
           "Icon gray color was canonicalized or leaked into next glyph");
    expect(calls[0].width != 5 && calls[0].height != 5,
           "Icon lost its separately executed dimension multiplies");
    expect(std::get<RacGlyphBinaryV1>(result.operations[4]) ==
                   RacGlyphBinaryV1{RacGlyphBinaryOpcodeV1::multiply_single, 9,
                                    2, 4} &&
               std::get<RacGlyphBinaryV1>(result.operations[5]) ==
                   RacGlyphBinaryV1{RacGlyphBinaryOpcodeV1::multiply_single, 10,
                                    2, 3},
           "Icon width/height source instruction order changed");
  }
}

void test_accents_and_zero_overlay_offset() {
  auto table = metrics();
  table.rows[128] = {17, 33, -12, 7};
  table.rows[192] = {255, 240, 127, 0};
  const auto input = state();
  const auto result =
      build_rac_float_glyph_program_v1(bytes({128, 0}), -1, table, input);
  const auto calls = draws(result);
  expect(result.operations.size() == 15 && calls.size() == 2 &&
             calls[0].kind == RacGlyphDrawKindV1::accent &&
             calls[0].metric_row_index == 192 &&
             calls[1].metric_row_index == 128 && calls[0].atlas_u == 255 &&
             calls[0].atlas_v == 240 && calls[0].rgbaq == input.rgbaq &&
             calls[1].rgbaq == input.rgbaq && calls[1].x == 0,
         "Accent zero X offset skipped overlay or advanced before base");
  expect(std::get<RacGlyphConvertWordV1>(result.operations[3]) ==
                 RacGlyphConvertWordV1{8, 0} &&
             std::get<RacGlyphBinaryV1>(result.operations[4]) ==
                 RacGlyphBinaryV1{RacGlyphBinaryOpcodeV1::multiply_single, 9, 8,
                                  2} &&
             std::get<RacGlyphConvertWordV1>(result.operations[5]) ==
                 RacGlyphConvertWordV1{10, 127},
         "Accent conversion-call delay slot lost instruction order");
  table.rows[128].advance_or_accent_x_offset = 0;
  const auto skipped =
      build_rac_float_glyph_program_v1(bytes({128, 0}), -1, table, input);
  expect(skipped.operations.size() == 1 && skipped.draw_count == 0,
         "Zero base advance did not suppress whole accent dispatch");
}

void test_spaces_skips_and_byte_domain() {
  auto table = metrics();
  table.rows[66].advance_or_accent_x_offset = 0;
  const auto result = build_rac_float_glyph_program_v1(bytes({32, 66, 65, 0}),
                                                       -1, table, state());
  const auto calls = draws(result);
  expect(result.consumed_bytes == 3 && result.operations.size() == 13 &&
             calls.size() == 1 && calls[0].source_byte_offset == 2 &&
             calls[0].x == 10,
         "Space lost Y conversion/advance or zero metric performed operations");
  for (unsigned byte = 1; byte < 232; ++byte) {
    const auto one = build_rac_float_glyph_program_v1(bytes({byte, 0}), -1,
                                                      metrics(), state());
    const auto count = byte >= 8 && byte < 16      ? 0U
                       : byte == 32                ? 0U
                       : byte >= 128 && byte < 168 ? 2U
                                                   : 1U;
    expect(draws(one).size() == count && one.consumed_bytes == 1,
           "Recovered byte dispatch boundary changed");
  }
  for (unsigned byte = 232; byte < 256; ++byte) {
    expect_error(
        [&] {
          (void)build_rac_float_glyph_program_v1(bytes({byte, 0}), -1,
                                                 metrics(), state());
        },
        "Unrecovered out-of-table byte accepted");
  }
}

void test_source_byte_limits_and_owned_nul() {
  for (auto limit : {-1, -2, std::numeric_limits<std::int32_t>::min(), 20}) {
    const auto all = build_rac_float_glyph_program_v1(
        bytes({8, 32, 65, 0, 255}), limit, metrics(), state());
    expect(all.consumed_bytes == 3 && all.reached_nul,
           "Negative or larger source count changed NUL semantics");
  }
  auto table = metrics();
  table.rows[66].advance_or_accent_x_offset = 0;
  for (auto byte : {8U, 32U, 65U, 66U}) {
    const auto limited =
        build_rac_float_glyph_program_v1(bytes({byte, 255}), 1, table, state());
    expect(limited.consumed_bytes == 1 && !limited.reached_nul,
           "Byte limit reached poison after consumed control/space/skip/glyph");
    (void)build_rac_float_glyph_program_v1(bytes({byte}), 1, table, state());
  }
  expect_error(
      [&] {
        (void)build_rac_float_glyph_program_v1({}, -1, metrics(), state());
      },
      "Missing initial source byte accepted");
  expect_error(
      [&] {
        (void)build_rac_float_glyph_program_v1(bytes({65}), -1, metrics(),
                                               state());
      },
      "Implicit terminator invented outside owned span");
}

void test_resource_limits() {
  auto limits = RacFloatGlyphLimitsV1{};
  limits.max_operations = 0;
  expect_error(
      [&] {
        (void)build_rac_float_glyph_program_v1({}, 0, metrics(), state(),
                                               limits);
      },
      "Initial unconditional arithmetic bypassed operation limit");
  limits.max_operations = 8;
  limits.max_consumed_bytes = 1;
  (void)build_rac_float_glyph_program_v1(bytes({65, 0}), -1, metrics(), state(),
                                         limits);
  limits.max_operations = 7;
  expect_error(
      [&] {
        (void)build_rac_float_glyph_program_v1(bytes({65, 0}), -1, metrics(),
                                               state(), limits);
      },
      "Final advance escaped operation limit");
  limits.max_operations = 100;
  expect_error(
      [&] {
        (void)build_rac_float_glyph_program_v1(bytes({8, 9, 0}), -1, metrics(),
                                               state(), limits);
      },
      "Controls bypassed consumed-byte resource limit");
  limits.max_consumed_bytes = 0;
  (void)build_rac_float_glyph_program_v1(bytes({0}), -1, metrics(), state(),
                                         limits);
  expect_error(
      [&] {
        (void)build_rac_float_glyph_program_v1(bytes({65, 0}), 1, metrics(),
                                               state(), limits);
      },
      "Nonzero text bypassed zero resource budget");
}
} // namespace

int main() {
  try {
    test_empty_and_palette_seed();
    test_ordinary_order_and_signed_metrics();
    test_palette_controls();
    test_icons_word_sign_extension();
    test_accents_and_zero_overlay_offset();
    test_spaces_skips_and_byte_domain();
    test_source_byte_limits_and_owned_nul();
    test_resource_limits();
    std::cout << "RAC float glyph tests passed (8 groups)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC float glyph tests failed: " << error.what() << '\n';
    return 1;
  }
}
