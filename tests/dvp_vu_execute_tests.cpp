#include "openrc/dvp_vu_execute.hpp"

#include "openrc/dvp_vu.hpp"
#include "openrc/elf.hpp"
#include "openrc/scene_block_vif.hpp"
#include "openrc/scene_block_vu.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kUpperNop = 0x000002ffU;
constexpr std::uint32_t kUpperEnd = 0x40000000U;
constexpr std::uint32_t kLowerNop = 0x8000033cU;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void expect_execution_error(Callable&& callable, const char* const message) {
    try {
        callable();
    } catch (const openrc::DvpVuExecutionError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] openrc::DvpVuWordV1 known_word(const std::uint32_t bits) {
    return openrc::DvpVuWordV1{bits, 0xffffffffU};
}

[[nodiscard]] openrc::DvpVuWordV1 known_float(const float value) {
    return known_word(std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] openrc::DvpVuVectorV1
known_vector(const std::array<std::uint32_t, 4>& lanes) {
    openrc::DvpVuVectorV1 result{};
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        result.lanes[lane] = known_word(lanes[lane]);
    }
    return result;
}

[[nodiscard]] openrc::DvpVuVectorV1
known_float_vector(const std::array<float, 4>& lanes) {
    openrc::DvpVuVectorV1 result{};
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        result.lanes[lane] = known_float(lanes[lane]);
    }
    return result;
}

void expect_known_u16(const openrc::DvpVuWordV1& word,
                      const std::uint16_t expected,
                      const char* const message) {
    expect((word.known_mask & 0xffffU) == 0xffffU, message);
    expect(static_cast<std::uint16_t>(word.bits) == expected, message);
}

void expect_known_float(const openrc::DvpVuWordV1& word,
                        const float expected,
                        const char* const message) {
    expect(word.known_mask == 0xffffffffU, message);
    expect(word.bits == std::bit_cast<std::uint32_t>(expected), message);
}

[[nodiscard]] bool has_warning(const openrc::DvpVuExecutionResultV1& result,
                               const openrc::DvpVuExecutionWarningV1 warning) {
    for (const auto candidate : result.warnings) {
        if (candidate == warning) {
            return true;
        }
    }
    return false;
}

void write_le32(std::vector<std::byte>& bytes,
                const std::size_t offset,
                const std::uint32_t value) {
    bytes[offset + 0U] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void append_le16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void append_le32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    const auto old_size = bytes.size();
    bytes.resize(old_size + 4U);
    write_le32(bytes, old_size, value);
}

void pad_to_four(std::vector<std::byte>& bytes) {
    while ((bytes.size() & 3U) != 0U) {
        bytes.push_back(std::byte{0});
    }
}

[[nodiscard]] std::uint32_t vif_code(const std::uint16_t immediate,
                                     const std::uint8_t raw_num,
                                     const openrc::SceneBlockVifOpcode opcode) {
    return static_cast<std::uint32_t>(immediate) |
           (static_cast<std::uint32_t>(raw_num) << 16U) |
           (static_cast<std::uint32_t>(opcode) << 24U);
}

void append_unpack_code(std::vector<std::byte>& bytes,
                        const openrc::SceneBlockVifOpcode opcode,
                        const std::uint16_t address,
                        const bool is_unsigned) {
    auto immediate = static_cast<std::uint16_t>(address & 0x03ffU);
    if (is_unsigned) {
        immediate = static_cast<std::uint16_t>(immediate | 0x4000U);
    }
    append_le32(bytes, vif_code(immediate, 1U, opcode));
}

[[nodiscard]] openrc::SceneBlockVuSnapshotV1 make_bridge_snapshot() {
    std::vector<std::byte> bytes;

    append_unpack_code(bytes,
                       openrc::SceneBlockVifOpcode::unpack_v3_16,
                       7U,
                       true);
    append_le16(bytes, 0x0011U);
    append_le16(bytes, 0x0022U);
    append_le16(bytes, 0x0033U);
    pad_to_four(bytes);

    append_unpack_code(bytes,
                       openrc::SceneBlockVifOpcode::unpack_v4_32,
                       7U,
                       true);
    append_le32(bytes, 0x11111111U);
    append_le32(bytes, 0x22222222U);
    append_le32(bytes, 0x33333333U);
    append_le32(bytes, 0x44444444U);

    const openrc::SceneBlockVuLimits limits{
        openrc::SceneBlockVifLimits{1U << 20U, 4096U, 1U << 20U},
        4096U};
    return openrc::execute_scene_block_vu_v1(
        std::span<const std::byte>{bytes},
        openrc::SceneBlockVuExecutionOptionsV1{0U},
        limits);
}

[[nodiscard]] constexpr std::uint32_t
upper_fields(const std::uint8_t destination_mask,
             const std::uint8_t ft,
             const std::uint8_t fs,
             const std::uint8_t fd) {
    return (static_cast<std::uint32_t>(destination_mask & 0x0fU) << 21U) |
           (static_cast<std::uint32_t>(ft & 0x1fU) << 16U) |
           (static_cast<std::uint32_t>(fs & 0x1fU) << 11U) |
           (static_cast<std::uint32_t>(fd & 0x1fU) << 6U);
}

[[nodiscard]] constexpr std::uint32_t
lower_fields(const std::uint8_t destination_mask,
             const std::uint8_t it,
             const std::uint8_t is,
             const std::uint8_t id = 0U) {
    return (static_cast<std::uint32_t>(destination_mask & 0x0fU) << 21U) |
           (static_cast<std::uint32_t>(it & 0x1fU) << 16U) |
           (static_cast<std::uint32_t>(is & 0x1fU) << 11U) |
           (static_cast<std::uint32_t>(id & 0x1fU) << 6U);
}

[[nodiscard]] constexpr std::uint32_t fsand(const std::uint8_t it,
                                            const std::uint16_t immediate) {
    const auto encoded_immediate =
        static_cast<std::uint32_t>(immediate & 0x07ffU) |
        (static_cast<std::uint32_t>(immediate & 0x0800U) << 10U);
    return 0x2c000000U | (static_cast<std::uint32_t>(it & 0x0fU) << 16U) |
           encoded_immediate;
}

[[nodiscard]] openrc::DvpVuProgramV1
decode_words(const std::vector<std::uint32_t>& lower_words,
             const std::vector<std::uint32_t>& upper_words = {}) {
    expect(upper_words.empty() || upper_words.size() == lower_words.size(),
           "synthetic upper/lower word counts must match");

    std::vector<std::byte> bytes(lower_words.size() * 8U);
    for (std::size_t index = 0; index < lower_words.size(); ++index) {
        write_le32(bytes, index * 8U, lower_words[index]);
        write_le32(bytes,
                   index * 8U + 4U,
                   upper_words.empty() ? kUpperNop : upper_words[index]);
    }

    openrc::ElfDvpOverlay overlay{};
    overlay.overlay_section_index = 1U;
    overlay.code_section_index = 7U;
    overlay.virtual_memory_address = 0U;
    overlay.code_file_offset = 0U;
    overlay.size = static_cast<std::uint32_t>(bytes.size());
    const std::array overlays{overlay};
    const std::array<std::uint16_t, 1> entrypoints{0U};
    const openrc::DvpVuLimits limits{1U << 20U,
                                     64U,
                                     0x4000U,
                                     64U,
                                     4096U,
                                     8192U};
    return openrc::decode_dvp_vu_program_v1(
        std::span<const std::byte>{bytes},
        std::span<const openrc::ElfDvpOverlay>{overlays},
        std::span<const std::uint16_t>{entrypoints},
        limits);
}

[[nodiscard]] openrc::DvpVuExecutionLimitsV1 generous_execution_limits() {
    return openrc::DvpVuExecutionLimitsV1{4096U, 16U, 16U, 4096U};
}

[[nodiscard]] openrc::DvpVuExecutionResultV1 execute(
    const openrc::DvpVuProgramV1& program,
    openrc::DvpVuExecutionStateV1 state,
    const openrc::DvpVuExecutionLimitsV1 limits = generous_execution_limits()) {
    return openrc::execute_dvp_vu_program_v1(
        program,
        std::move(state),
        openrc::DvpVuExecutionOptionsV1{0U, false},
        limits);
}

void test_bridge_replays_exact_prefix_and_preserves_unknown_lane() {
    const auto snapshot = make_bridge_snapshot();
    expect(snapshot.writes.size() == 2U,
           "bridge fixture should contain two ordered VIF writes");

    const auto program = decode_words({kLowerNop, kLowerNop},
                                      {kUpperNop | kUpperEnd, kUpperNop});
    const auto first = openrc::make_scene_block_dvp_vu_invocation_v1(
        snapshot,
        program,
        openrc::SceneBlockDvpVuInvocationOptionsV1{1U, 0U, known_word(23U)},
        openrc::SceneBlockDvpVuBridgeLimitsV1{2U});

    expect(first.applied_write_count == 1U,
           "bridge should replay exactly the requested write prefix");
    expect(first.entrypoint_address == 0U,
           "bridge should preserve the explicitly selected entrypoint");
    expect(first.initial_state.xtop_qword == known_word(23U),
           "bridge must preserve explicit XTOP independently of UNPACK TOPS");
    expect(first.initial_state.data_memory[7].lanes[0] == known_word(0x11U),
           "V3 prefix x lane should be known");
    expect(first.initial_state.data_memory[7].lanes[1] == known_word(0x22U),
           "V3 prefix y lane should be known");
    expect(first.initial_state.data_memory[7].lanes[2] == known_word(0x33U),
           "V3 prefix z lane should be known");
    expect(first.initial_state.data_memory[7].lanes[3] == known_word(0U),
           "V3 prefix w lane should read its zero alignment padding");

    const auto second = openrc::make_scene_block_dvp_vu_invocation_v1(
        snapshot,
        program,
        openrc::SceneBlockDvpVuInvocationOptionsV1{2U, 0U, known_word(29U)},
        openrc::SceneBlockDvpVuBridgeLimitsV1{2U});
    expect(second.applied_write_count == 2U,
           "bridge should replay the complete two-write prefix");
    expect(
        second.initial_state.data_memory[7] ==
            known_vector({0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U}),
        "later prefix write should replace every destination lane");
}

void test_apply_writes_preserves_existing_state_and_is_transactional() {
    const auto snapshot = make_bridge_snapshot();
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.data_memory[7U] = known_vector({90U, 91U, 92U, 93U});
    state.data_memory[8U] = known_vector({80U, 81U, 82U, 83U});
    state.vf[1U] = known_vector({70U, 71U, 72U, 73U});

    openrc::apply_scene_block_dvp_vu_writes_v1(
        state,
        snapshot,
        0U,
        1U,
        openrc::SceneBlockDvpVuBridgeLimitsV1{2U});
    expect(state.data_memory[7U].lanes[0] == known_word(0x11U) &&
               state.data_memory[7U].lanes[1] == known_word(0x22U) &&
               state.data_memory[7U].lanes[2] == known_word(0x33U) &&
               state.data_memory[7U].lanes[3] == known_word(0U),
           "write replay should preserve the known V3 padding lane");
    expect(state.data_memory[8U] ==
               known_vector({80U, 81U, 82U, 83U}) &&
               state.vf[1U] == known_vector({70U, 71U, 72U, 73U}),
           "write replay must preserve unrelated RAM and registers");

    openrc::apply_scene_block_dvp_vu_writes_v1(
        state,
        snapshot,
        1U,
        1U,
        openrc::SceneBlockDvpVuBridgeLimitsV1{2U});
    expect(state.data_memory[7U] ==
               known_vector(
                   {0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U}),
           "a later replay range should overwrite the same destination");

    auto malformed = snapshot;
    malformed.writes[1U].destination_qword =
        static_cast<std::uint16_t>(openrc::kDvpVuDataMemoryQwordCount);
    auto transactional_state = openrc::make_dvp_vu_execution_state_v1();
    transactional_state.data_memory[7U] =
        known_vector({50U, 51U, 52U, 53U});
    expect_execution_error(
        [&transactional_state, &malformed] {
            openrc::apply_scene_block_dvp_vu_writes_v1(
                transactional_state,
                malformed,
                0U,
                2U,
                openrc::SceneBlockDvpVuBridgeLimitsV1{2U});
        },
        "malformed later write should reject the whole replay range");
    expect(transactional_state.data_memory[7U] ==
               known_vector({50U, 51U, 52U, 53U}),
           "failed preflight must not apply an earlier valid write");
}

void test_branch_executes_one_delay_pair() {
    const auto branch = 0x40000000U | 2U;
    const auto iaddi_delay = 0x80000032U | lower_fields(0U, 1U, 0U, 5U);
    const auto iaddi_skipped = 0x80000032U | lower_fields(0U, 1U, 0U, 9U);
    const auto iaddi_target = 0x80000032U | lower_fields(0U, 2U, 1U, 1U);
    const auto program = decode_words(
        {branch, iaddi_delay, iaddi_skipped, iaddi_target, kLowerNop},
        {kUpperNop, kUpperNop, kUpperNop, kUpperNop | kUpperEnd, kUpperNop});

    const auto result =
        execute(program, openrc::make_dvp_vu_execution_state_v1());
    expect(result.termination == openrc::DvpVuTerminationV1::program_end,
           "branch fixture should terminate through E");
    expect(result.instruction_trace ==
               std::vector<std::uint16_t>({0U, 1U, 3U, 4U}),
           "taken branch should execute exactly one delay pair and skip "
           "fallthrough");
    expect_known_u16(result.final_state.vi[1],
                     5U,
                     "branch delay pair should commit its VI write");
    expect_known_u16(
        result.final_state.vi[2],
        6U,
        "branch target should observe the committed delay-pair value");
}

void test_e_delay_lq_warns_and_commits() {
    const auto lq = lower_fields(0x0fU, 2U, 1U);
    const auto program =
        decode_words({kLowerNop, lq}, {kUpperNop | kUpperEnd, kUpperNop});
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.vi[1] = known_word(5U);
    state.data_memory[5] = known_vector({1U, 2U, 3U, 4U});

    const auto result = execute(program, std::move(state));
    expect(result.termination == openrc::DvpVuTerminationV1::program_end,
           "E plus mapped delay pair should end normally");
    expect(result.executed_instruction_pairs == 2U,
           "E must execute exactly one following pair");
    expect(result.final_state.vf[2] == known_vector({1U, 2U, 3U, 4U}),
           "LQ in the E delay pair must execute and commit");
    expect(has_warning(result,
                       openrc::DvpVuExecutionWarningV1::
                           documented_undefined_e_delay_memory),
           "memory operation in E delay pair should produce compatibility "
           "warning");
}

void test_status_latency_and_same_pair_snapshot() {
    const auto subw = upper_fields(0x01U, 26U, 22U, 3U) | 0x07U;
    const auto program = decode_words({fsand(3U, 2U),
                                       kLowerNop,
                                       kLowerNop,
                                       fsand(1U, 2U),
                                       fsand(2U, 2U),
                                       kLowerNop,
                                       kLowerNop},
                                      {subw,
                                       kUpperNop,
                                       kUpperNop,
                                       kUpperNop,
                                       kUpperNop,
                                       kUpperNop | kUpperEnd,
                                       kUpperNop});
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.status_flags = known_word(0U);
    state.vf[22].lanes[3] = known_float(0.0F);
    state.vf[26].lanes[3] = known_float(1.0F);

    const auto result = execute(program, std::move(state));
    expect(result.termination == openrc::DvpVuTerminationV1::program_end,
           "STATUS latency fixture should end normally");
    expect_known_u16(result.final_state.vi[3],
                     0U,
                     "same-pair FSAND must read the old STATUS snapshot");
    expect_known_u16(result.final_state.vi[1],
                     0U,
                     "FSAND three pairs after SUB must still see old STATUS");
    expect_known_u16(result.final_state.vi[2],
                     2U,
                     "FSAND four pairs after SUB must see new sign status bit");
}

void test_add_sub_reference_operand_families() {
    // Generated instruction/operand cases, not external hardware fixtures.
    struct Family {
        std::uint32_t add_opcode;
        std::uint32_t sub_opcode;
        bool scalar;
        bool accumulator;
    };
    constexpr std::array families{
        Family{0x28U, 0x2cU, false, false}, // vector
        Family{0x03U, 0x07U, false, false}, // broadcast w
        Family{0x22U, 0x26U, true, false},  // I
        Family{0x20U, 0x24U, true, false},  // Q
        Family{0x2bcU, 0x2fcU, false, true},
        Family{0x3fU, 0x7fU, false, true},
        Family{0x23eU, 0x27eU, true, true},
        Family{0x23cU, 0x27cU, true, true},
    };
    for (const auto &family : families) {
        for (const bool subtract : {false, true}) {
            const auto instruction =
                upper_fields(0x08U, family.scalar ? 0U : 2U, 1U,
                             family.accumulator ? 0U : 3U) |
                (subtract ? family.sub_opcode : family.add_opcode);
            const auto program = decode_words(
                {kLowerNop, kLowerNop}, {instruction | kUpperEnd, kUpperNop});
            auto state = openrc::make_dvp_vu_execution_state_v1();
            const auto right = subtract ? 0x35000000U : 0xb5000000U;
            state.vf[1U] = known_vector({0x41000000U, 11U, 12U, 13U});
            state.vf[2U] = known_vector({right, 21U, 22U, right});
            const auto sentinel = known_vector({31U, 32U, 33U, 34U});
            state.vf[3U] = sentinel;
            state.accumulator = sentinel;
            state.scalar_i = known_word(right);
            state.scalar_q = known_word(right);
            const auto result = execute(program, std::move(state));
            expect(result.termination == openrc::DvpVuTerminationV1::program_end,
                   "ADD/SUB operand family must remain executable");
            const auto &output = family.accumulator
                                     ? result.final_state.accumulator
                                     : result.final_state.vf[3U];
            auto expected = sentinel;
            expected.lanes[0U] = known_word(0x40ffffffU);
            expect(output == expected,
                   "ADD/SUB family must apply one-guard arithmetic and mask");
            expect((family.accumulator ? result.final_state.vf[3U]
                                       : result.final_state.accumulator) == sentinel,
                   "ADD/SUB must not write the other destination kind");
            expect(has_warning(result,
                               openrc::DvpVuExecutionWarningV1::
                                   vu_add_sub_reference_model) &&
                       !has_warning(result,
                                    openrc::DvpVuExecutionWarningV1::
                                        host_float_approximation),
                   "ADD/SUB must retain its distinct reference qualification");
        }
    }
}

void test_add_reference_unknown_lanes_and_alias() {
    const auto add = upper_fields(0x0bU, 2U, 1U, 1U) | 0x28U;
    const auto program = decode_words(
        {kLowerNop, kLowerNop, kLowerNop, kLowerNop, kLowerNop, kLowerNop},
        {add, kUpperNop, kUpperNop, kUpperNop, kUpperNop | kUpperEnd, kUpperNop});
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.vf[1U] = known_vector({0x40800000U, 0x12345678U, 0x00802307U, 0U});
    state.vf[2U] = known_vector({0xb4800000U, 0U, 0x80802300U, 0U});
    state.vf[2U].lanes[1U].known_mask = 0U; // inactive lane
    state.vf[2U].lanes[3U].known_mask = 0xfffffffeU; // active unknown
    const auto result = execute(program, std::move(state));
    expect(result.final_state.vf[1U].lanes[0U] == known_word(0x407fffffU) &&
               result.final_state.vf[1U].lanes[1U] == known_word(0x12345678U) &&
               result.final_state.vf[1U].lanes[2U] == known_word(0x00600000U) &&
               result.final_state.vf[1U].lanes[3U].known_mask == 0U,
           "aliased ADD must preserve snapshot, inactive data and unknown lanes");
    expect((result.final_state.mac_flags.known_mask & 0xffffU) == 0xeeeeU &&
               (result.final_state.mac_flags.bits & 0xeeeeU) == 0x0202U,
           "underflow remnant must publish Z+U, not erase inactive known flags");
    expect(has_warning(result,
                       openrc::DvpVuExecutionWarningV1::vu_add_sub_reference_model),
           "partly unknown ADD must not become an unqualified success");
}

void test_add_sub_remnant_mac_latency_and_vf0() {
    const auto fmor = [](const std::uint8_t destination) {
        return 0x36000000U | lower_fields(0U, destination, 0U);
    };
    // Discarded VF0 destination still has an arithmetic MAC result.
    const auto sub = upper_fields(0x08U, 2U, 1U, 0U) | 0x2cU;
    const auto program = decode_words(
        {fmor(1U), kLowerNop, kLowerNop, fmor(2U), fmor(3U), kLowerNop},
        {sub, kUpperNop, kUpperNop, kUpperNop, kUpperNop | kUpperEnd, kUpperNop});
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.mac_flags = known_word(0U);
    state.status_flags = known_word(0U);
    state.vf[1U].lanes[0U] = known_word(0x00805407U);
    state.vf[2U].lanes[0U] = known_word(0x00805400U);
    const auto result = execute(program, std::move(state));
    expect_known_u16(result.final_state.vi[1U], 0U,
                     "same-pair MAC read must still use the old flags");
    expect_known_u16(result.final_state.vi[2U], 0U,
                     "reference numeric flags must not arrive one pair early");
    expect_known_u16(result.final_state.vi[3U], 0x0808U,
                     "reference numeric U+Z must arrive after four pairs");
    expect(result.final_state.status_flags.bits == 0x145U,
           "reference result must retain both underflow and zero sticky bits");
    expect(result.final_state.vf[0U] ==
               known_vector({0U, 0U, 0U, 0x3f800000U}),
           "reference arithmetic must not alter architectural VF0");
}

void test_add_reference_does_not_qualify_other_arithmetic() {
    constexpr std::array legacy_opcodes{0x2aU, 0x29U, 0x2dU};
    for (const auto opcode : legacy_opcodes) {
        const auto legacy = upper_fields(0x08U, 2U, 1U, 3U) | opcode;
        const auto program = decode_words(
            {kLowerNop, kLowerNop}, {legacy | kUpperEnd, kUpperNop});
        auto state = openrc::make_dvp_vu_execution_state_v1();
        state.vf[1U] = known_float_vector({2.0F, 0.0F, 0.0F, 0.0F});
        state.vf[2U] = known_float_vector({3.0F, 0.0F, 0.0F, 0.0F});
        state.accumulator = known_float_vector({4.0F, 0.0F, 0.0F, 0.0F});
        const auto result = execute(program, std::move(state));
        expect(has_warning(result,
                           openrc::DvpVuExecutionWarningV1::host_float_approximation) &&
                   !has_warning(result,
                                openrc::DvpVuExecutionWarningV1::
                                    vu_add_sub_reference_model),
               "MUL/MADD/MSUB remain separately host-approximate");
    }
    const auto add = upper_fields(0x08U, 2U, 1U, 3U) | 0x28U;
    const auto mul = upper_fields(0x08U, 2U, 1U, 4U) | 0x2aU;
    const auto mixed = decode_words(
        {kLowerNop, kLowerNop, kLowerNop, kLowerNop},
        {add, add, mul | kUpperEnd, kUpperNop});
    const auto result = execute(mixed, openrc::make_dvp_vu_execution_state_v1());
    expect(result.warnings.size() == 2U &&
               has_warning(result,
                           openrc::DvpVuExecutionWarningV1::host_float_approximation) &&
               has_warning(result,
                           openrc::DvpVuExecutionWarningV1::vu_add_sub_reference_model),
           "mixed programs must retain both deduplicated qualification warnings");
}

void test_loi_reads_previous_i_then_publishes_literal() {
    constexpr auto immediate = 0x80000000U;
    const auto addi = upper_fields(0x08U, 0U, 1U, 2U) | 0x22U;
    const auto muli = upper_fields(0x08U, 0U, 1U, 3U) | 0x1eU;
    const auto program = decode_words(
        {0x40000000U, 0x40e00000U, kLowerNop, kLowerNop},
        {kUpperNop | immediate, addi | immediate,
         muli | kUpperEnd, kUpperNop});
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.vf[1U].lanes[0U] = known_float(3.0F);
    const auto result = execute(program, std::move(state));
    expect(result.termination == openrc::DvpVuTerminationV1::program_end,
           "paired LOI test should terminate normally");
    expect_known_float(result.final_state.vf[2U].lanes[0U], 5.0F,
                       "ADDI must use prior I=2, not paired LOI=7");
    expect_known_float(result.final_state.vf[3U].lanes[0U], 21.0F,
                       "next MULI must use newly published I=7");
    expect(result.final_state.scalar_i == known_float(7.0F),
           "LOI must persist in the scalar register");

    const auto unknown_program = decode_words(
        {0x40e00000U, kLowerNop},
        {addi | immediate | kUpperEnd, muli});
    auto unknown = openrc::make_dvp_vu_execution_state_v1();
    unknown.vf[1U].lanes[0U] = known_float(3.0F);
    const auto unknown_result = execute(unknown_program, std::move(unknown));
    expect(unknown_result.final_state.vf[2U].lanes[0U].known_mask == 0U,
           "paired literal cannot turn unknown prior I into known input");
    expect_known_float(unknown_result.final_state.vf[3U].lanes[0U], 21.0F,
                       "E delay instruction must see the published I");
}

void test_ftoi_instruction_masks_flags_and_knownness() {
    for (const auto instruction : {0x17cU, 0x17dU, 0x17eU, 0x17fU}) {
        const auto program = decode_words(
            {kLowerNop, kLowerNop},
            {upper_fields(0x0bU, 1U, 1U, 0U) | instruction | kUpperEnd,
             upper_fields(0x0fU, 0U, 2U, 0U) | instruction});
        auto state = openrc::make_dvp_vu_execution_state_v1();
        state.vf[1U] = known_vector(
            {0x7fffffffU, 0xfeedbeefU, 0xffffffffU, 0x3f800000U});
        state.vf[1U].lanes[3U].known_mask = 0x7fffffffU;
        state.mac_flags = known_word(0xa55aU);
        state.status_flags = known_word(0x369U);
        state.clip_flags = known_word(0x123456U);
        const auto result = execute(program, std::move(state));
        expect(result.termination == openrc::DvpVuTerminationV1::program_end,
               "FTOI masked in-place fixture must end normally");
        expect(result.final_state.vf[1U].lanes[0U] == known_word(0x7fffffffU) &&
                   result.final_state.vf[1U].lanes[1U] == known_word(0xfeedbeefU) &&
                   result.final_state.vf[1U].lanes[2U] == known_word(0x80000000U),
               "FTOI must saturate by sign and preserve unselected Y");
        expect(result.final_state.vf[1U].lanes[3U].known_mask == 0U,
               "FTOI must not invent missing input bits");
        expect(result.final_state.vf[0U] ==
                   known_vector({0U, 0U, 0U, 0x3f800000U}),
               "FTOI must not modify VF0");
        expect(result.final_state.mac_flags.bits == 0xa55aU &&
                   result.final_state.status_flags.bits == 0x369U &&
                   result.final_state.clip_flags.bits == 0x123456U,
               "FTOI saturation must leave all flags unchanged");
        expect(result.warnings.empty(),
               "integer-only FTOI has no host-float warning");
    }
    const auto itof = decode_words(
        {kLowerNop, kLowerNop},
        {upper_fields(0x0fU, 1U, 2U, 0U) | 0x13cU | kUpperEnd, kUpperNop});
    expect(has_warning(execute(itof, openrc::make_dvp_vu_execution_state_v1()),
                       openrc::DvpVuExecutionWarningV1::host_float_approximation),
           "Unrecovered ITOF must retain its approximation warning");
}

void test_mac_flag_reads_and_partial_information() {
    const auto program = decode_words(
        {0x34000000U | lower_fields(0U, 1U, 10U),
         0x36000000U | lower_fields(0U, 2U, 11U),
         0x30000000U | lower_fields(0U, 3U, 12U),
         0x30000000U | lower_fields(0U, 4U, 13U),
         0x36000000U | lower_fields(0U, 0U, 10U), kLowerNop},
        {kUpperNop, kUpperNop, kUpperNop, kUpperNop,
         kUpperNop | kUpperEnd, kUpperNop});
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.mac_flags = known_word(0xa55aU);
    state.status_flags = known_word(0x123U);
    state.clip_flags = known_word(0x456789U);
    state.vi[10U] = known_word(0x00ffU);
    state.vi[11U] = known_word(0xf000U);
    state.vi[12U] = known_word(0xa55aU);
    state.vi[13U] = known_word(0xa55bU);
    const auto result = execute(program, std::move(state));
    expect_known_u16(result.final_state.vi[1U], 0x005aU, "FMAND result");
    expect_known_u16(result.final_state.vi[2U], 0xf55aU, "FMOR result");
    expect_known_u16(result.final_state.vi[3U], 1U, "FMEQ equality");
    expect_known_u16(result.final_state.vi[4U], 0U, "FMEQ inequality");
    expect_known_u16(result.final_state.vi[0U], 0U, "VI0 remains zero");
    expect(result.final_state.mac_flags.bits == 0xa55aU &&
               result.final_state.status_flags.bits == 0x123U &&
               result.final_state.clip_flags.bits == 0x456789U,
           "MAC flag tests must not write any flags");
    expect(result.warnings.empty(), "integer flag tests need no float warning");

    auto partial = openrc::make_dvp_vu_execution_state_v1();
    partial.mac_flags = {0x8001U, 0x8001U};
    partial.vi[10U] = known_word(0U);
    partial.vi[11U] = known_word(0xffffU);
    partial.vi[12U] = known_word(0U);
    partial.vi[13U] = known_word(0x8001U);
    const auto mixed = execute(program, std::move(partial));
    expect_known_u16(mixed.final_state.vi[1U], 0U,
                     "AND zero must settle unknown bits");
    expect_known_u16(mixed.final_state.vi[2U], 0xffffU,
                     "OR ones must settle unknown bits");
    expect_known_u16(mixed.final_state.vi[3U], 0U,
                     "known mismatch must settle FMEQ false");
    expect(mixed.final_state.vi[4U].known_mask == 0xfffeU,
           "FMEQ unresolved low bit must remain unknown");
}

void test_mac_latency_and_immediate_branch() {
    const auto fmor = [](const std::uint8_t destination) {
        return 0x36000000U | lower_fields(0U, destination, 0U);
    };
    const auto add = upper_fields(0x0eU, 0U, 1U, 31U) | 0x28U;
    const auto branch = 0x50000000U | lower_fields(0U, 4U, 10U) | 2U;
    const auto skipped = 0x80000032U | lower_fields(0U, 5U, 0U, 9U);
    const auto program = decode_words(
        {fmor(2U), kLowerNop, kLowerNop, fmor(3U), fmor(4U),
         branch, kLowerNop, skipped, kLowerNop, kLowerNop},
        {add, kUpperNop, kUpperNop, kUpperNop, kUpperNop,
         kUpperNop, kUpperNop, kUpperNop, kUpperNop | kUpperEnd, kUpperNop});
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.mac_flags = known_word(0xabcdU);
    state.vi[10U] = known_word(0x0aU);
    state.vi[5U] = known_word(0U);
    state.vf[1U] = known_float_vector({0.0F, 1.0F, 0.0F, 0.0F});
    const auto result = execute(program, std::move(state));
    expect_known_u16(result.final_state.vi[2U], 0xabcdU,
                     "same-pair FMOR reads prior MAC");
    expect_known_u16(result.final_state.vi[3U], 0xabcdU,
                     "FMOR before four pairs reads prior MAC");
    expect_known_u16(result.final_state.vi[4U], 0x0aU,
                     "FMOR at four pairs reads X/Z zero flags");
    expect_known_u16(result.final_state.vi[5U], 0U,
                     "branch directly following FMOR sees its result");
    expect(result.instruction_trace ==
               std::vector<std::uint16_t>{0U, 1U, 2U, 3U, 4U, 5U, 6U, 8U, 9U},
           "MAC-read dependent branch must execute its delay slot only");
}

void test_minmax_preserves_selected_raw_encodings() {
    struct Case {
        std::uint32_t left, right, minimum, maximum;
    };
    constexpr std::array cases{
        Case{0U, 0x80000000U, 0x80000000U, 0U},
        Case{0U, 1U, 0U, 1U},
        Case{0x80000000U, 0x80000001U, 0x80000001U, 0x80000000U},
        Case{0x007ffffeU, 0x007fffffU, 0x007ffffeU, 0x007fffffU},
        Case{0x807ffffeU, 0x807fffffU, 0x807fffffU, 0x807ffffeU},
        Case{0x7f800000U, 0x7f7fffffU, 0x7f7fffffU, 0x7f800000U},
        Case{0xff800000U, 0xff7fffffU, 0xff800000U, 0xff7fffffU},
        Case{0xffffffffU, 0x7fffffffU, 0xffffffffU, 0x7fffffffU},
        Case{0x80000001U, 1U, 0x80000001U, 1U},
    };
    const auto program = decode_words(
        {kLowerNop, kLowerNop, kLowerNop},
        {upper_fields(0x08U, 2U, 1U, 3U) | 0x2fU,
         upper_fields(0x08U, 2U, 1U, 4U) | 0x2bU | kUpperEnd, kUpperNop});
    for (const auto &item : cases) {
        for (const auto reverse : {false, true}) {
            auto state = openrc::make_dvp_vu_execution_state_v1();
            state.vf[1U].lanes[0U] = known_word(reverse ? item.right : item.left);
            state.vf[2U].lanes[0U] = known_word(reverse ? item.left : item.right);
            state.mac_flags = known_word(0xa55aU);
            const auto result = execute(program, std::move(state));
            expect(result.final_state.vf[3U].lanes[0U] == known_word(item.minimum) &&
                       result.final_state.vf[4U].lanes[0U] == known_word(item.maximum),
                   "MIN/MAX must select raw bits without arithmetic normalization");
            expect(result.final_state.mac_flags.bits == 0xa55aU,
                   "MIN/MAX must not update MAC flags");
        }
    }
}

void test_queued_mac_producers_and_unknown_branch() {
    const auto fmor = [](const std::uint8_t destination) {
        return 0x36000000U | lower_fields(0U, destination, 0U);
    };
    const auto add = upper_fields(0x0eU, 0U, 1U, 31U) | 0x28U;
    const auto sub = upper_fields(0x0fU, 0U, 0U, 30U) | 0x2cU;
    const auto program = decode_words(
        {kLowerNop, kLowerNop, kLowerNop, kLowerNop, fmor(2U),
         fmor(3U), kLowerNop},
        {add, sub, kUpperNop, kUpperNop, kUpperNop,
         kUpperNop | kUpperEnd, kUpperNop});
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.vf[1U] = known_float_vector({0.0F, 1.0F, 0.0F, 0.0F});
    const auto result = execute(program, std::move(state));
    expect_known_u16(result.final_state.vi[2U], 0x0aU,
                     "FMOR must read first producer before second is ready");
    expect_known_u16(result.final_state.vi[3U], 0x0fU,
                     "next FMOR must read independently queued producer");

    const auto unknown_program = decode_words(
        {0x30000000U | lower_fields(0U, 1U, 10U),
         0x50000000U | lower_fields(0U, 1U, 0U) | 2U,
         0x80000032U | lower_fields(0U, 2U, 0U, 7U),
         kLowerNop, kLowerNop, kLowerNop},
        {kUpperNop, kUpperNop, kUpperNop, kUpperNop,
         kUpperNop | kUpperEnd, kUpperNop});
    auto unknown = openrc::make_dvp_vu_execution_state_v1();
    unknown.mac_flags = {1U, 0xfffeU}; // bit0 differs, but is unknown.
    unknown.vi[10U] = known_word(0U);
    const auto unresolved = execute(unknown_program, std::move(unknown));
    expect(unresolved.termination ==
               openrc::DvpVuTerminationV1::indeterminate_control,
           "unproven FMEQ must not choose a branch path");
    expect_known_u16(unresolved.final_state.vi[2U], 7U,
                     "indeterminate branch still executes its delay pair");
    expect(unresolved.instruction_trace ==
               std::vector<std::uint16_t>{0U, 1U, 2U},
           "indeterminate control must stop before either branch successor");
}

void test_clip_latency_four_pairs() {
    const auto clipw = upper_fields(0x0eU, 10U, 8U, 0U) | 0x1ffU;
    const auto fcand_positive_x = 0x24000001U;
    const auto program = decode_words({kLowerNop,
                                       kLowerNop,
                                       kLowerNop,
                                       kLowerNop,
                                       fcand_positive_x,
                                       kLowerNop,
                                       kLowerNop},
                                      {clipw,
                                       kUpperNop,
                                       kUpperNop,
                                       kUpperNop,
                                       kUpperNop,
                                       kUpperNop | kUpperEnd,
                                       kUpperNop});
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.clip_flags = known_word(0U);
    state.vf[8] = known_float_vector({2.0F, 0.0F, 0.0F, 0.0F});
    state.vf[10].lanes[3] = known_float(1.0F);

    const auto result = execute(program, std::move(state));
    expect(result.termination == openrc::DvpVuTerminationV1::program_end,
           "CLIP latency fixture should end normally");
    expect_known_u16(result.final_state.vi[1],
                     1U,
                     "FCAND four pairs after CLIP should observe +x clip bit");
}

void test_div_q_latency_six_and_seven_pairs() {
    const auto div = 0x800003bcU | lower_fields(0U, 2U, 1U);
    const auto mulq_old = upper_fields(0x08U, 0U, 5U, 3U) | 0x1cU;
    const auto mulq_new = upper_fields(0x08U, 0U, 5U, 4U) | 0x1cU;

    std::vector<std::uint32_t> lower(13U, kLowerNop);
    std::vector<std::uint32_t> upper(13U, kUpperNop);
    lower[0] = div;
    upper[6] = mulq_old;
    upper[7] = mulq_new;
    upper[11] |= kUpperEnd;
    const auto program = decode_words(lower, upper);

    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.scalar_q = known_float(10.0F);
    state.vf[1].lanes[0] = known_float(6.0F);
    state.vf[2].lanes[0] = known_float(2.0F);
    state.vf[5].lanes[0] = known_float(2.0F);

    const auto result = execute(program, std::move(state));
    expect(result.termination == openrc::DvpVuTerminationV1::program_end,
           "DIV/Q latency fixture should end normally");
    expect_known_float(result.final_state.vf[3].lanes[0],
                       20.0F,
                       "MULq six pairs after DIV should use visible old Q");
    expect_known_float(result.final_state.vf[4].lanes[0],
                       6.0F,
                       "MULq seven pairs after DIV should use new quotient");
    expect(has_warning(result,
                       openrc::DvpVuExecutionWarningV1::q_read_before_ready),
           "early MULq should diagnose a Q read before readiness");
}

void test_data_memory_address_wraps() {
    const auto lq_wrapped = lower_fields(0x0fU, 2U, 1U) | 1U;
    const auto program = decode_words(
        {lq_wrapped, kLowerNop, kLowerNop, kLowerNop, kLowerNop, kLowerNop},
        {kUpperNop,
         kUpperNop,
         kUpperNop,
         kUpperNop,
         kUpperNop | kUpperEnd,
         kUpperNop});
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.vi[1] = known_word(1023U);
    state.data_memory[0] = known_vector({7U, 8U, 9U, 10U});

    const auto result = execute(program, std::move(state));
    expect(result.termination == openrc::DvpVuTerminationV1::program_end,
           "wrapped LQ fixture should end normally");
    expect(result.final_state.vf[2] == known_vector({7U, 8U, 9U, 10U}),
           "LQ address 1023+1 should wrap to data-memory qword zero");
}

void test_partial_address_knownness_and_malformed_registers() {
    const auto lq = lower_fields(0x0fU, 2U, 1U);
    const auto lqi = 0x8000037cU | lower_fields(0x0fU, 3U, 1U);
    const auto program =
        decode_words({lq, lqi, kLowerNop},
                     {kUpperNop, kUpperNop | kUpperEnd, kUpperNop});
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.vi[1] = openrc::DvpVuWordV1{0x03ffU, 0x03ffU};
    state.data_memory[1023U] = known_vector({10U, 20U, 30U, 40U});

    const auto result = execute(program, std::move(state));
    expect(result.termination == openrc::DvpVuTerminationV1::program_end,
           "known low ten VI bits should be enough for a VU RAM address");
    expect(result.final_state.vf[2] == known_vector({10U, 20U, 30U, 40U}) &&
               result.final_state.vf[3] == known_vector({10U, 20U, 30U, 40U}),
           "LQ/LQI should use a determinate wrapped address with unknown high "
           "VI bits");
    expect((result.final_state.vi[1].known_mask & 0x03ffU) == 0x03ffU &&
               (result.final_state.vi[1].bits & 0x03ffU) == 0U,
           "LQI should preserve the known low-bit result across wrap");

    auto malformed = decode_words({kLowerNop, kLowerNop},
                                  {kUpperNop | kUpperEnd, kUpperNop});
    malformed.instructions[0U].upper.fs = 32U;
    expect_execution_error(
        [&malformed] {
            (void)execute(malformed, openrc::make_dvp_vu_execution_state_v1());
        },
        "out-of-range public VF operand should be rejected before execution");
}

[[nodiscard]] openrc::DvpVuVectorV1 gif_tag(const std::uint16_t nloop,
                                            const bool eop,
                                            const std::uint8_t format,
                                            const std::uint8_t nreg) {
    const auto low64 = static_cast<std::uint64_t>(nloop & 0x7fffU) |
                       (static_cast<std::uint64_t>(eop) << 15U) |
                       (static_cast<std::uint64_t>(format & 0x03U) << 58U) |
                       (static_cast<std::uint64_t>(nreg & 0x0fU) << 60U);
    return known_vector({static_cast<std::uint32_t>(low64),
                         static_cast<std::uint32_t>(low64 >> 32U),
                         0U,
                         0U});
}

[[nodiscard]] openrc::DvpVuExecutionStateV1
make_multitag_state(const std::uint16_t base_qword) {
    auto state = openrc::make_dvp_vu_execution_state_v1();
    state.vi[1] = known_word(base_qword);
    state.data_memory[base_qword] = gif_tag(1U, false, 0U, 1U);
    state.data_memory[(base_qword + 1U) & 1023U] =
        known_vector({1U, 2U, 3U, 4U});
    state.data_memory[(base_qword + 2U) & 1023U] = gif_tag(1U, true, 0U, 2U);
    state.data_memory[(base_qword + 3U) & 1023U] =
        known_vector({5U, 6U, 7U, 8U});
    state.data_memory[(base_qword + 4U) & 1023U] =
        known_vector({9U, 10U, 11U, 12U});
    return state;
}

[[nodiscard]] openrc::DvpVuProgramV1
make_xgkick_program(const std::size_t event_count) {
    std::vector<std::uint32_t> lower(event_count + 2U, kLowerNop);
    std::vector<std::uint32_t> upper(event_count + 2U, kUpperNop);
    for (std::size_t index = 0; index < event_count; ++index) {
        lower[index] = 0x800006fcU | lower_fields(0U, 0U, 1U);
    }
    upper[event_count] |= kUpperEnd;
    return decode_words(lower, upper);
}

void test_xgkick_copies_multitag_packet() {
    const auto program = make_xgkick_program(1U);
    const auto result = execute(program, make_multitag_state(10U));

    expect(result.termination == openrc::DvpVuTerminationV1::program_end,
           "multi-tag XGKICK fixture should end normally");
    expect(result.xgkick_events.size() == 1U,
           "XGKICK should emit one synchronous diagnostic event");
    const auto& event = result.xgkick_events.front();
    expect(event.base_qword == known_word(10U),
           "XGKICK event should retain known base qword");
    expect(event.tags.size() == 2U,
           "XGKICK should continue parsing GIFtags until EOP");
    expect(event.packet_qwords.size() == 5U,
           "two packed GIFtags and their payloads should copy five qwords");
    expect(event.packet_complete && !event.encountered_indeterminate_tag,
           "fully known EOP packet should be marked complete");
    expect(!event.tags[0].tag.eop &&
               event.tags[0].tag.payload_qword_count == 1U,
           "first packed GIFtag should describe one payload qword");
    expect(event.tags[1].tag.eop && event.tags[1].tag.payload_qword_count == 2U,
           "second packed GIFtag should end after two payload qwords");
    expect(event.tags[0].packet_qword_index == 0U &&
               event.tags[1].packet_qword_index == 2U,
           "GIFtag packet indices do not identify their copied qwords");
    expect(event.tags[0].tag.registers_known &&
               event.tags[0].tag.registers[0U] == 0U &&
               event.tags[1].tag.registers_known &&
               event.tags[1].tag.registers[0U] == 0U &&
               event.tags[1].tag.registers[1U] == 0U,
           "known GIFtag REGS descriptors were not retained");
}

void test_execution_and_xgkick_limits_are_independent() {
    const auto end_program = decode_words({kLowerNop, kLowerNop},
                                          {kUpperNop | kUpperEnd, kUpperNop});
    auto limits = generous_execution_limits();
    limits.max_instruction_pairs = 1U;
    const auto instruction_limited =
        execute(end_program, openrc::make_dvp_vu_execution_state_v1(), limits);
    expect(instruction_limited.termination ==
               openrc::DvpVuTerminationV1::instruction_limit,
           "instruction-pair cap should stop before required E delay pair");

    const auto two_events_program = make_xgkick_program(2U);
    limits = generous_execution_limits();
    limits.max_xgkick_events = 1U;
    const auto event_limited =
        execute(two_events_program, make_multitag_state(10U), limits);
    expect(event_limited.termination ==
               openrc::DvpVuTerminationV1::xgkick_event_limit,
           "second XGKICK should trip the independent event cap");

    const auto one_event_program = make_xgkick_program(1U);
    limits = generous_execution_limits();
    limits.max_xgkick_tags_per_event = 1U;
    const auto tag_limited =
        execute(one_event_program, make_multitag_state(10U), limits);
    expect(tag_limited.termination ==
               openrc::DvpVuTerminationV1::xgkick_tag_limit,
           "second GIFtag should trip the per-event tag cap");

    limits = generous_execution_limits();
    limits.max_xgkick_qwords_per_event = 4U;
    const auto qword_limited =
        execute(one_event_program, make_multitag_state(10U), limits);
    expect(qword_limited.termination ==
               openrc::DvpVuTerminationV1::xgkick_qword_limit,
           "fifth copied packet qword should trip the per-event qword cap");
}

} // namespace

int main() {
    try {
        test_bridge_replays_exact_prefix_and_preserves_unknown_lane();
        test_apply_writes_preserves_existing_state_and_is_transactional();
        test_branch_executes_one_delay_pair();
        test_e_delay_lq_warns_and_commits();
        test_status_latency_and_same_pair_snapshot();
        test_add_sub_reference_operand_families();
        test_add_reference_unknown_lanes_and_alias();
        test_add_sub_remnant_mac_latency_and_vf0();
        test_add_reference_does_not_qualify_other_arithmetic();
        test_loi_reads_previous_i_then_publishes_literal();
        test_ftoi_instruction_masks_flags_and_knownness();
        test_mac_flag_reads_and_partial_information();
        test_mac_latency_and_immediate_branch();
        test_minmax_preserves_selected_raw_encodings();
        test_queued_mac_producers_and_unknown_branch();
        test_clip_latency_four_pairs();
        test_div_q_latency_six_and_seven_pairs();
        test_data_memory_address_wraps();
        test_partial_address_knownness_and_malformed_registers();
        test_xgkick_copies_multitag_packet();
        test_execution_and_xgkick_limits_are_independent();
        std::cout << "dvp vu execute tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "dvp vu execute test failure: " << exception.what()
                  << '\n';
        return 1;
    }
}
