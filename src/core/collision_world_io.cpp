#include "openrc/collision_world_io.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::array<std::byte, 8U> kMagic{
    std::byte{'O'}, std::byte{'R'}, std::byte{'C'}, std::byte{'C'},
    std::byte{'O'}, std::byte{'L'}, std::byte{0U}, std::byte{0U},
};

constexpr std::uint64_t kFormatVersionOffset = 0x08U;
constexpr std::uint64_t kHeaderBytesOffset = 0x0cU;
constexpr std::uint64_t kTotalBytesOffset = 0x10U;
constexpr std::uint64_t kQ6UnitsOffset = 0x18U;
constexpr std::uint64_t kGridCellSizeQ6Offset = 0x1cU;
constexpr std::uint64_t kVertexCountOffset = 0x20U;
constexpr std::uint64_t kTriangleCountOffset = 0x28U;
constexpr std::uint64_t kGridCellCountOffset = 0x30U;
constexpr std::uint64_t kGridReferenceCountOffset = 0x38U;
constexpr std::uint64_t kVertexTableOffsetOffset = 0x40U;
constexpr std::uint64_t kTriangleTableOffsetOffset = 0x48U;
constexpr std::uint64_t kGridCellTableOffsetOffset = 0x50U;
constexpr std::uint64_t kGridReferenceTableOffsetOffset = 0x58U;
constexpr std::uint64_t kHeaderReservedOffset = 0x60U;

constexpr std::uint64_t kTriangleLayerOffset = 0x0cU;
constexpr std::uint64_t kTriangleSurfaceFlagsOffset = 0x0dU;
constexpr std::uint64_t kTriangleRawSurfaceOffset = 0x0eU;
constexpr std::uint64_t kTriangleSurfaceKindOffset = 0x0fU;
constexpr std::uint64_t kTriangleSoundIdOffset = 0x10U;
constexpr std::uint64_t kTriangleReservedOffset = 0x11U;
constexpr std::uint8_t kTriangleHasSourceSurfaceFlag = UINT8_C(1);

constexpr std::uint64_t kGridCellReferenceCountOffset = 0x0cU;
constexpr std::uint64_t kGridCellFirstReferenceOffset = 0x10U;

[[noreturn]] void fail(const std::string& message) {
    throw CollisionWorldIoError(message);
}

void validate_limits(const CollisionWorldIoLimitsV1& limits) {
    if (limits.max_encoded_bytes < kCollisionWorldIoHeaderBytesV1 ||
        limits.world.max_vertices == 0U ||
        limits.world.max_triangles == 0U ||
        limits.world.max_grid_cells == 0U ||
        limits.world.max_grid_triangle_references == 0U ||
        limits.world.grid_cell_size_q6 <= 0) {
        fail("CollisionWorldV1 I/O limits must all be positive and bounded");
    }
    if (limits.world.max_vertices >
            std::numeric_limits<std::uint32_t>::max() ||
        limits.world.max_triangles >
            std::numeric_limits<std::uint32_t>::max()) {
        fail("CollisionWorldV1 indexed-mesh limits exceed the V1 index width");
    }
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string(description) + " overflows uint64_t");
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string(description) + " overflows uint64_t");
    }
    return left * right;
}

[[nodiscard]] std::size_t host_size(
    const std::uint64_t value,
    const char* const description) {
    if (value > std::numeric_limits<std::size_t>::max()) {
        fail(std::string(description) + " exceeds host size_t");
    }
    return static_cast<std::size_t>(value);
}

void require_range(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const std::uint64_t size,
    const char* const description) {
    if (offset > bytes.size() || size > bytes.size() - offset) {
        fail(std::string(description) + " lies outside CollisionWorldV1");
    }
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint8_t read_u8(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const char* const description) {
    require_range(bytes, offset, 1U, description);
    return byte_value(bytes[host_size(offset, description)]);
}

[[nodiscard]] std::uint32_t read_u32(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const char* const description) {
    require_range(bytes, offset, 4U, description);
    const auto begin = host_size(offset, description);
    return static_cast<std::uint32_t>(byte_value(bytes[begin])) |
           (static_cast<std::uint32_t>(byte_value(bytes[begin + 1U])) << 8U) |
           (static_cast<std::uint32_t>(byte_value(bytes[begin + 2U])) << 16U) |
           (static_cast<std::uint32_t>(byte_value(bytes[begin + 3U])) << 24U);
}

[[nodiscard]] std::int32_t read_i32(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const char* const description) {
    return std::bit_cast<std::int32_t>(read_u32(bytes, offset, description));
}

[[nodiscard]] std::uint64_t read_u64(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const char* const description) {
    require_range(bytes, offset, 8U, description);
    const auto begin = host_size(offset, description);
    std::uint64_t result = 0U;
    for (std::size_t index = 0U; index < sizeof(result); ++index) {
        result |= static_cast<std::uint64_t>(byte_value(bytes[begin + index]))
                  << (index * 8U);
    }
    return result;
}

void write_u8(
    std::vector<std::byte>& bytes,
    const std::uint64_t offset,
    const std::uint8_t value) {
    bytes[host_size(offset, "A CollisionWorldV1 output byte offset")] =
        static_cast<std::byte>(value);
}

void write_u32(
    std::vector<std::byte>& bytes,
    const std::uint64_t offset,
    const std::uint32_t value) {
    const auto begin = host_size(offset, "A CollisionWorldV1 output word offset");
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[begin + index] =
            static_cast<std::byte>((value >> (index * 8U)) & UINT32_C(0xff));
    }
}

void write_i32(
    std::vector<std::byte>& bytes,
    const std::uint64_t offset,
    const std::int32_t value) {
    write_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void write_u64(
    std::vector<std::byte>& bytes,
    const std::uint64_t offset,
    const std::uint64_t value) {
    const auto begin = host_size(offset, "A CollisionWorldV1 output word offset");
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[begin + index] =
            static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
    }
}

[[nodiscard]] bool all_zero(
    const std::span<const std::byte> bytes,
    const std::uint64_t begin,
    const std::uint64_t end,
    const char* const description) {
    if (end < begin) {
        fail(std::string(description) + " has an inverted range");
    }
    require_range(bytes, begin, end - begin, description);
    const auto range = bytes.subspan(
        host_size(begin, description),
        host_size(end - begin, description));
    return std::all_of(
        range.begin(), range.end(),
        [](const std::byte value) { return value == std::byte{0U}; });
}

struct EncodedLayout final {
    std::uint64_t vertex_offset = 0U;
    std::uint64_t triangle_offset = 0U;
    std::uint64_t grid_cell_offset = 0U;
    std::uint64_t grid_reference_offset = 0U;
    std::uint64_t total_bytes = 0U;
};

[[nodiscard]] EncodedLayout encoded_layout(
    const std::uint64_t vertex_count,
    const std::uint64_t triangle_count,
    const std::uint64_t grid_cell_count,
    const std::uint64_t grid_reference_count) {
    EncodedLayout result;
    result.vertex_offset = kCollisionWorldIoHeaderBytesV1;
    result.triangle_offset = checked_add(
        result.vertex_offset,
        checked_multiply(
            vertex_count, kCollisionWorldIoVertexBytesV1,
            "The CollisionWorldV1 vertex table size"),
        "The CollisionWorldV1 triangle table offset");
    result.grid_cell_offset = checked_add(
        result.triangle_offset,
        checked_multiply(
            triangle_count, kCollisionWorldIoTriangleBytesV1,
            "The CollisionWorldV1 triangle table size"),
        "The CollisionWorldV1 grid-cell table offset");
    result.grid_reference_offset = checked_add(
        result.grid_cell_offset,
        checked_multiply(
            grid_cell_count, kCollisionWorldIoGridCellBytesV1,
            "The CollisionWorldV1 grid-cell table size"),
        "The CollisionWorldV1 grid-reference table offset");
    result.total_bytes = checked_add(
        result.grid_reference_offset,
        checked_multiply(
            grid_reference_count, kCollisionWorldIoGridReferenceBytesV1,
            "The CollisionWorldV1 grid-reference table size"),
        "The CollisionWorldV1 total size");
    return result;
}

[[nodiscard]] bool coordinate_less(
    const CollisionGridCoordinateV1& left,
    const CollisionGridCoordinateV1& right) noexcept {
    return std::tie(left.x, left.y, left.z) <
           std::tie(right.x, right.y, right.z);
}

[[nodiscard]] CollisionWorldV1 rebuild_world(
    CollisionMeshV1 mesh,
    const CollisionWorldBuildLimitsV1 limits,
    const char* const context) {
    try {
        return build_collision_world_v1(std::move(mesh), limits);
    } catch (const CollisionWorldError& error) {
        fail(std::string(context) + ": " + error.what());
    }
}

void require_count(
    const std::uint64_t count,
    const std::uint64_t limit,
    const std::size_t container_max,
    const char* const description) {
    if (count > limit || count > container_max ||
        count > std::numeric_limits<std::size_t>::max()) {
        fail(std::string(description) + " exceeds its caller or host limit");
    }
}

[[nodiscard]] CollisionSurfaceV1 decode_surface(
    const bool has_source_type,
    const std::uint8_t raw_type,
    const std::uint8_t kind,
    const std::uint8_t sound_id) {
    if (has_source_type) {
        if (kind != (raw_type & UINT8_C(0x1f)) ||
            sound_id != (raw_type >> 5U)) {
            fail("A CollisionWorldV1 triangle has an inconsistent surface split");
        }
    } else if (raw_type != 0U || kind != 0U || sound_id != 0U) {
        fail("A CollisionWorldV1 triangle without a source surface has surface data");
    }
    return {has_source_type, raw_type, kind, sound_id};
}

} // namespace

std::vector<std::byte> encode_collision_world_v1(
    const CollisionWorldV1& world,
    const CollisionWorldIoLimitsV1 limits) {
    validate_limits(limits);
    if (world.schema_version != kCollisionWorldSchemaVersionV1) {
        fail("CollisionWorldV1 has an unsupported world schema version");
    }
    const auto rebuilt = rebuild_world(
        world.mesh,
        limits.world,
        "CollisionWorldV1 mesh validation failed during encoding");
    if (world.grid != rebuilt.grid) {
        fail("CollisionWorldV1 has a stale or non-canonical uniform grid");
    }

    const auto vertex_count =
        static_cast<std::uint64_t>(rebuilt.mesh.vertices.size());
    const auto triangle_count =
        static_cast<std::uint64_t>(rebuilt.mesh.triangles.size());
    const auto grid_cell_count =
        static_cast<std::uint64_t>(rebuilt.grid.cells.size());
    const auto grid_reference_count =
        static_cast<std::uint64_t>(rebuilt.grid.triangle_references.size());
    const auto layout = encoded_layout(
        vertex_count, triangle_count, grid_cell_count, grid_reference_count);
    if (layout.total_bytes > limits.max_encoded_bytes) {
        fail("CollisionWorldV1 encoded size exceeds the caller's byte limit");
    }
    std::vector<std::byte> result(
        host_size(layout.total_bytes, "The CollisionWorldV1 encoded size"),
        std::byte{0U});
    std::copy(kMagic.begin(), kMagic.end(), result.begin());
    write_u32(result, kFormatVersionOffset, kCollisionWorldIoFormatVersionV1);
    write_u32(result, kHeaderBytesOffset, kCollisionWorldIoHeaderBytesV1);
    write_u64(result, kTotalBytesOffset, layout.total_bytes);
    write_u32(
        result, kQ6UnitsOffset,
        static_cast<std::uint32_t>(kCollisionQ6UnitsPerWorldUnitV1));
    write_u32(
        result, kGridCellSizeQ6Offset,
        static_cast<std::uint32_t>(limits.world.grid_cell_size_q6));
    write_u64(result, kVertexCountOffset, vertex_count);
    write_u64(result, kTriangleCountOffset, triangle_count);
    write_u64(result, kGridCellCountOffset, grid_cell_count);
    write_u64(result, kGridReferenceCountOffset, grid_reference_count);
    write_u64(result, kVertexTableOffsetOffset, layout.vertex_offset);
    write_u64(result, kTriangleTableOffsetOffset, layout.triangle_offset);
    write_u64(result, kGridCellTableOffsetOffset, layout.grid_cell_offset);
    write_u64(
        result, kGridReferenceTableOffsetOffset,
        layout.grid_reference_offset);

    for (std::size_t index = 0U; index < rebuilt.mesh.vertices.size(); ++index) {
        const auto offset = checked_add(
            layout.vertex_offset,
            checked_multiply(
                index, kCollisionWorldIoVertexBytesV1,
                "A CollisionWorldV1 vertex record offset"),
            "A CollisionWorldV1 vertex record offset");
        const auto& vertex = rebuilt.mesh.vertices[index];
        write_i32(result, offset + 0U, vertex.x);
        write_i32(result, offset + 4U, vertex.y);
        write_i32(result, offset + 8U, vertex.z);
    }
    for (std::size_t index = 0U; index < rebuilt.mesh.triangles.size(); ++index) {
        const auto offset = checked_add(
            layout.triangle_offset,
            checked_multiply(
                index, kCollisionWorldIoTriangleBytesV1,
                "A CollisionWorldV1 triangle record offset"),
            "A CollisionWorldV1 triangle record offset");
        const auto& triangle = rebuilt.mesh.triangles[index];
        for (std::size_t vertex = 0U;
             vertex < triangle.vertex_indices.size(); ++vertex) {
            write_u32(
                result, offset + vertex * 4U,
                triangle.vertex_indices[vertex]);
        }
        write_u8(
            result, offset + kTriangleLayerOffset,
            static_cast<std::uint8_t>(triangle.layer));
        write_u8(
            result, offset + kTriangleSurfaceFlagsOffset,
            triangle.surface.has_source_type
                ? kTriangleHasSourceSurfaceFlag
                : UINT8_C(0));
        write_u8(
            result, offset + kTriangleRawSurfaceOffset,
            triangle.surface.raw_type);
        write_u8(
            result, offset + kTriangleSurfaceKindOffset,
            triangle.surface.kind);
        write_u8(
            result, offset + kTriangleSoundIdOffset,
            triangle.surface.sound_id);
    }
    for (std::size_t index = 0U; index < rebuilt.grid.cells.size(); ++index) {
        const auto offset = checked_add(
            layout.grid_cell_offset,
            checked_multiply(
                index, kCollisionWorldIoGridCellBytesV1,
                "A CollisionWorldV1 grid-cell record offset"),
            "A CollisionWorldV1 grid-cell record offset");
        const auto& cell = rebuilt.grid.cells[index];
        write_i32(result, offset + 0U, cell.coordinate.x);
        write_i32(result, offset + 4U, cell.coordinate.y);
        write_i32(result, offset + 8U, cell.coordinate.z);
        write_u32(
            result, offset + kGridCellReferenceCountOffset,
            cell.reference_count);
        write_u64(
            result, offset + kGridCellFirstReferenceOffset,
            cell.first_reference);
    }
    for (std::size_t index = 0U;
         index < rebuilt.grid.triangle_references.size(); ++index) {
        const auto offset = checked_add(
            layout.grid_reference_offset,
            checked_multiply(
                index, kCollisionWorldIoGridReferenceBytesV1,
                "A CollisionWorldV1 grid-reference offset"),
            "A CollisionWorldV1 grid-reference offset");
        write_u32(
            result, offset,
            rebuilt.grid.triangle_references[index]);
    }
    return result;
}

CollisionWorldV1 decode_collision_world_v1(
    const std::span<const std::byte> bytes,
    const CollisionWorldIoLimitsV1 limits) {
    validate_limits(limits);
    if (bytes.size() > limits.max_encoded_bytes) {
        fail("CollisionWorldV1 exceeds the caller's byte limit");
    }
    if (bytes.size() < kCollisionWorldIoHeaderBytesV1) {
        fail("CollisionWorldV1 header is truncated");
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        fail("CollisionWorldV1 magic is invalid");
    }
    if (read_u32(bytes, kFormatVersionOffset, "The format version") !=
            kCollisionWorldIoFormatVersionV1 ||
        read_u32(bytes, kHeaderBytesOffset, "The header size") !=
            kCollisionWorldIoHeaderBytesV1 ||
        read_u32(bytes, kQ6UnitsOffset, "The Q6 unit count") !=
            static_cast<std::uint32_t>(
                kCollisionQ6UnitsPerWorldUnitV1) ||
        !all_zero(
            bytes, kHeaderReservedOffset, kCollisionWorldIoHeaderBytesV1,
            "The CollisionWorldV1 reserved header")) {
        fail("CollisionWorldV1 header schema or reserved bytes are invalid");
    }
    const auto encoded_grid_cell_size = read_u32(
        bytes, kGridCellSizeQ6Offset, "The grid cell size");
    if (encoded_grid_cell_size !=
        static_cast<std::uint32_t>(limits.world.grid_cell_size_q6)) {
        fail("CollisionWorldV1 grid cell size disagrees with caller policy");
    }
    const auto total_bytes = read_u64(
        bytes, kTotalBytesOffset, "The total byte size");
    if (total_bytes != bytes.size()) {
        fail("CollisionWorldV1 total byte size is invalid or has trailing data");
    }

    const auto vertex_count = read_u64(
        bytes, kVertexCountOffset, "The vertex count");
    const auto triangle_count = read_u64(
        bytes, kTriangleCountOffset, "The triangle count");
    const auto grid_cell_count = read_u64(
        bytes, kGridCellCountOffset, "The grid-cell count");
    const auto grid_reference_count = read_u64(
        bytes, kGridReferenceCountOffset, "The grid-reference count");
    const auto layout = encoded_layout(
        vertex_count, triangle_count, grid_cell_count, grid_reference_count);
    if (layout.total_bytes != total_bytes ||
        read_u64(bytes, kVertexTableOffsetOffset, "The vertex table offset") !=
            layout.vertex_offset ||
        read_u64(
            bytes, kTriangleTableOffsetOffset,
            "The triangle table offset") != layout.triangle_offset ||
        read_u64(
            bytes, kGridCellTableOffsetOffset,
            "The grid-cell table offset") != layout.grid_cell_offset ||
        read_u64(
            bytes, kGridReferenceTableOffsetOffset,
            "The grid-reference table offset") !=
            layout.grid_reference_offset) {
        fail("CollisionWorldV1 table offsets or exact encoded size are invalid");
    }

    CollisionMeshV1 mesh;
    CollisionUniformGridV1 encoded_grid;
    encoded_grid.cell_size_q6 = limits.world.grid_cell_size_q6;
    require_count(
        vertex_count, limits.world.max_vertices,
        mesh.vertices.max_size(), "The CollisionWorldV1 vertex count");
    require_count(
        triangle_count, limits.world.max_triangles,
        mesh.triangles.max_size(), "The CollisionWorldV1 triangle count");
    require_count(
        grid_cell_count, limits.world.max_grid_cells,
        encoded_grid.cells.max_size(), "The CollisionWorldV1 grid-cell count");
    require_count(
        grid_reference_count,
        limits.world.max_grid_triangle_references,
        encoded_grid.triangle_references.max_size(),
        "The CollisionWorldV1 grid-reference count");
    const auto host_vertex_count = host_size(
        vertex_count, "The CollisionWorldV1 vertex count");
    const auto host_triangle_count = host_size(
        triangle_count, "The CollisionWorldV1 triangle count");
    const auto host_grid_cell_count = host_size(
        grid_cell_count, "The CollisionWorldV1 grid-cell count");
    const auto host_grid_reference_count = host_size(
        grid_reference_count, "The CollisionWorldV1 grid-reference count");

    mesh.vertices.reserve(host_vertex_count);
    for (std::size_t index = 0U; index < host_vertex_count; ++index) {
        const auto offset = layout.vertex_offset +
            index * kCollisionWorldIoVertexBytesV1;
        mesh.vertices.push_back({
            read_i32(bytes, offset + 0U, "A CollisionWorldV1 vertex X"),
            read_i32(bytes, offset + 4U, "A CollisionWorldV1 vertex Y"),
            read_i32(bytes, offset + 8U, "A CollisionWorldV1 vertex Z"),
        });
    }

    mesh.triangles.reserve(host_triangle_count);
    for (std::size_t index = 0U; index < host_triangle_count; ++index) {
        const auto offset = layout.triangle_offset +
            index * kCollisionWorldIoTriangleBytesV1;
        CollisionTriangleV1 triangle;
        for (std::size_t vertex = 0U;
             vertex < triangle.vertex_indices.size(); ++vertex) {
            triangle.vertex_indices[vertex] = read_u32(
                bytes, offset + vertex * 4U,
                "A CollisionWorldV1 triangle vertex index");
            if (triangle.vertex_indices[vertex] >= vertex_count) {
                fail("A CollisionWorldV1 triangle references a missing vertex");
            }
        }
        const auto layer = read_u8(
            bytes, offset + kTriangleLayerOffset,
            "A CollisionWorldV1 triangle layer");
        if (layer > static_cast<std::uint8_t>(
                        CollisionLayerV1::hero_only)) {
            fail("A CollisionWorldV1 triangle uses an unknown layer");
        }
        triangle.layer = static_cast<CollisionLayerV1>(layer);
        const auto surface_flags = read_u8(
            bytes, offset + kTriangleSurfaceFlagsOffset,
            "A CollisionWorldV1 triangle surface flag");
        if ((surface_flags & static_cast<std::uint8_t>(
                                 ~kTriangleHasSourceSurfaceFlag)) != 0U) {
            fail("A CollisionWorldV1 triangle uses unknown surface flags");
        }
        if (!all_zero(
                bytes,
                offset + kTriangleReservedOffset,
                offset + kCollisionWorldIoTriangleBytesV1,
                "A CollisionWorldV1 triangle reserved field")) {
            fail("A CollisionWorldV1 triangle reserved field is non-zero");
        }
        triangle.surface = decode_surface(
            (surface_flags & kTriangleHasSourceSurfaceFlag) != 0U,
            read_u8(
                bytes, offset + kTriangleRawSurfaceOffset,
                "A CollisionWorldV1 raw surface type"),
            read_u8(
                bytes, offset + kTriangleSurfaceKindOffset,
                "A CollisionWorldV1 surface kind"),
            read_u8(
                bytes, offset + kTriangleSoundIdOffset,
                "A CollisionWorldV1 sound ID"));
        mesh.triangles.push_back(triangle);
    }

    encoded_grid.cells.reserve(host_grid_cell_count);
    std::uint64_t expected_first_reference = 0U;
    for (std::size_t index = 0U; index < host_grid_cell_count; ++index) {
        const auto offset = layout.grid_cell_offset +
            index * kCollisionWorldIoGridCellBytesV1;
        CollisionUniformGridCellV1 cell;
        cell.coordinate = {
            read_i32(bytes, offset + 0U, "A CollisionWorldV1 grid-cell X"),
            read_i32(bytes, offset + 4U, "A CollisionWorldV1 grid-cell Y"),
            read_i32(bytes, offset + 8U, "A CollisionWorldV1 grid-cell Z"),
        };
        cell.reference_count = read_u32(
            bytes, offset + kGridCellReferenceCountOffset,
            "A CollisionWorldV1 grid-cell reference count");
        cell.first_reference = read_u64(
            bytes, offset + kGridCellFirstReferenceOffset,
            "A CollisionWorldV1 grid-cell first reference");
        if (cell.reference_count == 0U) {
            fail("A CollisionWorldV1 grid cell is empty");
        }
        if (!encoded_grid.cells.empty() &&
            !coordinate_less(
                encoded_grid.cells.back().coordinate, cell.coordinate)) {
            fail("CollisionWorldV1 grid cells are duplicate or out of order");
        }
        if (cell.first_reference != expected_first_reference ||
            cell.reference_count >
                grid_reference_count - expected_first_reference) {
            fail("A CollisionWorldV1 grid cell has a non-canonical reference range");
        }
        expected_first_reference += cell.reference_count;
        encoded_grid.cells.push_back(cell);
    }
    if (expected_first_reference != grid_reference_count) {
        fail("CollisionWorldV1 grid cells do not own every encoded reference");
    }

    encoded_grid.triangle_references.reserve(host_grid_reference_count);
    for (std::size_t index = 0U; index < host_grid_reference_count; ++index) {
        const auto offset = layout.grid_reference_offset +
            index * kCollisionWorldIoGridReferenceBytesV1;
        const auto triangle_index = read_u32(
            bytes, offset, "A CollisionWorldV1 grid triangle reference");
        if (triangle_index >= triangle_count) {
            fail("A CollisionWorldV1 grid cell references a missing triangle");
        }
        encoded_grid.triangle_references.push_back(triangle_index);
    }
    for (const auto& cell : encoded_grid.cells) {
        const auto begin = host_size(
            cell.first_reference,
            "A CollisionWorldV1 grid-cell reference offset");
        const auto end = begin + cell.reference_count;
        for (auto index = begin + 1U; index < end; ++index) {
            if (encoded_grid.triangle_references[index - 1U] >=
                encoded_grid.triangle_references[index]) {
                fail("CollisionWorldV1 grid-cell references are duplicate or out of order");
            }
        }
    }

    auto rebuilt = rebuild_world(
        std::move(mesh),
        limits.world,
        "CollisionWorldV1 mesh validation failed during decoding");
    if (encoded_grid != rebuilt.grid) {
        fail("CollisionWorldV1 encoded grid disagrees with its rebuilt mesh index");
    }
    return rebuilt;
}

} // namespace openrc
