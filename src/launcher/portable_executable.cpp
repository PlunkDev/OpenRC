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

constexpr std::array<std::string_view, 5U> kForbiddenRuntimeImportPrefixes{
    "libc++",
    "libunwind",
    "libstdc++",
    "libgcc",
    "libwinpthread",
};

[[nodiscard]] bool is_forbidden_runtime_import(
    const std::string_view imported_module) noexcept {
    if (!imported_module.ends_with(".dll")) {
        return false;
    }
    return std::ranges::any_of(
        kForbiddenRuntimeImportPrefixes,
        [imported_module](const std::string_view prefix) {
            return imported_module.starts_with(prefix);
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
    const std::uint32_t rva) {
    if (rva < headers_size && rva < bytes.size()) {
        return static_cast<std::size_t>(rva);
    }

    for (const auto& section : sections) {
        const auto span = std::max(section.virtual_size, section.raw_size);
        if (rva < section.virtual_address) {
            continue;
        }
        const auto relative = rva - section.virtual_address;
        if (relative >= span || relative >= section.raw_size) {
            continue;
        }
        const auto offset = checked_add(
            static_cast<std::size_t>(section.raw_offset),
            static_cast<std::size_t>(relative),
            "RVA");
        if (offset < bytes.size()) {
            return offset;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string read_import_name(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset) {
    std::string name;
    name.reserve(32U);
    for (std::size_t index = 0U; index < kMaximumImportNameBytes; ++index) {
        const auto character_offset = checked_add(offset, index, "import name");
        require_range(bytes, character_offset, 1U, "import name");
        const auto character = bytes[character_offset];
        if (character == 0U) {
            if (name.empty()) {
                throw std::runtime_error("PE import has an empty module name");
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
    require_range(
        bytes,
        optional_offset,
        data_directories_offset + 16U,
        "optional-header data directories");
    if (optional_size < data_directories_offset + 16U) {
        throw std::runtime_error("PE optional header omits the import directory");
    }

    const auto directory_count = read_u32(
        bytes,
        optional_offset + number_of_directories_offset,
        "data-directory count");
    if (directory_count <= 1U) {
        return {};
    }

    const auto headers_size = read_u32(
        bytes,
        optional_offset + 60U,
        "PE header size");
    const auto import_rva = read_u32(
        bytes,
        optional_offset + data_directories_offset + 8U,
        "import-directory RVA");
    const auto import_size = read_u32(
        bytes,
        optional_offset + data_directories_offset + 12U,
        "import-directory size");
    if ((import_rva == 0U) != (import_size == 0U)) {
        throw std::runtime_error("PE import directory has inconsistent bounds");
    }
    if (import_rva == 0U) {
        return {};
    }
    if (import_size < 20U || import_size > kMaximumImports * 20U) {
        throw std::runtime_error("PE import directory is outside the accepted bounds");
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

    const auto import_offset = file_offset_for_rva(
        bytes,
        sections,
        headers_size,
        import_rva);
    if (!import_offset) {
        throw std::runtime_error("PE import-directory RVA is not file-backed");
    }

    std::vector<std::string> imports;
    const auto descriptor_capacity = static_cast<std::size_t>(import_size / 20U);
    bool found_terminator = false;
    for (std::size_t index = 0U; index < descriptor_capacity; ++index) {
        const auto descriptor_offset = checked_add(
            *import_offset,
            index * 20U,
            "import descriptor");
        require_range(bytes, descriptor_offset, 20U, "import descriptor");

        bool is_zero = true;
        for (std::size_t byte_index = 0U; byte_index < 20U; ++byte_index) {
            if (bytes[descriptor_offset + byte_index] != 0U) {
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
            descriptor_offset + 12U,
            "import name RVA");
        if (name_rva == 0U) {
            throw std::runtime_error("PE import has a null module-name RVA");
        }
        const auto name_offset = file_offset_for_rva(
            bytes,
            sections,
            headers_size,
            name_rva);
        if (!name_offset) {
            throw std::runtime_error("PE import-name RVA is not file-backed");
        }
        imports.push_back(read_import_name(bytes, *name_offset));
    }
    if (!found_terminator) {
        throw std::runtime_error("PE import directory has no bounded terminator");
    }
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
