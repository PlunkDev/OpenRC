#pragma once

#include "openrc/rac_font_metrics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace openrc {

using RacGlyphValueIdV1 = std::uint32_t;
inline constexpr RacGlyphValueIdV1 kRacGlyphInputXV1 = 0U;
inline constexpr RacGlyphValueIdV1 kRacGlyphInputYV1 = 1U;
inline constexpr RacGlyphValueIdV1 kRacGlyphInputScaleV1 = 2U;
inline constexpr RacGlyphValueIdV1 kRacGlyphSingle16V1 = 3U;
inline constexpr RacGlyphValueIdV1 kRacGlyphSingle24V1 = 4U;
inline constexpr std::uint32_t kRacGlyphSingle16BitsV1 = 0x41800000U;
inline constexpr std::uint32_t kRacGlyphSingle24BitsV1 = 0x41c00000U;

struct RacGlyphConvertWordV1 {
  RacGlyphValueIdV1 result = 0U;
  std::int32_t word = 0;
  bool operator==(const RacGlyphConvertWordV1 &) const = default;
};

enum class RacGlyphBinaryOpcodeV1 { multiply_single, add_single };
struct RacGlyphBinaryV1 {
  RacGlyphBinaryOpcodeV1 opcode = RacGlyphBinaryOpcodeV1::multiply_single;
  RacGlyphValueIdV1 result = 0U;
  RacGlyphValueIdV1 left = 0U;
  RacGlyphValueIdV1 right = 0U;
  bool operator==(const RacGlyphBinaryV1 &) const = default;
};

enum class RacGlyphDrawKindV1 { ordinary, accent, icon };
struct RacGlyphDrawV1 {
  RacGlyphDrawKindV1 kind = RacGlyphDrawKindV1::ordinary;
  std::uint32_t source_byte_offset = 0U;
  std::uint8_t source_byte = 0U;
  std::uint16_t metric_row_index = 0U;
  RacGlyphValueIdV1 x = 0U;
  RacGlyphValueIdV1 y = 0U;
  RacGlyphValueIdV1 width = 0U;
  RacGlyphValueIdV1 height = 0U;
  std::uint8_t atlas_u = 0U;
  std::uint8_t atlas_v = 0U;
  std::uint8_t source_width = 16U;
  std::uint8_t source_height = 16U;
  // Verbatim low 64-bit arguments of the original quad emitter, NOT RGBA8.
  std::uint64_t rgbaq = 0U;
  std::uint64_t tex0 = 0U;
  bool operator==(const RacGlyphDrawV1 &) const = default;
};

using RacGlyphOperationV1 =
    std::variant<RacGlyphConvertWordV1, RacGlyphBinaryV1, RacGlyphDrawV1>;

struct RacFloatGlyphStateV1 {
  std::uint64_t rgbaq = 0U;
  std::uint64_t tex0 = 0U;
  std::array<std::uint32_t, 8U> palette{};
  bool color_controls_enabled = true;
  bool preserve_palette_zero = false;
};

struct RacFloatGlyphLimitsV1 {
  std::uint32_t max_consumed_bytes = 65536U;
  std::uint32_t max_operations = 1048576U;
};

struct RacFloatGlyphProgramV1 {
  // IDs 0..2 are symbolic caller X/Y/scale. IDs 3/4 are raw single 16/24.
  // Each conversion/arithmetic instruction defines the next ID, starting at 5.
  // Draw calls are interleaved, never moved across pen arithmetic. They still
  // require the separately recovered original emitter and EE numeric behavior.
  std::vector<RacGlyphOperationV1> operations;
  std::uint32_t value_count = 5U;
  std::uint32_t consumed_bytes = 0U;
  std::uint32_t draw_count = 0U;
  bool reached_nul = false;
  // This source global write occurs BEFORE every operation, even for limit 0.
  bool palette_zero_written = false;
  std::array<std::uint32_t, 8U> final_palette{};
  // Internal diagnostic state, not an original function return-value contract.
  std::uint64_t final_rgbaq = 0U;
  RacGlyphValueIdV1 final_pen_x = kRacGlyphInputXV1;
};

class RacFloatGlyphError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compiler-only source plan of the original float glyph/control dispatch.
// No floating-point operation is evaluated here: no hostfloat, reassociation,
// conversion shortcut, FCSR claim, clipping, sampling, blending or rendering.
// The text span must own every examined byte, including NUL unless byte_limit
// stops first. A reached unsupported metric index is rejected, not substituted.
// byte_limit has the sign-extended 32-bit caller contract; its raw word is
// compared with the source byte counter. Negative values are not rewritten to
// an artificial positive limit. Resource limits independently bound the plan.
[[nodiscard]] RacFloatGlyphProgramV1 build_rac_float_glyph_program_v1(
    std::span<const std::byte> text, std::int32_t byte_limit,
    const RacFontMetricTableV1 &metrics, const RacFloatGlyphStateV1 &state,
    const RacFloatGlyphLimitsV1 &limits = {});

} // namespace openrc
