#include "openrc/elf.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::size_t kElf32HeaderSize = 52;
constexpr std::size_t kElf32ProgramHeaderSize = 32;
constexpr std::size_t kElf32SectionHeaderSize = 40;
constexpr std::uint8_t kElfClass32 = 1;
constexpr std::uint8_t kElfDataLittleEndian = 1;
constexpr std::uint8_t kElfCurrentVersion = 1;
constexpr std::uint16_t kMachineMips = 8;
constexpr std::uint32_t kElfVersionCurrent = 1;
constexpr std::uint32_t kProgramTypeLoad = 1;
constexpr std::uint32_t kSectionTypeSymbolTable = 2;
constexpr std::uint32_t kSectionTypeStringTable = 3;
constexpr std::uint32_t kSectionTypeNoBits = 8;
constexpr std::uint32_t kSectionTypeRel = 9;
constexpr std::uint32_t kSectionTypeDynamicSymbolTable = 11;
constexpr std::uint32_t kSectionTypeIopModule = 0x70000080U;
constexpr std::uint32_t kSectionFlagAllocated = 0x2;
constexpr std::uint32_t kSectionFlagExecutable = 0x4;
constexpr std::uint16_t kExtendedProgramHeaderCount = 0xffffU;
constexpr std::uint16_t kExtendedSectionNameIndex = 0xffffU;
constexpr std::uint64_t kElf32AddressSpaceEnd = std::uint64_t{1} << 32U;
constexpr std::uint16_t kMaximumProgramHeaderCount = 4096;
constexpr std::uint16_t kMaximumSectionHeaderCount = 16384;
constexpr std::uint32_t kMaximumSectionNameTableSize = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumSectionNameLength = 4096;
constexpr std::uint32_t kElf32RelocationSize = 8;
constexpr std::uint32_t kMaximumRelocationEntryCount = 1'000'000;
constexpr std::uint32_t kMaximumIopModuleSectionSize = 4096;
constexpr std::size_t kIopModuleFixedSize = 26;
constexpr std::size_t kMaximumIopModuleNameLength = 255;
constexpr std::uint32_t kIopImportMagic = 0x41e00000U;
constexpr std::uint32_t kMipsJumpReturnAddress = 0x03e00008U;
constexpr std::uint32_t kMipsAddImmediateUnsignedZero = 0x24000000U;
constexpr std::size_t kIopImportHeaderSize = 20;
constexpr std::size_t kIopImportStubSize = 8;
constexpr std::size_t kMaximumIopImportLibraries = 4096;
constexpr std::size_t kMaximumIopImportStubsPerLibrary = 65536;
constexpr std::size_t kMaximumIopImportStubsTotal = 1'000'000;
constexpr std::uint64_t kMaximumIopImportScanBytes = 256ULL * 1024ULL * 1024ULL;

[[nodiscard]] std::uint8_t byte_value(const std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) {
    return static_cast<std::uint16_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint16_t>(byte_value(bytes[offset + 1])) << 8U);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3])) << 24U);
}

void require_file_range(
    const std::uint64_t offset,
    const std::uint64_t size,
    const std::size_t file_size,
    const char* description) {
    const auto bounded_file_size = static_cast<std::uint64_t>(file_size);
    if (offset > bounded_file_size || size > bounded_file_size - offset) {
        throw ElfError(std::string(description) + " points outside the ELF file");
    }
}

void require_table_range(
    const std::uint32_t offset,
    const std::uint16_t entry_size,
    const std::uint16_t entry_count,
    const std::size_t file_size,
    const char* description) {
    const auto table_size =
        static_cast<std::uint64_t>(entry_size) * static_cast<std::uint64_t>(entry_count);
    require_file_range(offset, table_size, file_size, description);
}

void inspect_program_headers(
    const std::span<const std::byte> bytes,
    const std::uint32_t table_offset,
    const std::uint16_t entry_size,
    const std::uint16_t entry_count,
    ElfReport& report) {
    report.program_headers.reserve(entry_count);

    for (std::uint32_t index = 0; index < entry_count; ++index) {
        const auto offset = static_cast<std::size_t>(
            static_cast<std::uint64_t>(table_offset) +
            static_cast<std::uint64_t>(index) * entry_size);
        const auto type = read_le32(bytes, offset);
        const auto file_offset = read_le32(bytes, offset + 4);
        const auto virtual_address = read_le32(bytes, offset + 8);
        const auto file_byte_count = read_le32(bytes, offset + 16);
        const auto memory_byte_count = read_le32(bytes, offset + 20);
        const auto flags = read_le32(bytes, offset + 24);
        const auto alignment = read_le32(bytes, offset + 28);

        require_file_range(file_offset, file_byte_count, bytes.size(), "Program segment");
        report.program_headers.push_back(ElfProgramHeader{
            type,
            file_offset,
            virtual_address,
            file_byte_count,
            memory_byte_count,
            flags,
            alignment,
        });
        if (type != kProgramTypeLoad) {
            continue;
        }

        if (file_byte_count > memory_byte_count) {
            throw ElfError("Loadable segment has a file size larger than its memory size");
        }

        ++report.loadable_segment_count;
        if (memory_byte_count == 0) {
            continue;
        }

        const auto range_begin = static_cast<std::uint64_t>(virtual_address);
        const auto range_end = range_begin + memory_byte_count;
        if (range_end > kElf32AddressSpaceEnd) {
            throw ElfError("Loadable segment exceeds the ELF32 virtual address space");
        }

        if (!report.loadable_virtual_address_range) {
            report.loadable_virtual_address_range = ElfVirtualAddressRange{range_begin, range_end};
        } else {
            report.loadable_virtual_address_range->begin =
                std::min(report.loadable_virtual_address_range->begin, range_begin);
            report.loadable_virtual_address_range->end =
                std::max(report.loadable_virtual_address_range->end, range_end);
        }
    }
}

void inspect_section_headers(
    const std::span<const std::byte> bytes,
    const std::uint32_t table_offset,
    const std::uint16_t entry_size,
    const std::uint16_t entry_count,
    const std::uint16_t section_name_index,
    ElfReport& report) {
    report.section_headers.reserve(entry_count);

    for (std::uint32_t index = 0; index < entry_count; ++index) {
        const auto offset = static_cast<std::size_t>(
            static_cast<std::uint64_t>(table_offset) +
            static_cast<std::uint64_t>(index) * entry_size);

        ElfSectionHeader section;
        section.name_offset = read_le32(bytes, offset);
        section.type = read_le32(bytes, offset + 4);
        section.flags = read_le32(bytes, offset + 8);
        section.virtual_address = read_le32(bytes, offset + 12);
        section.file_offset = read_le32(bytes, offset + 16);
        section.size = read_le32(bytes, offset + 20);
        section.link = read_le32(bytes, offset + 24);
        section.info = read_le32(bytes, offset + 28);
        section.alignment = read_le32(bytes, offset + 32);
        section.entry_size = read_le32(bytes, offset + 36);
        if (section.type != kSectionTypeNoBits) {
            require_file_range(section.file_offset, section.size, bytes.size(), "Section");
        }
        report.section_headers.push_back(std::move(section));
    }

    if (entry_count == 0) {
        return;
    }

    if (section_name_index == 0) {
        for (const auto& section : report.section_headers) {
            if (section.name_offset != 0) {
                throw ElfError("ELF sections have names but no section-name string table");
            }
        }
        return;
    }

    const auto& string_table_section = report.section_headers[section_name_index];
    if (string_table_section.type != kSectionTypeStringTable) {
        throw ElfError("ELF section-name table is not a string-table section");
    }
    if (string_table_section.size == 0) {
        throw ElfError("ELF section-name string table is empty");
    }
    if (string_table_section.size > kMaximumSectionNameTableSize) {
        throw ElfError("ELF section-name string table exceeds the supported size limit");
    }

    const auto string_table = bytes.subspan(
        static_cast<std::size_t>(string_table_section.file_offset),
        static_cast<std::size_t>(string_table_section.size));
    if (byte_value(string_table.front()) != 0) {
        throw ElfError("ELF section-name string table does not start with a null byte");
    }

    for (auto& section : report.section_headers) {
        if (section.name_offset >= string_table.size()) {
            throw ElfError("ELF section name offset is outside the section-name string table");
        }

        const auto name_begin = string_table.begin() + section.name_offset;
        const auto name_end = std::find(name_begin, string_table.end(), std::byte{0});
        if (name_end == string_table.end()) {
            throw ElfError("ELF section name is not null-terminated");
        }
        const auto name_size = static_cast<std::size_t>(name_end - name_begin);
        if (name_size > kMaximumSectionNameLength) {
            throw ElfError("ELF section name exceeds the supported length limit");
        }
        section.name.reserve(name_size);
        for (auto iterator = name_begin; iterator != name_end; ++iterator) {
            section.name.push_back(static_cast<char>(byte_value(*iterator)));
        }
    }

    if (byte_value(string_table.back()) != 0) {
        throw ElfError("ELF section-name string table does not end with a null byte");
    }
}

[[nodiscard]] std::array<std::uint64_t, 3> allocated_section_sizes(
    const ElfReport& report) {
    std::array<std::uint64_t, 3> sizes{};
    for (const auto& section : report.section_headers) {
        if ((section.flags & kSectionFlagAllocated) == 0 ||
            section.type == kSectionTypeIopModule) {
            continue;
        }
        if (section.type == kSectionTypeNoBits) {
            sizes[2] += section.size;
        } else if ((section.flags & kSectionFlagExecutable) != 0) {
            sizes[0] += section.size;
        } else {
            sizes[1] += section.size;
        }
    }
    return sizes;
}

void inspect_iop_module_info(
    const std::span<const std::byte> bytes,
    ElfReport& report) {
    std::optional<std::size_t> module_section_index;
    for (std::size_t index = 0; index < report.section_headers.size(); ++index) {
        const auto& section = report.section_headers[index];
        const auto has_canonical_name = section.name == ".iopmod";
        const auto has_iop_module_type = section.type == kSectionTypeIopModule;
        if (has_canonical_name && !has_iop_module_type) {
            throw ElfError(".iopmod does not use the expected IOP module section type");
        }
        if (!has_iop_module_type) {
            continue;
        }
        if (report.section_name_table_index != 0 && !has_canonical_name) {
            throw ElfError("IOP module section does not use the canonical .iopmod name");
        }
        if (module_section_index) {
            throw ElfError("ELF contains more than one IOP module section");
        }
        module_section_index = index;
    }
    if (!module_section_index) {
        return;
    }

    const auto& section = report.section_headers[*module_section_index];
    if (section.size < kIopModuleFixedSize + 1) {
        throw ElfError(".iopmod is too small to contain its fixed fields and module name");
    }
    if (section.size > kMaximumIopModuleSectionSize) {
        throw ElfError(".iopmod exceeds the supported size limit");
    }

    const auto module = bytes.subspan(
        static_cast<std::size_t>(section.file_offset),
        static_cast<std::size_t>(section.size));
    const auto name_bytes = module.subspan(kIopModuleFixedSize);
    const auto name_end = std::find(name_bytes.begin(), name_bytes.end(), std::byte{0});
    if (name_end == name_bytes.end()) {
        throw ElfError(".iopmod module name is not null-terminated");
    }
    const auto name_size = static_cast<std::size_t>(name_end - name_bytes.begin());
    if (name_size == 0) {
        throw ElfError(".iopmod module name is empty");
    }
    if (name_size > kMaximumIopModuleNameLength) {
        throw ElfError(".iopmod module name exceeds the supported length limit");
    }
    for (auto iterator = name_bytes.begin(); iterator != name_end; ++iterator) {
        const auto character = byte_value(*iterator);
        if (character < 0x20U || character > 0x7eU) {
            throw ElfError(".iopmod module name contains a non-printable byte");
        }
    }
    if (std::any_of(name_end + 1, name_bytes.end(), [](const std::byte value) {
            return value != std::byte{0};
        })) {
        throw ElfError(".iopmod contains nonzero data after its module name");
    }

    IopModuleInfo info;
    info.section_index = static_cast<std::uint16_t>(*module_section_index);
    info.module_info_address = read_le32(module, 0);
    info.entry_point = read_le32(module, 4);
    info.global_pointer = read_le32(module, 8);
    info.text_size = read_le32(module, 12);
    info.data_size = read_le32(module, 16);
    info.bss_size = read_le32(module, 20);
    info.version = read_le16(module, 24);
    info.name.reserve(name_size);
    for (auto iterator = name_bytes.begin(); iterator != name_end; ++iterator) {
        info.name.push_back(static_cast<char>(byte_value(*iterator)));
    }

    const auto allocated_sizes = allocated_section_sizes(report);
    info.allocated_text_size = allocated_sizes[0];
    info.allocated_data_size = allocated_sizes[1];
    info.allocated_bss_size = allocated_sizes[2];
    info.text_size_matches_allocated_sections =
        info.text_size == info.allocated_text_size;
    info.data_size_matches_allocated_sections =
        info.data_size == info.allocated_data_size;
    info.bss_size_matches_allocated_sections =
        info.bss_size == info.allocated_bss_size;
    report.iop_module_info = std::move(info);
}

void inspect_relocations(
    const std::span<const std::byte> bytes,
    ElfReport& report) {
    for (std::size_t section_index = 0;
         section_index < report.section_headers.size();
         ++section_index) {
        const auto& section = report.section_headers[section_index];
        if (section.type != kSectionTypeRel) {
            continue;
        }
        if (section.link >= report.section_headers.size()) {
            throw ElfError("SHT_REL symbol-table link is out of range");
        }
        const auto linked_section_type = report.section_headers[section.link].type;
        if (linked_section_type != kSectionTypeSymbolTable &&
            linked_section_type != kSectionTypeDynamicSymbolTable) {
            throw ElfError("SHT_REL link does not reference a symbol-table section");
        }
        if (section.info >= report.section_headers.size()) {
            throw ElfError("SHT_REL target-section index is out of range");
        }
        if (section.entry_size != kElf32RelocationSize) {
            throw ElfError("SHT_REL entry size is not the canonical ELF32 size");
        }
        if (section.size % kElf32RelocationSize != 0) {
            throw ElfError("SHT_REL section size is not a multiple of its entry size");
        }
        const auto entry_count = section.size / kElf32RelocationSize;
        if (entry_count > kMaximumRelocationEntryCount) {
            throw ElfError("SHT_REL entry count exceeds the supported limit");
        }

        std::array<std::uint32_t, 256> type_counts{};
        for (std::uint32_t index = 0; index < entry_count; ++index) {
            const auto offset = static_cast<std::size_t>(
                static_cast<std::uint64_t>(section.file_offset) +
                static_cast<std::uint64_t>(index) * kElf32RelocationSize);
            const auto relocation_info = read_le32(bytes, offset + 4);
            ++type_counts[relocation_info & 0xffU];
        }

        ElfRelocationSummary summary;
        summary.section_index = static_cast<std::uint16_t>(section_index);
        summary.symbol_table_section_index = section.link;
        summary.target_section_index = section.info;
        summary.entry_count = entry_count;
        for (std::size_t type = 0; type < type_counts.size(); ++type) {
            if (type_counts[type] == 0) {
                continue;
            }
            summary.types.push_back(ElfRelocationTypeCount{
                static_cast<std::uint8_t>(type),
                type_counts[type],
            });
        }
        report.relocation_summaries.push_back(std::move(summary));
    }
}

[[nodiscard]] std::optional<std::string> parse_iop_import_name(
    const std::span<const std::byte> header) {
    std::string name;
    bool padding_started = false;
    for (std::size_t index = 12; index < kIopImportHeaderSize; ++index) {
        const auto character = byte_value(header[index]);
        if (character == 0) {
            padding_started = true;
            continue;
        }
        if (padding_started) {
            return std::nullopt;
        }
        if (character < 0x20U || character > 0x7eU) {
            return std::nullopt;
        }
        name.push_back(static_cast<char>(character));
    }
    if (name.empty()) {
        return std::nullopt;
    }
    return name;
}

struct IopImportCandidate {
    IopImportLibrary library;
    std::size_t next_section_offset = 0;
};

[[nodiscard]] std::optional<IopImportCandidate> parse_iop_import_candidate(
    const std::span<const std::byte> section_bytes,
    const ElfSectionHeader& section,
    const std::size_t section_index,
    const std::size_t section_offset) {
    if (section_offset + kIopImportHeaderSize > section_bytes.size()) {
        return std::nullopt;
    }
    const auto header = section_bytes.subspan(section_offset, kIopImportHeaderSize);
    if (read_le32(header, 4) != 0) {
        return std::nullopt;
    }
    auto name = parse_iop_import_name(header);
    if (!name) {
        return std::nullopt;
    }

    IopImportCandidate candidate;
    candidate.library.section_index = static_cast<std::uint16_t>(section_index);
    candidate.library.section_offset = static_cast<std::uint32_t>(section_offset);
    const auto virtual_address =
        static_cast<std::uint64_t>(section.virtual_address) + section_offset;
    if (virtual_address >= kElf32AddressSpaceEnd) {
        return std::nullopt;
    }
    candidate.library.virtual_address = static_cast<std::uint32_t>(virtual_address);
    candidate.library.version = read_le16(header, 8);
    candidate.library.flags = read_le16(header, 10);
    candidate.library.name = std::move(*name);

    auto stub_offset = section_offset + kIopImportHeaderSize;
    while (true) {
        if (stub_offset + kIopImportStubSize > section_bytes.size()) {
            return std::nullopt;
        }
        const auto first_instruction = read_le32(section_bytes, stub_offset);
        const auto second_instruction =
            read_le32(section_bytes, stub_offset + sizeof(std::uint32_t));
        if (first_instruction == 0 && second_instruction == 0) {
            candidate.next_section_offset = stub_offset + kIopImportStubSize;
            return candidate;
        }
        if (first_instruction != kMipsJumpReturnAddress ||
            (second_instruction & 0xffff0000U) != kMipsAddImmediateUnsignedZero) {
            return std::nullopt;
        }
        if (candidate.library.ordinals.size() >= kMaximumIopImportStubsPerLibrary) {
            return std::nullopt;
        }
        candidate.library.ordinals.push_back(
            static_cast<std::uint16_t>(second_instruction & 0xffffU));
        stub_offset += kIopImportStubSize;
    }
}

void inspect_iop_imports(
    const std::span<const std::byte> bytes,
    ElfReport& report) {
    std::uint64_t scanned_byte_count = 0;
    std::size_t total_stub_count = 0;

    for (std::size_t section_index = 0;
         section_index < report.section_headers.size();
         ++section_index) {
        const auto& section = report.section_headers[section_index];
        if (section.type == kSectionTypeNoBits ||
            (section.flags & kSectionFlagExecutable) == 0 ||
            section.size < sizeof(std::uint32_t)) {
            continue;
        }

        scanned_byte_count += section.size;
        if (scanned_byte_count > kMaximumIopImportScanBytes) {
            throw ElfError("Executable sections exceed the supported IOP import scan limit");
        }

        const auto section_bytes = bytes.subspan(
            static_cast<std::size_t>(section.file_offset),
            static_cast<std::size_t>(section.size));
        const auto alignment_adjustment =
            static_cast<std::size_t>((4U - (section.virtual_address & 3U)) & 3U);
        std::size_t section_offset = alignment_adjustment;
        while (section_offset + sizeof(std::uint32_t) <= section_bytes.size()) {
            if (read_le32(section_bytes, section_offset) != kIopImportMagic) {
                section_offset += sizeof(std::uint32_t);
                continue;
            }
            auto candidate = parse_iop_import_candidate(
                section_bytes,
                section,
                section_index,
                section_offset);
            if (!candidate) {
                section_offset += sizeof(std::uint32_t);
                continue;
            }
            if (report.iop_import_libraries.size() >= kMaximumIopImportLibraries) {
                throw ElfError("IOP import library count exceeds the supported limit");
            }
            if (candidate->library.ordinals.size() >
                kMaximumIopImportStubsTotal - total_stub_count) {
                throw ElfError("IOP import stub count exceeds the supported limit");
            }
            total_stub_count += candidate->library.ordinals.size();
            section_offset = candidate->next_section_offset;
            report.iop_import_libraries.push_back(std::move(candidate->library));
        }
    }
}

} // namespace

ElfReport inspect_elf(const std::span<const std::byte> executable_bytes) {
    if (executable_bytes.size() < kElf32HeaderSize) {
        throw ElfError("The file is too small to contain an ELF32 header");
    }

    if (byte_value(executable_bytes[0]) != 0x7fU ||
        byte_value(executable_bytes[1]) != 'E' ||
        byte_value(executable_bytes[2]) != 'L' ||
        byte_value(executable_bytes[3]) != 'F') {
        throw ElfError("The file does not have an ELF signature");
    }
    if (byte_value(executable_bytes[4]) != kElfClass32) {
        throw ElfError("The executable is not ELF32");
    }
    if (byte_value(executable_bytes[5]) != kElfDataLittleEndian) {
        throw ElfError("The executable is not little-endian ELF");
    }
    if (byte_value(executable_bytes[6]) != kElfCurrentVersion ||
        read_le32(executable_bytes, 20) != kElfVersionCurrent) {
        throw ElfError("The executable uses an unsupported ELF version");
    }

    const auto machine = read_le16(executable_bytes, 18);
    if (machine != kMachineMips) {
        throw ElfError("The executable is not for the MIPS architecture");
    }
    if (read_le16(executable_bytes, 40) != kElf32HeaderSize) {
        throw ElfError("The executable has an invalid ELF32 header size");
    }

    const auto program_header_offset = read_le32(executable_bytes, 28);
    const auto section_header_offset = read_le32(executable_bytes, 32);
    const auto program_header_size = read_le16(executable_bytes, 42);
    const auto program_header_count = read_le16(executable_bytes, 44);
    const auto section_header_size = read_le16(executable_bytes, 46);
    const auto section_header_count = read_le16(executable_bytes, 48);
    const auto section_name_index = read_le16(executable_bytes, 50);

    if (program_header_count == kExtendedProgramHeaderCount) {
        throw ElfError("Extended ELF program header numbering is not supported");
    }
    if (program_header_count > kMaximumProgramHeaderCount) {
        throw ElfError("ELF program header count exceeds the supported limit");
    }
    if (section_header_count > kMaximumSectionHeaderCount) {
        throw ElfError("ELF section header count exceeds the supported limit");
    }
    if (program_header_count != 0 && program_header_size < kElf32ProgramHeaderSize) {
        throw ElfError("ELF32 program header entries are too small");
    }
    if (section_header_count != 0 && section_header_size < kElf32SectionHeaderSize) {
        throw ElfError("ELF32 section header entries are too small");
    }
    if (section_header_count == 0 && section_header_offset != 0) {
        throw ElfError("Extended ELF section numbering is not supported");
    }
    if (section_name_index == kExtendedSectionNameIndex) {
        throw ElfError("Extended ELF section-name indexing is not supported");
    }
    if (section_name_index != 0 && section_name_index >= section_header_count) {
        throw ElfError("ELF section-name table index is out of range");
    }

    require_table_range(
        program_header_offset,
        program_header_size,
        program_header_count,
        executable_bytes.size(),
        "Program header table");
    require_table_range(
        section_header_offset,
        section_header_size,
        section_header_count,
        executable_bytes.size(),
        "Section header table");

    ElfReport report;
    report.file_size = executable_bytes.size();
    report.type = read_le16(executable_bytes, 16);
    report.machine = machine;
    report.flags = read_le32(executable_bytes, 36);
    report.entry_point = read_le32(executable_bytes, 24);
    report.program_header_count = program_header_count;
    report.section_header_count = section_header_count;
    report.section_name_table_index = section_name_index;

    inspect_program_headers(
        executable_bytes,
        program_header_offset,
        program_header_size,
        program_header_count,
        report);
    inspect_section_headers(
        executable_bytes,
        section_header_offset,
        section_header_size,
        section_header_count,
        section_name_index,
        report);
    inspect_iop_module_info(executable_bytes, report);
    inspect_relocations(executable_bytes, report);
    inspect_iop_imports(executable_bytes, report);
    return report;
}

ElfReport inspect_elf(const std::filesystem::path& executable_path) {
    std::ifstream stream(executable_path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw ElfError("Cannot open the ELF file");
    }
    const auto end_position = stream.tellg();
    if (end_position < 0) {
        throw ElfError("Cannot determine the ELF file size from the open stream");
    }
    const auto file_size = static_cast<std::uintmax_t>(end_position);
    if (file_size > std::numeric_limits<std::size_t>::max() ||
        file_size > static_cast<std::uintmax_t>(
            std::numeric_limits<std::streamsize>::max())) {
        throw ElfError("ELF file is too large to load into memory");
    }

    stream.seekg(0, std::ios::beg);
    if (!stream) {
        throw ElfError("Cannot seek to the beginning of the ELF file");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
    if (!bytes.empty()) {
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
            throw ElfError("Unexpected end of ELF file");
        }
    }
    char trailing_byte = 0;
    stream.read(&trailing_byte, 1);
    if (stream.gcount() != 0) {
        throw ElfError("The ELF file grew while it was being read");
    }

    auto report = inspect_elf(std::span<const std::byte>(bytes));
    report.executable_path = executable_path;
    return report;
}

} // namespace openrc
