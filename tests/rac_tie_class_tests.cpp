#include "openrc/rac_tie_class.hpp"

#include <algorithm>
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

constexpr std::size_t kClassBytes = 0x200U;
constexpr std::size_t kPacketTableOffset = 0x70U;
constexpr std::size_t kPacketDataOffset = 0x80U;
constexpr std::size_t kVertexDataOffset = 0xc0U;
constexpr std::size_t kAdGifOffset = 0x140U;
constexpr std::size_t kLightingOffset = 0x1e0U;
constexpr std::size_t kSecondaryLightingOffset = 0x1f0U;
constexpr openrc::RacTieClassLimitsV1 kLimits{
    0x10000U,
    256U,
    4096U,
    65'536U,
    65'536U,
    65'536U,
    16U,
};

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

void write_i16(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::int16_t value) {
    write_le16(bytes, offset, std::bit_cast<std::uint16_t>(value));
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

void write_float(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const float value) {
    write_le32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void write_dinky_vertex(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::array<std::int16_t, 3U> position,
    const std::uint16_t gs_offset,
    const std::int16_t s,
    const std::int16_t t) {
    write_i16(bytes, offset, position[0U]);
    write_i16(bytes, offset + 0x02U, position[1U]);
    write_i16(bytes, offset + 0x04U, position[2U]);
    write_le16(bytes, offset + 0x06U, gs_offset);
    write_i16(bytes, offset + 0x08U, s);
    write_i16(bytes, offset + 0x0aU, t);
    write_i16(bytes, offset + 0x0cU, 4096);
}

void write_fat_vertex(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::array<std::int16_t, 3U> position,
    const std::uint16_t gs_offset,
    const std::int16_t s,
    const std::int16_t t) {
    write_le16(bytes, offset + 0x06U, gs_offset);
    write_i16(bytes, offset + 0x08U, position[0U]);
    write_i16(bytes, offset + 0x0aU, position[1U]);
    write_i16(bytes, offset + 0x0cU, position[2U]);
    write_i16(bytes, offset + 0x10U, s);
    write_i16(bytes, offset + 0x12U, t);
    write_i16(bytes, offset + 0x14U, 4096);
}

[[nodiscard]] std::vector<std::byte> make_class() {
    std::vector<std::byte> bytes(kClassBytes, std::byte{0});
    write_le32(bytes, 0x00U, kPacketTableOffset);
    bytes[0x20U] = std::byte{1};
    bytes[0x23U] = std::byte{2};
    write_le32(bytes, 0x2cU, kAdGifOffset);
    write_float(bytes, 0x30U, 1.0F);
    write_float(bytes, 0x34U, 2.0F);
    write_float(bytes, 0x38U, 3.0F);
    write_float(bytes, 0x3cU, 4.0F);
    write_float(bytes, 0x40U, 2.0F);

    write_le32(bytes, kPacketTableOffset, 0x10U);
    bytes[kPacketTableOffset + 0x04U] = std::byte{2};
    bytes[kPacketTableOffset + 0x08U] = std::byte{4};
    bytes[kPacketTableOffset + 0x09U] = std::byte{7};

    write_le32(bytes, kPacketDataOffset + 0x00U, 16U);
    write_le32(bytes, kPacketDataOffset + 0x04U, 0xffffffffU);
    write_le32(bytes, kPacketDataOffset + 0x08U, 0xffffffffU);
    write_le32(bytes, kPacketDataOffset + 0x0cU, 0xffffffffU);
    write_le32(bytes, kPacketDataOffset + 0x10U, 0U);
    write_le32(bytes, kPacketDataOffset + 0x14U, 0x50U);
    bytes[kPacketDataOffset + 0x23U] = std::byte{2};
    bytes[kPacketDataOffset + 0x28U] = std::byte{12};

    bytes[kPacketDataOffset + 0x2cU] = std::byte{3};
    bytes[kPacketDataOffset + 0x2eU] = std::byte{6};
    bytes[kPacketDataOffset + 0x30U] = std::byte{3};
    bytes[kPacketDataOffset + 0x32U] = std::byte{22};

    write_dinky_vertex(
        bytes, kVertexDataOffset + 0x00U, {512, 0, 0}, 7U, 4096, -2048);
    write_dinky_vertex(
        bytes, kVertexDataOffset + 0x10U, {0, 512, 0}, 10U, 0, 4096);
    write_dinky_vertex(
        bytes, kVertexDataOffset + 0x20U, {0, 0, 512}, 13U, 2048, 2048);
    write_dinky_vertex(
        bytes, kVertexDataOffset + 0x30U, {-512, 0, 0}, 23U, -4096, 0);
    write_fat_vertex(
        bytes, kVertexDataOffset + 0x40U, {0, -512, 0}, 26U, 1024, 3072);
    write_fat_vertex(
        bytes, kVertexDataOffset + 0x58U, {0, 0, -512}, 29U, -1024, -3072);
    return bytes;
}

void add_lighting_indices(std::vector<std::byte>& bytes) {
    bytes[kPacketTableOffset + 0x0aU] = std::byte{0x16};
    bytes[kPacketTableOffset + 0x0bU] = std::byte{3};
    constexpr std::array<std::uint8_t, 12U> indices{
        1U, 2U, 3U, 4U, 5U, 6U, 63U, 0xffU, 7U, 8U, 9U, 0xffU};
    for (std::size_t index = 0U; index < indices.size(); ++index) {
        const auto value = indices[index];
        bytes[kLightingOffset + index] = static_cast<std::byte>(value);
        bytes[kSecondaryLightingOffset + index] = static_cast<std::byte>(
            value == 0xffU ? value : value + 64U);
    }
}

template <typename Mutation>
void expect_rejected(Mutation&& mutation, const std::string& message) {
    auto bytes = make_class();
    std::invoke(std::forward<Mutation>(mutation), bytes);
    try {
        (void)openrc::parse_rac_tie_class_v1(bytes, kLimits);
    } catch (const openrc::RacTieClassError&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_valid_class() {
    const auto result = openrc::parse_rac_tie_class_v1(make_class(), kLimits);
    expect(
        result.input_bytes == kClassBytes &&
            result.header_range == openrc::RacTieRangeV1{0U, 0x70U} &&
            result.lod_packet_table_offsets[0U] == kPacketTableOffset &&
            result.lod_packet_counts[0U] == 1U &&
            result.texture_count == 2U && result.scale == 2.0F &&
            result.ad_gif_range ==
                openrc::RacTieRangeV1{kAdGifOffset, 0xa0U} &&
            result.high_lod_packet_table_range ==
                openrc::RacTieRangeV1{kPacketTableOffset, 0x10U},
        "RAC1 TIE class metadata is wrong");
    expect(
        result.packets.size() == 1U && result.primitives.size() == 2U &&
            result.vertices.size() == 6U && result.triangles.size() == 2U &&
            result.packets[0U].source_dinky_vertex_count == 4U &&
            result.packets[0U].source_fat_vertex_count == 2U &&
            result.packets[0U].primitive_count == 2U &&
            result.packets[0U].vertex_count == 6U &&
            result.packets[0U].triangle_count == 2U,
        "RAC1 TIE packet geometry totals are wrong");
    expect(
        result.vertices[0U].position ==
                std::array<float, 3U>{1.0F, 0.0F, 0.0F} &&
            result.vertices[0U].texture_coordinate ==
                std::array<float, 2U>{1.0F, -0.5F} &&
            result.vertices[4U].position ==
                std::array<float, 3U>{0.0F, -1.0F, 0.0F} &&
            result.vertices[4U].source_range ==
                openrc::RacTieRangeV1{kVertexDataOffset + 0x40U, 0x18U},
        "RAC1 TIE dinky/fat vertex decoding is wrong");
    expect(
        result.primitives[0U].local_texture_index == 0U &&
            result.primitives[1U].local_texture_index == 1U &&
            result.triangles[0U].vertex_indices ==
                std::array<std::uint32_t, 3U>{0U, 1U, 2U} &&
            result.triangles[1U].vertex_indices ==
                std::array<std::uint32_t, 3U>{3U, 4U, 5U} &&
            result.triangles[0U].local_texture_index == 0U &&
            result.triangles[1U].local_texture_index == 1U,
        "RAC1 TIE material or strip assembly is wrong");
    expect(result.packets[0U].lighting_indices_range.size == 0U &&
        result.packets[0U].secondary_lighting_indices_range.size == 0U &&
        std::all_of(result.vertices.begin(), result.vertices.end(),
            [](const auto& vertex) {
                return vertex.lighting_palette_index_count == 0U;
            }),
        "An omitted RAC1 TIE lighting stream manufactured palette indices");
}

void test_lighting_indices_and_morph_delta() {
    auto bytes = make_class();
    add_lighting_indices(bytes);
    write_i16(bytes, kVertexDataOffset + 0x40U, -32768);
    write_i16(bytes, kVertexDataOffset + 0x42U, 42);
    write_i16(bytes, kVertexDataOffset + 0x44U, 32767);
    // Encoded vertex order differs from GS write order. Indices must follow
    // their encoded owner through sorting, never be assigned after assembly.
    std::swap_ranges(bytes.begin() + kVertexDataOffset,
        bytes.begin() + kVertexDataOffset + 0x10U,
        bytes.begin() + kVertexDataOffset + 0x10U);
    auto result = openrc::parse_rac_tie_class_v1(bytes, kLimits);
    expect(result.packets[0U].lighting_indices_range ==
            openrc::RacTieRangeV1{kLightingOffset, 12U} &&
        result.packets[0U].secondary_lighting_indices_range ==
            openrc::RacTieRangeV1{kSecondaryLightingOffset, 12U} &&
        result.vertices[0U].lighting_palette_index_count == 1U &&
        result.vertices[0U].lighting_palette_indices[0U] == 2U &&
        result.vertices[1U].lighting_palette_indices[0U] == 1U &&
        result.vertices[4U].lighting_palette_index_count == 3U &&
        result.vertices[4U].lighting_palette_indices ==
            std::array<std::uint8_t, 3U>{5U, 6U, 63U} &&
        result.vertices[4U].quantized_morph_delta ==
            std::array<std::int16_t, 3U>{-32768, 42, 32767},
        "RAC1 TIE palette ownership or signed morph delta was lost");

    bytes = make_class();
    add_lighting_indices(bytes);
    write_le16(bytes, kVertexDataOffset + 0x0eU, 10U);
    result = openrc::parse_rac_tie_class_v1(bytes, kLimits);
    expect(result.packets[0U].discarded_duplicate_write_count == 1U &&
        result.vertices[1U].lighting_palette_indices[0U] == 1U &&
        result.vertices[1U].source_range == result.vertices[0U].source_range,
        "A duplicated RAC1 TIE GS write lost its source palette owner");
}

void test_lighting_rejections() {
    const auto rejected = [](const auto& mutation, const std::string& message) {
        expect_rejected([&](auto& bytes) {
            add_lighting_indices(bytes);
            mutation(bytes);
        }, message);
    };
    rejected([](auto& bytes) {
        bytes[kPacketTableOffset + 0x0bU] = std::byte{0};
    }, "A partially omitted RAC1 TIE lighting stream was accepted");
    rejected([](auto& bytes) {
        bytes[kPacketTableOffset + 0x0bU] = std::byte{2};
    }, "A RAC1 TIE lighting count disagreeing with vertex layout was accepted");
    rejected([](auto& bytes) {
        bytes.resize(kSecondaryLightingOffset + 11U);
    }, "A truncated secondary RAC1 TIE lighting stream was accepted");
    rejected([](auto& bytes) {
        bytes[kLightingOffset] = std::byte{64};
    }, "An out-of-range RAC1 TIE palette index was accepted");
    rejected([](auto& bytes) {
        bytes[kSecondaryLightingOffset + 5U] = std::byte{69};
    }, "An incorrect secondary RAC1 TIE palette index was accepted");
    rejected([](auto& bytes) {
        bytes[kLightingOffset + 7U] = std::byte{0};
    }, "An incorrect RAC1 TIE fat palette sentinel was accepted");

    // Five dinky vertices require three explicit zero alignment bytes.
    auto padded = make_class();
    add_lighting_indices(padded);
    padded[kPacketDataOffset + 0x28U] = std::byte{14};
    std::fill(padded.begin() + 0x100U, padded.begin() + 0x130U, std::byte{0});
    write_dinky_vertex(padded, 0x100U, {0, -512, 0}, 26U, 1024, 3072);
    write_fat_vertex(padded, 0x110U, {0, 0, -512}, 29U, -1024, -3072);
    for (const auto offset : {kLightingOffset, kSecondaryLightingOffset}) {
        std::fill(padded.begin() + offset + 5U,
            padded.begin() + offset + 8U, std::byte{0});
    }
    (void)openrc::parse_rac_tie_class_v1(padded, kLimits);
    padded[kSecondaryLightingOffset + 6U] = std::byte{1};
    try {
        (void)openrc::parse_rac_tie_class_v1(padded, kLimits);
    } catch (const openrc::RacTieClassError&) {
        return;
    }
    throw std::runtime_error("Nonzero RAC1 TIE dinky index padding was accepted");
}

void test_limits() {
    const auto bytes = make_class();
    const std::array<openrc::RacTieClassLimitsV1, 8U> candidates{{
        {0U, 1U, 1U, 1U, 1U, 1U, 1U},
        {kClassBytes - 1U, 1U, 1U, 1U, 1U, 1U, 1U},
        {kClassBytes, 0U, 1U, 1U, 1U, 1U, 1U},
        {kClassBytes, 1U, 1U, 1U, 1U, 1U, 1U},
        {kClassBytes, 1U, 2U, 5U, 6U, 2U, 2U},
        {kClassBytes, 1U, 2U, 6U, 5U, 2U, 2U},
        {kClassBytes, 1U, 2U, 6U, 6U, 1U, 2U},
        {kClassBytes, 1U, 2U, 6U, 6U, 2U, 1U},
    }};
    for (const auto limits : candidates) {
        try {
            (void)openrc::parse_rac_tie_class_v1(bytes, limits);
        } catch (const openrc::RacTieClassError&) {
            continue;
        }
        throw std::runtime_error("a RAC1 TIE caller limit was ignored");
    }
}

void test_explicit_lod() {
    auto bytes=make_class();
    write_le32(bytes,4U,kPacketTableOffset);
    bytes[0x21U]=std::byte{1};
    write_float(bytes,0x10U,10.0F);write_float(bytes,0x14U,20.0F);write_float(bytes,0x18U,30.0F);
    write_le16(bytes,0x24U,4U);
    const auto result=openrc::parse_rac_tie_lod_class_v1(bytes,kLimits,1U);
    expect(result.selected_lod==1U&&result.packets.size()==1U&&
        result.selected_lod_packet_table_range==openrc::RacTieRangeV1{kPacketTableOffset,16U}&&
        result.lod_threshold_bits[1U]==std::bit_cast<std::uint32_t>(20.0F)&&result.mode_bits==4U,
        "Explicit RAC1 TIE LOD assembly lost source metadata");
    const auto empty=openrc::parse_rac_tie_lod_class_v1(bytes,kLimits,2U);
    expect(empty.selected_lod==2U&&empty.packets.empty(),"An absent source TIE LOD fell back to another table");
    try{(void)openrc::parse_rac_tie_lod_class_v1(bytes,kLimits,3U);}
    catch(const openrc::RacTieClassError&){return;}
    throw std::runtime_error("An out-of-range RAC1 TIE LOD was accepted");
}

void test_rejections() {
    expect_rejected(
        [](auto& bytes) { write_float(bytes, 0x40U, 0.0F); },
        "a zero RAC1 TIE scale was accepted");
    expect_rejected(
        [](auto& bytes) { write_float(bytes, 0x30U, std::bit_cast<float>(0x7fc00000U)); },
        "a non-finite RAC1 TIE bounding sphere was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, kPacketTableOffset, 0x11U); },
        "an unaligned RAC1 TIE packet data offset was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kPacketDataOffset + 0x2dU] = std::byte{1}; },
        "a non-zero RAC1 TIE strip reserved byte was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kPacketDataOffset + 0x28U] = std::byte{11}; },
        "an invalid RAC1 TIE dinky/fat split was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, kPacketDataOffset + 0x14U, 0xa0U); },
        "an absent RAC1 TIE local material was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kPacketDataOffset + 0x2eU] = std::byte{7}; },
        "an impossible RAC1 TIE GS schedule was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kPacketDataOffset + 0x2cU] = std::byte{4}; },
        "a mismatched RAC1 TIE strip vertex count was accepted");
}

} // namespace

int main() {
    try {
        test_valid_class();
        test_lighting_indices_and_morph_delta();
        test_lighting_rejections();
        test_explicit_lod();
        test_limits();
        test_rejections();
        std::cout << "OpenRC RAC1 TIE class tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC RAC1 TIE class tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
