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
    constexpr std::size_t pe_offset = 0x80U;
    constexpr std::size_t coff_offset = pe_offset + 4U;
    constexpr std::size_t optional_offset = coff_offset + 20U;
    constexpr std::size_t raw_section_offset = 0x200U;
    constexpr std::size_t import_name_offset = raw_section_offset + 0x40U;
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
    write_u32(bytes, 0x3cU, static_cast<std::uint32_t>(pe_offset));
    write_u32(bytes, pe_offset, 0x00004550U);
    write_u16(
        bytes,
        coff_offset,
        matching_architecture ? expected_machine : foreign_machine);
    write_u16(bytes, coff_offset + 2U, 1U);
    write_u16(
        bytes,
        coff_offset + 16U,
        static_cast<std::uint16_t>(optional_size));
    write_u16(
        bytes,
        optional_offset,
        static_cast<std::uint16_t>(is_64_bit ? 0x020bU : 0x010bU));
    write_u32(bytes, optional_offset + 60U, 0x200U);
    write_u32(bytes, optional_offset + directory_count_offset, 16U);
    write_u32(bytes, optional_offset + directories_offset + 8U, 0x1000U);
    write_u32(bytes, optional_offset + directories_offset + 12U, 40U);

    const auto section_offset = optional_offset + optional_size;
    write_u32(bytes, section_offset + 8U, 0x200U);
    write_u32(bytes, section_offset + 12U, 0x1000U);
    write_u32(bytes, section_offset + 16U, 0x200U);
    write_u32(
        bytes,
        section_offset + 20U,
        static_cast<std::uint32_t>(raw_section_offset));

    write_u32(bytes, raw_section_offset + 12U, 0x1040U);
    for (std::size_t index = 0U; index < import_name.size(); ++index) {
        bytes.at(import_name_offset + index) =
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
