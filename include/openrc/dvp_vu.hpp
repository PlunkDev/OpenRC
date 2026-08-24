#pragma once

#include "openrc/elf.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kDvpVuInstructionBytes = 8U;
inline constexpr std::uint32_t kDvpVu1MicroMemoryBytes = 0x4000U;
inline constexpr std::uint16_t kDvpVu1InstructionCount = 0x0800U;
inline constexpr std::uint8_t kDvpVuLaneMaskX = 0x08U;
inline constexpr std::uint8_t kDvpVuLaneMaskY = 0x04U;
inline constexpr std::uint8_t kDvpVuLaneMaskZ = 0x02U;
inline constexpr std::uint8_t kDvpVuLaneMaskW = 0x01U;

struct DvpVuLimits {
    std::uint64_t max_input_bytes = 0;
    std::uint64_t max_overlay_chunks = 0;
    std::uint64_t max_code_bytes = 0;
    std::uint64_t max_entrypoints = 0;
    std::uint64_t max_control_transfers = 0;
    std::uint64_t max_cfg_edges = 0;
};

struct DvpVuByteRangeV1 {
    // Offsets use coordinates in the byte span passed to the decoder.
    std::uint64_t offset = 0;
    std::uint64_t size = 0;

    [[nodiscard]] bool operator==(const DvpVuByteRangeV1&) const = default;
};

struct DvpVuCodeChunkV1 {
    // Index in the caller-provided overlay span. Output chunks are instead
    // ordered by VU virtual address.
    std::uint64_t input_overlay_index = 0;
    std::uint16_t overlay_section_index = 0;
    std::uint16_t code_section_index = 0;
    DvpVuByteRangeV1 source_range;
    // Byte address in VU1 micro memory.
    std::uint32_t virtual_byte_address = 0;
    std::uint64_t first_instruction_index = 0;
    std::uint16_t instruction_count = 0;
};

struct DvpVuInstructionRunV1 {
    // VU microinstruction address, in paired instructions rather than bytes.
    std::uint16_t first_instruction_address = 0;
    std::uint16_t instruction_count = 0;
    std::uint64_t first_instruction_index = 0;
};

enum class DvpVuComponent : std::uint8_t {
    none = 0,
    x = 1,
    y = 2,
    z = 3,
    w = 4,
};

enum class DvpVuUpperOperandMode : std::uint8_t {
    none = 0,
    vector = 1,
    broadcast = 2,
    scalar_i = 3,
    scalar_q = 4,
};

enum class DvpVuUpperOpcode : std::uint8_t {
    unknown = 0,
    nop,
    abs,
    add,
    addi,
    addq,
    adda,
    addai,
    addaq,
    clip,
    ftoi0,
    ftoi4,
    ftoi12,
    ftoi15,
    itof0,
    itof4,
    itof12,
    itof15,
    madd,
    maddi,
    maddq,
    madda,
    maddai,
    maddaq,
    max,
    maxi,
    mini,
    minii,
    msub,
    msubi,
    msubq,
    msuba,
    msubai,
    msubaq,
    mul,
    muli,
    mulq,
    mula,
    mulai,
    mulaq,
    opmula,
    opmsub,
    sub,
    subi,
    subq,
    suba,
    subai,
    subaq,
};

struct DvpVuUpperInstructionV1 {
    DvpVuUpperOpcode opcode = DvpVuUpperOpcode::unknown;
    DvpVuUpperOperandMode operand_mode = DvpVuUpperOperandMode::none;
    // Raw I/E/M/D/T field before its shift into bits 31-27.
    std::uint8_t raw_flags = 0;
    std::uint8_t destination_mask = 0;
    std::uint8_t ft = 0;
    std::uint8_t fs = 0;
    std::uint8_t fd = 0;
    DvpVuComponent broadcast_component = DvpVuComponent::none;
    bool immediate = false;
    bool end = false;
    bool m = false;
    bool d = false;
    bool t = false;
};

enum class DvpVuLowerKind : std::uint8_t {
    instruction = 0,
    immediate_literal = 1,
};

enum class DvpVuLowerOpcode : std::uint8_t {
    unknown = 0,
    nop,
    b,
    bal,
    div,
    fcand,
    fceq,
    fcor,
    fcset,
    fsand,
    fseq,
    fsor,
    fsset,
    iadd,
    iaddi,
    iaddiu,
    iand,
    ibeq,
    ibgez,
    ibgtz,
    iblez,
    ibltz,
    ibne,
    ilw,
    ilwr,
    ior,
    isub,
    isubiu,
    isw,
    iswr,
    jalr,
    jr,
    lq,
    lqd,
    lqi,
    mfir,
    move,
    mr32,
    mtir,
    sq,
    sqd,
    sqi,
    xgkick,
    xitop,
    xtop,
};

struct DvpVuLowerInstructionV1 {
    DvpVuLowerKind kind = DvpVuLowerKind::instruction;
    DvpVuLowerOpcode opcode = DvpVuLowerOpcode::unknown;
    // These preserve the raw 5-bit fields. Integer-register users interpret
    // the architectural register number from the low four bits.
    std::uint8_t destination_mask = 0;
    std::uint8_t it = 0;
    std::uint8_t is = 0;
    std::uint8_t id = 0;
    DvpVuComponent ft_component = DvpVuComponent::none;
    DvpVuComponent fs_component = DvpVuComponent::none;
    std::int32_t signed_immediate = 0;
    std::uint32_t unsigned_immediate = 0;
};

struct DvpVuInstructionPairV1 {
    // Paired-instruction address in the VU1 0x800-entry micro memory.
    std::uint16_t instruction_address = 0;
    // Index into DvpVuProgramV1::code_chunks.
    std::uint64_t code_chunk_index = 0;
    DvpVuByteRangeV1 source_range;
    std::uint32_t raw_lower = 0;
    std::uint32_t raw_upper = 0;
    DvpVuUpperInstructionV1 upper;
    DvpVuLowerInstructionV1 lower;
};

enum class DvpVuMemoryAccessKind : std::uint8_t {
    vector_load = 0,
    vector_store = 1,
    integer_load = 2,
    integer_store = 3,
};

enum class DvpVuMemoryAddressMode : std::uint8_t {
    base_offset = 0,
    base = 1,
    pre_decrement = 2,
    post_increment = 3,
};

struct DvpVuMemoryAccessV1 {
    std::uint64_t instruction_index = 0;
    DvpVuMemoryAccessKind kind = DvpVuMemoryAccessKind::vector_load;
    DvpVuMemoryAddressMode address_mode =
        DvpVuMemoryAddressMode::base_offset;
    std::uint8_t base_vi = 0;
    // VF for vector accesses and VI for integer accesses.
    std::uint8_t value_register = 0;
    std::uint8_t component_mask = 0;
    // Offset is measured in VU1 data-memory qwords. Pre-decrement forms use
    // -1 and post-increment/base forms use zero.
    std::int16_t qword_offset = 0;
};

enum class DvpVuControlTransferKind : std::uint8_t {
    unconditional_branch = 0,
    conditional_branch = 1,
    direct_call = 2,
    indirect_jump = 3,
    indirect_call = 4,
    end_after_delay_slot = 5,
};

enum class DvpVuBranchCondition : std::uint8_t {
    always = 0,
    equal,
    not_equal,
    greater_equal_zero,
    greater_zero,
    less_equal_zero,
    less_zero,
};

struct DvpVuControlTransferV1 {
    std::uint64_t instruction_index = 0;
    DvpVuControlTransferKind kind =
        DvpVuControlTransferKind::unconditional_branch;
    DvpVuBranchCondition condition = DvpVuBranchCondition::always;
    std::optional<std::uint16_t> direct_target_address;
    std::optional<std::uint16_t> delay_slot_address;
    // PC+2 metadata when the instruction has a sequential continuation.
    // For calls this is the architectural return-site candidate; the CFG
    // emits only the call edge and does not infer a return edge.
    std::optional<std::uint16_t> continuation_address;
    std::optional<std::uint8_t> target_vi;
    std::optional<std::uint8_t> link_vi;
    bool direct_target_decoded = false;
    bool delay_slot_decoded = false;
};

struct DvpVuBasicBlockV1 {
    std::uint64_t first_instruction_index = 0;
    std::uint16_t first_instruction_address = 0;
    std::uint16_t instruction_count = 0;
    std::uint64_t first_edge_index = 0;
    std::uint32_t edge_count = 0;
    bool starts_at_entrypoint = false;
};

enum class DvpVuCfgEdgeKind : std::uint8_t {
    fallthrough = 0,
    branch_taken = 1,
    branch_not_taken = 2,
    direct_call = 3,
    indirect_jump = 4,
    indirect_call = 5,
    program_end = 6,
    unresolved_direct = 7,
    unresolved_fallthrough = 8,
};

struct DvpVuCfgEdgeV1 {
    std::uint64_t source_block_index = 0;
    std::optional<std::uint64_t> target_block_index;
    std::optional<std::uint64_t> control_transfer_index;
    std::optional<std::uint16_t> target_instruction_address;
    DvpVuCfgEdgeKind kind = DvpVuCfgEdgeKind::fallthrough;
};

struct DvpVuEntrypointV1 {
    std::uint16_t instruction_address = 0;
    std::uint64_t instruction_index = 0;
    std::uint64_t basic_block_index = 0;
};

struct DvpVuProgramV1 {
    std::uint64_t input_bytes = 0;
    std::uint64_t total_code_bytes = 0;
    std::vector<DvpVuCodeChunkV1> code_chunks;
    std::vector<DvpVuInstructionRunV1> instruction_runs;
    std::vector<DvpVuInstructionPairV1> instructions;
    std::vector<DvpVuEntrypointV1> entrypoints;
    std::vector<DvpVuMemoryAccessV1> memory_accesses;
    std::vector<DvpVuControlTransferV1> control_transfers;
    std::vector<DvpVuBasicBlockV1> basic_blocks;
    std::vector<DvpVuCfgEdgeV1> cfg_edges;
    std::uint64_t unknown_upper_count = 0;
    std::uint64_t unknown_lower_count = 0;
    std::uint64_t unresolved_direct_target_count = 0;
    std::uint64_t missing_delay_slot_count = 0;
    std::uint64_t indirect_control_transfer_count = 0;
};

class DvpVuError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Composes non-overlapping DVP overlay chunks into the bounded VU1 micro
// address space and decodes their instruction pairs. The result owns only
// metadata, raw words, and source ranges; it neither copies code bytes nor
// retains pointers or spans into either input. The context-insensitive CFG
// rejects instruction-run starts, entrypoints, direct targets, or control
// transfers in delay slots, plus multiple control-transfer effects in one
// instruction pair, instead of returning a graph that could bypass required
// delay-slot execution.
[[nodiscard]] DvpVuProgramV1 decode_dvp_vu_program_v1(
    std::span<const std::byte> elf_bytes,
    std::span<const ElfDvpOverlay> overlay_chunks,
    std::span<const std::uint16_t> entrypoints,
    DvpVuLimits limits);

} // namespace openrc
