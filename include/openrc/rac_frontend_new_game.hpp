#pragma once

#include "openrc/rac_float_glyph.hpp"
#include "openrc/rac_frontend_gs_scope.hpp"
#include "openrc/rac_text_bank.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace openrc {

// Compiler-only immutable original address ownership, not a runtime RAM/VM.
struct RacFrontendNewGameSourceV1 {
  std::uint32_t address = 0U;
  std::span<const std::byte> bytes;
};

enum class RacFrontendNewGameNumericKindV1 { timer_1f98c0, timed_color_21c6c0 };
struct RacFrontendNewGameNumericV1 {
  RacFrontendNewGameNumericKindV1 kind =
      RacFrontendNewGameNumericKindV1::timer_1f98c0;
  std::uint32_t source_call_pc = 0U;
  std::array<std::uint64_t, 3U> arguments{};
  // Neither call has an f12 ABI; this must be zero.
  std::uint32_t single_argument_bits = 0U;
  std::uint64_t returned_low64 = 0U;
  // True only when the callee's result was computed from its owned inputs.
  // False identifies the remaining non-exact timed-mixer observation.
  bool evaluated = false;
  // Optional diagnostic expectations never override an evaluated value.
  // The reached call kind, PC and all actual arguments are cross-checked.
  // Timer MFC1 sign-extends its word; timed-color's final PPACH/PPACB result
  // zeroes the upper32 bits. These source ABI constraints are also checked.
  bool operator==(const RacFrontendNewGameNumericV1 &) const = default;
};

// Source 1f98c0 with its actual unit time scale. CVT.S.W, ADDA(.25,.25),
// MADD(argument,1), and TRUNC are exact for -2^23 <= argument < 2^23.
// Other scales/ranges throw; there is no host-float or unknown-ACC fallback.
// The signed integer result retains the source MFC1 sign extension.
[[nodiscard]] std::uint64_t evaluate_rac_frontend_timer_v1(
    std::uint32_t argument_word, std::uint32_t time_scale_bits);

struct RacFrontendTimedColorPlanV1 {
  std::uint32_t clamped_age = 0U;
  std::uint32_t duration = 0U;
  std::uint32_t timer_calls = 0U;
  std::uint64_t left_color = 0U;
  std::uint64_t right_color = 0U;
  // Present only on the source DIV arm; these are CVT.S.W operand words.
  std::optional<std::uint32_t> division_numerator;
  std::optional<std::uint32_t> division_denominator;
  // Set only when division, SUB, both products and ACC sum are all exact.
  // A pending result does not substitute rational interpolation for DIV/MADD.
  std::optional<std::uint32_t> factor_bits;
  std::optional<std::uint64_t> returned_low64;
};

// Executes 21c6c0's signed age clamp, 64-bit -1 color defaults, timer calls,
// and its branch/operand order. Exact dyadic mixtures use integer arithmetic;
// the non-exact DIV/MADD domain remains explicitly pending. A reached DIV by
// zero also remains pending; the over-duration branch still executes normally.
[[nodiscard]] RacFrontendTimedColorPlanV1 plan_rac_frontend_timed_color_v1(
    std::uint32_t age_word, std::uint64_t left_color,
    std::uint64_t right_color, std::uint32_t timer_argument_word,
    std::uint32_t time_scale_bits);

// Evaluated original1fa8a8 call at21b814, whose actual f12 is exactly0.5.
// Separate from numeric observations: the returned low64 is computed, not
// supplied. Source unpacking discards input bits32..127; final PPACH/PPACB
// zeroes return bits32..127. This API carries the observable low64 ABI only.
struct RacFrontendNewGameHalfMixV1 {
  std::uint32_t source_call_pc = 0U;
  std::uint64_t left_color = 0U;
  std::uint64_t right_color = 0U;
  std::uint64_t returned_low64 = 0U;
  bool operator==(const RacFrontendNewGameHalfMixV1 &) const = default;
};

// Exact value-only specialization of source1fa8a8 for its reached factor0.5.
// Byte lanes0..255, their halves and their sum are all exactly representable:
// VU SUB(1,.5), ITOF0, MULAw and MADDx cannot discard a value bit, overflow or
// underflow within this domain. FTOI0 truncates the final half-integer.
// This does not qualify arbitrary factors, VU ACC/flags/latency, or timed
// color.
[[nodiscard]] std::uint64_t
mix_rac_frontend_color_half_v1(std::uint64_t left_color,
                               std::uint64_t right_color) noexcept;

struct RacFrontendNewGameWriteV1 {
  std::uint32_t source_pc = 0U;
  std::uint32_t node_offset = 0U;
  std::uint32_t width = 4U;
  std::uint32_t value = 0U;
  bool operator==(const RacFrontendNewGameWriteV1 &) const = default;
};
struct RacFrontendNewGameTextV1 {
  std::uint32_t source_call_pc = 0U;
  std::uint32_t key = 0U;
  // -1 is original missing-key diagnostic; no guessed localization fallback.
  std::int32_t row_index = -1;
  std::uint32_t returned_source_address = 0U;
  bool operator==(const RacFrontendNewGameTextV1 &) const = default;
};
struct RacFrontendNewGameControlV1 {
  enum class Kind { preamble, begin, bind, colors, format, end };
  Kind kind = Kind::preamble;
  std::uint32_t source_call_pc = 0U;
  std::uint64_t value = 0U;
  bool operator==(const RacFrontendNewGameControlV1 &) const = default;
};
struct RacFrontendNewGameGlyphLineV1 {
  std::uint32_t line_index = 0U;
  RacFloatGlyphProgramV1 program;
};
struct RacFrontendNewGameLayoutV1 {
  std::uint32_t source_call_pc = 0U;
  RacTextLayoutRequestV1 request;
  RacTextLayoutPlanV1 plan;
  // Exact bounded source text used by this call, including its known NUL.
  std::vector<std::byte> text;
  RacFrontendGsScissorV1 entry_scissor;
  RacFrontendGsScissorV1 exit_scissor;
  std::vector<RacFrontendNewGameGlyphLineV1> glyph_lines;
};
using RacFrontendNewGameEffectV1 =
    std::variant<RacFrontendNewGameWriteV1, RacFrontendNewGameNumericV1,
                 RacFrontendNewGameTextV1, RacFrontendNewGameControlV1,
                 RacFrontendNewGameLayoutV1, RacFrontendNewGameHalfMixV1>;

struct RacFrontendNewGameLimitsV1 {
  std::uint64_t max_source_bytes = 16U * 1024U * 1024U;
  std::uint32_t max_source_regions = 1024U;
  std::uint32_t max_text_bytes = 65536U;
  std::uint32_t max_bank_entries = 65536U;
  std::uint32_t max_effects = 256U;
  // At most five source layout calls; this budget applies to each call.
  std::uint64_t max_layout_byte_visits_per_call = 1048576U;
  std::uint32_t max_total_glyph_operations = 1048576U;
};

struct RacFrontendNewGameInputsV1 {
  std::uint32_t node_address = 0U;
  std::array<std::byte, 80U> node_bytes{};
  std::span<const RacFrontendNewGameSourceV1> source;
  // The caller supplies the actually selected bank and its actual relocated
  // row pointer words. A reached nonnull row must match its parsed raw text
  // bytes in source ownership. A real zero row pointer retains source null
  // fallback behavior; it is never changed to a host string.
  const RacTextBankV1 *text_bank = nullptr;
  std::span<const std::uint32_t> relocated_text_addresses;
  const RacFontMetricTablesV1 *font_metrics = nullptr;
  std::span<const RacFrontendTextureEntryV1> textures;
  RacFrontendGsRegionV1 texture_payload;
  std::uint32_t allocator_begin = 0U;
  std::span<const RacFrontendNewGameNumericV1> numeric_observations;
  // Empty observations are valid when every reached numeric call executes.
  RacFrontendNewGameLimitsV1 limits;
};

struct RacFrontendNewGamePlanV1 {
  std::array<std::byte, 80U> final_node_bytes{};
  std::vector<RacFrontendNewGameEffectV1> effects;
  std::array<std::byte, 96U> preamble_packets{};
  RacFrontendGsBindingsV1 bindings;
  std::uint32_t font_source_id = 0U;
  std::uint32_t returned_word = 1U;
  // These text-state observations are populated only after batch_reached;
  // early-return plans neither read nor invent incoming palette/color state.
  std::array<std::uint32_t, 8U> final_palette{};
  bool final_inline_colors_enabled = false;
  bool batch_reached = false;
  std::optional<RacFrontendTimedColorPlanV1> timed_color;
};

class RacFrontendNewGameError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Complete bounded control owner21b298, used by original New Game text node.
// All selection/timer/scroll/early-return arms retain source integer semantics.
// The reached layout, float glyph and cold-binder helpers actually run; their
// numeric programs are not substituted by host math. Unit-scale timers and
// exact dyadic timed colors execute. Non-exact timed DIV/MADD results still
// require an explicit observation, marked evaluated=false in the effect.
// The actual constant-half mixing call evaluates its exact bounded byte domain.
// No runtime schema, host font, renderer, source-byte embedding or implied GS
// residency. Source literals are supplied through their original owned ranges.
[[nodiscard]] RacFrontendNewGamePlanV1
plan_rac_frontend_new_game_v1(const RacFrontendNewGameInputsV1 &input);

} // namespace openrc
