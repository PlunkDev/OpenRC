#pragma once

#include "openrc/scene_block_directory.hpp"
#include "openrc/scene_block_vif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::size_t kSceneBlockTaskMaximumDmaReferences = 3U;
inline constexpr std::uint16_t kSceneBlockTaskInputBankQwords = 328U;
inline constexpr std::uint32_t kSceneBlockTaskVifFlushaCode = 0x13000000U;
inline constexpr std::uint32_t kSceneBlockTaskVifMscalCode = 0x14000000U;

enum class SceneBlockTaskDmaReferenceKindV1 : std::uint8_t {
  // Runtime template t4: STCYCL 4/4, NOP.
  stcycl_4_4 = 0,
  // Runtime template t5: STCYCL 2/1, UNPACK V4_8 unsigned through TOPS.
  stcycl_2_1_unpack_v4_8,
  // Runtime template t6: STMOD normal, UNPACK V4_8 unsigned through TOPS.
  stmod_normal_unpack_v4_8,
};

struct SceneBlockTaskBuildLimitsV1 {
  std::uint64_t max_block_bytes = 0U;
  std::uint64_t max_dma_references = 0U;
  // Aggregate source bytes copied by all emulated DMA REF tags.
  std::uint64_t max_reference_bytes = 0U;
  // Bounds the expanded stream and validates it with the public VIF parser.
  SceneBlockVifLimits vif;
};

struct SceneBlockTaskByteRangeV1 {
  std::uint64_t offset = 0U;
  std::uint64_t size = 0U;

  [[nodiscard]] bool
  operator==(const SceneBlockTaskByteRangeV1 &) const = default;
};

struct SceneBlockTaskDmaReferenceV1 {
  SceneBlockTaskDmaReferenceKindV1 kind =
      SceneBlockTaskDmaReferenceKindV1::stcycl_4_4;

  // Neutral form of the runtime REF tag. The address word is represented by
  // source_block_range instead of retaining an EE pointer.
  std::uint32_t dma_tag_word0 = 0U;
  std::uint16_t qword_count = 0U;

  // Both ranges address bytes owned by SceneBlockDirectoryEntryV1::block_bytes.
  // The remainder-relative form matches the offsets read by the EE task.
  SceneBlockTaskByteRangeV1 source_remainder_range;
  SceneBlockTaskByteRangeV1 source_block_range;
  // Original decoded-input coordinates. This keeps provenance useful after
  // the directory entry which supplied the owned bytes has been destroyed.
  SceneBlockTaskByteRangeV1 source_input_range;

  // Coordinates in SceneBlockTaskVifInvocationV1::vif_bytes. The inline
  // range is the tag-transferred upper 64 bits; the reference range is the
  // byte-for-byte expansion of the REF payload which follows it.
  SceneBlockTaskByteRangeV1 inline_vif_range;
  SceneBlockTaskByteRangeV1 expanded_reference_range;
  std::array<std::uint32_t, 2U> inline_vif_codes{};
};

struct SceneBlockTaskUnpackPatchV1 {
  std::uint8_t raw_num = 0U;
  std::uint8_t destination_qword = 0U;
  std::uint32_t raw_code = 0U;
  std::uint64_t dma_reference_index = 0U;
  SceneBlockTaskByteRangeV1 code_range;
};

struct SceneBlockTaskVifInvocationV1 {
  // VU pair address encoded in the task's trailing MSCAL immediate.
  std::uint16_t entrypoint_address = 0U;
  std::uint32_t trailing_flusha_code = kSceneBlockTaskVifFlushaCode;
  std::uint32_t trailing_mscal_code = kSceneBlockTaskVifMscalCode;

  std::vector<SceneBlockTaskDmaReferenceV1> dma_references;
  std::uint64_t total_reference_bytes = 0U;

  // Exact VIF byte stream obtained by expanding the task's REF tags. It
  // deliberately ends after the final UNPACK payload, immediately before
  // the separate t7 FLUSHA/MSCAL pair.
  std::vector<std::byte> vif_bytes;
  SceneBlockTaskUnpackPatchV1 tail_unpack;
};

class SceneBlockTaskError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Builds one exact, bounded SceneBlock record write stream for overlay 55907.
// The supported entrypoints are 6, 8, 10, 14, 16, and 20. The result owns all
// expanded bytes and provenance and never retains a pointer into entry.
//
// This models DMA tag transfer and ordered REF expansion only. It neither
// executes FLUSHA/MSCAL nor mutates VIF/VU state.
[[nodiscard]] SceneBlockTaskVifInvocationV1
build_scene_block_task_vif_invocation_v1(
    const SceneBlockDirectoryEntryV1 &entry, std::uint16_t entrypoint_address,
    SceneBlockTaskBuildLimitsV1 limits);

} // namespace openrc
