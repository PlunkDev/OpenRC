#include "openrc/rac_frontend_gs_scope.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace openrc {
namespace {
constexpr std::uint32_t kAddressEnd = 0x10000000U;
constexpr std::uint32_t kGsBytes = 0x400000U;

void put(std::span<std::byte> out, std::size_t offset, std::uint64_t value,
         std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    out[offset + i] = static_cast<std::byte>(value >> (i * 8U));
  }
}

std::uint32_t word(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0U;
  for (std::size_t i = 0; i < 4U; ++i) {
    value |=
        static_cast<std::uint32_t>(std::to_integer<unsigned>(bytes[offset + i]))
        << (i * 8U);
  }
  return value;
}

void region(RacFrontendGsRegionV1 value) {
  if (value.address == 0U || (value.address & 15U) != 0U ||
      (value.size & 15U) != 0U || value.address >= kAddressEnd ||
      value.size > kAddressEnd - value.address) {
    throw RacFrontendGsScopeError(
        "Invalid bounded frontend source-address region");
  }
}

bool overlaps(RacFrontendGsRegionV1 a, RacFrontendGsRegionV1 b) {
  return a.address < b.address + b.size && b.address < a.address + a.size;
}

bool contains(RacFrontendGsRegionV1 owner, RacFrontendGsRegionV1 part) {
  return part.address >= owner.address && part.size <= owner.size &&
         part.address - owner.address <= owner.size - part.size;
}

std::uint32_t pixel_count(std::uint32_t width, std::uint32_t height) {
  if (!std::has_single_bit(width) || !std::has_single_bit(height) ||
      width > 2048U || height > 2048U) {
    throw RacFrontendGsScopeError(
        "Frontend binding dimensions exceed transfer domain");
  }
  const auto pixels = width * height;
  if ((pixels & 255U) != 0U || pixels / 16U > 0x7fffU) {
    throw RacFrontendGsScopeError(
        "Frontend allocation loses source block or IMAGE extent");
  }
  return pixels;
}

std::uint64_t tex0(std::uint32_t base, std::uint32_t width,
                   std::uint32_t height) {
  const auto tw = static_cast<std::uint64_t>(std::countr_zero(width));
  const auto th = static_cast<std::uint64_t>(std::countr_zero(height));
  return ((base + 1024U) >> 8U) |
         (static_cast<std::uint64_t>(std::max(1U, width >> 6U)) << 14U) |
         (0x13ULL << 20U) | (tw << 26U) | (th << 30U) | (1ULL << 34U) |
         (static_cast<std::uint64_t>(base >> 8U) << 37U) | (1ULL << 63U);
}

void gs_write(std::span<std::byte> packet, std::uint8_t address,
              std::uint64_t value) {
  put(packet, 0U, 0x10000002U, 4U);
  put(packet, 12U, 0x50000002U, 4U);
  put(packet, 16U, 0x1000000000008001ULL, 8U);
  put(packet, 24U, 0xeU, 8U);
  put(packet, 32U, value, 8U);
  put(packet, 40U, address, 8U);
}

void next(std::span<std::byte> bytes, std::size_t offset,
          std::uint32_t target) {
  put(bytes, offset, 0x20000000U, 4U);
  put(bytes, offset + 4U, target, 4U);
}

void transfer(std::span<std::byte> bytes, std::size_t at,
              RacFrontendGsRegionV1 source, std::uint16_t destination,
              std::uint32_t width, std::uint32_t height, bool palette) {
  // Source20c2f8 templates plus their original explicit field writes.
  put(bytes, at, 0x10000005U, 4U);
  put(bytes, at + 12U, 0x50000005U, 4U);
  put(bytes, at + 16U, 0x1000000000000003ULL, 8U);
  put(bytes, at + 24U, 0xeU, 8U);
  const auto dbw = std::max(1U, width >> 6U);
  const auto bitblt = static_cast<std::uint64_t>(destination) << 32U |
                      static_cast<std::uint64_t>(dbw) << 48U |
                      static_cast<std::uint64_t>(palette ? 0U : 0x13U) << 56U;
  put(bytes, at + 32U, bitblt, 8U);
  put(bytes, at + 40U, 0x50U, 8U);
  put(bytes, at + 48U, width | (static_cast<std::uint64_t>(height) << 32U), 8U);
  put(bytes, at + 56U, 0x52U, 8U);
  put(bytes, at + 72U, 0x53U, 8U);
  const auto qwords = source.size / 16U;
  put(bytes, at + 80U, 0x0800000000008000ULL | qwords, 8U);
  put(bytes, at + 96U, 0x30000000U + qwords, 4U);
  put(bytes, at + 100U, source.address, 4U);
  put(bytes, at + 108U, 0x50000000U + qwords, 4U);
}

void validate_bindings(const RacFrontendGsBindingsV1 &plan) {
  region(plan.source_payload);
  if (plan.uploads.size() > 64U || plan.allocator_begin > kGsBytes ||
      (plan.allocator_begin & 255U) != 0U) {
    throw RacFrontendGsScopeError("Invalid frontend batch allocation plan");
  }
  auto cursor = plan.allocator_begin;
  std::array<bool, 64U> seen{};
  for (const auto &upload : plan.uploads) {
    const auto pixels = pixel_count(upload.width, upload.height);
    region(upload.palette);
    region(upload.pixels);
    if (upload.source_id >= seen.size() || seen[upload.source_id] ||
        upload.palette.size != 1024U || upload.pixels.size != pixels ||
        !contains(plan.source_payload, upload.palette) ||
        !contains(plan.source_payload, upload.pixels) ||
        (upload.palette.address - plan.source_payload.address) / 16U >
            0xffffU ||
        (upload.pixels.address - plan.source_payload.address) / 16U > 0xffffU ||
        pixels + 1024U > kGsBytes - cursor ||
        upload.palette_base_blocks != (cursor >> 8U) ||
        upload.pixel_base_blocks != ((cursor + 1024U) >> 8U) ||
        upload.tex0 != tex0(cursor, upload.width, upload.height)) {
      throw RacFrontendGsScopeError(
          "Frontend upload plan lost source allocation ownership");
    }
    seen[upload.source_id] = true;
    cursor += pixels + 1024U;
  }
  if (cursor != plan.allocator_end) {
    throw RacFrontendGsScopeError(
        "Frontend allocator end disagrees with uploads");
  }
  seen.fill(false);
  std::size_t cold_count = 0U;
  for (const auto &binding : plan.bindings) {
    const auto found = std::find_if(
        plan.uploads.begin(), plan.uploads.end(), [&](const auto &upload) {
          return upload.source_id == binding.source_id;
        });
    if (found == plan.uploads.end() || found->tex0 != binding.tex0) {
      throw RacFrontendGsScopeError(
          "Frontend bind result disagrees with cold cache");
    }
    if (!seen[binding.source_id]) {
      if (plan.uploads[cold_count].source_id != binding.source_id) {
        throw RacFrontendGsScopeError(
            "Frontend upload order differs from first source bind calls");
      }
      seen[binding.source_id] = true;
      ++cold_count;
    }
  }
  if (cold_count != plan.uploads.size()) {
    throw RacFrontendGsScopeError("Frontend upload has no source bind call");
  }
}
} // namespace

RacFrontendGsBindingsV1 plan_rac_frontend_gs_bindings_v1(
    std::span<const RacFrontendTextureEntryV1> textures,
    std::span<const std::uint32_t> ordered_source_ids,
    RacFrontendGsRegionV1 source_payload, std::uint32_t allocator_begin,
    std::uint32_t max_bind_calls) {
  region(source_payload);
  if (textures.size() > 64U || ordered_source_ids.size() > max_bind_calls ||
      allocator_begin > kGsBytes || (allocator_begin & 255U) != 0U) {
    throw RacFrontendGsScopeError(
        "Frontend batch input exceeds bounded source domain");
  }
  RacFrontendGsBindingsV1 result;
  result.source_payload = source_payload;
  result.allocator_begin = allocator_begin;
  result.allocator_end = allocator_begin;
  std::array<std::optional<std::uint64_t>, 64U> cache{};
  for (auto id : ordered_source_ids) {
    if (id >= textures.size() || textures[id].source_index != id) {
      throw RacFrontendGsScopeError(
          "Frontend source texture ID is outside selected catalog");
    }
    if (!cache[id]) {
      const auto &entry = textures[id];
      const auto pixels = pixel_count(entry.width, entry.height);
      if ((entry.palette_offset & 15U) != 0U ||
          (entry.pixel_offset & 15U) != 0U ||
          entry.palette_offset / 16U > 0xffffU ||
          entry.pixel_offset / 16U > 0xffffU ||
          entry.palette_offset > source_payload.size ||
          1024U > source_payload.size - entry.palette_offset ||
          entry.pixel_offset > source_payload.size ||
          pixels > source_payload.size - entry.pixel_offset ||
          pixels + 1024U > kGsBytes - result.allocator_end) {
        throw RacFrontendGsScopeError(
            "Frontend source/GS upload range is not lossless and owned");
      }
      RacFrontendGsUploadV1 upload;
      upload.source_id = id;
      upload.palette = {source_payload.address + entry.palette_offset, 1024U};
      upload.pixels = {source_payload.address + entry.pixel_offset, pixels};
      upload.width = static_cast<std::uint16_t>(entry.width);
      upload.height = static_cast<std::uint16_t>(entry.height);
      upload.palette_base_blocks =
          static_cast<std::uint16_t>(result.allocator_end >> 8U);
      upload.pixel_base_blocks =
          static_cast<std::uint16_t>((result.allocator_end + 1024U) >> 8U);
      upload.tex0 = tex0(result.allocator_end, entry.width, entry.height);
      cache[id] = upload.tex0;
      result.allocator_end += pixels + 1024U;
      result.uploads.push_back(upload);
    }
    result.bindings.push_back({id, *cache[id]});
  }
  return result;
}

RacFrontendGsScissorV1
emit_rac_frontend_scissor_v1(const RacTextClipCallV1 &clip,
                             std::uint32_t screen_width_word,
                             std::uint32_t screen_height_word) noexcept {
  const auto width_last = std::bit_cast<std::int32_t>(screen_width_word - 1U);
  const auto height_last = std::bit_cast<std::int32_t>(screen_height_word - 1U);
  auto widen = [](std::int32_t value) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
  };
  RacFrontendGsScissorV1 result;
  result.raw_scissor =
      widen(std::max(0, clip.left)) |
      (widen(std::min(width_last, clip.right_inclusive)) << 16U) |
      (widen(std::max(0, clip.top)) << 32U) |
      (widen(std::min(height_last, clip.bottom_inclusive)) << 48U);
  gs_write(result.packet, 0x40U, result.raw_scissor);
  return result;
}

const std::array<std::byte, 48U> &rac_frontend_gs_flush_source_v1() noexcept {
  static const auto bytes = [] {
    std::array<std::byte, 48U> result{};
    put(result, 0U, 0x2000000000008001ULL, 8U);
    put(result, 8U, 0xeeU, 8U);
    put(result, 16U, 1U, 8U);
    put(result, 24U, 0x3fU, 8U);
    put(result, 32U, 0x8000000511304000ULL, 8U);
    put(result, 40U, 6U, 8U);
    return result;
  }();
  return bytes;
}

RacFrontendGsBatchV1 emit_rac_frontend_gs_batch_v1(
    const RacFrontendGsBindingsV1 &bindings,
    std::span<const std::byte> draw_packets, std::uint32_t command_address,
    RacFrontendGsRegionV1 flush_source, bool upload_gate,
    const RacFrontendGsIncomingV1 &incoming, std::uint32_t max_output_bytes) {
  validate_bindings(bindings);
  if ((draw_packets.size() & 15U) != 0U) {
    throw RacFrontendGsScopeError("Frontend draw block is not qword aligned");
  }
  for (std::size_t at = 0U; at < draw_packets.size();) {
    const auto tag = word(draw_packets, at);
    const auto qwords = tag & 0xffffU;
    const auto size = (static_cast<std::size_t>(qwords) + 1U) * 16U;
    if ((tag & 0xffff0000U) != 0x10000000U || qwords == 0U ||
        word(draw_packets, at + 4U) != 0U ||
        word(draw_packets, at + 8U) != 0U ||
        word(draw_packets, at + 12U) != (0x50000000U | qwords) ||
        size > draw_packets.size() - at) {
      throw RacFrontendGsScopeError(
          "Frontend draw packet escapes bounded CNT/DIRECT ownership");
    }
    at += size;
  }
  const auto upload_size =
      upload_gate ? bindings.uploads.size() * 224ULL + 16ULL : 0ULL;
  const auto total =
      96ULL + 16ULL + draw_packets.size() + 16ULL + upload_size + 16ULL;
  if (total > max_output_bytes ||
      total > std::numeric_limits<std::uint32_t>::max()) {
    throw RacFrontendGsScopeError("Frontend command output budget exceeded");
  }
  RacFrontendGsBatchV1 result;
  result.incoming = incoming;
  result.command_region = {command_address, static_cast<std::uint32_t>(total)};
  region(result.command_region);
  if (result.command_region.size == kAddressEnd - command_address) {
    throw RacFrontendGsScopeError(
        "Frontend NEXT continuation exceeds source address domain");
  }
  if (overlaps(result.command_region, bindings.source_payload)) {
    throw RacFrontendGsScopeError(
        "Frontend command storage aliases source payload");
  }
  if (upload_gate) {
    region(flush_source);
    if (flush_source.size != 48U ||
        overlaps(flush_source, result.command_region) ||
        overlaps(flush_source, bindings.source_payload)) {
      throw RacFrontendGsScopeError(
          "Frontend flush storage is not independently owned");
    }
  }
  if (total > static_cast<std::uint64_t>(result.bytes.max_size())) {
    throw RacFrontendGsScopeError(
        "Frontend command storage exceeds host capacity");
  }
  result.bytes.resize(static_cast<std::size_t>(total));
  gs_write(std::span<std::byte>(result.bytes).first(48U), 0x42U, 0x44U);
  gs_write(std::span<std::byte>(result.bytes).subspan(48U, 48U), 0x47U,
           0x2004bU);
  const auto draw_begin = 112U;
  const auto draw_end =
      draw_begin + static_cast<std::uint32_t>(draw_packets.size());
  const auto upload_begin = draw_end + 16U;
  next(result.bytes, 96U, command_address + upload_begin);
  std::copy(draw_packets.begin(), draw_packets.end(),
            result.bytes.begin() + draw_begin);
  next(result.bytes, draw_end, command_address + result.command_region.size);
  auto at = upload_begin;
  result.execution_order.push_back(
      {RacFrontendGsSegmentKindV1::callback_preamble, 0U, 96U});
  if (upload_gate) {
    for (const auto &upload : bindings.uploads) {
      transfer(result.bytes, at, upload.palette, upload.palette_base_blocks,
               16U, 16U, true);
      at += 112U;
      transfer(result.bytes, at, upload.pixels, upload.pixel_base_blocks,
               upload.width, upload.height, false);
      at += 112U;
      result.external_reads.push_back(upload.palette);
      result.external_reads.push_back(upload.pixels);
    }
    put(result.bytes, at, 0x30000003U, 4U);
    put(result.bytes, at + 4U, flush_source.address, 4U);
    put(result.bytes, at + 12U, 0x50000003U, 4U);
    at += 16U;
    result.external_reads.push_back(flush_source);
    result.execution_order.push_back({RacFrontendGsSegmentKindV1::uploads,
                                      upload_begin,
                                      static_cast<std::uint32_t>(upload_size)});
    result.uploads_emitted = true;
  }
  next(result.bytes, at, command_address + draw_begin);
  result.execution_order.push_back(
      {RacFrontendGsSegmentKindV1::draw_block, draw_begin,
       static_cast<std::uint32_t>(draw_packets.size())});
  return result;
}

} // namespace openrc
