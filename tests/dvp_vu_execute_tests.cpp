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
    expect(first.initial_state.data_memory[7].lanes[3].known_mask == 0U,
           "V3 prefix w lane must remain indeterminate");

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
        test_branch_executes_one_delay_pair();
        test_e_delay_lq_warns_and_commits();
        test_status_latency_and_same_pair_snapshot();
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
