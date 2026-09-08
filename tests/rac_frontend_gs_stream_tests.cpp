#include "openrc/gif_gs.hpp"
#include "openrc/rac_float_quad.hpp"
#include "openrc/rac_frontend_gs_scope.hpp"
#include "openrc/rac_frontend_gs_stream.hpp"
#include "openrc/rac_frontend_owner.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace openrc;
using Bytes = std::vector<std::byte>;
constexpr RacFrontendGsStreamLimitsV1 kTransport{8U, 65536U, 512U, 65536U};
constexpr GifGsDecodeLimitsV1 kGif{512U, 4096U, 1024U, 64U, 64U, 1U};

void expect(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class Error, class F> void rejects(F action, const char *message) {
  try {
    action();
  } catch (const Error &) {
    return;
  }
  throw std::runtime_error(message);
}
void put(Bytes &bytes, std::size_t at, std::uint64_t value,
         unsigned width = 8U) {
  for (unsigned i = 0U; i < width; ++i)
    bytes.at(at + i) = static_cast<std::byte>(value >> (8U * i));
}
void append64(Bytes &bytes, std::uint64_t value) {
  const auto at = bytes.size();
  bytes.resize(at + 8U);
  put(bytes, at, value);
}
void tag(Bytes &bytes, unsigned format, unsigned loops, unsigned nreg,
         std::uint64_t descriptors, bool eop = true, bool pre = false,
         unsigned prim = 0U) {
  append64(bytes,
           loops | (std::uint64_t{eop} << 15U) | (std::uint64_t{pre} << 46U) |
               (std::uint64_t{prim} << 47U) | (std::uint64_t{format} << 58U) |
               (std::uint64_t{nreg & 15U} << 60U));
  append64(bytes, descriptors);
}
void dma(Bytes &bytes, std::size_t at, unsigned kind, unsigned qwc,
         std::uint32_t address = 0U) {
  put(bytes, at, (std::uint64_t{kind} << 28U) | qwc, 4U);
  put(bytes, at + 4U, address, 4U);
  put(bytes, at + 8U, 0U, 4U);
  put(bytes, at + 12U, qwc == 0U ? 0U : 0x50000000U | qwc, 4U);
}
Bytes single_nop() {
  Bytes bytes;
  tag(bytes, 0U, 1U, 1U, 0xfU);
  append64(bytes, 0U);
  append64(bytes, 0U);
  return bytes;
}

struct Fixture {
  Bytes payload = Bytes(4096U);
  RacFrontendGsBindingsV1 bindings;
  RacFrontendGsBatchV1 batch;
  Fixture(bool gate = true) {
    for (std::size_t i = 0; i < payload.size(); ++i)
      payload[i] = static_cast<std::byte>((i * 173U + 91U) & 255U);
    RacFrontendTextureEntryV1 entry;
    entry.source_index = 0U;
    entry.palette_offset = 0U;
    entry.pixel_offset = 1024U;
    entry.width = 16U;
    entry.height = 16U;
    const std::array entries{entry};
    const std::array ids{0U, 0U};
    bindings = plan_rac_frontend_gs_bindings_v1(
        entries, ids, {0x600000U, 4096U}, 0x20000U, 2U);
    RacFloatQuadInputsV1 quad;
    quad.converted_words = {8U, 264U, 8U, 264U};
    quad.screen_offset_words.fill(0x8000U);
    quad.source_width = quad.source_height = 16U;
    quad.rgbaq = 0x3f80000080808080ULL;
    quad.tex0 = bindings.bindings.front().tex0;
    const auto emitted = emit_rac_float_quad_v1(quad);
    batch = emit_rac_frontend_gs_batch_v1(bindings, *emitted.packet, 0x500000U,
                                          {0x200000U, 48U}, gate, {}, 65536U);
  }
  auto stream() const {
    const std::array sources{
        RacFrontendGsSourceV1{0x600000U, payload},
        RacFrontendGsSourceV1{0x200000U, rac_frontend_gs_flush_source_v1()}};
    return read_rac_frontend_gs_stream_v1({0x500000U, batch.bytes}, sources,
                                          kTransport);
  }
};

void test_real_owners_to_linear_gif() {
  Fixture f;
  const auto stream = f.stream();
  expect(stream.continuation_address == 0x500000U + f.batch.bytes.size() &&
             stream.visited_dma_tags.size() == 11U &&
             stream.transfers.size() == 8U,
         "Actual source transport chain extent/order differs");
  expect(stream.visited_dma_tags[2] == 0x500060U &&
             stream.visited_dma_tags[3] > stream.visited_dma_tags[10],
         "Transport used physical memory order instead of actual NEXT jumps");
  const auto decoded = decode_gif_gs_linear_stream_v1(stream.gif_bytes, kGif);
  expect(decoded.tag_count == 8U && decoded.images.size() == 2U &&
             decoded.registers.register_writes.size() == 23U &&
             decoded.registers.vertices.size() == 4U &&
             decoded.registers.emitted_primitive_count == 2U,
         "Source complete batch lost image/register/draw operations");
  expect(decoded.images[0].preceding_register_write_count == 5U &&
             decoded.images[1].preceding_register_write_count == 8U &&
             decoded.images[0].bytes ==
                 Bytes(f.payload.begin(), f.payload.begin() + 1024) &&
             decoded.images[1].bytes ==
                 Bytes(f.payload.begin() + 1024, f.payload.begin() + 1280),
         "IMAGE header and later REF payload were not joined losslessly");
  expect(!decoded.registers.final_primitive.attributes_known &&
             !decoded.registers.final_texture_binding.selected_context_index,
         "Transport invented incoming GS state or texture residency");
  auto poisoned_metadata = f;
  poisoned_metadata.batch.execution_order.clear();
  poisoned_metadata.batch.external_reads.clear();
  expect(
      poisoned_metadata.stream().gif_bytes == stream.gif_bytes,
      "Consumer trusted producer annotations instead of actual command bytes");
  f.payload.assign(f.payload.size(), std::byte{0});
  expect(decoded.images[0].bytes.front() == std::byte{91} &&
             !stream.gif_bytes.empty(),
         "Output retained borrowed source memory");
}

void test_gate_zero_does_not_resolve_unreached_sources() {
  Fixture f(false);
  const auto stream = read_rac_frontend_gs_stream_v1({0x500000U, f.batch.bytes},
                                                     {}, kTransport);
  const auto decoded = decode_gif_gs_linear_stream_v1(stream.gif_bytes, kGif);
  expect(decoded.images.empty() &&
             decoded.registers.register_writes.size() == 15U &&
             decoded.registers.vertices.size() == 4U,
         "Disabled upload gate required or fabricated external texture reads");
}

void test_source_mapping_and_budget_guards() {
  Fixture f;
  const std::array sources{
      RacFrontendGsSourceV1{0x600000U, f.payload},
      RacFrontendGsSourceV1{0x200000U, rac_frontend_gs_flush_source_v1()}};
  auto run = [&](RacFrontendGsStreamLimitsV1 limits = kTransport) {
    return read_rac_frontend_gs_stream_v1({0x500000U, f.batch.bytes}, sources,
                                          limits);
  };
  const auto expected = run();
  auto exact = kTransport;
  exact.max_source_regions = 3U;
  exact.max_input_bytes = f.batch.bytes.size() + f.payload.size() + 48U;
  exact.max_dma_tags = expected.visited_dma_tags.size();
  exact.max_gif_bytes = expected.gif_bytes.size();
  expect(run(exact).gif_bytes == expected.gif_bytes, "Exact budgets rejected");
  for (unsigned field = 0U; field < 4U; ++field) {
    auto limits = exact;
    if (field == 0U)
      --limits.max_source_regions;
    if (field == 1U)
      --limits.max_input_bytes;
    if (field == 2U)
      --limits.max_dma_tags;
    if (field == 3U)
      --limits.max_gif_bytes;
    rejects<RacFrontendGsStreamError>([&] { (void)run(limits); },
                                      "Transport budget ignored");
  }
  rejects<RacFrontendGsStreamError>(
      [&] {
        (void)read_rac_frontend_gs_stream_v1({0x500000U, f.batch.bytes}, {},
                                             kTransport);
      },
      "Unowned REF accepted");
  auto aliases = sources;
  aliases[1] = {0x600000U, f.payload};
  rejects<RacFrontendGsStreamError>(
      [&] {
        (void)read_rac_frontend_gs_stream_v1({0x500000U, f.batch.bytes},
                                             aliases, kTransport);
      },
      "Ambiguous source mapping accepted");
}

void test_transport_rejects_escaping_and_unsupported_commands() {
  Fixture f(false);
  const auto clean = f.batch.bytes;
  for (auto address : {0x500060U, 0x600000U, 0x500061U, 0x80000000U}) {
    f.batch.bytes = clean;
    put(f.batch.bytes, 100U, address, 4U);
    rejects<RacFrontendGsStreamError>(
        [&] { (void)f.stream(); },
        "Cyclic, external, unaligned or SPR NEXT was accepted");
  }
  for (auto control :
       {0x90000002U, 0x50000002U, 0x10010002U, 0x10000000U, 0x1000ffffU}) {
    f.batch.bytes = clean;
    put(f.batch.bytes, 0U, control, 4U);
    rejects<RacFrontendGsStreamError>(
        [&] { (void)f.stream(); },
        "Unsupported DMA/VIF or truncated inline payload accepted");
  }
  for (auto offset : {4U, 8U, 12U}) {
    f.batch.bytes = clean;
    put(f.batch.bytes, offset, 1U, 4U);
    rejects<RacFrontendGsStreamError>([&] { (void)f.stream(); },
                                      "Non-source CNT framing accepted");
  }
}

void test_direct_boundary_is_not_gif_boundary() {
  // Split a REGLIST payload halfway through its GIF packet, including NREG0.
  Bytes gif;
  tag(gif, 1U, 1U, 16U, 0xffffffffffffffffULL);
  for (unsigned i = 0U; i < 16U; ++i)
    append64(gif, i);
  Bytes commands(32U + gif.size());
  dma(commands, 0U, 1U, 2U);
  std::copy_n(gif.begin(), 32U, commands.begin() + 16);
  dma(commands, 48U, 1U, static_cast<unsigned>(gif.size() / 16U - 2U));
  std::copy(gif.begin() + 32, gif.end(), commands.begin() + 64);
  const auto stream =
      read_rac_frontend_gs_stream_v1({0x500000U, commands}, {}, kTransport);
  const auto decoded = decode_gif_gs_linear_stream_v1(stream.gif_bytes, kGif);
  expect(stream.gif_bytes == gif &&
             decoded.registers.register_writes.size() == 16U,
         "DIRECT split reset GIF framing or lost upper64 data");
}

void test_linear_image_padding_and_stream_limits() {
  Bytes gif = single_nop();
  tag(gif, 2U, 2U, 16U, 0xeeeeeeeeeeeeeeeeULL, true, true, 0x7ffU);
  const auto image_offset = gif.size();
  const auto fake_tag = single_nop();
  gif.insert(gif.end(), fake_tag.begin(), fake_tag.end());
  const auto tail = single_nop();
  gif.insert(gif.end(), tail.begin(), tail.end());
  const auto result = decode_gif_gs_linear_stream_v1(gif, kGif);
  expect(result.images.size() == 1U && result.images[0].bytes == fake_tag &&
             result.images[0].packet_qword_index == image_offset / 16U &&
             result.images[0].preceding_register_write_count == 1U &&
             result.registers.register_writes.size() == 2U &&
             !result.registers.final_primitive.known,
         "IMAGE payload/PRE was interpreted as register commands");
  for (unsigned field = 0U; field < 3U; ++field) {
    auto limits = kGif;
    if (field == 0U)
      limits.max_tags = 2U;
    if (field == 1U)
      limits.max_packet_qwords = gif.size() / 16U - 1U;
    if (field == 2U)
      limits.max_register_writes = 1U;
    rejects<GifGsDecodeError>(
        [&] { (void)decode_gif_gs_linear_stream_v1(gif, limits); },
        "Linear GIF aggregate limit ignored");
  }
  auto partial = gif;
  partial.pop_back();
  rejects<GifGsDecodeError>(
      [&] { (void)decode_gif_gs_linear_stream_v1(partial, kGif); },
      "Partial final GIF qword accepted");
  Bytes truncated;
  tag(truncated, 2U, 2U, 0U, 0U);
  append64(truncated, 0U);
  append64(truncated, 0U);
  rejects<GifGsDecodeError>(
      [&] { (void)decode_gif_gs_linear_stream_v1(truncated, kGif); },
      "Incomplete IMAGE accepted");
  auto no_eop = single_nop();
  no_eop[1] &= std::byte{0x7f};
  rejects<GifGsDecodeError>(
      [&] { (void)decode_gif_gs_linear_stream_v1(no_eop, kGif); },
      "Open final GIF packet accepted");
}

void test_images_and_empty_tags_preserve_pending_strip() {
  Bytes gif;
  tag(gif, 1U, 1U, 4U, 0x5510U);
  append64(gif, 4U);                    // Triangle strip.
  append64(gif, 0x4000000080604020ULL); // Explicit raw GS Q=2.
  append64(gif, 0x0000000300020001ULL);
  append64(gif, 0x0000000600050004ULL);
  tag(gif, 2U, 1U, 1U, 0U, true, true, 7U);
  append64(gif, 0xffffffffffffffffULL);
  append64(gif, 0xffffffffffffffffULL);
  // No payload means even a reserved-format tag must ignore PRE/PRIM.
  tag(gif, 3U, 0U, 0U, 0U, true, true, 7U);
  tag(gif, 1U, 1U, 2U, 0x55U);
  append64(gif, 0x0000000900080007ULL);
  append64(gif, 0x0000000c000b000aULL);
  const auto result = decode_gif_gs_linear_stream_v1(gif, kGif);
  expect(result.tag_count == 4U && result.end_of_packet_count == 4U &&
             result.registers.vertices.size() == 4U &&
             result.registers.emitted_primitive_count == 2U &&
             result.registers.final_primitive.raw == 4U &&
             result.registers.final_texture.q == 2.0F,
         "IMAGE, empty tag or EOP reset GS state/pending primitive assembly");
  expect(result.registers.primitives[0].vertex_indices ==
                 std::array<std::uint64_t, 3>{0U, 1U, 2U} &&
             result.registers.primitives[1].vertex_indices ==
                 std::array<std::uint64_t, 3>{1U, 2U, 3U},
         "Primitive assembly did not carry across IMAGE/empty tags");
  Bytes reserved;
  tag(reserved, 3U, 1U, 0U, 0U);
  append64(reserved, 0U);
  append64(reserved, 0U);
  rejects<GifGsDecodeError>(
      [&] { (void)decode_gif_gs_linear_stream_v1(reserved, kGif); },
      "Nonempty unqualified GIF format accepted");
}

void test_rtt_packet_subsequence_and_live_restore() {
  RacFrontendOwnerInputsV1 input;
  input.node_root_address = 0x300000U;
  auto &slot = input.draw_slots[1][0];
  slot.source = {0x400000U, 1U, 1U};
  slot.node_address = 0x410000U;
  slot.callback_address = 0x220000U;
  slot.x_y_width_height = {12U, 24U, 180U, 90U};
  slot.callback_return_word = 1U;
  auto &live = slot.rtt_live.emplace();
  live.depth_base_half = 0x120;
  live.depth_format_half = 0x32U;
  live.clear_color_word = 0x80402010U;
  live.clear_offset_reads = {0x7c00U, 0x7800U, 0x7c00U, 0x7800U};
  live.callback_node_after_setup = 0x420000U;
  live.callback_after_setup = 0x221000U;
  live.restore_command_cursor_word = 0x500000U;
  live.live_draw_environment = 0xa0230000U;
  live.restore_environment_region = {0x230030U, 144U};
  live.restore_width_half = 512;
  live.restore_height_half = 416;
  const auto plan = plan_rac_frontend_owner_v1(input);

  Bytes commands;
  unsigned packets = 0U;
  // This tests the emitted packet subsequence only. Opaque callbacks and
  // projectors in the enclosing plan have NOT been executed or assumed empty.
  for (const auto &effect : plan.effects) {
    const auto *packet = std::get_if<RacFrontendOwnerPacketV1>(&effect);
    if (const auto *setup = std::get_if<RacFrontendRttSetupV1>(&effect))
      packet = &setup->packet;
    if (packet) {
      commands.insert(commands.end(), packet->bytes.begin(),
                      packet->bytes.end());
      ++packets;
    }
  }
  expect(packets == 6U,
         "RTT packet subsequence lost setup, clear or REF restore");
  Bytes environment;
  tag(environment, 0U, 8U, 1U, 0xeU);
  constexpr std::array<std::uint64_t, 8> values{0x00080001U,
                                                0x02000020U,
                                                0x0000600000007000ULL,
                                                0x019f000001ff0000ULL,
                                                1U,
                                                1U,
                                                0U,
                                                0x50000U};
  constexpr std::array<unsigned, 8> addresses{0x4cU, 0x4eU, 0x18U, 0x40U,
                                              0x1aU, 0x46U, 0x45U, 0x47U};
  for (std::size_t i = 0U; i < values.size(); ++i) {
    append64(environment, values[i]);
    append64(environment, addresses[i]);
  }
  const std::array mappings{RacFrontendGsSourceV1{0x230030U, environment}};
  const auto stream = read_rac_frontend_gs_stream_v1({0x500000U, commands},
                                                     mappings, kTransport);
  const auto decoded = decode_gif_gs_linear_stream_v1(stream.gif_bytes, kGif);
  expect(stream.transfers.size() == 6U && stream.transfers.back().referenced &&
             stream.transfers.back().byte_count == 144U &&
             decoded.tag_count == 8U && decoded.end_of_packet_count == 5U &&
             decoded.registers.register_writes.size() == 28U &&
             decoded.registers.emitted_primitive_count == 2U &&
             decoded.images.empty(),
         "RTT multi-tag CNT, open clear or exact REF9 was not consumed");
  const auto &raster = decoded.registers.final_raster.context;
  expect(raster.xy_offset && raster.scissor &&
             raster.xy_offset->ofx == 0x7000U &&
             raster.xy_offset->ofy == 0x6000U &&
             raster.scissor->scax1 == 511U && raster.scissor->scay1 == 415U,
         "Restore did not consume the caller's live environment bytes");
}
} // namespace

int main() {
  try {
    test_real_owners_to_linear_gif();
    test_gate_zero_does_not_resolve_unreached_sources();
    test_source_mapping_and_budget_guards();
    test_transport_rejects_escaping_and_unsupported_commands();
    test_direct_boundary_is_not_gif_boundary();
    test_linear_image_padding_and_stream_limits();
    test_images_and_empty_tags_preserve_pending_strip();
    test_rtt_packet_subsequence_and_live_restore();
    std::cout << "Frontend transport and shared linear GIF tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
