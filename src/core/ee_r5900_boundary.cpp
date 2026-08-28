#include "openrc/ee_r5900_boundary.hpp"

#include "openrc/elf.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <string>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint16_t kElfTypeExecutable = 2U;
constexpr std::uint32_t kProgramTypeLoad = 1U;
constexpr std::uint32_t kProgramFlagExecute = 0x01U;
constexpr std::uint32_t kSectionTypeNoBits = 8U;
constexpr std::uint32_t kSectionFlagAllocated = 0x02U;
constexpr std::uint32_t kSectionFlagExecutable = 0x04U;
constexpr std::uint32_t kInstructionBytes = 4U;
constexpr std::uint8_t kZeroGpr = 0U;
constexpr std::uint8_t kV1Gpr = 3U;
constexpr std::uint8_t kRaGpr = 31U;
constexpr std::uint64_t kElf32AddressSpaceEnd = UINT64_C(0x1'0000'0000);

[[noreturn]] void fail(const std::string& message) {
    throw EeR5900BoundaryError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string(description) + " overflows its 64-bit range");
    }
    return left + right;
}

void validate_limits(const EeR5900BoundaryLimitsV1 limits) {
    if (limits.max_input_bytes == 0U || limits.max_code_regions == 0U ||
        limits.max_code_bytes == 0U || limits.max_instructions == 0U ||
        limits.max_control_transfers == 0U ||
        limits.max_syscall_sites == 0U) {
        fail("EE/R5900 boundary inventory limits must all be non-zero");
    }
}

[[nodiscard]] std::uint32_t read_le32(const std::span<const std::byte> bytes,
                                      const std::uint64_t offset) {
    const auto index = static_cast<std::size_t>(offset);
    return std::to_integer<std::uint32_t>(bytes[index]) |
           (std::to_integer<std::uint32_t>(bytes[index + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[index + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[index + 3U]) << 24U);
}

[[nodiscard]] EeR5900OpcodeV1 classify_opcode(const std::uint32_t word) {
    if (word == 0U) {
        return EeR5900OpcodeV1::nop;
    }

    const auto primary = static_cast<std::uint8_t>(word >> 26U);
    const auto rs = static_cast<std::uint8_t>((word >> 21U) & 0x1fU);
    const auto rt = static_cast<std::uint8_t>((word >> 16U) & 0x1fU);
    const auto rd = static_cast<std::uint8_t>((word >> 11U) & 0x1fU);
    const auto sa = static_cast<std::uint8_t>((word >> 6U) & 0x1fU);
    const auto function = static_cast<std::uint8_t>(word & 0x3fU);

    switch (primary) {
    case 0x00U:
        if (function == 0x08U && rt == 0U && rd == 0U && sa == 0U) {
            return EeR5900OpcodeV1::jr;
        }
        if (function == 0x09U && rt == 0U && sa == 0U) {
            return EeR5900OpcodeV1::jalr;
        }
        if (function == 0x0cU) {
            return EeR5900OpcodeV1::syscall;
        }
        return EeR5900OpcodeV1::unclassified;
    case 0x01U:
        switch (rt) {
        case 0x00U:
            return EeR5900OpcodeV1::bltz;
        case 0x01U:
            return EeR5900OpcodeV1::bgez;
        case 0x02U:
            return EeR5900OpcodeV1::bltzl;
        case 0x03U:
            return EeR5900OpcodeV1::bgezl;
        case 0x10U:
            return EeR5900OpcodeV1::bltzal;
        case 0x11U:
            return EeR5900OpcodeV1::bgezal;
        case 0x12U:
            return EeR5900OpcodeV1::bltzall;
        case 0x13U:
            return EeR5900OpcodeV1::bgezall;
        default:
            return EeR5900OpcodeV1::unclassified;
        }
    case 0x02U:
        return EeR5900OpcodeV1::j;
    case 0x03U:
        return EeR5900OpcodeV1::jal;
    case 0x04U:
        return EeR5900OpcodeV1::beq;
    case 0x05U:
        return EeR5900OpcodeV1::bne;
    case 0x06U:
        return rt == 0U ? EeR5900OpcodeV1::blez
                        : EeR5900OpcodeV1::unclassified;
    case 0x07U:
        return rt == 0U ? EeR5900OpcodeV1::bgtz
                        : EeR5900OpcodeV1::unclassified;
    case 0x08U:
        return EeR5900OpcodeV1::addi;
    case 0x09U:
        return EeR5900OpcodeV1::addiu;
    case 0x0dU:
        return EeR5900OpcodeV1::ori;
    case 0x10U:
    case 0x11U:
    case 0x12U:
        return rs == 0x08U && rt <= 0x03U
                   ? EeR5900OpcodeV1::coprocessor_branch
                   : EeR5900OpcodeV1::unclassified;
    case 0x14U:
        return EeR5900OpcodeV1::beql;
    case 0x15U:
        return EeR5900OpcodeV1::bnel;
    case 0x16U:
        return rt == 0U ? EeR5900OpcodeV1::blezl
                        : EeR5900OpcodeV1::unclassified;
    case 0x17U:
        return rt == 0U ? EeR5900OpcodeV1::bgtzl
                        : EeR5900OpcodeV1::unclassified;
    case 0x18U:
        return EeR5900OpcodeV1::daddi;
    case 0x19U:
        return EeR5900OpcodeV1::daddiu;
    default:
        return EeR5900OpcodeV1::unclassified;
    }
}

[[nodiscard]] bool is_control_transfer(const EeR5900OpcodeV1 opcode) {
    switch (opcode) {
    case EeR5900OpcodeV1::j:
    case EeR5900OpcodeV1::jal:
    case EeR5900OpcodeV1::jr:
    case EeR5900OpcodeV1::jalr:
    case EeR5900OpcodeV1::beq:
    case EeR5900OpcodeV1::bne:
    case EeR5900OpcodeV1::blez:
    case EeR5900OpcodeV1::bgtz:
    case EeR5900OpcodeV1::beql:
    case EeR5900OpcodeV1::bnel:
    case EeR5900OpcodeV1::blezl:
    case EeR5900OpcodeV1::bgtzl:
    case EeR5900OpcodeV1::bltz:
    case EeR5900OpcodeV1::bgez:
    case EeR5900OpcodeV1::bltzl:
    case EeR5900OpcodeV1::bgezl:
    case EeR5900OpcodeV1::bltzal:
    case EeR5900OpcodeV1::bgezal:
    case EeR5900OpcodeV1::bltzall:
    case EeR5900OpcodeV1::bgezall:
    case EeR5900OpcodeV1::coprocessor_branch:
        return true;
    case EeR5900OpcodeV1::unclassified:
    case EeR5900OpcodeV1::nop:
    case EeR5900OpcodeV1::addi:
    case EeR5900OpcodeV1::addiu:
    case EeR5900OpcodeV1::daddi:
    case EeR5900OpcodeV1::daddiu:
    case EeR5900OpcodeV1::ori:
    case EeR5900OpcodeV1::syscall:
        return false;
    }
    return false;
}

[[nodiscard]] bool is_likely_branch(const EeR5900OpcodeV1 opcode,
                                    const std::uint8_t raw_rt) {
    switch (opcode) {
    case EeR5900OpcodeV1::beql:
    case EeR5900OpcodeV1::bnel:
    case EeR5900OpcodeV1::blezl:
    case EeR5900OpcodeV1::bgtzl:
    case EeR5900OpcodeV1::bltzl:
    case EeR5900OpcodeV1::bgezl:
    case EeR5900OpcodeV1::bltzall:
    case EeR5900OpcodeV1::bgezall:
        return true;
    case EeR5900OpcodeV1::coprocessor_branch:
        return (raw_rt & 0x02U) != 0U;
    default:
        return false;
    }
}

[[nodiscard]] bool is_branch_with_link(const EeR5900OpcodeV1 opcode) {
    return opcode == EeR5900OpcodeV1::bltzal ||
           opcode == EeR5900OpcodeV1::bgezal ||
           opcode == EeR5900OpcodeV1::bltzall ||
           opcode == EeR5900OpcodeV1::bgezall;
}

[[nodiscard]] bool is_decoded_address(
    const std::span<const EeR5900CodeRegionV1> regions,
    const std::uint32_t address) {
    const auto address64 = static_cast<std::uint64_t>(address);
    for (const auto& region : regions) {
        const auto begin = static_cast<std::uint64_t>(region.virtual_address);
        const auto end = begin + region.source_range.size;
        if (address64 < begin || address64 >= end) {
            continue;
        }
        return ((address64 - begin) % kInstructionBytes) == 0U;
    }
    return false;
}

[[nodiscard]] std::uint32_t direct_jump_target(
    const EeR5900InstructionV1& instruction) {
    const auto pc_plus_four = instruction.virtual_address + kInstructionBytes;
    return (pc_plus_four & 0xf0000000U) |
           ((instruction.raw_word & 0x03ffffffU) << 2U);
}

[[nodiscard]] std::uint32_t direct_branch_target(
    const EeR5900InstructionV1& instruction) {
    const auto pc_plus_four = instruction.virtual_address + kInstructionBytes;
    const auto delta = static_cast<std::uint32_t>(
        instruction.signed_immediate * static_cast<std::int32_t>(kInstructionBytes));
    return pc_plus_four + delta;
}

[[nodiscard]] EeR5900ControlTransferV1 make_control_transfer(
    const std::uint64_t instruction_index,
    const EeR5900InstructionV1& instruction,
    const std::span<const EeR5900CodeRegionV1> regions) {
    EeR5900ControlTransferV1 result;
    result.instruction_index = instruction_index;
    result.delay_slot_address =
        instruction.virtual_address + kInstructionBytes;
    result.delay_slot_decoded =
        is_decoded_address(regions, *result.delay_slot_address);
    result.likely = is_likely_branch(instruction.opcode, instruction.rt);

    if (instruction.opcode == EeR5900OpcodeV1::j ||
        instruction.opcode == EeR5900OpcodeV1::jal) {
        result.kind = instruction.opcode == EeR5900OpcodeV1::jal
                          ? EeR5900ControlTransferKindV1::direct_call
                          : EeR5900ControlTransferKindV1::direct_jump;
        result.direct_target_address = direct_jump_target(instruction);
        if (instruction.opcode == EeR5900OpcodeV1::jal) {
            result.link_gpr = kRaGpr;
            result.continuation_address =
                instruction.virtual_address + 2U * kInstructionBytes;
        }
    } else if (instruction.opcode == EeR5900OpcodeV1::jr) {
        result.target_gpr = instruction.rs;
        result.kind = instruction.rs == kRaGpr
                          ? EeR5900ControlTransferKindV1::return_via_ra
                          : EeR5900ControlTransferKindV1::indirect_jump;
    } else if (instruction.opcode == EeR5900OpcodeV1::jalr) {
        result.target_gpr = instruction.rs;
        if (instruction.rd == kZeroGpr) {
            result.kind = EeR5900ControlTransferKindV1::indirect_jump;
        } else {
            result.kind = EeR5900ControlTransferKindV1::indirect_call;
            result.link_gpr = instruction.rd;
            result.continuation_address =
                instruction.virtual_address + 2U * kInstructionBytes;
        }
    } else {
        result.direct_target_address = direct_branch_target(instruction);
        result.continuation_address =
            instruction.virtual_address + 2U * kInstructionBytes;
        if (is_branch_with_link(instruction.opcode)) {
            result.kind = EeR5900ControlTransferKindV1::conditional_call;
            result.link_gpr = kRaGpr;
        } else {
            result.kind = EeR5900ControlTransferKindV1::conditional_branch;
        }
    }

    if (result.direct_target_address) {
        result.direct_target_decoded =
            is_decoded_address(regions, *result.direct_target_address);
    }
    return result;
}

struct SelectorProof {
    std::optional<std::int64_t> value;
    EeR5900SyscallSelectorProofV1 kind =
        EeR5900SyscallSelectorProofV1::none;
};

[[nodiscard]] SelectorProof immediate_v1_selector(
    const EeR5900InstructionV1& instruction) {
    if (instruction.rs != kZeroGpr || instruction.rt != kV1Gpr) {
        return {};
    }

    switch (instruction.opcode) {
    case EeR5900OpcodeV1::addi:
        return {instruction.signed_immediate,
                EeR5900SyscallSelectorProofV1::addi_zero};
    case EeR5900OpcodeV1::addiu:
        return {instruction.signed_immediate,
                EeR5900SyscallSelectorProofV1::addiu_zero};
    case EeR5900OpcodeV1::daddi:
        return {instruction.signed_immediate,
                EeR5900SyscallSelectorProofV1::daddi_zero};
    case EeR5900OpcodeV1::daddiu:
        return {instruction.signed_immediate,
                EeR5900SyscallSelectorProofV1::daddiu_zero};
    case EeR5900OpcodeV1::ori:
        return {instruction.unsigned_immediate,
                EeR5900SyscallSelectorProofV1::ori_zero};
    default:
        return {};
    }
}

[[nodiscard]] bool is_adjacent_in_same_region(
    const EeR5900InstructionV1& left,
    const EeR5900InstructionV1& right) {
    return left.code_region_index == right.code_region_index &&
           left.virtual_address + kInstructionBytes == right.virtual_address &&
           left.source_range.offset + kInstructionBytes ==
               right.source_range.offset;
}

} // namespace

EeR5900BoundaryInventoryV1 inventory_ee_r5900_boundaries_v1(
    const std::span<const std::byte> elf_bytes,
    const EeR5900BoundaryLimitsV1 limits) {
    validate_limits(limits);
    const auto input_size = static_cast<std::uint64_t>(elf_bytes.size());
    if (input_size > limits.max_input_bytes) {
        fail("EE/R5900 boundary input exceeds the caller's byte limit");
    }

    ElfReport elf;
    try {
        elf = inspect_elf(elf_bytes);
    } catch (const ElfError& error) {
        fail("Invalid EE/R5900 ELF: " + std::string(error.what()));
    }
    if (elf.type != kElfTypeExecutable) {
        fail("EE/R5900 boundary inventory requires an executable ELF");
    }
    if (elf.iop_module_info) {
        fail("EE/R5900 boundary inventory does not accept an IOP/IRX module");
    }

    std::vector<bool> excluded_sections(elf.section_headers.size(), false);
    if (elf.dvp_overlay_table) {
        for (const auto& overlay : elf.dvp_overlay_table->overlays) {
            excluded_sections[overlay.overlay_section_index] = true;
            excluded_sections[overlay.code_section_index] = true;
        }
    }

    EeR5900BoundaryInventoryV1 result;
    result.input_bytes = input_size;

    for (std::size_t section_index = 0U;
         section_index < elf.section_headers.size(); ++section_index) {
        const auto& section = elf.section_headers[section_index];
        const auto executable_allocated =
            (section.flags & (kSectionFlagAllocated | kSectionFlagExecutable)) ==
            (kSectionFlagAllocated | kSectionFlagExecutable);
        if (!executable_allocated || section.size == 0U ||
            section.type == kSectionTypeNoBits ||
            excluded_sections[section_index]) {
            continue;
        }
        if ((section.file_offset % kInstructionBytes) != 0U ||
            (section.virtual_address % kInstructionBytes) != 0U ||
            (section.size % kInstructionBytes) != 0U) {
            fail("An EE/R5900 code section is not four-byte aligned");
        }

        const auto file_begin = static_cast<std::uint64_t>(section.file_offset);
        const auto file_end = checked_add(
            file_begin, section.size, "EE/R5900 code-section file range");
        if (file_end > input_size) {
            fail("An EE/R5900 code section exceeds the ELF input");
        }
        const auto virtual_end = checked_add(
            section.virtual_address, section.size,
            "EE/R5900 code-section virtual range");
        if (virtual_end > kElf32AddressSpaceEnd) {
            fail("An EE/R5900 code section exceeds the ELF32 address space");
        }

        std::optional<std::size_t> mapped_program_header;
        for (std::size_t program_index = 0U;
             program_index < elf.program_headers.size(); ++program_index) {
            const auto& program = elf.program_headers[program_index];
            if (program.type != kProgramTypeLoad ||
                (program.flags & kProgramFlagExecute) == 0U) {
                continue;
            }
            const auto program_file_begin =
                static_cast<std::uint64_t>(program.file_offset);
            const auto program_file_end = program_file_begin + program.file_size;
            const auto program_virtual_begin =
                static_cast<std::uint64_t>(program.virtual_address);
            const auto program_virtual_end =
                program_virtual_begin + program.file_size;
            if (file_begin < program_file_begin || file_end > program_file_end ||
                section.virtual_address < program_virtual_begin ||
                virtual_end > program_virtual_end ||
                file_begin - program_file_begin !=
                    static_cast<std::uint64_t>(section.virtual_address) -
                        program_virtual_begin) {
                continue;
            }
            if (mapped_program_header) {
                fail("An EE/R5900 code section maps to multiple executable PT_LOAD segments");
            }
            mapped_program_header = program_index;
        }
        if (!mapped_program_header) {
            fail("An EE/R5900 code section is not consistently file-backed by an executable PT_LOAD segment");
        }
        if (section_index > std::numeric_limits<std::uint16_t>::max() ||
            *mapped_program_header > std::numeric_limits<std::uint16_t>::max()) {
            fail("An EE/R5900 code-region index exceeds its public range");
        }
        if (result.code_regions.size() >= limits.max_code_regions) {
            fail("EE/R5900 code-region count exceeds the caller's limit");
        }

        result.total_code_bytes = checked_add(
            result.total_code_bytes, section.size,
            "Aggregate EE/R5900 code bytes");
        if (result.total_code_bytes > limits.max_code_bytes) {
            fail("Aggregate EE/R5900 code bytes exceed the caller's limit");
        }
        result.code_regions.push_back(EeR5900CodeRegionV1{
            static_cast<std::uint16_t>(section_index),
            static_cast<std::uint16_t>(*mapped_program_header),
            section.name,
            EeR5900ByteRangeV1{section.file_offset, section.size},
            section.virtual_address,
            0U,
            section.size / kInstructionBytes,
        });
    }

    if (result.code_regions.empty()) {
        fail("The ELF contains no file-backed EE/R5900 executable code section");
    }
    std::sort(result.code_regions.begin(), result.code_regions.end(),
              [](const auto& left, const auto& right) {
                  if (left.virtual_address != right.virtual_address) {
                      return left.virtual_address < right.virtual_address;
                  }
                  if (left.source_range.offset != right.source_range.offset) {
                      return left.source_range.offset < right.source_range.offset;
                  }
                  return left.section_index < right.section_index;
              });

    for (std::size_t index = 1U; index < result.code_regions.size(); ++index) {
        const auto& previous = result.code_regions[index - 1U];
        const auto& current = result.code_regions[index];
        const auto previous_end =
            static_cast<std::uint64_t>(previous.virtual_address) +
            previous.source_range.size;
        if (previous_end > current.virtual_address) {
            fail("EE/R5900 code regions overlap in virtual address space");
        }
    }
    std::vector<std::size_t> source_order(result.code_regions.size());
    std::iota(source_order.begin(), source_order.end(), 0U);
    std::sort(source_order.begin(), source_order.end(),
              [&result](const auto left, const auto right) {
                  return result.code_regions[left].source_range.offset <
                         result.code_regions[right].source_range.offset;
              });
    for (std::size_t index = 1U; index < source_order.size(); ++index) {
        const auto& previous = result.code_regions[source_order[index - 1U]];
        const auto& current = result.code_regions[source_order[index]];
        const auto previous_end = previous.source_range.offset +
                                  previous.source_range.size;
        if (previous_end > current.source_range.offset) {
            fail("EE/R5900 code regions overlap in the ELF file");
        }
    }

    if (!is_decoded_address(result.code_regions, elf.entry_point)) {
        fail("The ELF entrypoint is outside the selected EE/R5900 code regions");
    }

    std::uint64_t total_instruction_count = 0U;
    for (auto& region : result.code_regions) {
        region.first_instruction_index = total_instruction_count;
        total_instruction_count = checked_add(
            total_instruction_count, region.instruction_count,
            "Aggregate EE/R5900 instruction count");
    }
    if (total_instruction_count > limits.max_instructions) {
        fail("Aggregate EE/R5900 instruction count exceeds the caller's limit");
    }
    if (total_instruction_count > result.instructions.max_size()) {
        fail("Aggregate EE/R5900 instruction count exceeds the host container range");
    }

    std::uint64_t control_transfer_count = 0U;
    std::uint64_t syscall_site_count = 0U;
    for (const auto& region : result.code_regions) {
        for (std::uint64_t local_index = 0U;
             local_index < region.instruction_count; ++local_index) {
            const auto file_offset = region.source_range.offset +
                                     local_index * kInstructionBytes;
            const auto opcode = classify_opcode(read_le32(elf_bytes, file_offset));
            if (is_control_transfer(opcode)) {
                control_transfer_count = checked_add(
                    control_transfer_count, 1U,
                    "EE/R5900 control-transfer count");
            }
            if (opcode == EeR5900OpcodeV1::syscall) {
                syscall_site_count = checked_add(
                    syscall_site_count, 1U, "EE/R5900 syscall-site count");
            }
        }
    }
    if (control_transfer_count > limits.max_control_transfers) {
        fail("EE/R5900 control-transfer count exceeds the caller's limit");
    }
    if (syscall_site_count > limits.max_syscall_sites) {
        fail("EE/R5900 syscall-site count exceeds the caller's limit");
    }
    if (control_transfer_count > result.control_transfers.max_size() ||
        syscall_site_count > result.syscall_sites.max_size()) {
        fail("EE/R5900 boundary metadata exceeds the host container range");
    }

    result.instructions.reserve(static_cast<std::size_t>(total_instruction_count));
    result.control_transfers.reserve(
        static_cast<std::size_t>(control_transfer_count));
    result.syscall_sites.reserve(static_cast<std::size_t>(syscall_site_count));

    for (std::size_t region_index = 0U;
         region_index < result.code_regions.size(); ++region_index) {
        const auto& region = result.code_regions[region_index];
        for (std::uint64_t local_index = 0U;
             local_index < region.instruction_count; ++local_index) {
            const auto file_offset = region.source_range.offset +
                                     local_index * kInstructionBytes;
            const auto word = read_le32(elf_bytes, file_offset);
            const auto immediate = static_cast<std::uint16_t>(word & 0xffffU);
            const auto signed_immediate = static_cast<std::int32_t>(
                static_cast<std::int16_t>(immediate));
            const auto opcode = classify_opcode(word);
            if (opcode == EeR5900OpcodeV1::unclassified) {
                ++result.unclassified_instruction_count;
            }
            result.instructions.push_back(EeR5900InstructionV1{
                region.virtual_address +
                    static_cast<std::uint32_t>(local_index * kInstructionBytes),
                region_index,
                EeR5900ByteRangeV1{file_offset, kInstructionBytes},
                word,
                opcode,
                static_cast<std::uint8_t>(word >> 26U),
                static_cast<std::uint8_t>((word >> 21U) & 0x1fU),
                static_cast<std::uint8_t>((word >> 16U) & 0x1fU),
                static_cast<std::uint8_t>((word >> 11U) & 0x1fU),
                static_cast<std::uint8_t>((word >> 6U) & 0x1fU),
                static_cast<std::uint8_t>(word & 0x3fU),
                signed_immediate,
                immediate,
            });
        }
    }

    for (std::size_t instruction_index = 0U;
         instruction_index < result.instructions.size(); ++instruction_index) {
        const auto& instruction = result.instructions[instruction_index];
        if (is_control_transfer(instruction.opcode)) {
            auto transfer = make_control_transfer(
                instruction_index, instruction, result.code_regions);
            if (!transfer.delay_slot_decoded) {
                ++result.missing_delay_slot_count;
            }
            if (transfer.direct_target_address &&
                !transfer.direct_target_decoded) {
                ++result.direct_target_outside_code_count;
            }
            result.control_transfers.push_back(std::move(transfer));
        }

        if (instruction.opcode != EeR5900OpcodeV1::syscall) {
            continue;
        }
        EeR5900SyscallSiteV1 syscall;
        syscall.instruction_index = instruction_index;
        syscall.encoded_instruction_code =
            (instruction.raw_word >> 6U) & 0x000fffffU;

        if (instruction_index != 0U) {
            const auto& previous = result.instructions[instruction_index - 1U];
            if (is_adjacent_in_same_region(previous, instruction)) {
                const auto proof = immediate_v1_selector(previous);
                if (proof.value) {
                    syscall.selector_v1 = proof.value;
                    syscall.selector_source_instruction_index =
                        instruction_index - 1U;
                    syscall.selector_proof = proof.kind;

                    if (instruction_index + 2U < result.instructions.size()) {
                        const auto& return_instruction =
                            result.instructions[instruction_index + 1U];
                        const auto& delay_instruction =
                            result.instructions[instruction_index + 2U];
                        if (is_adjacent_in_same_region(
                                instruction, return_instruction) &&
                            is_adjacent_in_same_region(
                                return_instruction, delay_instruction) &&
                            return_instruction.opcode ==
                                EeR5900OpcodeV1::jr &&
                            return_instruction.rs == kRaGpr) {
                            syscall.exact_wrapper_entry_address =
                                previous.virtual_address;
                        }
                    }
                }
            }
        }
        result.syscall_sites.push_back(std::move(syscall));
    }

    return result;
}

} // namespace openrc
