#pragma once

#include "openrc/rac_float_glyph.hpp"
#include "openrc/rac_frontend_gs_scope.hpp"
#include "openrc/rac_text_bank.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

enum class RacFrontendNewGameNumericKindV1 {
  timer_1f98c0,
  color_mix_1fa8a8,
  timed_color_21c6c0
};
struct RacFrontendNewGameNumericV1 {
  RacFrontendNewGameNumericKindV1 kind =
      RacFrontendNewGameNumericKindV1::timer_1f98c0;
  std::uint32_t source_call_pc = 0U;
  std::array<std::uint64_t, 3U> arguments{};
  // color_mix receives raw f12=0x3f000000; the other calls have no f12 ABI.
  std::uint32_t single_argument_bits = 0U;
  std::uint64_t returned_low64 = 0U;
  // Supplied observations of unexecuted numeric callees, not evaluated values.
  // The reached call kind, PC and all actual arguments are cross-checked.
  bool operator==(const RacFrontendNewGameNumericV1 &) const = default;
};

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
                 RacFrontendNewGameLayoutV1>;

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
};

class RacFrontendNewGameError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Complete bounded control owner21b298, used by original New Game text node.
// All selection/timer/scroll/early-return arms retain source integer semantics.
// The reached layout, float glyph and cold-binder helpers actually run; their
// numeric programs are not evaluated or substituted by host math. Timer and
// color callees are explicit ordered observations, not successful fake no-ops.
// No runtime schema, host font, renderer, source-byte embedding or implied GS
// residency. Source literals are supplied through their original owned ranges.
[[nodiscard]] RacFrontendNewGamePlanV1
plan_rac_frontend_new_game_v1(const RacFrontendNewGameInputsV1 &input);

} // namespace openrc
