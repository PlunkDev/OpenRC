#include "openrc/rac_frontend_gs_scope.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace openrc;
using Bytes = std::vector<std::byte>;
constexpr RacFrontendGsRegionV1 kPayload{0x600000U, 0x10000U};
constexpr RacFrontendGsRegionV1 kFlush{0x200000U, 48U};
constexpr std::uint32_t kCommands = 0x500000U;

void expect(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F operation, const char *message) {
  try {
    operation();
  } catch (const RacFrontendGsScopeError &) {
    return;
  }
  throw std::runtime_error(message);
}
std::uint64_t read(std::span<const std::byte> bytes, std::size_t at,
                   std::size_t width = 8U) {
  std::uint64_t result = 0U;
  for (std::size_t i = 0; i < width; ++i)
    result |= std::to_integer<std::uint64_t>(bytes[at + i]) << (i * 8U);
  return result;
}
void write(Bytes &bytes, std::size_t at, std::uint32_t word) {
  for (unsigned i = 0; i < 4U; ++i)
    bytes[at + i] = static_cast<std::byte>(word >> (8U * i));
}
std::vector<RacFrontendTextureEntryV1> catalog() {
  std::vector<RacFrontendTextureEntryV1> result(4U);
  // Synthetic IDs 0 and 3 fully alias; 0/1 share only the CLUT.
  constexpr std::array<std::array<std::uint32_t, 4U>, 4U> rows{
      {{0U, 1024U, 16U, 16U},
       {0U, 1280U, 32U, 16U},
       {2048U, 3072U, 128U, 16U},
       {0U, 1024U, 16U, 16U}}};
  for (std::size_t i = 0; i < result.size(); ++i) {
    auto &e = result[i];
    e.source_index = static_cast<std::uint32_t>(i);
    e.palette_offset = rows[i][0];
    e.pixel_offset = rows[i][1];
    e.width = rows[i][2];
    e.height = rows[i][3];
  }
  return result;
}
RacFrontendGsBindingsV1 bindings() {
  constexpr std::array ids{0U, 1U, 0U, 3U, 2U};
  return plan_rac_frontend_gs_bindings_v1(
      catalog(), ids, kPayload, 0x20000U,
      static_cast<std::uint32_t>(ids.size()));
}
RacFrontendGsBindingsV1 empty_bindings() {
  return plan_rac_frontend_gs_bindings_v1({}, {}, kPayload, 0U, 0U);
}
Bytes draw() {
  const auto scissor = emit_rac_frontend_scissor_v1({3, 100, 5, 80}, 512, 416);
  return {scissor.packet.begin(), scissor.packet.end()};
}
RacFrontendGsBatchV1 batch(const RacFrontendGsBindingsV1 &plan,
                           std::span<const std::byte> body, bool gate = true) {
  return emit_rac_frontend_gs_batch_v1(plan, body, kCommands, kFlush, gate, {},
                                       65536U);
}

void test_source_scissor_and_signed_edges() {
  const auto value =
      emit_rac_frontend_scissor_v1({-5, 1000, -9, 999}, 512, 416);
  expect(value.raw_scissor == 0x019f000001ff0000ULL,
         "Scissor did not clamp to live inclusive dimensions");
  expect(read(value.packet, 0U, 4U) == 0x10000002U &&
             read(value.packet, 12U, 4U) == 0x50000002U &&
             read(value.packet, 16U) == 0x1000000000008001ULL &&
             read(value.packet, 24U) == 0xeU &&
             read(value.packet, 32U) == value.raw_scissor &&
             read(value.packet, 40U) == 0x40U,
         "Source SCISSOR CNT/DIRECT/AD fields changed");
  expect(emit_rac_frontend_scissor_v1({7, -1, 9, 4}, 512, 416).raw_scissor ==
             0xffffffffffff0007ULL,
         "Negative upper bound was field-masked instead of sign-extended");
  expect(emit_rac_frontend_scissor_v1({0, 1, 0, 1}, 0, 0).raw_scissor ==
             0xffffffffffff0000ULL,
         "Zero live dimensions did not retain source ADDIU wrapping");
  expect(emit_rac_frontend_scissor_v1({9, 2, 8, 1}, 512, 416).raw_scissor ==
             0x0001000800020009ULL,
         "Inverted source rectangle was normalized");
  const auto edge = emit_rac_frontend_scissor_v1(
      {0x123456, std::numeric_limits<std::int32_t>::max(), 0, 0}, 0x80000000U,
      0x80000001U);
  expect(edge.raw_scissor == 0x00007fffffff3456ULL,
         "Source 32-bit width subtraction / unmasked OR differs");
}

void test_cold_cache_and_alias_allocations() {
  const auto plan = bindings();
  expect(plan.bindings.size() == 5U && plan.uploads.size() == 4U &&
             plan.bindings[0] == plan.bindings[2],
         "Repeated source ID did not hit the per-batch cold cache");
  expect(plan.uploads[0].palette == plan.uploads[1].palette &&
             plan.uploads[0].palette == plan.uploads[2].palette &&
             plan.uploads[0].pixels == plan.uploads[2].pixels &&
             plan.uploads[0].tex0 != plan.uploads[2].tex0,
         "Distinct aliased IDs were incorrectly deduplicated in GS allocation");
  expect(plan.allocator_begin == 0x20000U &&
             plan.allocator_end == 0x20000U + 4U * 1024U + 3072U,
         "Source allocator byte increments changed");
  expect(plan.uploads[0].palette_base_blocks == 512U &&
             plan.uploads[0].pixel_base_blocks == 516U &&
             ((plan.uploads[0].tex0 >> 14U) & 63U) == 1U &&
             ((plan.uploads[3].tex0 >> 14U) & 63U) == 2U &&
             (plan.uploads[0].tex0 >> 61U) == 4U,
         "Source TEX0 block, small-width DBW or CLD fields changed");
  expect(bindings().bindings == plan.bindings,
         "A new batch inherited the previous batch's cache");
}

void test_complete_chain_and_source_uploads() {
  const auto plan = bindings();
  const auto body = draw();
  const auto result = batch(plan, body);
  const auto &bytes = result.bytes;
  const std::uint32_t draw_end = 112U + static_cast<std::uint32_t>(body.size());
  const auto upload_begin = draw_end + 16U;
  const auto upload_size = 4U * 224U + 16U;
  expect(bytes.size() == 96U + 16U + 48U + 16U + upload_size + 16U &&
             result.command_region ==
                 RacFrontendGsRegionV1{
                     kCommands, static_cast<std::uint32_t>(bytes.size())},
         "Owned batch extent changed");
  expect(read(bytes, 32U) == 0x44U && read(bytes, 40U) == 0x42U &&
             read(bytes, 80U) == 0x2004bU && read(bytes, 88U) == 0x47U,
         "Original callback ALPHA/TEST order or values changed");
  expect(read(bytes, 96U, 4U) == 0x20000000U &&
             read(bytes, 100U, 4U) == kCommands + upload_begin &&
             read(bytes, draw_end + 4U, 4U) == kCommands + bytes.size() &&
             read(bytes, bytes.size() - 12U, 4U) == kCommands + 112U,
         "NEXT chain no longer executes upload suffix before the draw block");
  expect(
      result.execution_order ==
          std::vector<RacFrontendGsSegmentV1>{
              {RacFrontendGsSegmentKindV1::callback_preamble, 0U, 96U},
              {RacFrontendGsSegmentKindV1::uploads, upload_begin, upload_size},
              {RacFrontendGsSegmentKindV1::draw_block, 112U, 48U}},
      "Explicit execution order disagrees with original NEXT chain");
  expect(std::equal(body.begin(), body.end(), bytes.begin() + 112U) &&
             result.external_reads.size() == 9U && result.uploads_emitted,
         "Owned source body or explicit REF ownership changed");
  for (std::size_t i = 0U; i < plan.uploads.size(); ++i) {
    for (std::size_t plane = 0U; plane < 2U; ++plane) {
      const auto at = upload_begin + i * 224U + plane * 112U;
      const auto &upload = plan.uploads[i];
      const auto source = plane == 0U ? upload.palette : upload.pixels;
      const auto width = plane == 0U ? 16U : upload.width;
      const auto height = plane == 0U ? 16U : upload.height;
      expect(
          read(bytes, at, 4U) == 0x10000005U &&
              read(bytes, at + 12U, 4U) == 0x50000005U &&
              read(bytes, at + 16U) == 0x1000000000000003ULL &&
              read(bytes, at + 40U) == 0x50U &&
              read(bytes, at + 48U) ==
                  (width | (std::uint64_t{height} << 32U)) &&
              read(bytes, at + 56U) == 0x52U && read(bytes, at + 64U) == 0U &&
              read(bytes, at + 72U) == 0x53U &&
              read(bytes, at + 80U) ==
                  (0x0800000000008000ULL | source.size / 16U) &&
              read(bytes, at + 96U, 4U) == 0x30000000U + source.size / 16U &&
              read(bytes, at + 100U, 4U) == source.address &&
              read(bytes, at + 108U, 4U) == 0x50000000U + source.size / 16U &&
              result.external_reads[i * 2U + plane] == source,
          "Original plane upload/REF order or raw fields changed");
      const auto bitblt = read(bytes, at + 32U);
      expect(
          (bitblt & 0xffffffffULL) == 0U &&
              ((bitblt >> 48U) & 63U) == std::max(1U, width >> 6U) &&
              ((bitblt >> 56U) & 63U) == (plane == 0U ? 0U : 0x13U),
          "Small texture DBW or original CLUT/index transfer format changed");
    }
  }
  const auto flush = upload_begin + 4U * 224U;
  expect(read(bytes, flush, 4U) == 0x30000003U &&
             read(bytes, flush + 4U, 4U) == kFlush.address &&
             result.external_reads.back() == kFlush,
         "Source flush/TEX0-seed REF was removed");
}

void test_live_gate_and_zero_queue() {
  for (const auto &plan : {bindings(), empty_bindings()}) {
    const auto disabled = batch(plan, {}, false);
    expect(disabled.bytes.size() == 144U && !disabled.uploads_emitted &&
               disabled.external_reads.empty() &&
               disabled.execution_order.size() == 2U &&
               read(disabled.bytes, 100U, 4U) == kCommands + 128U &&
               read(disabled.bytes, 132U, 4U) == kCommands + 112U,
           "Disabled original gate incorrectly uploaded or inferred residency");
  }
  const auto empty = batch(empty_bindings(), {});
  expect(empty.bytes.size() == 160U && empty.uploads_emitted &&
             empty.external_reads == std::vector<RacFrontendGsRegionV1>{kFlush},
         "Enabled zero-count queue lost original unconditional flush REF");
  const auto ignored_flush = emit_rac_frontend_gs_batch_v1(
      empty_bindings(), {}, kCommands, {1U, 3U}, false, {}, 144U);
  expect(ignored_flush.bytes.size() == 144U,
         "Unreached disabled-gate flush mapping was required");
  const auto &literal = rac_frontend_gs_flush_source_v1();
  expect(read(literal, 0U) == 0x2000000000008001ULL &&
             read(literal, 8U) == 0xeeU && read(literal, 16U) == 1U &&
             read(literal, 24U) == 0x3fU &&
             read(literal, 32U) == 0x8000000511304000ULL &&
             read(literal, 40U) == 6U,
         "Original referenced TEXFLUSH/TEX0 bytes changed");
}

void test_incoming_state_is_explicit_and_preserved() {
  RacFrontendGsIncomingV1 incoming;
  incoming.registers[0x14U] = 0xfeedface01234567ULL;
  incoming.registers[0x4cU] = 0xdecafbad00ab1234ULL;
  auto body = draw();
  const auto result = emit_rac_frontend_gs_batch_v1(
      bindings(), body, kCommands, kFlush, true, incoming, 65536U);
  body[32] = std::byte{0};
  expect(result.incoming.registers == incoming.registers &&
             !result.incoming.registers[0x1aU] &&
             !result.incoming.registers[0x47U] &&
             result.bytes[112U + 32U] == std::byte{3},
         "Scope invented live GS defaults or retained borrowed draw storage");
}

void test_binding_domains_and_source_ownership() {
  const std::array ids{0U};
  auto entries = catalog();
  auto run = [&](RacFrontendGsRegionV1 payload = kPayload,
                 std::uint32_t base = 0U, std::uint32_t budget = 1U) {
    return plan_rac_frontend_gs_bindings_v1(entries, ids, payload, base,
                                            budget);
  };
  rejects([&] { (void)run(kPayload, 1U); }, "Unaligned GS allocator accepted");
  rejects([&] { (void)run(kPayload, 0x400000U); },
          "GS allocation overflow accepted");
  rejects([&] { (void)run(kPayload, 0U, 0U); }, "Bind call budget ignored");
  rejects([&] { (void)run({kPayload.address, 1024U}); },
          "Missing source pixels accepted");
  entries[0].source_index = 1U;
  rejects([&] { (void)run(); }, "Source ID mismatch accepted");
  entries = catalog();
  entries[0].pixel_offset = 1025U;
  rejects([&] { (void)run(); }, "Unaligned source QW offset accepted");
  entries = catalog();
  entries[0].pixel_offset = 0x100000U;
  rejects([&] { (void)run({kPayload.address, 0x200000U}); },
          "Source 16-bit relocated QW offset overflow accepted");
  entries = catalog();
  entries[0].width = 8U;
  entries[0].height = 16U;
  rejects([&] { (void)run(); }, "Source allocator lost sub-block granularity");
  entries[0].height = 32U;
  expect(run().uploads.front().width == 8U,
         "Lossless small-width upload rejected despite original DBW floor");
  entries[0].width = 4096U;
  entries[0].height = 1U;
  rejects([&] { (void)run(); }, "Otherwise-owned TRXREG overflow accepted");
  entries[0].width = 1024U;
  entries[0].height = 512U;
  rejects([&] { (void)run({kPayload.address, 0x100000U}); },
          "Otherwise-owned IMAGE NLOOP overflow accepted");
  entries.resize(65U);
  rejects([&] { (void)run(); },
          "Original bounded upload queue capacity ignored");
}

void test_packet_and_storage_guards() {
  const auto plan = bindings();
  auto body = draw();
  const auto exact = batch(plan, body).bytes.size();
  auto emit = [&](std::uint32_t address, RacFrontendGsRegionV1 flush,
                  std::uint32_t budget) {
    return emit_rac_frontend_gs_batch_v1(plan, body, address, flush, true, {},
                                         budget);
  };
  expect(
      emit(kCommands, kFlush, static_cast<std::uint32_t>(exact)).bytes.size() ==
          exact,
      "Exact caller capacity rejected");
  rejects(
      [&] {
        (void)emit(kCommands, kFlush, static_cast<std::uint32_t>(exact - 1U));
      },
      "Insufficient command capacity accepted");
  for (const auto address : {0U, kCommands + 1U, kPayload.address, 0x10000000U,
                             0x10000000U - static_cast<std::uint32_t>(exact)})
    rejects([&] { (void)emit(address, kFlush, 65536U); },
            "Unowned/aligned/out-of-domain NEXT command address accepted");
  for (const auto flush : {RacFrontendGsRegionV1{kCommands, 48U},
                           RacFrontendGsRegionV1{kPayload.address, 48U},
                           RacFrontendGsRegionV1{kFlush.address, 64U}})
    rejects([&] { (void)emit(kCommands, flush, 65536U); },
            "Aliased or wrong-extent flush owner accepted");
  for (const auto tag :
       {0x30000002U, 0x20000002U, 0x90000002U, 0x10000000U, 0x10000003U}) {
    body = draw();
    write(body, 0U, tag);
    rejects([&] { (void)batch(plan, body); },
            "Escaping/truncated DMA body accepted");
  }
  for (const auto field : {4U, 8U, 12U}) {
    body = draw();
    write(body, field, 1U);
    rejects([&] { (void)batch(plan, body); },
            "Non-source CNT/DIRECT prefix accepted");
  }
  body = draw();
  body.pop_back();
  rejects([&] { (void)batch(plan, body); }, "Partial final qword accepted");
}

void test_mutated_plan_rejected() {
  const auto clean = bindings();
  auto plan = clean;
  plan.uploads[0].pixels.address += 16U;
  plan.uploads[0].pixels.size += 16U;
  rejects([&] { (void)batch(plan, {}); },
          "Changed pixel owner extent accepted");
  plan = clean;
  ++plan.allocator_end;
  rejects([&] { (void)batch(plan, {}); }, "Changed allocator end accepted");
  plan = clean;
  plan.uploads[0].tex0 ^= 1U;
  rejects([&] { (void)batch(plan, {}); }, "Changed source TEX0 accepted");
  plan = clean;
  plan.uploads[1].source_id = 0U;
  rejects([&] { (void)batch(plan, {}); }, "Duplicate cold upload accepted");
  plan = clean;
  std::swap(plan.bindings[0], plan.bindings[1]);
  rejects([&] { (void)batch(plan, {}); },
          "Changed first-bind queue order accepted");
  plan = clean;
  plan.bindings.clear();
  rejects([&] { (void)batch(plan, {}); }, "Orphan source upload accepted");
  plan = clean;
  plan.bindings[0].source_id = 100U;
  rejects([&] { (void)batch(plan, {}); }, "Unbound source result accepted");
}
} // namespace

int main() {
  try {
    test_source_scissor_and_signed_edges();
    test_cold_cache_and_alias_allocations();
    test_complete_chain_and_source_uploads();
    test_live_gate_and_zero_queue();
    test_incoming_state_is_explicit_and_preserved();
    test_binding_domains_and_source_ownership();
    test_packet_and_storage_guards();
    test_mutated_plan_rejected();
    std::cout << "8 frontend GS source-scope test groups passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
