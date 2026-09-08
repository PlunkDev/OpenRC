#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace openrc {

struct RacIntegerQuadInputsV1 {
  // X,Y,width,height: the original pixel-coordinate caller's low words.
  std::array<std::uint32_t, 4> rectangle_words{};
  // The two actual source LW observations, in source order Y then X. Each
  // value is reused for that axis's near and far corner, unlike float owner.
  std::array<std::uint32_t, 2> screen_offset_reads{};
  // U,V,width,height, before the source wrapping endpoint additions/shifts.
  std::array<std::uint32_t, 4> uv_rectangle_words{};
  std::uint64_t rgbaq = 0U;
  std::uint64_t tex0 = 0U;
};

inline constexpr std::size_t kRacIntegerQuadPacketBytesV1 = 128U;

struct RacIntegerQuadEmissionV1 {
  // X0,X1,Y0,Y1, retaining source wrapping words and sign-extension on pack.
  std::array<std::uint32_t, 4> coordinate_words{};
  std::array<std::byte, kRacIntegerQuadPacketBytesV1> packet{};
  // Intermediate original cursor publications relative to entry, not a
  // promise of atomic visibility in the original source memory.
  std::array<std::uint32_t, 3> source_cursor_advances{};
};

class RacIntegerQuadError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Complete compiler-side integer pixel quad owner1f5800. No COP1, source call,
// CPU culling, CLAMP write or normalization. It always emits the original
// 128-byte CNT/DIRECT7 + REGLIST11 packet, including the final zero padding.
// Pixel/UV endpoint additions wrap BEFORE shifts; raw RGBAQ/TEX0 and original
// signed-word OR pollution are retained. This is the actual integer callee,
// never a replacement for a source caller that selected floating glyphs.
// Does not establish incoming GS state, texture residency or rasterization.
[[nodiscard]] RacIntegerQuadEmissionV1
emit_rac_integer_quad_v1(const RacIntegerQuadInputsV1 &input) noexcept;

// Owned append checks complete aligned capacity before copying. Rejection
// changes neither cursor nor output. This safe native publication wrapper is
// separate from the original intermediate cursor/read/write visibility.
void append_rac_integer_quad_v1(const RacIntegerQuadInputsV1 &input,
                                std::span<std::byte> output,
                                std::size_t &cursor);

} // namespace openrc
