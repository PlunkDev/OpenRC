#include "openrc/rac_frontend_texture.hpp"

#include "openrc/ps2_palette.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <tuple>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint64_t kHeaderPrefixBytes = 0x84U;
constexpr std::uint64_t kPaletteBytes = kRacFrontendTexturePaletteBytesV1;
constexpr std::uint32_t kMaximumSourceOffset = 0xffff0U;
// TRXREG RRW/RRH have 12 bits; 2048 is their largest representable power of
// two. The width-derived DBW is at most 32, within its six-bit field. This is
// a transfer representation bound, not a claim about the sampling hardware.
constexpr std::uint32_t kMaximumTransferDimension = 2048U;
// The source emits one unchunked IMAGE tag and one DMA REF. NLOOP is narrower
// than DMA QWC: bit 15 is already EOP, so a 0x8000-qword image is not lossless.
constexpr std::uint64_t kMaximumImageQwords = 0x7fffU;

[[noreturn]] void fail(const char *const message) {
  throw RacFrontendTextureError(message);
}

std::uint32_t read32(const std::span<const std::byte> bytes,
                     const std::size_t offset) noexcept {
  std::uint32_t result = 0U;
  for (unsigned index = 0U; index < 4U; ++index) {
    result |= std::to_integer<std::uint32_t>(bytes[offset + index])
              << (index * 8U);
  }
  return result;
}

void require_range(const std::uint64_t offset, const std::uint64_t size,
                   const std::uint64_t end) {
  if (offset > end || size > end - offset) {
    fail("RAC frontend texture range exceeds its owned source region");
  }
}

void add_bounded(std::uint64_t &total, const std::uint64_t amount,
                 const std::uint64_t limit) {
  if (total > limit || amount > limit - total) {
    fail("RAC frontend texture aggregate limit exceeded");
  }
  total += amount;
}

void validate_limits(const RacFrontendTextureLimitsV1 limits) {
  if (limits.max_input_bytes == 0U || limits.max_textures == 0U ||
      limits.max_width == 0U || limits.max_height == 0U ||
      limits.max_pixels_per_texture == 0U || limits.max_total_pixels == 0U ||
      limits.max_total_output_bytes == 0U) {
    fail("RAC frontend texture limits must all be non-zero");
  }
}

struct OwnedRange {
  RacFrontendTextureRangeV1 range;
  bool palette = false;
};

} // namespace

RacFrontendTextureBankV1 parse_rac_frontend_texture_bank_v1(
    const std::span<const std::byte> decoded_frontend,
    const RacFrontendTextureLimitsV1 limits) {
  validate_limits(limits);
  const auto input_size = static_cast<std::uint64_t>(decoded_frontend.size());
  if (input_size < kHeaderPrefixBytes || input_size > limits.max_input_bytes ||
      input_size > std::numeric_limits<std::uint32_t>::max()) {
    fail("RAC frontend texture input has an invalid bounded extent");
  }

  RacFrontendTextureBankV1 result;
  result.input_bytes = input_size;
  result.shared_data_offset = read32(decoded_frontend, 4U);
  result.texture_data_offset = read32(decoded_frontend, 0x68U);
  result.next_data_offset = read32(decoded_frontend, 0x80U);
  const auto count = read32(decoded_frontend, 0x58U);
  const auto directory = read32(decoded_frontend, 0x5cU);
  const auto directory_end = read32(decoded_frontend, 0xcU);
  const auto directory_size = static_cast<std::uint64_t>(count) * 16U;
  if (count > std::numeric_limits<std::int32_t>::max() ||
      count > limits.max_textures || directory < kHeaderPrefixBytes ||
      (directory & 15U) != 0U || directory_size > input_size ||
      static_cast<std::uint64_t>(directory) + directory_size != directory_end ||
      directory_end > result.shared_data_offset ||
      (result.shared_data_offset & 15U) != 0U ||
      (result.texture_data_offset & 15U) != 0U ||
      (result.next_data_offset & 15U) != 0U ||
      result.texture_data_offset > result.next_data_offset) {
    fail("RAC frontend texture directory or payload boundaries are invalid");
  }
  require_range(directory, directory_size, input_size);
  const auto payload_start =
      static_cast<std::uint64_t>(result.shared_data_offset) +
      result.texture_data_offset;
  const auto payload_end =
      static_cast<std::uint64_t>(result.shared_data_offset) +
      result.next_data_offset;
  require_range(payload_start, payload_end - payload_start, input_size);
  result.table_range = {directory, directory_size};
  result.payload_range = {payload_start, payload_end - payload_start};

  // Count both owned palette copies for every source ID before allocating
  // metadata. Aliases do not circumvent output budgets by sharing offsets.
  add_bounded(result.total_output_bytes,
              static_cast<std::uint64_t>(count) * kPaletteBytes * 2U,
              limits.max_total_output_bytes);
  std::vector<RacFrontendTextureEntryV1> entries;
  std::vector<OwnedRange> regions;
  if (static_cast<std::uint64_t>(count) > entries.max_size() ||
      static_cast<std::uint64_t>(count) * 2U > regions.max_size()) {
    fail("RAC frontend texture metadata exceeds host container limits");
  }
  entries.reserve(count);
  regions.reserve(static_cast<std::size_t>(count) * 2U);

  for (std::uint32_t index = 0U; index < count; ++index) {
    const auto row_offset =
        static_cast<std::uint64_t>(directory) + index * 16ULL;
    const auto row = static_cast<std::size_t>(row_offset);
    RacFrontendTextureEntryV1 entry;
    entry.source_index = index;
    entry.table_entry_range = {row_offset, 16U};
    entry.palette_offset = read32(decoded_frontend, row);
    entry.pixel_offset = read32(decoded_frontend, row + 4U);
    entry.width = read32(decoded_frontend, row + 8U);
    entry.height = read32(decoded_frontend, row + 12U);
    if (entry.palette_offset > kMaximumSourceOffset ||
        entry.pixel_offset > kMaximumSourceOffset ||
        (entry.palette_offset & 15U) != 0U ||
        (entry.pixel_offset & 15U) != 0U) {
      fail("RAC frontend texture offset cannot survive source QW relocation");
    }
    if (!std::has_single_bit(entry.width) ||
        !std::has_single_bit(entry.height) || entry.width > limits.max_width ||
        entry.height > limits.max_height ||
        entry.width > kMaximumTransferDimension ||
        entry.height > kMaximumTransferDimension) {
      fail("RAC frontend texture dimensions exceed their source or caller "
           "domain");
    }
    const auto pixels = static_cast<std::uint64_t>(entry.width) * entry.height;
    if (pixels < 16U || pixels / 16U > kMaximumImageQwords ||
        pixels > limits.max_pixels_per_texture) {
      fail("RAC frontend texture pixel count violates its transfer or caller "
           "bound");
    }
    entry.palette_range = {payload_start + entry.palette_offset, kPaletteBytes};
    entry.pixel_range = {payload_start + entry.pixel_offset, pixels};
    require_range(entry.palette_range.offset, kPaletteBytes, payload_end);
    require_range(entry.pixel_range.offset, pixels, payload_end);
    add_bounded(result.total_pixel_count, pixels, limits.max_total_pixels);
    add_bounded(result.total_output_bytes, pixels * 5U,
                limits.max_total_output_bytes);
    entries.push_back(entry);
    regions.push_back({entry.palette_range, true});
    regions.push_back({entry.pixel_range, false});
  }

  std::ranges::sort(regions, [](const auto &left, const auto &right) {
    return std::tie(left.range.offset, left.range.size, left.palette) <
           std::tie(right.range.offset, right.range.size, right.palette);
  });
  auto cursor = payload_start;
  const OwnedRange *previous = nullptr;
  for (const auto &region : regions) {
    if (previous != nullptr && region.range == previous->range &&
        region.palette == previous->palette) {
      continue;
    }
    if (region.range.offset != cursor) {
      fail("RAC frontend texture ranges overlap or leave unclaimed payload "
           "bytes");
    }
    cursor += region.range.size;
    if (region.palette) {
      ++result.unique_palette_count;
    } else {
      ++result.unique_pixel_range_count;
    }
    previous = &region;
  }
  if (cursor != payload_end) {
    fail("RAC frontend texture ranges do not own the complete texture payload");
  }

  if (static_cast<std::uint64_t>(count) > result.textures.max_size()) {
    fail("RAC frontend texture results exceed host container limits");
  }
  result.textures.reserve(count);
  for (const auto &entry : entries) {
    const auto pixels = entry.pixel_range.size;
    RacFrontendTextureV1 texture;
    texture.entry = entry;
    if (pixels > texture.indices.max_size() ||
        pixels * 4U > texture.rgba.max_size()) {
      fail("RAC frontend texture byte data exceeds host container limits");
    }
    const auto raw_palette = decoded_frontend.subspan(
        static_cast<std::size_t>(entry.palette_range.offset),
        static_cast<std::size_t>(kPaletteBytes));
    std::ranges::copy(raw_palette, texture.raw_palette.begin());
    for (unsigned logical = 0U; logical < 256U; ++logical) {
      const auto stored = static_cast<std::size_t>(psmt8_clut_storage_index_v1(
                              static_cast<std::uint8_t>(logical))) *
                          4U;
      const auto output = static_cast<std::size_t>(logical) * 4U;
      std::copy_n(raw_palette.begin() + stored, 3U,
                  texture.palette_rgba.begin() + output);
      texture.palette_rgba[output + 3U] =
          static_cast<std::byte>(ps2_alpha_to_rgba8_v1(
              std::to_integer<std::uint8_t>(raw_palette[stored + 3U])));
    }
    const auto indices = decoded_frontend.subspan(
        static_cast<std::size_t>(entry.pixel_range.offset),
        static_cast<std::size_t>(pixels));
    texture.indices.assign(indices.begin(), indices.end());
    texture.rgba.resize(static_cast<std::size_t>(pixels * 4U));
    for (std::size_t pixel = 0U; pixel < texture.indices.size(); ++pixel) {
      const auto palette_offset =
          std::to_integer<std::size_t>(texture.indices[pixel]) * 4U;
      std::copy_n(texture.palette_rgba.begin() + palette_offset, 4U,
                  texture.rgba.begin() + pixel * 4U);
    }
    result.textures.push_back(std::move(texture));
  }
  return result;
}

} // namespace openrc
