#include "openrc/elf.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kElfHeaderSize = 52;
constexpr std::size_t kProgramHeaderSize = 32;
constexpr std::size_t kSectionHeaderSize = 40;
constexpr std::size_t kProgramHeaderOffset = kElfHeaderSize;
constexpr std::size_t kSectionHeaderOffset = 0x100;
constexpr std::size_t kSectionNameTableOffset = 0x1c0;
constexpr char kSectionNames[] = "\0.text\0.bss\0.shstrtab";
constexpr std::uint32_t kTextNameOffset = 1;
constexpr std::uint32_t kBssNameOffset = 7;
constexpr std::uint32_t kStringTableNameOffset = 12;
constexpr std::size_t kIrxSectionHeaderOffset = 0x100;
constexpr std::size_t kIrxTextOffset = 0x300;
constexpr std::size_t kIrxDataOffset = 0x380;
constexpr std::size_t kIrxRelocationOffset = 0x390;
constexpr std::size_t kIrxModuleOffset = 0x3a0;
constexpr std::size_t kIrxSymbolTableOffset = 0x3d0;
constexpr std::size_t kIrxStringTableOffset = 0x3f0;
constexpr std::size_t kIrxSectionNameTableOffset = 0x420;
constexpr std::size_t kIrxTextSectionIndex = 1;
constexpr std::size_t kIrxRelocationSectionIndex = 4;
constexpr std::size_t kIrxModuleSectionIndex = 5;
constexpr char kIrxSectionNames[] =
    "\0.text\0.data\0.bss\0.rel.text\0.iopmod\0.symtab\0.strtab\0.shstrtab";
constexpr std::size_t kDvpCodeOffset = 0x300;
constexpr std::size_t kDvpFirstOverlayOffset = 0x330;
constexpr std::size_t kDvpSecondOverlayOffset = 0x340;
constexpr std::size_t kDvpTableOffset = 0x358;
constexpr std::size_t kDvpStringTableOffset = 0x380;
constexpr std::size_t kDvpSectionNameTableOffset = 0x3c0;
constexpr std::size_t kDvpCodeSectionIndex = 1;
constexpr std::size_t kDvpFirstOverlaySectionIndex = 2;
constexpr std::size_t kDvpSecondOverlaySectionIndex = 3;
constexpr std::size_t kDvpTableSectionIndex = 4;
constexpr std::size_t kDvpStringTableSectionIndex = 5;
constexpr std::uint32_t kDvpCodeNameOffset = 1;
constexpr std::uint32_t kDvpFirstOverlayNameOffset =
    kDvpCodeNameOffset + sizeof(".vutext");
constexpr std::uint32_t kDvpSecondOverlayNameOffset =
    kDvpFirstOverlayNameOffset + sizeof(".DVP.overlay.alpha");
constexpr std::uint32_t kDvpTableNameOffset =
    kDvpSecondOverlayNameOffset + sizeof(".DVP.overlay.beta");
constexpr std::uint32_t kDvpStringTableNameOffset =
    kDvpTableNameOffset + sizeof(".DVP.ovlytab");
constexpr std::uint32_t kDvpSectionNameTableNameOffset =
    kDvpStringTableNameOffset + sizeof(".DVP.ovlystrtab");
constexpr std::uint32_t kDvpFirstRecordNameOffset = 1;
constexpr std::uint32_t kDvpSecondRecordNameOffset =
    kDvpFirstRecordNameOffset + sizeof(".DVP.overlay.alpha");
constexpr char kDvpOverlayNames[] =
    "\0.DVP.overlay.alpha\0.DVP.overlay.beta";
constexpr char kDvpSectionNames[] =
    "\0.vutext\0.DVP.overlay.alpha\0.DVP.overlay.beta"
    "\0.DVP.ovlytab\0.DVP.ovlystrtab\0.shstrtab";

void write_le16(std::uint8_t* target, const std::uint16_t value) {
    target[0] = static_cast<std::uint8_t>(value & 0xffU);
    target[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void write_le32(std::uint8_t* target, const std::uint32_t value) {
    target[0] = static_cast<std::uint8_t>(value & 0xffU);
    target[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    target[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    target[3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::vector<std::uint8_t> make_valid_elf() {
    std::vector<std::uint8_t> elf(0x280, 0);
    elf[0] = 0x7fU;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 1;
    elf[5] = 1;
    elf[6] = 1;
    write_le16(elf.data() + 16, 2);
    write_le16(elf.data() + 18, 8);
    write_le32(elf.data() + 20, 1);
    write_le32(elf.data() + 24, 0x00100010U);
    write_le32(elf.data() + 28, static_cast<std::uint32_t>(kProgramHeaderOffset));
    write_le32(elf.data() + 32, static_cast<std::uint32_t>(kSectionHeaderOffset));
    write_le16(elf.data() + 40, static_cast<std::uint16_t>(kElfHeaderSize));
    write_le16(elf.data() + 42, static_cast<std::uint16_t>(kProgramHeaderSize));
    write_le16(elf.data() + 44, 2);
    write_le16(elf.data() + 46, static_cast<std::uint16_t>(kSectionHeaderSize));
    write_le16(elf.data() + 48, 4);
    write_le16(elf.data() + 50, 3);

    auto* first_program_header = elf.data() + kProgramHeaderOffset;
    write_le32(first_program_header, 1);
    write_le32(first_program_header + 4, 0x200);
    write_le32(first_program_header + 8, 0x00100000U);
    write_le32(first_program_header + 12, 0x00100000U);
    write_le32(first_program_header + 16, 0x20);
    write_le32(first_program_header + 20, 0x40);
    write_le32(first_program_header + 24, 5);
    write_le32(first_program_header + 28, 0x10);

    auto* second_program_header = first_program_header + kProgramHeaderSize;
    write_le32(second_program_header, 1);
    write_le32(second_program_header + 4, 0x240);
    write_le32(second_program_header + 8, 0x00200000U);
    write_le32(second_program_header + 12, 0x00200000U);
    write_le32(second_program_header + 16, 0x10);
    write_le32(second_program_header + 20, 0x80);
    write_le32(second_program_header + 24, 6);
    write_le32(second_program_header + 28, 0x40);

    auto* first_data_section = elf.data() + kSectionHeaderOffset + kSectionHeaderSize;
    write_le32(first_data_section, kTextNameOffset);
    write_le32(first_data_section + 4, 1);
    write_le32(first_data_section + 8, 6);
    write_le32(first_data_section + 12, 0x00100000U);
    write_le32(first_data_section + 16, 0x200);
    write_le32(first_data_section + 20, 0x20);
    write_le32(first_data_section + 24, 3);
    write_le32(first_data_section + 28, 2);
    write_le32(first_data_section + 32, 0x10);
    write_le32(first_data_section + 36, 4);

    auto* no_bits_section = first_data_section + kSectionHeaderSize;
    write_le32(no_bits_section, kBssNameOffset);
    write_le32(no_bits_section + 4, 8);
    write_le32(no_bits_section + 8, 3);
    write_le32(no_bits_section + 12, 0x00100040U);
    write_le32(no_bits_section + 16, 0xfffffff0U);
    write_le32(no_bits_section + 20, 0x100);
    write_le32(no_bits_section + 32, 0x10);

    auto* string_table_section = no_bits_section + kSectionHeaderSize;
    write_le32(string_table_section, kStringTableNameOffset);
    write_le32(string_table_section + 4, 3);
    write_le32(string_table_section + 16, static_cast<std::uint32_t>(kSectionNameTableOffset));
    write_le32(string_table_section + 20, static_cast<std::uint32_t>(sizeof(kSectionNames)));
    write_le32(string_table_section + 32, 1);

    for (std::size_t index = 0; index < sizeof(kSectionNames); ++index) {
        elf[kSectionNameTableOffset + index] = static_cast<std::uint8_t>(kSectionNames[index]);
    }
    return elf;
}

void write_section_header(
    std::vector<std::uint8_t>& elf,
    const std::size_t index,
    const std::uint32_t name,
    const std::uint32_t type,
    const std::uint32_t flags,
    const std::uint32_t address,
    const std::uint32_t offset,
    const std::uint32_t size,
    const std::uint32_t link,
    const std::uint32_t info,
    const std::uint32_t alignment,
    const std::uint32_t entry_size) {
    auto* section =
        elf.data() + kIrxSectionHeaderOffset + index * kSectionHeaderSize;
    write_le32(section, name);
    write_le32(section + 4, type);
    write_le32(section + 8, flags);
    write_le32(section + 12, address);
    write_le32(section + 16, offset);
    write_le32(section + 20, size);
    write_le32(section + 24, link);
    write_le32(section + 28, info);
    write_le32(section + 32, alignment);
    write_le32(section + 36, entry_size);
}

std::vector<std::uint8_t> make_valid_irx() {
    std::vector<std::uint8_t> elf(0x500, 0);
    elf[0] = 0x7fU;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 1;
    elf[5] = 1;
    elf[6] = 1;
    write_le16(elf.data() + 16, 0xff80U);
    write_le16(elf.data() + 18, 8);
    write_le32(elf.data() + 20, 1);
    write_le32(elf.data() + 24, 0x1000);
    write_le32(elf.data() + 32, static_cast<std::uint32_t>(kIrxSectionHeaderOffset));
    write_le32(elf.data() + 36, 1);
    write_le16(elf.data() + 40, static_cast<std::uint16_t>(kElfHeaderSize));
    write_le16(elf.data() + 42, static_cast<std::uint16_t>(kProgramHeaderSize));
    write_le16(elf.data() + 46, static_cast<std::uint16_t>(kSectionHeaderSize));
    write_le16(elf.data() + 48, 9);
    write_le16(elf.data() + 50, 8);

    write_section_header(
        elf, 1, 1, 1, 6, 0x1000, static_cast<std::uint32_t>(kIrxTextOffset),
        0x80, 0, 0, 4, 0);
    write_section_header(
        elf, 2, 7, 1, 3, 0x2000, static_cast<std::uint32_t>(kIrxDataOffset),
        0x10, 0, 0, 4, 0);
    write_section_header(elf, 3, 13, 8, 3, 0x2010, 0, 0x20, 0, 0, 4, 0);
    write_section_header(
        elf, 4, 18, 9, 0, 0, static_cast<std::uint32_t>(kIrxRelocationOffset),
        0x10, 6, 1, 4, 8);
    write_section_header(
        elf, 5, 28, 0x70000080U, 0, 0, static_cast<std::uint32_t>(kIrxModuleOffset),
        0x30, 0, 0, 4, 0);
    write_section_header(
        elf, 6, 36, 2, 0, 0, static_cast<std::uint32_t>(kIrxSymbolTableOffset),
        0x20, 7, 1, 4, 0x10);
    write_section_header(
        elf, 7, 44, 3, 0, 0, static_cast<std::uint32_t>(kIrxStringTableOffset),
        1, 0, 0, 1, 0);
    write_section_header(
        elf, 8, 52, 3, 0, 0, static_cast<std::uint32_t>(kIrxSectionNameTableOffset),
        static_cast<std::uint32_t>(sizeof(kIrxSectionNames)), 0, 0, 1, 0);

    auto* import = elf.data() + kIrxTextOffset;
    write_le32(import, 0x41e00000U);
    write_le32(import + 4, 0);
    write_le16(import + 8, 0x0102);
    write_le16(import + 10, 3);
    constexpr char library_name[] = "sysclib";
    for (std::size_t index = 0; index < sizeof(library_name); ++index) {
        import[12 + index] = static_cast<std::uint8_t>(library_name[index]);
    }
    write_le32(import + 20, 0x03e00008U);
    write_le32(import + 24, 0x24000004U);
    write_le32(import + 28, 0x03e00008U);
    write_le32(import + 32, 0x24000007U);

    auto* first_relocation = elf.data() + kIrxRelocationOffset;
    write_le32(first_relocation, 0x1010);
    write_le32(first_relocation + 4, 0x00000102);
    write_le32(first_relocation + 8, 0x1020);
    write_le32(first_relocation + 12, 0x00000206);

    auto* module = elf.data() + kIrxModuleOffset;
    write_le32(module, 0x12345678);
    write_le32(module + 4, 0x1000);
    write_le32(module + 8, 0x2010);
    write_le32(module + 12, 0x80);
    write_le32(module + 16, 0x10);
    write_le32(module + 20, 0x20);
    write_le16(module + 24, 0x0102);
    constexpr char module_name[] = "synth";
    for (std::size_t index = 0; index < sizeof(module_name); ++index) {
        module[26 + index] = static_cast<std::uint8_t>(module_name[index]);
    }

    for (std::size_t index = 0; index < sizeof(kIrxSectionNames); ++index) {
        elf[kIrxSectionNameTableOffset + index] =
            static_cast<std::uint8_t>(kIrxSectionNames[index]);
    }
    return elf;
}

std::vector<std::uint8_t> make_valid_dvp_elf() {
    std::vector<std::uint8_t> elf(0x500, 0);
    elf[0] = 0x7fU;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 1;
    elf[5] = 1;
    elf[6] = 1;
    write_le16(elf.data() + 16, 2);
    write_le16(elf.data() + 18, 8);
    write_le32(elf.data() + 20, 1);
    write_le32(elf.data() + 24, 0x1000);
    write_le32(elf.data() + 28, static_cast<std::uint32_t>(kProgramHeaderOffset));
    write_le32(elf.data() + 32, static_cast<std::uint32_t>(kIrxSectionHeaderOffset));
    write_le16(elf.data() + 40, static_cast<std::uint16_t>(kElfHeaderSize));
    write_le16(elf.data() + 42, static_cast<std::uint16_t>(kProgramHeaderSize));
    write_le16(elf.data() + 44, 1);
    write_le16(elf.data() + 46, static_cast<std::uint16_t>(kSectionHeaderSize));
    write_le16(elf.data() + 48, 7);
    write_le16(elf.data() + 50, 6);

    auto* program_header = elf.data() + kProgramHeaderOffset;
    write_le32(program_header, 1);
    write_le32(program_header + 4, static_cast<std::uint32_t>(kDvpCodeOffset));
    write_le32(program_header + 8, 0x2000);
    write_le32(program_header + 12, 0x1000);
    write_le32(program_header + 16, 0x30);
    write_le32(program_header + 20, 0x30);
    write_le32(program_header + 24, 5);
    write_le32(program_header + 28, 0x10);

    write_section_header(
        elf,
        kDvpCodeSectionIndex,
        kDvpCodeNameOffset,
        1,
        6,
        0x2000,
        static_cast<std::uint32_t>(kDvpCodeOffset),
        0x30,
        0,
        0,
        0x10,
        0);
    write_section_header(
        elf,
        kDvpFirstOverlaySectionIndex,
        kDvpFirstOverlayNameOffset,
        0x7ffff421U,
        5,
        0,
        static_cast<std::uint32_t>(kDvpFirstOverlayOffset),
        0x10,
        0,
        0,
        1,
        0);
    write_section_header(
        elf,
        kDvpSecondOverlaySectionIndex,
        kDvpSecondOverlayNameOffset,
        0x7ffff421U,
        5,
        0,
        static_cast<std::uint32_t>(kDvpSecondOverlayOffset),
        0x18,
        0,
        0,
        1,
        0);
    write_section_header(
        elf,
        kDvpTableSectionIndex,
        kDvpTableNameOffset,
        0x7ffff420U,
        1,
        0,
        static_cast<std::uint32_t>(kDvpTableOffset),
        0x18,
        kDvpStringTableSectionIndex,
        0,
        4,
        12);
    write_section_header(
        elf,
        kDvpStringTableSectionIndex,
        kDvpStringTableNameOffset,
        3,
        1,
        0,
        static_cast<std::uint32_t>(kDvpStringTableOffset),
        static_cast<std::uint32_t>(sizeof(kDvpOverlayNames)),
        0,
        0,
        1,
        0);
    write_section_header(
        elf,
        6,
        kDvpSectionNameTableNameOffset,
        3,
        0,
        0,
        static_cast<std::uint32_t>(kDvpSectionNameTableOffset),
        static_cast<std::uint32_t>(sizeof(kDvpSectionNames)),
        0,
        0,
        1,
        0);

    for (std::size_t index = 0U; index < 0x30U; ++index) {
        elf[kDvpCodeOffset + index] =
            static_cast<std::uint8_t>(index + 1U);
    }
    write_le32(elf.data() + kDvpTableOffset, kDvpFirstRecordNameOffset);
    write_le32(elf.data() + kDvpTableOffset + 4, 0x1000);
    write_le32(elf.data() + kDvpTableOffset + 8, 0x0000);
    write_le32(elf.data() + kDvpTableOffset + 12, kDvpSecondRecordNameOffset);
    write_le32(elf.data() + kDvpTableOffset + 16, 0x1010);
    write_le32(elf.data() + kDvpTableOffset + 20, 0x0800);

    for (std::size_t index = 0U; index < sizeof(kDvpOverlayNames); ++index) {
        elf[kDvpStringTableOffset + index] =
            static_cast<std::uint8_t>(kDvpOverlayNames[index]);
    }
    for (std::size_t index = 0U; index < sizeof(kDvpSectionNames); ++index) {
        elf[kDvpSectionNameTableOffset + index] =
            static_cast<std::uint8_t>(kDvpSectionNames[index]);
    }
    return elf;
}

std::span<const std::byte> as_bytes(const std::vector<std::uint8_t>& bytes) {
    return std::as_bytes(std::span<const std::uint8_t>(bytes));
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("failed to create synthetic ELF");
    }
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_rejected(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes,
    const std::string& message) {
    write_file(path, bytes);
    bool rejected = false;
    try {
        (void)openrc::inspect_elf(path);
    } catch (const openrc::ElfError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void expect_span_rejected(
    const std::vector<std::uint8_t>& bytes,
    const std::string& message) {
    bool rejected = false;
    try {
        (void)openrc::inspect_elf(as_bytes(bytes));
    } catch (const openrc::ElfError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void expect_no_iop_imports(
    const std::vector<std::uint8_t>& bytes,
    const std::string& message) {
    const auto report = openrc::inspect_elf(as_bytes(bytes));
    expect(report.iop_import_libraries.empty(), message);
}

void test_valid_elf(const std::filesystem::path& directory) {
    const auto path = directory / "synthetic-valid.elf";
    write_file(path, make_valid_elf());

    const auto report = openrc::inspect_elf(path);
    expect(report.executable_path == path, "ELF path was not preserved");
    expect(report.file_size == 0x280, "ELF size was not reported");
    expect(report.type == 2, "ELF type was not parsed");
    expect(report.machine == 8, "ELF machine was not parsed");
    expect(report.flags == 0, "ELF flags were not parsed");
    expect(report.entry_point == 0x00100010U, "entry point was not parsed");
    expect(report.program_header_count == 2, "program header count was not parsed");
    expect(report.section_header_count == 4, "section header count was not parsed");
    expect(report.section_name_table_index == 3, "section-name table index was not parsed");
    expect(report.program_headers.size() == 2, "program header inventory is incomplete");
    expect(report.section_headers.size() == 4, "section header inventory is incomplete");

    const auto& first_segment = report.program_headers[0];
    expect(first_segment.type == 1, "program segment type was not parsed");
    expect(first_segment.file_offset == 0x200, "program segment file offset was not parsed");
    expect(first_segment.virtual_address == 0x00100000U, "program segment address was not parsed");
    expect(first_segment.physical_address == 0x00100000U, "program segment physical address was not parsed");
    expect(first_segment.file_size == 0x20, "program segment file size was not parsed");
    expect(first_segment.memory_size == 0x40, "program segment memory size was not parsed");
    expect(first_segment.flags == 5, "program segment flags were not parsed");
    expect(first_segment.alignment == 0x10, "program segment alignment was not parsed");

    const auto& second_segment = report.program_headers[1];
    expect(second_segment.file_offset == 0x240, "second program segment offset was not parsed");
    expect(second_segment.virtual_address == 0x00200000U, "second program segment address was not parsed");
    expect(second_segment.physical_address == 0x00200000U, "second program segment physical address was not parsed");
    expect(second_segment.file_size == 0x10, "second program segment file size was not parsed");
    expect(second_segment.memory_size == 0x80, "second program segment memory size was not parsed");
    expect(second_segment.flags == 6, "second program segment flags were not parsed");
    expect(second_segment.alignment == 0x40, "second program segment alignment was not parsed");

    const auto& text_section = report.section_headers[1];
    expect(text_section.name == ".text", "section name was not resolved");
    expect(text_section.name_offset == kTextNameOffset, "section name offset was not parsed");
    expect(text_section.type == 1, "section type was not parsed");
    expect(text_section.flags == 6, "section flags were not parsed");
    expect(text_section.virtual_address == 0x00100000U, "section address was not parsed");
    expect(text_section.file_offset == 0x200, "section file offset was not parsed");
    expect(text_section.size == 0x20, "section size was not parsed");
    expect(text_section.link == 3, "section link was not parsed");
    expect(text_section.info == 2, "section info was not parsed");
    expect(text_section.alignment == 0x10, "section alignment was not parsed");
    expect(text_section.entry_size == 4, "section entry size was not parsed");

    const auto& no_bits_section = report.section_headers[2];
    expect(no_bits_section.name == ".bss", "NOBITS section name was not resolved");
    expect(no_bits_section.type == 8, "NOBITS section type was not parsed");
    expect(no_bits_section.file_offset == 0xfffffff0U, "NOBITS file offset was not preserved");
    expect(no_bits_section.size == 0x100, "NOBITS section size was not parsed");

    const auto& string_table_section = report.section_headers[3];
    expect(string_table_section.name == ".shstrtab", "string-table name was not resolved");
    expect(string_table_section.type == 3, "string-table type was not parsed");
    expect(
        string_table_section.file_offset == kSectionNameTableOffset,
        "string-table offset was not parsed");
    expect(string_table_section.size == sizeof(kSectionNames), "string-table size was not parsed");
    expect(report.loadable_segment_count == 2, "loadable segment count was not parsed");
    expect(report.loadable_virtual_address_range.has_value(), "loadable address range is missing");
    expect(
        report.loadable_virtual_address_range->begin == 0x00100000U,
        "loadable address range start is incorrect");
    expect(
        report.loadable_virtual_address_range->end == 0x00200080U,
        "loadable address range end is incorrect");
}

void test_valid_irx_inventory() {
    const auto bytes = make_valid_irx();
    const auto report = openrc::inspect_elf(as_bytes(bytes));

    expect(report.executable_path.empty(), "span ELF should not invent a source path");
    expect(report.file_size == bytes.size(), "span ELF size was not reported");
    expect(report.type == 0xff80U, "IRX ELF type was not parsed");
    expect(report.machine == 8, "IRX machine was not parsed");
    expect(report.flags == 1, "IRX flags were not parsed");

    expect(report.iop_module_info.has_value(), ".iopmod inventory is missing");
    const auto& module = *report.iop_module_info;
    expect(module.section_index == kIrxModuleSectionIndex, ".iopmod section index is wrong");
    expect(module.module_info_address == 0x12345678, ".iopmod word 0 was not preserved");
    expect(module.entry_point == 0x1000, ".iopmod entry point was not parsed");
    expect(module.global_pointer == 0x2010, ".iopmod global pointer was not parsed");
    expect(module.text_size == 0x80, ".iopmod text size was not parsed");
    expect(module.data_size == 0x10, ".iopmod data size was not parsed");
    expect(module.bss_size == 0x20, ".iopmod BSS size was not parsed");
    expect(module.version == 0x0102, ".iopmod version was not parsed");
    expect(module.name == "synth", ".iopmod name was not parsed");
    expect(module.allocated_text_size == 0x80, "allocated text inventory is wrong");
    expect(module.allocated_data_size == 0x10, "allocated data inventory is wrong");
    expect(module.allocated_bss_size == 0x20, "allocated BSS inventory is wrong");
    expect(module.text_size_matches_allocated_sections, "text consistency was not reported");
    expect(module.data_size_matches_allocated_sections, "data consistency was not reported");
    expect(module.bss_size_matches_allocated_sections, "BSS consistency was not reported");

    expect(report.relocation_summaries.size() == 1, "relocation summary is missing");
    const auto& relocations = report.relocation_summaries.front();
    expect(
        relocations.section_index == kIrxRelocationSectionIndex,
        "relocation section index is wrong");
    expect(relocations.symbol_table_section_index == 6, "relocation link is wrong");
    expect(relocations.target_section_index == 1, "relocation target is wrong");
    expect(relocations.entry_count == 2, "relocation count is wrong");
    expect(relocations.types.size() == 2, "relocation type inventory is incomplete");
    expect(
        relocations.types[0].type == 2 && relocations.types[0].count == 1,
        "first relocation type count is wrong");
    expect(
        relocations.types[1].type == 6 && relocations.types[1].count == 1,
        "second relocation type count is wrong");

    expect(report.iop_import_libraries.size() == 1, "IOP import inventory is missing");
    const auto& library = report.iop_import_libraries.front();
    expect(library.section_index == kIrxTextSectionIndex, "IOP import section index is wrong");
    expect(library.section_offset == 0, "IOP import section offset is wrong");
    expect(library.virtual_address == 0x1000, "IOP import address is wrong");
    expect(library.version == 0x0102, "IOP import version is wrong");
    expect(library.flags == 3, "IOP import flags are wrong");
    expect(library.name == "sysclib", "IOP import name is wrong");
    expect(
        library.ordinals == std::vector<std::uint16_t>({4, 7}),
        "IOP import ordinals are wrong");
}

void test_irx_consistency_is_diagnostic() {
    auto bytes = make_valid_irx();
    write_le32(bytes.data() + kIrxModuleOffset + 12, 1);
    const auto report = openrc::inspect_elf(as_bytes(bytes));
    expect(report.iop_module_info.has_value(), "mismatched .iopmod was not inventoried");
    expect(
        !report.iop_module_info->text_size_matches_allocated_sections,
        "mismatched text size should be diagnostic, not accepted as matching");
}

void test_iop_module_is_detected_by_type_without_section_names() {
    auto bytes = make_valid_irx();
    write_le16(bytes.data() + 50, 0);
    for (std::size_t index = 0; index < 9; ++index) {
        write_le32(
            bytes.data() + kIrxSectionHeaderOffset + index * kSectionHeaderSize,
            0);
    }

    const auto report = openrc::inspect_elf(as_bytes(bytes));
    expect(report.iop_module_info.has_value(), "IOP module type was not detected without names");
    expect(report.iop_module_info->name == "synth", "typed IOP module payload was not parsed");
}

void test_unknown_relocation_type_is_preserved() {
    auto bytes = make_valid_irx();
    write_le32(bytes.data() + kIrxRelocationOffset + 12, 0x0000027f);
    const auto report = openrc::inspect_elf(as_bytes(bytes));
    expect(report.relocation_summaries.size() == 1, "relocation summary is missing");
    expect(
        report.relocation_summaries[0].types[1].type == 0x7f,
        "neutral relocation inventory discarded an unknown MIPS type");
}

void test_valid_dvp_overlay_inventory() {
    const auto report = openrc::inspect_elf(as_bytes(make_valid_dvp_elf()));
    expect(report.dvp_overlay_table.has_value(), "DVP overlay table is missing");
    expect(
        report.program_headers.size() == 1U &&
            report.program_headers[0].physical_address == 0x1000U &&
            report.program_headers[0].virtual_address == 0x2000U,
        "DVP test segment did not preserve distinct physical and virtual addresses");
    const auto& table = report.dvp_overlay_table.value();
    expect(
        table.section_index == kDvpTableSectionIndex,
        "DVP overlay table section index is wrong");
    expect(
        table.string_table_section_index == kDvpStringTableSectionIndex,
        "DVP overlay string-table index is wrong");
    expect(table.overlays.size() == 2U, "DVP overlay inventory is incomplete");

    const auto& first = table.overlays[0];
    expect(
        first.overlay_section_index == kDvpFirstOverlaySectionIndex,
        "first DVP overlay section index is wrong");
    expect(
        first.code_section_index == kDvpCodeSectionIndex,
        "first DVP code section index is wrong");
    expect(
        first.name_offset == kDvpFirstRecordNameOffset,
        "first DVP overlay name offset is wrong");
    expect(
        first.name == ".DVP.overlay.alpha",
        "first DVP overlay name is wrong");
    expect(
        first.load_memory_address == 0x1000U,
        "first DVP overlay LMA is wrong");
    expect(
        first.virtual_memory_address == 0U,
        "first DVP overlay VMA is wrong");
    expect(
        first.code_file_offset == kDvpCodeOffset,
        "first DVP overlay code file offset is wrong");
    expect(first.size == 0x10U, "first DVP overlay size is wrong");

    const auto& second = table.overlays[1];
    expect(
        second.overlay_section_index == kDvpSecondOverlaySectionIndex,
        "second DVP overlay section index is wrong");
    expect(
        second.code_section_index == kDvpCodeSectionIndex,
        "second DVP code section index is wrong");
    expect(
        second.name == ".DVP.overlay.beta",
        "second DVP overlay name is wrong");
    expect(
        second.load_memory_address == 0x1010U,
        "second DVP overlay LMA is wrong");
    expect(
        second.virtual_memory_address == 0x0800U,
        "second DVP overlay VMA is wrong");
    expect(
        second.code_file_offset == kDvpCodeOffset + 0x10U,
        "second DVP overlay code file offset is wrong");
    expect(second.size == 0x18U, "second DVP overlay size is wrong");
}

void test_malformed_dvp_overlay_metadata() {
    const auto table_header_offset =
        kIrxSectionHeaderOffset +
        kDvpTableSectionIndex * kSectionHeaderSize;
    const auto string_header_offset =
        kIrxSectionHeaderOffset +
        kDvpStringTableSectionIndex * kSectionHeaderSize;
    const auto first_overlay_header_offset =
        kIrxSectionHeaderOffset +
        kDvpFirstOverlaySectionIndex * kSectionHeaderSize;
    const auto second_overlay_header_offset =
        kIrxSectionHeaderOffset +
        kDvpSecondOverlaySectionIndex * kSectionHeaderSize;

    auto bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + table_header_offset + 36, 8);
    expect_span_rejected(
        bytes,
        "noncanonical DVP overlay entry size should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + table_header_offset + 20, 20);
    expect_span_rejected(
        bytes,
        "unaligned DVP overlay table size should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + table_header_offset + 24, 7);
    expect_span_rejected(
        bytes,
        "out-of-range DVP string-table link should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + table_header_offset + 24, kDvpCodeSectionIndex);
    expect_span_rejected(
        bytes,
        "DVP table link to a non-string section should be rejected");

    bytes = make_valid_dvp_elf();
    write_le16(bytes.data() + 44, 0);
    expect_span_rejected(
        bytes,
        "DVP overlay LMA without a PT_LOAD mapping should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + kProgramHeaderOffset + 16, 8);
    expect_span_rejected(
        bytes,
        "DVP overlay backed only by PT_LOAD memory should be rejected");

    bytes = make_valid_dvp_elf();
    bytes[kDvpStringTableOffset] = 'x';
    expect_span_rejected(
        bytes,
        "DVP string table without a leading null should be rejected");

    bytes = make_valid_dvp_elf();
    bytes[kDvpStringTableOffset + sizeof(kDvpOverlayNames) - 1U] = 'x';
    expect_span_rejected(
        bytes,
        "DVP string table without a trailing null should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(
        bytes.data() + kDvpTableOffset,
        static_cast<std::uint32_t>(sizeof(kDvpOverlayNames)));
    expect_span_rejected(
        bytes,
        "out-of-range DVP overlay name should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + kDvpTableOffset, 0);
    expect_span_rejected(bytes, "empty DVP overlay name should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(
        bytes.data() + kDvpTableOffset + 12,
        kDvpFirstRecordNameOffset);
    expect_span_rejected(
        bytes,
        "duplicate DVP overlay reference should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(
        bytes.data() + second_overlay_header_offset,
        kDvpFirstOverlayNameOffset);
    expect_span_rejected(
        bytes,
        "duplicate DVP overlay section name should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + kDvpTableOffset + 4, 0x2000);
    expect_span_rejected(
        bytes,
        "unbacked DVP overlay LMA should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + kDvpTableOffset + 4, 0x1028);
    expect_span_rejected(
        bytes,
        "DVP overlay spanning past its code section should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + kDvpTableOffset + 4, 0xfffffff8U);
    expect_span_rejected(
        bytes,
        "overflowing DVP overlay load range should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + kDvpTableOffset + 8, 0xfffffff8U);
    expect_span_rejected(
        bytes,
        "overflowing DVP overlay virtual range should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + first_overlay_header_offset + 20, 0);
    expect_span_rejected(bytes, "empty DVP overlay section should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + table_header_offset + 20, 12);
    expect_span_rejected(
        bytes,
        "DVP table and overlay-section count mismatch should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(
        bytes.data() + second_overlay_header_offset + 4,
        0x7ffff420U);
    expect_span_rejected(
        bytes,
        "multiple DVP overlay tables should be rejected");

    bytes = make_valid_dvp_elf();
    write_le32(bytes.data() + string_header_offset + 20, 0);
    expect_span_rejected(bytes, "empty DVP overlay string table should be rejected");
}

void test_export_marker_is_not_guessed_as_import() {
    auto bytes = make_valid_irx();
    write_le32(bytes.data() + kIrxTextOffset, 0x41c00000U);
    const auto report = openrc::inspect_elf(as_bytes(bytes));
    expect(
        report.iop_import_libraries.empty(),
        "IOP export marker should not be interpreted using guessed ordinal boundaries");
}

void test_malformed_irx_metadata() {
    auto bytes = make_valid_irx();
    for (std::size_t index = 26; index < 0x30; ++index) {
        bytes[kIrxModuleOffset + index] = 'x';
    }
    expect_span_rejected(bytes, ".iopmod name without a terminator should be rejected");

    bytes = make_valid_irx();
    bytes[kIrxModuleOffset + 32] = 'x';
    expect_span_rejected(bytes, "nonzero .iopmod name padding should be rejected");

    bytes = make_valid_irx();
    auto* module_section =
        bytes.data() + kIrxSectionHeaderOffset +
        kIrxModuleSectionIndex * kSectionHeaderSize;
    write_le32(module_section + 4, 1);
    expect_span_rejected(bytes, "wrong .iopmod section type should be rejected");

    bytes = make_valid_irx();
    module_section =
        bytes.data() + kIrxSectionHeaderOffset +
        kIrxModuleSectionIndex * kSectionHeaderSize;
    write_le32(module_section, 1);
    expect_span_rejected(bytes, "typed IOP module with a noncanonical name should be rejected");

    bytes = make_valid_irx();
    auto* relocation =
        bytes.data() + kIrxSectionHeaderOffset +
        kIrxRelocationSectionIndex * kSectionHeaderSize;
    write_le32(relocation + 24, 9);
    expect_span_rejected(bytes, "out-of-range relocation link should be rejected");

    bytes = make_valid_irx();
    relocation =
        bytes.data() + kIrxSectionHeaderOffset +
        kIrxRelocationSectionIndex * kSectionHeaderSize;
    write_le32(relocation + 36, 4);
    expect_span_rejected(bytes, "undersized relocation entries should be rejected");

    bytes = make_valid_irx();
    relocation =
        bytes.data() + kIrxSectionHeaderOffset +
        kIrxRelocationSectionIndex * kSectionHeaderSize;
    write_le32(relocation + 24, 1);
    expect_span_rejected(bytes, "relocation link to .text should be rejected");

    bytes = make_valid_irx();
    relocation =
        bytes.data() + kIrxSectionHeaderOffset +
        kIrxRelocationSectionIndex * kSectionHeaderSize;
    write_le32(relocation + 36, 16);
    expect_span_rejected(bytes, "noncanonical 16-byte SHT_REL entries should be rejected");
}

void test_iop_import_magic_collisions() {
    auto bytes = make_valid_irx();
    write_le32(bytes.data() + kIrxTextOffset + 4, 1);
    expect_no_iop_imports(
        bytes,
        "magic collision with a nonzero reserved link should be ignored");

    bytes = make_valid_irx();
    write_le32(bytes.data() + kIrxTextOffset + 20, 0x03e00009U);
    expect_no_iop_imports(bytes, "magic collision with an invalid stub should be ignored");

    bytes = make_valid_irx();
    auto* text =
        bytes.data() + kIrxSectionHeaderOffset +
        kIrxTextSectionIndex * kSectionHeaderSize;
    write_le32(text + 20, 36);
    expect_no_iop_imports(bytes, "unterminated import-like data should be ignored");

    bytes = make_valid_irx();
    write_le32(bytes.data() + kIrxTextOffset, 0);
    write_le32(bytes.data() + kIrxTextOffset + 0x7c, 0x41e00000U);
    expect_no_iop_imports(bytes, "magic collision at the end of a section should be ignored");
}

void test_identity_checks(const std::filesystem::path& directory) {
    auto bytes = make_valid_elf();
    bytes[5] = 2;
    expect_rejected(directory / "big-endian.elf", bytes, "big-endian ELF should be rejected");

    bytes = make_valid_elf();
    write_le16(bytes.data() + 18, 3);
    expect_rejected(directory / "non-mips.elf", bytes, "non-MIPS ELF should be rejected");

    bytes = make_valid_elf();
    bytes.resize(kElfHeaderSize - 1);
    expect_rejected(directory / "truncated-header.elf", bytes, "truncated ELF header should be rejected");
}

void test_malformed_bounds(const std::filesystem::path& directory) {
    auto bytes = make_valid_elf();
    write_le32(bytes.data() + 28, static_cast<std::uint32_t>(bytes.size() - 16));
    expect_rejected(
        directory / "program-table-outside.elf",
        bytes,
        "out-of-bounds program header table should be rejected");

    bytes = make_valid_elf();
    write_le32(bytes.data() + kProgramHeaderOffset + 4, static_cast<std::uint32_t>(bytes.size() - 8));
    write_le32(bytes.data() + kProgramHeaderOffset + 16, 0x20);
    expect_rejected(
        directory / "segment-outside.elf",
        bytes,
        "out-of-bounds segment should be rejected");

    bytes = make_valid_elf();
    auto* section = bytes.data() + kSectionHeaderOffset + kSectionHeaderSize;
    write_le32(section + 16, static_cast<std::uint32_t>(bytes.size() - 4));
    write_le32(section + 20, 8);
    expect_rejected(
        directory / "section-outside.elf",
        bytes,
        "out-of-bounds section should be rejected");

    bytes = make_valid_elf();
    write_le32(bytes.data() + kProgramHeaderOffset + 8, 0xfffffff0U);
    write_le32(bytes.data() + kProgramHeaderOffset + 20, 0x40);
    expect_rejected(
        directory / "address-overflow.elf",
        bytes,
        "segment outside the ELF32 address space should be rejected");

    bytes = make_valid_elf();
    write_le32(bytes.data() + kProgramHeaderOffset + 12, 0xfffffff0U);
    write_le32(bytes.data() + kProgramHeaderOffset + 20, 0x40);
    expect_rejected(
        directory / "physical-address-overflow.elf",
        bytes,
        "segment outside the ELF32 physical address space should be rejected");
}

void test_malformed_section_names(const std::filesystem::path& directory) {
    const auto string_table_header_offset =
        kSectionHeaderOffset + 3 * kSectionHeaderSize;
    const auto text_header_offset = kSectionHeaderOffset + kSectionHeaderSize;

    auto bytes = make_valid_elf();
    write_le32(bytes.data() + string_table_header_offset + 4, 1);
    expect_rejected(
        directory / "section-names-not-strtab.elf",
        bytes,
        "section-name table with the wrong section type should be rejected");

    bytes = make_valid_elf();
    write_le32(
        bytes.data() + text_header_offset,
        static_cast<std::uint32_t>(sizeof(kSectionNames)));
    expect_rejected(
        directory / "section-name-offset-outside.elf",
        bytes,
        "out-of-bounds section name offset should be rejected");

    bytes = make_valid_elf();
    write_le32(
        bytes.data() + text_header_offset,
        static_cast<std::uint32_t>(sizeof(kSectionNames) - 1));
    bytes[kSectionNameTableOffset + sizeof(kSectionNames) - 1] = 'x';
    expect_rejected(
        directory / "section-name-without-null.elf",
        bytes,
        "section name without a terminating null byte should be rejected");

    bytes = make_valid_elf();
    bytes[kSectionNameTableOffset] = 'x';
    expect_rejected(
        directory / "section-names-missing-leading-null.elf",
        bytes,
        "section-name table without a leading null byte should be rejected");

    bytes = make_valid_elf();
    write_le16(bytes.data() + 50, 0);
    expect_rejected(
        directory / "named-sections-without-table.elf",
        bytes,
        "named sections without a section-name table should be rejected");
}

void test_inventory_limits(const std::filesystem::path& directory) {
    auto bytes = make_valid_elf();
    write_le16(bytes.data() + 44, 4097);
    expect_rejected(
        directory / "too-many-program-headers.elf",
        bytes,
        "excessive program header count should be rejected before allocation");

    bytes = make_valid_elf();
    write_le16(bytes.data() + 48, 16385);
    expect_rejected(
        directory / "too-many-section-headers.elf",
        bytes,
        "excessive section header count should be rejected before allocation");
}

} // namespace

int main() {
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto test_directory = std::filesystem::temp_directory_path() /
        ("OpenRC-elf-tests-" + std::to_string(unique_suffix));

    try {
        std::filesystem::create_directories(test_directory);
        test_valid_elf(test_directory);
        test_valid_irx_inventory();
        test_irx_consistency_is_diagnostic();
        test_iop_module_is_detected_by_type_without_section_names();
        test_unknown_relocation_type_is_preserved();
        test_valid_dvp_overlay_inventory();
        test_malformed_dvp_overlay_metadata();
        test_export_marker_is_not_guessed_as_import();
        test_malformed_irx_metadata();
        test_iop_import_magic_collisions();
        test_identity_checks(test_directory);
        test_malformed_bounds(test_directory);
        test_malformed_section_names(test_directory);
        test_inventory_limits(test_directory);
        std::filesystem::remove_all(test_directory);
        std::cout << "OpenRC ELF tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(test_directory, ignored);
        std::cerr << "OpenRC ELF tests failed: " << error.what() << '\n';
        return 1;
    }
}
