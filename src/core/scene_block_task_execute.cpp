#include "openrc/scene_block_task_execute.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace openrc {
namespace {

constexpr std::size_t kQwordBytes = 16U;
constexpr std::size_t kPreambleBytes =
    kSceneBlockTaskPreambleQwordCount * kQwordBytes;
constexpr std::uint32_t kKnown = 0xffffffffU;

[[noreturn]] void fail(const std::string &message) {
  throw SceneBlockTaskExecutionError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_le32(const std::span<const std::byte> bytes,
                                      const std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

void require_word(const std::span<const std::byte> bytes,
                  const std::size_t offset, const std::uint32_t expected,
                  const char *const description) {
  if (read_le32(bytes, offset) != expected) {
    fail(std::string("SceneBlock task preamble has an invalid ") + description);
  }
}

[[nodiscard]] std::uint16_t
tops_qword(const SceneBlockTaskExecutionStateV1 &state,
           const bool double_buffer) {
  if (state.base_qword >= kDvpVuDataMemoryQwordCount ||
      state.offset_qwords >= kDvpVuDataMemoryQwordCount) {
    fail("SceneBlock task BASE/OFFSET lies outside VU1 memory");
  }
  const auto result =
      static_cast<std::uint32_t>(state.base_qword) +
      (double_buffer ? static_cast<std::uint32_t>(state.offset_qwords) : 0U);
  if (result >= kDvpVuDataMemoryQwordCount) {
    fail("SceneBlock task TOPS lies outside VU1 memory");
  }
  return static_cast<std::uint16_t>(result);
}

[[nodiscard]] DvpVuWordV1 known_word(const std::uint16_t value) noexcept {
  return DvpVuWordV1{value, kKnown};
}

} // namespace

SceneBlockTaskPreambleV1
parse_scene_block_task_preamble_v1(const std::span<const std::byte> bytes) {
  if (bytes.size() != kPreambleBytes) {
    fail("SceneBlock task preamble must contain exactly 17 qwords");
  }

  require_word(bytes, 0U, 0x10000010U, "CNT tag");
  require_word(bytes, 4U, 0x00000000U, "CNT address word");
  require_word(bytes, 8U, 0x01000404U, "STCYCL 4/4 code");
  require_word(bytes, 12U, 0x6c0f0290U, "seed UNPACK code");

  constexpr std::size_t kTrailingOffset =
      (1U + kSceneBlockTaskSeedQwordCount) * kQwordBytes;
  require_word(bytes, kTrailingOffset + 0U, 0x00000000U, "trailing NOP");
  require_word(bytes, kTrailingOffset + 4U, 0x14000000U, "initial MSCAL code");
  require_word(bytes, kTrailingOffset + 8U, 0x03000000U, "BASE code");
  require_word(bytes, kTrailingOffset + 12U, 0x02000148U, "OFFSET code");

  SceneBlockTaskPreambleV1 result;
  for (std::size_t qword = 0U; qword < result.seed_qwords.size(); ++qword) {
    const auto qword_offset = (qword + 1U) * kQwordBytes;
    for (std::size_t lane = 0U; lane < result.seed_qwords[qword].lanes.size();
         ++lane) {
      result.seed_qwords[qword].lanes[lane] = DvpVuWordV1{
          read_le32(bytes, qword_offset + lane * sizeof(std::uint32_t)),
          kKnown,
      };
    }
  }
  return result;
}

SceneBlockTaskInitializationResultV1 initialize_scene_block_task_execution_v1(
    const DvpVuProgramV1 &program, const SceneBlockTaskPreambleV1 &preamble,
    const DvpVuExecutionLimitsV1 limits) {
  return initialize_scene_block_task_execution_v1(
      program, preamble, SceneBlockTaskFrameInputV1{}, limits);
}

SceneBlockTaskInitializationResultV1 initialize_scene_block_task_execution_v1(
    const DvpVuProgramV1 &program, const SceneBlockTaskPreambleV1 &preamble,
    const SceneBlockTaskFrameInputV1 &frame_input,
    const DvpVuExecutionLimitsV1 limits) {
  static_assert(kSceneBlockTaskSeedFirstQword + kSceneBlockTaskSeedQwordCount <=
                kDvpVuDataMemoryQwordCount);
  static_assert(kSceneBlockTaskFrameTransformFirstQword +
                    kSceneBlockTaskFrameTransformQwordCount <=
                kDvpVuDataMemoryQwordCount);
  static_assert(kSceneBlockTaskFrameTransformSecondQword +
                    kSceneBlockTaskFrameTransformQwordCount <=
                kDvpVuDataMemoryQwordCount);

  auto initial_state = make_dvp_vu_execution_state_v1();
  // The preamble's MSCAL 0 observes VIF1 TOP=0. XTOP is latched by MSCAL,
  // so entry 0 must not begin with an accidentally indeterminate value.
  initial_state.xtop_qword = known_word(0U);
  for (std::size_t index = 0U; index < preamble.seed_qwords.size(); ++index) {
    initial_state.data_memory[kSceneBlockTaskSeedFirstQword + index] =
        preamble.seed_qwords[index];
  }
  for (std::size_t index = 0U; index < frame_input.transform_qwords.size();
       ++index) {
    initial_state.data_memory[kSceneBlockTaskFrameTransformFirstQword + index] =
        frame_input.transform_qwords[index];
    initial_state.data_memory[kSceneBlockTaskFrameTransformSecondQword + index] =
        frame_input.transform_qwords[index];
  }

  SceneBlockTaskInitializationResultV1 result;
  result.vu_execution =
      execute_dvp_vu_program_v1(program, std::move(initial_state),
                                DvpVuExecutionOptionsV1{
                                    kSceneBlockTaskInitializationEntrypoint,
                                    false,
                                },
                                limits);
  if (result.vu_execution.termination != DvpVuTerminationV1::program_end) {
    return result;
  }

  SceneBlockTaskExecutionStateV1 ready;
  ready.vu_state = result.vu_execution.final_state;
  ready.vif_state.tops_qword = 0U;
  ready.vif_state.cycle_length = 4U;
  ready.vif_state.write_length = 4U;
  ready.vif_state.addition_mode = SceneBlockVuAdditionMode::normal;
  ready.base_qword = 0U;
  ready.offset_qwords = kSceneBlockTaskInputBankQwords;
  ready.double_buffer = false;
  ready.completed_record_invocations = 0U;
  result.ready_state = std::move(ready);
  return result;
}

SceneBlockTaskRecordExecutionV1 execute_scene_block_task_record_v1(
    const SceneBlockDirectoryEntryV1 &entry,
    const std::uint16_t entrypoint_address, const DvpVuProgramV1 &program,
    const SceneBlockTaskExecutionStateV1 &state,
    const SceneBlockTaskExecutionLimitsV1 limits) {
  if (limits.max_vif_vector_writes == 0U) {
    fail("SceneBlock task VIF write limit must be non-zero");
  }

  SceneBlockTaskRecordExecutionV1 result;
  result.top_qword = tops_qword(state, state.double_buffer);
  result.next_tops_qword = tops_qword(state, !state.double_buffer);
  if (state.vif_state.tops_qword != result.top_qword) {
    fail("SceneBlock task carried TOPS disagrees with BASE/OFFSET/DBF state");
  }
  result.invocation = build_scene_block_task_vif_invocation_v1(
      entry, entrypoint_address, limits.build);

  auto vif_initial_state = state.vif_state;
  vif_initial_state.tops_qword = result.top_qword;
  result.vif_execution = execute_scene_block_vu_from_state_v1(
      std::span<const std::byte>{result.invocation.vif_bytes},
      vif_initial_state,
      SceneBlockVuLimits{
          limits.build.vif,
          limits.max_vif_vector_writes,
      });

  auto vu_initial_state = state.vu_state;
  apply_scene_block_dvp_vu_writes_v1(
      vu_initial_state, result.vif_execution, 0U,
      static_cast<std::uint64_t>(result.vif_execution.writes.size()),
      limits.bridge);
  vu_initial_state.xtop_qword = known_word(result.top_qword);

  result.vu_execution = execute_dvp_vu_program_v1(
      program, std::move(vu_initial_state),
      DvpVuExecutionOptionsV1{entrypoint_address, false}, limits.dvp);
  if (result.vu_execution.termination != DvpVuTerminationV1::program_end) {
    return result;
  }
  if (state.completed_record_invocations ==
      std::numeric_limits<std::uint64_t>::max()) {
    fail("SceneBlock task completed-invocation count overflows");
  }

  SceneBlockTaskExecutionStateV1 ready;
  ready.vu_state = result.vu_execution.final_state;
  ready.vif_state = result.vif_execution.final_state;
  ready.vif_state.tops_qword = result.next_tops_qword;
  ready.base_qword = state.base_qword;
  ready.offset_qwords = state.offset_qwords;
  ready.double_buffer = !state.double_buffer;
  ready.completed_record_invocations = state.completed_record_invocations + 1U;
  result.ready_state = std::move(ready);
  return result;
}

} // namespace openrc
