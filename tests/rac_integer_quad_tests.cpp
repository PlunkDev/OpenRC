#include "openrc/gif_gs.hpp"
#include "openrc/rac_frontend_gs_stream.hpp"
#include "openrc/rac_frontend_owner.hpp"
#include "openrc/rac_integer_quad.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace openrc;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class Error, class F> void rejects(F action) {
  try {
    action();
  } catch (const Error &) {
    return;
  }
  throw std::runtime_error("Expected explicit rejection");
}
std::uint64_t read(std::span<const std::byte> bytes, std::size_t at,
                   unsigned width = 8U) {
  std::uint64_t value = 0U;
  for (unsigned i = 0U; i < width; ++i)
    value |= std::to_integer<std::uint64_t>(bytes[at + i]) << (8U * i);
  return value;
}
RacIntegerQuadInputsV1 fixture() {
  return {{0U, 0U, 16U, 16U},
          {0x8000U, 0x8000U},
          {1U, 2U, 3U, 4U},
          0x4000000080402010ULL,
          0x8123456789abcdefULL};
}
GifGsLinearDecodeResultV1 decode(std::span<const std::byte> commands) {
  const auto stream = read_rac_frontend_gs_stream_v1({0x400000U, commands}, {},
                                                     {1U, 8192U, 64U, 8192U});
  return decode_gif_gs_linear_stream_v1(stream.gif_bytes,
                                        {64U, 512U, 512U, 32U, 32U, 1U});
}

void test_complete_packet_and_no_invented_clamp() {
  const auto in = fixture();
  const auto out = emit_rac_integer_quad_v1(in);
  check(out.coordinate_words ==
            std::array<std::uint32_t, 4>{0x7ff8U, 0x80f8U, 0x7ff8U, 0x80f8U},
        "Original pixel coordinate projection differs");
  check(out.source_cursor_advances ==
            std::array<std::uint32_t, 3>{16U, 32U, 128U},
        "Source cursor publications differ");
  check(read(out.packet, 0U, 4U) == 0x10000007U &&
            read(out.packet, 12U, 4U) == 0x50000007U &&
            read(out.packet, 16U) == 0xb400000000008001ULL &&
            read(out.packet, 24U) == 0x53535353106ULL &&
            read(out.packet, 32U) == in.tex0 &&
            read(out.packet, 48U) == in.rgbaq && read(out.packet, 120U) == 0U,
        "Complete source packet header/raw words/padding differ");
  check(read(out.packet, 56U) == 0x200010U &&
            read(out.packet, 104U) == 0x600040U &&
            read(out.packet, 64U) == 0x00fffff07ff87ff8ULL,
        "Original UV/XYZ packing differs");
  const auto gs = decode(out.packet);
  check(gs.registers.register_writes.size() == 11U &&
            gs.registers.vertices.size() == 4U &&
            gs.registers.emitted_primitive_count == 2U &&
            gs.registers.final_texture.q == 2.0F &&
            !gs.registers.final_texture_contexts[0].clamp,
        "Integer quad invented CLAMP or lost source draw state");
  check(!gs.registers.final_primitive.attributes_known,
        "Integer quad invented inherited PRMODECONT");
}

void test_wrapping_sign_extension_and_no_culling() {
  auto in = fixture();
  in.rectangle_words = {0x0fffffffU, 0xffffffffU, 1U, 2U};
  in.screen_offset_reads = {0U, 0x8000U};
  in.uv_rectangle_words = {0x08000000U, 0U, 0U, 0U};
  const auto out = emit_rac_integer_quad_v1(in);
  check(out.coordinate_words ==
            std::array<std::uint32_t, 4>{0x7fe8U, 0x7ff8U, 0xffffffe8U, 8U},
        "Source endpoint/offset word wrap changed");
  check(read(out.packet, 56U) == 0xffffffff80000000ULL &&
            read(out.packet, 64U) == 0xffffffffffe87fe8ULL,
        "Source sign extension or OR pollution was normalized");
  in.uv_rectangle_words = {0x10000U, 1U, 0U, 0U};
  const auto overlapping = emit_rac_integer_quad_v1(in);
  for (std::size_t at = 56U; at <= 104U; at += 16U)
    check(read(overlapping.packet, at) == 0x00200000U,
          "Overlapping UV fields require source addition, not OR");
  for (const auto extent : {0U, 0xffffffffU, 0x80000000U}) {
    in.rectangle_words = {0x20000000U, 0x10000000U, extent, extent};
    const auto emitted = emit_rac_integer_quad_v1(in);
    check(emitted.packet.size() == 128U &&
              decode(emitted.packet).registers.vertices.size() == 4U,
          "CPU culling was invented for zero/inverted/offscreen input");
  }
}

void test_raw_register_bits_and_independent_offsets() {
  auto in = fixture();
  in.screen_offset_reads = {0x7200U, 0x8300U};
  check(emit_rac_integer_quad_v1(in).coordinate_words ==
            std::array<std::uint32_t, 4>{0x82f8U, 0x83f8U, 0x71f8U, 0x72f8U},
        "Source Y-then-X observation order changed");
  for (unsigned bit = 0; bit < 64U; ++bit) {
    in.rgbaq = std::uint64_t{1U} << bit;
    in.tex0 = ~in.rgbaq;
    const auto out = emit_rac_integer_quad_v1(in);
    check(read(out.packet, 48U) == in.rgbaq && read(out.packet, 32U) == in.tex0,
          "Full raw source argument bits were not retained");
  }
}

void test_owned_append_atomicity() {
  const auto in = fixture();
  const auto out = emit_rac_integer_quad_v1(in);
  std::vector<std::byte> bytes(160U, std::byte{0x93});
  std::size_t cursor = 16U;
  append_rac_integer_quad_v1(in, bytes, cursor);
  check(cursor == 144U &&
            std::equal(out.packet.begin(), out.packet.end(),
                       bytes.begin() + 16) &&
            bytes.front() == std::byte{0x93} && bytes.back() == std::byte{0x93},
        "Owned append extent differs");
  for (const auto initial : {1U, 48U, 160U, 176U}) {
    auto position = static_cast<std::size_t>(initial);
    const auto before = bytes;
    rejects<RacIntegerQuadError>(
        [&] { append_rac_integer_quad_v1(in, bytes, position); });
    check(position == initial && bytes == before,
          "Failed append changed output/cursor");
  }
}

void test_real_owner_composite_binding() {
  RacFrontendOwnerInputsV1 input;
  input.node_root_address = 0x300000U;
  auto &slot = input.draw_slots[1][0];
  slot.source = {0x400000U, 1U, 1U};
  slot.node_address = 0x410000U;
  slot.callback_address = 0x220000U;
  slot.x_y_width_height = {12U, 24U, 180U, 90U};
  slot.callback_return_word = 8U;
  auto &live = slot.rtt_live.emplace();
  live.depth_base_half = 0x120;
  live.callback_node_after_setup = 0x420000U;
  live.callback_after_setup = 0x221000U;
  live.restore_command_cursor_word = 0x500000U;
  live.live_draw_environment = 0x230000U;
  live.restore_environment_region = {0x230030U, 144U};
  live.restore_width_half = 512;
  live.restore_height_half = 416;
  live.composite_tex0 = 0x8123456789abcdefULL;
  const auto plan = plan_rac_frontend_owner_v1(input);
  unsigned executed = 0U;
  for (const auto &effect : plan.effects) {
    const auto *call = std::get_if<RacFrontendOwnerCallV1>(&effect);
    if (!call ||
        call->kind != RacFrontendOwnerCallKindV1::composite_quad_1f5800)
      continue;
    const auto packet =
        emit_rac_frontend_composite_packet_v1(*call, {0x7600U, 0x8100U});
    const auto gs = decode(packet.bytes);
    check(packet.source_owner == 0x1f5800U && packet.external_reads.empty() &&
              read(packet.bytes, 32U) == *live.composite_tex0 &&
              read(packet.bytes, 48U) == 0x80808080ULL &&
              read(packet.bytes, 56U) == 0x01300260U &&
              gs.registers.emitted_primitive_count == 2U,
          "Actual original RTT crop/composite call did not reach shared GS "
          "consumer");
    auto wrong = *call;
    wrong.kind = RacFrontendOwnerCallKindV1::callback;
    rejects<RacFrontendOwnerError>(
        [&] { (void)emit_rac_frontend_composite_packet_v1(wrong, {0U, 0U}); });
    wrong = *call;
    wrong.source_call_pc += 4U;
    rejects<RacFrontendOwnerError>(
        [&] { (void)emit_rac_frontend_composite_packet_v1(wrong, {0U, 0U}); });
    wrong = *call;
    wrong.callback_address = 0x220000U;
    rejects<RacFrontendOwnerError>(
        [&] { (void)emit_rac_frontend_composite_packet_v1(wrong, {0U, 0U}); });
    ++executed;
  }
  check(executed == 1U, "Expected one reached original composite");
}
} // namespace

int main() {
  try {
    test_complete_packet_and_no_invented_clamp();
    test_wrapping_sign_extension_and_no_culling();
    test_raw_register_bits_and_independent_offsets();
    test_owned_append_atomicity();
    test_real_owner_composite_binding();
    std::cout << "5 original integer quad test groups passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
