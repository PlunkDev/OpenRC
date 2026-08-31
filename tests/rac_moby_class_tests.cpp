#include "openrc/rac_moby_class.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kClassBytes = 0xd0U;
constexpr std::uint32_t kSequenceOffset = 0x50U;
constexpr std::uint32_t kJointMetadataOffset = 0x70U;
constexpr std::uint32_t kPacketTableOffset = 0x80U;
constexpr std::uint32_t kVifOffset = 0x90U;
constexpr std::uint32_t kVertexOffset = 0xb0U;
constexpr openrc::RacMobyClassLimitsV1 kLimits{0x10000U};

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

void write_packet_entry(std::vector<std::byte> &bytes,
                        const std::size_t entry_offset,
                        const std::uint32_t vif_offset,
                        const std::uint32_t vertex_offset) {
  write_le32(bytes, entry_offset + 0x00U, vif_offset);
  write_le16(bytes, entry_offset + 0x04U, 2U);
  write_le32(bytes, entry_offset + 0x08U, vertex_offset);
  bytes[entry_offset + 0x0cU] = std::byte{2};
  bytes[entry_offset + 0x0dU] = std::byte{2};
  bytes[entry_offset + 0x0eU] = std::byte{1};
  bytes[entry_offset + 0x0fU] = std::byte{4};
}

[[nodiscard]] std::vector<std::byte> make_class() {
  std::vector<std::byte> bytes(kClassBytes, std::byte{0});
  write_le32(bytes, 0x00U, kPacketTableOffset);
  bytes[0x04U] = std::byte{1};
  bytes[0x07U] = std::byte{1};
  bytes[0x0bU] = std::byte{0xff};
  bytes[0x0cU] = std::byte{1};
  write_le32(bytes, 0x1cU, kJointMetadataOffset);
  write_le32(bytes, 0x24U, std::bit_cast<std::uint32_t>(1.0F));
  write_le32(bytes, 0x3cU, std::bit_cast<std::uint32_t>(2.0F));
  write_le32(bytes, 0x48U, kSequenceOffset);

  write_packet_entry(bytes, kPacketTableOffset, kVifOffset, kVertexOffset);
  return bytes;
}

[[nodiscard]] std::vector<std::byte> make_meshless_class() {
  std::vector<std::byte> bytes(0x90U, std::byte{0});
  bytes[0x08U] = std::byte{20};
  bytes[0x0bU] = std::byte{0xff};
  bytes[0x0cU] = std::byte{1};
  write_le32(bytes, 0x1cU, 0x70U);
  write_le32(bytes, 0x24U, std::bit_cast<std::uint32_t>(1.0F));
  write_le32(bytes, 0x3cU, std::bit_cast<std::uint32_t>(2.0F));
  write_le32(bytes, 0x48U, 0x50U);
  return bytes;
}

[[nodiscard]] std::vector<std::byte> make_multi_packet_class() {
  std::vector<std::byte> bytes(0x200U, std::byte{0});
  write_le32(bytes, 0x00U, kPacketTableOffset);
  bytes[0x04U] = std::byte{2};
  bytes[0x05U] = std::byte{1};
  bytes[0x06U] = std::byte{2};
  bytes[0x07U] = std::byte{3};
  bytes[0x0bU] = std::byte{0xff};
  bytes[0x0cU] = std::byte{1};
  write_le32(bytes, 0x1cU, kJointMetadataOffset);
  write_le32(bytes, 0x24U, std::bit_cast<std::uint32_t>(1.0F));
  write_le32(bytes, 0x3cU, std::bit_cast<std::uint32_t>(2.0F));
  write_le32(bytes, 0x48U, kSequenceOffset);

  write_packet_entry(bytes, 0x80U, 0xe0U, 0x100U);
  write_packet_entry(bytes, 0x90U, 0x180U, 0x1a0U);
  write_packet_entry(bytes, 0xa0U, 0x140U, 0x160U);
  write_packet_entry(bytes, 0xb0U, 0xe0U, 0x100U);
  write_packet_entry(bytes, 0xc0U, 0x1c0U, 0x1e0U);
  return bytes;
}

[[nodiscard]] std::vector<std::byte> make_shadowed_class() {
  auto bytes = make_class();
  bytes.resize(0x140U, std::byte{0});
  bytes[0x08U] = std::byte{1};
  bytes[0x0fU] = std::byte{1};
  write_le32(bytes, 0x14U, 0xa0U);
  write_le32(bytes, 0x18U, 0xe0U);
  write_le32(bytes, 0x1cU, 0xf0U);
  write_packet_entry(bytes, kPacketTableOffset, 0x100U, 0x120U);
  return bytes;
}

template <typename Mutation>
void expect_rejected(Mutation &&mutation, const std::string &message) {
  auto bytes = make_class();
  std::invoke(std::forward<Mutation>(mutation), bytes);
  try {
    (void)openrc::parse_rac_moby_class_v1(bytes, kLimits);
  } catch (const openrc::RacMobyClassError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_valid_class() {
  const auto result = openrc::parse_rac_moby_class_v1(make_class(), kLimits);
  expect(result.input_bytes == kClassBytes && result.scale == 1.0F &&
             result.bounding_sphere[3U] == 2.0F &&
             result.high_lod_packet_count == 1U &&
             result.low_lod_packet_count == 0U &&
             result.metal_packet_count == 0U &&
             result.sequence_offsets.size() == 1U &&
             result.sequence_offsets[0U] == kSequenceOffset,
         "RAC MobyClass header metadata is wrong");
  expect(result.header_range == openrc::RacMobyClassRangeV1{0U, 0x48U} &&
             result.sequence_offset_table_range ==
                 openrc::RacMobyClassRangeV1{0x48U, 4U} &&
             result.packet_table_range ==
                 openrc::RacMobyClassRangeV1{kPacketTableOffset, 0x10U},
         "RAC MobyClass directory ranges are wrong");
  expect(result.packets.size() == 1U &&
             result.packets[0U].kind == openrc::RacMobyPacketKindV1::high_lod &&
             result.packets[0U].table_entry_range ==
                 openrc::RacMobyClassRangeV1{kPacketTableOffset, 0x10U} &&
             result.packets[0U].vif_range ==
                 openrc::RacMobyClassRangeV1{kVifOffset, 0x20U} &&
             result.packets[0U].vertex_range ==
                 openrc::RacMobyClassRangeV1{kVertexOffset, 0x20U} &&
             result.packets[0U].transfer_vertex_count == 4U,
         "RAC MobyClass packet metadata is wrong");
}

void test_meshless_class() {
  const auto result =
      openrc::parse_rac_moby_class_v1(make_meshless_class(), kLimits);
  expect(result.packet_table_offset == 0U &&
             result.packet_table_range == openrc::RacMobyClassRangeV1{} &&
             result.packets.empty() && result.sequence_offsets.size() == 1U &&
             result.sequence_offsets[0U] == 0x50U &&
             result.joint_count == 20U &&
             result.skeleton_range == openrc::RacMobyClassRangeV1{} &&
             result.common_translation_range ==
                 openrc::RacMobyClassRangeV1{},
         "a coherent meshless RAC MobyClass was not preserved");

  auto incoherent = make_meshless_class();
  write_le32(incoherent, 0x00U, 0x50U);
  try {
    (void)openrc::parse_rac_moby_class_v1(incoherent, kLimits);
  } catch (const openrc::RacMobyClassError &) {
    return;
  }
  throw std::runtime_error(
      "a meshless RAC MobyClass with a packet table was accepted");
}

void test_zero_joint_optional_skeleton_marker() {
  auto bytes = make_meshless_class();
  bytes[0x08U] = std::byte{0};
  write_le32(bytes, 0x14U, 0x80U);
  const auto result = openrc::parse_rac_moby_class_v1(bytes, kLimits);
  expect(result.skeleton_range ==
             openrc::RacMobyClassRangeV1{0x80U, 0U},
         "a zero-joint optional skeleton marker was not preserved");
}

void test_multiple_packet_kinds_and_shared_storage() {
  const auto result =
      openrc::parse_rac_moby_class_v1(make_multi_packet_class(), kLimits);
  expect(result.packets.size() == 5U && result.high_lod_packet_count == 2U &&
             result.low_lod_packet_count == 1U &&
             result.metal_packet_count == 2U,
         "multi-packet RAC MobyClass counts are wrong");
  expect(result.packets[0U].kind == openrc::RacMobyPacketKindV1::high_lod &&
             result.packets[1U].kind == openrc::RacMobyPacketKindV1::high_lod &&
             result.packets[2U].kind == openrc::RacMobyPacketKindV1::low_lod &&
             result.packets[3U].kind == openrc::RacMobyPacketKindV1::metal &&
             result.packets[4U].kind == openrc::RacMobyPacketKindV1::metal,
         "multi-packet RAC MobyClass kinds are wrong");
  expect(result.packets[0U].vif_range == result.packets[3U].vif_range &&
             result.packets[1U].vif_range.offset >
                 result.packets[2U].vif_range.offset,
         "shared or non-physical-order packet storage was not accepted");
}

void test_shadow_range() {
  const auto result =
      openrc::parse_rac_moby_class_v1(make_shadowed_class(), kLimits);
  expect(result.shadow_range == openrc::RacMobyClassRangeV1{0x90U, 0x10U} &&
             result.skeleton_range == openrc::RacMobyClassRangeV1{0xa0U, 0x40U},
         "RAC MobyClass shadow range is wrong");

  auto shadow_overlaps_table = make_shadowed_class();
  write_le32(shadow_overlaps_table, 0x14U, 0x90U);
  try {
    (void)openrc::parse_rac_moby_class_v1(shadow_overlaps_table, kLimits);
  } catch (const openrc::RacMobyClassError &) {
    return;
  }
  throw std::runtime_error(
      "a RAC MobyClass shadow overlapping its packet table was accepted");
}

void test_limits() {
  const auto bytes = make_class();
  for (const auto limits : {openrc::RacMobyClassLimitsV1{0U},
                            openrc::RacMobyClassLimitsV1{kClassBytes - 1U}}) {
    try {
      (void)openrc::parse_rac_moby_class_v1(bytes, limits);
    } catch (const openrc::RacMobyClassError &) {
      continue;
    }
    throw std::runtime_error("a RAC MobyClass caller limit was ignored");
  }
}

void test_header_rejections() {
  expect_rejected([](auto &bytes) { bytes.resize(0x40U); },
                  "a truncated RAC MobyClass header was accepted");
  expect_rejected([](auto &bytes) { bytes.push_back(std::byte{0}); },
                  "an unaligned RAC MobyClass envelope was accepted");
  auto local_format_byte = make_class();
  local_format_byte[0x0bU] = std::byte{0x13};
  const auto local_result =
      openrc::parse_rac_moby_class_v1(local_format_byte, kLimits);
  expect(local_result.rac12_format_byte == 0x13U,
         "a local RAC1 MobyClass format byte was not preserved");
  try {
    (void)openrc::parse_rac_moby_class_v1(
        local_format_byte,
        openrc::RacMobyClassLimitsV1{kLimits.max_input_bytes, true});
    throw std::runtime_error(
        "a non-0xff shared-bank MobyClass format byte was accepted");
  } catch (const openrc::RacMobyClassError &) {
  }
  expect_rejected(
      [](auto &bytes) {
        write_le32(bytes, 0x24U,
                   std::bit_cast<std::uint32_t>(
                       std::numeric_limits<float>::quiet_NaN()));
      },
      "a non-finite RAC MobyClass scale was accepted");
  expect_rejected([](auto &bytes) { write_le32(bytes, 0x3cU, 0U); },
                  "a non-positive MobyClass bounding radius was accepted");
  expect_rejected([](auto &bytes) { bytes[0x04U] = std::byte{0}; },
                  "a MobyClass without high-LOD packets was accepted");
  expect_rejected([](auto &bytes) { bytes[0x07U] = std::byte{2}; },
                  "a bad MobyClass metal-begin index was accepted");
  expect_rejected(
      [](auto &bytes) { write_le32(bytes, 0x00U, kPacketTableOffset + 1U); },
      "an unaligned MobyClass packet table was accepted");
  expect_rejected([](auto &bytes) { bytes[0x0cU] = std::byte{32}; },
                  "an overlapping MobyClass sequence table was accepted");
  expect_rejected([](auto &bytes) { write_le32(bytes, 0x48U, 0x51U); },
                  "an unaligned MobyClass sequence was accepted");
  expect_rejected([](auto &bytes) { write_le32(bytes, 0x1cU, 0U); },
                  "missing MobyClass joint metadata was accepted");
}

void test_packet_rejections() {
  expect_rejected(
      [](auto &bytes) {
        write_le32(bytes, kPacketTableOffset + 0x00U, kVifOffset + 1U);
      },
      "an unaligned MobyClass VIF range was accepted");
  expect_rejected(
      [](auto &bytes) { write_le16(bytes, kPacketTableOffset + 0x04U, 0U); },
      "an empty MobyClass VIF range was accepted");
  expect_rejected(
      [](auto &bytes) { write_le16(bytes, kPacketTableOffset + 0x06U, 2U); },
      "an out-of-range MobyClass texture UNPACK offset was accepted");
  expect_rejected(
      [](auto &bytes) {
        write_le32(bytes, kPacketTableOffset + 0x08U, kClassBytes);
      },
      "an out-of-range MobyClass vertex block was accepted");
  expect_rejected(
      [](auto &bytes) { bytes[kPacketTableOffset + 0x0cU] = std::byte{0}; },
      "an empty MobyClass vertex block was accepted");
  expect_rejected(
      [](auto &bytes) { bytes[kPacketTableOffset + 0x0dU] = std::byte{1}; },
      "a bad MobyClass vertex D formula was accepted");
  expect_rejected(
      [](auto &bytes) { bytes[kPacketTableOffset + 0x0eU] = std::byte{2}; },
      "a bad MobyClass vertex E formula was accepted");
  expect_rejected(
      [](auto &bytes) { bytes[kPacketTableOffset + 0x0fU] = std::byte{0}; },
      "a zero MobyClass transfer-vertex count was accepted");
  expect_rejected(
      [](auto &bytes) {
        write_le32(bytes, kPacketTableOffset + 0x08U, kVifOffset + 0x10U);
      },
      "overlapping MobyClass VIF and vertex ranges were accepted");
}

void test_fixed_range_overlap_rejection() {
  auto bytes = make_shadowed_class();
  write_le32(bytes, 0x18U, 0xd0U);
  try {
    (void)openrc::parse_rac_moby_class_v1(bytes, kLimits);
  } catch (const openrc::RacMobyClassError &) {
    return;
  }
  throw std::runtime_error(
      "overlapping fixed RAC MobyClass ranges were accepted");
}

} // namespace

int main() {
  try {
    test_valid_class();
    test_meshless_class();
    test_zero_joint_optional_skeleton_marker();
    test_multiple_packet_kinds_and_shared_storage();
    test_shadow_range();
    test_limits();
    test_header_rejections();
    test_packet_rejections();
    test_fixed_range_overlap_rejection();
    std::cout << "OpenRC RacMobyClassV1 tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OpenRC RacMobyClassV1 tests failed: " << error.what() << '\n';
    return 1;
  }
}
