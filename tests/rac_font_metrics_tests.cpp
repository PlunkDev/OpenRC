#include "openrc/rac_font_metrics.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

void expect(const bool condition, const char *const description) {
  if (!condition) {
    throw std::runtime_error(description);
  }
}

template <class F>
void expect_error(F operation, const char *const description) {
  try {
    operation();
  } catch (const openrc::RacFontMetricsError &) {
    return;
  }
  throw std::runtime_error(description);
}

// Synthetic bytes only: intentionally not an extracted source font table.
Bytes table(const unsigned seed) {
  Bytes bytes(openrc::kRacFontMetricTableBytesV1);
  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::byte>((index * 19U + seed) & 255U);
  }
  return bytes;
}

int signed_byte(const std::byte value) {
  const auto raw = std::to_integer<int>(value);
  return raw < 128 ? raw : raw - 256;
}

void expect_rows(const openrc::RacFontMetricTableV1 &decoded,
                 const std::span<const std::byte> bytes) {
  expect(decoded.rows.size() == 232U, "Wrong metric row count");
  for (std::size_t index = 0U; index < decoded.rows.size(); ++index) {
    const auto &row = decoded.rows[index];
    const auto offset = index * 4U;
    expect(row.atlas_u == std::to_integer<unsigned>(bytes[offset]) &&
               row.atlas_v == std::to_integer<unsigned>(bytes[offset + 1U]),
           "Unsigned atlas coordinates changed");
    expect(row.y_offset == signed_byte(bytes[offset + 2U]) &&
               row.advance_or_accent_x_offset ==
                   signed_byte(bytes[offset + 3U]),
           "Signed metric byte changed");
  }
}

void test_table_preserves_all_rows() {
  const auto bytes = table(71U);
  const auto decoded = openrc::parse_rac_font_metric_table_v1(bytes);
  expect_rows(decoded, bytes);

  auto owned_source = bytes;
  const auto owned = openrc::parse_rac_font_metric_table_v1(owned_source);
  std::fill(owned_source.begin(), owned_source.end(), std::byte{0});
  expect(owned == decoded, "Metric result borrowed source storage");
}

void test_unsigned_and_signed_byte_domain() {
  Bytes bytes(openrc::kRacFontMetricTableBytesV1);
  // Exercise every possible value of every field, including -128, -1, 0,127
  // for signed values and the full 128..255 unsigned coordinate domain.
  for (unsigned raw = 0U; raw < 256U; ++raw) {
    std::fill(bytes.begin(), bytes.end(), static_cast<std::byte>(raw));
    const auto decoded = openrc::parse_rac_font_metric_table_v1(bytes);
    expect_rows(decoded, bytes);
  }
}

void test_three_table_order_and_boundaries() {
  Bytes block;
  for (const auto seed : {17U, 99U, 213U}) {
    const auto bytes = table(seed);
    block.insert(block.end(), bytes.begin(), bytes.end());
  }
  const auto result = openrc::parse_rac_font_metric_tables_v1(block);
  for (std::size_t index = 0U; index < result.tables.size(); ++index) {
    const auto slice = std::span<const std::byte>(block).subspan(
        index * openrc::kRacFontMetricTableBytesV1,
        openrc::kRacFontMetricTableBytesV1);
    expect_rows(result.tables[index], slice);
    expect(result.tables[index] ==
               openrc::parse_rac_font_metric_table_v1(slice),
           "Combined and standalone metric decoding differ");
  }
  expect(result.tables[0] != result.tables[1] &&
             result.tables[1] != result.tables[2],
         "Separate font tables were aliased or replaced");
}

void test_exact_extents() {
  const Bytes oversized(openrc::kRacFontMetricTablesBytesV1 + 17U);
  for (std::size_t count = 0U; count < openrc::kRacFontMetricTableBytesV1;
       ++count) {
    expect_error(
        [&] {
          (void)openrc::parse_rac_font_metric_table_v1(
              std::span<const std::byte>(oversized).first(count));
        },
        "Truncated table was accepted");
  }
  for (std::size_t count = 0U; count < openrc::kRacFontMetricTablesBytesV1;
       ++count) {
    expect_error(
        [&] {
          (void)openrc::parse_rac_font_metric_tables_v1(
              std::span<const std::byte>(oversized).first(count));
        },
        "Truncated three-table block was accepted");
  }
  for (std::size_t excess = 1U; excess <= 17U; ++excess) {
    expect_error(
        [&] {
          (void)openrc::parse_rac_font_metric_table_v1(
              std::span<const std::byte>(oversized).first(
                  openrc::kRacFontMetricTableBytesV1 + excess));
        },
        "Table with trailing bytes was accepted");
    expect_error(
        [&] {
          (void)openrc::parse_rac_font_metric_tables_v1(
              std::span<const std::byte>(oversized).first(
                  openrc::kRacFontMetricTablesBytesV1 + excess));
        },
        "Three-table block with trailing bytes was accepted");
  }
  expect_error(
      [&] {
        (void)openrc::parse_rac_font_metric_table_v1(
            std::span<const std::byte>(oversized).first(
                openrc::kRacFontMetricTablesBytesV1));
      },
      "Whole block was silently treated as one table");
}

} // namespace

int main() {
  try {
    test_table_preserves_all_rows();
    test_unsigned_and_signed_byte_domain();
    test_three_table_order_and_boundaries();
    test_exact_extents();
    std::cout << "RAC font metrics tests passed (4 groups)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC font metrics tests failed: " << error.what() << '\n';
    return 1;
  }
}
