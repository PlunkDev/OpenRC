#include "openrc/ee_r5900_boundary.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kElfHeaderSize = 52U;
constexpr std::size_t kProgramHeaderSize = 32U;
constexpr std::size_t kSectionHeaderSize = 40U;
constexpr std::size_t kProgramHeaderOffset = kElfHeaderSize;
constexpr std::size_t kSectionHeaderOffset = 0x100U;
constexpr std::size_t kSectionNameTableOffset = 0x180U;
constexpr std::size_t kCodeOffset = 0x200U;
constexpr std::uint32_t kCodeAddress = 0x00100000U;
constexpr char kSectionNames[] = "\0.text\0.shstrtab";
constexpr openrc::EeR5900BoundaryLimitsV1 kLimits{
    4096U,
    8U,
    4096U,
    1024U,
    1024U,
    1024U};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_le16(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_le32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void write_section_header(
    std::vector<std::byte>& bytes,
    const std::size_t index,
    const std::uint32_t name_offset,
    const std::uint32_t type,
    const std::uint32_t flags,
    const std::uint32_t address,
    const std::uint32_t file_offset,
    const std::uint32_t size,
    const std::uint32_t alignment) {
    const auto offset = kSectionHeaderOffset + index * kSectionHeaderSize;
    write_le32(bytes, offset + 0U, name_offset);
    write_le32(bytes, offset + 4U, type);
    write_le32(bytes, offset + 8U, flags);
    write_le32(bytes, offset + 12U, address);
    write_le32(bytes, offset + 16U, file_offset);
    write_le32(bytes, offset + 20U, size);
    write_le32(bytes, offset + 32U, alignment);
}

[[nodiscard]] std::vector<std::byte> make_elf(
    const std::vector<std::uint32_t>& words) {
    std::vector<std::byte> bytes(0x300U, std::byte{0});
    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    write_le16(bytes, 16U, 2U);
    write_le16(bytes, 18U, 8U);
    write_le32(bytes, 20U, 1U);
    write_le32(bytes, 24U, kCodeAddress);
    write_le32(bytes, 28U, kProgramHeaderOffset);
    write_le32(bytes, 32U, kSectionHeaderOffset);
    write_le16(bytes, 40U, kElfHeaderSize);
    write_le16(bytes, 42U, kProgramHeaderSize);
    write_le16(bytes, 44U, 1U);
    write_le16(bytes, 46U, kSectionHeaderSize);
    write_le16(bytes, 48U, 3U);
    write_le16(bytes, 50U, 2U);

    const auto code_bytes = static_cast<std::uint32_t>(words.size() * 4U);
    write_le32(bytes, kProgramHeaderOffset + 0U, 1U);
    write_le32(bytes, kProgramHeaderOffset + 4U, kCodeOffset);
    write_le32(bytes, kProgramHeaderOffset + 8U, kCodeAddress);
    write_le32(bytes, kProgramHeaderOffset + 12U, kCodeAddress);
    write_le32(bytes, kProgramHeaderOffset + 16U, code_bytes);
    write_le32(bytes, kProgramHeaderOffset + 20U, code_bytes);
    write_le32(bytes, kProgramHeaderOffset + 24U, 5U);
    write_le32(bytes, kProgramHeaderOffset + 28U, 0x10U);

    write_section_header(
        bytes,
        1U,
        1U,
        1U,
        6U,
        kCodeAddress,
        kCodeOffset,
        code_bytes,
        4U);
    write_section_header(
        bytes,
        2U,
        7U,
        3U,
        0U,
        0U,
        kSectionNameTableOffset,
        sizeof(kSectionNames),
        1U);
    for (std::size_t index = 0U; index < sizeof(kSectionNames); ++index) {
        bytes[kSectionNameTableOffset + index] =
            static_cast<std::byte>(kSectionNames[index]);
    }
    for (std::size_t index = 0U; index < words.size(); ++index) {
        write_le32(bytes, kCodeOffset + index * 4U, words[index]);
    }
    return bytes;
}

[[nodiscard]] std::vector<std::uint32_t> boundary_words() {
    constexpr std::uint32_t kSyscallCode = 0x12345U;
    return {
        0x2403fffbU,
        (kSyscallCode << 6U) | 0x0cU,
        0x03e00008U,
        0x00000000U,
        (0x03U << 26U) | ((kCodeAddress + 8U * 4U) >> 2U),
        0x00000000U,
        (0x14U << 26U) | 1U,
        0x00000000U,
        0x0320f809U,
        0x00000000U,
        (0x02U << 26U) | 0x03ffffffU,
        0x00000000U};
}

template <typename Function>
void expect_boundary_error(Function&& function, const std::string& message) {
    try {
        std::invoke(std::forward<Function>(function));
    } catch (const openrc::EeR5900BoundaryError&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_control_and_syscall_inventory() {
    const auto words = boundary_words();
    const auto elf = make_elf(words);
    const auto report =
        openrc::inventory_ee_r5900_boundaries_v1(elf, kLimits);
    expect(report.code_regions.size() == 1U, "code-region count is wrong");
    expect(
        report.code_regions[0].section_name == ".text" &&
            report.code_regions[0].source_range.offset == kCodeOffset &&
            report.code_regions[0].virtual_address == kCodeAddress,
        "code-region provenance is wrong");
    expect(
        report.instructions.size() == words.size() &&
            report.total_code_bytes == words.size() * 4U,
        "instruction or code-byte count is wrong");
    expect(report.control_transfers.size() == 5U, "control-transfer count is wrong");
    expect(report.syscall_sites.size() == 1U, "syscall-site count is wrong");

    const auto& syscall = report.syscall_sites.front();
    expect(
        syscall.encoded_instruction_code == 0x12345U &&
            syscall.selector_v1 == -5 &&
            syscall.selector_source_instruction_index == 0U &&
            syscall.selector_proof ==
                openrc::EeR5900SyscallSelectorProofV1::addiu_zero &&
            syscall.exact_wrapper_entry_address == kCodeAddress,
        "exact syscall-wrapper proof is wrong");

    const auto direct_call = report.control_transfers[1U];
    expect(
        direct_call.kind == openrc::EeR5900ControlTransferKindV1::direct_call &&
            direct_call.direct_target_address == kCodeAddress + 8U * 4U &&
            direct_call.direct_target_decoded &&
            direct_call.delay_slot_decoded,
        "JAL target or delay-slot provenance is wrong");
    expect(
        report.control_transfers[2U].likely &&
            report.control_transfers[2U].direct_target_address ==
                kCodeAddress + 8U * 4U,
        "likely branch decoding is wrong");
    expect(
        report.control_transfers[3U].kind ==
                openrc::EeR5900ControlTransferKindV1::indirect_call &&
            report.control_transfers[3U].target_gpr == 25U &&
            report.control_transfers[3U].link_gpr == 31U,
        "JALR register provenance is wrong");
    expect(
        report.direct_target_outside_code_count == 1U &&
            report.missing_delay_slot_count == 0U,
        "outside-target or delay-slot counters are wrong");
}

void test_selector_requires_zero_base() {
    auto words = boundary_words();
    words[0] = 0x2483fffbU;
    const auto report = openrc::inventory_ee_r5900_boundaries_v1(
        make_elf(words),
        kLimits);
    expect(
        !report.syscall_sites[0].selector_v1 &&
            !report.syscall_sites[0].exact_wrapper_entry_address,
        "a non-constant v1 write became a syscall selector proof");
}

void test_limits_and_envelope_rejections() {
    const auto elf = make_elf(boundary_words());
    auto limits = kLimits;
    limits.max_control_transfers = 4U;
    expect_boundary_error(
        [&] {
            (void)openrc::inventory_ee_r5900_boundaries_v1(elf, limits);
        },
        "control-transfer limit was ignored");

    limits = kLimits;
    limits.max_input_bytes = 0U;
    expect_boundary_error(
        [&] {
            (void)openrc::inventory_ee_r5900_boundaries_v1(elf, limits);
        },
        "zero caller limit was accepted");

    auto relocatable = elf;
    write_le16(relocatable, 16U, 1U);
    expect_boundary_error(
        [&] {
            (void)openrc::inventory_ee_r5900_boundaries_v1(
                relocatable,
                kLimits);
        },
        "a non-executable ELF was accepted");

    auto misaligned = elf;
    write_le32(
        misaligned,
        kSectionHeaderOffset + kSectionHeaderSize + 20U,
        static_cast<std::uint32_t>(boundary_words().size() * 4U - 1U));
    expect_boundary_error(
        [&] {
            (void)openrc::inventory_ee_r5900_boundaries_v1(
                misaligned,
                kLimits);
        },
        "a non-word-sized executable section was accepted");
}

} // namespace

int main() {
    try {
        test_control_and_syscall_inventory();
        test_selector_requires_zero_base();
        test_limits_and_envelope_rejections();
        std::cout << "OpenRC EE/R5900 boundary inventory tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "OpenRC EE/R5900 boundary inventory tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
