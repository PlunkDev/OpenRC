#include "openrc/rac_font_metrics.hpp"

#include <bit>

namespace openrc {

RacFontMetricTableV1
parse_rac_font_metric_table_v1(const std::span<const std::byte> bytes) {
  if (bytes.size() != kRacFontMetricTableBytesV1) {
    throw RacFontMetricsError(
        "RAC font metrics require exactly 232 four-byte rows");
  }
  RacFontMetricTableV1 result;
  for (std::size_t index = 0U; index < result.rows.size(); ++index) {
    const auto offset = index * 4U;
    result.rows[index] = {
        std::to_integer<std::uint8_t>(bytes[offset]),
        std::to_integer<std::uint8_t>(bytes[offset + 1U]),
        std::bit_cast<std::int8_t>(
            std::to_integer<std::uint8_t>(bytes[offset + 2U])),
        std::bit_cast<std::int8_t>(
            std::to_integer<std::uint8_t>(bytes[offset + 3U])),
    };
  }
  return result;
}

RacFontMetricTablesV1
parse_rac_font_metric_tables_v1(const std::span<const std::byte> bytes) {
  if (bytes.size() != kRacFontMetricTablesBytesV1) {
    throw RacFontMetricsError(
        "RAC font metric block requires exactly three tables");
  }
  RacFontMetricTablesV1 result;
  for (std::size_t index = 0U; index < result.tables.size(); ++index) {
    result.tables[index] = parse_rac_font_metric_table_v1(bytes.subspan(
        index * kRacFontMetricTableBytesV1, kRacFontMetricTableBytesV1));
  }
  return result;
}

} // namespace openrc
