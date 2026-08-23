#include "openrc/scene_block_vu.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

static_assert(openrc::kSceneBlockVuLaneCount == 4U);
static_assert(openrc::kSceneBlockVuMemoryQwordCount == 1024U);

constexpr openrc::SceneBlockVuLimits kGenerousLimits{
    openrc::SceneBlockVifLimits{
        1024U * 1024U,
        4096U,
        1024U * 1024U,
    },
    4096U,
};

void expect(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void expect_vu_error(Callable&& callable, const char* const message) {
    try {
        callable();
    } catch (const openrc::SceneBlockVuError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] constexpr std::uint32_t make_code(
    const std::uint16_t immediate,
    const std::uint8_t raw_num,
    const std::uint8_t opcode) noexcept {
    return static_cast<std::uint32_t>(immediate) |
        (static_cast<std::uint32_t>(raw_num) << 16U) |
        (static_cast<std::uint32_t>(opcode) << 24U);
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

void append_stcycl(
    std::vector<std::byte>& bytes,
    const std::uint8_t cycle_length,
    const std::uint8_t write_length) {
    const auto immediate = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(cycle_length) |
        (static_cast<std::uint16_t>(write_length) << 8U));
    append_le32(bytes, make_code(immediate, 0U, 0x01U));
}

void append_stmod(
    std::vector<std::byte>& bytes,
    const openrc::SceneBlockVuAdditionMode mode) {
    append_le32(
        bytes,
        make_code(
            static_cast<std::uint16_t>(mode),
            0U,
            0x05U));
}

void append_strow(
    std::vector<std::byte>& bytes,
    const std::array<std::uint32_t, 4U>& row) {
    append_le32(bytes, make_code(0U, 0U, 0x30U));
    for (const auto value : row) {
        append_le32(bytes, value);
    }
}

void append_unpack_code(
    std::vector<std::byte>& bytes,
    const openrc::SceneBlockVifOpcode opcode,
    const std::uint16_t address,
    const std::uint8_t raw_num,
    const bool unsigned_data,
    const bool use_tops) {
    auto immediate = static_cast<std::uint16_t>(address & 0x03ffU);
    if (unsigned_data) {
        immediate = static_cast<std::uint16_t>(immediate | 0x4000U);
    }
    if (use_tops) {
        immediate = static_cast<std::uint16_t>(immediate | 0x8000U);
    }
    append_le32(
        bytes,
        make_code(
            immediate,
            raw_num,
            static_cast<std::uint8_t>(opcode)));
}

void pad_to_four(std::vector<std::byte>& bytes) {
    while (bytes.size() % openrc::kSceneBlockVifPayloadAlignment != 0U) {
        bytes.push_back(std::byte{0});
    }
}

[[nodiscard]] std::vector<std::byte> payload16(
    const std::initializer_list<std::uint16_t> values) {
    std::vector<std::byte> bytes;
    for (const auto value : values) {
        append_le16(bytes, value);
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> payload32(
    const std::initializer_list<std::uint32_t> values) {
    std::vector<std::byte> bytes;
    for (const auto value : values) {
        append_le32(bytes, value);
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> payload8(
    const std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> bytes;
    for (const auto value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

void expect_value(
    const openrc::SceneBlockVuValueV1& actual,
    const bool known,
    const std::uint32_t bits,
    const char* const message) {
    expect(
        actual.state ==
            (known
                 ? openrc::SceneBlockVuValueState::known
                 : openrc::SceneBlockVuValueState::indeterminate),
        message);
    if (known) {
        expect(actual.bits == bits, message);
    }
}

void expect_known_vector(
    const std::array<openrc::SceneBlockVuValueV1, 4U>& actual,
    const std::array<std::uint32_t, 4U>& expected,
    const char* const message) {
    for (std::size_t lane = 0U; lane < actual.size(); ++lane) {
        expect_value(actual[lane], true, expected[lane], message);
    }
}

void expect_indeterminate_vector(
    const std::array<openrc::SceneBlockVuValueV1, 4U>& actual,
    const char* const message) {
    for (const auto& lane : actual) {
        expect_value(lane, false, 0U, message);
    }
}

[[nodiscard]] std::uint8_t known_mask(
    const openrc::SceneBlockVuMemoryQwordV1& qword) {
    std::uint8_t result = 0U;
    for (std::size_t lane = 0U; lane < qword.lanes.size(); ++lane) {
        if (qword.lanes[lane].state == openrc::SceneBlockVuValueState::known) {
            result = static_cast<std::uint8_t>(
                result | (std::uint8_t{1U} << lane));
        }
    }
    return result;
}

void test_control_state_and_empty_memory() {
    std::vector<std::byte> bytes;
    append_le32(bytes, make_code(0U, 0U, 0x00U));
    append_stcycl(bytes, 4U, 2U);
    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::offset);
    constexpr std::array<std::uint32_t, 4U> kRow{
        0x00000001U,
        0xffffffffU,
        0x7fffffffU,
        0x80000000U,
    };
    append_strow(bytes, kRow);
    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::difference);

    const auto snapshot = openrc::execute_scene_block_vu_v1(
        bytes,
        openrc::SceneBlockVuExecutionOptionsV1{37U},
        kGenerousLimits);

    expect(snapshot.stream.commands.size() == 5U,
           "executor did not retain the internal parser report");
    expect(snapshot.final_state.tops_qword == 37U,
           "executor did not retain the explicit TOPS value");
    expect(snapshot.final_state.cycle_length == 4U,
           "executor final CL is wrong");
    expect(snapshot.final_state.write_length == 2U,
           "executor final WL is wrong");
    expect(snapshot.final_state.addition_mode ==
               openrc::SceneBlockVuAdditionMode::difference,
           "executor final MODE is wrong");
    expect_known_vector(snapshot.final_state.row, kRow,
                        "STROW did not load the final ROW state");
    expect(snapshot.writes.empty(), "control commands generated vector writes");
    expect(snapshot.total_vector_writes == 0U,
           "control-only stream has a non-zero write total");
    expect(snapshot.unique_qword_writes == 0U,
           "control-only stream has unique memory writes");
    expect(snapshot.overwrite_vector_writes == 0U,
           "control-only stream has overwrite events");
    expect(snapshot.wrapped_vector_writes == 0U,
           "control-only stream has wrapped writes");
    for (const auto& qword : snapshot.memory) {
        expect(qword.write_count == 0U,
               "untouched VU memory has a write count");
        expect(!qword.last_write_index.has_value(),
               "untouched VU memory has provenance");
        expect_indeterminate_vector(
            qword.lanes,
            "untouched VU memory is not indeterminate");
    }
}

void check_single_unpack(
    const openrc::SceneBlockVifOpcode opcode,
    const bool unsigned_data,
    const std::vector<std::byte>& payload,
    const std::array<std::uint32_t, 4U>& expected,
    const std::uint8_t known_lanes,
    const std::uint8_t component_bytes) {
    std::vector<std::byte> bytes;
    append_unpack_code(
        bytes,
        opcode,
        7U,
        1U,
        unsigned_data,
        false);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    pad_to_four(bytes);

    const auto snapshot = openrc::execute_scene_block_vu_v1(
        bytes,
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        kGenerousLimits);
    expect(snapshot.stream.commands.size() == 1U,
           "single UNPACK parser report has the wrong size");
    expect(snapshot.writes.size() == 1U,
           "single UNPACK did not generate one vector write");
    const auto& write = snapshot.writes.front();
    expect(write.command_index == 0U,
           "single UNPACK provenance has the wrong command index");
    expect(write.output_vector_index == 0U,
           "single UNPACK provenance has the wrong output index");
    expect(write.input_vector_index == std::optional<std::uint16_t>{0U},
           "single UNPACK provenance has the wrong input index");
    expect(write.unwrapped_destination_qword == 7U &&
               write.destination_qword == 7U && !write.wrapped,
           "single UNPACK destination is wrong");
    expect(write.addition_mode == openrc::SceneBlockVuAdditionMode::normal,
           "single UNPACK used the wrong MODE");

    for (std::size_t lane = 0U; lane < 4U; ++lane) {
        const bool known = lane < known_lanes;
        const auto& lane_write = write.lanes[lane];
        expect(
            lane_write.source ==
                (known
                     ? openrc::SceneBlockVuLaneSource::payload
                     : openrc::SceneBlockVuLaneSource::v3_w_indeterminate),
            "single UNPACK lane source is wrong");
        const auto expected_range = known
            ? openrc::SceneBlockVifRange{
                  4U + lane * component_bytes,
                  component_bytes}
            : openrc::SceneBlockVifRange{0U, 0U};
        expect(lane_write.source_range == expected_range,
               "single UNPACK source range is wrong");
        expect_value(lane_write.unpacked_value, known, expected[lane],
                     "single UNPACK decoded value is wrong");
        expect_value(lane_write.row_before, false, 0U,
                     "initial ROW should be indeterminate");
        expect_value(lane_write.written_value, known, expected[lane],
                     "single UNPACK written value is wrong");
    }

    const auto& memory = snapshot.memory[7U];
    expect(memory.write_count == 1U,
           "single UNPACK memory write count is wrong");
    expect(memory.last_write_index == std::optional<std::uint64_t>{0U},
           "single UNPACK memory provenance is wrong");
    expect(known_mask(memory) ==
               static_cast<std::uint8_t>((1U << known_lanes) - 1U),
           "single UNPACK known-component mask is wrong");
    for (std::size_t lane = 0U; lane < 4U; ++lane) {
        expect_value(memory.lanes[lane], lane < known_lanes, expected[lane],
                     "single UNPACK final memory is wrong");
    }
    expect(snapshot.total_vector_writes == 1U &&
               snapshot.unique_qword_writes == 1U &&
               snapshot.overwrite_vector_writes == 0U &&
               snapshot.wrapped_vector_writes == 0U,
           "single UNPACK aggregate write counters are wrong");

    std::fill(bytes.begin(), bytes.end(), std::byte{0xff});
    expect_value(snapshot.memory[7U].lanes[0], true, expected[0],
                 "snapshot retained a pointer into the input stream");
}

void test_unpack_formats_and_signedness() {
    constexpr std::array<std::uint32_t, 4U> kSigned16{
        0xffff8000U, 0xffffffffU, 0x00007fffU, 0x00000001U};
    constexpr std::array<std::uint32_t, 4U> kUnsigned16{
        0x00008000U, 0x0000ffffU, 0x00007fffU, 0x00000001U};
    constexpr std::array<std::uint32_t, 4U> kRaw32{
        0x80000000U, 0xffffffffU, 0x7fffffffU, 0x00000001U};
    constexpr std::array<std::uint32_t, 4U> kSigned8{
        0xffffff80U, 0xffffffffU, 0x0000007fU, 0x00000001U};
    constexpr std::array<std::uint32_t, 4U> kUnsigned8{
        0x00000080U, 0x000000ffU, 0x0000007fU, 0x00000001U};

    const auto v3_payload = payload16({0x8000U, 0xffffU, 0x7fffU});
    check_single_unpack(
        openrc::SceneBlockVifOpcode::unpack_v3_16,
        false,
        v3_payload,
        kSigned16,
        3U,
        2U);
    check_single_unpack(
        openrc::SceneBlockVifOpcode::unpack_v3_16,
        true,
        v3_payload,
        kUnsigned16,
        3U,
        2U);

    const auto v4_32_payload =
        payload32({0x80000000U, 0xffffffffU, 0x7fffffffU, 1U});
    check_single_unpack(
        openrc::SceneBlockVifOpcode::unpack_v4_32,
        false,
        v4_32_payload,
        kRaw32,
        4U,
        4U);
    check_single_unpack(
        openrc::SceneBlockVifOpcode::unpack_v4_32,
        true,
        v4_32_payload,
        kRaw32,
        4U,
        4U);

    const auto v4_16_payload =
        payload16({0x8000U, 0xffffU, 0x7fffU, 1U});
    check_single_unpack(
        openrc::SceneBlockVifOpcode::unpack_v4_16,
        false,
        v4_16_payload,
        kSigned16,
        4U,
        2U);
    check_single_unpack(
        openrc::SceneBlockVifOpcode::unpack_v4_16,
        true,
        v4_16_payload,
        kUnsigned16,
        4U,
        2U);

    const auto v4_8_payload = payload8({0x80U, 0xffU, 0x7fU, 1U});
    check_single_unpack(
        openrc::SceneBlockVifOpcode::unpack_v4_8,
        false,
        v4_8_payload,
        kSigned8,
        4U,
        1U);
    check_single_unpack(
        openrc::SceneBlockVifOpcode::unpack_v4_8,
        true,
        v4_8_payload,
        kUnsigned8,
        4U,
        1U);
}

void test_strow_and_addition_modes() {
    std::vector<std::byte> bytes;
    constexpr std::array<std::uint32_t, 4U> kInitialRow{
        1U, 2U, 0xffffffffU, 0x80000000U};
    constexpr std::array<std::uint32_t, 4U> kInputA{
        10U, 20U, 1U, 0x80000000U};
    constexpr std::array<std::uint32_t, 4U> kInputB{
        3U, 2U, 2U, 2U};
    constexpr std::array<std::uint32_t, 4U> kAddedA{
        11U, 22U, 0U, 0U};
    constexpr std::array<std::uint32_t, 4U> kAddedB{
        14U, 24U, 2U, 2U};

    append_strow(bytes, kInitialRow);
    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::normal);
    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_32,
        10U,
        1U,
        false,
        false);
    const auto input_a = payload32(
        {kInputA[0], kInputA[1], kInputA[2], kInputA[3]});
    bytes.insert(bytes.end(), input_a.begin(), input_a.end());

    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::offset);
    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_32,
        11U,
        1U,
        false,
        false);
    bytes.insert(bytes.end(), input_a.begin(), input_a.end());

    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::difference);
    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_32,
        12U,
        2U,
        false,
        false);
    bytes.insert(bytes.end(), input_a.begin(), input_a.end());
    const auto input_b = payload32(
        {kInputB[0], kInputB[1], kInputB[2], kInputB[3]});
    bytes.insert(bytes.end(), input_b.begin(), input_b.end());

    const auto snapshot = openrc::execute_scene_block_vu_v1(
        bytes,
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        kGenerousLimits);
    expect(snapshot.writes.size() == 4U,
           "MODE fixture generated the wrong number of writes");
    expect(snapshot.stream.commands.size() == 7U,
           "MODE fixture parser report has the wrong command count");

    const std::array<std::uint64_t, 4U> expected_commands{2U, 4U, 6U, 6U};
    const std::array<std::uint16_t, 4U> expected_outputs{0U, 0U, 0U, 1U};
    const std::array<openrc::SceneBlockVuAdditionMode, 4U> expected_modes{
        openrc::SceneBlockVuAdditionMode::normal,
        openrc::SceneBlockVuAdditionMode::offset,
        openrc::SceneBlockVuAdditionMode::difference,
        openrc::SceneBlockVuAdditionMode::difference,
    };
    for (std::size_t index = 0U; index < snapshot.writes.size(); ++index) {
        expect(snapshot.writes[index].command_index == expected_commands[index],
               "MODE write has the wrong command provenance");
        expect(snapshot.writes[index].output_vector_index ==
                   expected_outputs[index],
               "MODE write has the wrong output provenance");
        expect(snapshot.writes[index].addition_mode == expected_modes[index],
               "MODE write retained the wrong mode");
    }

    expect_known_vector(snapshot.memory[10U].lanes, kInputA,
                        "MODE 0 did not write the unpacked value");
    expect_known_vector(snapshot.memory[11U].lanes, kAddedA,
                        "MODE 1 did not add ROW modulo 2^32");
    expect_known_vector(snapshot.memory[12U].lanes, kAddedA,
                        "MODE 2 first result is wrong");
    expect_known_vector(snapshot.memory[13U].lanes, kAddedB,
                        "MODE 2 did not accumulate ROW");

    for (std::size_t lane = 0U; lane < 4U; ++lane) {
        expect_value(snapshot.writes[0U].lanes[lane].row_before,
                     true, kInitialRow[lane],
                     "MODE 0 ROW provenance is wrong");
        expect_value(snapshot.writes[1U].lanes[lane].row_before,
                     true, kInitialRow[lane],
                     "MODE 1 unexpectedly changed ROW");
        expect_value(snapshot.writes[2U].lanes[lane].row_before,
                     true, kInitialRow[lane],
                     "MODE 2 initial ROW is wrong");
        expect_value(snapshot.writes[3U].lanes[lane].row_before,
                     true, kAddedA[lane],
                     "MODE 2 did not expose its accumulated ROW");
    }
    expect(
        snapshot.writes[3U].lanes[2U].source_range ==
            openrc::SceneBlockVifRange{100U, 4U},
        "MODE 2 second-vector source range is wrong");
    expect_known_vector(snapshot.final_state.row, kAddedB,
                        "MODE 2 final ROW is wrong");
    expect(snapshot.final_state.addition_mode ==
               openrc::SceneBlockVuAdditionMode::difference,
           "MODE fixture final mode is wrong");
    expect(snapshot.total_vector_writes == 4U &&
               snapshot.unique_qword_writes == 4U &&
               snapshot.overwrite_vector_writes == 0U,
           "MODE fixture aggregate counters are wrong");
}

void test_v3_unknown_overwrite_and_difference_propagation() {
    std::vector<std::byte> bytes;
    append_strow(bytes, {10U, 20U, 30U, 40U});
    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::difference);

    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_32,
        5U,
        1U,
        false,
        false);
    const auto first = payload32({100U, 200U, 300U, 400U});
    bytes.insert(bytes.end(), first.begin(), first.end());

    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v3_16,
        5U,
        1U,
        true,
        false);
    const auto v3 = payload16({1U, 2U, 3U});
    bytes.insert(bytes.end(), v3.begin(), v3.end());
    pad_to_four(bytes);

    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_8,
        6U,
        1U,
        true,
        false);
    const auto final_input = payload8({1U, 1U, 1U, 1U});
    bytes.insert(bytes.end(), final_input.begin(), final_input.end());

    const auto snapshot = openrc::execute_scene_block_vu_v1(
        bytes,
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        kGenerousLimits);
    expect(snapshot.writes.size() == 3U,
           "V3 propagation fixture has the wrong write count");
    expect(snapshot.memory[5U].write_count == 2U,
           "V3 overwrite was not counted");
    expect(snapshot.memory[5U].last_write_index ==
               std::optional<std::uint64_t>{1U},
           "V3 overwrite provenance is wrong");
    expect(known_mask(snapshot.memory[5U]) == 0x07U,
           "V3 overwrite did not clear the prior known W lane");
    expect_value(snapshot.memory[5U].lanes[0U], true, 111U,
                 "V3 MODE 2 X result is wrong");
    expect_value(snapshot.memory[5U].lanes[1U], true, 222U,
                 "V3 MODE 2 Y result is wrong");
    expect_value(snapshot.memory[5U].lanes[2U], true, 333U,
                 "V3 MODE 2 Z result is wrong");
    expect_value(snapshot.memory[5U].lanes[3U], false, 0U,
                 "V3 W was treated as known");

    const auto& v3_w = snapshot.writes[1U].lanes[3U];
    expect(v3_w.source == openrc::SceneBlockVuLaneSource::v3_w_indeterminate,
           "V3 W has the wrong lane source");
    expect(v3_w.source_range == openrc::SceneBlockVifRange{0U, 0U},
           "V3 W has a payload source range");
    expect_value(v3_w.row_before, true, 440U,
                 "V3 W ROW provenance is wrong");
    expect_value(v3_w.unpacked_value, false, 0U,
                 "V3 W unpacked value is known");
    expect_value(v3_w.written_value, false, 0U,
                 "V3 W MODE 2 result is known");

    expect(known_mask(snapshot.memory[6U]) == 0x07U,
           "unknown difference ROW did not propagate to the next W");
    expect_value(snapshot.memory[6U].lanes[0U], true, 112U,
                 "post-V3 difference X is wrong");
    expect_value(snapshot.memory[6U].lanes[1U], true, 223U,
                 "post-V3 difference Y is wrong");
    expect_value(snapshot.memory[6U].lanes[2U], true, 334U,
                 "post-V3 difference Z is wrong");
    expect_value(snapshot.memory[6U].lanes[3U], false, 0U,
                 "post-V3 difference W did not remain unknown");
    expect(
        snapshot.writes[2U].lanes[3U].source ==
            openrc::SceneBlockVuLaneSource::payload &&
            snapshot.writes[2U].lanes[3U].source_range ==
                openrc::SceneBlockVifRange{63U, 1U},
        "known payload after V3 has the wrong W source provenance");
    expect_value(snapshot.writes[2U].lanes[3U].unpacked_value, true, 1U,
                 "known payload after V3 decoded incorrectly");
    expect_value(snapshot.writes[2U].lanes[3U].row_before, false, 0U,
                 "V3 MODE 2 did not poison ROW.W");
    expect_value(snapshot.final_state.row[3U], false, 0U,
                 "unknown ROW.W did not survive to final state");
    expect(snapshot.total_vector_writes == 3U &&
               snapshot.unique_qword_writes == 2U &&
               snapshot.overwrite_vector_writes == 1U,
           "V3 overwrite aggregate counters are wrong");
}

void test_skip_cycle_addressing() {
    std::vector<std::byte> bytes;
    append_stcycl(bytes, 4U, 2U);
    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_8,
        10U,
        5U,
        true,
        false);
    for (std::uint8_t vector = 1U; vector <= 5U; ++vector) {
        const auto payload = payload8({vector, vector, vector, vector});
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }

    const auto snapshot = openrc::execute_scene_block_vu_v1(
        bytes,
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        kGenerousLimits);
    constexpr std::array<std::uint16_t, 5U> kAddresses{
        10U, 11U, 14U, 15U, 18U};
    expect(snapshot.writes.size() == kAddresses.size(),
           "skip-mode write count is wrong");
    for (std::size_t index = 0U; index < kAddresses.size(); ++index) {
        const auto& write = snapshot.writes[index];
        expect(write.command_index == 1U &&
                   write.output_vector_index == index &&
                   write.input_vector_index ==
                       std::optional<std::uint16_t>{
                           static_cast<std::uint16_t>(index)},
               "skip-mode vector provenance is wrong");
        expect(write.unwrapped_destination_qword == kAddresses[index] &&
                   write.destination_qword == kAddresses[index] &&
                   !write.wrapped,
               "skip-mode destination sequence is wrong");
        for (const auto& lane : snapshot.memory[kAddresses[index]].lanes) {
            expect_value(
                lane,
                true,
                static_cast<std::uint32_t>(index + 1U),
                "skip-mode payload value is wrong");
        }
    }
    expect(
        snapshot.writes[2U].lanes[3U].source_range ==
            openrc::SceneBlockVifRange{19U, 1U},
        "skip-mode source range is wrong");
    for (const auto hole : std::array<std::size_t, 4U>{12U, 13U, 16U, 17U}) {
        expect(snapshot.memory[hole].write_count == 0U &&
                   known_mask(snapshot.memory[hole]) == 0U,
               "skip-mode hole was written");
    }
    expect(snapshot.final_state.cycle_length == 4U &&
               snapshot.final_state.write_length == 2U,
           "skip-mode final cycle state is wrong");
    expect(snapshot.total_vector_writes == 5U &&
               snapshot.unique_qword_writes == 5U,
           "skip-mode aggregate counters are wrong");
}

void test_fill_cycle_unknown_and_source_mapping() {
    std::vector<std::byte> bytes;
    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_32,
        22U,
        1U,
        true,
        false);
    const auto prior = payload32({100U, 200U, 300U, 400U});
    bytes.insert(bytes.end(), prior.begin(), prior.end());

    append_stcycl(bytes, 2U, 4U);
    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_16,
        20U,
        6U,
        true,
        false);
    const auto fill_payload = payload16({
        1U, 2U, 3U, 4U,
        5U, 6U, 7U, 8U,
        9U, 10U, 11U, 12U,
        13U, 14U, 15U, 16U,
    });
    bytes.insert(bytes.end(), fill_payload.begin(), fill_payload.end());

    const auto snapshot = openrc::execute_scene_block_vu_v1(
        bytes,
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        kGenerousLimits);
    expect(snapshot.stream.commands[2U].input_vector_count == 4U,
           "fill-mode parser input count is wrong");
    expect(snapshot.writes.size() == 7U,
           "fill-mode output writes were not all retained");
    constexpr std::array<int, 6U> kInputIndices{0, 1, -1, -1, 2, 3};
    for (std::size_t output = 0U; output < kInputIndices.size(); ++output) {
        const auto& write = snapshot.writes[output + 1U];
        expect(write.command_index == 2U &&
                   write.output_vector_index == output &&
                   write.destination_qword == 20U + output,
               "fill-mode vector provenance is wrong");
        if (kInputIndices[output] < 0) {
            expect(!write.input_vector_index.has_value(),
                   "fill-generated vector has an input index");
            for (const auto& lane : write.lanes) {
                expect(lane.source ==
                           openrc::SceneBlockVuLaneSource::
                               cycle_fill_indeterminate,
                       "fill-generated lane has the wrong source");
                expect(lane.source_range ==
                           openrc::SceneBlockVifRange{0U, 0U},
                       "fill-generated lane has a source range");
                expect_value(lane.unpacked_value, false, 0U,
                             "fill-generated unpacked lane is known");
                expect_value(lane.written_value, false, 0U,
                             "fill-generated written lane is known");
            }
        } else {
            expect(
                write.input_vector_index ==
                    std::optional<std::uint16_t>{
                        static_cast<std::uint16_t>(kInputIndices[output])},
                "fill-mode payload vector has the wrong input index");
            for (const auto& lane : write.lanes) {
                expect(lane.source == openrc::SceneBlockVuLaneSource::payload,
                       "fill-mode payload lane has the wrong source");
                expect_value(lane.written_value, true, lane.unpacked_value.bits,
                             "fill-mode payload lane was not written");
            }
        }
    }

    expect(
        snapshot.writes[1U].lanes[0U].source_range ==
            openrc::SceneBlockVifRange{28U, 2U} &&
            snapshot.writes[5U].lanes[2U].source_range ==
                openrc::SceneBlockVifRange{48U, 2U},
        "fill-mode input-to-source mapping is wrong");
    expect(known_mask(snapshot.memory[20U]) == 0x0fU &&
               known_mask(snapshot.memory[21U]) == 0x0fU &&
               known_mask(snapshot.memory[22U]) == 0U &&
               known_mask(snapshot.memory[23U]) == 0U &&
               known_mask(snapshot.memory[24U]) == 0x0fU &&
               known_mask(snapshot.memory[25U]) == 0x0fU,
           "fill-mode known-component masks are wrong");
    expect(snapshot.memory[22U].write_count == 2U &&
               snapshot.memory[22U].last_write_index ==
                   std::optional<std::uint64_t>{3U},
           "fill overwrite provenance is wrong");
    expect(snapshot.total_vector_writes == 7U &&
               snapshot.unique_qword_writes == 6U &&
               snapshot.overwrite_vector_writes == 1U,
           "fill-mode aggregate counters are wrong");
}

void test_fill_difference_poisoning() {
    std::vector<std::byte> bytes;
    append_strow(bytes, {10U, 20U, 30U, 40U});
    append_stmod(bytes, openrc::SceneBlockVuAdditionMode::difference);
    append_stcycl(bytes, 1U, 3U);
    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_8,
        30U,
        4U,
        true,
        false);
    const auto payload = payload8({
        1U, 2U, 3U, 4U,
        5U, 6U, 7U, 8U,
    });
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    const auto snapshot = openrc::execute_scene_block_vu_v1(
        bytes,
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        kGenerousLimits);
    expect(snapshot.stream.commands[3U].input_vector_count == 2U,
           "difference fill fixture consumes the wrong input count");
    expect(snapshot.writes.size() == 4U,
           "difference fill fixture has the wrong output count");
    expect_known_vector(
        snapshot.memory[30U].lanes,
        {11U, 22U, 33U, 44U},
        "difference fill first payload result is wrong");
    for (const auto address : std::array<std::size_t, 3U>{31U, 32U, 33U}) {
        expect(known_mask(snapshot.memory[address]) == 0U,
               "difference fill did not propagate unknown ROW");
    }
    expect(!snapshot.writes[1U].input_vector_index.has_value() &&
               !snapshot.writes[2U].input_vector_index.has_value(),
           "difference fill generated input provenance");
    expect(snapshot.writes[3U].input_vector_index ==
               std::optional<std::uint16_t>{1U},
           "post-fill payload input index is wrong");
    for (std::size_t lane = 0U; lane < 4U; ++lane) {
        expect(snapshot.writes[3U].lanes[lane].source ==
                   openrc::SceneBlockVuLaneSource::payload,
               "post-fill payload lane lost its source");
        expect_value(snapshot.writes[3U].lanes[lane].unpacked_value,
                     true,
                     static_cast<std::uint32_t>(lane + 5U),
                     "post-fill payload value decoded incorrectly");
        expect_value(snapshot.writes[3U].lanes[lane].row_before,
                     false,
                     0U,
                     "fill-generated MODE 2 vector did not poison ROW");
        expect_value(snapshot.writes[3U].lanes[lane].written_value,
                     false,
                     0U,
                     "post-fill MODE 2 result is falsely known");
    }
    expect(
        snapshot.writes[3U].lanes[3U].source_range ==
            openrc::SceneBlockVifRange{39U, 1U},
        "post-fill payload source range is wrong");
    expect_indeterminate_vector(
        snapshot.final_state.row,
        "difference fill did not leave ROW indeterminate");
}

void test_tops_wrap_and_cross_command_provenance() {
    std::vector<std::byte> bytes;
    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_8,
        1023U,
        3U,
        true,
        false);
    for (std::uint8_t value = 1U; value <= 3U; ++value) {
        const auto vector = payload8({value, value, value, value});
        bytes.insert(bytes.end(), vector.begin(), vector.end());
    }
    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_8,
        1018U,
        3U,
        true,
        true);
    for (std::uint8_t value = 4U; value <= 6U; ++value) {
        const auto vector = payload8({value, value, value, value});
        bytes.insert(bytes.end(), vector.begin(), vector.end());
    }

    const auto snapshot = openrc::execute_scene_block_vu_v1(
        bytes,
        openrc::SceneBlockVuExecutionOptionsV1{5U},
        kGenerousLimits);
    expect(snapshot.writes.size() == 6U,
           "TOPS fixture has the wrong write count");
    constexpr std::array<std::uint64_t, 3U> kUnwrapped{
        1023U, 1024U, 1025U};
    constexpr std::array<std::uint16_t, 3U> kWrapped{
        1023U, 0U, 1U};
    for (std::size_t command = 0U; command < 2U; ++command) {
        for (std::size_t output = 0U; output < 3U; ++output) {
            const auto write_index = command * 3U + output;
            const auto& write = snapshot.writes[write_index];
            expect(write.command_index == command &&
                       write.output_vector_index == output,
                   "TOPS fixture command/output provenance is wrong");
            expect(write.unwrapped_destination_qword == kUnwrapped[output] &&
                       write.destination_qword == kWrapped[output] &&
                       write.wrapped == (output != 0U),
                   "TOPS or wrap destination is wrong");
        }
    }
    expect(snapshot.final_state.tops_qword == 5U,
           "TOPS fixture final state is wrong");
    expect(snapshot.memory[1023U].write_count == 2U &&
               snapshot.memory[1023U].last_write_index ==
                   std::optional<std::uint64_t>{3U},
           "TOPS address 1023 overwrite provenance is wrong");
    expect(snapshot.memory[0U].write_count == 2U &&
               snapshot.memory[0U].last_write_index ==
                   std::optional<std::uint64_t>{4U},
           "wrapped address 0 overwrite provenance is wrong");
    expect(snapshot.memory[1U].write_count == 2U &&
               snapshot.memory[1U].last_write_index ==
                   std::optional<std::uint64_t>{5U},
           "wrapped address 1 overwrite provenance is wrong");
    expect(snapshot.writes[4U].command_index == 1U &&
               snapshot.writes[4U].output_vector_index == 1U,
           "last_write_index does not resolve to full provenance");
    expect(
        snapshot.writes[4U].lanes[2U].source_range ==
            openrc::SceneBlockVifRange{26U, 1U},
        "TOPS overwrite source range is wrong");
    for (std::size_t lane = 0U; lane < 4U; ++lane) {
        expect_value(snapshot.memory[1023U].lanes[lane], true, 4U,
                     "TOPS overwrite value at 1023 is wrong");
        expect_value(snapshot.memory[0U].lanes[lane], true, 5U,
                     "TOPS overwrite value at 0 is wrong");
        expect_value(snapshot.memory[1U].lanes[lane], true, 6U,
                     "TOPS overwrite value at 1 is wrong");
    }
    expect(snapshot.total_vector_writes == 6U &&
               snapshot.unique_qword_writes == 3U &&
               snapshot.overwrite_vector_writes == 3U &&
               snapshot.wrapped_vector_writes == 4U,
           "TOPS/wrap aggregate counters are wrong");
}

void test_skip_wrap_self_overwrite_provenance() {
    std::vector<std::byte> bytes;
    append_stcycl(bytes, 128U, 1U);
    append_unpack_code(
        bytes,
        openrc::SceneBlockVifOpcode::unpack_v4_8,
        3U,
        9U,
        true,
        false);
    for (std::uint8_t value = 1U; value <= 9U; ++value) {
        const auto vector = payload8({value, value, value, value});
        bytes.insert(bytes.end(), vector.begin(), vector.end());
    }

    const auto snapshot = openrc::execute_scene_block_vu_v1(
        bytes,
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        kGenerousLimits);
    constexpr std::array<std::uint64_t, 9U> kUnwrapped{
        3U, 131U, 259U, 387U, 515U, 643U, 771U, 899U, 1027U};
    expect(snapshot.writes.size() == kUnwrapped.size(),
           "self-wrap fixture has the wrong write count");
    for (std::size_t index = 0U; index < kUnwrapped.size(); ++index) {
        expect(snapshot.writes[index].unwrapped_destination_qword ==
                   kUnwrapped[index],
               "skip-mode unwrapped address is wrong");
    }
    expect(snapshot.writes[8U].destination_qword == 3U &&
               snapshot.writes[8U].wrapped &&
               snapshot.writes[8U].command_index == 1U &&
               snapshot.writes[8U].output_vector_index == 8U &&
               snapshot.writes[8U].input_vector_index ==
                   std::optional<std::uint16_t>{8U},
           "self-wrap event provenance is wrong");
    expect(
        snapshot.writes[8U].lanes[3U].source_range ==
            openrc::SceneBlockVifRange{43U, 1U},
        "self-wrap final source range is wrong");
    expect(snapshot.memory[3U].write_count == 2U &&
               snapshot.memory[3U].last_write_index ==
                   std::optional<std::uint64_t>{8U},
           "self-overwrite memory provenance is wrong");
    for (const auto& lane : snapshot.memory[3U].lanes) {
        expect_value(lane, true, 9U,
                     "self-overwrite final memory value is wrong");
    }
    expect(snapshot.total_vector_writes == 9U &&
               snapshot.unique_qword_writes == 8U &&
               snapshot.overwrite_vector_writes == 1U &&
               snapshot.wrapped_vector_writes == 1U,
           "self-wrap aggregate counters are wrong");
}

void test_limits_and_errors() {
    std::vector<std::byte> four_writes;
    append_unpack_code(
        four_writes,
        openrc::SceneBlockVifOpcode::unpack_v4_8,
        0U,
        4U,
        true,
        false);
    const auto four_payload = payload8({
        1U, 1U, 1U, 1U,
        2U, 2U, 2U, 2U,
        3U, 3U, 3U, 3U,
        4U, 4U, 4U, 4U,
    });
    four_writes.insert(
        four_writes.end(), four_payload.begin(), four_payload.end());

    auto exact_limits = kGenerousLimits;
    exact_limits.max_vector_writes = 4U;
    const auto exact = openrc::execute_scene_block_vu_v1(
        four_writes,
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        exact_limits);
    expect(exact.total_vector_writes == 4U,
           "exact vector-write limit was not accepted");

    auto short_limits = exact_limits;
    short_limits.max_vector_writes = 3U;
    expect_vu_error(
        [&] {
            (void)openrc::execute_scene_block_vu_v1(
                four_writes,
                openrc::SceneBlockVuExecutionOptionsV1{0U},
                short_limits);
        },
        "vector-write limit one below the requirement was accepted");

    auto zero_limits = exact_limits;
    zero_limits.max_vector_writes = 0U;
    expect_vu_error(
        [&] {
            (void)openrc::execute_scene_block_vu_v1(
                four_writes,
                openrc::SceneBlockVuExecutionOptionsV1{0U},
                zero_limits);
        },
        "zero vector-write limit was accepted");
    expect_vu_error(
        [&] {
            (void)openrc::execute_scene_block_vu_v1(
                four_writes,
                openrc::SceneBlockVuExecutionOptionsV1{1024U},
                exact_limits);
        },
        "TOPS outside VU1 memory was accepted");

    auto input_limited = exact_limits;
    input_limited.vif.max_input_bytes = four_writes.size() - 1U;
    expect_vu_error(
        [&] {
            (void)openrc::execute_scene_block_vu_v1(
                four_writes,
                openrc::SceneBlockVuExecutionOptionsV1{0U},
                input_limited);
        },
        "internal parser input limit was ignored");

    std::vector<std::byte> overlapping;
    for (std::uint8_t command = 0U; command < 2U; ++command) {
        append_unpack_code(
            overlapping,
            openrc::SceneBlockVifOpcode::unpack_v4_8,
            8U,
            2U,
            true,
            false);
        const auto payload = payload8({
            static_cast<std::uint8_t>(command + 1U), 0U, 0U, 0U,
            static_cast<std::uint8_t>(command + 2U), 0U, 0U, 0U,
        });
        overlapping.insert(
            overlapping.end(), payload.begin(), payload.end());
    }
    expect_vu_error(
        [&] {
            (void)openrc::execute_scene_block_vu_v1(
                overlapping,
                openrc::SceneBlockVuExecutionOptionsV1{0U},
                short_limits);
        },
        "overwrites were not counted toward the vector-write limit");
    const auto overlapping_exact = openrc::execute_scene_block_vu_v1(
        overlapping,
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        exact_limits);
    expect(overlapping_exact.total_vector_writes == 4U &&
               overlapping_exact.unique_qword_writes == 2U &&
               overlapping_exact.overwrite_vector_writes == 2U,
           "overlap fixture counters are wrong at the exact limit");

    std::vector<std::byte> fill;
    append_stcycl(fill, 1U, 4U);
    append_unpack_code(
        fill,
        openrc::SceneBlockVifOpcode::unpack_v4_8,
        0U,
        4U,
        true,
        false);
    const auto one_input = payload8({1U, 2U, 3U, 4U});
    fill.insert(fill.end(), one_input.begin(), one_input.end());
    expect_vu_error(
        [&] {
            (void)openrc::execute_scene_block_vu_v1(
                fill,
                openrc::SceneBlockVuExecutionOptionsV1{0U},
                short_limits);
        },
        "fill outputs were not counted toward the vector-write limit");
    const auto fill_exact = openrc::execute_scene_block_vu_v1(
        fill,
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        exact_limits);
    expect(fill_exact.stream.commands[1U].input_vector_count == 1U &&
               fill_exact.total_vector_writes == 4U,
           "fill vector-write limit used input rather than output count");

    std::vector<std::byte> num_zero;
    append_unpack_code(
        num_zero,
        openrc::SceneBlockVifOpcode::unpack_v4_8,
        0U,
        0U,
        true,
        false);
    num_zero.insert(num_zero.end(), 256U * 4U, std::byte{1});
    auto num_limits = kGenerousLimits;
    num_limits.max_vector_writes = 255U;
    expect_vu_error(
        [&] {
            (void)openrc::execute_scene_block_vu_v1(
                num_zero,
                openrc::SceneBlockVuExecutionOptionsV1{0U},
                num_limits);
        },
        "raw NUM zero bypassed the vector-write limit");
    num_limits.max_vector_writes = 256U;
    const auto num_exact = openrc::execute_scene_block_vu_v1(
        num_zero,
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        num_limits);
    expect(num_exact.total_vector_writes == 256U,
           "raw NUM zero did not produce 256 writes");

    std::vector<std::byte> malformed;
    append_unpack_code(
        malformed,
        openrc::SceneBlockVifOpcode::unpack_v3_16,
        0U,
        1U,
        true,
        false);
    const auto short_v3 = payload16({1U, 2U, 3U});
    malformed.insert(malformed.end(), short_v3.begin(), short_v3.end());
    expect_vu_error(
        [&] {
            (void)openrc::execute_scene_block_vu_v1(
                malformed,
                openrc::SceneBlockVuExecutionOptionsV1{0U},
                kGenerousLimits);
        },
        "executor bypassed parser padding validation");
}

} // namespace

int main() {
    try {
        test_control_state_and_empty_memory();
        test_unpack_formats_and_signedness();
        test_strow_and_addition_modes();
        test_v3_unknown_overwrite_and_difference_propagation();
        test_skip_cycle_addressing();
        test_fill_cycle_unknown_and_source_mapping();
        test_fill_difference_poisoning();
        test_tops_wrap_and_cross_command_provenance();
        test_skip_wrap_self_overwrite_provenance();
        test_limits_and_errors();
        std::cout << "SceneBlock VU executor tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SceneBlock VU executor tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
