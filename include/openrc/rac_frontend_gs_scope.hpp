#pragma once

#include "openrc/rac_frontend_texture.hpp"
#include "openrc/rac_text_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Compiler-side source-address regions, never host pointers. References must
// resolve to caller-owned, immutable original/prepared source bytes at submit.
struct RacFrontendGsRegionV1 {
  std::uint32_t address = 0U;
  std::uint32_t size = 0U;
  bool operator==(const RacFrontendGsRegionV1 &) const = default;
};

struct RacFrontendGsUploadV1 {
  std::uint32_t source_id = 0U;
  RacFrontendGsRegionV1 palette;
  RacFrontendGsRegionV1 pixels;
  std::uint16_t width = 0U;
  std::uint16_t height = 0U;
  std::uint16_t palette_base_blocks = 0U;
  std::uint16_t pixel_base_blocks = 0U;
  std::uint64_t tex0 = 0U;
  bool operator==(const RacFrontendGsUploadV1 &) const = default;
};

struct RacFrontendGsBindingV1 {
  std::uint32_t source_id = 0U;
  std::uint64_t tex0 = 0U;
  bool operator==(const RacFrontendGsBindingV1 &) const = default;
};

struct RacFrontendGsBindingsV1 {
  RacFrontendGsRegionV1 source_payload;
  std::uint32_t allocator_begin = 0U;
  std::uint32_t allocator_end = 0U;
  // Source order of bind calls, including repeat calls that hit this batch's
  // descriptor cache. Different source IDs remain separate even for aliases.
  std::vector<RacFrontendGsBindingV1> bindings;
  std::vector<RacFrontendGsUploadV1> uploads;
};

// Missing entries mean unknown incoming GS state, not hardware defaults. A
// downstream renderer must establish the state it consumes. This scope neither
// infers TEX1 nor mistakes source ELF display bytes for live FRAME/XYOFFSET.
struct RacFrontendGsIncomingV1 {
  std::array<std::optional<std::uint64_t>, 128U> registers{};
};

struct RacFrontendGsScissorV1 {
  std::uint64_t raw_scissor = 0U;
  std::array<std::byte, 48U> packet{};
};

enum class RacFrontendGsSegmentKindV1 {
  callback_preamble,
  uploads,
  draw_block
};
struct RacFrontendGsSegmentV1 {
  RacFrontendGsSegmentKindV1 kind =
      RacFrontendGsSegmentKindV1::callback_preamble;
  std::uint32_t offset = 0U;
  std::uint32_t size = 0U;
  bool operator==(const RacFrontendGsSegmentV1 &) const = default;
};

struct RacFrontendGsBatchV1 {
  RacFrontendGsIncomingV1 incoming;
  RacFrontendGsRegionV1 command_region;
  // Source memory order differs from execution order: NEXT jumps take the
  // upload suffix before the draw block. Final NEXT targets command_region end.
  std::vector<std::byte> bytes;
  std::vector<RacFrontendGsSegmentV1> execution_order;
  std::vector<RacFrontendGsRegionV1> external_reads;
  // No upload references/flush are emitted when the original live gate is 0.
  // False is not a claim that the texture already resides in GS memory.
  bool uploads_emitted = false;
};

class RacFrontendGsScopeError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Cold per-batch source descriptor cache / source allocator, corresponding to
// begin1f4630 then bind1f4868. Caller supplies the LIVE allocator reset base
// and the bounded source payload mapping; no ELF pointer becomes a host
// pointer. Supported lossless domain: <=64 descriptors, 256-byte allocation
// granularity, existing transfer limits, and all allocations wholly inside 4MiB
// GS memory. Unrelated level-material cache invalidations in source begin are
// not modeled.
[[nodiscard]] RacFrontendGsBindingsV1 plan_rac_frontend_gs_bindings_v1(
    std::span<const RacFrontendTextureEntryV1> textures,
    std::span<const std::uint32_t> ordered_source_ids,
    RacFrontendGsRegionV1 source_payload, std::uint32_t allocator_begin,
    std::uint32_t max_bind_calls);

// Exact source clamp-and-OR SCISSOR_1 writer234d58. Width/height are raw live
// source words. Negative/inverted results retain source sign-extension and OR
// behavior; no host rectangle normalization or independent field masking.
[[nodiscard]] RacFrontendGsScissorV1
emit_rac_frontend_scissor_v1(const RacTextClipCallV1 &clip,
                             std::uint32_t screen_width_word,
                             std::uint32_t screen_height_word) noexcept;

// Exact 48-byte read-only GIF data referenced by source234e80: TEXFLUSH=1,
// then raw TEX0_1=0x8000000511304000. This is not a replaceable texture bind:
// its CLD write must be retained in the command stream.
[[nodiscard]] const std::array<std::byte, 48U> &
rac_frontend_gs_flush_source_v1() noexcept;

// Compiler-only callback ALPHA/TEST + begin/draw/end submission scope. The draw
// span must contain only complete CNT/DIRECT packets (quad/clip packets fit),
// never caller REF/NEXT addresses. It is copied into owned output. Source data
// and flush-literal regions must not alias command storage. The flush mapping
// must own exactly rac_frontend_gs_flush_source_v1().size() bytes; the caller
// is responsible for installing those bytes at that source address. The final
// continuation address names one-past-output, not permission to dereference it.
// No hardware submission, residency inference, complete GS state fold or
// raster.
[[nodiscard]] RacFrontendGsBatchV1 emit_rac_frontend_gs_batch_v1(
    const RacFrontendGsBindingsV1 &bindings,
    std::span<const std::byte> draw_packets, std::uint32_t command_address,
    RacFrontendGsRegionV1 flush_source, bool upload_gate,
    const RacFrontendGsIncomingV1 &incoming, std::uint32_t max_output_bytes);

} // namespace openrc
