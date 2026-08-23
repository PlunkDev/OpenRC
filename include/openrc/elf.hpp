#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

struct ElfVirtualAddressRange {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
};

struct ElfProgramHeader {
    std::uint32_t type = 0;
    std::uint32_t file_offset = 0;
    std::uint32_t virtual_address = 0;
    std::uint32_t file_size = 0;
    std::uint32_t memory_size = 0;
    std::uint32_t flags = 0;
    std::uint32_t alignment = 0;
};

struct ElfSectionHeader {
    std::string name;
    std::uint32_t name_offset = 0;
    std::uint32_t type = 0;
    std::uint32_t flags = 0;
    std::uint32_t virtual_address = 0;
    std::uint32_t file_offset = 0;
    std::uint32_t size = 0;
    std::uint32_t link = 0;
    std::uint32_t info = 0;
    std::uint32_t alignment = 0;
    std::uint32_t entry_size = 0;
};

struct ElfRelocationTypeCount {
    std::uint8_t type = 0;
    std::uint32_t count = 0;
};

struct ElfRelocationSummary {
    std::uint16_t section_index = 0;
    std::uint32_t symbol_table_section_index = 0;
    std::uint32_t target_section_index = 0;
    std::uint32_t entry_count = 0;
    std::vector<ElfRelocationTypeCount> types;
};

struct IopModuleInfo {
    std::uint16_t section_index = 0;
    std::uint32_t module_info_address = 0;
    std::uint32_t entry_point = 0;
    std::uint32_t global_pointer = 0;
    std::uint32_t text_size = 0;
    std::uint32_t data_size = 0;
    std::uint32_t bss_size = 0;
    std::uint16_t version = 0;
    std::string name;
    std::uint64_t allocated_text_size = 0;
    std::uint64_t allocated_data_size = 0;
    std::uint64_t allocated_bss_size = 0;
    bool text_size_matches_allocated_sections = false;
    bool data_size_matches_allocated_sections = false;
    bool bss_size_matches_allocated_sections = false;
};

struct IopImportLibrary {
    std::uint16_t section_index = 0;
    std::uint32_t section_offset = 0;
    std::uint32_t virtual_address = 0;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::string name;
    std::vector<std::uint16_t> ordinals;
};

struct ElfReport {
    std::filesystem::path executable_path;
    std::uintmax_t file_size = 0;
    std::uint16_t type = 0;
    std::uint16_t machine = 0;
    std::uint32_t flags = 0;
    std::uint32_t entry_point = 0;
    std::uint16_t program_header_count = 0;
    std::uint16_t section_header_count = 0;
    std::uint16_t section_name_table_index = 0;
    std::vector<ElfProgramHeader> program_headers;
    std::vector<ElfSectionHeader> section_headers;
    std::uint16_t loadable_segment_count = 0;
    std::optional<ElfVirtualAddressRange> loadable_virtual_address_range;
    std::optional<IopModuleInfo> iop_module_info;
    std::vector<ElfRelocationSummary> relocation_summaries;
    std::vector<IopImportLibrary> iop_import_libraries;
};

class ElfError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] ElfReport inspect_elf(std::span<const std::byte> executable_bytes);
[[nodiscard]] ElfReport inspect_elf(const std::filesystem::path& executable_path);

} // namespace openrc
