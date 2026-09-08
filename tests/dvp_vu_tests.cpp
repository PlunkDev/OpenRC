#include "openrc/dvp_vu.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr openrc::DvpVuLimits kGenerousLimits{
    1024U * 1024U,
    64U,
    openrc::kDvpVu1MicroMemoryBytes,
    64U,
    4096U,
    8192U,
};

constexpr std::uint32_t kUpperNop = 0x000002ffU;
constexpr std::uint32_t kLowerNop = 0x8000033cU;

void expect(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void expect_dvp_vu_error(Callable&& callable, const char* message) {
    try {
        callable();
    } catch (const openrc::DvpVuError&) {
        return;
    }
    throw std::runtime_error(message);
}

void write_le32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void write_pair(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t lower,
    const std::uint32_t upper = kUpperNop) {
    write_le32(bytes, offset, lower);
    write_le32(bytes, offset + 4U, upper);
}

[[nodiscard]] openrc::ElfDvpOverlay overlay(
    const std::uint32_t virtual_address,
    const std::uint64_t file_offset,
    const std::uint32_t size,
    const std::uint16_t section_index = 1U) {
    openrc::ElfDvpOverlay result;
    result.overlay_section_index = section_index;
    result.code_section_index = 7U;
    result.virtual_memory_address = virtual_address;
    result.code_file_offset = file_offset;
    result.size = size;
    return result;
}

[[nodiscard]] openrc::DvpVuProgramV1 decode_words(
    const std::span<const std::uint32_t> lower_words,
    const std::span<const std::uint32_t> upper_words = {}) {
    expect(
        upper_words.empty() || upper_words.size() == lower_words.size(),
        "test word arrays have different lengths");
    std::vector<std::byte> bytes(
        lower_words.size() * openrc::kDvpVuInstructionBytes,
        std::byte{0});
    for (std::size_t index = 0U; index < lower_words.size(); ++index) {
        write_pair(
            bytes,
            index * openrc::kDvpVuInstructionBytes,
            lower_words[index],
            upper_words.empty() ? kUpperNop : upper_words[index]);
    }
    const std::array chunks{
        overlay(0U, 0U, static_cast<std::uint32_t>(bytes.size()))};
    return openrc::decode_dvp_vu_program_v1(
        bytes, chunks, std::span<const std::uint16_t>{}, kGenerousLimits);
}

[[nodiscard]] constexpr std::uint32_t upper_fields(
    const std::uint8_t destination_mask,
    const std::uint8_t ft,
    const std::uint8_t fs,
    const std::uint8_t fd) {
    return (static_cast<std::uint32_t>(destination_mask & 0x0fU) << 21U) |
        (static_cast<std::uint32_t>(ft & 0x1fU) << 16U) |
        (static_cast<std::uint32_t>(fs & 0x1fU) << 11U) |
        (static_cast<std::uint32_t>(fd & 0x1fU) << 6U);
}

[[nodiscard]] constexpr std::uint32_t lower_fields(
    const std::uint8_t destination_mask,
    const std::uint8_t it,
    const std::uint8_t is,
    const std::uint8_t id = 0U) {
    return (static_cast<std::uint32_t>(destination_mask & 0x0fU) << 21U) |
        (static_cast<std::uint32_t>(it & 0x1fU) << 16U) |
        (static_cast<std::uint32_t>(is & 0x1fU) << 11U) |
        (static_cast<std::uint32_t>(id & 0x1fU) << 6U);
}

void test_upper_opcode_corpus_and_raw_fields() {
    struct Case {
        std::uint32_t word;
        openrc::DvpVuUpperOpcode opcode;
        openrc::DvpVuUpperOperandMode mode;
        openrc::DvpVuComponent component;
    };

    constexpr auto fields = upper_fields(0x0fU, 9U, 10U, 11U);
    const std::array cases{
        Case{kUpperNop, openrc::DvpVuUpperOpcode::nop,
             openrc::DvpVuUpperOperandMode::none,
             openrc::DvpVuComponent::none},
        Case{fields | 0x28U, openrc::DvpVuUpperOpcode::add,
             openrc::DvpVuUpperOperandMode::vector,
             openrc::DvpVuComponent::none},
        Case{fields | 0x03U, openrc::DvpVuUpperOpcode::add,
             openrc::DvpVuUpperOperandMode::broadcast,
             openrc::DvpVuComponent::w},
        Case{upper_fields(0x0eU, 9U, 10U, 0U) | 0x1ffU,
             openrc::DvpVuUpperOpcode::clip,
             openrc::DvpVuUpperOperandMode::broadcast,
             openrc::DvpVuComponent::w},
        Case{upper_fields(0x0fU, 9U, 10U, 0U) | 0x17cU,
             openrc::DvpVuUpperOpcode::ftoi0,
             openrc::DvpVuUpperOperandMode::vector,
             openrc::DvpVuComponent::none},
        Case{upper_fields(0x0fU, 9U, 10U, 0U) | 0x17dU,
             openrc::DvpVuUpperOpcode::ftoi4,
             openrc::DvpVuUpperOperandMode::vector,
             openrc::DvpVuComponent::none},
        Case{upper_fields(0x0fU, 9U, 10U, 0U) | 0x13cU,
             openrc::DvpVuUpperOpcode::itof0,
             openrc::DvpVuUpperOperandMode::vector,
             openrc::DvpVuComponent::none},
        Case{upper_fields(0x0fU, 9U, 10U, 0U) | 0xbcU | 0x01U,
             openrc::DvpVuUpperOpcode::madda,
             openrc::DvpVuUpperOperandMode::broadcast,
             openrc::DvpVuComponent::y},
        Case{fields | 0x08U | 0x02U,
             openrc::DvpVuUpperOpcode::madd,
             openrc::DvpVuUpperOperandMode::broadcast,
             openrc::DvpVuComponent::z},
        Case{upper_fields(0x0cU, 9U, 10U, 0U) | 0x1bcU | 0x03U,
             openrc::DvpVuUpperOpcode::mula,
             openrc::DvpVuUpperOperandMode::broadcast,
             openrc::DvpVuComponent::w},
        Case{fields | 0x2aU, openrc::DvpVuUpperOpcode::mul,
             openrc::DvpVuUpperOperandMode::vector,
             openrc::DvpVuComponent::none},
        Case{upper_fields(0x0eU, 0U, 10U, 11U) | 0x1cU,
             openrc::DvpVuUpperOpcode::mulq,
             openrc::DvpVuUpperOperandMode::scalar_q,
             openrc::DvpVuComponent::none},
        Case{fields | 0x18U | 0x03U,
             openrc::DvpVuUpperOpcode::mul,
             openrc::DvpVuUpperOperandMode::broadcast,
             openrc::DvpVuComponent::w},
        Case{fields | 0x10U, openrc::DvpVuUpperOpcode::max,
             openrc::DvpVuUpperOperandMode::broadcast,
             openrc::DvpVuComponent::x},
        Case{fields | 0x14U | 0x02U,
             openrc::DvpVuUpperOpcode::mini,
             openrc::DvpVuUpperOperandMode::broadcast,
             openrc::DvpVuComponent::z},
        Case{fields | 0x04U | 0x03U,
             openrc::DvpVuUpperOpcode::sub,
             openrc::DvpVuUpperOperandMode::broadcast,
             openrc::DvpVuComponent::w},
    };

    std::vector<std::uint32_t> lower(cases.size(), kLowerNop);
    std::vector<std::uint32_t> upper;
    upper.reserve(cases.size());
    for (std::size_t index = 0U; index < cases.size(); ++index) {
        const auto& item = cases[index];
        upper.push_back(item.word);
    }
    const auto report = decode_words(lower, upper);
    expect(report.unknown_upper_count == 0U,
           "a confirmed upper corpus opcode decoded as unknown");
    for (std::size_t index = 0U; index < cases.size(); ++index) {
        const auto& decoded = report.instructions[index].upper;
        expect(decoded.opcode == cases[index].opcode,
               "upper opcode classification is wrong");
        expect(decoded.operand_mode == cases[index].mode,
               "upper operand mode is wrong");
        expect(decoded.broadcast_component == cases[index].component,
               "upper broadcast component is wrong");
    }

    const auto& vector = report.instructions[1U].upper;
    expect(vector.destination_mask == 0x0fU && vector.ft == 9U &&
               vector.fs == 10U && vector.fd == 11U,
           "upper raw operand fields were not preserved");
}

void test_lower_opcode_corpus_and_immediates() {
    struct Case {
        std::uint32_t word;
        openrc::DvpVuLowerOpcode opcode;
    };
    constexpr auto registers = lower_fields(0U, 5U, 6U, 7U);
    constexpr std::uint32_t kNegativeThree11 = 0x7fdU;
    const std::array cases{
        Case{0x40000002U, openrc::DvpVuLowerOpcode::b},
        Case{0x42050002U, openrc::DvpVuLowerOpcode::bal},
        Case{0x800003bcU | lower_fields(0U, 3U, 4U),
             openrc::DvpVuLowerOpcode::div},
        Case{0x24012345U, openrc::DvpVuLowerOpcode::fcand},
        Case{0x22023456U, openrc::DvpVuLowerOpcode::fcset},
        Case{0x34050000U | lower_fields(0U, 0U, 6U),
             openrc::DvpVuLowerOpcode::fmand},
        Case{0x30050000U | lower_fields(0U, 0U, 6U),
             openrc::DvpVuLowerOpcode::fmeq},
        Case{0x36050000U | lower_fields(0U, 0U, 6U),
             openrc::DvpVuLowerOpcode::fmor},
        Case{0x2c050345U, openrc::DvpVuLowerOpcode::fsand},
        Case{0x2a200340U, openrc::DvpVuLowerOpcode::fsset},
        Case{0x80000030U | registers, openrc::DvpVuLowerOpcode::iadd},
        Case{0x80000032U | lower_fields(0U, 5U, 6U, 0x1dU),
             openrc::DvpVuLowerOpcode::iaddi},
        Case{0x10000000U | lower_fields(0x0aU, 5U, 6U) | 0x321U,
             openrc::DvpVuLowerOpcode::iaddiu},
        Case{0x80000034U | registers, openrc::DvpVuLowerOpcode::iand},
        Case{0x50000002U | lower_fields(0U, 5U, 6U),
             openrc::DvpVuLowerOpcode::ibeq},
        Case{0x5e000002U | lower_fields(0U, 0U, 6U),
             openrc::DvpVuLowerOpcode::ibgez},
        Case{0x5c000002U | lower_fields(0U, 0U, 6U),
             openrc::DvpVuLowerOpcode::iblez},
        Case{0x58000002U | lower_fields(0U, 0U, 6U),
             openrc::DvpVuLowerOpcode::ibltz},
        Case{0x52000002U | lower_fields(0U, 5U, 6U),
             openrc::DvpVuLowerOpcode::ibne},
        Case{0x08000000U | lower_fields(0x08U, 5U, 6U) |
                 kNegativeThree11,
             openrc::DvpVuLowerOpcode::ilw},
        Case{0x800003feU | lower_fields(0x04U, 5U, 6U),
             openrc::DvpVuLowerOpcode::ilwr},
        Case{0x80000035U | registers, openrc::DvpVuLowerOpcode::ior},
        Case{0x0a000000U | lower_fields(0x0aU, 5U, 6U) | 7U,
             openrc::DvpVuLowerOpcode::isw},
        Case{0x800003ffU | lower_fields(0x0aU, 5U, 6U),
             openrc::DvpVuLowerOpcode::iswr},
        Case{0x48000000U | lower_fields(0U, 0U, 6U),
             openrc::DvpVuLowerOpcode::jr},
        Case{lower_fields(0x0fU, 5U, 6U) | kNegativeThree11,
             openrc::DvpVuLowerOpcode::lq},
        Case{0x8000037cU | lower_fields(0x0fU, 5U, 6U),
             openrc::DvpVuLowerOpcode::lqi},
        Case{0x800003fdU | lower_fields(0x08U, 5U, 6U),
             openrc::DvpVuLowerOpcode::mfir},
        Case{0x8000033cU | lower_fields(0x02U, 5U, 6U),
             openrc::DvpVuLowerOpcode::move},
        Case{0x8000033dU | lower_fields(0x0fU, 5U, 6U),
             openrc::DvpVuLowerOpcode::mr32},
        Case{0x800003fcU | lower_fields(0x03U, 5U, 6U),
             openrc::DvpVuLowerOpcode::mtir},
        Case{kLowerNop, openrc::DvpVuLowerOpcode::nop},
        Case{0x02000000U | lower_fields(0x0fU, 5U, 6U) | 7U,
             openrc::DvpVuLowerOpcode::sq},
        Case{0x8000037dU | lower_fields(0x0fU, 5U, 6U),
             openrc::DvpVuLowerOpcode::sqi},
        Case{0x800006fcU | lower_fields(0U, 0U, 6U),
             openrc::DvpVuLowerOpcode::xgkick},
        Case{0x800006bcU | lower_fields(0U, 5U, 0U),
             openrc::DvpVuLowerOpcode::xtop},
    };

    for (std::size_t index = 0U; index < cases.size(); ++index) {
        const auto& item = cases[index];
        // Isolating each candidate also keeps synthetic control transfers out
        // of other delay slots. The immediate 2 targets address 3.
        const std::array words{item.word, kLowerNop, kLowerNop, kLowerNop};
        const auto report = decode_words(words);
        if (report.unknown_lower_count != 0U) {
            throw std::runtime_error(
                "confirmed lower corpus opcode decoded as unknown at case " +
                std::to_string(index));
        }
        expect(report.instructions[0U].lower.opcode == item.opcode,
               "lower opcode classification is wrong");
    }

    const std::array signed_five_words{
        0x80000032U | lower_fields(0U, 5U, 6U, 0x1dU)};
    const auto signed_five = decode_words(signed_five_words);
    expect(signed_five.instructions[0U].lower.signed_immediate == -3,
           "signed five-bit immediate was not sign extended");
    const std::array split_fifteen_words{
        0x10000000U | lower_fields(0x0aU, 5U, 6U) | 0x321U};
    const auto split_fifteen = decode_words(split_fifteen_words);
    expect(split_fifteen.instructions[0U].lower.unsigned_immediate == 0x5321U,
           "split unsigned 15-bit immediate is wrong");
    const std::array signed_eleven_words{
        0x08000000U | lower_fields(0x08U, 5U, 6U) |
            kNegativeThree11};
    const auto signed_eleven = decode_words(signed_eleven_words);
    expect(signed_eleven.instructions[0U].lower.signed_immediate == -3,
           "signed eleven-bit immediate was not sign extended");
    const std::array component_words{
        0x800003bcU | lower_fields(0U, 3U, 4U)};
    const auto component_report = decode_words(component_words);
    expect(component_report.instructions[0U].lower.ft_component ==
               openrc::DvpVuComponent::x,
           "lower component selector is wrong");
    const std::array fsset_words{0x2a200340U};
    const auto fsset_report = decode_words(fsset_words);
    expect(fsset_report.instructions[0U].lower.opcode ==
                   openrc::DvpVuLowerOpcode::fsset &&
               fsset_report.instructions[0U].lower.unsigned_immediate ==
                   0x0b40U,
           "FSSET did not preserve immediate bit 11");
}

void test_mac_flag_read_encoding() {
    for (const auto opcode : {0x30000000U, 0x34000000U, 0x36000000U}) {
        const auto word = opcode | lower_fields(0U, 13U, 10U);
        const auto report = decode_words(std::array{word});
        expect(report.unknown_lower_count == 0U &&
                   report.instructions[0U].lower.it == 13U &&
                   report.instructions[0U].lower.is == 10U,
               "MAC test must preserve source and destination fields");
        // No lane mask or immediate belongs to these instructions.
        for (const auto reserved : {0x01000000U, 0x00200000U, 0x400U, 1U}) {
            const auto bad = decode_words(std::array{word | reserved});
            expect(bad.unknown_lower_count == 1U,
                   "reserved MAC test bits must not decode as a valid op");
        }
    }
}

void test_out_of_order_chunks_gaps_flags_literal_and_zero_copy() {
    std::vector<std::byte> bytes(80U, std::byte{0});
    constexpr auto all_flags = 0xf8000000U;
    write_pair(bytes, 16U, 0xdeadbeefU, kUpperNop | all_flags);
    write_pair(bytes, 24U, kLowerNop, kUpperNop);
    write_pair(bytes, 64U, kLowerNop, kUpperNop);

    std::vector<openrc::ElfDvpOverlay> chunks{
        overlay(0x20U, 64U, 8U, 12U),
        overlay(0U, 16U, 16U, 11U),
    };
    constexpr std::array<std::uint16_t, 2U> entries{0U, 4U};
    auto report = openrc::decode_dvp_vu_program_v1(
        bytes, chunks, entries, kGenerousLimits);

    expect(report.code_chunks.size() == 2U &&
               report.code_chunks[0U].input_overlay_index == 1U &&
               report.code_chunks[1U].input_overlay_index == 0U,
           "out-of-order chunks were not sorted with provenance");
    expect(report.instruction_runs.size() == 2U &&
               report.instruction_runs[0U].first_instruction_address == 0U &&
               report.instruction_runs[0U].instruction_count == 2U &&
               report.instruction_runs[1U].first_instruction_address == 4U,
           "logical code gaps were not preserved as separate runs");
    const auto& literal = report.instructions[0U];
    expect(literal.raw_lower == 0xdeadbeefU &&
               literal.lower.kind ==
                   openrc::DvpVuLowerKind::immediate_literal &&
               literal.lower.unsigned_immediate == 0xdeadbeefU,
           "upper I did not turn the lower word into a literal");
    expect(literal.upper.immediate && literal.upper.end && literal.upper.m &&
               literal.upper.d && literal.upper.t &&
               literal.upper.raw_flags == 0x1fU,
           "upper flags were not preserved");
    expect(report.unknown_lower_count == 0U,
           "an immediate literal was counted as an unknown lower opcode");
    expect(literal.source_range == openrc::DvpVuByteRangeV1{16U, 8U},
           "absolute instruction source range is wrong");
    expect(report.entrypoints.size() == 2U &&
               report.entrypoints[0U].instruction_address == 0U &&
               report.entrypoints[1U].instruction_address == 4U,
           "explicit entrypoints were not retained");

    std::fill(bytes.begin(), bytes.end(), std::byte{0xff});
    bytes.clear();
    chunks.clear();
    expect(report.instructions[0U].raw_lower == 0xdeadbeefU &&
               report.code_chunks[0U].source_range.offset == 16U,
           "DVP VU report retained a dependency on input storage");
    static_assert(
        std::is_trivially_copyable_v<openrc::DvpVuInstructionPairV1>);
}

void test_typed_memory_accesses() {
    constexpr auto mask_xz =
        openrc::kDvpVuLaneMaskX | openrc::kDvpVuLaneMaskZ;
    const std::array words{
        lower_fields(0x0bU, 17U, 18U) | 0x7fdU,
        0x8000037eU | lower_fields(0x0fU, 3U, 4U),
        0x8000037cU | lower_fields(0x0fU, 5U, 6U),
        0x02000000U | lower_fields(0x0fU, 19U, 20U) | 5U,
        0x8000037fU | lower_fields(0x0fU, 21U, 22U),
        0x8000037dU | lower_fields(0x0fU, 23U, 24U),
        0x08000000U | lower_fields(openrc::kDvpVuLaneMaskX, 21U, 22U) |
            0x7ffU,
        0x800003feU |
            lower_fields(openrc::kDvpVuLaneMaskY, 23U, 24U),
        0x0a000000U | lower_fields(mask_xz, 25U, 26U) | 7U,
        0x800003ffU | lower_fields(mask_xz, 27U, 28U),
    };
    const auto report = decode_words(words);
    expect(report.memory_accesses.size() == words.size(),
           "typed memory-access inventory is incomplete");

    const auto& lq = report.memory_accesses[0U];
    expect(lq.kind == openrc::DvpVuMemoryAccessKind::vector_load &&
               lq.address_mode ==
                   openrc::DvpVuMemoryAddressMode::base_offset &&
               lq.base_vi == 2U && lq.value_register == 17U &&
               lq.component_mask == 0x0bU && lq.qword_offset == -3,
           "LQ typed operands are wrong");
    expect(report.memory_accesses[1U].address_mode ==
                   openrc::DvpVuMemoryAddressMode::pre_decrement &&
               report.memory_accesses[1U].qword_offset == -1 &&
               report.memory_accesses[2U].address_mode ==
                   openrc::DvpVuMemoryAddressMode::post_increment,
           "LQD/LQI address updates are wrong");

    const auto& sq = report.memory_accesses[3U];
    expect(sq.kind == openrc::DvpVuMemoryAccessKind::vector_store &&
               sq.base_vi == 3U && sq.value_register == 20U &&
               sq.qword_offset == 5,
           "SQ base/source register roles are reversed");
    expect(report.memory_accesses[4U].base_vi == 5U &&
               report.memory_accesses[4U].value_register == 22U &&
               report.memory_accesses[5U].base_vi == 7U &&
               report.memory_accesses[5U].value_register == 24U,
           "SQD/SQI register roles or VI normalization are wrong");

    const auto& ilw = report.memory_accesses[6U];
    expect(ilw.kind == openrc::DvpVuMemoryAccessKind::integer_load &&
               ilw.base_vi == 6U && ilw.value_register == 5U &&
               ilw.component_mask == openrc::kDvpVuLaneMaskX &&
               ilw.qword_offset == -1,
           "ILW typed operands are wrong");
    const auto& isw = report.memory_accesses[8U];
    expect(isw.kind == openrc::DvpVuMemoryAccessKind::integer_store &&
               isw.base_vi == 10U && isw.value_register == 9U &&
               isw.component_mask == mask_xz,
           "ISW multi-component mask or register operands are wrong");
    expect(report.memory_accesses[9U].component_mask == mask_xz &&
               report.memory_accesses[9U].address_mode ==
                   openrc::DvpVuMemoryAddressMode::base,
           "ISWR multi-component register form is wrong");
}

void test_control_flow_delay_slots_and_basic_blocks() {
    std::array<std::uint32_t, 10U> lower{};
    lower.fill(kLowerNop);
    lower[0U] = 0x50000003U | lower_fields(0U, 1U, 2U);
    lower[2U] = 0x40000003U;
    lower[4U] = 0x420f0003U;
    lower[6U] = 0x48000000U | lower_fields(0U, 0U, 7U);
    std::array<std::uint32_t, 10U> upper{};
    upper.fill(kUpperNop);
    upper[8U] |= 0x40000000U;
    constexpr std::array<std::uint16_t, 1U> entries{0U};

    std::vector<std::byte> bytes(lower.size() * 8U, std::byte{0});
    for (std::size_t index = 0U; index < lower.size(); ++index) {
        write_pair(bytes, index * 8U, lower[index], upper[index]);
    }
    const std::array chunks{
        overlay(0U, 0U, static_cast<std::uint32_t>(bytes.size()))};
    const auto report = openrc::decode_dvp_vu_program_v1(
        bytes, chunks, entries, kGenerousLimits);

    expect(report.control_transfers.size() == 5U,
           "control-transfer inventory is incomplete");
    const auto& conditional = report.control_transfers[0U];
    expect(conditional.kind ==
                   openrc::DvpVuControlTransferKind::conditional_branch &&
               conditional.condition == openrc::DvpVuBranchCondition::equal &&
               conditional.direct_target_address == 4U &&
               conditional.delay_slot_address == 1U &&
               conditional.continuation_address == 2U &&
               conditional.direct_target_decoded &&
               conditional.delay_slot_decoded,
           "conditional branch target or delay metadata is wrong");
    expect(report.control_transfers[2U].kind ==
                   openrc::DvpVuControlTransferKind::direct_call &&
               report.control_transfers[2U].link_vi == 15U &&
               report.control_transfers[2U].continuation_address == 6U,
           "BAL link or continuation metadata is wrong");
    expect(report.control_transfers[3U].kind ==
                   openrc::DvpVuControlTransferKind::indirect_jump &&
               report.control_transfers[3U].target_vi == 7U,
           "JR target metadata is wrong");
    expect(report.control_transfers[4U].kind ==
                   openrc::DvpVuControlTransferKind::end_after_delay_slot &&
               report.control_transfers[4U].delay_slot_address == 9U,
           "upper E termination metadata is wrong");
    expect(report.unresolved_direct_target_count == 0U &&
               report.missing_delay_slot_count == 0U &&
               report.indirect_control_transfer_count == 1U,
           "control-flow diagnostic totals are wrong");

    expect(report.basic_blocks.size() == 5U,
           "delay-slot blocks were not partitioned at PC+2");
    for (const auto& block : report.basic_blocks) {
        expect(block.instruction_count == 2U,
               "a control block did not include its delay slot");
    }
    expect(report.cfg_edges.size() == 6U,
           "CFG edge inventory has the wrong size");
    expect(report.cfg_edges[0U].kind ==
                   openrc::DvpVuCfgEdgeKind::branch_taken &&
               report.cfg_edges[1U].kind ==
                   openrc::DvpVuCfgEdgeKind::branch_not_taken &&
               report.cfg_edges[3U].kind ==
                   openrc::DvpVuCfgEdgeKind::direct_call &&
               report.cfg_edges[4U].kind ==
                   openrc::DvpVuCfgEdgeKind::indirect_jump &&
               report.cfg_edges[5U].kind ==
                   openrc::DvpVuCfgEdgeKind::program_end,
           "CFG edge kinds are wrong");
    expect(report.entrypoints[0U].basic_block_index == 0U &&
               report.basic_blocks[0U].starts_at_entrypoint,
           "entrypoint was not mapped to its basic block");
}

void test_rejected_ambiguous_delay_slot_cfg_shapes() {
    const auto decode_four = [](const std::array<std::uint32_t, 4U>& lower,
                                const std::span<const std::uint16_t> entries =
                                    std::span<const std::uint16_t>{}) {
        std::vector<std::byte> bytes(lower.size() * 8U, std::byte{0});
        for (std::size_t index = 0U; index < lower.size(); ++index) {
            write_pair(bytes, index * 8U, lower[index]);
        }
        const std::array chunks{
            overlay(0U, 0U, static_cast<std::uint32_t>(bytes.size()))};
        return openrc::decode_dvp_vu_program_v1(
            bytes, chunks, entries, kGenerousLimits);
    };

    expect_dvp_vu_error(
        [&] {
            (void)decode_four({
                0x40000002U,
                0x40000002U,
                kLowerNop,
                kLowerNop,
            });
        },
        "a control transfer in a delay slot produced a CFG");

    expect_dvp_vu_error(
        [&] {
            (void)decode_four({
                0x40000002U,
                kLowerNop,
                0x40000003U,
                kLowerNop,
            });
        },
        "a direct target in a delay slot produced a CFG");

    constexpr std::array<std::uint16_t, 1U> delay_entry{1U};
    expect_dvp_vu_error(
        [&] {
            (void)decode_four({
                0x40000002U,
                kLowerNop,
                kLowerNop,
                kLowerNop,
            }, delay_entry);
        },
        "an entrypoint in a delay slot produced a CFG");

    std::vector<std::byte> wrapped_run_bytes(24U, std::byte{0});
    write_pair(wrapped_run_bytes, 0U, kLowerNop);
    write_pair(wrapped_run_bytes, 8U, kLowerNop);
    // Pair 0x7ff branches to pair 1 and executes pair 0 as its decoded delay
    // slot. Pair 0 is a separate instruction-run start in sorted output.
    write_pair(wrapped_run_bytes, 16U, 0x40000001U);
    const std::array wrapped_run_chunks{
        overlay(0x3ff8U, 16U, 8U, 2U),
        overlay(0U, 0U, 16U, 1U),
    };
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                wrapped_run_bytes, wrapped_run_chunks,
                std::span<const std::uint16_t>{}, kGenerousLimits);
        },
        "an instruction-run start in a wrapped delay slot produced a CFG");

    std::vector<std::byte> dual_control_bytes(16U, std::byte{0});
    write_pair(
        dual_control_bytes, 0U, 0x40000000U,
        kUpperNop | 0x40000000U);
    write_pair(dual_control_bytes, 8U, kLowerNop);
    const std::array dual_control_chunk{overlay(0U, 0U, 16U)};
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                dual_control_bytes, dual_control_chunk,
                std::span<const std::uint16_t>{}, kGenerousLimits);
        },
        "multiple control effects in one pair produced a CFG");
}

void test_unresolved_target_and_missing_delay_slot() {
    const std::array lower{0x40000002U};
    const auto report = decode_words(lower);
    expect(report.control_transfers.size() == 1U &&
               report.unresolved_direct_target_count == 1U &&
               report.missing_delay_slot_count == 1U,
           "unloaded target or delay-slot diagnostics are wrong");
    expect(report.basic_blocks.size() == 1U &&
               report.cfg_edges.size() == 1U &&
               report.cfg_edges[0U].kind ==
                   openrc::DvpVuCfgEdgeKind::unresolved_direct &&
               report.cfg_edges[0U].target_instruction_address == 3U &&
               !report.cfg_edges[0U].target_block_index,
           "unresolved direct branch CFG metadata is wrong");
}

void test_branch_address_wrap_and_unknown_preservation() {
    std::vector<std::byte> bytes(40U, std::byte{0});
    // Address 0 branches backwards through the 0x800-pair PC to 0x7fe.
    write_pair(bytes, 0U, 0x400007fdU, 0x02000000U);
    write_pair(bytes, 8U, kLowerNop, kUpperNop);
    write_pair(bytes, 16U, kLowerNop, kUpperNop);
    // Address 0x7fe branches forward through the PC wrap to address 2.
    write_pair(bytes, 24U, 0x40000003U, kUpperNop);
    write_pair(bytes, 32U, 0x7e000000U, kUpperNop);
    const std::array chunks{
        overlay(0x3ff0U, 24U, 16U, 2U),
        overlay(0U, 0U, 24U, 1U),
    };
    constexpr std::array<std::uint16_t, 2U> entries{0U, 0x7feU};
    const auto report = openrc::decode_dvp_vu_program_v1(
        bytes, chunks, entries, kGenerousLimits);

    expect(report.control_transfers.size() == 2U &&
               report.control_transfers[0U].direct_target_address == 0x7feU &&
               report.control_transfers[0U].delay_slot_address == 1U &&
               report.control_transfers[1U].direct_target_address == 2U &&
               report.control_transfers[1U].delay_slot_address == 0x7ffU &&
               report.control_transfers[1U].continuation_address == 0U,
           "VU1 branch PC wrapping is wrong");
    expect(report.unresolved_direct_target_count == 0U &&
               report.missing_delay_slot_count == 0U,
           "wrapped decoded targets or delay slots were marked unresolved");
    expect(report.unknown_upper_count == 1U &&
               report.instructions[0U].raw_upper == 0x02000000U &&
               report.instructions[0U].upper.opcode ==
                   openrc::DvpVuUpperOpcode::unknown,
           "unknown upper opcode was not preserved");
    expect(report.unknown_lower_count == 1U &&
               report.instructions[4U].raw_lower == 0x7e000000U &&
               report.instructions[4U].lower.opcode ==
                   openrc::DvpVuLowerOpcode::unknown,
           "unknown lower opcode was not preserved");
}

void test_structural_and_limit_rejections() {
    std::vector<std::byte> bytes(32U, std::byte{0});
    write_pair(bytes, 0U, kLowerNop);
    write_pair(bytes, 8U, kLowerNop);
    write_pair(bytes, 16U, kLowerNop);
    write_pair(bytes, 24U, kLowerNop);
    const std::array valid_chunks{overlay(0U, 0U, 32U)};

    for (std::size_t field = 0U; field < 6U; ++field) {
        auto limits = kGenerousLimits;
        switch (field) {
        case 0U:
            limits.max_input_bytes = 0U;
            break;
        case 1U:
            limits.max_overlay_chunks = 0U;
            break;
        case 2U:
            limits.max_code_bytes = 0U;
            break;
        case 3U:
            limits.max_entrypoints = 0U;
            break;
        case 4U:
            limits.max_control_transfers = 0U;
            break;
        case 5U:
            limits.max_cfg_edges = 0U;
            break;
        default:
            break;
        }
        expect_dvp_vu_error(
            [&] {
                (void)openrc::decode_dvp_vu_program_v1(
                    bytes, valid_chunks,
                    std::span<const std::uint16_t>{}, limits);
            },
            "a zero DVP VU limit was accepted");
    }

    auto limits = kGenerousLimits;
    limits.max_input_bytes = bytes.size() - 1U;
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                bytes, valid_chunks, std::span<const std::uint16_t>{},
                limits);
        },
        "input-byte limit was ignored");
    limits = kGenerousLimits;
    limits.max_code_bytes = 31U;
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                bytes, valid_chunks, std::span<const std::uint16_t>{},
                limits);
        },
        "code-byte limit was ignored");

    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                bytes, std::span<const openrc::ElfDvpOverlay>{},
                std::span<const std::uint16_t>{}, kGenerousLimits);
        },
        "empty overlay program was accepted");

    for (const auto& bad_chunk : std::array{
             overlay(0U, 0U, 0U),
             overlay(1U, 0U, 8U),
             overlay(0U, 0U, 9U),
             overlay(openrc::kDvpVu1MicroMemoryBytes - 8U, 0U, 16U),
             overlay(0U, 33U, 8U),
             overlay(0U, 28U, 8U),
         }) {
        const std::array chunks{bad_chunk};
        expect_dvp_vu_error(
            [&] {
                (void)openrc::decode_dvp_vu_program_v1(
                    bytes, chunks, std::span<const std::uint16_t>{},
                    kGenerousLimits);
            },
            "malformed DVP VU chunk was accepted");
    }

    const std::array overlapping{
        overlay(0U, 0U, 16U, 1U),
        overlay(8U, 16U, 16U, 2U),
    };
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                bytes, overlapping, std::span<const std::uint16_t>{},
                kGenerousLimits);
        },
        "overlapping DVP VU chunks were accepted");

    limits = kGenerousLimits;
    limits.max_overlay_chunks = 1U;
    const std::array two_chunks{
        overlay(0U, 0U, 8U, 1U),
        overlay(8U, 8U, 8U, 2U),
    };
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                bytes, two_chunks, std::span<const std::uint16_t>{},
                limits);
        },
        "overlay-chunk limit was ignored");

    constexpr std::array<std::uint16_t, 2U> duplicate_entries{0U, 0U};
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                bytes, valid_chunks, duplicate_entries, kGenerousLimits);
        },
        "duplicate entrypoint was accepted");
    constexpr std::array<std::uint16_t, 1U> outside_entry{0x800U};
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                bytes, valid_chunks, outside_entry, kGenerousLimits);
        },
        "out-of-range entrypoint was accepted");
    constexpr std::array<std::uint16_t, 1U> gap_entry{5U};
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                bytes, valid_chunks, gap_entry, kGenerousLimits);
        },
        "entrypoint in unloaded code was accepted");
    limits = kGenerousLimits;
    limits.max_entrypoints = 1U;
    constexpr std::array<std::uint16_t, 2U> two_entries{0U, 1U};
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                bytes, valid_chunks, two_entries, limits);
        },
        "entrypoint limit was ignored");

    std::vector<std::byte> branch_bytes(16U, std::byte{0});
    // Target address 2 is unloaded while delay address 1 is decoded. This
    // exercises the edge limit rather than the ambiguous-delay validation.
    write_pair(branch_bytes, 0U, 0x50000001U);
    write_pair(branch_bytes, 8U, kLowerNop);
    const std::array branch_chunk{overlay(0U, 0U, 16U)};

    std::vector<std::byte> two_branch_bytes(32U, std::byte{0});
    write_pair(two_branch_bytes, 0U, 0x50000001U);
    write_pair(two_branch_bytes, 8U, kLowerNop);
    write_pair(two_branch_bytes, 16U, 0x500007fdU);
    write_pair(two_branch_bytes, 24U, kLowerNop);
    const std::array two_branch_chunk{overlay(0U, 0U, 32U)};
    limits = kGenerousLimits;
    limits.max_control_transfers = 1U;
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                two_branch_bytes, two_branch_chunk,
                std::span<const std::uint16_t>{}, limits);
        },
        "control-transfer limit was ignored");
    limits = kGenerousLimits;
    limits.max_cfg_edges = 1U;
    expect_dvp_vu_error(
        [&] {
            (void)openrc::decode_dvp_vu_program_v1(
                branch_bytes, branch_chunk,
                std::span<const std::uint16_t>{}, limits);
        },
        "CFG edge limit was ignored");
}

} // namespace

int main() {
    try {
        test_upper_opcode_corpus_and_raw_fields();
        test_lower_opcode_corpus_and_immediates();
        test_mac_flag_read_encoding();
        test_out_of_order_chunks_gaps_flags_literal_and_zero_copy();
        test_typed_memory_accesses();
        test_control_flow_delay_slots_and_basic_blocks();
        test_rejected_ambiguous_delay_slot_cfg_shapes();
        test_unresolved_target_and_missing_delay_slot();
        test_branch_address_wrap_and_unknown_preservation();
        test_structural_and_limit_rejections();
        std::cout << "DVP VU tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DVP VU tests failed: " << error.what() << '\n';
        return 1;
    }
}
