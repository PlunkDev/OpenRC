#include "openrc/rac_frontend_texture.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using Limits = openrc::RacFrontendTextureLimitsV1;
constexpr std::size_t kDirectory = 0xa0U;
constexpr std::size_t kShared = 0x100U;
constexpr std::size_t kPayload = 0x140U;
constexpr std::size_t kPayloadBytes = 3072U;
constexpr std::uint64_t kTotalPixels = 1280U;
constexpr std::uint64_t kOutputBytes = 14592U;

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <class F> void expect_error(F operation, const char *message) {
  try {
    operation();
  } catch (const openrc::RacFrontendTextureError &) {
    return;
  }
  throw std::runtime_error(message);
}

void write32(Bytes &bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned index = 0U; index < 4U; ++index)
    bytes.at(offset + index) =
        static_cast<std::byte>((value >> (index * 8U)) & 255U);
}

Bytes fixture() {
  // Entirely synthetic metadata, CLUTs and indices. Prefix/tail represent
  // unrelated frontend resources and are not texture storage padding.
  Bytes bytes(kPayload + kPayloadBytes + 2048U, std::byte{0xe7});
  std::fill_n(bytes.begin(), kPayload, std::byte{0});
  write32(bytes, 4U, kShared);
  write32(bytes, 0xcU, kDirectory + 4U * 16U);
  write32(bytes, 0x58U, 4U);
  write32(bytes, 0x5cU, kDirectory);
  write32(bytes, 0x68U, kPayload - kShared);
  write32(bytes, 0x80U, kPayload + kPayloadBytes - kShared);
  constexpr std::array<std::array<std::uint32_t, 4U>, 4U> rows{{
      {0U, 1024U, 16U, 16U},
      {0U, 1280U, 32U, 16U},
      {1792U, 2816U, 16U, 16U},
      {0U, 1024U, 16U, 16U},
  }};
  for (std::size_t index = 0U; index < rows.size(); ++index)
    for (std::size_t field = 0U; field < 4U; ++field)
      write32(bytes, kDirectory + index * 16U + field * 4U, rows[index][field]);
  for (std::size_t offset : {0U, 1792U}) {
    for (unsigned stored = 0U; stored < 256U; ++stored) {
      const auto at = kPayload + offset + stored * 4U;
      bytes[at] = static_cast<std::byte>(stored);
      bytes[at + 1U] = static_cast<std::byte>((stored * 3U + offset) & 255U);
      bytes[at + 2U] = static_cast<std::byte>(255U - stored);
      bytes[at + 3U] = static_cast<std::byte>(stored);
    }
  }
  for (const auto &row : rows)
    for (std::size_t index = 0U; index < row[2] * row[3]; ++index)
      bytes[kPayload + row[1] + index] = static_cast<std::byte>(index & 255U);
  return bytes;
}

Limits exact_limits(const Bytes &bytes) {
  return {bytes.size(), 4U, 32U, 16U, 512U, kTotalPixels, kOutputBytes};
}

std::size_t independent_storage_index(std::size_t logical) {
  // An explicit 32-entry source ordering, independent of the production helper.
  constexpr std::array<std::size_t, 32U> order{
      0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  16U, 17U, 18U,
      19U, 20U, 21U, 22U, 23U, 8U,  9U,  10U, 11U, 12U, 13U,
      14U, 15U, 24U, 25U, 26U, 27U, 28U, 29U, 30U, 31U};
  return logical / 32U * 32U + order[logical % 32U];
}

void test_all_rows_and_source_aliases() {
  const auto bytes = fixture();
  const auto bank =
      openrc::parse_rac_frontend_texture_bank_v1(bytes, exact_limits(bytes));
  expect(bank.input_bytes == bytes.size() &&
             bank.shared_data_offset == kShared &&
             bank.texture_data_offset == kPayload - kShared &&
             bank.next_data_offset == kPayload + kPayloadBytes - kShared,
         "Source header coordinate spaces were changed");
  expect(bank.table_range ==
                 openrc::RacFrontendTextureRangeV1{kDirectory, 64U} &&
             bank.payload_range ==
                 openrc::RacFrontendTextureRangeV1{kPayload, kPayloadBytes},
         "Owned source boundaries changed");
  expect(bank.textures.size() == 4U && bank.unique_palette_count == 2U &&
             bank.unique_pixel_range_count == 3U &&
             bank.total_pixel_count == kTotalPixels &&
             bank.total_output_bytes == kOutputBytes,
         "Source ID, alias, or aggregate accounting changed");
  for (std::size_t index = 0U; index < bank.textures.size(); ++index) {
    const auto &texture = bank.textures[index];
    const auto &entry = texture.entry;
    expect(entry.source_index == index &&
               entry.table_entry_range ==
                   openrc::RacFrontendTextureRangeV1{kDirectory + 16U * index,
                                                     16U},
           "Descriptor order was not preserved");
    expect(entry.palette_range.offset == kPayload + entry.palette_offset &&
               entry.pixel_range.offset == kPayload + entry.pixel_offset &&
               entry.palette_range.size == 1024U &&
               entry.pixel_range.size ==
                   static_cast<std::uint64_t>(entry.width) * entry.height,
           "Descriptor ranges do not match source fields");
    expect(std::equal(texture.raw_palette.begin(), texture.raw_palette.end(),
                      bytes.begin() + entry.palette_range.offset),
           "Raw palette bytes were changed");
    expect(texture.indices.size() == entry.pixel_range.size &&
               std::equal(texture.indices.begin(), texture.indices.end(),
                          bytes.begin() + entry.pixel_range.offset),
           "Raw indices were changed or swizzled");
  }
  expect(bank.textures[0].entry.palette_range ==
                 bank.textures[1].entry.palette_range &&
             bank.textures[0].entry.pixel_range ==
                 bank.textures[3].entry.pixel_range &&
             bank.textures[0].indices == bank.textures[3].indices &&
             bank.textures[0].raw_palette == bank.textures[3].raw_palette,
         "Real same-kind palette/pixel aliases were not retained");
}

void test_raw_and_canonical_palette_domains() {
  const auto bytes = fixture();
  const auto bank =
      openrc::parse_rac_frontend_texture_bank_v1(bytes, exact_limits(bytes));
  for (const auto &texture : bank.textures) {
    for (std::size_t logical = 0U; logical < 256U; ++logical) {
      const auto storage = independent_storage_index(logical) * 4U;
      const auto output = logical * 4U;
      for (std::size_t channel = 0U; channel < 3U; ++channel)
        expect(texture.palette_rgba[output + channel] ==
                   texture.raw_palette[storage + channel],
               "Canonical CLUT permutation changed RGB");
      const auto alpha =
          std::to_integer<unsigned>(texture.raw_palette[storage + 3U]);
      expect(std::to_integer<unsigned>(texture.palette_rgba[output + 3U]) ==
                 std::min(255U, 2U * alpha),
             "Canonical asset alpha conversion is incorrect");
    }
    expect(texture.raw_palette[127U * 4U + 3U] == std::byte{127} &&
               texture.raw_palette[128U * 4U + 3U] == std::byte{128} &&
               texture.raw_palette[255U * 4U + 3U] == std::byte{255},
           "Original raw alpha was saturated or expanded in place");
    expect(texture.rgba.size() == texture.indices.size() * 4U,
           "RGBA expansion has wrong size");
    for (std::size_t pixel = 0U; pixel < texture.indices.size(); ++pixel) {
      const auto offset =
          std::to_integer<std::size_t>(texture.indices[pixel]) * 4U;
      expect(std::equal(texture.palette_rgba.begin() + offset,
                        texture.palette_rgba.begin() + offset + 4U,
                        texture.rgba.begin() + pixel * 4U),
             "RGBA expansion changed index order or pixel channels");
    }
  }
}

void test_ownership_and_unrelated_bytes() {
  auto bytes = fixture();
  const auto bank =
      openrc::parse_rac_frontend_texture_bank_v1(bytes, exact_limits(bytes));
  std::fill(bytes.begin() + kPayload + kPayloadBytes, bytes.end(),
            std::byte{0x1a});
  write32(bytes, 0x60U, 0xff00ccddU);
  const auto changed =
      openrc::parse_rac_frontend_texture_bank_v1(bytes, exact_limits(bytes));
  for (std::size_t index = 0U; index < bank.textures.size(); ++index)
    expect(changed.textures[index].entry == bank.textures[index].entry &&
               changed.textures[index].rgba == bank.textures[index].rgba,
           "Unrelated frontend data was interpreted as texture bytes");
  std::fill(bytes.begin(), bytes.end(), std::byte{0});
  expect(bank.textures[0].indices[255U] == std::byte{255} &&
             bank.textures[0].raw_palette[255U * 4U + 3U] == std::byte{255},
         "Parser result borrowed input storage");

  bytes = fixture();
  Bytes unaligned(1U, std::byte{0xfe});
  unaligned.insert(unaligned.end(), bytes.begin(), bytes.end());
  const auto parsed = openrc::parse_rac_frontend_texture_bank_v1(
      std::span<const std::byte>(unaligned).subspan(1U), exact_limits(bytes));
  expect(parsed.textures[0].rgba == bank.textures[0].rgba,
         "Parser requires host pointer alignment");
}

void test_empty_catalog() {
  auto bytes = fixture();
  write32(bytes, 0x58U, 0U);
  write32(bytes, 0xcU, kDirectory);
  write32(bytes, 0x80U, kPayload - kShared);
  const auto bank =
      openrc::parse_rac_frontend_texture_bank_v1(bytes, exact_limits(bytes));
  expect(bank.textures.empty() && bank.payload_range.size == 0U &&
             bank.table_range.size == 0U && bank.total_output_bytes == 0U &&
             bank.unique_palette_count == 0U &&
             bank.unique_pixel_range_count == 0U,
         "Empty source catalog invented resources");
}

void test_invalid_header_and_truncations() {
  const auto original = fixture();
  for (std::size_t size = 0U; size < kPayload + kPayloadBytes; ++size)
    expect_error(
        [&] {
          (void)openrc::parse_rac_frontend_texture_bank_v1(
              std::span<const std::byte>(original).first(size),
              exact_limits(original));
        },
        "Truncated owned source extent was accepted");
  constexpr std::array<std::pair<std::size_t, std::uint32_t>, 14U> mutations{
      {{0x58U, 0x80000000U},
       {0x58U, 0xffffffffU},
       {0x58U, 3U},
       {0x5cU, 0x80U},
       {0x5cU, 0xa1U},
       {0x5cU, 0xfffffff0U},
       {0xcU, 0xd0U},
       {0xcU, 0xf0U},
       {4U, 0xd0U},
       {4U, 0xfffffff0U},
       {4U, 0x101U},
       {0x68U, 0x41U},
       {0x80U, 0x30U},
       {0x80U, 0xffffffffU}}};
  for (const auto &[offset, value] : mutations) {
    auto bytes = original;
    write32(bytes, offset, value);
    expect_error(
        [&] {
          (void)openrc::parse_rac_frontend_texture_bank_v1(bytes,
                                                           exact_limits(bytes));
        },
        "Invalid frontend texture header was accepted");
  }
}

void test_invalid_offsets_dimensions_and_owned_neighbors() {
  const auto original = fixture();
  constexpr std::array<std::pair<std::size_t, std::uint32_t>, 14U> mutations{
      {{0U, 1U},
       {4U, 1025U},
       {0U, 0x100000U},
       {4U, 0x100000U},
       {0U, 0xfffffff0U},
       {4U, 0x80000000U},
       {8U, 0U},
       {12U, 0U},
       {8U, 24U},
       {12U, 15U},
       {8U, 0x80000000U},
       {12U, 0xffffffffU},
       {0U, kPayloadBytes},
       {4U, kPayloadBytes}}};
  for (const auto &[field, value] : mutations) {
    auto bytes = original;
    write32(bytes, kDirectory + field, value);
    auto limits = exact_limits(bytes);
    limits.max_width = std::numeric_limits<std::uint64_t>::max();
    limits.max_height = std::numeric_limits<std::uint64_t>::max();
    expect_error(
        [&] {
          (void)openrc::parse_rac_frontend_texture_bank_v1(bytes, limits);
        },
        "Invalid descriptor or neighboring-bank access was accepted");
  }
  auto tiny = original;
  write32(tiny, kDirectory + 8U, 1U);
  write32(tiny, kDirectory + 12U, 1U);
  expect_error(
      [&] {
        (void)openrc::parse_rac_frontend_texture_bank_v1(tiny,
                                                         exact_limits(tiny));
      },
      "Sub-qword source transfer was accepted");
}

void test_alias_kind_partial_overlap_and_gaps() {
  const auto original = fixture();
  for (const auto &mutation :
       std::array<std::pair<std::size_t, std::uint32_t>, 5U>{
           {{kDirectory + 16U + 4U, 1040U},
            {kDirectory + 32U, 16U},
            {kDirectory + 16U + 4U, 1296U},
            {0x80U, kPayload + kPayloadBytes - kShared + 16U},
            {kDirectory + 32U, 0U}}}) {
    auto bytes = original;
    write32(bytes, mutation.first, mutation.second);
    expect_error(
        [&] {
          (void)openrc::parse_rac_frontend_texture_bank_v1(bytes,
                                                           exact_limits(bytes));
        },
        "Partial alias, overlap or unclaimed bytes were accepted");
  }
  auto cross_kind = original;
  write32(cross_kind, kDirectory + 16U + 4U, 0U);
  write32(cross_kind, kDirectory + 16U + 12U, 32U);
  auto limits = exact_limits(cross_kind);
  limits.max_height = 32U;
  limits.max_pixels_per_texture = 1024U;
  limits.max_total_pixels = 100000U;
  limits.max_total_output_bytes = 1000000U;
  expect_error(
      [&] {
        (void)openrc::parse_rac_frontend_texture_bank_v1(cross_kind, limits);
      },
      "Exact cross-kind palette/index alias was accepted");
}

void test_aggregate_and_individual_limits() {
  const auto bytes = fixture();
  auto limits = exact_limits(bytes);
  const std::array<std::uint64_t Limits::*, 7U> fields{
      &Limits::max_input_bytes,
      &Limits::max_textures,
      &Limits::max_width,
      &Limits::max_height,
      &Limits::max_pixels_per_texture,
      &Limits::max_total_pixels,
      &Limits::max_total_output_bytes};
  for (const auto field : fields) {
    auto zero = limits;
    zero.*field = 0U;
    expect_error(
        [&] { (void)openrc::parse_rac_frontend_texture_bank_v1(bytes, zero); },
        "Zero policy limit was accepted");
    auto small = limits;
    --(small.*field);
    expect_error(
        [&] { (void)openrc::parse_rac_frontend_texture_bank_v1(bytes, small); },
        "Individual or aggregate limit was bypassed");
  }
  limits.max_total_pixels = kTotalPixels - 256U;
  expect_error(
      [&] { (void)openrc::parse_rac_frontend_texture_bank_v1(bytes, limits); },
      "Aliased source ID bypassed total pixel budget");
  limits = exact_limits(bytes);
  limits.max_total_output_bytes = 4U * 2048U - 1U;
  expect_error(
      [&] { (void)openrc::parse_rac_frontend_texture_bank_v1(bytes, limits); },
      "Raw and canonical palette copies bypassed output budget");
}

Bytes single_texture(const std::uint32_t width, const std::uint32_t height) {
  const auto pixels = static_cast<std::size_t>(width) * height;
  Bytes bytes(kPayload + 1024U + pixels + 16U, std::byte{0});
  write32(bytes, 4U, kShared);
  write32(bytes, 0xcU, kDirectory + 16U);
  write32(bytes, 0x58U, 1U);
  write32(bytes, 0x5cU, kDirectory);
  write32(bytes, 0x68U, kPayload - kShared);
  write32(bytes, 0x80U,
          static_cast<std::uint32_t>(kPayload + 1024U + pixels - kShared));
  write32(bytes, kDirectory, 0U);
  write32(bytes, kDirectory + 4U, 1024U);
  write32(bytes, kDirectory + 8U, width);
  write32(bytes, kDirectory + 12U, height);
  return bytes;
}

void test_original_transfer_field_bounds() {
  // All source ranges are complete and all caller limits are deliberately
  // loose. These errors must come from the source transfer fields themselves.
  constexpr Limits loose{1ULL << 32U, 16U,         1ULL << 32U, 1ULL << 32U,
                         1ULL << 32U, 1ULL << 32U, 1ULL << 36U};
  for (const auto &[width, height, diagnostic] :
       std::array<std::tuple<std::uint32_t, std::uint32_t, std::string_view>,
                  5U>{{{4096U, 1U, "dimensions"},
                       {1U, 4096U, "dimensions"},
                       {32768U, 1U, "dimensions"},
                       {1024U, 512U, "pixel count"},
                       {1024U, 1024U, "pixel count"}}}) {
    const auto bytes = single_texture(width, height);
    bool rejected = false;
    try {
      (void)openrc::parse_rac_frontend_texture_bank_v1(bytes, loose);
    } catch (const openrc::RacFrontendTextureError &error) {
      expect(std::string_view(error.what()).find(diagnostic) !=
                 std::string_view::npos,
             "Transfer-bound fixture failed for an unrelated reason");
      rejected = true;
    }
    expect(rejected, "Lossy original transfer fields were accepted");
  }
  // The original upload template retains DBW=1 for log_width<=6, not zero.
  // Exercise these small widths with complete single-qword image transfers.
  // Largest tested transfers fit 16384 qwords; the next power of two does not.
  for (const auto &[width, height] :
       std::array<std::pair<std::uint32_t, std::uint32_t>, 9U>{
           {{1U, 16U},
            {2U, 8U},
            {4U, 4U},
            {8U, 2U},
            {16U, 1U},
            {512U, 512U},
            {1024U, 256U},
            {2048U, 128U},
            {128U, 2048U}}}) {
    const auto bytes = single_texture(width, height);
    const auto parsed =
        openrc::parse_rac_frontend_texture_bank_v1(bytes, loose);
    expect(parsed.textures.size() == 1U &&
               parsed.textures[0].entry.width == width &&
               parsed.textures[0].entry.height == height &&
               parsed.total_pixel_count ==
                   static_cast<std::uint64_t>(width) * height,
           "Lossless source transfer dimensions were changed or rejected");
  }
}

} // namespace

int main() {
  try {
    test_all_rows_and_source_aliases();
    test_raw_and_canonical_palette_domains();
    test_ownership_and_unrelated_bytes();
    test_empty_catalog();
    test_invalid_header_and_truncations();
    test_invalid_offsets_dimensions_and_owned_neighbors();
    test_alias_kind_partial_overlap_and_gaps();
    test_aggregate_and_individual_limits();
    test_original_transfer_field_bounds();
    std::cout << "RAC frontend texture tests passed (9 groups)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC frontend texture tests failed: " << error.what() << '\n';
    return 1;
  }
}
