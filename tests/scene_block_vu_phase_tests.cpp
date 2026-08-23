#include "openrc/scene_block_vu_phase.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr openrc::SceneBlockVuLimits kGenerousLimits{
    openrc::SceneBlockVifLimits{
        1024ULL * 1024ULL,
        4096U,
        1024ULL * 1024ULL,
    },
    65536U,
};

void expect(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void expect_phase_error(Callable&& callable, const char* message) {
    try {
        callable();
    } catch (const openrc::SceneBlockVuPhaseError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] constexpr std::uint32_t
make_code(const std::uint16_t immediate, const std::uint8_t raw_num,
          const openrc::SceneBlockVifOpcode opcode) noexcept {
    return static_cast<std::uint32_t>(immediate) |
           (static_cast<std::uint32_t>(raw_num) << 16U) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(opcode))
            << 24U);
}

void append_le16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void append_le32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

void pad_to_vifcode(std::vector<std::byte>& bytes) {
    while ((bytes.size() % openrc::kSceneBlockVifPayloadAlignment) != 0U) {
        bytes.push_back(std::byte{0});
    }
}

void append_nop(std::vector<std::byte>& bytes) {
    append_le32(bytes, make_code(0U, 0U, openrc::SceneBlockVifOpcode::nop));
}

void append_stcycl(std::vector<std::byte>& bytes,
                   const std::uint8_t cycle_length,
                   const std::uint8_t write_length) {
    const auto immediate = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(cycle_length) |
        (static_cast<std::uint16_t>(write_length) << 8U));
    append_le32(bytes,
                make_code(immediate, 0U, openrc::SceneBlockVifOpcode::stcycl));
}

void append_stmod(std::vector<std::byte>& bytes,
                  const openrc::SceneBlockVuAdditionMode mode) {
    append_le32(bytes, make_code(static_cast<std::uint16_t>(mode), 0U,
                                 openrc::SceneBlockVifOpcode::stmod));
}

void append_strow(std::vector<std::byte>& bytes,
                  const std::array<std::uint32_t, 4U> row) {
    append_le32(bytes, make_code(0U, 0U, openrc::SceneBlockVifOpcode::strow));
    for (const auto value : row) {
        append_le32(bytes, value);
    }
}

// The narrow scalar parameters mirror the VIFcode fields at call sites.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
void append_unpack_code(std::vector<std::byte>& bytes,
                        const openrc::SceneBlockVifOpcode opcode,
                        const std::uint16_t address, const std::uint8_t raw_num,
                        const bool unsigned_data = false,
                        const bool use_tops = false) {
    std::uint16_t immediate = address;
    if (unsigned_data) {
        immediate = static_cast<std::uint16_t>(immediate | 0x4000U);
    }
    if (use_tops) {
        immediate = static_cast<std::uint16_t>(immediate | 0x8000U);
    }
    append_le32(bytes, make_code(immediate, raw_num, opcode));
}
// NOLINTEND(bugprone-easily-swappable-parameters)

void append_payload8(std::vector<std::byte>& bytes,
                     const std::initializer_list<std::uint8_t> values) {
    for (const auto value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    pad_to_vifcode(bytes);
}

void append_payload16(std::vector<std::byte>& bytes,
                      const std::initializer_list<std::uint16_t> values) {
    for (const auto value : values) {
        append_le16(bytes, value);
    }
    pad_to_vifcode(bytes);
}

[[nodiscard]] openrc::SceneBlockVuSnapshotV1
execute(const std::vector<std::byte>& bytes, const std::uint16_t tops = 0U) {
    return openrc::execute_scene_block_vu_v1(
        bytes, openrc::SceneBlockVuExecutionOptionsV1{tops}, kGenerousLimits);
}

[[nodiscard]] bool value_equal(const openrc::SceneBlockVuValueV1 left,
                               const openrc::SceneBlockVuValueV1 right) {
    return left.state == right.state &&
           (left.state == openrc::SceneBlockVuValueState::indeterminate ||
            left.bits == right.bits);
}

void expect_state_equal(const openrc::SceneBlockVuStateV1& actual,
                        const openrc::SceneBlockVuStateV1& expected,
                        const char* message) {
    expect(actual.tops_qword == expected.tops_qword &&
               actual.cycle_length == expected.cycle_length &&
               actual.write_length == expected.write_length &&
               actual.addition_mode == expected.addition_mode,
           message);
    for (std::size_t lane = 0U; lane < actual.row.size(); ++lane) {
        expect(value_equal(actual.row[lane], expected.row[lane]), message);
    }
}

void expect_known_row(const openrc::SceneBlockVuStateV1& state,
                      const std::array<std::uint32_t, 4U> expected,
                      const char* message) {
    for (std::size_t lane = 0U; lane < state.row.size(); ++lane) {
        expect(state.row[lane].state == openrc::SceneBlockVuValueState::known &&
                   state.row[lane].bits == expected[lane],
               message);
    }
}

void expect_indeterminate_row(const openrc::SceneBlockVuStateV1& state,
                              const char* message) {
    for (const auto lane : state.row) {
        expect(lane.state == openrc::SceneBlockVuValueState::indeterminate,
               message);
    }
}

void expect_partition(
    const openrc::SceneBlockVuSnapshotV1& snapshot,
    const std::vector<openrc::SceneBlockVuCommandPhaseV1>& phases) {
    std::uint64_t next_command = 0U;
    std::uint64_t next_stream_byte = 0U;
    std::uint64_t next_write = 0U;
    const openrc::SceneBlockVuStateV1* previous_state = nullptr;
    for (const auto& phase : phases) {
        expect(phase.first_command_index == next_command &&
                   phase.stream_range.offset == next_stream_byte &&
                   phase.first_write_index == next_write,
               "phase ranges are not gapless and ordered");
        expect(phase.command_count != 0U, "phase has no commands");
        const auto first_command =
            static_cast<std::size_t>(phase.first_command_index);
        const auto last_command = static_cast<std::size_t>(
            phase.first_command_index + phase.command_count - 1U);
        const auto expected_stream_end =
            snapshot.stream.commands[last_command].packet_range.offset +
            snapshot.stream.commands[last_command].packet_range.size;
        expect(phase.stream_range.offset ==
                       snapshot.stream.commands[first_command]
                           .packet_range.offset &&
                   phase.stream_range.size ==
                       expected_stream_end - phase.stream_range.offset,
               "phase stream range does not match its command packets");
        if (previous_state != nullptr) {
            expect_state_equal(phase.state_before, *previous_state,
                               "adjacent phase states are not continuous");
        }
        expect(phase.control_command_count + phase.unpack_command_count +
                       phase.nop_command_count ==
                   phase.command_count,
               "phase command classes do not cover its command range");
        expect(phase.unique_qword_count + phase.internal_overwrite_count ==
                   phase.write_count,
               "phase write accounting is inconsistent");

        std::uint64_t run_qwords = 0U;
        std::uint64_t previous_end = 0U;
        bool first_run = true;
        for (const auto run : phase.unique_destination_runs) {
            const auto end =
                static_cast<std::uint64_t>(run.first_qword) + run.qword_count;
            expect(run.qword_count != 0U &&
                       end <= openrc::kSceneBlockVuMemoryQwordCount,
                   "phase qword run is empty or outside VU1 memory");
            expect(first_run || static_cast<std::uint64_t>(run.first_qword) >
                                    previous_end,
                   "phase qword runs are not sorted, disjoint, and maximal");
            first_run = false;
            previous_end = end;
            run_qwords += run.qword_count;
        }
        expect(run_qwords == phase.unique_qword_count,
               "phase qword runs do not exactly cover its unique writes");

        next_command += phase.command_count;
        next_stream_byte += phase.stream_range.size;
        next_write += phase.write_count;
        previous_state = &phase.state_after;
    }
    expect(next_command == snapshot.stream.commands.size() &&
               next_stream_byte == snapshot.stream.input_bytes &&
               next_write == snapshot.writes.size(),
           "phase ranges do not exactly partition the snapshot");
    if (previous_state != nullptr) {
        expect_state_equal(*previous_state, snapshot.final_state,
                           "last phase does not end at final snapshot state");
    }
}

void test_nop_ownership_state_and_skip_runs() {
    std::vector<std::byte> bytes;
    append_nop(bytes);                                             // 0
    append_strow(bytes, {10U, 20U, 30U, 40U});                     // 1
    append_nop(bytes);                                             // 2
    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::offset); // 3
    append_stcycl(bytes, 1U, 1U);                                  // 4
    append_nop(bytes);                                             // 5
    append_unpack_code(                                            // 6
        bytes, openrc::SceneBlockVifOpcode::unpack_v4_8, 10U, 2U, true, true);
    append_payload8(bytes, {
                               1U,
                               2U,
                               3U,
                               4U,
                               5U,
                               6U,
                               7U,
                               8U,
                           });
    append_nop(bytes);            // 7
    append_nop(bytes);            // 8
    append_stcycl(bytes, 2U, 1U); // 9
    append_nop(bytes);            // 10
    append_unpack_code(           // 11
        bytes, openrc::SceneBlockVifOpcode::unpack_v3_16, 20U, 3U, true, false);
    append_payload16(bytes, {
                                1U,
                                2U,
                                3U,
                                4U,
                                5U,
                                6U,
                                7U,
                                8U,
                                9U,
                            });
    append_nop(bytes);                                             // 12
    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::normal); // 13
    append_strow(bytes, {1U, 2U, 3U, 4U});                         // 14
    append_nop(bytes);                                             // 15

    auto snapshot = execute(bytes, 5U);
    auto phases = openrc::group_scene_block_vu_phases_v1(snapshot);
    expect(phases.size() == 3U, "NOP fixture has the wrong phase count");
    expect_partition(snapshot, phases);

    const auto& first = phases[0U];
    expect(first.first_command_index == 0U && first.command_count == 9U &&
               first.control_command_count == 3U &&
               first.unpack_command_count == 1U &&
               first.nop_command_count == 5U,
           "leading or pre-boundary NOP ownership is wrong");
    expect(first.first_write_index == 0U && first.write_count == 2U &&
               first.unique_qword_count == 2U &&
               first.internal_overwrite_count == 0U &&
               first.prior_phase_overwrite_count == 0U &&
               first.unique_destination_runs ==
                   std::vector<openrc::SceneBlockVuQwordRunV1>{{15U, 2U}},
           "first phase write coverage is wrong");
    expect(first.state_before.tops_qword == 5U &&
               first.state_before.cycle_length == 1U &&
               first.state_before.write_length == 1U &&
               first.state_before.addition_mode ==
                   openrc::SceneBlockVuAdditionMode::normal,
           "first phase initial state is wrong");
    expect_indeterminate_row(first.state_before,
                             "first phase initial ROW is not indeterminate");
    expect(first.state_after.cycle_length == 1U &&
               first.state_after.write_length == 1U &&
               first.state_after.addition_mode ==
                   openrc::SceneBlockVuAdditionMode::offset,
           "first phase control state is wrong");
    expect_known_row(first.state_after, {10U, 20U, 30U, 40U},
                     "first phase STROW state was not reconstructed");

    const auto& second = phases[1U];
    expect(second.first_command_index == 9U && second.command_count == 4U &&
               second.control_command_count == 1U &&
               second.unpack_command_count == 1U &&
               second.nop_command_count == 2U,
           "second phase command ownership is wrong");
    expect(second.first_write_index == 2U && second.write_count == 3U &&
               second.unique_qword_count == 3U &&
               second.unique_destination_runs ==
                   std::vector<openrc::SceneBlockVuQwordRunV1>{
                       {20U, 1U}, {22U, 1U}, {24U, 1U}},
           "skip-mode phase exposed a false contiguous range");
    expect_state_equal(second.state_before, first.state_after,
                       "second phase did not inherit the first phase state");
    expect(second.state_after.cycle_length == 2U &&
               second.state_after.write_length == 1U &&
               second.state_after.addition_mode ==
                   openrc::SceneBlockVuAdditionMode::offset,
           "skip-mode phase state is wrong");
    expect_known_row(second.state_after, {10U, 20U, 30U, 40U},
                     "MODE 1 unexpectedly changed ROW");

    const auto& terminal = phases[2U];
    expect(terminal.first_command_index == 13U &&
               terminal.command_count == 3U &&
               terminal.control_command_count == 2U &&
               terminal.unpack_command_count == 0U &&
               terminal.nop_command_count == 1U &&
               terminal.first_write_index == 5U && terminal.write_count == 0U &&
               terminal.unique_destination_runs.empty(),
           "terminal control-only phase is wrong");
    expect_known_row(terminal.state_after, {1U, 2U, 3U, 4U},
                     "terminal STROW was not reflected in state_after");
    expect_state_equal(terminal.state_after, snapshot.final_state,
                       "terminal phase does not end at final snapshot state");

    const auto retained_first_range = phases.front().stream_range;
    const auto retained_last_row = phases.back().state_after.row;
    snapshot = {};
    expect(phases.front().stream_range == retained_first_range &&
               phases.back().state_after.row == retained_last_row,
           "phase result retained pointers into the snapshot");

    static_assert(std::is_trivially_copyable_v<openrc::SceneBlockVuQwordRunV1>);
}

void test_wrap_cross_phase_and_internal_overwrites() {
    std::vector<std::byte> bytes;
    append_stcycl(bytes, 4U, 4U); // phase 0
    append_unpack_code(bytes, openrc::SceneBlockVifOpcode::unpack_v4_8, 1022U,
                       4U, true, false);
    append_payload8(bytes, {
                               1U,
                               1U,
                               1U,
                               1U,
                               2U,
                               2U,
                               2U,
                               2U,
                               3U,
                               3U,
                               3U,
                               3U,
                               4U,
                               4U,
                               4U,
                               4U,
                           });
    append_nop(bytes);
    append_nop(bytes);

    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::offset); // phase 1
    append_unpack_code(bytes, openrc::SceneBlockVifOpcode::unpack_v4_8, 1023U,
                       3U, true, false);
    append_payload8(bytes, {
                               5U,
                               5U,
                               5U,
                               5U,
                               6U,
                               6U,
                               6U,
                               6U,
                               7U,
                               7U,
                               7U,
                               7U,
                           });

    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::offset); // phase 2
    for (std::uint8_t seed = 8U; seed <= 10U; seed += 2U) {
        append_unpack_code(bytes, openrc::SceneBlockVifOpcode::unpack_v4_8, 10U,
                           2U, true, false);
        append_payload8(bytes, {
                                   seed,
                                   seed,
                                   seed,
                                   seed,
                                   static_cast<std::uint8_t>(seed + 1U),
                                   static_cast<std::uint8_t>(seed + 1U),
                                   static_cast<std::uint8_t>(seed + 1U),
                                   static_cast<std::uint8_t>(seed + 1U),
                               });
    }

    const auto snapshot = execute(bytes);
    const auto phases = openrc::group_scene_block_vu_phases_v1(snapshot);
    expect(phases.size() == 3U, "overlap fixture has the wrong phase count");
    expect_partition(snapshot, phases);

    expect(phases[0U].command_count == 4U && phases[0U].nop_command_count == 2U,
           "NOPs before a phase boundary were not left-attached");
    expect(phases[0U].write_count == 4U &&
               phases[0U].unique_qword_count == 4U &&
               phases[0U].unique_destination_runs ==
                   std::vector<openrc::SceneBlockVuQwordRunV1>{{0U, 2U},
                                                               {1022U, 2U}},
           "wrapped phase runs were not split and sorted");
    expect(phases[1U].write_count == 3U &&
               phases[1U].unique_qword_count == 3U &&
               phases[1U].internal_overwrite_count == 0U &&
               phases[1U].prior_phase_overwrite_count == 3U &&
               phases[1U].unique_destination_runs ==
                   std::vector<openrc::SceneBlockVuQwordRunV1>{{0U, 2U},
                                                               {1023U, 1U}},
           "cross-phase overwrite classification is wrong");
    expect(phases[2U].write_count == 4U &&
               phases[2U].unique_qword_count == 2U &&
               phases[2U].internal_overwrite_count == 2U &&
               phases[2U].prior_phase_overwrite_count == 0U &&
               phases[2U].unique_destination_runs ==
                   std::vector<openrc::SceneBlockVuQwordRunV1>{{10U, 2U}},
           "internal overwrite classification is wrong");
    expect(snapshot.unique_qword_writes == 6U &&
               snapshot.overwrite_vector_writes == 5U &&
               snapshot.wrapped_vector_writes == 4U &&
               phases[0U].internal_overwrite_count +
                       phases[0U].prior_phase_overwrite_count +
                       phases[1U].internal_overwrite_count +
                       phases[1U].prior_phase_overwrite_count +
                       phases[2U].internal_overwrite_count +
                       phases[2U].prior_phase_overwrite_count ==
                   snapshot.overwrite_vector_writes,
           "phase overwrite classes do not partition global overwrites");
    expect(phases[1U].state_before.cycle_length == 4U &&
               phases[1U].state_before.write_length == 4U &&
               phases[1U].state_before.addition_mode ==
                   openrc::SceneBlockVuAdditionMode::normal &&
               phases[1U].state_after.addition_mode ==
                   openrc::SceneBlockVuAdditionMode::offset &&
               phases[2U].state_before.cycle_length == 4U &&
               phases[2U].state_before.write_length == 4U,
           "phase state did not preserve inherited STCYCL values");
}

void test_empty_nop_only_initial_data_and_zero_cycle_fill() {
    {
        const openrc::SceneBlockVuSnapshotV1 snapshot;
        const auto phases = openrc::group_scene_block_vu_phases_v1(snapshot);
        expect(phases.empty(), "empty stream produced a phase");
        expect_partition(snapshot, phases);
    }
    {
        std::vector<std::byte> bytes;
        append_nop(bytes);
        append_nop(bytes);
        append_nop(bytes);
        const auto snapshot = execute(bytes);
        const auto phases = openrc::group_scene_block_vu_phases_v1(snapshot);
        expect(phases.size() == 1U && phases[0U].command_count == 3U &&
                   phases[0U].nop_command_count == 3U &&
                   phases[0U].control_command_count == 0U &&
                   phases[0U].unpack_command_count == 0U &&
                   phases[0U].write_count == 0U,
               "NOP-only stream did not produce one neutral phase");
        expect_partition(snapshot, phases);
    }
    {
        std::vector<std::byte> bytes;
        append_unpack_code(bytes, openrc::SceneBlockVifOpcode::unpack_v4_8, 7U,
                           1U, true, false);
        append_payload8(bytes, {1U, 2U, 3U, 4U});
        append_nop(bytes);
        const auto snapshot = execute(bytes);
        const auto phases = openrc::group_scene_block_vu_phases_v1(snapshot);
        expect(phases.size() == 1U && phases[0U].control_command_count == 0U &&
                   phases[0U].unpack_command_count == 1U &&
                   phases[0U].nop_command_count == 1U &&
                   phases[0U].unique_destination_runs ==
                       std::vector<openrc::SceneBlockVuQwordRunV1>{{7U, 1U}},
               "initial data-only phase is wrong");
        expect_partition(snapshot, phases);
    }
    {
        std::vector<std::byte> bytes;
        append_stcycl(bytes, 0U, 0U);
        append_unpack_code(bytes, openrc::SceneBlockVifOpcode::unpack_v4_8,
                           100U, 4U, true, false);
        const auto snapshot = execute(bytes);
        const auto phases = openrc::group_scene_block_vu_phases_v1(snapshot);
        expect(
            phases.size() == 1U && phases[0U].write_count == 4U &&
                phases[0U].unique_destination_runs ==
                    std::vector<openrc::SceneBlockVuQwordRunV1>{{100U, 4U}} &&
                phases[0U].state_after.cycle_length == 0U &&
                phases[0U].state_after.write_length == 256U,
            "raw zero STCYCL fill phase is wrong");
        for (const auto& write : snapshot.writes) {
            expect(!write.input_vector_index.has_value(),
                   "raw zero CL fill write unexpectedly consumed input");
        }
        expect_partition(snapshot, phases);
    }
}

void test_difference_row_and_v3_unknown_lane() {
    std::vector<std::byte> bytes;
    append_strow(bytes, {10U, 20U, 30U, 40U});
    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::difference);
    append_stcycl(bytes, 2U, 1U);
    append_unpack_code(bytes, openrc::SceneBlockVifOpcode::unpack_v3_16, 50U,
                       2U, true, false);
    append_payload16(bytes, {
                                1U,
                                2U,
                                3U,
                                4U,
                                5U,
                                6U,
                            });

    const auto snapshot = execute(bytes);
    const auto phases = openrc::group_scene_block_vu_phases_v1(snapshot);
    expect(phases.size() == 1U, "difference fixture split unexpectedly");
    expect_partition(snapshot, phases);
    expect(
        phases[0U].unique_destination_runs ==
            std::vector<openrc::SceneBlockVuQwordRunV1>{{50U, 1U}, {52U, 1U}},
        "difference V3 skip coverage is wrong");
    expect(phases[0U].state_after.row[0U].state ==
                   openrc::SceneBlockVuValueState::known &&
               phases[0U].state_after.row[0U].bits == 15U &&
               phases[0U].state_after.row[1U].bits == 27U &&
               phases[0U].state_after.row[2U].bits == 39U &&
               phases[0U].state_after.row[3U].state ==
                   openrc::SceneBlockVuValueState::indeterminate,
           "MODE 2 or V3-W ROW reconstruction is wrong");
    expect_state_equal(phases[0U].state_after, snapshot.final_state,
                       "difference phase state does not match final state");
}

template <typename Mutator>
void expect_rejected_snapshot(const openrc::SceneBlockVuSnapshotV1& valid,
                              Mutator&& mutate, const char* message) {
    auto invalid = valid;
    mutate(invalid);
    expect_phase_error(
        [&] { (void)openrc::group_scene_block_vu_phases_v1(invalid); },
        message);
}

void test_inconsistent_snapshot_rejections() {
    std::vector<std::byte> bytes;
    append_unpack_code(bytes, openrc::SceneBlockVifOpcode::unpack_v4_8, 7U, 2U,
                       true, false);
    append_payload8(bytes, {
                               1U,
                               2U,
                               3U,
                               4U,
                               5U,
                               6U,
                               7U,
                               8U,
                           });
    const auto valid = execute(bytes);
    expect(openrc::group_scene_block_vu_phases_v1(valid).size() == 1U,
           "validation base fixture is invalid");

    expect_rejected_snapshot(
        valid,
        [](auto& snapshot) {
            snapshot.stream.commands[0U].packet_range.offset = 1U;
        },
        "a gapped packet partition was accepted");
    expect_rejected_snapshot(
        valid,
        [](auto& snapshot) { snapshot.stream.commands[0U].raw_code ^= 1U; },
        "raw VIFcode disagreement was accepted");
    expect_rejected_snapshot(
        valid,
        [](auto& snapshot) {
            snapshot.stream.commands[0U].output_vector_count = 1U;
        },
        "UNPACK output metadata disagreement was accepted");
    expect_rejected_snapshot(
        valid, [](auto& snapshot) { snapshot.writes[0U].command_index = 1U; },
        "out-of-order write command provenance was accepted");
    expect_rejected_snapshot(
        valid,
        [](auto& snapshot) { snapshot.writes[0U].destination_qword = 999U; },
        "wrong wrapped destination was accepted");
    expect_rejected_snapshot(
        valid,
        [](auto& snapshot) {
            snapshot.writes[0U].lanes[0U].row_before = {
                openrc::SceneBlockVuValueState::known, 1U};
        },
        "write ROW disagreement was accepted");
    expect_rejected_snapshot(
        valid,
        [](auto& snapshot) {
            snapshot.writes[0U].lanes[0U].source_range.offset += 1U;
        },
        "lane source range disagreement was accepted");
    expect_rejected_snapshot(
        valid, [](auto& snapshot) { --snapshot.total_vector_writes; },
        "wrong total write count was accepted");
    expect_rejected_snapshot(
        valid, [](auto& snapshot) { ++snapshot.memory[7U].write_count; },
        "wrong memory write count was accepted");
    expect_rejected_snapshot(
        valid,
        [](auto& snapshot) {
            snapshot.memory[7U].last_write_index = std::nullopt;
        },
        "wrong last-write provenance was accepted");
    expect_rejected_snapshot(
        valid, [](auto& snapshot) { snapshot.final_state.cycle_length = 2U; },
        "wrong final phase state was accepted");

    auto irrelevant_bits = valid;
    irrelevant_bits.final_state.row[0U].bits = 0xdeadbeefU;
    irrelevant_bits.memory[500U].lanes[2U].bits = 0xa5a5a5a5U;
    expect(openrc::group_scene_block_vu_phases_v1(irrelevant_bits).size() == 1U,
           "irrelevant bits of indeterminate values changed validation");
}

} // namespace

int main() {
    try {
        test_nop_ownership_state_and_skip_runs();
        test_wrap_cross_phase_and_internal_overwrites();
        test_empty_nop_only_initial_data_and_zero_cycle_fill();
        test_difference_row_and_v3_unknown_lane();
        test_inconsistent_snapshot_rejections();
        std::cout << "SceneBlock VU phase tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SceneBlock VU phase tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
