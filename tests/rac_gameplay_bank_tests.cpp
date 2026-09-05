#include "openrc/rac_gameplay_bank.hpp"

#include <array>
#include <bit>
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
constexpr std::uint32_t kBankBytes = 0x510U;
constexpr std::uint32_t kLevelSettingsOffset = 0xb0U;
constexpr std::uint32_t kMobyClassesOffset = 0x1c0U;
constexpr std::uint32_t kMobyInstancesOffset = 0x1d0U;
constexpr std::uint32_t kTieClassesOffset = 0x2c0U;
constexpr std::uint32_t kTieInstancesOffset = 0x2d0U;
constexpr std::uint32_t kShrubClassesOffset = 0x3d0U;
constexpr std::uint32_t kShrubInstancesOffset = 0x3e0U;
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

void write_matrix(std::vector<std::byte>& bytes,
                  const std::size_t offset,
                  const std::array<float, 16U>& matrix) {
    for (std::size_t index = 0U; index < matrix.size(); ++index) {
        write_le32(bytes,
                   offset + index * sizeof(std::uint32_t),
                   std::bit_cast<std::uint32_t>(matrix[index]));
    }
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
        } else if (index == 23U) {
            block_offset += 0x100U;
        } else if (index == 25U) {
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
    write_le32(bytes, kMobyInstancesOffset + 0x28U, 0x123U);
    write_le32(bytes,
               kMobyInstancesOffset + 0x2cU,
               std::bit_cast<std::uint32_t>(1.5F));
    write_le32(bytes, kMobyInstancesOffset + 0x30U, 256U);
    write_le32(bytes, kMobyInstancesOffset + 0x40U,
               std::bit_cast<std::uint32_t>(10.0F));
    write_le32(bytes, kMobyInstancesOffset + 0x44U,
               std::bit_cast<std::uint32_t>(20.0F));
    write_le32(bytes, kMobyInstancesOffset + 0x48U,
               std::bit_cast<std::uint32_t>(30.0F));
    write_le32(bytes, kMobyInstancesOffset + 0x4cU,
               std::bit_cast<std::uint32_t>(0.1F));
    write_le32(bytes, kMobyInstancesOffset + 0x50U,
               std::bit_cast<std::uint32_t>(0.2F));
    write_le32(bytes, kMobyInstancesOffset + 0x54U,
               std::bit_cast<std::uint32_t>(0.3F));
    write_le32(bytes, kMobyInstancesOffset + 0x58U, 7U);
    write_le32(bytes, kMobyInstancesOffset + 0x60U,
               std::bit_cast<std::uint32_t>(4.0F));
    write_le32(bytes, kMobyInstancesOffset + 0x68U, 9U);
    write_le32(bytes, kMobyInstancesOffset + 0x6cU, 10U);
    write_le32(bytes, kMobyInstancesOffset + 0x70U, 0x11223344U);
    write_le32(bytes, kMobyInstancesOffset + 0x80U, 11U);

    write_le32(bytes, kTieClassesOffset, 2U);
    write_le32(bytes, kTieClassesOffset + 4U, 0x7e4U);
    write_le32(bytes, kTieClassesOffset + 8U, 0x7e5U);
    write_le32(bytes, kTieInstancesOffset, 1U);
    constexpr std::array<float, 16U> kTieMatrix{
        0.5F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.5F, 0.0F, 0.0F,
        0.0F, 0.0F, 0.5F, 0.0F,
        105.0F, 203.0F, 37.0F, 0.01F};
    const auto tie_record = kTieInstancesOffset + 0x10U;
    write_le32(bytes, tie_record, 0x7e4U);
    write_le32(bytes, tie_record + 4U, 0x8cU);
    write_matrix(bytes, tie_record + 0x10U, kTieMatrix);
    write_le32(bytes, tie_record + 0xdcU, 0x12345678U);

    write_le32(bytes, kShrubClassesOffset, 2U);
    write_le32(bytes, kShrubClassesOffset + 4U, 0x1d1U);
    write_le32(bytes, kShrubClassesOffset + 8U, 0x1d2U);
    write_le32(bytes, kShrubInstancesOffset, 1U);
    constexpr std::array<float, 16U> kShrubMatrix{
        0.65F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.65F, 0.0F, 0.0F,
        0.0F, 0.0F, 0.65F, 0.0F,
        172.0F, 167.0F, 31.25F, 0.01F};
    const auto shrub_record = kShrubInstancesOffset + 0x10U;
    write_le32(bytes, shrub_record, 0x1d1U);
    write_le32(bytes, shrub_record + 4U, 0x42000000U);
    write_matrix(bytes, shrub_record + 0x10U, kShrubMatrix);
    write_le32(bytes, shrub_record + 0x6cU, 0x87654321U);
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
               result.moby_class_ids ==
                   std::vector<std::uint32_t>{0x123U, 0x456U} &&
               result.static_moby_count == 1U &&
               result.spawnable_moby_count == 3U &&
               result.static_mobies.size() == 1U &&
               result.tie_class_count == 2U &&
               result.tie_class_ids ==
                   std::vector<std::uint32_t>{0x7e4U, 0x7e5U} &&
               result.tie_instance_count == 1U &&
               result.tie_instances.size() == 1U &&
               result.shrub_class_count == 2U &&
               result.shrub_class_ids ==
                   std::vector<std::uint32_t>{0x1d1U, 0x1d2U} &&
               result.shrub_instance_count == 1U &&
               result.shrub_instances.size() == 1U,
           "RAC gameplay semantic metadata is wrong");
    const auto& moby = result.static_mobies.front();
    expect(moby.record_range == openrc::RacGameplayRangeV1{0x1e0U, 0x78U} &&
               moby.class_id == 0x123U && moby.scale == 1.5F &&
               moby.draw_distance_raw == 256U &&
               moby.position == std::array<float, 3>{10.0F, 20.0F, 30.0F} &&
               moby.rotation == std::array<float, 3>{0.1F, 0.2F, 0.3F} &&
               moby.group_index == 7 && moby.rooted_distance == 4.0F &&
               moby.pvar_index == 9 && moby.occlusion == 10 &&
               moby.mode_bits == 0x11223344U && moby.light_index == 11,
           "RAC gameplay moby-instance placement metadata is wrong");
    const auto& tie = result.tie_instances.front();
    expect(tie.record_range ==
                   openrc::RacGameplayRangeV1{0x2e0U, 0xe0U} &&
               tie.class_id == 0x7e4U && tie.matrix[0U] == 0.5F &&
               tie.matrix[5U] == 0.5F && tie.matrix[10U] == 0.5F &&
               tie.matrix[12U] == 105.0F && tie.matrix[13U] == 203.0F &&
               tie.matrix[14U] == 37.0F && tie.matrix[15U] == 0.01F &&
               tie.matrix_bits[15U] ==
                   std::bit_cast<std::uint32_t>(0.01F) &&
               tie.raw_words[1U] == 0x8cU &&
               tie.raw_words.back() == 0x12345678U,
           "RAC gameplay TIE-instance metadata is wrong");
    const auto& shrub = result.shrub_instances.front();
    expect(shrub.record_range ==
                   openrc::RacGameplayRangeV1{0x3f0U, 0x70U} &&
               shrub.class_id == 0x1d1U && shrub.matrix[0U] == 0.65F &&
               shrub.matrix[5U] == 0.65F && shrub.matrix[10U] == 0.65F &&
               shrub.matrix[12U] == 172.0F && shrub.matrix[13U] == 167.0F &&
               shrub.matrix[14U] == 31.25F &&
               shrub.matrix[15U] == 0.01F &&
               shrub.matrix_bits[0U] ==
                   std::bit_cast<std::uint32_t>(0.65F) &&
               shrub.raw_words[1U] == 0x42000000U &&
               shrub.raw_words.back() == 0x87654321U,
           "RAC gameplay shrub-instance metadata is wrong");
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
                   openrc::RacGameplayRangeV1{kMobyInstancesOffset, 0x90U} &&
               result.blocks[22U].range ==
                   openrc::RacGameplayRangeV1{kTieClassesOffset, 0x10U} &&
               result.blocks[23U].range ==
                   openrc::RacGameplayRangeV1{kTieInstancesOffset, 0x100U} &&
               result.blocks[24U].range ==
                   openrc::RacGameplayRangeV1{kShrubClassesOffset, 0x10U} &&
               result.blocks[25U].range ==
                   openrc::RacGameplayRangeV1{kShrubInstancesOffset, 0x90U},
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
          openrc::RacGameplayBankLimitsV1{kBankBytes - 1U},
          openrc::RacGameplayBankLimitsV1{kBankBytes, 1U, 65'536U},
          openrc::RacGameplayBankLimitsV1{kBankBytes, 65'536U, 0U},
          openrc::RacGameplayBankLimitsV1{
              kBankBytes, 65'536U, 65'536U, 1U},
          openrc::RacGameplayBankLimitsV1{
              kBankBytes, 65'536U, 65'536U, 65'536U, 0U},
          openrc::RacGameplayBankLimitsV1{
              kBankBytes, 65'536U, 65'536U, 65'536U, 65'536U, 1U},
          openrc::RacGameplayBankLimitsV1{
              kBankBytes,
              65'536U,
              65'536U,
              65'536U,
              65'536U,
              65'536U,
              0U}}) {
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
        [](auto& bytes) {
            write_le32(bytes, kMobyInstancesOffset + 0x28U, 0x999U);
        },
        "a RAC1 moby reference to an absent class was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, kMobyClassesOffset + 8U, 0x123U);
        },
        "duplicate RAC1 moby class IDs were accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, kMobyInstancesOffset + 0x2cU, 0x7fc00000U);
        },
        "a non-finite RAC1 moby scale was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, kMobyInstancesOffset + 0x40U, 0x7f800000U);
        },
        "a non-finite RAC1 moby position was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kMobyInstancesOffset + 0x88U] = std::byte{1}; },
        "non-zero moby alignment padding was accepted");

    expect_rejected(
        [](auto& bytes) { write_le32(bytes, kTieClassesOffset, 0xffffffffU); },
        "a negative TIE-class count was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, kTieClassesOffset + 8U, 0x7e4U); },
        "duplicate TIE class IDs were accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kTieClassesOffset + 0x0cU] = std::byte{1}; },
        "non-zero TIE-class padding was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, kTieInstancesOffset + 4U, 1U); },
        "non-zero TIE-instance header padding was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, kTieInstancesOffset, 2U); },
        "a truncated TIE-instance list was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, kTieInstancesOffset + 0x10U, 0x999U);
        },
        "a TIE reference to an absent class was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, kTieInstancesOffset + 0x20U, 0x7fc00000U);
        },
        "a non-finite TIE matrix was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kTieInstancesOffset + 0xf0U] = std::byte{1}; },
        "non-zero TIE-instance padding was accepted");

    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, kShrubClassesOffset, 0xffffffffU);
        },
        "a negative shrub-class count was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, kShrubClassesOffset + 8U, 0x1d1U);
        },
        "duplicate shrub class IDs were accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kShrubClassesOffset + 0x0cU] = std::byte{1}; },
        "non-zero shrub-class padding was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, kShrubInstancesOffset + 8U, 1U); },
        "non-zero shrub-instance header padding was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, kShrubInstancesOffset, 2U); },
        "a truncated shrub-instance list was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, kShrubInstancesOffset + 0x10U, 0x999U);
        },
        "a shrub reference to an absent class was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, kShrubInstancesOffset + 0x20U, 0x7f800000U);
        },
        "a non-finite shrub matrix was accepted");
    expect_rejected(
        [](auto& bytes) {
            bytes[kShrubInstancesOffset + 0x80U] = std::byte{1};
        },
        "non-zero shrub-instance padding was accepted");
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
