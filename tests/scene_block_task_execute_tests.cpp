#include "openrc/scene_block_task_execute.hpp"

#include "openrc/dvp_vu.hpp"
#include "openrc/elf.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kUpperNop = 0x000002ffU;
constexpr std::uint32_t kUpperEnd = 0x40000000U;
constexpr std::uint32_t kLowerNop = 0x8000033cU;

constexpr std::array<std::uint32_t, openrc::kSceneBlockSectionBoundaryCount>
    kBoundaries{0x00U, 0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U, 0x70U, 0x80U};

constexpr openrc::SceneBlockTaskBuildLimitsV1 kBuildLimits{
    1U << 20U,
    openrc::kSceneBlockTaskMaximumDmaReferences,
    1U << 20U,
    openrc::SceneBlockVifLimits{1U << 20U, 4096U, 1U << 20U},
};

constexpr openrc::SceneBlockTaskExecutionLimitsV1 kExecutionLimits{
    kBuildLimits,
    4096U,
    openrc::SceneBlockDvpVuBridgeLimitsV1{4096U},
    openrc::DvpVuExecutionLimitsV1{64U, 8U, 16U, 4096U},
};

void expect(const bool condition, const char *const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callable>
void expect_task_execution_error(Callable &&callable,
                                 const char *const message) {
  try {
    callable();
  } catch (const openrc::SceneBlockTaskExecutionError &) {
    return;
  }
  throw std::runtime_error(message);
}

void write_le32(std::vector<std::byte> &bytes, const std::size_t offset,
                const std::uint32_t value) {
  bytes.at(offset + 0U) = static_cast<std::byte>(value & 0xffU);
  bytes.at(offset + 1U) = static_cast<std::byte>((value >> 8U) & 0xffU);
  bytes.at(offset + 2U) = static_cast<std::byte>((value >> 16U) & 0xffU);
  bytes.at(offset + 3U) = static_cast<std::byte>((value >> 24U) & 0xffU);
}

[[nodiscard]] constexpr std::uint32_t
lower_fields(const std::uint8_t destination_mask, const std::uint8_t it,
             const std::uint8_t is, const std::uint8_t id = 0U) {
  return (static_cast<std::uint32_t>(destination_mask & 0x0fU) << 21U) |
         (static_cast<std::uint32_t>(it & 0x1fU) << 16U) |
         (static_cast<std::uint32_t>(is & 0x1fU) << 11U) |
         (static_cast<std::uint32_t>(id & 0x1fU) << 6U);
}

[[nodiscard]] openrc::DvpVuWordV1 known_word(const std::uint32_t bits) {
  return openrc::DvpVuWordV1{bits, 0xffffffffU};
}

[[nodiscard]] openrc::DvpVuVectorV1
known_vector(const std::array<std::uint32_t, 4U> &lanes) {
  openrc::DvpVuVectorV1 result;
  for (std::size_t lane = 0U; lane < lanes.size(); ++lane) {
    result.lanes[lane] = known_word(lanes[lane]);
  }
  return result;
}

void expect_known_u16(const openrc::DvpVuWordV1 &word,
                      const std::uint16_t expected, const char *const message) {
  expect((word.known_mask & 0xffffU) == 0xffffU, message);
  expect(static_cast<std::uint16_t>(word.bits) == expected, message);
}

[[nodiscard]] std::vector<std::byte> make_preamble_bytes() {
  constexpr std::size_t kQwordBytes = 16U;
  std::vector<std::byte> bytes(openrc::kSceneBlockTaskPreambleQwordCount *
                               kQwordBytes);

  write_le32(bytes, 0U, 0x10000010U);
  write_le32(bytes, 4U, 0x00000000U);
  write_le32(bytes, 8U, 0x01000404U);
  write_le32(bytes, 12U, 0x6c0f0290U);

  for (std::size_t qword = 0U; qword < openrc::kSceneBlockTaskSeedQwordCount;
       ++qword) {
    for (std::size_t lane = 0U; lane < 4U; ++lane) {
      write_le32(bytes, (qword + 1U) * kQwordBytes + lane * 4U,
                 0x10000000U + static_cast<std::uint32_t>(qword * 4U + lane));
    }
  }

  constexpr std::size_t kTrailingOffset =
      (1U + openrc::kSceneBlockTaskSeedQwordCount) * kQwordBytes;
  write_le32(bytes, kTrailingOffset + 0U, 0x00000000U);
  write_le32(bytes, kTrailingOffset + 4U, 0x14000000U);
  write_le32(bytes, kTrailingOffset + 8U, 0x03000000U);
  write_le32(bytes, kTrailingOffset + 12U, 0x02000148U);
  return bytes;
}

[[nodiscard]] openrc::SceneBlockDirectoryEntryV1 make_entry() {
  constexpr std::uint32_t kBlockOffset = 0x1000U;
  constexpr std::uint32_t kOpaqueSize = kBoundaries.back();

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
  entry.raw_words[8U] = 0x01010101U;
  entry.raw_words[9U] = 0x00040404U;
  entry.raw_words[10U] = 0x00050000U;
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
        kBoundaries[index + 1U] - kBoundaries[index],
    };
  }

  entry.block_bytes.resize(static_cast<std::size_t>(owned_size));
  const auto tail_offset =
      openrc::kSceneBlockDirectoryV1Stride + kBoundaries[5U];
  for (std::size_t byte = 0U; byte < openrc::kSceneBlockSectionAlignment;
       ++byte) {
    entry.block_bytes[tail_offset + byte] = static_cast<std::byte>(byte + 1U);
  }
  return entry;
}

[[nodiscard]] openrc::DvpVuProgramV1 make_task_program() {
  constexpr std::size_t kPairCount = 11U;
  std::array<std::uint32_t, kPairCount> lower{};
  std::array<std::uint32_t, kPairCount> upper{};
  lower.fill(kLowerNop);
  upper.fill(kUpperNop);
  lower[0U] =
      lower_fields(0x0fU, 1U, 0U) | openrc::kSceneBlockTaskSeedFirstQword;
  lower[1U] = 0x800006bcU | lower_fields(0U, 3U, 0U);
  upper[1U] |= kUpperEnd;
  lower[6U] = 0x800006bcU | lower_fields(0U, 1U, 0U);
  lower[7U] = lower_fields(0x0fU, 2U, 1U) | 5U;
  lower[8U] = 0x02000000U | lower_fields(0x0fU, 0U, 2U) | 900U;
  upper[9U] |= kUpperEnd;

  std::vector<std::byte> bytes(kPairCount * openrc::kDvpVuInstructionBytes);
  for (std::size_t pair = 0U; pair < kPairCount; ++pair) {
    write_le32(bytes, pair * 8U, lower[pair]);
    write_le32(bytes, pair * 8U + 4U, upper[pair]);
  }

  openrc::ElfDvpOverlay overlay{};
  overlay.overlay_section_index = 1U;
  overlay.code_section_index = 7U;
  overlay.virtual_memory_address = 0U;
  overlay.code_file_offset = 0U;
  overlay.size = static_cast<std::uint32_t>(bytes.size());
  const std::array overlays{overlay};
  constexpr std::array<std::uint16_t, 2U> entrypoints{0U, 6U};
  constexpr openrc::DvpVuLimits limits{1U << 20U, 8U, 0x4000U, 8U, 128U, 256U};
  return openrc::decode_dvp_vu_program_v1(
      std::span<const std::byte>{bytes},
      std::span<const openrc::ElfDvpOverlay>{overlays},
      std::span<const std::uint16_t>{entrypoints}, limits);
}

void expect_tail_vector(const openrc::DvpVuExecutionStateV1 &state,
                        const std::uint16_t address,
                        const std::uint32_t first_value,
                        const char *const message) {
  expect(state.data_memory[address] ==
             known_vector({first_value, first_value + 1U, first_value + 2U,
                           first_value + 3U}),
         message);
}

void test_exact_preamble_and_initialization() {
  const auto bytes = make_preamble_bytes();
  const auto preamble = openrc::parse_scene_block_task_preamble_v1(bytes);
  expect(preamble.seed_qwords.front() ==
             known_vector({0x10000000U, 0x10000001U, 0x10000002U, 0x10000003U}),
         "the first task seed qword is wrong");
  expect(preamble.seed_qwords.back() ==
             known_vector({0x10000038U, 0x10000039U, 0x1000003aU, 0x1000003bU}),
         "the final task seed qword is wrong");

  const auto program = make_task_program();
  const auto initialized = openrc::initialize_scene_block_task_execution_v1(
      program, preamble, kExecutionLimits.dvp);
  expect(initialized.vu_execution.termination ==
             openrc::DvpVuTerminationV1::program_end,
         "task entry 0 did not terminate normally");
  expect(initialized.vu_execution.executed_instruction_pairs == 3U &&
             initialized.vu_execution.instruction_trace ==
                 std::vector<std::uint16_t>({0U, 1U, 2U}),
         "task entry 0 did not execute its E delay pair");
  expect(initialized.ready_state.has_value(),
         "normal task initialization did not produce carried state");

  const auto &state = *initialized.ready_state;
  expect(state.vu_state.xtop_qword == known_word(0U),
         "task entry 0 did not receive the initial VIF TOP through XTOP");
  expect_known_u16(state.vu_state.vi[3U], 0U,
                   "task entry 0 observed an indeterminate initial XTOP");
  expect(state.vu_state.vf[1U] == preamble.seed_qwords.front(),
         "entry 0 did not observe the preloaded seed memory");
  for (std::size_t index = 0U; index < preamble.seed_qwords.size(); ++index) {
    expect(state.vu_state.data_memory[openrc::kSceneBlockTaskSeedFirstQword +
                                      index] == preamble.seed_qwords[index],
           "task initialization did not preserve a seed qword");
  }
  expect(state.base_qword == 0U &&
             state.offset_qwords == openrc::kSceneBlockTaskInputBankQwords &&
             !state.double_buffer && state.completed_record_invocations == 0U,
         "task initialization control state is wrong");
  expect(state.vif_state.tops_qword == 0U &&
             state.vif_state.cycle_length == 4U &&
             state.vif_state.write_length == 4U,
         "task initialization VIF state is wrong");
}

void test_frame_transform_is_uploaded_to_both_input_banks() {
  const auto preamble =
      openrc::parse_scene_block_task_preamble_v1(make_preamble_bytes());
  const auto program = make_task_program();
  openrc::SceneBlockTaskFrameInputV1 frame_input;
  for (std::size_t index = 0U; index < frame_input.transform_qwords.size();
       ++index) {
    const auto first = static_cast<std::uint32_t>(0x20000000U + index * 4U);
    frame_input.transform_qwords[index] =
        known_vector({first, first + 1U, first + 2U, first + 3U});
  }

  const auto initialized = openrc::initialize_scene_block_task_execution_v1(
      program, preamble, frame_input, kExecutionLimits.dvp);
  expect(initialized.ready_state.has_value(),
         "frame-aware task initialization did not produce carried state");
  for (std::size_t index = 0U; index < frame_input.transform_qwords.size();
       ++index) {
    expect(initialized.ready_state->vu_state.data_memory
               [openrc::kSceneBlockTaskFrameTransformFirstQword + index] ==
               frame_input.transform_qwords[index],
           "the first input bank did not receive the frame transform");
    expect(initialized.ready_state->vu_state.data_memory
               [openrc::kSceneBlockTaskFrameTransformSecondQword + index] ==
               frame_input.transform_qwords[index],
           "the second input bank did not receive the frame transform");
  }
}

void test_two_record_invocations_toggle_banks_and_carry_state() {
  const auto preamble =
      openrc::parse_scene_block_task_preamble_v1(make_preamble_bytes());
  const auto program = make_task_program();
  const auto initialized = openrc::initialize_scene_block_task_execution_v1(
      program, preamble, kExecutionLimits.dvp);
  expect(initialized.ready_state.has_value(),
         "task execution fixture did not initialize");
  const auto entry = make_entry();
  const auto initial_state = *initialized.ready_state;

  const auto first = openrc::execute_scene_block_task_record_v1(
      entry, 6U, program, initial_state, kExecutionLimits);
  expect(first.invocation.dma_references.size() == 2U &&
             first.invocation.total_reference_bytes == 32U &&
             first.invocation.vif_bytes.size() == 48U,
         "entry 6 exact REF expansion is wrong");
  expect(first.invocation.tail_unpack.raw_num == 4U &&
             first.invocation.tail_unpack.destination_qword == 5U &&
             first.invocation.tail_unpack.raw_code == 0x6e04c005U &&
             first.invocation.tail_unpack.dma_reference_index == 1U &&
             first.invocation.tail_unpack.code_range ==
                 openrc::SceneBlockTaskByteRangeV1{28U, 4U},
         "entry 6 exact tail UNPACK is wrong");
  expect(first.vif_execution.initial_state.tops_qword == 0U &&
             first.vif_execution.final_state.cycle_length == 2U &&
             first.vif_execution.final_state.write_length == 1U,
         "entry 6 VIF state is wrong");
  expect(first.top_qword == 0U &&
             first.next_tops_qword == openrc::kSceneBlockTaskInputBankQwords,
         "the first task invocation selected the wrong VIF bank");
  expect(first.invocation.trailing_flusha_code == 0x13000000U &&
             first.invocation.trailing_mscal_code == 0x14000006U,
         "the task invocation control tail is wrong");
  expect(first.vif_execution.writes.size() == 4U,
         "the first task invocation produced the wrong VIF write count");
  constexpr std::array<std::uint16_t, 4U> kFirstDestinations{5U, 7U, 9U, 11U};
  for (std::size_t index = 0U; index < kFirstDestinations.size(); ++index) {
    expect(first.vif_execution.writes[index].destination_qword ==
               kFirstDestinations[index],
           "STCYCL 2/1 did not skip the expected first-bank qword");
  }
  expect(first.vu_execution.termination ==
                 openrc::DvpVuTerminationV1::program_end &&
             first.vu_execution.executed_instruction_pairs == 5U &&
             first.vu_execution.instruction_trace ==
                 std::vector<std::uint16_t>({6U, 7U, 8U, 9U, 10U}),
         "the first record microprogram did not terminate normally");
  expect_known_u16(first.vu_execution.final_state.vi[1U], 0U,
                   "XTOP did not expose the first bank to entry 6");
  expect(first.ready_state.has_value(),
         "the first normal record did not produce carried state");

  const auto &first_state = *first.ready_state;
  expect(first_state.double_buffer &&
             first_state.completed_record_invocations == 1U &&
             first_state.vif_state.tops_qword ==
                 openrc::kSceneBlockTaskInputBankQwords,
         "the first record did not arm the second input bank");
  expect_tail_vector(first_state.vu_state, 5U, 1U,
                     "first-bank VIF vector 0 was not replayed");
  expect_tail_vector(first_state.vu_state, 7U, 5U,
                     "first-bank VIF vector 1 was not replayed");
  expect_tail_vector(first_state.vu_state, 9U, 9U,
                     "first-bank VIF vector 2 was not replayed");
  expect_tail_vector(first_state.vu_state, 11U, 13U,
                     "first-bank VIF vector 3 was not replayed");
  expect(first_state.vu_state.vf[2U] == known_vector({1U, 2U, 3U, 4U}) &&
             first_state.vu_state.data_memory[900U] ==
                 known_vector({1U, 2U, 3U, 4U}),
         "entry 6 did not observe and store the first-bank VIF write");
  expect(initial_state.vu_state.data_memory[5U] == openrc::DvpVuVectorV1{},
         "record execution mutated its const input state");

  const auto second = openrc::execute_scene_block_task_record_v1(
      entry, 6U, program, first_state, kExecutionLimits);
  expect(second.top_qword == openrc::kSceneBlockTaskInputBankQwords &&
             second.next_tops_qword == 0U,
         "the second task invocation selected the wrong VIF bank");
  expect_known_u16(second.vu_execution.final_state.vi[1U],
                   openrc::kSceneBlockTaskInputBankQwords,
                   "XTOP did not expose the second bank to entry 6");
  expect(second.ready_state.has_value(),
         "the second normal record did not produce carried state");

  const auto &second_state = *second.ready_state;
  expect(!second_state.double_buffer &&
             second_state.completed_record_invocations == 2U &&
             second_state.vif_state.tops_qword == 0U,
         "the second record did not wrap back to the first input bank");
  expect_tail_vector(second_state.vu_state, 333U, 1U,
                     "second-bank VIF vector 0 was not replayed");
  expect_tail_vector(second_state.vu_state, 335U, 5U,
                     "second-bank VIF vector 1 was not replayed");
  expect_tail_vector(second_state.vu_state, 337U, 9U,
                     "second-bank VIF vector 2 was not replayed");
  expect_tail_vector(second_state.vu_state, 339U, 13U,
                     "second-bank VIF vector 3 was not replayed");
  expect_tail_vector(second_state.vu_state, 5U, 1U,
                     "the second invocation destroyed the first bank");
  expect(second_state.vu_state.vf[2U] == known_vector({1U, 2U, 3U, 4U}) &&
             second_state.vu_state.data_memory[900U] ==
                 known_vector({1U, 2U, 3U, 4U}),
         "entry 6 did not observe and store the second-bank VIF write");
  expect(second_state.vu_state
                 .data_memory[openrc::kSceneBlockTaskSeedFirstQword] ==
             preamble.seed_qwords.front(),
         "record execution destroyed preamble seed memory");
}

void test_framing_and_diagnostic_stops_are_not_resumable() {
  const auto good_bytes = make_preamble_bytes();
  {
    auto short_bytes = good_bytes;
    short_bytes.pop_back();
    expect_task_execution_error(
        [&short_bytes] {
          (void)openrc::parse_scene_block_task_preamble_v1(short_bytes);
        },
        "a short task preamble was accepted");
  }
  {
    auto malformed = good_bytes;
    write_le32(malformed, 8U, 0x01000101U);
    expect_task_execution_error(
        [&malformed] {
          (void)openrc::parse_scene_block_task_preamble_v1(malformed);
        },
        "a malformed task preamble control word was accepted");
  }

  const auto preamble = openrc::parse_scene_block_task_preamble_v1(good_bytes);
  const auto program = make_task_program();
  auto short_dvp_limits = kExecutionLimits.dvp;
  short_dvp_limits.max_instruction_pairs = 1U;
  const auto stopped_initialization =
      openrc::initialize_scene_block_task_execution_v1(program, preamble,
                                                       short_dvp_limits);
  expect(stopped_initialization.vu_execution.termination ==
                 openrc::DvpVuTerminationV1::instruction_limit &&
             !stopped_initialization.ready_state.has_value(),
         "an initialization diagnostic stop was exposed as resumable");

  const auto initialized = openrc::initialize_scene_block_task_execution_v1(
      program, preamble, kExecutionLimits.dvp);
  expect(initialized.ready_state.has_value(),
         "diagnostic-stop fixture did not initialize normally");
  auto short_record_limits = kExecutionLimits;
  short_record_limits.dvp.max_instruction_pairs = 1U;
  const auto stopped_record = openrc::execute_scene_block_task_record_v1(
      make_entry(), 6U, program, *initialized.ready_state, short_record_limits);
  expect(stopped_record.vu_execution.termination ==
                 openrc::DvpVuTerminationV1::instruction_limit &&
             !stopped_record.ready_state.has_value(),
         "a record diagnostic stop was exposed as resumable");

  auto zero_write_limits = kExecutionLimits;
  zero_write_limits.max_vif_vector_writes = 0U;
  expect_task_execution_error(
      [&] {
        (void)openrc::execute_scene_block_task_record_v1(
            make_entry(), 6U, program, *initialized.ready_state,
            zero_write_limits);
      },
      "a zero task VIF write limit was accepted");

  auto inconsistent_state = *initialized.ready_state;
  inconsistent_state.vif_state.tops_qword = 1U;
  expect_task_execution_error(
      [&] {
        (void)openrc::execute_scene_block_task_record_v1(
            make_entry(), 6U, program, inconsistent_state, kExecutionLimits);
      },
      "inconsistent carried TOPS and DBF state was accepted");
}

} // namespace

int main() {
  try {
    test_exact_preamble_and_initialization();
    test_frame_transform_is_uploaded_to_both_input_banks();
    test_two_record_invocations_toggle_banks_and_carry_state();
    test_framing_and_diagnostic_stops_are_not_resumable();
    std::cout << "SceneBlock task execution tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SceneBlock task execution tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
