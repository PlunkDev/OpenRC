#include "openrc/rac_frontend_gs_stream.hpp"

#include <algorithm>
#include <limits>

namespace openrc {
namespace {
constexpr std::uint32_t kAddressEnd = 0x10000000U;

void require(bool condition, const char *message) {
  if (!condition)
    throw RacFrontendGsStreamError(message);
}

std::uint32_t word(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0U;
  for (unsigned i = 0; i < 4; ++i)
    value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
  return value;
}

void validate_source(RacFrontendGsSourceV1 source) {
  require(source.address != 0U && source.address < kAddressEnd &&
              (source.address & 15U) == 0U && !source.bytes.empty() &&
              (source.bytes.size() & 15U) == 0U &&
              source.bytes.size() <= kAddressEnd - source.address,
          "Invalid bounded frontend source mapping");
}

bool contains(RacFrontendGsSourceV1 source, std::uint32_t address,
              std::uint32_t size) {
  return address >= source.address && size <= source.bytes.size() &&
         address - source.address <= source.bytes.size() - size;
}
} // namespace

RacFrontendGsStreamV1 read_rac_frontend_gs_stream_v1(
    RacFrontendGsSourceV1 commands,
    std::span<const RacFrontendGsSourceV1> external_sources,
    RacFrontendGsStreamLimitsV1 limits) {
  require(limits.max_source_regions != 0U && limits.max_input_bytes != 0U &&
              limits.max_dma_tags != 0U && limits.max_gif_bytes != 0U,
          "Frontend stream requires explicit nonzero limits");
  require(external_sources.size() < limits.max_source_regions,
          "Frontend source region count exceeds limit");
  std::vector<RacFrontendGsSourceV1> sources{commands};
  sources.insert(sources.end(), external_sources.begin(),
                 external_sources.end());
  std::uint64_t input_bytes = 0U;
  for (const auto &source : sources) {
    validate_source(source);
    require(source.bytes.size() <= limits.max_input_bytes - input_bytes,
            "Frontend mapped input extent exceeds limit");
    input_bytes += source.bytes.size();
  }
  std::sort(sources.begin(), sources.end(),
            [](const auto &a, const auto &b) { return a.address < b.address; });
  for (std::size_t i = 1U; i < sources.size(); ++i)
    require(sources[i - 1U].bytes.size() <=
                sources[i].address - sources[i - 1U].address,
            "Frontend source mappings alias");

  const auto continuation =
      commands.address + static_cast<std::uint32_t>(commands.bytes.size());
  require(continuation < kAddressEnd,
          "Frontend continuation exceeds source address domain");
  auto resolve = [&](std::uint32_t address, std::uint32_t size) {
    require((address & 15U) == 0U,
            "Frontend transfer source is not qword aligned");
    for (const auto &source : sources)
      if (contains(source, address, size))
        return source.bytes.subspan(address - source.address, size);
    throw RacFrontendGsStreamError(
        "Frontend reference has no complete source owner");
  };

  RacFrontendGsStreamV1 result;
  result.continuation_address = continuation;
  auto cursor = commands.address;
  while (cursor != continuation) {
    require(result.visited_dma_tags.size() < limits.max_dma_tags,
            "Frontend DMA tag budget exhausted (possibly cyclic chain)");
    require((cursor & 15U) == 0U && contains(commands, cursor, 16U),
            "Frontend NEXT escapes command storage");
    result.visited_dma_tags.push_back(cursor);
    const auto tag = commands.bytes.subspan(cursor - commands.address, 16U);
    const auto control = word(tag, 0U);
    const auto address = word(tag, 4U);
    const auto vif0 = word(tag, 8U);
    const auto vif1 = word(tag, 12U);
    const auto qwords = control & 0xffffU;
    const auto kind = control >> 28U;
    require((control & 0x0fff0000U) == 0U && vif0 == 0U,
            "Frontend tag has unsupported DMA flags or leading VIF code");
    if (kind == 2U) {
      require(qwords == 0U && vif1 == 0U,
              "Frontend NEXT is not the original zero-payload NOP form");
      // The next iteration checks alignment/ownership before any read.
      cursor = address;
      continue;
    }
    require((kind == 1U || kind == 3U) && qwords != 0U &&
                vif1 == (0x50000000U | qwords),
            "Frontend transfer is not a complete CNT/REF DIRECT");
    const auto size = qwords * 16U;
    const auto source_address = kind == 1U ? cursor + 16U : address;
    if (kind == 1U)
      require(address == 0U && contains(commands, source_address, size),
              "Frontend CNT inline payload escapes command storage");
    const auto payload = resolve(source_address, size);
    require(size <= limits.max_gif_bytes - result.gif_bytes.size() &&
                size <= result.gif_bytes.max_size() - result.gif_bytes.size(),
            "Frontend GIF output exceeds owned capacity limit");
    result.transfers.push_back(
        {cursor, source_address, size, result.gif_bytes.size(), kind == 3U});
    result.gif_bytes.insert(result.gif_bytes.end(), payload.begin(),
                            payload.end());
    cursor += kind == 1U ? 16U + size : 16U;
  }
  return result;
}

} // namespace openrc
