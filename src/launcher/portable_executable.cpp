#include "portable_executable.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace openrc::launcher {
namespace {

constexpr std::uint16_t kPe32Machine = 0x014cU;
constexpr std::uint16_t kPe64Machine = 0x8664U;
constexpr std::uint16_t kPe32OptionalMagic = 0x010bU;
constexpr std::uint16_t kPe64OptionalMagic = 0x020bU;
constexpr std::size_t kMaximumExecutableBytes = 512U * 1024U * 1024U;
constexpr std::size_t kMaximumSections = 96U;
constexpr std::size_t kMaximumImports = 4096U;
constexpr std::size_t kMaximumImportNameBytes = 512U;
constexpr std::size_t kDataDirectoryBytes = 8U;
constexpr std::size_t kImportDirectoryIndex = 1U;
constexpr std::size_t kDelayImportDirectoryIndex = 13U;
constexpr std::size_t kImportDescriptorBytes = 20U;
constexpr std::size_t kDelayImportDescriptorBytes = 32U;
constexpr std::uint32_t kDelayImportRvaAttribute = 0x1U;

constexpr std::array<std::string_view, 5U> kForbiddenRuntimeImportPrefixes{
    "libc++",
    "libunwind",
    "libstdc++",
    "libgcc",
    "libwinpthread",
};

[[nodiscard]] bool is_forbidden_runtime_import(
    const std::string_view imported_module) noexcept {
    const auto separator = imported_module.find_last_of("/\\");
    const auto basename = separator == std::string_view::npos
                              ? imported_module
                              : imported_module.substr(separator + 1U);
    if (basename.empty()) {
        return false;
    }

    const auto extension = basename.find_last_of('.');
    if (extension != std::string_view::npos &&
        basename.substr(extension) != ".dll") {
        return false;
    }

    return std::ranges::any_of(
        kForbiddenRuntimeImportPrefixes,
        [basename](const std::string_view prefix) {
            return basename.starts_with(prefix);
        });
}

[[nodiscard]] bool contains_range(
    const std::size_t size,
    const std::size_t offset,
    const std::size_t length) noexcept {
    return offset <= size && length <= size - offset;
}

void require_range(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::size_t length,
    const std::string_view description) {
    if (!contains_range(bytes.size(), offset, length)) {
        throw std::runtime_error(
            "truncated PE while reading " + std::string(description));
    }
}

[[nodiscard]] std::uint16_t read_u16(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::string_view description) {
    require_range(bytes, offset, 2U, description);
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t read_u32(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::string_view description) {
    require_range(bytes, offset, 4U, description);
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::size_t checked_add(
    const std::size_t left,
    const std::size_t right,
    const std::string_view description) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw std::runtime_error(
            "PE offset overflow while reading " + std::string(description));
    }
    return left + right;
}

[[nodiscard]] std::uint32_t checked_add_rva(
    const std::uint32_t rva,
    const std::size_t offset,
    const std::string_view description) {
    if (offset > static_cast<std::size_t>(
                     std::numeric_limits<std::uint32_t>::max() - rva)) {
        throw std::runtime_error(
            "PE RVA overflow while reading " + std::string(description));
    }
    return rva + static_cast<std::uint32_t>(offset);
}

[[nodiscard]] std::vector<std::uint8_t> read_executable(
    const std::filesystem::path& path) {
    std::error_code error;
    const auto file_size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error(
            "cannot read the runtime size: " + error.message());
    }
    if (file_size == 0U || file_size > kMaximumExecutableBytes ||
        file_size > static_cast<std::uintmax_t>(
                        std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("runtime executable size is outside the accepted bounds");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open the runtime executable");
    }
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        throw std::runtime_error("cannot read the complete runtime executable");
    }
    return bytes;
}

struct Section {
    std::uint32_t virtual_address = 0U;
    std::uint32_t virtual_size = 0U;
    std::uint32_t raw_offset = 0U;
    std::uint32_t raw_size = 0U;
};

[[nodiscard]] std::optional<std::size_t> file_offset_for_rva(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<Section>& sections,
    const std::uint32_t headers_size,
    const std::uint32_t rva,
    const std::size_t length = 1U) {
    if (rva < headers_size &&
        length <= static_cast<std::size_t>(headers_size - rva) &&
        contains_range(bytes.size(), static_cast<std::size_t>(rva), length)) {
        return static_cast<std::size_t>(rva);
    }

    for (const auto& section : sections) {
        const auto span = std::max(section.virtual_size, section.raw_size);
        if (rva < section.virtual_address) {
            continue;
        }
        const auto relative = rva - section.virtual_address;
        if (relative >= span || relative >= section.raw_size ||
            length > static_cast<std::size_t>(section.raw_size - relative) ||
            length > static_cast<std::size_t>(span - relative)) {
            continue;
        }
        const auto offset = checked_add(
            static_cast<std::size_t>(section.raw_offset),
            static_cast<std::size_t>(relative),
            "RVA");
        if (contains_range(bytes.size(), offset, length)) {
            return offset;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string read_import_name(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<Section>& sections,
    const std::uint32_t headers_size,
    const std::uint32_t name_rva) {
    std::string name;
    name.reserve(32U);
    for (std::size_t index = 0U; index < kMaximumImportNameBytes; ++index) {
        const auto character_rva = checked_add_rva(
            name_rva,
            index,
            "import name");
        const auto character_offset = file_offset_for_rva(
            bytes,
            sections,
            headers_size,
            character_rva);
        if (!character_offset) {
            throw std::runtime_error(
                "PE import module name is not fully file-backed");
        }
        const auto character = bytes[*character_offset];
        if (character == 0U) {
            if (name.empty()) {
                throw std::runtime_error("PE import has an empty module name");
            }
            const auto separator = name.find_last_of("/\\");
            if ((separator != std::string::npos &&
                 separator + 1U == name.size()) ||
                name.back() == '.' || name.back() == ' ') {
                throw std::runtime_error(
                    "PE import module name has an ambiguous basename");
            }
            return name;
        }
        if (character < 0x20U || character > 0x7eU) {
            throw std::runtime_error("PE import module name is not ASCII");
        }
        name.push_back(static_cast<char>(std::tolower(character)));
    }
    throw std::runtime_error("PE import module name is not terminated");
}

struct DataDirectory {
    std::uint32_t rva = 0U;
    std::uint32_t size = 0U;
};

[[nodiscard]] DataDirectory read_data_directory(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t optional_offset,
    const std::size_t optional_size,
    const std::size_t data_directories_offset,
    const std::uint32_t directory_count,
    const std::size_t directory_index,
    const std::string_view description) {
    if (directory_count <= directory_index) {
        return {};
    }

    const auto relative_offset = checked_add(
        data_directories_offset,
        directory_index * kDataDirectoryBytes,
        description);
    const auto relative_end = checked_add(
        relative_offset,
        kDataDirectoryBytes,
        description);
    if (optional_size < relative_end) {
        throw std::runtime_error(
            "PE optional header truncates the " + std::string(description));
    }

    const auto directory_offset = checked_add(
        optional_offset,
        relative_offset,
        description);
    const auto rva = read_u32(bytes, directory_offset, description);
    const auto size = read_u32(
        bytes,
        directory_offset + 4U,
        description);
    if ((rva == 0U) != (size == 0U)) {
        throw std::runtime_error(
            "PE " + std::string(description) + " has inconsistent bounds");
    }
    return DataDirectory{rva, size};
}

void require_directory_bounds(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<Section>& sections,
    const std::uint32_t headers_size,
    const DataDirectory directory,
    const std::size_t descriptor_size,
    const std::string_view description) {
    if (directory.rva == 0U) {
        return;
    }
    if (directory.size < descriptor_size ||
        directory.size > kMaximumImports * descriptor_size ||
        directory.size % descriptor_size != 0U) {
        throw std::runtime_error(
            "PE " + std::string(description) +
            " is outside the accepted bounds");
    }
    if (!file_offset_for_rva(
            bytes,
            sections,
            headers_size,
            directory.rva,
            directory.size)) {
        throw std::runtime_error(
            "PE " + std::string(description) +
            " is not fully file-backed");
    }
}

void append_standard_imports(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<Section>& sections,
    const std::uint32_t headers_size,
    const DataDirectory directory,
    std::vector<std::string>& imports) {
    if (directory.rva == 0U) {
        return;
    }
    require_directory_bounds(
        bytes,
        sections,
        headers_size,
        directory,
        kImportDescriptorBytes,
        "import directory");

    const auto descriptor_capacity = static_cast<std::size_t>(
        directory.size / kImportDescriptorBytes);
    bool found_terminator = false;
    for (std::size_t index = 0U; index < descriptor_capacity; ++index) {
        const auto descriptor_rva = checked_add_rva(
            directory.rva,
            index * kImportDescriptorBytes,
            "import descriptor");
        const auto descriptor_offset = file_offset_for_rva(
            bytes,
            sections,
            headers_size,
            descriptor_rva,
            kImportDescriptorBytes);
        if (!descriptor_offset) {
            throw std::runtime_error(
                "PE import descriptor is not fully file-backed");
        }

        bool is_zero = true;
        for (std::size_t byte_index = 0U;
             byte_index < kImportDescriptorBytes;
             ++byte_index) {
            if (bytes[*descriptor_offset + byte_index] != 0U) {
                is_zero = false;
                break;
            }
        }
        if (is_zero) {
            found_terminator = true;
            break;
        }

        const auto name_rva = read_u32(
            bytes,
            *descriptor_offset + 12U,
            "import name RVA");
        if (name_rva == 0U) {
            throw std::runtime_error("PE import has a null module-name RVA");
        }
        imports.push_back(read_import_name(
            bytes,
            sections,
            headers_size,
            name_rva));
    }
    if (!found_terminator) {
        throw std::runtime_error(
            "PE import directory has no bounded terminator");
    }
}

void append_delay_imports(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<Section>& sections,
    const std::uint32_t headers_size,
    const DataDirectory directory,
    std::vector<std::string>& imports) {
    if (directory.rva == 0U) {
        return;
    }
    require_directory_bounds(
        bytes,
        sections,
        headers_size,
        directory,
        kDelayImportDescriptorBytes,
        "delay-import directory");

    const auto descriptor_capacity = static_cast<std::size_t>(
        directory.size / kDelayImportDescriptorBytes);
    bool found_terminator = false;
    for (std::size_t index = 0U; index < descriptor_capacity; ++index) {
        const auto descriptor_rva = checked_add_rva(
            directory.rva,
            index * kDelayImportDescriptorBytes,
            "delay-import descriptor");
        const auto descriptor_offset = file_offset_for_rva(
            bytes,
            sections,
            headers_size,
            descriptor_rva,
            kDelayImportDescriptorBytes);
        if (!descriptor_offset) {
            throw std::runtime_error(
                "PE delay-import descriptor is not fully file-backed");
        }

        bool is_zero = true;
        for (std::size_t byte_index = 0U;
             byte_index < kDelayImportDescriptorBytes;
             ++byte_index) {
            if (bytes[*descriptor_offset + byte_index] != 0U) {
                is_zero = false;
                break;
            }
        }
        if (is_zero) {
            found_terminator = true;
            break;
        }

        const auto attributes = read_u32(
            bytes,
            *descriptor_offset,
            "delay-import attributes");
        if ((attributes & ~kDelayImportRvaAttribute) != 0U) {
            throw std::runtime_error(
                "PE delay-import descriptor has unknown attributes");
        }
        if ((attributes & kDelayImportRvaAttribute) == 0U) {
            throw std::runtime_error(
                "PE delay-import descriptor is not RVA-based");
        }

        const auto name_rva = read_u32(
            bytes,
            *descriptor_offset + 4U,
            "delay-import name RVA");
        if (name_rva == 0U) {
            throw std::runtime_error(
                "PE delay import has a null module-name RVA");
        }
        imports.push_back(read_import_name(
            bytes,
            sections,
            headers_size,
            name_rva));
    }
    if (!found_terminator) {
        throw std::runtime_error(
            "PE delay-import directory has no bounded terminator");
    }
}

[[nodiscard]] std::vector<std::string> inspect_imports(
    const std::vector<std::uint8_t>& bytes) {
    require_range(bytes, 0U, 0x40U, "DOS header");
    if (bytes[0U] != static_cast<std::uint8_t>('M') ||
        bytes[1U] != static_cast<std::uint8_t>('Z')) {
        throw std::runtime_error("runtime does not have a DOS/PE header");
    }

    const auto pe_offset = static_cast<std::size_t>(
        read_u32(bytes, 0x3cU, "PE header offset"));
    require_range(bytes, pe_offset, 24U, "PE and COFF headers");
    if (read_u32(bytes, pe_offset, "PE signature") != 0x00004550U) {
        throw std::runtime_error("runtime has an invalid PE signature");
    }

    const auto coff_offset = checked_add(pe_offset, 4U, "COFF header");
    const auto machine = read_u16(bytes, coff_offset, "COFF machine");
    const auto expected_machine = sizeof(void*) == 8U
                                      ? kPe64Machine
                                      : kPe32Machine;
    if (machine != expected_machine) {
        throw std::runtime_error(
            "runtime architecture does not match the launcher architecture");
    }

    const auto section_count = static_cast<std::size_t>(
        read_u16(bytes, coff_offset + 2U, "COFF section count"));
    if (section_count == 0U || section_count > kMaximumSections) {
        throw std::runtime_error("PE section count is outside the accepted bounds");
    }

    const auto optional_size = static_cast<std::size_t>(
        read_u16(bytes, coff_offset + 16U, "COFF optional-header size"));
    const auto optional_offset = checked_add(coff_offset, 20U, "optional header");
    require_range(bytes, optional_offset, optional_size, "optional header");

    const auto optional_magic = read_u16(bytes, optional_offset, "optional-header magic");
    const auto expected_optional_magic = sizeof(void*) == 8U
                                             ? kPe64OptionalMagic
                                             : kPe32OptionalMagic;
    if (optional_magic != expected_optional_magic) {
        throw std::runtime_error(
            "runtime PE format does not match the launcher architecture");
    }

    const auto number_of_directories_offset =
        sizeof(void*) == 8U ? 108U : 92U;
    const auto data_directories_offset = sizeof(void*) == 8U ? 112U : 96U;
    const auto required_fixed_optional_bytes = checked_add(
        number_of_directories_offset,
        4U,
        "optional-header fixed fields");
    if (optional_size < required_fixed_optional_bytes) {
        throw std::runtime_error(
            "PE optional header omits required fixed fields");
    }
    const auto directory_count = read_u32(
        bytes,
        optional_offset + number_of_directories_offset,
        "data-directory count");
    const auto headers_size = read_u32(
        bytes,
        optional_offset + 60U,
        "PE header size");
    const auto import_directory = read_data_directory(
        bytes,
        optional_offset,
        optional_size,
        data_directories_offset,
        directory_count,
        kImportDirectoryIndex,
        "import directory");
    const auto delay_import_directory = read_data_directory(
        bytes,
        optional_offset,
        optional_size,
        data_directories_offset,
        directory_count,
        kDelayImportDirectoryIndex,
        "delay-import directory");
    if (import_directory.rva == 0U && delay_import_directory.rva == 0U) {
        return {};
    }

    const auto section_table_offset = checked_add(
        optional_offset,
        optional_size,
        "section table");
    require_range(
        bytes,
        section_table_offset,
        section_count * 40U,
        "section table");
    std::vector<Section> sections;
    sections.reserve(section_count);
    for (std::size_t index = 0U; index < section_count; ++index) {
        const auto offset = section_table_offset + index * 40U;
        sections.push_back(Section{
            read_u32(bytes, offset + 12U, "section virtual address"),
            read_u32(bytes, offset + 8U, "section virtual size"),
            read_u32(bytes, offset + 20U, "section raw offset"),
            read_u32(bytes, offset + 16U, "section raw size"),
        });
    }

    std::vector<std::string> imports;
    append_standard_imports(
        bytes,
        sections,
        headers_size,
        import_directory,
        imports);
    append_delay_imports(
        bytes,
        sections,
        headers_size,
        delay_import_directory,
        imports);
    return imports;
}

} // namespace

PortableExecutableCheck check_runtime_executable(
    const std::filesystem::path& path) noexcept {
    try {
        const auto bytes = read_executable(path);
        const auto imports = inspect_imports(bytes);
        for (const auto& imported_module : imports) {
            if (is_forbidden_runtime_import(imported_module)) {
                return PortableExecutableCheck{
                    false,
                    "runtime imports forbidden compiler support library " +
                        imported_module};
            }
        }
        return PortableExecutableCheck{true, {}};
    } catch (const std::exception& error) {
        return PortableExecutableCheck{
            false,
            std::string("runtime executable validation failed: ") + error.what()};
    } catch (...) {
        return PortableExecutableCheck{
            false,
            "runtime executable validation failed with an unknown error"};
    }
}

} // namespace openrc::launcher
