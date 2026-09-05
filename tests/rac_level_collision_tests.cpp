#include "openrc/rac_level_collision.hpp"

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

constexpr std::size_t kCollisionBytes = 0x180U;
constexpr std::size_t kMeshOffset = 0x40U;
constexpr std::size_t kHeroOffset = 0x100U;
constexpr std::size_t kLeafOffset = 0x20U;
constexpr openrc::RacLevelCollisionLimitsV1 kLimits{
    0x1000U,
    64U,
    64U,
    64U,
    64U,
    1024U,
    1024U,
    64U,
    1024U,
    1024U,
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

void write_i32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::int32_t value) {
    write_le32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] std::uint32_t packed_signed(
    const std::int32_t value,
    const std::uint32_t bits) {
    return static_cast<std::uint32_t>(value) &
           ((UINT32_C(1) << bits) - 1U);
}

[[nodiscard]] std::uint32_t pack_vertex(
    const std::int32_t x_sixteenths,
    const std::int32_t y_sixteenths,
    const std::int32_t z_sixty_fourths) {
    return packed_signed(x_sixteenths, 10U) |
           (packed_signed(y_sixteenths, 10U) << 10U) |
           (packed_signed(z_sixty_fourths, 12U) << 20U);
}

[[nodiscard]] std::vector<std::byte> make_collision() {
    std::vector<std::byte> bytes(kCollisionBytes, std::byte{0});
    write_i32(bytes, 0x00U, static_cast<std::int32_t>(kMeshOffset));
    write_i32(bytes, 0x04U, static_cast<std::int32_t>(kHeroOffset));

    write_i16(bytes, kMeshOffset + 0x00U, -2);
    write_le16(bytes, kMeshOffset + 0x02U, 2U);
    write_le16(bytes, kMeshOffset + 0x04U, 0U);
    write_le16(bytes, kMeshOffset + 0x06U, 2U);

    write_i16(bytes, kMeshOffset + 0x08U, 3);
    write_le16(bytes, kMeshOffset + 0x0aU, 1U);
    write_le32(bytes, kMeshOffset + 0x0cU, 0x10U);

    write_i16(bytes, kMeshOffset + 0x10U, -1);
    write_le16(bytes, kMeshOffset + 0x12U, 2U);
    write_le32(bytes, kMeshOffset + 0x14U, 0U);
    write_le32(
        bytes, kMeshOffset + 0x18U,
        (static_cast<std::uint32_t>(kLeafOffset) << 8U) | 2U);

    const auto leaf = kMeshOffset + kLeafOffset;
    write_le16(bytes, leaf + 0x00U, 2U);
    bytes[leaf + 0x02U] = std::byte{4};
    bytes[leaf + 0x03U] = std::byte{1};
    write_le32(bytes, leaf + 0x04U, pack_vertex(-16, -16, 0));
    write_le32(bytes, leaf + 0x08U, pack_vertex(16, -16, 0));
    write_le32(bytes, leaf + 0x0cU, pack_vertex(16, 16, 0));
    write_le32(bytes, leaf + 0x10U, pack_vertex(-16, 16, 0));
    bytes[leaf + 0x14U] = std::byte{0};
    bytes[leaf + 0x15U] = std::byte{1};
    bytes[leaf + 0x16U] = std::byte{2};
    bytes[leaf + 0x17U] = std::byte{7};
    bytes[leaf + 0x18U] = std::byte{0};
    bytes[leaf + 0x19U] = std::byte{2};
    bytes[leaf + 0x1aU] = std::byte{3};
    bytes[leaf + 0x1bU] = std::byte{0x81};
    bytes[leaf + 0x1cU] = std::byte{3};

    write_i32(bytes, kHeroOffset + 0x00U, 1);
    const auto hero_table = kHeroOffset + 0x10U;
    write_le16(bytes, hero_table + 0x00U, 64U);
    write_le16(bytes, hero_table + 0x02U, 128U);
    write_le16(bytes, hero_table + 0x04U, 192U);
    write_le16(bytes, hero_table + 0x06U, 256U);
    write_le16(bytes, hero_table + 0x08U, 1U);
    write_le16(bytes, hero_table + 0x0aU, 3U);
    write_le32(bytes, hero_table + 0x0cU, 0x20U);

    const auto hero_data = kHeroOffset + 0x20U;
    const std::array<std::array<std::uint16_t, 3U>, 3U> hero_vertices{{
        {64U, 128U, 192U},
        {128U, 128U, 192U},
        {64U, 192U, 192U},
    }};
    for (std::size_t index = 0U; index < hero_vertices.size(); ++index) {
        for (std::size_t component = 0U; component < 3U; ++component) {
            write_le16(
                bytes, hero_data + index * 8U + component * 2U,
                hero_vertices[index][component]);
        }
    }
    bytes[hero_data + 0x18U] = std::byte{0};
    bytes[hero_data + 0x19U] = std::byte{1};
    bytes[hero_data + 0x1aU] = std::byte{2};
    return bytes;
}

[[nodiscard]] openrc::RacLevelCollisionV1 parse(
    const std::vector<std::byte>& bytes) {
    return openrc::parse_rac_level_collision_v1(bytes, kLimits);
}

template <typename Mutation>
void expect_rejected(Mutation&& mutation, const std::string& message) {
    auto bytes = make_collision();
    std::invoke(std::forward<Mutation>(mutation), bytes);
    try {
        (void)parse(bytes);
    } catch (const openrc::RacLevelCollisionError&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_valid_collision() {
    const auto result = parse(make_collision());
    expect(
        result.input_bytes == kCollisionBytes &&
            result.header_range ==
                openrc::RacLevelCollisionRangeV1{0U, 8U} &&
            result.main_mesh_range ==
                openrc::RacLevelCollisionRangeV1{0x40U, 0xc0U} &&
            result.hero_section_range ==
                openrc::RacLevelCollisionRangeV1{0x100U, 0x80U} &&
            result.z_base == -2 && result.z_count == 2U &&
            result.total_y_slots == 1U && result.total_x_slots == 2U,
        "RAC1 collision envelope or sparse tree totals are wrong");
    expect(
        result.octants.size() == 1U &&
            result.total_main_vertices == 4U &&
            result.total_main_faces == 2U &&
            result.total_main_quads == 1U,
        "RAC1 collision octant totals are wrong");
    const auto& octant = result.octants.front();
    expect(
        octant.grid_x == 0 && octant.grid_y == 3 && octant.grid_z == -1 &&
            octant.world_center ==
                openrc::RacLevelCollisionVectorV1{2.0F, 14.0F, -2.0F} &&
            octant.source_range ==
                openrc::RacLevelCollisionRangeV1{0x60U, 0x20U},
        "RAC1 collision octant coordinates or envelope are wrong");
    expect(
        octant.vertices[0U].local_position ==
                openrc::RacLevelCollisionVectorV1{-1.0F, -1.0F, 0.0F} &&
            octant.vertices[0U].world_position ==
                openrc::RacLevelCollisionVectorV1{1.0F, 13.0F, -2.0F} &&
            octant.faces[0U].vertex_count == 4U &&
            octant.faces[0U].packed_vertex_indices ==
                std::array<std::uint8_t, 4U>{0U, 1U, 2U, 3U} &&
            octant.faces[0U].surface_type == 7U &&
            octant.faces[1U].vertex_count == 3U &&
            octant.faces[1U].surface_type == 0x81U,
        "RAC1 packed collision geometry is wrong");
    expect(
        result.hero_groups.size() == 1U &&
            result.total_hero_vertices == 3U &&
            result.total_hero_triangles == 1U,
        "RAC1 hero collision totals are wrong");
    const auto& hero = result.hero_groups.front();
    expect(
        hero.bounding_sphere_center ==
                openrc::RacLevelCollisionVectorV1{1.0F, 2.0F, 3.0F} &&
            hero.bounding_sphere_radius == 4.0F &&
            hero.vertices[1U].world_position ==
                openrc::RacLevelCollisionVectorV1{2.0F, 2.0F, 3.0F} &&
            hero.triangles[0U].packed_vertex_indices ==
                std::array<std::uint8_t, 3U>{0U, 1U, 2U},
        "RAC1 hero collision data is wrong");
}

void test_no_hero_section() {
    auto bytes = make_collision();
    write_i32(bytes, 0x04U, 0);
    const auto result = parse(bytes);
    expect(
        result.hero_section_range == openrc::RacLevelCollisionRangeV1{} &&
            result.hero_groups.empty() &&
            result.main_mesh_range ==
                openrc::RacLevelCollisionRangeV1{0x40U, 0x140U},
        "RAC1 collision without a hero section was parsed incorrectly");
}

void test_vertex_only_octant() {
    auto bytes = make_collision();
    const auto leaf = kMeshOffset + kLeafOffset;
    write_le16(bytes, leaf, 0U);
    bytes[leaf + 0x02U] = std::byte{1};
    bytes[leaf + 0x03U] = std::byte{0};
    for (std::size_t offset = 8U; offset < 0x20U; ++offset) {
        bytes[leaf + offset] = std::byte{0};
    }
    const auto result = parse(bytes);
    expect(
        result.octants.size() == 1U &&
            result.octants[0U].vertices.size() == 1U &&
            result.octants[0U].faces.empty(),
        "a source-authored vertex-only collision octant was rejected");
}

void test_limits() {
    const auto bytes = make_collision();
    auto limits = kLimits;
    limits.max_octants = 0U;
    try {
        (void)openrc::parse_rac_level_collision_v1(bytes, limits);
    } catch (const openrc::RacLevelCollisionError&) {
        limits = kLimits;
        limits.max_main_faces = 1U;
        try {
            (void)openrc::parse_rac_level_collision_v1(bytes, limits);
        } catch (const openrc::RacLevelCollisionError&) {
            limits = kLimits;
            limits.max_hero_vertices = 2U;
            try {
                (void)openrc::parse_rac_level_collision_v1(bytes, limits);
            } catch (const openrc::RacLevelCollisionError&) {
                return;
            }
        }
    }
    throw std::runtime_error("a RAC1 collision caller limit was ignored");
}

void test_tree_rejections() {
    expect_rejected(
        [](auto& bytes) { write_i32(bytes, 0x00U, 4); },
        "an overlapping main collision mesh was accepted");
    expect_rejected(
        [](auto& bytes) { write_i32(bytes, 0x00U, 0x44); },
        "an unaligned main collision mesh was accepted");
    expect_rejected(
        [](auto& bytes) { write_le16(bytes, kMeshOffset + 0x06U, 0xffffU); },
        "an out-of-range collision Z node was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(bytes, kMeshOffset + 0x0cU, 0xfffffff0U);
        },
        "an out-of-range collision Y node was accepted");
    expect_rejected(
        [](auto& bytes) {
            write_le32(
                bytes, kMeshOffset + 0x18U,
                (static_cast<std::uint32_t>(kLeafOffset) << 8U) | 1U);
        },
        "an undersized collision octant envelope was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kMeshOffset + kLeafOffset + 0x15U] = std::byte{9}; },
        "a collision face with a missing vertex was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kMeshOffset + kLeafOffset + 0x1dU] = std::byte{1}; },
        "non-zero collision octant padding was accepted");
}

void test_hero_rejections() {
    expect_rejected(
        [](auto& bytes) { write_i32(bytes, kHeroOffset, -1); },
        "a negative hero collision group count was accepted");
    expect_rejected(
        [](auto& bytes) { write_le32(bytes, kHeroOffset + 0x1cU, 0x10U); },
        "hero collision data overlapping its table was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kHeroOffset + 0x20U + 6U] = std::byte{1}; },
        "a non-zero hero collision vertex reserved field was accepted");
    expect_rejected(
        [](auto& bytes) { bytes[kHeroOffset + 0x20U + 0x1aU] = std::byte{7}; },
        "a hero collision triangle with a missing vertex was accepted");
}

} // namespace

int main() {
    try {
        test_valid_collision();
        test_no_hero_section();
        test_vertex_only_octant();
        test_limits();
        test_tree_rejections();
        test_hero_rejections();
        std::cout << "RAC1 level collision tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RAC1 level collision test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
