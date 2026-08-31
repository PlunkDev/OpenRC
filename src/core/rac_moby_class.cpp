#include "openrc/rac_moby_class.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace openrc {
namespace {

constexpr std::uint32_t kRacMobySkeletonMatrixBytes = 0x40U;
constexpr std::uint32_t kRacMobyCommonTranslationBytes = 0x10U;
constexpr std::uint32_t kRacMobySequenceHeaderBytes = 0x1cU;

[[noreturn]] void fail(const std::string &message) {
  throw RacMobyClassError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(const std::span<const std::byte> bytes,
                                      const std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(byte_value(bytes[offset])) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(byte_value(bytes[offset + 1U])) << 8U);
}

[[nodiscard]] std::uint32_t read_le32(const std::span<const std::byte> bytes,
                                      const std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string("Integer overflow while calculating ") + description);
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(const std::uint64_t left,
                                             const std::uint64_t right,
                                             const char *const description) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string("Integer overflow while calculating ") + description);
  }
  return left * right;
}

[[nodiscard]] bool is_aligned(const std::uint64_t value) noexcept {
  return (value & (kRacMobyDataAlignmentV1 - 1U)) == 0U;
}

void require_range(const std::uint64_t offset, const std::uint64_t size,
                   const std::uint64_t input_size,
                   const char *const description) {
  if (offset > input_size || size > input_size - offset) {
    fail(std::string(description) + " exceeds the MobyClass input");
  }
}

void require_optional_aligned_offset(const std::uint32_t offset,
                                     const std::size_t input_size,
                                     const char *const description) {
  if (offset == 0U) {
    return;
  }
  if (offset < kRacMobyClassHeaderBytesV1 || !is_aligned(offset) ||
      offset >= input_size) {
    fail(std::string(description) + " is not a valid aligned MobyClass offset");
  }
}

[[nodiscard]] RacMobyClassRangeV1
require_fixed_range(const std::uint32_t offset, const std::uint64_t size,
                    const std::uint64_t minimum_offset,
                    const std::size_t input_size,
                    const char *const description) {
  if (size == 0U) {
    if (offset != 0U) {
      fail(std::string(description) +
           " has an offset despite having no records");
    }
    return {};
  }
  if (offset == 0U || offset < minimum_offset || !is_aligned(offset)) {
    fail(std::string(description) + " has a missing or unaligned offset");
  }
  require_range(offset, size, input_size, description);
  return RacMobyClassRangeV1{offset, size};
}

[[nodiscard]] RacMobyClassRangeV1 require_optional_fixed_range(
    const std::uint32_t offset, const std::uint64_t size,
    const std::uint64_t minimum_offset, const std::size_t input_size,
    const char *const description) {
  if (offset == 0U) {
    return {};
  }
  if (offset < minimum_offset || !is_aligned(offset)) {
    fail(std::string(description) + " has an unaligned offset");
  }
  require_range(offset, size, input_size, description);
  return RacMobyClassRangeV1{offset, size};
}

[[nodiscard]] bool ranges_overlap(const RacMobyClassRangeV1 left,
                                  const RacMobyClassRangeV1 right) noexcept {
  if (left.size == 0U || right.size == 0U) {
    return false;
  }
  return left.offset < right.offset + right.size &&
         right.offset < left.offset + left.size;
}

void require_disjoint(const RacMobyClassRangeV1 left,
                      const RacMobyClassRangeV1 right,
                      const char *const description) {
  if (ranges_overlap(left, right)) {
    fail(std::string(description) + " overlap");
  }
}

[[nodiscard]] RacMobyPacketKindV1
packet_kind(const std::uint32_t index, const RacMobyClassV1 &result) noexcept {
  if (index < result.high_lod_packet_count) {
    return RacMobyPacketKindV1::high_lod;
  }
  if (index < static_cast<std::uint32_t>(result.high_lod_packet_count) +
                  result.low_lod_packet_count) {
    return RacMobyPacketKindV1::low_lod;
  }
  return RacMobyPacketKindV1::metal;
}

} // namespace

RacMobyClassV1 parse_rac_moby_class_v1(const std::span<const std::byte> bytes,
                                       const RacMobyClassLimitsV1 limits) {
  if (limits.max_input_bytes == 0U) {
    fail("RacMobyClassV1 caller limits must be non-zero");
  }
  if (bytes.size() > limits.max_input_bytes) {
    fail("RacMobyClassV1 exceeds the caller's input-byte limit");
  }
  if (bytes.size() < kRacMobyClassHeaderBytesV1 ||
      bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
      !is_aligned(bytes.size())) {
    fail("RacMobyClassV1 has an invalid input envelope");
  }

  RacMobyClassV1 result;
  result.input_bytes = bytes.size();
  result.header_range = {0U, kRacMobyClassHeaderBytesV1};
  result.packet_table_offset = read_le32(bytes, 0x00U);
  result.high_lod_packet_count = byte_value(bytes[0x04U]);
  result.low_lod_packet_count = byte_value(bytes[0x05U]);
  result.metal_packet_count = byte_value(bytes[0x06U]);
  result.metal_packet_begin = byte_value(bytes[0x07U]);
  result.joint_count = byte_value(bytes[0x08U]);
  result.unknown_09 = byte_value(bytes[0x09U]);
  result.rac1_byte_a = byte_value(bytes[0x0aU]);
  result.rac12_format_byte = byte_value(bytes[0x0bU]);
  result.sequence_count = byte_value(bytes[0x0cU]);
  result.sound_count = byte_value(bytes[0x0dU]);
  result.lod_transition = byte_value(bytes[0x0eU]);
  result.shadow_qwords = byte_value(bytes[0x0fU]);
  result.collision_offset = read_le32(bytes, 0x10U);
  result.skeleton_offset = read_le32(bytes, 0x14U);
  result.common_translation_offset = read_le32(bytes, 0x18U);
  result.joint_metadata_offset = read_le32(bytes, 0x1cU);
  result.gif_usage_offset = read_le32(bytes, 0x20U);
  result.scale_bits = read_le32(bytes, 0x24U);
  result.scale = std::bit_cast<float>(result.scale_bits);
  result.sound_definitions_offset = read_le32(bytes, 0x28U);
  result.bangles_offset_qwords = byte_value(bytes[0x2cU]);
  result.mip_distance = byte_value(bytes[0x2dU]);
  result.rac1_short_2e = read_le16(bytes, 0x2eU);
  for (std::size_t index = 0U; index < result.bounding_sphere.size(); ++index) {
    result.bounding_sphere_bits[index] =
        read_le32(bytes, 0x30U + index * sizeof(std::uint32_t));
    result.bounding_sphere[index] =
        std::bit_cast<float>(result.bounding_sphere_bits[index]);
  }
  result.glow_rgba = read_le32(bytes, 0x40U);
  result.mode_bits = read_le16(bytes, 0x44U);
  result.type = byte_value(bytes[0x46U]);
  result.mode_bits_2 = byte_value(bytes[0x47U]);

  if (limits.require_shared_bank_byte_b_ff &&
      result.rac12_format_byte != 0xffU) {
    fail("RacMobyClassV1 lacks the shared-bank 0xff format byte");
  }
  if (!std::isfinite(result.scale) || result.scale <= 0.0F) {
    fail("RacMobyClassV1 has an invalid scale");
  }
  for (const auto component : result.bounding_sphere) {
    if (!std::isfinite(component)) {
      fail("RacMobyClassV1 has a non-finite bounding sphere");
    }
  }
  if (result.bounding_sphere.back() <= 0.0F) {
    fail("RacMobyClassV1 has a non-positive bounding radius");
  }

  const auto regular_packet_count =
      static_cast<std::uint32_t>(result.high_lod_packet_count) +
      result.low_lod_packet_count;
  if (result.metal_packet_begin != regular_packet_count) {
    fail("RacMobyClassV1 has an invalid RAC1 mesh packet directory");
  }
  const auto packet_count =
      static_cast<std::uint32_t>(result.metal_packet_begin) +
      result.metal_packet_count;
  const auto packet_table_bytes =
      checked_multiply(packet_count, kRacMobyPacketEntryBytesV1,
                       "the RacMobyClassV1 packet-table size");

  const auto sequence_table_bytes =
      checked_multiply(result.sequence_count, sizeof(std::uint32_t),
                       "the RacMobyClassV1 sequence-offset table size");
  const auto sequence_table_end =
      checked_add(kRacMobyClassHeaderBytesV1, sequence_table_bytes,
                  "the RacMobyClassV1 sequence-offset table end");
  require_range(kRacMobyClassHeaderBytesV1, sequence_table_bytes, bytes.size(),
                "The RacMobyClassV1 sequence-offset table");
  result.sequence_offset_table_range = {kRacMobyClassHeaderBytesV1,
                                        sequence_table_bytes};

  if (packet_count == 0U) {
    if (result.packet_table_offset != 0U) {
      fail("A meshless RacMobyClassV1 has a packet-table offset");
    }
  } else {
    if (result.high_lod_packet_count == 0U ||
        !is_aligned(result.packet_table_offset) ||
        result.packet_table_offset < sequence_table_end) {
      fail("RacMobyClassV1 has an invalid packet-table offset");
    }
    require_range(result.packet_table_offset, packet_table_bytes, bytes.size(),
                  "The RacMobyClassV1 packet table");
    result.packet_table_range = {result.packet_table_offset,
                                 packet_table_bytes};
  }

  require_optional_aligned_offset(result.collision_offset, bytes.size(),
                                  "The collision block");
  require_optional_aligned_offset(result.joint_metadata_offset, bytes.size(),
                                  "The joint metadata");
  require_optional_aligned_offset(result.gif_usage_offset, bytes.size(),
                                  "The GIF-usage table");
  if (result.joint_metadata_offset == 0U) {
    fail("RacMobyClassV1 is missing its joint metadata offset");
  }

  // RAC1 permits animation-only/local classes to declare joints while
  // borrowing or omitting the optional bind skeleton and common translations.
  // Wrench likewise reads each table only when its own offset is non-zero.
  result.skeleton_range = require_optional_fixed_range(
      result.skeleton_offset,
      checked_multiply(result.joint_count, kRacMobySkeletonMatrixBytes,
                       "the RacMobyClassV1 skeleton size"),
      sequence_table_end, bytes.size(), "The RacMobyClassV1 skeleton");
  result.common_translation_range = require_optional_fixed_range(
      result.common_translation_offset,
      checked_multiply(result.joint_count, kRacMobyCommonTranslationBytes,
                       "the RacMobyClassV1 common-translation size"),
      sequence_table_end, bytes.size(),
      "The RacMobyClassV1 common translations");
  result.sound_definitions_range = require_fixed_range(
      result.sound_definitions_offset,
      checked_multiply(result.sound_count, kRacMobySoundDefinitionBytesV1,
                       "the RacMobyClassV1 sound-definition size"),
      sequence_table_end, bytes.size(), "The RacMobyClassV1 sound definitions");

  if (result.shadow_qwords != 0U) {
    const auto shadow_bytes =
        checked_multiply(result.shadow_qwords, kRacMobyDataAlignmentV1,
                         "the RacMobyClassV1 shadow size");
    if (result.skeleton_offset < shadow_bytes) {
      fail("RacMobyClassV1 has an invalid shadow offset");
    }
    const auto shadow_offset = result.skeleton_offset - shadow_bytes;
    if (shadow_offset < sequence_table_end || !is_aligned(shadow_offset)) {
      fail("RacMobyClassV1 has an invalid shadow offset");
    }
    require_range(shadow_offset, shadow_bytes, bytes.size(),
                  "The RacMobyClassV1 shadow");
    result.shadow_range = {shadow_offset, shadow_bytes};
  }

  const RacMobyClassRangeV1 directory_range{0U, sequence_table_end};
  const std::array fixed_ranges{result.shadow_range, result.skeleton_range,
                                result.common_translation_range,
                                result.sound_definitions_range};
  for (const auto range : fixed_ranges) {
    require_disjoint(directory_range, range,
                     "The RacMobyClassV1 directory and fixed data");
    require_disjoint(result.packet_table_range, range,
                     "The RacMobyClassV1 packet table and fixed data");
  }
  for (std::size_t left = 0U; left < fixed_ranges.size(); ++left) {
    for (std::size_t right = left + 1U; right < fixed_ranges.size(); ++right) {
      require_disjoint(fixed_ranges[left], fixed_ranges[right],
                       "RacMobyClassV1 fixed data ranges");
    }
  }

  if (result.bangles_offset_qwords != 0U) {
    if (packet_count == 0U) {
      fail("A meshless RacMobyClassV1 has a bangle table");
    }
    const auto bangles_offset =
        static_cast<std::uint32_t>(result.bangles_offset_qwords) *
        kRacMobyDataAlignmentV1;
    if (bangles_offset < sequence_table_end ||
        bangles_offset >= result.packet_table_offset) {
      fail("RacMobyClassV1 has an invalid bangle-table offset");
    }
  }

  result.sequence_offsets.reserve(result.sequence_count);
  const auto sequence_data_limit =
      packet_count == 0U ? static_cast<std::uint64_t>(bytes.size())
                         : result.packet_table_offset;
  std::uint32_t previous_sequence_offset = 0U;
  for (std::uint32_t index = 0U; index < result.sequence_count; ++index) {
    const auto offset = read_le32(bytes, kRacMobyClassHeaderBytesV1 +
                                             static_cast<std::size_t>(index) *
                                                 sizeof(std::uint32_t));
    result.sequence_offsets.push_back(offset);
    if (offset == 0U) {
      continue;
    }
    if (!is_aligned(offset) || offset < sequence_table_end ||
        offset >= sequence_data_limit ||
        (previous_sequence_offset != 0U &&
         offset <= previous_sequence_offset)) {
      fail("RacMobyClassV1 has an invalid sequence offset");
    }
    require_range(offset, kRacMobySequenceHeaderBytes, sequence_data_limit,
                  "A RacMobyClassV1 sequence header");
    previous_sequence_offset = offset;
  }

  result.packets.reserve(packet_count);
  const auto packet_table_end =
      checked_add(result.packet_table_offset, packet_table_bytes,
                  "the RacMobyClassV1 packet-table end");
  for (std::uint32_t index = 0U; index < packet_count; ++index) {
    const auto entry_offset =
        static_cast<std::uint64_t>(result.packet_table_offset) +
        static_cast<std::uint64_t>(index) * kRacMobyPacketEntryBytesV1;
    const auto host_offset = static_cast<std::size_t>(entry_offset);
    const auto vif_offset = read_le32(bytes, host_offset + 0x00U);
    const auto vif_qwords = read_le16(bytes, host_offset + 0x04U);
    const auto texture_unpack_offset = read_le16(bytes, host_offset + 0x06U);
    const auto vertex_offset = read_le32(bytes, host_offset + 0x08U);
    const auto vertex_qwords = byte_value(bytes[host_offset + 0x0cU]);
    const auto formula_d = byte_value(bytes[host_offset + 0x0dU]);
    const auto formula_e = byte_value(bytes[host_offset + 0x0eU]);
    const auto transfer_vertices = byte_value(bytes[host_offset + 0x0fU]);

    if (vif_qwords == 0U || vertex_qwords == 0U || transfer_vertices == 0U ||
        !is_aligned(vif_offset) || !is_aligned(vertex_offset) ||
        vif_offset < packet_table_end) {
      fail("RacMobyClassV1 has an invalid packet data envelope");
    }
    const auto vif_bytes = checked_multiply(vif_qwords, kRacMobyDataAlignmentV1,
                                            "a RacMobyClassV1 VIF range size");
    const auto vertex_bytes =
        checked_multiply(vertex_qwords, kRacMobyDataAlignmentV1,
                         "a RacMobyClassV1 vertex range size");
    require_range(vif_offset, vif_bytes, bytes.size(),
                  "A RacMobyClassV1 VIF range");
    require_range(vertex_offset, vertex_bytes, bytes.size(),
                  "A RacMobyClassV1 vertex range");
    const auto vif_end =
        checked_add(vif_offset, vif_bytes, "a RacMobyClassV1 VIF range end");
    if (vif_end > vertex_offset ||
        (texture_unpack_offset != 0U && texture_unpack_offset >= vif_qwords)) {
      fail("RacMobyClassV1 has inconsistent VIF/vertex packet ranges");
    }
    const RacMobyClassRangeV1 vif_range{vif_offset, vif_bytes};
    const RacMobyClassRangeV1 vertex_range{vertex_offset, vertex_bytes};
    for (const auto fixed_range : fixed_ranges) {
      require_disjoint(vif_range, fixed_range,
                       "A RacMobyClassV1 VIF and fixed data range");
      require_disjoint(vertex_range, fixed_range,
                       "A RacMobyClassV1 vertex and fixed data range");
    }

    const auto expected_d = static_cast<std::uint32_t>(
        (0x0fU + static_cast<std::uint32_t>(transfer_vertices) * 6U) / 0x10U);
    const auto expected_e = static_cast<std::uint32_t>(
        (3U + static_cast<std::uint32_t>(transfer_vertices)) / 4U);
    if (formula_d != expected_d || formula_e != expected_e) {
      fail("RacMobyClassV1 packet vertex formulas do not match");
    }

    result.packets.push_back(
        RacMobyPacketV1{packet_kind(index, result),
                        {entry_offset, kRacMobyPacketEntryBytesV1},
                        {vif_offset, vif_bytes},
                        {vertex_offset, vertex_bytes},
                        texture_unpack_offset,
                        vertex_qwords,
                        transfer_vertices});
  }

  return result;
}

} // namespace openrc
