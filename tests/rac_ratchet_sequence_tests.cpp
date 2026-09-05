#include "openrc/rac_ratchet_sequence.hpp"

#include "openrc/disc_toc.hpp"
#include "openrc/rac_level_core.hpp"
#include "openrc/wad.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kRegularSequenceOffset = 0x40U;
constexpr std::uint32_t kRegularSequenceBytes = 0xc0U;
constexpr openrc::RacRatchetSequenceLimitsV1 kLimits{0x1000U, 0x400U, 0xffU,
                                                     0xffU};
constexpr std::uint64_t kRealDataMaximumBytes = UINT64_C(64) * 1024U * 1024U;

using Histogram = std::map<std::uint64_t, std::uint64_t>;

struct RealScanAggregate {
  std::uint64_t sequences = 0U;
  std::uint64_t frames = 0U;
  std::uint64_t trigger_words = 0U;
  Histogram phase_rate_bits;
  Histogram opaque_half_4;
  Histogram payload_qwords;
  Histogram primary_bytes;
  Histogram supplemental_a_counts;
  Histogram supplemental_b_offsets;
  Histogram supplemental_b_counts;
  Histogram trailing_padding_bytes;
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void write_le16(std::vector<std::byte> &bytes, const std::size_t offset,
                const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_le32(std::vector<std::byte> &bytes, const std::size_t offset,
                const std::uint32_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
  bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
  bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void fill(std::vector<std::byte> &bytes, const std::size_t offset,
          const std::size_t size, const std::byte value) {
  std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), size, value);
}

[[nodiscard]] std::vector<std::byte> make_regular_source() {
  std::vector<std::byte> bytes(0x200U, std::byte{0xee});
  fill(bytes, kRegularSequenceOffset, kRegularSequenceBytes, std::byte{0xcc});
  const auto base = static_cast<std::size_t>(kRegularSequenceOffset);

  write_le32(bytes, base + 0x00U, 0x01234567U);
  write_le32(bytes, base + 0x04U, 0x89abcdefU);
  write_le32(bytes, base + 0x08U, 0x55aa33ccU);
  write_le32(bytes, base + 0x0cU, 0xfedcba98U);
  bytes[base + 0x10U] = std::byte{2};
  bytes[base + 0x11U] = std::byte{0xa5};
  bytes[base + 0x12U] = std::byte{2};
  bytes[base + 0x13U] = std::byte{0x5a};
  write_le32(bytes, base + 0x14U, 0x13579bdfU);
  write_le32(bytes, base + 0x18U, 0x3e800000U);

  write_le32(bytes, base + 0x1cU, 0x50U);
  write_le32(bytes, base + 0x20U, 0x80U);
  write_le32(bytes, base + 0x24U, 0x0badf00dU);
  write_le32(bytes, base + 0x28U, 0x76543210U);

  for (std::size_t index = 0U; index < 0x20U; ++index) {
    bytes[0x70U + index] = static_cast<std::byte>(0x40U + index);
  }

  fill(bytes, 0x90U, 0x20U, std::byte{0xa1});
  write_le32(bytes, 0x90U, 0x3e000000U);
  write_le16(bytes, 0x94U, 0x5566U);
  write_le16(bytes, 0x96U, 1U);
  write_le16(bytes, 0x98U, 4U);
  write_le16(bytes, 0x9aU, 1U);
  write_le16(bytes, 0x9cU, 12U);
  write_le16(bytes, 0x9eU, 0U);
  fill(bytes, 0xc0U, 0x30U, std::byte{0xb2});
  write_le32(bytes, 0xc0U, 0x3f000000U);
  write_le16(bytes, 0xc4U, 0x1357U);
  write_le16(bytes, 0xc6U, 2U);
  write_le16(bytes, 0xc8U, 8U);
  write_le16(bytes, 0xcaU, 1U);
  write_le16(bytes, 0xccU, 16U);
  write_le16(bytes, 0xceU, 1U);
  fill(bytes, 0xf0U, 0x10U, std::byte{0x7e});
  return bytes;
}

template <typename Mutation>
void expect_regular_rejected(Mutation &&mutation, const std::string &message) {
  auto bytes = make_regular_source();
  std::invoke(std::forward<Mutation>(mutation), bytes);
  try {
    (void)openrc::parse_rac_ratchet_sequence_v1(
        bytes, {kRegularSequenceOffset, kRegularSequenceBytes}, kLimits);
  } catch (const openrc::RacRatchetSequenceError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_regular_sequence_and_owned_bytes() {
  auto bytes = make_regular_source();
  const std::vector<std::byte> expected_bytes(
      bytes.begin() + kRegularSequenceOffset,
      bytes.begin() + kRegularSequenceOffset + kRegularSequenceBytes);
  const auto result = openrc::parse_rac_ratchet_sequence_v1(
      bytes, {kRegularSequenceOffset, kRegularSequenceBytes}, kLimits);
  fill(bytes, kRegularSequenceOffset, kRegularSequenceBytes, std::byte{0});

  expect(result.source_range ==
                 openrc::RacRatchetSequenceRangeV1{kRegularSequenceOffset,
                                                   kRegularSequenceBytes} &&
             result.encoded_bytes == expected_bytes,
         "regular sequence bytes are not independently owned");
  expect(result.opaque_prefix_words ==
                 std::array<std::uint32_t, 4U>{0x01234567U, 0x89abcdefU,
                                               0x55aa33ccU, 0xfedcba98U} &&
             result.frame_count == 2U && result.continuous_sound_id == 0xa5U &&
             result.trigger_count == 2U && result.opaque_control_13 == 0x5aU &&
             result.opaque_word_14 == 0x13579bdfU &&
             result.sequence_phase_rate_override == 0.25F,
         "regular sequence header metadata changed");
  expect(result.header_range == openrc::RacRatchetSequenceRangeV1{0U, 0x1cU} &&
             result.frame_offset_table_range ==
                 openrc::RacRatchetSequenceRangeV1{0x1cU, 8U} &&
             result.trigger_list_range ==
                 openrc::RacRatchetSequenceRangeV1{0x24U, 8U} &&
             result.pre_frame_data_range ==
                 openrc::RacRatchetSequenceRangeV1{0x2cU, 0x24U} &&
             result.trigger_words ==
                 std::vector<std::uint32_t>{0x0badf00dU, 0x76543210U},
         "regular sequence directory ranges are wrong");

  expect(result.frames.size() == 2U &&
             result.frames[0U].packed_offset_word == 0x50U &&
             result.frames[0U].relative_offset == 0x50U &&
             result.frames[0U].source_offset == 0x90U &&
             result.frames[0U].range ==
                 openrc::RacRatchetSequenceRangeV1{0x50U, 0x20U} &&
             result.frames[0U].structural_header_range ==
                 openrc::RacRatchetSequenceRangeV1{0x50U, 0x10U} &&
             result.frames[0U].opaque_payload_range ==
                 openrc::RacRatchetSequenceRangeV1{0x60U, 0x10U} &&
             result.frames[0U].phase_rate == 0.125F &&
             result.frames[0U].opaque_half_4 == 0x5566U &&
             result.frames[0U].regular_payload_qwords == 1U &&
             result.frames[0U].primary_byte_count == 4U &&
             result.frames[0U].supplemental_a_count == 1U &&
             result.frames[0U].supplemental_b_byte_offset == 12U &&
             result.frames[0U].supplemental_b_count == 0U &&
             result.frames[0U].primary_payload_range ==
                 openrc::RacRatchetSequenceRangeV1{0x60U, 4U} &&
             result.frames[0U].supplemental_a_payload_range ==
                 openrc::RacRatchetSequenceRangeV1{0x64U, 8U} &&
             result.frames[0U].supplemental_b_payload_range ==
                 openrc::RacRatchetSequenceRangeV1{0x6cU, 0U} &&
             result.frames[0U].trailing_alignment_padding_range ==
                 openrc::RacRatchetSequenceRangeV1{0x6cU, 4U} &&
             result.frames[1U].range ==
                 openrc::RacRatchetSequenceRangeV1{0x80U, 0x30U} &&
             result.frames[1U].opaque_payload_range ==
                 openrc::RacRatchetSequenceRangeV1{0x90U, 0x20U} &&
             result.frames[1U].phase_rate == 0.5F &&
             result.frames[1U].opaque_half_4 == 0x1357U &&
             result.frames[1U].regular_payload_qwords == 2U &&
             result.frames[1U].primary_byte_count == 8U &&
             result.frames[1U].supplemental_a_count == 1U &&
             result.frames[1U].supplemental_b_byte_offset == 16U &&
             result.frames[1U].supplemental_b_count == 1U &&
             result.frames[1U].primary_payload_range ==
                 openrc::RacRatchetSequenceRangeV1{0x90U, 8U} &&
             result.frames[1U].supplemental_a_payload_range ==
                 openrc::RacRatchetSequenceRangeV1{0x98U, 8U} &&
             result.frames[1U].supplemental_b_payload_range ==
                 openrc::RacRatchetSequenceRangeV1{0xa0U, 8U} &&
             result.frames[1U].trailing_alignment_padding_range ==
                 openrc::RacRatchetSequenceRangeV1{0xa8U, 8U},
         "regular frame ranges were not derived from structural sizes");
  expect(result.encoded_bytes[0x6cU] == std::byte{0xa1} &&
             result.encoded_bytes[0x6fU] == std::byte{0xa1} &&
             result.encoded_bytes[0xa8U] == std::byte{0xb2} &&
             result.encoded_bytes[0xafU] == std::byte{0xb2},
         "non-zero trailing payload bytes were not preserved");
}

void test_envelope_and_limit_rejections() {
  const auto bytes = make_regular_source();
  for (const auto limits : {
           openrc::RacRatchetSequenceLimitsV1{0U, 0x400U, 0xffU, 0xffU},
           openrc::RacRatchetSequenceLimitsV1{0x1000U, 0U, 0xffU, 0xffU},
           openrc::RacRatchetSequenceLimitsV1{0x1000U, 0x400U, 0U, 0xffU},
           openrc::RacRatchetSequenceLimitsV1{0x1000U, 0x400U, 0xffU, 0U},
           openrc::RacRatchetSequenceLimitsV1{0x1ffU, 0x400U, 0xffU, 0xffU},
           openrc::RacRatchetSequenceLimitsV1{0x1000U, 0xbfU, 0xffU, 0xffU},
           openrc::RacRatchetSequenceLimitsV1{0x1000U, 0x400U, 1U, 0xffU},
           openrc::RacRatchetSequenceLimitsV1{0x1000U, 0x400U, 0xffU, 1U},
       }) {
    try {
      (void)openrc::parse_rac_ratchet_sequence_v1(
          bytes, {kRegularSequenceOffset, kRegularSequenceBytes}, limits);
    } catch (const openrc::RacRatchetSequenceError &) {
      continue;
    }
    throw std::runtime_error("a RacRatchetSequenceV1 limit was ignored");
  }

  for (const auto range : {
           openrc::RacRatchetSequenceRangeV1{0x41U, kRegularSequenceBytes},
           openrc::RacRatchetSequenceRangeV1{kRegularSequenceOffset, 0x1bU},
           openrc::RacRatchetSequenceRangeV1{0x1f0U, 0x20U},
       }) {
    try {
      (void)openrc::parse_rac_ratchet_sequence_v1(bytes, range, kLimits);
    } catch (const openrc::RacRatchetSequenceError &) {
      continue;
    }
    throw std::runtime_error("an invalid bounded sequence range was accepted");
  }

  expect_regular_rejected(
      [](auto &fixture) {
        fixture[kRegularSequenceOffset + 0x10U] = std::byte{0};
      },
      "a zero-frame sequence was accepted");
  expect_regular_rejected(
      [](auto &fixture) {
        fixture[kRegularSequenceOffset + 0x10U] = std::byte{0xff};
      },
      "a frame table leaving the bounded sequence was accepted");
}

void test_packed_offset_and_frame_rejections() {
  expect_regular_rejected(
      [](auto &bytes) { write_le32(bytes, 0x58U, 0x7fc00000U); },
      "a non-finite sequence phase-rate override was accepted");
  expect_regular_rejected(
      [](auto &bytes) { write_le32(bytes, 0x90U, 0xbf800000U); },
      "a negative frame phase rate was accepted");
  expect_regular_rejected(
      [](auto &bytes) { write_le32(bytes, 0x5cU, 0x80000050U); },
      "unsupported opaque frame-offset bits were accepted");
  expect_regular_rejected(
      [](auto &bytes) { write_le32(bytes, 0x5cU, 0xf0000050U); },
      "an unproven special marker was accepted");
  expect_regular_rejected([](auto &bytes) { write_le32(bytes, 0x60U, 0x50U); },
                          "two frames aliasing one offset were accepted");
  expect_regular_rejected([](auto &bytes) { write_le32(bytes, 0x5cU, 0x54U); },
                          "an unaligned regular frame was accepted");
  expect_regular_rejected(
      [](auto &bytes) { write_le32(bytes, 0x5cU, 0x20U); },
      "a frame overlapping the sequence directory was accepted");
  expect_regular_rejected([](auto &bytes) { write_le16(bytes, 0x96U, 4U); },
                          "overlapping regular frame ranges were accepted");
  expect_regular_rejected(
      [](auto &bytes) { write_le16(bytes, 0xc6U, 0xffffU); },
      "a regular frame escaping the bounded sequence was accepted");
  expect_regular_rejected(
      [](auto &bytes) {
        write_le16(bytes, 0x98U, 17U);
        write_le16(bytes, 0x9cU, 25U);
      },
      "a primary partition larger than its qword payload was accepted");
  expect_regular_rejected(
      [](auto &bytes) {
        write_le16(bytes, 0x9aU, 2U);
        write_le16(bytes, 0x9cU, 20U);
      },
      "supplemental-A records exceeding their qword payload were accepted");
  expect_regular_rejected(
      [](auto &bytes) { write_le16(bytes, 0x9eU, 2U); },
      "supplemental-B records exceeding their qword payload were accepted");
  expect_regular_rejected(
      [](auto &bytes) { write_le16(bytes, 0x9cU, 13U); },
      "a supplemental-B offset disagreeing with prior partitions was accepted");
  expect_regular_rejected(
      [](auto &bytes) {
        write_le16(bytes, 0x98U, 0U);
        write_le16(bytes, 0x9aU, 0U);
        write_le16(bytes, 0x9cU, 0U);
        write_le16(bytes, 0x9eU, 0U);
      },
      "a full qword of unexplained trailing payload was accepted");

  auto exact = make_regular_source();
  write_le16(exact, 0x98U, 0U);
  write_le16(exact, 0x9aU, 1U);
  write_le16(exact, 0x9cU, 8U);
  write_le16(exact, 0x9eU, 1U);
  const auto parsed = openrc::parse_rac_ratchet_sequence_v1(
      exact, {kRegularSequenceOffset, kRegularSequenceBytes}, kLimits);
  expect(parsed.frames[0U].primary_payload_range ==
                 openrc::RacRatchetSequenceRangeV1{0x60U, 0U} &&
             parsed.frames[0U].supplemental_a_payload_range ==
                 openrc::RacRatchetSequenceRangeV1{0x60U, 8U} &&
             parsed.frames[0U].supplemental_b_payload_range ==
                 openrc::RacRatchetSequenceRangeV1{0x68U, 8U} &&
             parsed.frames[0U].trailing_alignment_padding_range ==
                 openrc::RacRatchetSequenceRangeV1{0x70U, 0U},
         "an exact payload partition was not retained");
}

[[nodiscard]] std::vector<std::byte>
read_disc_extent(const std::filesystem::path &image_path,
                 const std::uint64_t lba, const std::uint64_t sectors) {
  if (sectors > kRealDataMaximumBytes / openrc::kDiscTocSectorSize) {
    throw std::runtime_error("real-data primary extent exceeds test limit");
  }
  const auto byte_count = sectors * openrc::kDiscTocSectorSize;
  if (lba > std::numeric_limits<std::uint64_t>::max() /
                openrc::kDiscTocSectorSize ||
      byte_count > std::numeric_limits<std::size_t>::max()) {
    throw std::runtime_error("real-data primary extent overflows host limits");
  }
  const auto byte_offset = lba * openrc::kDiscTocSectorSize;
  std::ifstream input(image_path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw std::runtime_error("cannot open real-data disc image");
  }
  const auto end_position = input.tellg();
  if (end_position < 0) {
    throw std::runtime_error("cannot determine real-data disc size");
  }
  const auto image_bytes = static_cast<std::uint64_t>(end_position);
  if (byte_offset > image_bytes || byte_count > image_bytes - byte_offset) {
    throw std::runtime_error("real-data primary extent leaves disc image");
  }
  input.seekg(static_cast<std::streamoff>(byte_offset), std::ios::beg);
  std::vector<std::byte> result(static_cast<std::size_t>(byte_count));
  input.read(reinterpret_cast<char *>(result.data()),
             static_cast<std::streamsize>(result.size()));
  if (input.gcount() != static_cast<std::streamsize>(result.size())) {
    throw std::runtime_error("short read from real-data primary extent");
  }
  return result;
}

void observe_frame(RealScanAggregate &aggregate,
                   const openrc::RacRatchetSequenceFrameV1 &frame) {
  ++aggregate.frames;
  ++aggregate.phase_rate_bits[std::bit_cast<std::uint32_t>(frame.phase_rate)];
  ++aggregate.opaque_half_4[frame.opaque_half_4];
  ++aggregate.payload_qwords[frame.regular_payload_qwords];
  ++aggregate.primary_bytes[frame.primary_byte_count];
  ++aggregate.supplemental_a_counts[frame.supplemental_a_count];
  ++aggregate.supplemental_b_offsets[frame.supplemental_b_byte_offset];
  ++aggregate.supplemental_b_counts[frame.supplemental_b_count];
  ++aggregate
        .trailing_padding_bytes[frame.trailing_alignment_padding_range.size];
}

void print_histogram(const char *const name, const Histogram &histogram) {
  std::cout << "histogram " << name << ':';
  for (const auto &[value, count] : histogram) {
    std::cout << ' ' << value << '=' << count;
  }
  std::cout << '\n';
}

void print_aggregate(const RealScanAggregate &aggregate) {
  std::cout << "aggregate sequences=" << aggregate.sequences
            << " frames=" << aggregate.frames
            << " trigger_words=" << aggregate.trigger_words << '\n';
  print_histogram("phase_rate_bits", aggregate.phase_rate_bits);
  print_histogram("opaque_half_4", aggregate.opaque_half_4);
  print_histogram("payload_qwords", aggregate.payload_qwords);
  print_histogram("primary_bytes", aggregate.primary_bytes);
  print_histogram("supplemental_a_counts", aggregate.supplemental_a_counts);
  print_histogram("supplemental_b_offsets", aggregate.supplemental_b_offsets);
  print_histogram("supplemental_b_counts", aggregate.supplemental_b_counts);
  print_histogram("trailing_padding_bytes", aggregate.trailing_padding_bytes);
}

void scan_real_level(const std::filesystem::path &image_path,
                     const std::uint32_t level_id,
                     RealScanAggregate &aggregate) {
  const auto report = openrc::inspect_disc_toc_assets(image_path);
  const auto assets = std::find_if(report.levels.begin(), report.levels.end(),
                                   [level_id](const auto &candidate) {
                                     return candidate.level_id == level_id;
                                   });
  const auto layout =
      std::find_if(report.layout.levels.begin(), report.layout.levels.end(),
                   [level_id](const auto &candidate) {
                     return candidate.level_id == level_id;
                   });
  if (assets == report.levels.end() || layout == report.layout.levels.end() ||
      layout->primary_extents.empty()) {
    throw std::runtime_error("requested real-data level is absent");
  }

  constexpr std::size_t kIndexSubrange = 2U;
  constexpr std::size_t kAssetSubrange = 10U;
  const auto &index_descriptor =
      assets->primary_extent0.subranges[kIndexSubrange];
  const auto &asset_descriptor =
      assets->primary_extent0.subranges[kAssetSubrange];
  const auto primary =
      read_disc_extent(image_path, layout->primary_extents.front().lba,
                       layout->primary_extents.front().sectors);
  const auto bounded = [&primary](const openrc::DiscTocSubrange &descriptor) {
    const auto offset = static_cast<std::uint64_t>(descriptor.relative_offset);
    const auto size = static_cast<std::uint64_t>(descriptor.byte_size);
    if (offset > primary.size() || size > primary.size() - offset) {
      throw std::runtime_error("real-data subrange leaves primary extent");
    }
    return std::span<const std::byte>(primary).subspan(
        static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
  };
  const auto index_bytes = bounded(index_descriptor);
  const auto encoded_assets = bounded(asset_descriptor);
  const auto decoded =
      openrc::decode_wad_bytes(encoded_assets, kRealDataMaximumBytes);
  const auto core = openrc::parse_rac_level_core_index_v1(
      index_bytes, encoded_assets, decoded.bytes,
      {kRealDataMaximumBytes, kRealDataMaximumBytes, kRealDataMaximumBytes,
       4096U, 255U, 4096U, 4096U, 4096U, 255U, 255U});

  const auto present = static_cast<std::size_t>(
      std::count_if(core.ratchet_sequence_offsets.begin(),
                    core.ratchet_sequence_offsets.end(),
                    [](const auto offset) { return offset != 0U; }));
  std::uint64_t regular = 0U;
  std::uint64_t frames = 0U;
  std::uint64_t trigger_words = 0U;
  std::uint64_t nonzero_opaque_word_14 = 0U;
  const openrc::RacRatchetSequenceLimitsV1 sequence_limits{
      kRealDataMaximumBytes, kRealDataMaximumBytes, 255U, 255U};
  for (const auto &candidate : core.ratchet_sequences) {
    openrc::RacRatchetSequenceV1 sequence;
    try {
      sequence = openrc::parse_rac_ratchet_sequence_v1(
          decoded.bytes,
          {candidate.asset_range.offset, candidate.asset_range.size},
          sequence_limits);
    } catch (const openrc::RacRatchetSequenceError &error) {
      throw std::runtime_error(
          "level " + std::to_string(level_id) + " sequence at " +
          std::to_string(candidate.asset_range.offset) + " (" +
          std::to_string(candidate.asset_range.size) +
          " bytes): " + error.what());
    }
    ++regular;
    frames += sequence.frames.size();
    trigger_words += sequence.trigger_words.size();
    nonzero_opaque_word_14 += sequence.opaque_word_14 != 0U ? 1U : 0U;
    ++aggregate.sequences;
    aggregate.trigger_words += sequence.trigger_words.size();
    for (const auto &frame : sequence.frames) {
      observe_frame(aggregate, frame);
    }
  }

  std::cout << "level=" << level_id << " present=" << present
            << " unique=" << core.ratchet_sequences.size()
            << " regular=" << regular << " unsupported_high_bits=0"
            << " frames=" << frames << " trigger_words=" << trigger_words
            << " nonzero_opaque_word_14=" << nonzero_opaque_word_14 << '\n';
}

} // namespace

int main(const int argc, char **argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--scan-disc") {
      RealScanAggregate aggregate;
      scan_real_level(argv[2], 0U, aggregate);
      scan_real_level(argv[2], 1U, aggregate);
      print_aggregate(aggregate);
      return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--scan-disc-all") {
      RealScanAggregate aggregate;
      for (std::uint32_t level = 0U; level < openrc::kDiscTocLevelCount;
           ++level) {
        scan_real_level(argv[2], level, aggregate);
      }
      print_aggregate(aggregate);
      return 0;
    }
    if (argc != 1) {
      throw std::runtime_error("usage: openrc-rac-ratchet-sequence-tests "
                               "[--scan-disc|--scan-disc-all disc.iso]");
    }
    test_regular_sequence_and_owned_bytes();
    test_envelope_and_limit_rejections();
    test_packed_offset_and_frame_rejections();
    std::cout << "RAC Ratchet sequence tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC Ratchet sequence test failure: " << error.what() << '\n';
    return 1;
  }
}
