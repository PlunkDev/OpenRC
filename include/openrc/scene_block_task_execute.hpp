#pragma once

#include "openrc/dvp_vu_execute.hpp"
#include "openrc/scene_block_task.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>

namespace openrc {

inline constexpr std::size_t kSceneBlockTaskPreambleQwordCount = 17U;
inline constexpr std::size_t kSceneBlockTaskSeedQwordCount = 15U;
inline constexpr std::uint16_t kSceneBlockTaskSeedFirstQword = 656U;
inline constexpr std::uint16_t kSceneBlockTaskInitializationEntrypoint = 0U;
inline constexpr std::size_t kSceneBlockTaskFrameTransformQwordCount = 4U;
inline constexpr std::uint16_t kSceneBlockTaskFrameTransformFirstQword = 5U;
inline constexpr std::uint16_t kSceneBlockTaskFrameTransformSecondQword =
    kSceneBlockTaskFrameTransformFirstQword +
    kSceneBlockTaskInputBankQwords;

struct SceneBlockTaskPreambleV1 {
  std::array<DvpVuVectorV1, kSceneBlockTaskSeedQwordCount> seed_qwords{};
};

// The EE render caller uploads the same four-qword frame transform to VU RAM
// 5..8 and 333..336 before starting the SceneBlock task. Keeping it explicit
// prevents a standalone invocation from silently inventing camera state.
struct SceneBlockTaskFrameInputV1 {
  std::array<DvpVuVectorV1, kSceneBlockTaskFrameTransformQwordCount>
      transform_qwords{};
};

struct SceneBlockTaskExecutionLimitsV1 {
  SceneBlockTaskBuildLimitsV1 build;
  std::uint64_t max_vif_vector_writes = 0U;
  SceneBlockDvpVuBridgeLimitsV1 bridge;
  DvpVuExecutionLimitsV1 dvp;
};

struct SceneBlockTaskExecutionStateV1 {
  DvpVuExecutionStateV1 vu_state;
  // vif_state.tops_qword must equal BASE + (DBF ? OFFSET : 0).
  SceneBlockVuStateV1 vif_state;
  std::uint16_t base_qword = 0U;
  std::uint16_t offset_qwords = kSceneBlockTaskInputBankQwords;
  bool double_buffer = false;
  std::uint64_t completed_record_invocations = 0U;
};

struct SceneBlockTaskInitializationResultV1 {
  DvpVuExecutionResultV1 vu_execution;
  // Present only after entry 0 reaches its E termination and all pending
  // VU effects have committed.
  std::optional<SceneBlockTaskExecutionStateV1> ready_state;
};

struct SceneBlockTaskRecordExecutionV1 {
  SceneBlockTaskVifInvocationV1 invocation;
  SceneBlockVuSnapshotV1 vif_execution;
  DvpVuExecutionResultV1 vu_execution;
  std::uint16_t top_qword = 0U;
  std::uint16_t next_tops_qword = 0U;
  // A diagnostic stop is intentionally not exposed as resumable task state.
  // Only normal E termination can be carried into the next MSCAL.
  std::optional<SceneBlockTaskExecutionStateV1> ready_state;
};

class SceneBlockTaskExecutionError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Validates the exact 17-qword task preamble framing and copies the 15
// user-executable-owned V4_32 seed qwords. Input is the expanded CNT payload:
// one DMA/VIF tag qword, 15 data qwords, and one trailing control qword.
[[nodiscard]] SceneBlockTaskPreambleV1
parse_scene_block_task_preamble_v1(std::span<const std::byte> bytes);

// Seeds VU RAM 656..670 and executes MSCAL 0 through normal E termination.
// Registers and all other RAM begin indeterminate except architectural
// VI0/VF0 and the MSCAL-latched XTOP=0, matching a freshly uploaded task
// overlay after the preamble's initial VIF control words.
[[nodiscard]] SceneBlockTaskInitializationResultV1
initialize_scene_block_task_execution_v1(
    const DvpVuProgramV1 &program, const SceneBlockTaskPreambleV1 &preamble,
    DvpVuExecutionLimitsV1 limits);

// Exact frame-aware form. The transform is installed in both double-buffered
// input banks before MSCAL 0, matching the two V4_32 UNPACKs emitted by the EE
// caller. Unknown lane masks remain unknown rather than being zero-filled.
[[nodiscard]] SceneBlockTaskInitializationResultV1
initialize_scene_block_task_execution_v1(
    const DvpVuProgramV1 &program, const SceneBlockTaskPreambleV1 &preamble,
    const SceneBlockTaskFrameInputV1 &frame_input,
    DvpVuExecutionLimitsV1 limits);

// Reconstructs one exact record write stream, applies it to carried VU RAM,
// supplies the current double-buffered TOP to XTOP, and runs the selected
// MSCAL until E. The input state is never modified.
[[nodiscard]] SceneBlockTaskRecordExecutionV1
execute_scene_block_task_record_v1(const SceneBlockDirectoryEntryV1 &entry,
                                   std::uint16_t entrypoint_address,
                                   const DvpVuProgramV1 &program,
                                   const SceneBlockTaskExecutionStateV1 &state,
                                   SceneBlockTaskExecutionLimitsV1 limits);

} // namespace openrc
