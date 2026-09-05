#include "portable_executable.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kPeOffset = 0x80U;
constexpr std::size_t kCoffOffset = kPeOffset + 4U;
constexpr std::size_t kOptionalOffset = kCoffOffset + 20U;
constexpr std::size_t kRawSectionOffset = 0x200U;
constexpr std::uint32_t kSectionRva = 0x1000U;
constexpr std::size_t kImportNameOffset = kRawSectionOffset + 0x40U;
constexpr std::size_t kDelayDescriptorOffset = kRawSectionOffset + 0x80U;
constexpr std::uint32_t kDelayDescriptorRva = kSectionRva + 0x80U;
constexpr std::size_t kDelayNameOffset = kRawSectionOffset + 0xe0U;
constexpr std::uint32_t kDelayNameRva = kSectionRva + 0xe0U;
constexpr std::uint32_t kDelayDirectoryBytes = 64U;

void write_u16(
    std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value & 0xffU);
    bytes.at(offset + 1U) = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(
    std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes.at(offset + index) = static_cast<std::uint8_t>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
}

[[nodiscard]] std::vector<std::uint8_t> make_pe(
    const std::string_view import_name,
    const bool matching_architecture = true) {
    const auto is_64_bit = sizeof(void*) == 8U;
    const auto optional_size = is_64_bit ? 0xf0U : 0xe0U;
    const auto directory_count_offset = is_64_bit ? 108U : 92U;
    const auto directories_offset = is_64_bit ? 112U : 96U;
    const auto expected_machine = static_cast<std::uint16_t>(
        is_64_bit ? 0x8664U : 0x014cU);
    const auto foreign_machine = static_cast<std::uint16_t>(
        is_64_bit ? 0x014cU : 0x8664U);

    std::vector<std::uint8_t> bytes(0x400U, 0U);
    bytes[0U] = static_cast<std::uint8_t>('M');
    bytes[1U] = static_cast<std::uint8_t>('Z');
    write_u32(bytes, 0x3cU, static_cast<std::uint32_t>(kPeOffset));
    write_u32(bytes, kPeOffset, 0x00004550U);
    write_u16(
        bytes,
        kCoffOffset,
        matching_architecture ? expected_machine : foreign_machine);
    write_u16(bytes, kCoffOffset + 2U, 1U);
    write_u16(
        bytes,
        kCoffOffset + 16U,
        static_cast<std::uint16_t>(optional_size));
    write_u16(
        bytes,
        kOptionalOffset,
        static_cast<std::uint16_t>(is_64_bit ? 0x020bU : 0x010bU));
    write_u32(bytes, kOptionalOffset + 60U, 0x200U);
    write_u32(bytes, kOptionalOffset + directory_count_offset, 16U);
    write_u32(
        bytes,
        kOptionalOffset + directories_offset + 8U,
        kSectionRva);
    write_u32(bytes, kOptionalOffset + directories_offset + 12U, 40U);

    const auto section_offset = kOptionalOffset + optional_size;
    write_u32(bytes, section_offset + 8U, 0x200U);
    write_u32(bytes, section_offset + 12U, kSectionRva);
    write_u32(bytes, section_offset + 16U, 0x200U);
    write_u32(
        bytes,
        section_offset + 20U,
        static_cast<std::uint32_t>(kRawSectionOffset));

    write_u32(
        bytes,
        kRawSectionOffset + 12U,
        kSectionRva + 0x40U);
    for (std::size_t index = 0U; index < import_name.size(); ++index) {
        bytes.at(kImportNameOffset + index) =
            static_cast<std::uint8_t>(import_name[index]);
    }
    return bytes;
}

struct DelayImportOptions {
    std::uint32_t attributes = 0x1U;
    std::uint32_t directory_rva = kDelayDescriptorRva;
    std::uint32_t directory_size = kDelayDirectoryBytes;
    std::uint32_t name_rva = kDelayNameRva;
    bool terminated = true;
};

[[nodiscard]] std::vector<std::uint8_t> make_delay_pe(
    const std::string_view import_name,
    const DelayImportOptions options = {}) {
    auto bytes = make_pe("KERNEL32.dll");
    const auto is_64_bit = sizeof(void*) == 8U;
    const auto directories_offset = is_64_bit ? 112U : 96U;
    constexpr std::size_t delay_directory_index = 13U;
    const auto delay_directory_offset =
        kOptionalOffset + directories_offset + delay_directory_index * 8U;
    write_u32(bytes, delay_directory_offset, options.directory_rva);
    write_u32(bytes, delay_directory_offset + 4U, options.directory_size);

    write_u32(bytes, kDelayDescriptorOffset, options.attributes);
    write_u32(bytes, kDelayDescriptorOffset + 4U, options.name_rva);
    write_u32(bytes, kDelayDescriptorOffset + 8U, kSectionRva + 0x120U);
    write_u32(bytes, kDelayDescriptorOffset + 12U, kSectionRva + 0x130U);
    write_u32(bytes, kDelayDescriptorOffset + 16U, kSectionRva + 0x140U);
    if (!options.terminated) {
        write_u32(bytes, kDelayDescriptorOffset + 32U, 0x1U);
        write_u32(
            bytes,
            kDelayDescriptorOffset + 36U,
            options.name_rva);
    }

    for (std::size_t index = 0U; index < import_name.size(); ++index) {
        bytes.at(kDelayNameOffset + index) =
            static_cast<std::uint8_t>(import_name[index]);
    }
    return bytes;
}

void write_file(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("could not create synthetic PE file");
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("could not write synthetic PE file");
    }
}

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path() /
                ("openrc-pe-check-" + std::to_string(suffix));
        if (!std::filesystem::create_directory(path_)) {
            throw std::runtime_error("could not create test directory");
        }
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void run_tests() {
    const TemporaryDirectory temporary_directory;

    const auto accepted_path = temporary_directory.path() / "accepted.exe";
    write_file(accepted_path, make_pe("KERNEL32.dll"));
    const auto accepted =
        openrc::launcher::check_runtime_executable(accepted_path);
    expect(accepted.accepted, "system-only synthetic PE was rejected");

    const auto libcxx_path = temporary_directory.path() / "libcxx.exe";
    write_file(libcxx_path, make_pe("libc++.dll"));
    const auto libcxx = openrc::launcher::check_runtime_executable(libcxx_path);
    expect(!libcxx.accepted, "libc++.dll import was accepted");
    expect(
        libcxx.detail.find("libc++.dll") != std::string::npos,
        "libc++.dll rejection did not identify the dependency");

    const auto unwind_path = temporary_directory.path() / "unwind.exe";
    write_file(unwind_path, make_pe("LiBuNwInD.DlL"));
    const auto unwind = openrc::launcher::check_runtime_executable(unwind_path);
    expect(!unwind.accepted, "case-insensitive libunwind.dll import was accepted");

    const auto future_compiler_runtime_path =
        temporary_directory.path() / "future-compiler-runtime.exe";
    write_file(future_compiler_runtime_path, make_pe("libgcc_s_future-9.dll"));
    const auto future_compiler_runtime =
        openrc::launcher::check_runtime_executable(future_compiler_runtime_path);
    expect(
        !future_compiler_runtime.accepted,
        "unknown libgcc runtime-family import was accepted");

    const auto backslash_path_path =
        temporary_directory.path() / "backslash-path.exe";
    write_file(backslash_path_path, make_pe("toolchain\\libc++.dll"));
    const auto backslash_path =
        openrc::launcher::check_runtime_executable(backslash_path_path);
    expect(
        !backslash_path.accepted,
        "path-qualified libc++.dll import was accepted");

    const auto slash_path_path = temporary_directory.path() / "slash-path.exe";
    write_file(slash_path_path, make_pe("toolchain/libunwind.dll"));
    const auto slash_path =
        openrc::launcher::check_runtime_executable(slash_path_path);
    expect(
        !slash_path.accepted,
        "forward-slash-qualified libunwind.dll import was accepted");

    const auto extensionless_path =
        temporary_directory.path() / "extensionless.exe";
    write_file(extensionless_path, make_pe("libstdc++"));
    const auto extensionless =
        openrc::launcher::check_runtime_executable(extensionless_path);
    expect(
        !extensionless.accepted,
        "extensionless compiler-runtime import was accepted");

    const auto trailing_dot_path =
        temporary_directory.path() / "trailing-dot.exe";
    write_file(trailing_dot_path, make_pe("libunwind.dll."));
    const auto trailing_dot =
        openrc::launcher::check_runtime_executable(trailing_dot_path);
    expect(
        !trailing_dot.accepted,
        "compiler-runtime import with a trailing dot was accepted");

    const auto trailing_space_path =
        temporary_directory.path() / "trailing-space.exe";
    write_file(trailing_space_path, make_pe("libc++.dll "));
    const auto trailing_space =
        openrc::launcher::check_runtime_executable(trailing_space_path);
    expect(
        !trailing_space.accepted,
        "compiler-runtime import with a trailing space was accepted");

    const auto accepted_delay_path =
        temporary_directory.path() / "accepted-delay.exe";
    write_file(accepted_delay_path, make_delay_pe("USER32.dll"));
    const auto accepted_delay =
        openrc::launcher::check_runtime_executable(accepted_delay_path);
    expect(
        accepted_delay.accepted,
        "system-only delay import was rejected");

    const auto forbidden_delay_path =
        temporary_directory.path() / "forbidden-delay.exe";
    write_file(forbidden_delay_path, make_delay_pe("libunwind.dll"));
    const auto forbidden_delay =
        openrc::launcher::check_runtime_executable(forbidden_delay_path);
    expect(
        !forbidden_delay.accepted,
        "delay-loaded libunwind.dll import was accepted");

    const auto extensionless_delay_path =
        temporary_directory.path() / "extensionless-delay.exe";
    write_file(extensionless_delay_path, make_delay_pe("libc++"));
    const auto extensionless_delay =
        openrc::launcher::check_runtime_executable(extensionless_delay_path);
    expect(
        !extensionless_delay.accepted,
        "extensionless delay-loaded libc++ import was accepted");

    const auto unknown_delay_attributes_path =
        temporary_directory.path() / "unknown-delay-attributes.exe";
    write_file(
        unknown_delay_attributes_path,
        make_delay_pe("USER32.dll", DelayImportOptions{.attributes = 0x3U}));
    const auto unknown_delay_attributes =
        openrc::launcher::check_runtime_executable(
            unknown_delay_attributes_path);
    expect(
        !unknown_delay_attributes.accepted,
        "delay import with unknown attributes was accepted");

    const auto va_based_delay_path =
        temporary_directory.path() / "va-based-delay.exe";
    write_file(
        va_based_delay_path,
        make_delay_pe("USER32.dll", DelayImportOptions{.attributes = 0U}));
    const auto va_based_delay =
        openrc::launcher::check_runtime_executable(va_based_delay_path);
    expect(
        !va_based_delay.accepted,
        "non-RVA-based delay import was accepted");

    const auto unterminated_delay_path =
        temporary_directory.path() / "unterminated-delay.exe";
    write_file(
        unterminated_delay_path,
        make_delay_pe(
            "USER32.dll",
            DelayImportOptions{.terminated = false}));
    const auto unterminated_delay =
        openrc::launcher::check_runtime_executable(unterminated_delay_path);
    expect(
        !unterminated_delay.accepted,
        "unterminated delay-import directory was accepted");

    const auto misaligned_delay_size_path =
        temporary_directory.path() / "misaligned-delay-size.exe";
    write_file(
        misaligned_delay_size_path,
        make_delay_pe(
            "USER32.dll",
            DelayImportOptions{.directory_size = 33U}));
    const auto misaligned_delay_size =
        openrc::launcher::check_runtime_executable(
            misaligned_delay_size_path);
    expect(
        !misaligned_delay_size.accepted,
        "misaligned delay-import directory size was accepted");

    const auto unbacked_delay_path =
        temporary_directory.path() / "unbacked-delay.exe";
    write_file(
        unbacked_delay_path,
        make_delay_pe(
            "USER32.dll",
            DelayImportOptions{.directory_rva = kSectionRva + 0x1e0U}));
    const auto unbacked_delay =
        openrc::launcher::check_runtime_executable(unbacked_delay_path);
    expect(
        !unbacked_delay.accepted,
        "partially file-backed delay-import directory was accepted");

    const auto null_delay_name_path =
        temporary_directory.path() / "null-delay-name.exe";
    write_file(
        null_delay_name_path,
        make_delay_pe(
            "USER32.dll",
            DelayImportOptions{.name_rva = 0U}));
    const auto null_delay_name =
        openrc::launcher::check_runtime_executable(null_delay_name_path);
    expect(
        !null_delay_name.accepted,
        "delay import with a null module-name RVA was accepted");

    const auto wrong_architecture_path =
        temporary_directory.path() / "wrong-architecture.exe";
    write_file(wrong_architecture_path, make_pe("KERNEL32.dll", false));
    const auto wrong_architecture =
        openrc::launcher::check_runtime_executable(wrong_architecture_path);
    expect(!wrong_architecture.accepted, "foreign PE architecture was accepted");

    const auto truncated_path = temporary_directory.path() / "truncated.exe";
    write_file(truncated_path, std::vector<std::uint8_t>{'M', 'Z'});
    const auto truncated =
        openrc::launcher::check_runtime_executable(truncated_path);
    expect(!truncated.accepted, "truncated PE was accepted");

    auto short_optional_bytes = make_pe("KERNEL32.dll");
    write_u16(short_optional_bytes, kCoffOffset + 16U, 2U);
    const auto short_optional_path =
        temporary_directory.path() / "short-optional-header.exe";
    write_file(short_optional_path, short_optional_bytes);
    const auto short_optional =
        openrc::launcher::check_runtime_executable(short_optional_path);
    expect(
        !short_optional.accepted,
        "PE with a truncated declared optional header was accepted");
}

} // namespace

int main(const int argument_count, char** arguments) {
    try {
        run_tests();
        if (argument_count == 2) {
            const auto actual = openrc::launcher::check_runtime_executable(
                std::filesystem::path(arguments[1]));
            expect(
                actual.accepted,
                "real runtime executable was rejected: " + actual.detail);
        } else if (argument_count != 1) {
            throw std::runtime_error(
                "expected zero arguments or one runtime executable path");
        }
        std::cout << "portable executable tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "portable executable tests failed: " << error.what() << '\n';
        return 1;
    }
}
