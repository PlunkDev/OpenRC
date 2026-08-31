#include "openrc/rac_level_core.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kIndexBytes = 0x690U;
constexpr std::uint32_t kEncodedBytes = 0x20U;
constexpr std::uint32_t kDecodedBytes = 0x500U;
constexpr std::uint32_t kMobyTableOffset = 0xe0U;
constexpr std::uint32_t kMobyTextureTableOffset = 0x1c0U;
constexpr std::uint32_t kRatchetSequenceTableOffset = 0x270U;
constexpr std::uint32_t kGadgetTableOffset = 0x670U;
constexpr openrc::RacLevelCoreLimitsV1 kLimits{
    0x10000U,
    0x10000U,
    0x10000U,
    1024U,
    255U,
    1024U,
};

struct Fixture {
    std::vector<std::byte> index;
    std::vector<std::byte> encoded;
    std::vector<std::byte> decoded;
};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

void write_array_range(
    std::vector<std::byte>& bytes,
    const std::size_t header_offset,
    const std::uint32_t count,
    const std::uint32_t offset) {
    write_le32(bytes, header_offset, count);
    write_le32(bytes, header_offset + 4U, offset);
}

void write_wad_header(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t logical_size) {
    bytes[offset] = std::byte{'W'};
    bytes[offset + 1U] = std::byte{'A'};
    bytes[offset + 2U] = std::byte{'D'};
    write_le32(bytes, offset + 3U, logical_size);
}

void fill_unused_textures(
    std::vector<std::byte>& bytes,
    const std::size_t record_offset) {
    for (std::size_t index = 0U; index < 16U; ++index) {
        bytes[record_offset + 0x10U + index] = std::byte{0xff};
    }
}

[[nodiscard]] Fixture make_fixture() {
    Fixture fixture{
        std::vector<std::byte>(kIndexBytes, std::byte{0}),
        std::vector<std::byte>(kEncodedBytes, std::byte{0}),
        std::vector<std::byte>(kDecodedBytes, std::byte{0}),
    };
    auto& index = fixture.index;

    write_array_range(index, 0x00U, 1U, 0x240U);
    write_le32(index, 0x08U, 0U);
    write_le32(index, 0x0cU, 0x40U);
    write_le32(index, 0x10U, 0x80U);
    write_le32(index, 0x14U, 0xc0U);
    write_array_range(index, 0x18U, 4U, kMobyTableOffset);
    write_array_range(index, 0x20U, 1U, 0x160U);
    write_array_range(index, 0x28U, 1U, 0x180U);
    write_array_range(index, 0x30U, 1U, 0x1b0U);
    write_array_range(index, 0x38U, 4U, kMobyTextureTableOffset);
    write_array_range(index, 0x40U, 1U, 0x200U);
    write_array_range(index, 0x48U, 1U, 0x210U);
    write_array_range(index, 0x50U, 1U, 0x220U);
    write_array_range(index, 0x58U, 1U, 0x230U);
    write_le32(index, 0x60U, 0x100U);
    write_le32(index, 0x6cU, 0x250U);
    write_le32(index, 0x70U, 0x260U);
    write_le32(index, 0x78U, kRatchetSequenceTableOffset);
    write_le32(index, 0x7cU, 0x340U);
    write_le32(index, 0x80U, 2U);
    write_le32(index, 0x84U, kGadgetTableOffset);
    write_le32(index, 0x88U, kEncodedBytes);
    write_le32(index, 0x8cU, kDecodedBytes);

    // Exact 0x24-byte original RAC1 trailer after the 0xbc-byte header.
    index[0xc1U] = std::byte{0x5c};
    index[0xc5U] = std::byte{0x58};

    fill_unused_textures(index, 0xe0U);
    write_le32(index, 0xe0U, 0x100U);
    write_le32(index, 0xe4U, 0U);
    index[0xf0U] = std::byte{0};
    index[0xf1U] = std::byte{1};

    fill_unused_textures(index, 0x100U);
    write_le32(index, 0x100U, 0x180U);
    write_le32(index, 0x104U, 10U);
    index[0x110U] = std::byte{1};

    fill_unused_textures(index, 0x120U);
    write_le32(index, 0x124U, 11U);
    index[0x130U] = std::byte{2};

    fill_unused_textures(index, 0x140U);
    write_le32(index, 0x144U, 12U);
    index[0x150U] = std::byte{3};

    // Only the first word of these records is needed as a proven next asset
    // boundary for the final non-empty Moby class.
    write_le32(index, 0x160U, 0x200U);
    write_le32(index, 0x180U, 0x280U);
    write_le32(index, kRatchetSequenceTableOffset, 0x300U);

    write_le32(index, 0x670U, 0x380U);
    write_le32(index, 0x674U, 11U);
    write_le32(index, 0x678U, 0x30U);
    write_le32(index, 0x680U, 0x3c0U);
    write_le32(index, 0x684U, 12U);
    write_le32(index, 0x688U, 0x120U);

    write_wad_header(fixture.encoded, 0U, kEncodedBytes);
    write_wad_header(fixture.decoded, 0x380U, 0x30U);
    write_wad_header(fixture.decoded, 0x3c0U, 0x120U);
    return fixture;
}

[[nodiscard]] openrc::RacLevelCoreIndexV1 parse(const Fixture& fixture) {
    return openrc::parse_rac_level_core_index_v1(
        fixture.index,
        fixture.encoded,
        fixture.decoded,
        kLimits);
}

template <typename Mutation>
void expect_rejected(Mutation&& mutation, const std::string& message) {
    auto fixture = make_fixture();
    std::invoke(std::forward<Mutation>(mutation), fixture);
    try {
        (void)parse(fixture);
    } catch (const openrc::RacLevelCoreError&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_valid_level_core() {
    const auto result = parse(make_fixture());
    expect(
        result.index_input_bytes == kIndexBytes &&
            result.encoded_asset_input_bytes == kEncodedBytes &&
            result.decoded_asset_input_bytes == kDecodedBytes &&
            result.header_range == openrc::RacLevelCoreRangeV1{0U, 0xbcU} &&
            result.rac1_header_trailer_range ==
                openrc::RacLevelCoreRangeV1{0xbcU, 0x24U},
        "RAC level-core input ranges are wrong");
    expect(
        result.header.moby_classes.count == 4U &&
            result.header.moby_classes.offset == kMobyTableOffset &&
            result.header.moby_textures.count == 4U &&
            result.header.assets_encoded_size == kEncodedBytes &&
            result.header.assets_decoded_size == kDecodedBytes,
        "RAC level-core header fields are wrong");
    expect(
        result.moby_class_table_range ==
                openrc::RacLevelCoreRangeV1{0xe0U, 0x80U} &&
            result.moby_texture_table_range ==
                openrc::RacLevelCoreRangeV1{0x1c0U, 0x40U} &&
            result.ratchet_sequence_table_range ==
                openrc::RacLevelCoreRangeV1{0x270U, 0x400U} &&
            result.gadget_table_range ==
                openrc::RacLevelCoreRangeV1{0x670U, 0x20U},
        "RAC level-core directory ranges are wrong");
    expect(
        result.moby_classes.size() == 4U &&
            result.moby_classes[0U].class_id == 0 &&
            result.moby_classes[0U].texture_slots[0U] == 0U &&
            result.moby_classes[0U].texture_slots[1U] == 1U &&
            result.moby_classes[0U].used_texture_slot_count == 2U &&
            result.moby_classes[0U].asset_range ==
                openrc::RacLevelCoreRangeV1{0x100U, 0x80U} &&
            result.moby_classes[1U].asset_range ==
                openrc::RacLevelCoreRangeV1{0x180U, 0x80U} &&
            result.moby_classes[2U].asset_range ==
                openrc::RacLevelCoreRangeV1{},
        "RAC level-core Moby-class records are wrong");
    expect(
        result.gadgets.size() == 2U &&
            result.gadgets[0U].class_id == 11 &&
            result.gadgets[0U].encoded_range ==
                openrc::RacLevelCoreRangeV1{0x380U, 0x30U} &&
            result.gadgets[0U].padding_after_range ==
                openrc::RacLevelCoreRangeV1{0x3b0U, 0x10U} &&
            result.gadgets[1U].padding_after_range ==
                openrc::RacLevelCoreRangeV1{0x4e0U, 0x20U} &&
            result.gadget_asset_prefix_range ==
                openrc::RacLevelCoreRangeV1{0U, 0x380U} &&
            result.gadget_asset_chain_range ==
                openrc::RacLevelCoreRangeV1{0x380U, 0x180U} &&
            result.total_gadget_encoded_bytes == 0x150U &&
            result.total_gadget_padding_bytes == 0x30U,
        "RAC level-core gadget records are wrong");
}

void test_non_numeric_class_order_is_allowed() {
    auto fixture = make_fixture();
    write_le32(fixture.index, 0x104U, 1007U);
    write_le32(fixture.index, 0x124U, 10U);
    write_le32(fixture.index, 0x674U, 10U);
    const auto result = parse(fixture);
    expect(
        result.moby_classes[1U].class_id == 1007 &&
            result.moby_classes[2U].class_id == 10,
        "RAC level-core incorrectly requires numeric class-ID order");
}

void test_limits() {
    const auto fixture = make_fixture();
    const std::vector<openrc::RacLevelCoreLimitsV1> limits{
        {0U, 1U, 1U, 1U, 1U, 1U},
        {kIndexBytes - 1U, 1U, 1U, 1U, 1U, 1U},
        {kIndexBytes, kEncodedBytes - 1U, 1U, 1U, 1U, 1U},
        {kIndexBytes, kEncodedBytes, kDecodedBytes - 1U, 1U, 1U, 1U},
        {kIndexBytes, kEncodedBytes, kDecodedBytes, 3U, 4U, 2U},
        {kIndexBytes, kEncodedBytes, kDecodedBytes, 4U, 3U, 2U},
        {kIndexBytes, kEncodedBytes, kDecodedBytes, 4U, 4U, 1U},
    };
    for (const auto& candidate : limits) {
        try {
            (void)openrc::parse_rac_level_core_index_v1(
                fixture.index,
                fixture.encoded,
                fixture.decoded,
                candidate);
        } catch (const openrc::RacLevelCoreError&) {
            continue;
        }
        throw std::runtime_error("a RAC level-core caller limit was ignored");
    }
}

void test_header_and_table_rejections() {
    expect_rejected(
        [](auto& fixture) { fixture.index[0xc1U] = std::byte{0}; },
        "the exact RAC1 header trailer was not validated");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x1cU, 0xf0U); },
        "a noncanonical Moby-class table offset was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x3cU, 0x1d0U); },
        "a gap in the canonical table chain was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x84U, 0x660U); },
        "a nonterminal gadget table was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x88U, 0x10U); },
        "an encoded level-core size mismatch was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x8cU, 0x4c0U); },
        "a decoded level-core size mismatch was accepted");
}

void test_moby_class_rejections() {
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0xe8U, 1U); },
        "a non-zero Moby-class reserved word was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x104U, 0U); },
        "a duplicate Moby class ID was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x100U, 0x181U); },
        "an unaligned Moby-class asset offset was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x100U, 0xc0U); },
        "decreasing Moby-class asset offsets were accepted");
    expect_rejected(
        [](auto& fixture) {
            fixture.index[0xf1U] = std::byte{0xff};
            fixture.index[0xf2U] = std::byte{1};
        },
        "a texture after the 0xff slot sentinel was accepted");
    expect_rejected(
        [](auto& fixture) { fixture.index[0xf0U] = std::byte{4}; },
        "an out-of-range Moby texture index was accepted");
}

void test_gadget_rejections() {
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x674U, 99U); },
        "a gadget ID absent from the Moby table was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x124U, 12U); },
        "a duplicate gadget-compatible Moby ID was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x120U, 0x200U); },
        "a gadget Moby with a local asset offset was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x67cU, 1U); },
        "a non-zero gadget reserved word was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x678U, 0x20U); },
        "a gadget WadV1 size mismatch was accepted");
    expect_rejected(
        [](auto& fixture) { fixture.decoded[0x3b0U] = std::byte{1}; },
        "non-zero gadget alignment padding was accepted");
    expect_rejected(
        [](auto& fixture) { write_le32(fixture.index, 0x680U, 0x400U); },
        "a gap in the terminal gadget chain was accepted");
}

} // namespace

int main() {
    try {
        test_valid_level_core();
        test_non_numeric_class_order_is_allowed();
        test_limits();
        test_header_and_table_rejections();
        test_moby_class_rejections();
        test_gadget_rejections();
        std::cout << "RAC level-core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RAC level-core test failure: " << error.what() << '\n';
        return 1;
    }
}
