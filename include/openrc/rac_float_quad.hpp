#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>

namespace openrc {

// Fixed source operation sequence for the floating frontend quad. Values0..3
// are raw single X/Y/width/height; value4 is raw single16 (0x41800000).
// Each step defines value5+index. Operand interpretation follows its opcode;
// read steps have no operands, add_immediate_word's right is a raw word.
// This describes original COP1/word/read order; it is NOT a numeric evaluator.
enum class RacFloatQuadOpcodeV1 {
  multiply_single,
  add_single,
  convert_single_to_word,
  read_screen_x_word,
  read_screen_y_word,
  add_word,
  add_immediate_word,
};

struct RacFloatQuadStepV1 {
  RacFloatQuadOpcodeV1 opcode = RacFloatQuadOpcodeV1::multiply_single;
  std::uint32_t left = 0U;
  std::uint32_t right = 0U;
  [[nodiscard]] bool operator==(const RacFloatQuadStepV1 &) const = default;
};

inline constexpr std::array<std::uint32_t, 4> kRacFloatQuadCoordinateValuesV1{
    11U, 16U, 22U, 26U}; // X0,X1,Y0,Y1; four raw signed words.

[[nodiscard]] const std::array<RacFloatQuadStepV1, 22> &
rac_float_quad_source_steps_v1() noexcept;

struct RacFloatQuadInputsV1 {
  // Explicit outputs of ALL FOUR source conversions, in X0,X1,Y0,Y1 order.
  // Supplying host-float guesses here does not make them qualified EE results.
  std::array<std::uint32_t, 4> converted_words{};
  // Four actual source reads, X,X,Y,Y, not one assumed stable global pair.
  std::array<std::uint32_t, 4> screen_offset_words{};
  // Exact source argument low64. Source UV endpoint ADDU/SLL operations use
  // low32/sign extension, whereas CLAMP shifts also retain original low64.
  // Glyph callers provide unsigned byte origins and16/24 by16 source extents;
  // this helper also preserves the raw arithmetic for other source callers.
  std::uint64_t atlas_u = 0U;
  std::uint64_t atlas_v = 0U;
  std::uint64_t source_width = 0U;
  std::uint64_t source_height = 0U;
  std::uint64_t rgbaq = 0U;
  std::uint64_t tex0 = 0U;
};

enum class RacFloatQuadCullV1 {
  emitted,
  left_above_9000,
  right_below_7000,
  top_above_9000,
  bottom_below_7000,
};

inline constexpr std::size_t kRacFloatQuadPacketBytesV1 = 0x90U;

struct RacFloatQuadEmissionV1 {
  std::array<std::uint32_t, 4> coordinate_words{};
  RacFloatQuadCullV1 cull = RacFloatQuadCullV1::emitted;
  // Original DMA/VIF prefix + GIF REGLIST13 payload, including its unused
  // final upper64 padding word. Not a neutral runtime package or submitted DMA.
  std::optional<std::array<std::byte, kRacFloatQuadPacketBytesV1>> packet;
  // Original cursor writes after prefix, GIFtag and payload. They describe
  // relative offsets, not a claim of atomic original publication.
  std::array<std::uint32_t, 3> source_cursor_advances{};
};

class RacFloatQuadError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compiler-only exact post-conversion projection, ordered cull and packet
// construction. Full raw color/TEX0, signed-word effects and UV/CLAMP fields
// are preserved. Partial/offscreen/inverted rectangles are not normalized.
// All numeric steps must have occurred before any cull can be credited.
// No implicit PRMODE/TEX1/TEST/ALPHA/XYOFFSET or texture residency is supplied.
[[nodiscard]] RacFloatQuadEmissionV1
emit_rac_float_quad_v1(const RacFloatQuadInputsV1 &input);

// Bounded compiler-owned append. A culled call touches neither storage nor
// cursor, even at end-of-buffer. An emitted call requires16-byte alignment
// and full capacity; failure leaves both unchanged. This owned-buffer safety
// wrapper is distinct from the source's intermediate cursor publications.
[[nodiscard]] RacFloatQuadCullV1
append_rac_float_quad_v1(const RacFloatQuadInputsV1 &input,
                         std::span<std::byte> output, std::size_t &cursor);

} // namespace openrc
