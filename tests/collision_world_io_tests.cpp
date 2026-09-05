#include "openrc/collision_world.hpp"
#include "openrc/collision_world_io.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::CollisionWorldIoLimitsV1 kLimits{
    1024U * 1024U,
    {
        10'000U,
        10'000U,
        10'000U,
        100'000U,
        openrc::kCollisionDefaultGridCellSizeQ6V1,
    },
};

constexpr std::size_t kFormatVersionOffset = 0x08U;
constexpr std::size_t kHeaderBytesOffset = 0x0cU;
constexpr std::size_t kTotalBytesOffset = 0x10U;
constexpr std::size_t kQ6UnitsOffset = 0x18U;
constexpr std::size_t kGridCellSizeOffset = 0x1cU;
constexpr std::size_t kVertexCountOffset = 0x20U;
constexpr std::size_t kVertexTableOffsetOffset = 0x40U;
constexpr std::size_t kTriangleTableOffsetOffset = 0x48U;
constexpr std::size_t kGridCellTableOffsetOffset = 0x50U;
constexpr std::size_t kGridReferenceTableOffsetOffset = 0x58U;
constexpr std::size_t kHeaderReservedOffset = 0x60U;

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callback>
void expect_io_error(Callback&& callback, const std::string& message) {
    try {
        std::invoke(std::forward<Callback>(callback));
    } catch (const openrc::CollisionWorldIoError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] std::uint32_t read_u32(
    const std::vector<std::byte>& bytes,
    const std::size_t offset) {
    const auto byte_value = [&bytes](const std::size_t index) {
        return std::to_integer<std::uint32_t>(bytes[index]);
    };
    return byte_value(offset) | (byte_value(offset + 1U) << 8U) |
           (byte_value(offset + 2U) << 16U) |
           (byte_value(offset + 3U) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(
    const std::vector<std::byte>& bytes,
    const std::size_t offset) {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        value |= static_cast<std::uint64_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

void write_u8(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint8_t value) {
    bytes[offset] = static_cast<std::byte>(value);
}

void write_u32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & UINT32_C(0xff));
    }
}

void write_u64(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint64_t value) {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
    }
}

[[nodiscard]] openrc::CollisionSurfaceV1 raw_surface(
    const std::uint8_t raw_type) {
    return {
        true,
        raw_type,
        static_cast<std::uint8_t>(raw_type & UINT8_C(0x1f)),
        static_cast<std::uint8_t>(raw_type >> 5U),
    };
}

[[nodiscard]] openrc::CollisionWorldV1 make_world() {
    openrc::CollisionMeshV1 mesh;
    mesh.vertices = {
        {-64, -64, 0},
        {64, -64, 0},
        {0, 64, 0},
        {512, 0, 64},
        {576, 0, 64},
        {512, 64, 64},
    };
    mesh.triangles = {
        {
            {0U, 1U, 2U},
            raw_surface(0x8dU),
            openrc::CollisionLayerV1::world,
        },
        {
            {0U, 1U, 2U},
            {},
            openrc::CollisionLayerV1::hero_only,
        },
        {
            {3U, 4U, 5U},
            raw_surface(0x1fU),
            openrc::CollisionLayerV1::world,
        },
    };
    return openrc::build_collision_world_v1(
        std::move(mesh), kLimits.world);
}

template <typename Mutation>
void expect_decode_rejected(Mutation&& mutation, const std::string& message) {
    auto bytes = openrc::encode_collision_world_v1(make_world(), kLimits);
    std::invoke(std::forward<Mutation>(mutation), bytes);
    expect_io_error(
        [&] { (void)openrc::decode_collision_world_v1(bytes, kLimits); },
        message);
}

void test_identity_layout_and_round_trip() {
    expect(
        openrc::kCollisionWorldResourceIdV1 == "world/collision" &&
            openrc::kCollisionWorldResourceTypeIdV1 ==
                "openrc.collision-world" &&
            openrc::kCollisionWorldResourceSchemaVersionV1 == 1U &&
            openrc::kCollisionWorldIoFormatVersionV1 == 1U,
        "CollisionWorldV1 LevelPackage identity is wrong");
    const auto world = make_world();
    expect(
        world.grid.cells.size() == 5U &&
            world.grid.triangle_references.size() == 9U,
        "the I/O fixture does not exercise a multi-cell grid");
    const auto first = openrc::encode_collision_world_v1(world, kLimits);
    const auto second = openrc::encode_collision_world_v1(world, kLimits);
    constexpr std::size_t kExpectedBytes =
        openrc::kCollisionWorldIoHeaderBytesV1 +
        6U * openrc::kCollisionWorldIoVertexBytesV1 +
        3U * openrc::kCollisionWorldIoTriangleBytesV1 +
        5U * openrc::kCollisionWorldIoGridCellBytesV1 +
        9U * openrc::kCollisionWorldIoGridReferenceBytesV1;
    expect(
        first == second && first.size() == kExpectedBytes &&
            read_u64(first, kTotalBytesOffset) == kExpectedBytes,
        "CollisionWorldV1 encoding is not deterministic or tightly sized");
    expect(
        read_u64(first, kVertexTableOffsetOffset) == 0x80U &&
            read_u64(first, kTriangleTableOffsetOffset) == 0xc8U &&
            read_u64(first, kGridCellTableOffsetOffset) == 0x104U &&
            read_u64(first, kGridReferenceTableOffsetOffset) == 0x17cU,
        "CollisionWorldV1 table offsets are not canonical and contiguous");
    const auto vertex_offset = static_cast<std::size_t>(
        read_u64(first, kVertexTableOffsetOffset));
    expect(
        read_u32(first, vertex_offset) == UINT32_C(0xffffffc0) &&
            read_u32(first, vertex_offset + 4U) == UINT32_C(0xffffffc0) &&
            read_u32(first, vertex_offset + 8U) == 0U,
        "negative exact Q6 coordinates were not encoded little-endian");
    const auto decoded = openrc::decode_collision_world_v1(first, kLimits);
    expect(decoded == world,
           "CollisionWorldV1 did not round-trip mesh and rebuilt grid exactly");
    expect(
        openrc::encode_collision_world_v1(decoded, kLimits) == first,
        "decoded CollisionWorldV1 did not re-encode canonically");
}

void test_header_and_envelope_corruption() {
    expect_decode_rejected(
        [](auto& bytes) { bytes[0U] = std::byte{'X'}; },
        "bad CollisionWorldV1 magic was accepted");
    expect_decode_rejected(
        [](auto& bytes) { write_u32(bytes, kFormatVersionOffset, 2U); },
        "unknown CollisionWorldV1 format version was accepted");
    expect_decode_rejected(
        [](auto& bytes) { write_u32(bytes, kHeaderBytesOffset, 0x70U); },
        "bad CollisionWorldV1 header size was accepted");
    expect_decode_rejected(
        [](auto& bytes) { write_u32(bytes, kQ6UnitsOffset, 32U); },
        "unknown CollisionWorldV1 coordinate scale was accepted");
    expect_decode_rejected(
        [](auto& bytes) { write_u32(bytes, kGridCellSizeOffset, 128U); },
        "grid cell size disagreeing with caller policy was accepted");
    expect_decode_rejected(
        [](auto& bytes) { bytes[kHeaderReservedOffset] = std::byte{1U}; },
        "non-zero CollisionWorldV1 header reserved data was accepted");
    expect_decode_rejected(
        [](auto& bytes) { write_u64(bytes, kTotalBytesOffset, bytes.size() - 1U); },
        "bad CollisionWorldV1 total size was accepted");
    expect_decode_rejected(
        [](auto& bytes) {
            write_u64(
                bytes, kTriangleTableOffsetOffset,
                read_u64(bytes, kTriangleTableOffsetOffset) + 1U);
        },
        "non-canonical CollisionWorldV1 table offset was accepted");
    expect_decode_rejected(
        [](auto& bytes) { write_u64(bytes, kVertexCountOffset, UINT64_MAX); },
        "overflowing CollisionWorldV1 count was accepted");
    expect_decode_rejected(
        [](auto& bytes) { bytes.pop_back(); },
        "truncated CollisionWorldV1 was accepted");
    expect_decode_rejected(
        [](auto& bytes) { bytes.push_back(std::byte{0U}); },
        "trailing CollisionWorldV1 data was accepted");
}

void test_triangle_corruption() {
    expect_decode_rejected(
        [](auto& bytes) {
            const auto triangle = static_cast<std::size_t>(
                read_u64(bytes, kTriangleTableOffsetOffset));
            write_u32(bytes, triangle, 99U);
        },
        "out-of-range CollisionWorldV1 triangle index was accepted");
    expect_decode_rejected(
        [](auto& bytes) {
            const auto triangle = static_cast<std::size_t>(
                read_u64(bytes, kTriangleTableOffsetOffset));
            write_u8(bytes, triangle + 0x0cU, 2U);
        },
        "unknown CollisionWorldV1 triangle layer was accepted");
    expect_decode_rejected(
        [](auto& bytes) {
            const auto triangle = static_cast<std::size_t>(
                read_u64(bytes, kTriangleTableOffsetOffset));
            write_u8(bytes, triangle + 0x0dU, UINT8_C(0x80));
        },
        "unknown CollisionWorldV1 surface flags were accepted");
    expect_decode_rejected(
        [](auto& bytes) {
            const auto triangle = static_cast<std::size_t>(
                read_u64(bytes, kTriangleTableOffsetOffset));
            write_u8(bytes, triangle + 0x0fU, 0U);
        },
        "inconsistent CollisionWorldV1 raw surface split was accepted");
    expect_decode_rejected(
        [](auto& bytes) {
            const auto hero_triangle = static_cast<std::size_t>(
                read_u64(bytes, kTriangleTableOffsetOffset)) +
                openrc::kCollisionWorldIoTriangleBytesV1;
            write_u8(bytes, hero_triangle + 0x0eU, 1U);
        },
        "surface data without a source-surface flag was accepted");
    expect_decode_rejected(
        [](auto& bytes) {
            const auto triangle = static_cast<std::size_t>(
                read_u64(bytes, kTriangleTableOffsetOffset));
            write_u8(bytes, triangle + 0x11U, 1U);
        },
        "non-zero CollisionWorldV1 triangle reserved data was accepted");
}

void test_grid_corruption_and_rebuild_guard() {
    expect_decode_rejected(
        [](auto& bytes) {
            const auto cell = static_cast<std::size_t>(
                read_u64(bytes, kGridCellTableOffsetOffset));
            write_u32(bytes, cell + 0x0cU, 0U);
        },
        "empty CollisionWorldV1 grid cell was accepted");
    expect_decode_rejected(
        [](auto& bytes) {
            const auto cell = static_cast<std::size_t>(
                read_u64(bytes, kGridCellTableOffsetOffset));
            write_u64(bytes, cell + 0x10U, 1U);
        },
        "non-contiguous CollisionWorldV1 cell reference range was accepted");
    expect_decode_rejected(
        [](auto& bytes) {
            const auto cells = static_cast<std::size_t>(
                read_u64(bytes, kGridCellTableOffsetOffset));
            for (std::size_t index = 0U; index < 12U; ++index) {
                bytes[cells + openrc::kCollisionWorldIoGridCellBytesV1 + index] =
                    bytes[cells + index];
            }
        },
        "duplicate or unsorted CollisionWorldV1 grid cells were accepted");
    expect_decode_rejected(
        [](auto& bytes) {
            const auto references = static_cast<std::size_t>(
                read_u64(bytes, kGridReferenceTableOffsetOffset));
            write_u32(bytes, references, 1U);
            write_u32(bytes, references + 4U, 0U);
        },
        "descending CollisionWorldV1 cell references were accepted");
    expect_decode_rejected(
        [](auto& bytes) {
            const auto references = static_cast<std::size_t>(
                read_u64(bytes, kGridReferenceTableOffsetOffset));
            write_u32(bytes, references + 4U, 0U);
        },
        "duplicate CollisionWorldV1 cell references were accepted");
    expect_decode_rejected(
        [](auto& bytes) {
            const auto references = static_cast<std::size_t>(
                read_u64(bytes, kGridReferenceTableOffsetOffset));
            write_u32(bytes, references, 99U);
        },
        "out-of-range CollisionWorldV1 grid reference was accepted");

    // This remains structurally valid and sorted: triangle 1 is omitted and
    // triangle 2 substituted. Only the independent mesh rebuild can reject it.
    expect_decode_rejected(
        [](auto& bytes) {
            const auto references = static_cast<std::size_t>(
                read_u64(bytes, kGridReferenceTableOffsetOffset));
            write_u32(bytes, references + 4U, 2U);
        },
        "a structurally valid but stale CollisionWorldV1 grid bypassed rebuild validation");
    expect_decode_rejected(
        [](auto& bytes) {
            const auto cells = static_cast<std::size_t>(
                read_u64(bytes, kGridCellTableOffsetOffset));
            const auto last_cell = cells +
                4U * openrc::kCollisionWorldIoGridCellBytesV1;
            write_u32(bytes, last_cell, 3U);
        },
        "a sorted but mesh-inconsistent CollisionWorldV1 cell bypassed rebuild validation");
}

void test_encoder_validation_and_limits() {
    auto stale = make_world();
    stale.grid.triangle_references.pop_back();
    expect_io_error(
        [&] { (void)openrc::encode_collision_world_v1(stale, kLimits); },
        "encoder accepted a stale CollisionWorldV1 grid");

    auto invalid_mesh = make_world();
    invalid_mesh.mesh.triangles[0U].surface.kind = 0U;
    expect_io_error(
        [&] {
            (void)openrc::encode_collision_world_v1(invalid_mesh, kLimits);
        },
        "encoder leaked or ignored a CollisionWorldError from mesh rebuild");

    auto wrong_schema = make_world();
    wrong_schema.schema_version = 2U;
    expect_io_error(
        [&] {
            (void)openrc::encode_collision_world_v1(wrong_schema, kLimits);
        },
        "encoder accepted an unknown CollisionWorldV1 schema");

    const auto bytes = openrc::encode_collision_world_v1(make_world(), kLimits);
    auto byte_limit = kLimits;
    byte_limit.max_encoded_bytes = bytes.size() - 1U;
    expect_io_error(
        [&] {
            (void)openrc::encode_collision_world_v1(make_world(), byte_limit);
        },
        "encoder ignored its encoded-byte limit");
    expect_io_error(
        [&] { (void)openrc::decode_collision_world_v1(bytes, byte_limit); },
        "decoder ignored its encoded-byte limit");

    auto count_limit = kLimits;
    count_limit.world.max_vertices = 5U;
    expect_io_error(
        [&] { (void)openrc::decode_collision_world_v1(bytes, count_limit); },
        "decoder ignored its vertex limit before allocation");

    auto grid_policy = kLimits;
    grid_policy.world.grid_cell_size_q6 = 128;
    expect_io_error(
        [&] { (void)openrc::decode_collision_world_v1(bytes, grid_policy); },
        "decoder accepted a header grid size outside caller policy");
}

void test_practical_payload_size() {
    constexpr std::uint32_t kTriangleCount = 2048U;
    openrc::CollisionMeshV1 mesh;
    mesh.vertices.reserve(kTriangleCount * 3U);
    mesh.triangles.reserve(kTriangleCount);
    for (std::uint32_t index = 0U; index < kTriangleCount; ++index) {
        const auto x = static_cast<std::int32_t>(index % 64U) * 512;
        const auto y = static_cast<std::int32_t>(index / 64U) * 512;
        const auto first_vertex =
            static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({x, y, 0});
        mesh.vertices.push_back({x + 32, y, 0});
        mesh.vertices.push_back({x, y + 32, 0});
        mesh.triangles.push_back({
            {first_vertex, first_vertex + 1U, first_vertex + 2U},
            raw_surface(static_cast<std::uint8_t>(index & UINT8_C(0x7f))),
            openrc::CollisionLayerV1::world,
        });
    }
    const auto world = openrc::build_collision_world_v1(
        std::move(mesh), kLimits.world);
    const auto bytes = openrc::encode_collision_world_v1(world, kLimits);
    expect(
        world.grid.cells.size() == kTriangleCount &&
            world.grid.triangle_references.size() == kTriangleCount &&
            bytes.size() == 172'160U,
        "practical CollisionWorldV1 payload size is unexpectedly inflated");
    expect(
        openrc::decode_collision_world_v1(bytes, kLimits) == world,
        "practical multi-thousand-triangle payload did not round-trip");
}

} // namespace

int main() {
    try {
        test_identity_layout_and_round_trip();
        test_header_and_envelope_corruption();
        test_triangle_corruption();
        test_grid_corruption_and_rebuild_guard();
        test_encoder_validation_and_limits();
        test_practical_payload_size();
        std::cout << "collision world I/O tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "collision world I/O test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
