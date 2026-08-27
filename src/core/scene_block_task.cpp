#include "openrc/scene_block_task.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace openrc {
namespace {

constexpr std::uint32_t kDmaReferenceTag = 0x30000000U;
constexpr std::uint32_t kVifNop = 0x00000000U;
constexpr std::uint32_t kVifStcycl4_4 = 0x01000404U;
constexpr std::uint32_t kVifStcycl2_1 = 0x01000102U;
constexpr std::uint32_t kVifStmodNormal = 0x05000000U;
constexpr std::uint32_t kVifUnpackV4_8UnsignedTops = 0x6e00c000U;
constexpr std::uint64_t kInlineVifBytes = 8U;

struct PlannedReference {
  SceneBlockTaskDmaReferenceKindV1 kind =
      SceneBlockTaskDmaReferenceKindV1::stcycl_4_4;
  std::uint32_t source_remainder_offset = 0U;
  std::uint16_t qword_count = 0U;
  std::array<std::uint32_t, 2U> inline_vif_codes{};
};

[[noreturn]] void fail(const std::string &message) {
  throw SceneBlockTaskError(message);
}

[[nodiscard]] std::uint32_t low_half(const std::uint32_t value) noexcept {
  return value & 0xffffU;
}

[[nodiscard]] std::uint32_t high_half(const std::uint32_t value) noexcept {
  return value >> 16U;
}

[[nodiscard]] std::uint8_t
descriptor_byte(const SceneBlockDirectoryEntryV1 &entry,
                const std::size_t offset) noexcept {
  const auto word = entry.raw_words[offset / 4U];
  return static_cast<std::uint8_t>(word >> ((offset % 4U) * 8U));
}

[[nodiscard]] std::uint16_t
descriptor_half(const SceneBlockDirectoryEntryV1 &entry,
                const std::size_t offset) noexcept {
  const auto word = entry.raw_words[offset / 4U];
  return static_cast<std::uint16_t>(word >> ((offset % 4U) * 8U));
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string("Integer overflow while calculating ") + description);
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(const std::uint64_t left,
                                             const std::uint64_t right,
                                             const char *description) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string("Integer overflow while calculating ") + description);
  }
  return left * right;
}

void append_le32(std::vector<std::byte> &bytes, const std::uint32_t value) {
  bytes.push_back(static_cast<std::byte>(value & 0xffU));
  bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
  bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
  bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

[[nodiscard]] std::array<std::uint32_t, kSceneBlockSectionBoundaryCount>
expected_boundaries(const SceneBlockDirectoryEntryV1 &entry) noexcept {
  return {
      low_half(entry.raw_words[5]),
      high_half(entry.raw_words[5]),
      low_half(entry.raw_words[7]),
      low_half(entry.raw_words[6]),
      high_half(entry.raw_words[6]),
      high_half(entry.raw_words[7]),
      low_half(entry.raw_words[12]),
      high_half(entry.raw_words[11]),
      entry.opaque_size,
  };
}

void validate_limits(const SceneBlockTaskBuildLimitsV1 limits) {
  if (limits.max_block_bytes == 0U || limits.max_dma_references == 0U ||
      limits.max_reference_bytes == 0U || limits.vif.max_input_bytes == 0U ||
      limits.vif.max_commands == 0U || limits.vif.max_payload_bytes == 0U) {
    fail("SceneBlock task caller limits must all be non-zero");
  }
}

void validate_entry(const SceneBlockDirectoryEntryV1 &entry,
                    const SceneBlockTaskBuildLimitsV1 limits) {
  const auto owned_bytes = static_cast<std::uint64_t>(entry.block_bytes.size());
  if (owned_bytes > limits.max_block_bytes) {
    fail("The SceneBlock task block exceeds the caller's byte limit");
  }

  const auto expected_owned_bytes =
      checked_add(kSceneBlockDirectoryV1Stride, entry.opaque_size,
                  "the SceneBlock task block size");
  if (owned_bytes != expected_owned_bytes) {
    fail("The SceneBlock task block_bytes size disagrees with opaque_size");
  }
  if (entry.raw_words[4] != entry.block_offset ||
      entry.raw_words[14] != entry.opaque_size) {
    fail("The SceneBlock task decoded descriptor fields are inconsistent");
  }
  if (entry.raw_words[13] != kSceneBlockSectionMarker ||
      high_half(entry.raw_words[11]) != high_half(entry.raw_words[12])) {
    fail("The SceneBlock task packed section metadata is inconsistent");
  }

  const auto expected_block_end =
      checked_add(entry.block_offset, expected_owned_bytes,
                  "the SceneBlock task block end");
  if (entry.block_end != expected_block_end ||
      entry.block_range !=
          SceneBlockRange{entry.block_offset, expected_owned_bytes} ||
      entry.prefix_range !=
          SceneBlockRange{entry.block_offset, kSceneBlockDirectoryV1Stride} ||
      entry.remainder_range !=
          SceneBlockRange{checked_add(entry.block_offset,
                                      kSceneBlockDirectoryV1Stride,
                                      "the SceneBlock task remainder offset"),
                          entry.opaque_size}) {
    fail("The SceneBlock task owned ranges are inconsistent");
  }

  const auto boundaries = expected_boundaries(entry);
  if (entry.section_layout.relative_boundaries != boundaries ||
      boundaries.front() != 0U || boundaries.back() != entry.opaque_size) {
    fail("The SceneBlock task section boundaries are inconsistent");
  }

  const auto final_section_bytes = checked_multiply(
      low_half(entry.raw_words[11]), kSceneBlockSectionAlignment,
      "the SceneBlock task final section size");
  if (checked_add(boundaries[7U], final_section_bytes,
                  "the SceneBlock task final section end") !=
      entry.opaque_size) {
    fail("The SceneBlock task final section redundancy is inconsistent");
  }

  for (std::size_t index = 0U; index < kSceneBlockSectionCount; ++index) {
    const auto begin = boundaries[index];
    const auto end = boundaries[index + 1U];
    if (begin % kSceneBlockSectionAlignment != 0U ||
        end % kSceneBlockSectionAlignment != 0U || begin >= end) {
      fail("The SceneBlock task section boundaries are invalid");
    }
    const auto expected_range =
        SceneBlockRange{checked_add(entry.remainder_range.offset, begin,
                                    "a SceneBlock task section offset"),
                        end - begin};
    if (entry.section_layout.ranges[index] != expected_range) {
      fail("The SceneBlock task section ranges are inconsistent");
    }
  }
}

[[nodiscard]] std::uint32_t
make_tail_unpack_code(const std::uint8_t raw_num,
                      const std::uint8_t destination) noexcept {
  return kVifUnpackV4_8UnsignedTops |
         (static_cast<std::uint32_t>(raw_num) << 16U) | destination;
}

void validate_tail_fields(const std::uint8_t raw_num,
                          const std::uint8_t destination) {
  if (raw_num == 0U) {
    fail("The SceneBlock task tail UNPACK NUM must not use the 256-vector "
         "encoding");
  }
  if (raw_num % 4U != 0U) {
    fail("The SceneBlock task tail UNPACK NUM is not DMA-qword divisible");
  }
  const auto final_destination =
      checked_add(destination,
                  checked_multiply(raw_num - 1U, 2U,
                                   "the SceneBlock task tail destination span"),
                  "the SceneBlock task tail final destination");
  if (final_destination >= kSceneBlockTaskInputBankQwords) {
    fail("The SceneBlock task tail UNPACK exceeds one input bank");
  }
}

void add_reference(
    std::array<PlannedReference, kSceneBlockTaskMaximumDmaReferences> &plan,
    std::size_t &count, const SceneBlockTaskDmaReferenceKindV1 kind,
    const std::uint32_t source_remainder_offset,
    const std::uint16_t qword_count,
    const std::array<std::uint32_t, 2U> inline_codes) {
  if (count >= plan.size()) {
    fail("Internal SceneBlock task DMA reference count overflow");
  }
  plan[count++] = PlannedReference{kind, source_remainder_offset, qword_count,
                                   inline_codes};
}

} // namespace

SceneBlockTaskVifInvocationV1 build_scene_block_task_vif_invocation_v1(
    const SceneBlockDirectoryEntryV1 &entry,
    const std::uint16_t entrypoint_address,
    const SceneBlockTaskBuildLimitsV1 limits) {
  validate_limits(limits);
  validate_entry(entry, limits);

  const auto boundary0 = descriptor_half(entry, 0x14U);
  const auto boundary1 = descriptor_half(entry, 0x16U);
  const auto boundary4 = descriptor_half(entry, 0x1aU);
  const auto boundary5 = descriptor_half(entry, 0x1eU);
  const auto boundary6 = entry.section_layout.relative_boundaries[6U];
  const auto count0 = descriptor_byte(entry, 0x20U);
  const auto count1 = descriptor_byte(entry, 0x21U);
  const auto count2 = descriptor_byte(entry, 0x22U);
  const auto count3 = descriptor_byte(entry, 0x23U);
  const auto num6 = descriptor_byte(entry, 0x24U);
  const auto num8 = descriptor_byte(entry, 0x25U);
  const auto num_high = descriptor_byte(entry, 0x26U);
  const auto destination = descriptor_byte(entry, 0x2aU);

  std::array<PlannedReference, kSceneBlockTaskMaximumDmaReferences> plan{};
  std::size_t plan_count = 0U;
  std::uint8_t tail_num = 0U;

  const auto t4 = std::array<std::uint32_t, 2U>{kVifStcycl4_4, kVifNop};

  switch (entrypoint_address) {
  case 6U:
    tail_num = num6;
    validate_tail_fields(tail_num, destination);
    add_reference(plan, plan_count,
                  SceneBlockTaskDmaReferenceKindV1::stcycl_4_4, boundary0,
                  count1, t4);
    add_reference(
        plan, plan_count,
        SceneBlockTaskDmaReferenceKindV1::stcycl_2_1_unpack_v4_8, boundary5,
        static_cast<std::uint16_t>(tail_num >> 2U),
        {kVifStcycl2_1, make_tail_unpack_code(tail_num, destination)});
    break;

  case 8U:
  case 10U:
    tail_num = num8;
    validate_tail_fields(tail_num, destination);
    add_reference(plan, plan_count,
                  SceneBlockTaskDmaReferenceKindV1::stcycl_4_4, boundary1,
                  count2, t4);
    add_reference(
        plan, plan_count,
        SceneBlockTaskDmaReferenceKindV1::stmod_normal_unpack_v4_8, boundary5,
        static_cast<std::uint16_t>(tail_num >> 2U),
        {kVifStmodNormal, make_tail_unpack_code(tail_num, destination)});
    break;

  case 14U:
  case 16U:
  case 20U:
    tail_num = num_high;
    validate_tail_fields(tail_num, destination);
    add_reference(plan, plan_count,
                  SceneBlockTaskDmaReferenceKindV1::stcycl_4_4, boundary1,
                  count0, t4);
    add_reference(plan, plan_count,
                  SceneBlockTaskDmaReferenceKindV1::stcycl_4_4, boundary4,
                  count3, t4);
    add_reference(
        plan, plan_count,
        SceneBlockTaskDmaReferenceKindV1::stcycl_2_1_unpack_v4_8, boundary5,
        static_cast<std::uint16_t>(tail_num >> 2U),
        {kVifStcycl2_1, make_tail_unpack_code(tail_num, destination)});
    break;

  default:
    fail(
        "The SceneBlock task entrypoint is not one of 6, 8, 10, 14, 16, or 20");
  }

  if (plan_count > limits.max_dma_references) {
    fail("The SceneBlock task DMA reference limit was exceeded");
  }

  std::uint64_t total_reference_bytes = 0U;
  std::uint64_t expanded_bytes = 0U;
  for (std::size_t index = 0U; index < plan_count; ++index) {
    const auto &reference = plan[index];
    if (reference.qword_count == 0U) {
      fail("A SceneBlock task DMA reference has zero QWC");
    }
    if (reference.source_remainder_offset % kSceneBlockSectionAlignment != 0U) {
      fail("A SceneBlock task DMA reference source is not qword-aligned");
    }
    const auto reference_bytes =
        checked_multiply(reference.qword_count, kSceneBlockSectionAlignment,
                         "a SceneBlock task DMA reference size");
    const auto reference_end =
        checked_add(reference.source_remainder_offset, reference_bytes,
                    "a SceneBlock task DMA reference end");
    if (reference_end > entry.opaque_size) {
      fail("A SceneBlock task DMA reference exceeds the owned remainder");
    }

    const bool is_tail = index + 1U == plan_count;
    if (is_tail) {
      if (reference.source_remainder_offset != boundary5 ||
          reference_end > boundary6) {
        fail("The SceneBlock task tail reference exceeds section 5");
      }
    } else if (reference_end > boundary5) {
      fail("A SceneBlock task stored-stream reference exceeds section 4");
    }

    total_reference_bytes =
        checked_add(total_reference_bytes, reference_bytes,
                    "the SceneBlock task aggregate reference size");
    expanded_bytes =
        checked_add(expanded_bytes,
                    checked_add(kInlineVifBytes, reference_bytes,
                                "a SceneBlock task expanded reference size"),
                    "the SceneBlock task expanded VIF size");
  }
  if (total_reference_bytes > limits.max_reference_bytes) {
    fail("The SceneBlock task aggregate reference-byte limit was exceeded");
  }
  if (expanded_bytes > limits.vif.max_input_bytes) {
    fail("The SceneBlock task expanded VIF-byte limit was exceeded");
  }

  SceneBlockTaskVifInvocationV1 result;
  result.entrypoint_address = entrypoint_address;
  result.trailing_mscal_code = kSceneBlockTaskVifMscalCode | entrypoint_address;
  result.total_reference_bytes = total_reference_bytes;
  if (plan_count > result.dma_references.max_size() ||
      expanded_bytes > result.vif_bytes.max_size() ||
      expanded_bytes > std::numeric_limits<std::size_t>::max()) {
    fail("The SceneBlock task result exceeds a host container limit");
  }
  result.dma_references.reserve(plan_count);
  result.vif_bytes.reserve(static_cast<std::size_t>(expanded_bytes));

  const auto block_bytes = std::span<const std::byte>(entry.block_bytes);
  for (std::size_t index = 0U; index < plan_count; ++index) {
    const auto &planned = plan[index];
    const auto output_inline_offset =
        static_cast<std::uint64_t>(result.vif_bytes.size());
    append_le32(result.vif_bytes, planned.inline_vif_codes[0U]);
    append_le32(result.vif_bytes, planned.inline_vif_codes[1U]);

    const auto reference_bytes =
        checked_multiply(planned.qword_count, kSceneBlockSectionAlignment,
                         "a SceneBlock task copied reference size");
    const auto source_block_offset = checked_add(
        kSceneBlockDirectoryV1Stride, planned.source_remainder_offset,
        "a SceneBlock task source block offset");
    const auto host_source_offset =
        static_cast<std::size_t>(source_block_offset);
    const auto host_reference_bytes = static_cast<std::size_t>(reference_bytes);
    const auto payload =
        block_bytes.subspan(host_source_offset, host_reference_bytes);
    result.vif_bytes.insert(result.vif_bytes.end(), payload.begin(),
                            payload.end());

    SceneBlockTaskDmaReferenceV1 reference;
    reference.kind = planned.kind;
    reference.dma_tag_word0 = kDmaReferenceTag | planned.qword_count;
    reference.qword_count = planned.qword_count;
    reference.source_remainder_range = SceneBlockTaskByteRangeV1{
        planned.source_remainder_offset, reference_bytes};
    reference.source_block_range =
        SceneBlockTaskByteRangeV1{source_block_offset, reference_bytes};
    reference.source_input_range = SceneBlockTaskByteRangeV1{
        checked_add(entry.remainder_range.offset,
                    planned.source_remainder_offset,
                    "a SceneBlock task source input offset"),
        reference_bytes};
    reference.inline_vif_range =
        SceneBlockTaskByteRangeV1{output_inline_offset, kInlineVifBytes};
    reference.expanded_reference_range = SceneBlockTaskByteRangeV1{
        output_inline_offset + kInlineVifBytes, reference_bytes};
    reference.inline_vif_codes = planned.inline_vif_codes;
    result.dma_references.push_back(reference);
  }

  if (result.vif_bytes.size() != expanded_bytes ||
      result.dma_references.size() != plan_count) {
    fail("Internal SceneBlock task expansion accounting mismatch");
  }

  const auto tail_reference_index = plan_count - 1U;
  const auto &tail_reference = result.dma_references[tail_reference_index];
  result.tail_unpack = SceneBlockTaskUnpackPatchV1{
      tail_num, destination, make_tail_unpack_code(tail_num, destination),
      tail_reference_index,
      SceneBlockTaskByteRangeV1{tail_reference.inline_vif_range.offset + 4U,
                                kSceneBlockVifCodeSize}};

  SceneBlockVifStreamV1 parsed;
  try {
    parsed = parse_scene_block_vif_stream_v1(result.vif_bytes, limits.vif);
  } catch (const SceneBlockVifError &error) {
    fail("The expanded SceneBlock task VIF stream is invalid: " +
         std::string(error.what()));
  }
  if (parsed.commands.empty()) {
    fail("The expanded SceneBlock task VIF stream has no commands");
  }
  const auto &unpack = parsed.commands.back();
  if (unpack.raw_code != result.tail_unpack.raw_code ||
      unpack.code_range.offset != result.tail_unpack.code_range.offset ||
      unpack.code_range.size != result.tail_unpack.code_range.size ||
      unpack.payload_range.offset !=
          tail_reference.expanded_reference_range.offset ||
      unpack.payload_range.size !=
          tail_reference.expanded_reference_range.size ||
      unpack.opcode != SceneBlockVifOpcode::unpack_v4_8 ||
      unpack.raw_num != tail_num || unpack.destination_address != destination ||
      !unpack.unsigned_data || !unpack.use_tops) {
    fail("The expanded SceneBlock task tail UNPACK disagrees with its REF "
         "payload");
  }
  if (parsed.final_cycle_length != 2U || parsed.final_write_length != 1U) {
    fail("The expanded SceneBlock task stream does not leave STCYCL at 2/1");
  }

  std::uint16_t mode = 0U;
  for (const auto &command : parsed.commands) {
    if (command.opcode == SceneBlockVifOpcode::stmod) {
      mode = command.immediate;
    }
    if (command.code_range.offset == result.tail_unpack.code_range.offset &&
        mode != 0U) {
      fail("The expanded SceneBlock task tail UNPACK is not in normal STMOD "
           "mode");
    }
  }

  return result;
}

} // namespace openrc
