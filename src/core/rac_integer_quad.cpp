#include "openrc/rac_integer_quad.hpp"

#include <algorithm>
#include <bit>

namespace openrc {
namespace {
std::uint64_t widen(std::uint32_t word) noexcept {
  return static_cast<std::uint64_t>(
      static_cast<std::int64_t>(std::bit_cast<std::int32_t>(word)));
}
void put(std::span<std::byte> bytes, std::size_t at, std::uint64_t value,
         unsigned width = 8U) noexcept {
  for (unsigned i = 0U; i < width; ++i)
    bytes[at + i] = static_cast<std::byte>(value >> (i * 8U));
}
} // namespace

RacIntegerQuadEmissionV1
emit_rac_integer_quad_v1(const RacIntegerQuadInputsV1 &input) noexcept {
  const auto [x, y, width, height] = input.rectangle_words;
  const auto [offset_y, offset_x] = input.screen_offset_reads;
  const auto [u, v, texture_width, texture_height] = input.uv_rectangle_words;
  RacIntegerQuadEmissionV1 result;
  result.coordinate_words = {
      (x << 4U) + offset_x - 8U, ((x + width) << 4U) + offset_x - 8U,
      (y << 4U) + offset_y - 8U, ((y + height) << 4U) + offset_y - 8U};
  const std::array<std::uint32_t, 2> us{u << 4U, (u + texture_width) << 4U};
  const std::array<std::uint32_t, 2> vs{v << 20U, (v + texture_height) << 20U};
  auto &packet = result.packet;
  put(packet, 0U, 0x10000007U, 4U);
  put(packet, 12U, 0x50000007U, 4U);
  put(packet, 16U, 0xb400000000008001ULL);
  put(packet, 24U, 0x0000053535353106ULL);
  put(packet, 32U, input.tex0);
  put(packet, 40U, 0x154U);
  put(packet, 48U, input.rgbaq);
  constexpr std::array<unsigned, 4> xs{0U, 1U, 0U, 1U};
  constexpr std::array<unsigned, 4> ys{0U, 0U, 1U, 1U};
  for (std::size_t i = 0U; i < 4U; ++i) {
    put(packet, 56U + i * 16U, widen(us[xs[i]] + vs[ys[i]]));
    put(packet, 64U + i * 16U,
        widen(result.coordinate_words[xs[i]]) |
            (widen(result.coordinate_words[2U + ys[i]]) << 16U) |
            0x00fffff000000000ULL);
  }
  result.source_cursor_advances = {16U, 32U, 128U};
  return result;
}

void append_rac_integer_quad_v1(const RacIntegerQuadInputsV1 &input,
                                const std::span<std::byte> output,
                                std::size_t &cursor) {
  if ((cursor & 15U) != 0U || cursor > output.size() ||
      kRacIntegerQuadPacketBytesV1 > output.size() - cursor)
    throw RacIntegerQuadError(
        "Integer quad append needs aligned owned capacity");
  const auto emitted = emit_rac_integer_quad_v1(input);
  std::copy(emitted.packet.begin(), emitted.packet.end(),
            output.subspan(cursor).begin());
  cursor += emitted.packet.size();
}

} // namespace openrc
