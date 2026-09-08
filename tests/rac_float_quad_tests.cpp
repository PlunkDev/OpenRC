#include "openrc/gif_gs.hpp"
#include "openrc/rac_float_quad.hpp"
#include "openrc/rac_frontend_gs_scope.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void expect(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

template <class F> void expect_error(F operation, const char *message) {
  try {
    operation();
  } catch (const openrc::RacFloatQuadError &) {
    return;
  }
  throw std::runtime_error(message);
}

std::uint64_t read(const std::span<const std::byte> bytes, const std::size_t at,
                   const unsigned size = 8U) {
  std::uint64_t value = 0U;
  for (unsigned i = 0U; i < size; ++i) {
    value |=
        static_cast<std::uint64_t>(std::to_integer<unsigned>(bytes[at + i]))
        << (8U * i);
  }
  return value;
}

openrc::RacFloatQuadInputsV1 base() {
  return {{8, 264, 8, 264},
          {0x8000, 0x8000, 0x8000, 0x8000},
          32,
          48,
          16,
          16,
          0xa5a5a5a580abcdefULL,
          0x4a0123456789abcdULL};
}

void test_packet_and_raw_state() {
  const auto input = base();
  const auto out = openrc::emit_rac_float_quad_v1(input);
  expect(out.packet && out.cull == openrc::RacFloatQuadCullV1::emitted,
         "Ordinary source quad culled");
  const auto &packet = *out.packet;
  expect(read(packet, 0, 4) == 0x10000008 && read(packet, 4, 8) == 0 &&
             read(packet, 12, 4) == 0x50000008,
         "Original DMA/VIF framing differs");
  const auto tag = read(packet, 16);
  expect((tag & 0x7fff) == 1 && ((tag >> 15) & 1) == 1 &&
             ((tag >> 46) & 1) == 0 && ((tag >> 58) & 3) == 1 &&
             (tag >> 60) == 13,
         "REGLIST GIF tag differs");
  constexpr std::array<unsigned, 13> regs{6, 0, 8, 1, 3, 5, 3,
                                          5, 3, 5, 3, 5, 8};
  const auto descriptors = read(packet, 24);
  for (unsigned i = 0; i < regs.size(); ++i) {
    expect(((descriptors >> (4U * i)) & 15) == regs[i],
           "Wrong source register order");
  }
  expect((descriptors >> 52) == 0 && read(packet, 136) == 0,
         "Unused descriptor/payload padding is nonzero");
  expect(read(packet, 32) == input.tex0 && read(packet, 56) == input.rgbaq &&
             read(packet, 40) == 0x154 && read(packet, 128) == 5,
         "Raw texture/color/primitive/clamp reset changed");
  expect(read(packet, 48) == (0xaULL | (32ULL << 4) | (48ULL << 14) |
                              (48ULL << 24) | (64ULL << 34)),
         "Region clamp source inclusive maxima changed");
  const std::array<unsigned, 4> xs{0x8000, 0x8100, 0x8000, 0x8100};
  const std::array<unsigned, 4> ys{0x8000, 0x8000, 0x8100, 0x8100};
  for (unsigned i = 0; i < 4; ++i) {
    expect(read(packet, 72 + 16 * i) ==
               (0x00fffff000000000ULL | xs[i] |
                (static_cast<std::uint64_t>(ys[i]) << 16)),
           "Four-corner strip order changed");
  }
  expect(out.source_cursor_advances ==
             std::array<std::uint32_t, 3>{16, 32, 144},
         "Source intermediate cursor publications lost");
}

void test_all_ordered_cull_gates_and_wrapping() {
  constexpr std::array<openrc::RacFloatQuadCullV1, 4> gates{
      openrc::RacFloatQuadCullV1::left_above_9000,
      openrc::RacFloatQuadCullV1::right_below_7000,
      openrc::RacFloatQuadCullV1::top_above_9000,
      openrc::RacFloatQuadCullV1::bottom_below_7000};
  for (unsigned axis = 0; axis < 4; ++axis) {
    auto input = base();
    input.screen_offset_words = {0x12345678, 0x87654321, 0x80000000,
                                 0xffffffff};
    const std::uint32_t boundary = axis % 2 == 0 ? 0x9000 : 0x7000;
    for (unsigned i = 0; i < 4; ++i)
      input.converted_words[i] = 0x8000U - input.screen_offset_words[i] + 8U;
    input.converted_words[axis] =
        boundary - input.screen_offset_words[axis] + 8U;
    expect(openrc::emit_rac_float_quad_v1(input).packet.has_value(),
           "Source equality culled");
    input.converted_words[axis] += axis % 2 == 0 ? 1U : 0xffffffffU;
    const auto out = openrc::emit_rac_float_quad_v1(input);
    expect(out.cull == gates[axis] && !out.packet &&
               out.source_cursor_advances == std::array<std::uint32_t, 3>{},
           "Wrong source cull gate or cursor effect");
  }
  auto input = base();
  input.converted_words = {0x9009, 0x7007, 0x9009, 0x7007};
  input.screen_offset_words = {};
  expect(openrc::emit_rac_float_quad_v1(input).cull == gates[0],
         "Rejection gates reordered");
  input.converted_words = {0x9008, 0x7008, 0x9008, 0x7008};
  expect(openrc::emit_rac_float_quad_v1(input).packet.has_value(),
         "Inverted surviving quad normalized/rejected");
}

void test_raw_wide_uv_and_signed_coordinates() {
  auto input = base();
  input.atlas_u = 0xaaaaaaaa80000000ULL;
  input.atlas_v = 0xbbbbbbbb00000800ULL;
  input.source_width = 0x12345678ffffffffULL;
  input.source_height = 0;
  input.converted_words[0] = 7U - input.screen_offset_words[0]; // X0=-1
  const auto out = openrc::emit_rac_float_quad_v1(input);
  const auto &packet = *out.packet;
  expect(read(packet, 72) == 0xffffffffffffffffULL,
         "Source coordinate sign-extension OR masked away");
  expect(read(packet, 64) == 0xffffffff80000000ULL,
         "UV source word addition not sign extended");
  const auto expected_clamp = 0xaULL | (input.atlas_u << 4) |
                              (0x7fffffffULL << 14) | (input.atlas_v << 24) |
                              (0x800ULL << 34);
  expect(read(packet, 48) == expected_clamp,
         "Raw64 origins or wrapped endpoints changed");
}

void test_bounded_append() {
  std::array<std::byte, 176> bytes;
  bytes.fill(std::byte{0xa5});
  auto cursor = std::size_t{16};
  expect(openrc::append_rac_float_quad_v1(base(), bytes, cursor) ==
                 openrc::RacFloatQuadCullV1::emitted &&
             cursor == 160,
         "Owned append failed");
  expect(std::all_of(bytes.begin(), bytes.begin() + 16,
                     [](auto b) { return b == std::byte{0xa5}; }) &&
             std::all_of(bytes.begin() + 160, bytes.end(),
                         [](auto b) { return b == std::byte{0xa5}; }),
         "Append damaged guards");
  const auto saved = bytes;
  for (auto invalid : {std::size_t{1}, std::size_t{48},
                       std::numeric_limits<std::size_t>::max()}) {
    cursor = invalid;
    expect_error(
        [&] { (void)openrc::append_rac_float_quad_v1(base(), bytes, cursor); },
        "Invalid append accepted");
    expect(cursor == invalid && bytes == saved,
           "Failed append mutated storage");
  }
  auto culled = base();
  culled.converted_words[0] = 0x100000;
  cursor = bytes.size();
  expect(openrc::append_rac_float_quad_v1(culled, bytes, cursor) !=
                 openrc::RacFloatQuadCullV1::emitted &&
             cursor == bytes.size() && bytes == saved,
         "Cull touched append owner");
}

void test_ordered_numeric_recipe() {
  using Op = openrc::RacFloatQuadOpcodeV1;
  const auto &steps = openrc::rac_float_quad_source_steps_v1();
  unsigned conversions = 0, reads = 0, muls = 0, adds = 0;
  for (std::size_t i = 0; i < steps.size(); ++i) {
    const auto &step = steps[i];
    expect(step.left < i + 5, "Numeric recipe forward reference");
    if (step.opcode != Op::add_immediate_word)
      expect(step.right < i + 5, "Numeric recipe forward RHS");
    conversions += step.opcode == Op::convert_single_to_word;
    reads += step.opcode == Op::read_screen_x_word ||
             step.opcode == Op::read_screen_y_word;
    muls += step.opcode == Op::multiply_single;
    adds += step.opcode == Op::add_single;
  }
  expect(conversions == 4 && reads == 4 && muls == 4 && adds == 2,
         "Numeric source operation count changed");
  expect(steps[2] == openrc::RacFloatQuadStepV1{Op::add_single, 0, 2} &&
             steps[4] ==
                 openrc::RacFloatQuadStepV1{Op::multiply_single, 7, 4} &&
             steps[13] == openrc::RacFloatQuadStepV1{Op::add_single, 1, 3},
         "Far-corner ADD-then-MUL order was reassociated");
}

// Test adapter only: copy the actual source emitter's single-tag GIF bytes
// into the existing bounded decoder's immutable packet owner. This is not a
// claim that EE PATH2 submission has become VU PATH1 execution.
openrc::DvpVuXgkickEventV1 gif_event(std::span<const std::byte> bytes) {
  expect(bytes.size() >= 16 && bytes.size() % 16 == 0,
         "Invalid integration GIF span");
  openrc::DvpVuXgkickEventV1 event;
  for (std::size_t at = 0; at < bytes.size(); at += 16) {
    openrc::DvpVuVectorV1 qword;
    for (unsigned lane = 0; lane < 4; ++lane)
      qword.lanes[lane] = {
          static_cast<std::uint32_t>(read(bytes, at + 4 * lane, 4)),
          0xffffffffU};
    event.packet_qwords.push_back(qword);
  }
  const auto low = read(bytes, 0);
  const auto regs = read(bytes, 8);
  openrc::DvpVuGifTagV1 tag;
  tag.nloop = static_cast<std::uint16_t>(low & 0x7fff);
  tag.eop = ((low >> 15) & 1) != 0;
  tag.pre = ((low >> 46) & 1) != 0;
  tag.prim = static_cast<std::uint16_t>((low >> 47) & 0x7ff);
  tag.format = static_cast<std::uint8_t>((low >> 58) & 3);
  tag.register_count = static_cast<std::uint8_t>(low >> 60);
  if (tag.register_count == 0)
    tag.register_count = 16;
  tag.payload_qword_count = bytes.size() / 16 - 1;
  tag.registers_known = true;
  for (unsigned i = 0; i < tag.register_count; ++i)
    tag.registers[i] = static_cast<std::uint8_t>((regs >> (4 * i)) & 15);
  event.tags.push_back({0, tag, 0});
  event.packet_complete = true;
  return event;
}

void test_source_quad_scope_to_gs_stream() {
  using namespace openrc;
  constexpr GifGsDecodeLimitsV1 limits{8, 64, 64, 16, 8, 8};
  auto input = base();
  input.rgbaq = 0x3f80000080604020ULL;
  const auto quad = emit_rac_float_quad_v1(input);
  const auto source_event =
      gif_event(std::span<const std::byte>(*quad.packet).subspan(16));
  const auto unknown = decode_dvp_vu_xgkick_gs_v1(source_event, limits);
  expect(unknown.vertices.size() == 4 && unknown.primitives.size() == 2 &&
             !unknown.final_primitive.attributes_known &&
             !unknown.final_texture_binding.selected_context_index,
         "Source quad invented missing incoming PRMODECONT state");

  const auto clip = emit_rac_frontend_scissor_v1({0, 511, 0, 415}, 512, 416);
  std::vector<std::byte> body(clip.packet.begin(), clip.packet.end());
  body.insert(body.end(), quad.packet->begin(), quad.packet->end());
  input.rgbaq = 0x3f00000080808080ULL;
  input.converted_words = {264, 520, 264, 520};
  const auto second = emit_rac_float_quad_v1(input);
  body.insert(body.end(), second.packet->begin(), second.packet->end());
  const auto bindings =
      plan_rac_frontend_gs_bindings_v1({}, {}, {0x600000, 0x10000}, 0, 0);
  const auto scope = emit_rac_frontend_gs_batch_v1(
      bindings, body, 0x500000, {0x200000, 48}, false, {}, 4096);
  const auto draw = scope.execution_order.back();
  expect(draw.kind == RacFrontendGsSegmentKindV1::draw_block &&
             draw.size == body.size(),
         "Actual quad/scissor packets failed source callback composition");

  // Explicit synthetic incoming PRMODECONT=1, not an inferred live default.
  std::array<std::byte, 32> seed{};
  constexpr std::array<std::uint64_t, 4> seed_words{0x1000000000008001ULL, 0xe,
                                                    1, 0x1a};
  for (unsigned word = 0; word < seed_words.size(); ++word)
    for (unsigned byte = 0; byte < 8; ++byte)
      seed[word * 8 + byte] =
          static_cast<std::byte>(seed_words[word] >> (byte * 8));
  const std::span<const std::byte> bytes(scope.bytes);
  const std::array events{
      gif_event(seed),
      gif_event(bytes.subspan(16, 32)),
      gif_event(bytes.subspan(64, 32)),
      gif_event(bytes.subspan(draw.offset + 16, 32)),
      gif_event(bytes.subspan(draw.offset + 48 + 16, 128)),
      gif_event(bytes.subspan(draw.offset + 48 + 144 + 16, 128))};
  const auto result = decode_dvp_vu_xgkick_gs_stream_v1(events, limits);
  expect(result.register_writes.size() == 30 && result.vertices.size() == 8 &&
             result.primitives.size() == 4 &&
             result.emitted_primitive_count == 4,
         "Mixed PACKED/REGLIST callback did not emit two independent quads");
  expect(result.addressed_writes[1].dispatched_address == 0x42 &&
             result.addressed_writes[1].data_low.bits == 0x44 &&
             result.addressed_writes[2].dispatched_address == 0x47 &&
             result.addressed_writes[2].data_low.bits == 0x2004b,
         "Source callback ALPHA/TEST writes lost before actual quad");
  for (unsigned i = 0; i < 8; ++i) {
    const auto &vertex = result.vertices[i];
    const unsigned corner = i % 4;
    const unsigned origin = i < 4 ? 0x8000 : 0x8100;
    expect(vertex.x == origin + (corner % 2) * 256 &&
               vertex.y == origin + (corner / 2) * 256 &&
               vertex.z == 0x00fffff0U &&
               vertex.texture.u == (32 + (corner % 2) * 16) * 16 &&
               vertex.texture.v == (48 + (corner / 2) * 16) * 16 &&
               vertex.texture.q == (i < 4 ? 1.0F : 0.5F) &&
               vertex.kick == GifGsVertexKickV1::submitted,
           "Source raw64 quad fields changed across the shared GS decoder");
    expect(vertex.primitive.attributes_known &&
               vertex.primitive.fixed_texture_coordinates &&
               vertex.primitive.alpha_blending &&
               vertex.texture_binding.context.clamp->horizontal_mode == 2 &&
               vertex.texture_binding.context.clamp->minimum_u == 32 &&
               vertex.texture_binding.context.clamp->maximum_u == 48 &&
               vertex.raster.context.scissor->scax1 == 511,
           "Source scoped clamp/scissor/attributes lost at quad vertex");
  }
  expect(result.primitives[2].vertex_indices ==
                 std::array<std::uint64_t, 3>{4, 5, 6} &&
             result.final_texture_contexts[0].clamp->horizontal_mode == 1 &&
             result.final_texture_contexts[0].clamp->vertical_mode == 1,
         "Repeated source PRIM did not reset strip or final CLAMP leaked");
}

} // namespace

int main() {
  try {
    test_packet_and_raw_state();
    test_all_ordered_cull_gates_and_wrapping();
    test_raw_wide_uv_and_signed_coordinates();
    test_bounded_append();
    test_ordered_numeric_recipe();
    test_source_quad_scope_to_gs_stream();
    std::cout << "RAC floating quad tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
