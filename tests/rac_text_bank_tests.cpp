#include "openrc/rac_text_bank.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
constexpr openrc::RacTextBankLimitsV1 kLimits{65536U, 128U, 32768U};

void expect(const bool condition, const char *const description) {
  if (!condition) {
    throw std::runtime_error(description);
  }
}

template <class F>
void expect_error(F operation, const char *const description) {
  try {
    operation();
  } catch (const openrc::RacTextBankError &) {
    return;
  }
  throw std::runtime_error(description);
}

void write32(Bytes &bytes, const std::size_t offset,
             const std::uint32_t value) {
  for (unsigned index = 0U; index < 4U; ++index) {
    bytes.at(offset + index) =
        static_cast<std::byte>((value >> (index * 8U)) & 255U);
  }
}

std::uint32_t read32(const Bytes &bytes, const std::size_t offset) {
  std::uint32_t value = 0U;
  for (unsigned index = 0U; index < 4U; ++index) {
    value |= std::to_integer<std::uint32_t>(bytes.at(offset + index))
             << (index * 8U);
  }
  return value;
}

struct FixtureRow {
  std::uint32_t key;
  Bytes text;
  std::uint32_t auxiliary_8 = 0xffffffffU;
  std::uint32_t auxiliary_c = 0U;
};

Bytes bank(const std::vector<FixtureRow> &rows) {
  Bytes result(8U + rows.size() * 16U);
  write32(result, 0U, static_cast<std::uint32_t>(rows.size()));
  for (std::size_t index = 0U; index < rows.size(); ++index) {
    const auto offset = 8U + index * 16U;
    const auto &row = rows[index];
    write32(result, offset, static_cast<std::uint32_t>(result.size()));
    write32(result, offset + 4U, row.key);
    write32(result, offset + 8U, row.auxiliary_8);
    write32(result, offset + 12U, row.auxiliary_c);
    result.insert(result.end(), row.text.begin(), row.text.end());
    result.push_back(std::byte{0});
    while ((result.size() & 3U) != 0U) {
      result.push_back(std::byte{0});
    }
  }
  write32(result, 4U, static_cast<std::uint32_t>(result.size()));
  return result;
}

Bytes directory(const std::array<Bytes, 8U> &banks) {
  Bytes result(32U);
  for (std::size_t index = 0U; index < banks.size(); ++index) {
    while ((result.size() & 15U) != 0U) {
      result.push_back(std::byte{0});
    }
    write32(result, index * 4U, static_cast<std::uint32_t>(result.size()));
    result.insert(result.end(), banks[index].begin(), banks[index].end());
  }
  while ((result.size() & 2047U) != 0U) {
    result.push_back(std::byte{0});
  }
  return result;
}

std::array<Bytes, 8U> fixture_banks() {
  return {
      bank({{91U,
             {std::byte{1}, std::byte{0x11}, std::byte{0x8d}, std::byte{'a'}},
             147U,
             0x89abcdefU},
            {3U, {}},
            {91U, {std::byte{'b'}}}}),
      bank({}),
      bank({{71U, {std::byte{'x'}, std::byte{'y'}, std::byte{'z'}}}}),
      bank({}),
      bank({{100U, {std::byte{'j'}}}}),
      bank({{1U, {std::byte{0xff}}}}),
      bank({{0xffffffffU, {std::byte{0x12}}}}),
      bank({})};
}

void test_raw_text_order_and_first_match() {
  auto input = fixture_banks()[0U];
  const auto parsed = openrc::parse_rac_text_bank_v1(input, kLimits);
  expect(parsed.range == openrc::RacTextBankRangeV1{0U, input.size()},
         "standalone logical range changed");
  expect(parsed.row_directory_range == openrc::RacTextBankRangeV1{8U, 48U},
         "standalone row range changed");
  expect(parsed.entries.size() == 3U && parsed.total_text_bytes == 5U,
         "raw entry/text totals changed");
  const auto &first = parsed.entries.front();
  expect(first.key == 91U && parsed.entries[1U].key == 3U &&
             parsed.entries[2U].key == 91U,
         "source row order or duplicate keys were normalized");
  expect(first.raw_auxiliary_word_8 == 147U &&
             first.raw_auxiliary_word_c == 0x89abcdefU,
         "opaque source words were normalized");
  expect(first.relative_text_offset == 56U &&
             first.text_range == openrc::RacTextBankRangeV1{56U, 4U} &&
             first.text_bytes == Bytes{std::byte{1}, std::byte{0x11},
                                       std::byte{0x8d}, std::byte{'a'}},
         "text controls, glyph bytes, or range changed");
  expect(parsed.entries[1U].text_bytes.empty(), "empty text was rejected");
  expect(openrc::find_rac_text_bank_entry_v1(parsed, 91U) == &first,
         "duplicate lookup did not return the first original row");
  expect(openrc::find_rac_text_bank_entry_v1(parsed, 4U) == nullptr,
         "missing key was supplied a fabricated fallback");
  input.assign(input.size(), std::byte{0xff});
  expect(first.text_bytes[0U] == std::byte{1},
         "returned raw text borrowed invalidated source storage");
}

void test_all_numeric_slots_and_aggregate_ranges() {
  const auto banks = fixture_banks();
  const auto input = directory(banks);
  const auto parsed = openrc::parse_rac_text_bank_directory_v1(input, kLimits);
  expect(parsed.input_bytes == input.size() && parsed.total_entries == 7U &&
             parsed.total_text_bytes == 11U,
         "directory totals did not cover all eight slots");
  for (std::size_t index = 0U; index < banks.size(); ++index) {
    const auto relative = openrc::parse_rac_text_bank_v1(banks[index], kLimits);
    const auto offset = read32(input, index * 4U);
    const auto &slot = parsed.slots[index];
    expect(parsed.slot_offsets[index] == offset &&
               slot.range ==
                   openrc::RacTextBankRangeV1{offset, banks[index].size()},
           "numeric slot or absolute bank range changed");
    expect(slot.entries.size() == relative.entries.size(),
           "empty/partial source slot was replaced");
    for (std::size_t row = 0U; row < slot.entries.size(); ++row) {
      auto expected = relative.entries[row];
      expected.text_range.offset += offset;
      expect(slot.entries[row] == expected,
             "directory bank differs from its standalone parse");
    }
    const auto storage_end = index + 1U < banks.size()
                                 ? parsed.slot_offsets[index + 1U]
                                 : input.size();
    expect(parsed.padding_ranges[index] ==
               openrc::RacTextBankRangeV1{offset + banks[index].size(),
                                          storage_end - offset -
                                              banks[index].size()},
           "alignment range lost original source bytes");
  }
  expect(openrc::find_rac_text_bank_entry_v1(parsed.slots[1U], 91U) == nullptr,
         "empty numeric slot inherited another slot's text");
}

void test_empty_banks() {
  const auto empty = bank({});
  const auto parsed = openrc::parse_rac_text_bank_v1(empty, kLimits);
  expect(parsed.entries.empty() && parsed.total_text_bytes == 0U &&
             parsed.range.size == 8U,
         "empty eight-byte bank lost its explicit header");
  std::array<Bytes, 8U> banks;
  banks.fill(empty);
  const auto all_empty =
      openrc::parse_rac_text_bank_directory_v1(directory(banks), kLimits);
  expect(all_empty.total_entries == 0U && all_empty.total_text_bytes == 0U,
         "eight explicitly empty banks are not a valid empty directory");
}

void test_exact_logical_extent_and_count() {
  const auto good = fixture_banks()[0U];
  for (std::size_t length = 0U; length < good.size(); ++length) {
    expect_error(
        [&] {
          static_cast<void>(openrc::parse_rac_text_bank_v1(
              std::span(good).first(length), kLimits));
        },
        "truncated logical bank accepted");
  }
  auto bad = good;
  bad.push_back(std::byte{0});
  expect_error(
      [&] { static_cast<void>(openrc::parse_rac_text_bank_v1(bad, kLimits)); },
      "bytes after declared bank accepted");
  bad = good;
  write32(bad, 4U, read32(bad, 4U) - 4U);
  expect_error(
      [&] { static_cast<void>(openrc::parse_rac_text_bank_v1(bad, kLimits)); },
      "mismatched header size accepted");
  for (const auto count : {0U, 4U, 0x80000000U, 0xffffffffU}) {
    bad = good;
    write32(bad, 0U, count);
    expect_error(
        [&] {
          static_cast<void>(openrc::parse_rac_text_bank_v1(bad, kLimits));
        },
        "invalid, negative-source, or overflowing count accepted");
  }
  bad = bank({});
  bad.resize(12U);
  write32(bad, 4U, 12U);
  expect_error(
      [&] { static_cast<void>(openrc::parse_rac_text_bank_v1(bad, kLimits)); },
      "unowned bytes in an empty logical bank accepted");
}

void test_text_offsets_and_padding() {
  const auto good = fixture_banks()[0U];
  for (const auto offset : {0U, 8U, 55U, 57U, 60U, 0xffffffffU}) {
    auto bad = good;
    write32(bad, 8U, offset);
    expect_error(
        [&] {
          static_cast<void>(openrc::parse_rac_text_bank_v1(bad, kLimits));
        },
        "invalid/relocated/nonconsecutive text pointer accepted");
  }
  auto bad = good;
  write32(bad, 24U, read32(bad, 8U));
  expect_error(
      [&] { static_cast<void>(openrc::parse_rac_text_bank_v1(bad, kLimits)); },
      "unqualified aliased source text layout accepted");
  bad = good;
  bad[61U] = std::byte{1};
  expect_error(
      [&] { static_cast<void>(openrc::parse_rac_text_bank_v1(bad, kLimits)); },
      "nonzero text alignment padding accepted");
  bad = bank({{8U, {std::byte{'q'}, std::byte{'r'}, std::byte{'s'}}}});
  bad.back() = std::byte{'t'};
  expect_error(
      [&] { static_cast<void>(openrc::parse_rac_text_bank_v1(bad, kLimits)); },
      "unterminated final text accepted");
  bad = bank({{8U, {std::byte{'q'}}}});
  bad.resize(bad.size() + 4U);
  write32(bad, 4U, static_cast<std::uint32_t>(bad.size()));
  expect_error(
      [&] { static_cast<void>(openrc::parse_rac_text_bank_v1(bad, kLimits)); },
      "nonminimum logical tail padding accepted");
}

void test_directory_offsets_and_envelopes() {
  const auto good = directory(fixture_banks());
  for (const auto length : {0U, 31U, 32U, 2047U}) {
    expect_error(
        [&] {
          static_cast<void>(openrc::parse_rac_text_bank_directory_v1(
              std::span(good).first(length), kLimits));
        },
        "truncated directory sector accepted");
  }
  for (const auto offset : {0U, 16U, 33U, 48U, 0xfffffff0U}) {
    auto bad = good;
    write32(bad, 0U, offset);
    expect_error(
        [&] {
          static_cast<void>(
              openrc::parse_rac_text_bank_directory_v1(bad, kLimits));
        },
        "invalid first bank offset accepted");
  }
  auto bad = good;
  write32(bad, 4U, 32U);
  expect_error(
      [&] {
        static_cast<void>(
            openrc::parse_rac_text_bank_directory_v1(bad, kLimits));
      },
      "aliased bank offsets accepted");
  bad = good;
  write32(bad, 8U, read32(bad, 4U) - 16U);
  expect_error(
      [&] {
        static_cast<void>(
            openrc::parse_rac_text_bank_directory_v1(bad, kLimits));
      },
      "reversed bank offsets accepted");
  bad = good;
  write32(bad, 32U + 4U, read32(bad, 4U));
  expect_error(
      [&] {
        static_cast<void>(
            openrc::parse_rac_text_bank_directory_v1(bad, kLimits));
      },
      "bank reaching into next numeric slot accepted");
  bad = good;
  bad.resize(bad.size() + 2048U);
  expect_error(
      [&] {
        static_cast<void>(
            openrc::parse_rac_text_bank_directory_v1(bad, kLimits));
      },
      "extra unowned whole directory sector accepted");
  bad = good;
  bad.back() = std::byte{1};
  expect_error(
      [&] {
        static_cast<void>(
            openrc::parse_rac_text_bank_directory_v1(bad, kLimits));
      },
      "nonzero final sector padding accepted");
  bad = good;
  const auto second_bank = read32(bad, 4U);
  bad.at(second_bank + 8U) = std::byte{1};
  expect_error(
      [&] {
        static_cast<void>(
            openrc::parse_rac_text_bank_directory_v1(bad, kLimits));
      },
      "nonzero intermediate bank padding accepted");
}

void test_terminator_cannot_escape_owned_bank() {
  for (const auto last_slot : {false, true}) {
    std::array<Bytes, 8U> banks;
    banks.fill(bank({}));
    const auto slot = last_slot ? 7U : 0U;
    // 32 logical bytes place the next bank's zero header directly after the
    // unterminated first bank. 28 bytes place the final bank's terminator
    // directly before zero sector padding. Neither zero belongs to its text.
    const auto text =
        last_slot ? Bytes(3U, std::byte{'q'}) : Bytes(7U, std::byte{'q'});
    banks[slot] = bank({{81U, text}});
    auto input = directory(banks);
    const auto start = read32(input, slot * 4U);
    const auto logical_size = read32(input, start + 4U);
    const auto end = start + logical_size;
    expect(input[end] == std::byte{0},
           "fixture lacks the tempting zero outside the logical bank");
    input[end - 1U] = std::byte{'x'};
    expect_error(
        [&] {
          static_cast<void>(
              openrc::parse_rac_text_bank_directory_v1(input, kLimits));
        },
        "text terminator escaped into another bank or sector padding");
  }
}

void test_limits_are_aggregate_and_exact() {
  const auto one = fixture_banks()[0U];
  const auto all = directory(fixture_banks());
  auto limits = kLimits;
  limits.max_input_bytes = one.size();
  limits.max_total_entries = 3U;
  limits.max_total_text_bytes = 5U;
  static_cast<void>(openrc::parse_rac_text_bank_v1(one, limits));
  for (unsigned field = 0U; field < 3U; ++field) {
    auto reduced = limits;
    if (field == 0U) {
      --reduced.max_input_bytes;
    }
    if (field == 1U) {
      --reduced.max_total_entries;
    }
    if (field == 2U) {
      --reduced.max_total_text_bytes;
    }
    expect_error(
        [&] {
          static_cast<void>(openrc::parse_rac_text_bank_v1(one, reduced));
        },
        "standalone exact caller bound was exceeded");
  }
  limits = {all.size(), 7U, 11U};
  static_cast<void>(openrc::parse_rac_text_bank_directory_v1(all, limits));
  for (unsigned field = 0U; field < 3U; ++field) {
    auto reduced = limits;
    if (field == 0U) {
      --reduced.max_input_bytes;
    }
    if (field == 1U) {
      --reduced.max_total_entries;
    }
    if (field == 2U) {
      --reduced.max_total_text_bytes;
    }
    expect_error(
        [&] {
          static_cast<void>(
              openrc::parse_rac_text_bank_directory_v1(all, reduced));
        },
        "directory reset a caller bound per slot");
  }
  for (unsigned field = 0U; field < 3U; ++field) {
    auto invalid = kLimits;
    if (field == 0U) {
      invalid.max_input_bytes = 0U;
    }
    if (field == 1U) {
      invalid.max_total_entries = 0U;
    }
    if (field == 2U) {
      invalid.max_total_text_bytes = 0U;
    }
    expect_error(
        [&] {
          static_cast<void>(openrc::parse_rac_text_bank_v1(bank({}), invalid));
        },
        "zero standalone limit accepted");
    expect_error(
        [&] {
          static_cast<void>(
              openrc::parse_rac_text_bank_directory_v1(all, invalid));
        },
        "zero directory limit accepted");
  }
}

} // namespace

int main() {
  try {
    test_raw_text_order_and_first_match();
    test_all_numeric_slots_and_aggregate_ranges();
    test_empty_banks();
    test_exact_logical_extent_and_count();
    test_text_offsets_and_padding();
    test_directory_offsets_and_envelopes();
    test_terminator_cannot_escape_owned_bank();
    test_limits_are_aggregate_and_exact();
    std::cout << "RAC keyed text-bank tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
