#include "openrc/scene_block_task.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

constexpr openrc::SceneBlockTaskBuildLimitsV1 kGenerousLimits{
    1024U * 1024U,
    openrc::kSceneBlockTaskMaximumDmaReferences,
    1024U * 1024U,
    openrc::SceneBlockVifLimits{1024U * 1024U, 1024U, 1024U * 1024U},
};

constexpr std::array<std::uint32_t, openrc::kSceneBlockSectionBoundaryCount>
    kBoundaries{0x00U, 0x20U, 0x30U, 0x50U, 0x60U, 0x90U, 0xb0U, 0xc0U, 0xd0U};

constexpr std::uint32_t kBlockOffset = 0x1000U;
constexpr std::uint32_t kOpaqueSize = kBoundaries.back();
constexpr std::uint32_t kStcycl4_4 = 0x01000404U;
constexpr std::uint32_t kStcycl2_1 = 0x01000102U;
constexpr std::uint32_t kStmodNormal = 0x05000000U;
constexpr std::uint32_t kUnpackBase = 0x6e00c000U;

void expect(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callable>
void expect_task_error(Callable &&callable, const char *message) {
  try {
    callable();
  } catch (const openrc::SceneBlockTaskError &) {
    return;
  }
  throw std::runtime_error(message);
}

void append_le32(std::vector<std::byte> &bytes, const std::uint32_t value) {
  bytes.push_back(static_cast<std::byte>(value & 0xffU));
  bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
  bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
  bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

void store_le32(std::vector<std::byte> &bytes, const std::size_t offset,
                const std::uint32_t value) {
  for (std::size_t byte = 0U; byte < 4U; ++byte) {
    bytes.at(offset + byte) =
        static_cast<std::byte>((value >> (byte * 8U)) & 0xffU);
  }
}

void set_descriptor_byte(openrc::SceneBlockDirectoryEntryV1 &entry,
                         const std::size_t offset, const std::uint8_t value) {
  const auto index = offset / 4U;
  const auto shift = static_cast<std::uint32_t>((offset % 4U) * 8U);
  const auto mask = ~(0xffU << shift);
  entry.raw_words[index] = (entry.raw_words[index] & mask) |
                           (static_cast<std::uint32_t>(value) << shift);
}

[[nodiscard]] openrc::SceneBlockDirectoryEntryV1 make_entry() {
  openrc::SceneBlockDirectoryEntryV1 entry;
  entry.directory_entry_offset = 0x400U;
  entry.block_offset = kBlockOffset;
  entry.opaque_size = kOpaqueSize;
  entry.block_end =
      kBlockOffset + openrc::kSceneBlockDirectoryV1Stride + kOpaqueSize;

  entry.raw_words[4U] = entry.block_offset;
  entry.raw_words[5U] = kBoundaries[0U] | (kBoundaries[1U] << 16U);
  entry.raw_words[6U] = kBoundaries[3U] | (kBoundaries[4U] << 16U);
  entry.raw_words[7U] = kBoundaries[2U] | (kBoundaries[5U] << 16U);
  // byte20=c0=3, byte21=c1=5, byte22=c2=4, byte23=c3=3.
  entry.raw_words[8U] = 0x03040503U;
  // byte24=n6=4, byte25=n8=8, byte26=nHigh=8.
  entry.raw_words[9U] = 0x00080804U;
  // Tail UNPACK destination byte at descriptor offset 0x2a.
  entry.raw_words[10U] = 0x00190000U;
  entry.raw_words[11U] =
      ((kOpaqueSize - kBoundaries[7U]) / openrc::kSceneBlockSectionAlignment) |
      (kBoundaries[7U] << 16U);
  entry.raw_words[12U] = kBoundaries[6U] | (kBoundaries[7U] << 16U);
  entry.raw_words[13U] = openrc::kSceneBlockSectionMarker;
  entry.raw_words[14U] = entry.opaque_size;

  const auto owned_size = openrc::kSceneBlockDirectoryV1Stride +
                          static_cast<std::uint64_t>(kOpaqueSize);
  entry.block_range = openrc::SceneBlockRange{kBlockOffset, owned_size};
  entry.prefix_range = openrc::SceneBlockRange{
      kBlockOffset, openrc::kSceneBlockDirectoryV1Stride};
  entry.remainder_range = openrc::SceneBlockRange{
      kBlockOffset + openrc::kSceneBlockDirectoryV1Stride, kOpaqueSize};
  entry.section_layout.relative_boundaries = kBoundaries;
  for (std::size_t index = 0U; index < openrc::kSceneBlockSectionCount;
       ++index) {
    entry.section_layout.ranges[index] = openrc::SceneBlockRange{
        entry.remainder_range.offset + kBoundaries[index],
        kBoundaries[index + 1U] - kBoundaries[index]};
  }

  entry.block_bytes.resize(static_cast<std::size_t>(owned_size));
  const auto remainder = openrc::kSceneBlockDirectoryV1Stride;
  // Entry 8/10 uses t6, so its stored stream must already have selected
  // STCYCL 2/1 before t6 contributes STMOD normal and the tail UNPACK.
  store_le32(entry.block_bytes, remainder + 0x5cU, kStcycl2_1);
  for (std::size_t index = kBoundaries[5U]; index < kBoundaries[6U]; ++index) {
    entry.block_bytes[remainder + index] =
        static_cast<std::byte>((0xa5U + index * 13U) & 0xffU);
  }
  return entry;
}

struct ExpectedReference {
  openrc::SceneBlockTaskDmaReferenceKindV1 kind;
  std::uint32_t source;
  std::uint16_t qwc;
  std::array<std::uint32_t, 2U> codes;
};

[[nodiscard]] std::vector<ExpectedReference>
expected_references(const std::uint16_t entrypoint) {
  const auto tail4 = kUnpackBase | (4U << 16U) | 0x19U;
  const auto tail8 = kUnpackBase | (8U << 16U) | 0x19U;
  if (entrypoint == 6U) {
    return {
        {openrc::SceneBlockTaskDmaReferenceKindV1::stcycl_4_4,
         0x00U,
         5U,
         {kStcycl4_4, 0U}},
        {openrc::SceneBlockTaskDmaReferenceKindV1::stcycl_2_1_unpack_v4_8,
         0x90U,
         1U,
         {kStcycl2_1, tail4}},
    };
  }
  if (entrypoint == 8U || entrypoint == 10U) {
    return {
        {openrc::SceneBlockTaskDmaReferenceKindV1::stcycl_4_4,
         0x20U,
         4U,
         {kStcycl4_4, 0U}},
        {openrc::SceneBlockTaskDmaReferenceKindV1::stmod_normal_unpack_v4_8,
         0x90U,
         2U,
         {kStmodNormal, tail8}},
    };
  }
  return {
      {openrc::SceneBlockTaskDmaReferenceKindV1::stcycl_4_4,
       0x20U,
       3U,
       {kStcycl4_4, 0U}},
      {openrc::SceneBlockTaskDmaReferenceKindV1::stcycl_4_4,
       0x60U,
       3U,
       {kStcycl4_4, 0U}},
      {openrc::SceneBlockTaskDmaReferenceKindV1::stcycl_2_1_unpack_v4_8,
       0x90U,
       2U,
       {kStcycl2_1, tail8}},
  };
}

[[nodiscard]] std::vector<std::byte>
expand_expected(const openrc::SceneBlockDirectoryEntryV1 &entry,
                const std::vector<ExpectedReference> &references) {
  std::vector<std::byte> bytes;
  for (const auto &reference : references) {
    append_le32(bytes, reference.codes[0U]);
    append_le32(bytes, reference.codes[1U]);
    const auto source = openrc::kSceneBlockDirectoryV1Stride + reference.source;
    const auto size = static_cast<std::size_t>(reference.qwc) *
                      openrc::kSceneBlockSectionAlignment;
    bytes.insert(bytes.end(), entry.block_bytes.begin() + source,
                 entry.block_bytes.begin() + source + size);
  }
  return bytes;
}

void test_all_entrypoint_variants_and_provenance() {
  const auto original = make_entry();
  for (const auto entrypoint :
       std::array<std::uint16_t, 6U>{6U, 8U, 10U, 14U, 16U, 20U}) {
    auto entry = original;
    const auto expected = expected_references(entrypoint);
    auto result = openrc::build_scene_block_task_vif_invocation_v1(
        entry, entrypoint, kGenerousLimits);

    expect(result.entrypoint_address == entrypoint,
           "task entrypoint was not preserved");
    expect(result.trailing_flusha_code == 0x13000000U,
           "task FLUSHA code is wrong");
    expect(result.trailing_mscal_code == (0x14000000U | entrypoint),
           "task MSCAL code is wrong");
    expect(result.dma_references.size() == expected.size(),
           "task reference count is wrong");
    expect(result.vif_bytes == expand_expected(entry, expected),
           "task expanded VIF bytes differ from exact REF expansion");

    std::uint64_t output_offset = 0U;
    std::uint64_t total_reference_bytes = 0U;
    for (std::size_t index = 0U; index < expected.size(); ++index) {
      const auto &wanted = expected[index];
      const auto &actual = result.dma_references[index];
      const auto size = static_cast<std::uint64_t>(wanted.qwc) *
                        openrc::kSceneBlockSectionAlignment;
      expect(actual.kind == wanted.kind, "task reference kind is wrong");
      expect(actual.dma_tag_word0 == (0x30000000U | wanted.qwc),
             "task REF tag word is wrong");
      expect(actual.qword_count == wanted.qwc, "task reference QWC is wrong");
      expect(actual.source_remainder_range ==
                 openrc::SceneBlockTaskByteRangeV1{wanted.source, size},
             "task remainder provenance is wrong");
      expect(
          actual.source_block_range ==
              openrc::SceneBlockTaskByteRangeV1{
                  openrc::kSceneBlockDirectoryV1Stride + wanted.source, size},
          "task owned-block provenance is wrong");
      expect(actual.source_input_range ==
                 openrc::SceneBlockTaskByteRangeV1{
                     original.remainder_range.offset + wanted.source, size},
             "task decoded-input provenance is wrong");
      expect(actual.inline_vif_range ==
                 openrc::SceneBlockTaskByteRangeV1{output_offset, 8U},
             "task inline VIF range is wrong");
      expect(actual.expanded_reference_range ==
                 openrc::SceneBlockTaskByteRangeV1{output_offset + 8U, size},
             "task expanded REF range is wrong");
      expect(actual.inline_vif_codes == wanted.codes,
             "task inline VIF codes are wrong");
      output_offset += 8U + size;
      total_reference_bytes += size;
    }
    expect(result.total_reference_bytes == total_reference_bytes,
           "task aggregate REF byte count is wrong");
    expect(result.vif_bytes.size() == output_offset,
           "task expanded stream size is wrong");

    const auto raw_num = entrypoint == 6U ? 4U : 8U;
    expect(result.tail_unpack.raw_num == raw_num,
           "task tail NUM patch is wrong");
    expect(result.tail_unpack.destination_qword == 0x19U,
           "task tail destination patch is wrong");
    expect(result.tail_unpack.raw_code ==
               (kUnpackBase | (raw_num << 16U) | 0x19U),
           "task tail UNPACK patch code is wrong");
    expect(result.tail_unpack.dma_reference_index == expected.size() - 1U,
           "task tail patch provenance is wrong");
    expect(result.tail_unpack.code_range.offset + 4U ==
               result.dma_references.back().expanded_reference_range.offset,
           "task tail patch does not precede its payload");

    const auto owned_result = result.vif_bytes;
    std::fill(entry.block_bytes.begin(), entry.block_bytes.end(),
              std::byte{0xff});
    entry.raw_words.fill(0xffffffffU);
    expect(result.vif_bytes == owned_result,
           "task result retained a dependency on the directory entry");
  }
}

void test_entrypoint_field_and_range_rejections() {
  const auto base = make_entry();
  for (const auto entrypoint :
       std::array<std::uint16_t, 6U>{0U, 2U, 4U, 12U, 18U, 22U}) {
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(
              base, entrypoint, kGenerousLimits);
        },
        "an unsupported task entrypoint was accepted");
  }

  {
    auto entry = base;
    set_descriptor_byte(entry, 0x21U, 0U);
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(
              entry, 6U, kGenerousLimits);
        },
        "a zero-QWC t4 reference was accepted");
  }
  {
    auto entry = base;
    set_descriptor_byte(entry, 0x24U, 0U);
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(
              entry, 6U, kGenerousLimits);
        },
        "a NUM-zero tail was accepted");
  }
  {
    auto entry = base;
    set_descriptor_byte(entry, 0x24U, 6U);
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(
              entry, 6U, kGenerousLimits);
        },
        "a non-qword-divisible NUM was accepted");
  }
  {
    auto entry = base;
    set_descriptor_byte(entry, 0x25U, 12U);
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(
              entry, 8U, kGenerousLimits);
        },
        "a tail reference beyond section 5 was accepted");
  }
  {
    auto entry = base;
    set_descriptor_byte(entry, 0x24U, 40U);
    set_descriptor_byte(entry, 0x2aU, 0xffU);
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(
              entry, 6U, kGenerousLimits);
        },
        "a tail destination beyond the 328-qword bank was accepted");
  }
  {
    auto entry = base;
    set_descriptor_byte(entry, 0x21U, 0xffU);
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(
              entry, 6U, kGenerousLimits);
        },
        "a stored-stream reference beyond the remainder was accepted");
  }
}

void test_metadata_limit_and_vif_rejections() {
  const auto base = make_entry();
  {
    auto entry = base;
    entry.block_bytes.pop_back();
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(
              entry, 6U, kGenerousLimits);
        },
        "an inconsistent owned block size was accepted");
  }
  {
    auto entry = base;
    ++entry.section_layout.ranges[3U].size;
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(
              entry, 6U, kGenerousLimits);
        },
        "inconsistent decoded section metadata was accepted");
  }
  {
    auto limits = kGenerousLimits;
    limits.max_block_bytes = base.block_bytes.size() - 1U;
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(base, 6U,
                                                                 limits);
        },
        "the task block-byte limit was ignored");
  }
  {
    auto limits = kGenerousLimits;
    limits.max_dma_references = 2U;
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(base, 14U,
                                                                 limits);
        },
        "the task reference-count limit was ignored");
  }
  {
    auto limits = kGenerousLimits;
    limits.max_reference_bytes = 127U;
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(base, 14U,
                                                                 limits);
        },
        "the task aggregate REF-byte limit was ignored");
  }
  {
    auto limits = kGenerousLimits;
    limits.vif.max_input_bytes = 151U;
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(base, 14U,
                                                                 limits);
        },
        "the expanded task VIF-byte limit was ignored");
  }
  for (std::size_t field = 0U; field < 6U; ++field) {
    auto limits = kGenerousLimits;
    switch (field) {
    case 0U:
      limits.max_block_bytes = 0U;
      break;
    case 1U:
      limits.max_dma_references = 0U;
      break;
    case 2U:
      limits.max_reference_bytes = 0U;
      break;
    case 3U:
      limits.vif.max_input_bytes = 0U;
      break;
    case 4U:
      limits.vif.max_commands = 0U;
      break;
    case 5U:
      limits.vif.max_payload_bytes = 0U;
      break;
    default:
      break;
    }
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(base, 6U,
                                                                 limits);
        },
        "a zero mandatory task limit was accepted");
  }
  {
    auto entry = base;
    store_le32(entry.block_bytes, openrc::kSceneBlockDirectoryV1Stride,
               0x02000000U);
    const auto before = entry.block_bytes;
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(
              entry, 6U, kGenerousLimits);
        },
        "an unsupported stored VIF opcode was accepted");
    expect(entry.block_bytes == before,
           "failed task validation mutated the input entry");
  }
  {
    auto entry = base;
    store_le32(entry.block_bytes, openrc::kSceneBlockDirectoryV1Stride + 0x5cU,
               0U);
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(
              entry, 8U, kGenerousLimits);
        },
        "t6 was accepted without inherited STCYCL 2/1");
  }
  {
    auto limits = kGenerousLimits;
    limits.vif.max_commands = 1U;
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(base, 6U,
                                                                 limits);
        },
        "the task VIF command limit was ignored");
  }
  {
    auto limits = kGenerousLimits;
    limits.vif.max_payload_bytes = 15U;
    expect_task_error(
        [&] {
          (void)openrc::build_scene_block_task_vif_invocation_v1(base, 6U,
                                                                 limits);
        },
        "the task VIF payload limit was ignored");
  }
}

} // namespace

int main() {
  try {
    test_all_entrypoint_variants_and_provenance();
    test_entrypoint_field_and_range_rejections();
    test_metadata_limit_and_vif_rejections();
    std::cout << "SceneBlock task tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SceneBlock task tests failed: " << error.what() << '\n';
    return 1;
  }
}
