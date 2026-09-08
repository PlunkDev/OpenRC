#include "openrc/rac_float_quad.hpp"

#include <algorithm>
#include <bit>

namespace openrc {
namespace {

std::uint64_t signed_word(const std::uint32_t word) {
  return static_cast<std::uint64_t>(
      static_cast<std::int64_t>(std::bit_cast<std::int32_t>(word)));
}

std::uint32_t add_word(const std::uint64_t a, const std::uint64_t b) {
  return static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b);
}

void put(std::span<std::byte> packet, const std::size_t offset,
         const std::uint64_t value, const unsigned width) {
  for (unsigned byte = 0U; byte < width; ++byte) {
    packet[offset + byte] =
        static_cast<std::byte>((value >> (8U * byte)) & 255U);
  }
}

} // namespace

const std::array<RacFloatQuadStepV1, 22> &
rac_float_quad_source_steps_v1() noexcept {
  using Op = RacFloatQuadOpcodeV1;
  // Preserve the original operation/interleaving order, including integer
  // work in conversion call delays. Result value ID is index+5.
  static constexpr std::array<RacFloatQuadStepV1, 22> steps{{
      {Op::multiply_single, 0U, 4U},              // 5: x*16
      {Op::convert_single_to_word, 5U, 0U},       // 6
      {Op::add_single, 0U, 2U},                   // 7: x+w
      {Op::read_screen_x_word, 0U, 0U},           // 8
      {Op::multiply_single, 7U, 4U},              // 9
      {Op::add_word, 6U, 8U},                     // 10
      {Op::add_immediate_word, 10U, 0xfffffff8U}, // 11: X0
      {Op::convert_single_to_word, 9U, 0U},       // 12
      {Op::read_screen_x_word, 0U, 0U},           // 13
      {Op::multiply_single, 1U, 4U},              // 14
      {Op::add_word, 12U, 13U},                   // 15
      {Op::add_immediate_word, 15U, 0xfffffff8U}, // 16: X1
      {Op::convert_single_to_word, 14U, 0U},      // 17
      {Op::add_single, 1U, 3U},                   // 18
      {Op::read_screen_y_word, 0U, 0U},           // 19
      {Op::add_word, 17U, 19U},                   // 20
      {Op::multiply_single, 18U, 4U},             // 21
      {Op::add_immediate_word, 20U, 0xfffffff8U}, // 22: Y0
      {Op::convert_single_to_word, 21U, 0U},      // 23
      {Op::read_screen_y_word, 0U, 0U},           // 24
      {Op::add_word, 23U, 24U},                   // 25
      {Op::add_immediate_word, 25U, 0xfffffff8U}, // 26: Y1
  }};
  return steps;
}

RacFloatQuadEmissionV1
emit_rac_float_quad_v1(const RacFloatQuadInputsV1 &input) {
  RacFloatQuadEmissionV1 result;
  for (std::size_t axis = 0U; axis < result.coordinate_words.size(); ++axis) {
    result.coordinate_words[axis] =
        input.converted_words[axis] + input.screen_offset_words[axis] - 8U;
  }
  const auto &xy = result.coordinate_words;
  if (std::bit_cast<std::int32_t>(xy[0]) > 0x9000) {
    result.cull = RacFloatQuadCullV1::left_above_9000;
  } else if (std::bit_cast<std::int32_t>(xy[1]) < 0x7000) {
    result.cull = RacFloatQuadCullV1::right_below_7000;
  } else if (std::bit_cast<std::int32_t>(xy[2]) > 0x9000) {
    result.cull = RacFloatQuadCullV1::top_above_9000;
  } else if (std::bit_cast<std::int32_t>(xy[3]) < 0x7000) {
    result.cull = RacFloatQuadCullV1::bottom_below_7000;
  }
  if (result.cull != RacFloatQuadCullV1::emitted) {
    return result;
  }

  auto &packet = result.packet.emplace();
  const auto u0 = input.atlas_u;
  const auto v0 = input.atlas_v;
  const auto u1 = signed_word(add_word(u0, input.source_width));
  const auto v1 = signed_word(add_word(v0, input.source_height));
  const auto clamp =
      0xaULL | (u0 << 4U) | (u1 << 14U) | (v0 << 24U) | (v1 << 34U);
  // CNT/QWC8, with VIF DIRECT8 in the tag-transfer word.
  put(packet, 0U, (1U << 28U) | 8U, 4U);
  put(packet, 12U, (0x50U << 24U) | 8U, 4U);
  // NLOOP1, EOP1, PRE0, REGLIST, NREG13, followed by13 descriptors.
  put(packet, 16U, 1ULL | (1ULL << 15U) | (1ULL << 58U) | (13ULL << 60U), 8U);
  constexpr std::array<std::uint8_t, 13> descriptors{6, 0, 8, 1, 3, 5, 3,
                                                     5, 3, 5, 3, 5, 8};
  std::uint64_t packed_descriptors = 0U;
  for (std::size_t i = 0U; i < descriptors.size(); ++i) {
    packed_descriptors |= static_cast<std::uint64_t>(descriptors[i])
                          << (4U * i);
  }
  put(packet, 24U, packed_descriptors, 8U);
  put(packet, 32U, input.tex0, 8U);
  put(packet, 40U, 0x154U, 8U);
  put(packet, 48U, clamp, 8U);
  put(packet, 56U, input.rgbaq, 8U);
  const std::array<std::uint64_t, 4> us{u0, u1, u0, u1};
  const std::array<std::uint64_t, 4> vs{v0, v0, v1, v1};
  constexpr std::array<std::size_t, 4> xs{0U, 1U, 0U, 1U};
  constexpr std::array<std::size_t, 4> ys{2U, 2U, 3U, 3U};
  for (std::size_t i = 0U; i < 4U; ++i) {
    const auto u = static_cast<std::uint32_t>(us[i]) << 4U;
    const auto v = static_cast<std::uint32_t>(vs[i]) << 20U;
    const auto uv = signed_word(u + v);
    const auto xyz = signed_word(xy[xs[i]]) | (signed_word(xy[ys[i]]) << 16U) |
                     0x00fffff000000000ULL;
    put(packet, 64U + i * 16U, uv, 8U);
    put(packet, 72U + i * 16U, xyz, 8U);
  }
  put(packet, 128U, 5U, 8U); // CLAMP reset, not old state restoration.
  result.source_cursor_advances = {16U, 32U, 144U};
  return result;
}

RacFloatQuadCullV1 append_rac_float_quad_v1(const RacFloatQuadInputsV1 &input,
                                            const std::span<std::byte> output,
                                            std::size_t &cursor) {
  const auto result = emit_rac_float_quad_v1(input);
  if (!result.packet) {
    return result.cull;
  }
  if (cursor % 16U != 0U || cursor > output.size() ||
      result.packet->size() > output.size() - cursor) {
    throw RacFloatQuadError(
        "Original quad append needs aligned owned capacity");
  }
  std::copy(result.packet->begin(), result.packet->end(),
            output.subspan(cursor).begin());
  cursor += result.packet->size();
  return result.cull;
}

} // namespace openrc
