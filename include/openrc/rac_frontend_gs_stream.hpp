#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Immutable compiler-side mappings. Addresses name original/relocated source
// memory, never host pointers or runtime resource handles.
struct RacFrontendGsSourceV1 {
  std::uint32_t address = 0U;
  std::span<const std::byte> bytes;
};

struct RacFrontendGsStreamLimitsV1 {
  std::uint32_t max_source_regions = 0U; // Includes command storage.
  std::uint64_t max_input_bytes = 0U;    // All mapped bytes, not just reads.
  std::uint64_t max_dma_tags = 0U;
  std::uint64_t max_gif_bytes = 0U;
};

struct RacFrontendGsTransferV1 {
  std::uint32_t dma_tag_address = 0U;
  std::uint32_t source_address = 0U;
  std::uint32_t byte_count = 0U;
  std::uint64_t gif_byte_offset = 0U;
  bool referenced = false;
};

struct RacFrontendGsStreamV1 {
  std::vector<std::byte> gif_bytes;
  std::vector<std::uint32_t> visited_dma_tags;
  std::vector<RacFrontendGsTransferV1> transfers;
  std::uint32_t continuation_address = 0U;
};

class RacFrontendGsStreamError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Executes the complete transport domain emitted by the recovered frontend
// owners: canonical CNT/DIRECT, REF/DIRECT and zero-QWC NEXT/NOP. Reads actual
// tag addresses, not a producer's execution_order metadata. Starts at command
// base and stops BEFORE reading its one-past-end continuation. NEXT targets
// stay in command storage; REF reads require an explicit immutable mapping.
// Other DMA/VIF modes, IRQ/SPR and partial DIRECT transfers are not guessed.
// Every step/read/output is bounded; loops cannot bypass the tag/output limits.
// DIRECT boundaries are concatenated without interpreting GIF tags: an IMAGE
// header and its following REF payload may belong to different transfers.
// No DMA device, path scheduling, GS memory or rasterization is executed.
[[nodiscard]] RacFrontendGsStreamV1 read_rac_frontend_gs_stream_v1(
    RacFrontendGsSourceV1 commands,
    std::span<const RacFrontendGsSourceV1> external_sources,
    RacFrontendGsStreamLimitsV1 limits);

} // namespace openrc
