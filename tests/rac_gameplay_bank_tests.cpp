#include "openrc/rac_gameplay_bank.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::RacGameplayBankLimitsV1 kLimits{0x10000U};
constexpr std::uint32_t kBankBytes = 0x3a0U;
constexpr std::uint32_t kLevelSettingsOffset = 0xb0U;
constexpr std::uint32_t kMobyClassesOffset = 0x1c0U;
constexpr std::uint32_t kMobyInstancesOffset = 0x1d0U;
constexpr std::array<std::uint32_t, openrc::kRacGameplayBlockCountV1>
    kPhysicalPointerSlots{
        0x88U, 0x00U, 0x10U, 0x14U, 0x18U, 0x1cU, 0x20U, 0x24U, 0x28U,
        0x2cU, 0x04U, 0x80U, 0x08U, 0x0cU, 0x40U, 0x44U, 0x54U, 0x58U,
        0x50U, 0x5cU, 0x48U, 0x4cU, 0x30U, 0x34U, 0x38U, 0x3cU, 0x70U,
        0x60U, 0x64U, 0x68U, 0x6cU, 0x84U, 0x7cU, 0x78U, 0x74U, 0x8cU};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_le32(std::vector<std::byte>& bytes,
                const std::size_t offset,
                const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

[[nodiscard]] std::vector<std::byte> make_bank() {
    std::vector<std::byte> bytes(kBankBytes, std::byte{0});

    auto block_offset = openrc::kRacGameplayFirstBlockOffsetV1;
    for (std::size_t index = 0U; index < kPhysicalPointerSlots.size();
         ++index) {
        write_le32(bytes, kPhysicalPointerSlots[index], block_offset);
        if (index == 1U) {
            block_offset += 0x50U;
        } else if (index == 15U) {
            block_offset += 0x90U;
        } else {
            block_offset += 0x10U;
        }
    }

    write_le32(bytes, kMobyClassesOffset, 2U);
    write_le32(bytes, kMobyClassesOffset + 4U, 0x123U);
    write_le32(bytes, kMobyClassesOffset + 8U, 0x456U);

    write_le32(bytes, kMobyInstancesOffset, 1U);
    write_le32(bytes, kMobyInstancesOffset + 4U, 3U);
    write_le32(bytes,
               kMobyInstancesOffset + 0x10U,
               openrc::kRacGameplayMobyRecordBytesV1);
    return bytes;
}

template <typename Mutation>
void expect_rejected(Mutation&& mutation, const std::string& message) {
    auto bytes = make_bank();
    std::invoke(std::forward<Mutation>(mutation), bytes);
    try {
        (void)openrc::parse_rac_gameplay_bank_v1(bytes, kLimits);
    } catch (const openrc::RacGameplayBankError&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_valid_bank() {
    const auto result =
        openrc::parse_rac_gameplay_bank_v1(make_bank(), kLimits);
    expect(result.input_bytes == kBankBytes &&
               result.blocks.size() == openrc::kRacGameplayBlockCountV1 &&
               result.moby_class_count == 2U &&
               result.static_moby_count == 1U &&
               result.spawnable_moby_count == 3U,
           "RAC gameplay semantic metadata is wrong");
    expect(result.header_range == openrc::RacGameplayRangeV1{0U, 0x94U} &&
               result.header_padding_range ==
                   openrc::RacGameplayRangeV1{0x94U, 0x0cU},
           "RAC gameplay header ranges are wrong");
    expect(result.blocks[0U].kind ==
                   openrc::RacGameplayBlockKindV1::environment_sample_points &&
               result.blocks[0U].range ==
                   openrc::RacGameplayRangeV1{0xa0U, 0x10U} &&
               result.blocks[1U].kind ==
                   openrc::RacGameplayBlockKindV1::level_settings &&
               result.blocks[1U].range ==
                   openrc::RacGameplayRangeV1{kLevelSettingsOffset, 0x50U} &&
               result.blocks[14U].kind ==
                   openrc::RacGameplayBlockKindV1::moby_classes &&
               result.blocks[14U].range ==
                   openrc::RacGameplayRangeV1{kMobyClassesOffset, 0x10U} &&
               result.blocks[15U].range ==
                   openrc::RacGameplayRangeV1{kMobyInstancesOffset, 0x90U},
           "RAC gameplay block ranges are wrong");
    expect(openrc::find_rac_gameplay_block_v1(
               result, openrc::RacGameplayBlockKindV1::moby_instances) ==
                   &result.blocks[15U] &&
               openrc::rac_gameplay_block_name_v1(
                   openrc::RacGameplayBlockKindV1::moby_instances) ==
                   "moby instances",
           "RAC gameplay block lookup is wrong");
}

void test_limits() {
    const auto bytes = make_bank();
    for (const auto limits :
         {openrc::RacGameplayBankLimitsV1{0U},
          openrc::RacGameplayBankLimitsV1{kBankBytes - 1U}}) {
        try {
            (void)openrc::parse_rac_gameplay_bank_v1(bytes, limits);
        } catch (const openrc::RacGameplayBankError&) {
            continue;
        }
        throw std::runtime_error("a RAC gameplay limit was ignored");
    }
}

void test_structural_rejections() {
    expect_rejected([](auto& bytes) { write_le32(bytes, 0x90U, 1U); },
                    "a non-zero reserved directory slot was accepted");
    expect_rejected([](auto& bytes) { bytes[0x94U] = std::byte{1}; },
                    "non-zero header padding was accepted");
    expect_rejected([](auto& bytes) { write_le32(bytes, 0x88U, 0xa1U); },
                    "an unaligned block pointer was accepted");
    expect_rejected([](auto& bytes) { write_le32(bytes, 0x40U, 0xa0U); },
                    "a duplicate block pointer was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, 0x40U, kMobyInstancesOffset);
            write_le32(bytes, 0x44U, kMobyClassesOffset);
        },
        "noncanonical physical block order was accepted");
    expect_rejected([](auto& bytes) { write_le32(bytes, 0x88U, 0U); },
                    "a missing gameplay block was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, kMobyClassesOffset, 0xffffffffU); },
        "a negative moby-class count was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kMobyClassesOffset + 0x0cU] = std::byte{1}; },
        "non-zero moby-class padding was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, kMobyInstancesOffset + 8U, 1U); },
        "non-zero moby header padding was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, kMobyInstancesOffset + 0x10U, 0x70U);
        },
        "a wrong RAC1 moby record size was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kMobyInstancesOffset + 0x88U] = std::byte{1}; },
        "non-zero moby alignment padding was accepted");
}

} // namespace

int main() {
    try {
        test_valid_bank();
        test_limits();
        test_structural_rejections();
        std::cout << "OpenRC RacGameplayBankV1 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC RacGameplayBankV1 tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
