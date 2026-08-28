#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

struct EeR5900BoundaryLimitsV1 {
    std::uint64_t max_input_bytes = 0U;
    std::uint64_t max_code_regions = 0U;
    std::uint64_t max_code_bytes = 0U;
    std::uint64_t max_instructions = 0U;
    std::uint64_t max_control_transfers = 0U;
    std::uint64_t max_syscall_sites = 0U;
};

struct EeR5900ByteRangeV1 {
    std::uint64_t offset = 0U;
    std::uint64_t size = 0U;

    [[nodiscard]] bool operator==(const EeR5900ByteRangeV1&) const = default;
};

struct EeR5900CodeRegionV1 {
    std::uint16_t section_index = 0U;
    std::uint16_t program_header_index = 0U;
    std::string section_name;
    EeR5900ByteRangeV1 source_range;
    std::uint32_t virtual_address = 0U;
    std::uint64_t first_instruction_index = 0U;
    std::uint64_t instruction_count = 0U;
};

// V1 deliberately classifies only the instruction forms needed to inventory
// EE calls, branches, and kernel-system-call sites. Every other word remains
// available as `unclassified`; that name does not imply an invalid encoding.
enum class EeR5900OpcodeV1 : std::uint8_t {
    unclassified = 0,
    nop,
    addi,
    addiu,
    daddi,
    daddiu,
    ori,
    j,
    jal,
    jr,
    jalr,
    beq,
    bne,
    blez,
    bgtz,
    beql,
    bnel,
    blezl,
    bgtzl,
    bltz,
    bgez,
    bltzl,
    bgezl,
    bltzal,
    bgezal,
    bltzall,
    bgezall,
    coprocessor_branch,
    syscall,
};

struct EeR5900InstructionV1 {
    std::uint32_t virtual_address = 0U;
    std::uint64_t code_region_index = 0U;
    EeR5900ByteRangeV1 source_range;
    std::uint32_t raw_word = 0U;
    EeR5900OpcodeV1 opcode = EeR5900OpcodeV1::unclassified;
    std::uint8_t primary_opcode = 0U;
    std::uint8_t rs = 0U;
    std::uint8_t rt = 0U;
    std::uint8_t rd = 0U;
    std::uint8_t sa = 0U;
    std::uint8_t function = 0U;
    std::int32_t signed_immediate = 0;
    std::uint32_t unsigned_immediate = 0U;
};

enum class EeR5900ControlTransferKindV1 : std::uint8_t {
    direct_jump = 0,
    conditional_branch,
    direct_call,
    conditional_call,
    indirect_jump,
    indirect_call,
    return_via_ra,
};

struct EeR5900ControlTransferV1 {
    std::uint64_t instruction_index = 0U;
    EeR5900ControlTransferKindV1 kind =
        EeR5900ControlTransferKindV1::direct_jump;
    std::optional<std::uint32_t> direct_target_address;
    std::optional<std::uint8_t> target_gpr;
    std::optional<std::uint8_t> link_gpr;
    std::optional<std::uint32_t> delay_slot_address;
    std::optional<std::uint32_t> continuation_address;
    bool likely = false;
    bool direct_target_decoded = false;
    bool delay_slot_decoded = false;
};

enum class EeR5900SyscallSelectorProofV1 : std::uint8_t {
    none = 0,
    addi_zero,
    addiu_zero,
    daddi_zero,
    daddiu_zero,
    ori_zero,
};

struct EeR5900SyscallSiteV1 {
    std::uint64_t instruction_index = 0U;
    // Raw bits 25..6 of the SYSCALL instruction. This is not presented as the
    // PlayStation 2 kernel ABI selector, which the EE supplies through GPR v1.
    std::uint32_t encoded_instruction_code = 0U;
    std::optional<std::int64_t> selector_v1;
    std::optional<std::uint64_t> selector_source_instruction_index;
    EeR5900SyscallSelectorProofV1 selector_proof =
        EeR5900SyscallSelectorProofV1::none;
    // Present only for the exact adjacent shape
    //   immediate v1,zero; syscall; jr ra; <decoded delay slot>.
    std::optional<std::uint32_t> exact_wrapper_entry_address;
};

struct EeR5900BoundaryInventoryV1 {
    std::uint64_t input_bytes = 0U;
    std::uint64_t total_code_bytes = 0U;
    std::vector<EeR5900CodeRegionV1> code_regions;
    std::vector<EeR5900InstructionV1> instructions;
    std::vector<EeR5900ControlTransferV1> control_transfers;
    std::vector<EeR5900SyscallSiteV1> syscall_sites;
    std::uint64_t unclassified_instruction_count = 0U;
    std::uint64_t missing_delay_slot_count = 0U;
    std::uint64_t direct_target_outside_code_count = 0U;
};

class EeR5900BoundaryError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Inventories the file-backed EE/R5900 executable sections of one complete
// ELF32 image. DVP overlay placeholder sections and every allocated section
// which backs DVP/VU code are excluded using the parsed overlay table. The
// result is an owning, deterministic linear instruction-word inventory rather
// than a function, reachability, or full-ISA claim.
[[nodiscard]] EeR5900BoundaryInventoryV1
inventory_ee_r5900_boundaries_v1(std::span<const std::byte> elf_bytes,
                                 EeR5900BoundaryLimitsV1 limits);

} // namespace openrc
