#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::size_t kRacFrontendTextureEntryBytesV1 = 16U;
inline constexpr std::size_t kRacFrontendTexturePaletteBytesV1 = 1024U;

struct RacFrontendTextureLimitsV1 {
  std::uint64_t max_input_bytes = 0U;
  std::uint64_t max_textures = 0U;
  std::uint64_t max_width = 0U;
  std::uint64_t max_height = 0U;
  std::uint64_t max_pixels_per_texture = 0U;
  std::uint64_t max_total_pixels = 0U;
  // Raw + canonical palette bytes and index + RGBA bytes, summed per source
  // texture, including aliases. This bounds owned byte data, not sizeof
  // metadata.
  std::uint64_t max_total_output_bytes = 0U;
};

struct RacFrontendTextureRangeV1 {
  std::uint64_t offset = 0U;
  std::uint64_t size = 0U;

  [[nodiscard]] bool
  operator==(const RacFrontendTextureRangeV1 &) const = default;
};

struct RacFrontendTextureEntryV1 {
  std::uint32_t source_index = 0U;
  std::uint32_t palette_offset = 0U;
  std::uint32_t pixel_offset = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  // All ranges are absolute within the supplied decoded frontend WAD span.
  // Equal same-kind ranges retain real source aliases; IDs are never
  // deduplicated.
  RacFrontendTextureRangeV1 table_entry_range;
  RacFrontendTextureRangeV1 palette_range;
  RacFrontendTextureRangeV1 pixel_range;

  [[nodiscard]] bool
  operator==(const RacFrontendTextureEntryV1 &) const = default;
};

struct RacFrontendTextureV1 {
  RacFrontendTextureEntryV1 entry;
  // Original GS storage order and original alpha bytes, without conversion.
  std::array<std::byte, kRacFrontendTexturePaletteBytesV1> raw_palette{};
  // Logical CLUT order with existing asset alpha conversion min(255,2*a).
  // This is canonical asset RGBA, not an emulation of the original GS blend.
  std::array<std::byte, kRacFrontendTexturePaletteBytesV1> palette_rgba{};
  // Original linear PSMT8 indices and their canonical RGBA expansion.
  std::vector<std::byte> indices;
  std::vector<std::byte> rgba;
};

struct RacFrontendTextureBankV1 {
  std::uint64_t input_bytes = 0U;
  std::uint32_t shared_data_offset = 0U;
  std::uint32_t texture_data_offset = 0U;
  std::uint32_t next_data_offset = 0U;
  RacFrontendTextureRangeV1 table_range;
  RacFrontendTextureRangeV1 payload_range;
  std::uint64_t total_pixel_count = 0U;
  std::uint64_t total_output_bytes = 0U;
  std::uint64_t unique_palette_count = 0U;
  std::uint64_t unique_pixel_range_count = 0U;
  std::vector<RacFrontendTextureV1> textures;
};

class RacFrontendTextureError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compiler-only RAC1 frontend texture catalog in an already-decoded global WAD.
// Reads only its established header fields (+4,+c,+58,+5c,+68,+80); this does
// not validate unrelated WAD systems or identify a game revision by itself.
// The supported retail layout has a 16-byte-aligned directory ending at +c,
// and an exactly covered texture payload from shared_data + header[+68] up to
// shared_data + header[+80]. Exact same-kind byte-range aliases are supported;
// partial/cross-kind overlaps and unclaimed gaps are outside this format.
// Offsets must survive the source's 16-byte/u16 relocation without truncation;
// Positive power-of-two dimensions are at most 2048 (lossless TRXREG/DBW),
// and pixels/16 must fit the single original IMAGE NLOOP (1..0x7fff).
// These are transfer representation bounds, not a hardware sampling guarantee.
// All returned byte data is owned. No host glyph, UI, or sampling policy is
// added.
[[nodiscard]] RacFrontendTextureBankV1
parse_rac_frontend_texture_bank_v1(std::span<const std::byte> decoded_frontend,
                                   RacFrontendTextureLimitsV1 limits);

} // namespace openrc
