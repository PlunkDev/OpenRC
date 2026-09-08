#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace openrc {

inline constexpr std::size_t kRacFontMetricRowCountV1 = 232U;
inline constexpr std::size_t kRacFontMetricTableBytesV1 =
    kRacFontMetricRowCountV1 * 4U;
inline constexpr std::size_t kRacFontMetricTableCountV1 = 3U;
inline constexpr std::size_t kRacFontMetricTablesBytesV1 =
    kRacFontMetricTableCountV1 * kRacFontMetricTableBytesV1;

struct RacFontMetricV1 {
  std::uint8_t atlas_u = 0U;
  std::uint8_t atlas_v = 0U;
  std::int8_t y_offset = 0;
  // The fourth byte advances the pen for an ordinary glyph. The accent
  // dispatch instead reads it as the overlay's X offset; preserve both uses.
  std::int8_t advance_or_accent_x_offset = 0;

  [[nodiscard]] bool operator==(const RacFontMetricV1 &) const = default;
};

struct RacFontMetricTableV1 {
  // Original byte-indexed order, including control/empty rows and the accent
  // rows at 0xc0..0xe7. This is not a Unicode or ASCII mapping.
  std::array<RacFontMetricV1, kRacFontMetricRowCountV1> rows{};

  [[nodiscard]] bool operator==(const RacFontMetricTableV1 &) const = default;
};

struct RacFontMetricTablesV1 {
  // Source order of the three consecutive tables. Runtime font selection and
  // atlas binding are separate contracts, not selected by this parser.
  std::array<RacFontMetricTableV1, kRacFontMetricTableCountV1> tables{};

  [[nodiscard]] bool operator==(const RacFontMetricTablesV1 &) const = default;
};

class RacFontMetricsError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compiler-only original RAC1 metrics: exactly 232 four-byte rows, with no
// header or alignment padding. The caller must establish the source region;
// these bytes alone do not identify an ELF, font atlas, or game revision.
// Every byte value is retained; no glyph remapping or invented cell sizes.
[[nodiscard]] RacFontMetricTableV1
parse_rac_font_metric_table_v1(std::span<const std::byte> bytes);

// Exactly three consecutive tables, with no leading/trailing data. Fixed
// extents bound all reads and allocations independently of source contents.
[[nodiscard]] RacFontMetricTablesV1
parse_rac_font_metric_tables_v1(std::span<const std::byte> bytes);

} // namespace openrc
