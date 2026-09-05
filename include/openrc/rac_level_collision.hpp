#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kRacLevelCollisionHeaderBytesV1 = 0x08U;
inline constexpr std::uint32_t kRacLevelCollisionHeroTableOffsetV1 = 0x10U;
inline constexpr std::uint32_t kRacLevelCollisionHeroGroupBytesV1 = 0x10U;
inline constexpr std::uint32_t kRacLevelCollisionCellSizeV1 = 4U;
inline constexpr std::uint32_t kRacLevelCollisionSectionAlignmentV1 = 0x40U;

struct RacLevelCollisionLimitsV1 {
    std::uint64_t max_input_bytes = 0U;
    std::uint64_t max_z_slots = 0U;
    std::uint64_t max_y_slots = 0U;
    std::uint64_t max_x_slots = 0U;
    std::uint64_t max_octants = 0U;
    std::uint64_t max_main_vertices = 0U;
    std::uint64_t max_main_faces = 0U;
    std::uint64_t max_hero_groups = 0U;
    std::uint64_t max_hero_vertices = 0U;
    std::uint64_t max_hero_triangles = 0U;
};

struct RacLevelCollisionRangeV1 {
    std::uint64_t offset = 0U;
    std::uint64_t size = 0U;

    [[nodiscard]] bool operator==(
        const RacLevelCollisionRangeV1&) const = default;
};

struct RacLevelCollisionVectorV1 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    [[nodiscard]] bool operator==(
        const RacLevelCollisionVectorV1&) const = default;
};

struct RacLevelCollisionVertexV1 {
    RacLevelCollisionRangeV1 source_range;
    std::uint32_t packed_value = 0U;
    RacLevelCollisionVectorV1 local_position;
    RacLevelCollisionVectorV1 world_position;
};

struct RacLevelCollisionFaceV1 {
    RacLevelCollisionRangeV1 source_record_range;
    // Non-empty only for a quad. Its fourth packed index is stored in a
    // separate byte array after all face records in the source format.
    RacLevelCollisionRangeV1 source_quad_index_range;
    std::array<std::uint8_t, 4U> packed_vertex_indices{};
    std::uint8_t vertex_count = 0U;
    std::uint8_t surface_type = 0U;
};

struct RacLevelCollisionOctantV1 {
    RacLevelCollisionRangeV1 source_range;
    std::uint32_t mesh_relative_offset = 0U;
    std::uint32_t declared_bytes = 0U;
    std::int32_t grid_x = 0;
    std::int32_t grid_y = 0;
    std::int32_t grid_z = 0;
    RacLevelCollisionVectorV1 world_center;
    std::vector<RacLevelCollisionVertexV1> vertices;
    std::vector<RacLevelCollisionFaceV1> faces;
};

struct RacLevelHeroCollisionVertexV1 {
    RacLevelCollisionRangeV1 source_range;
    std::array<std::uint16_t, 3U> packed_position{};
    RacLevelCollisionVectorV1 world_position;
};

struct RacLevelHeroCollisionTriangleV1 {
    RacLevelCollisionRangeV1 source_range;
    // Source winding is retained exactly. A neutral collision compiler can
    // choose its runtime winding without losing provenance.
    std::array<std::uint8_t, 3U> packed_vertex_indices{};
};

struct RacLevelHeroCollisionGroupV1 {
    RacLevelCollisionRangeV1 table_entry_range;
    RacLevelCollisionRangeV1 data_range;
    std::array<std::uint16_t, 4U> packed_bounding_sphere{};
    RacLevelCollisionVectorV1 bounding_sphere_center;
    float bounding_sphere_radius = 0.0F;
    std::uint32_t hero_relative_data_offset = 0U;
    std::vector<RacLevelHeroCollisionVertexV1> vertices;
    std::vector<RacLevelHeroCollisionTriangleV1> triangles;
};

struct RacLevelCollisionV1 {
    std::uint64_t input_bytes = 0U;
    RacLevelCollisionRangeV1 header_range;
    RacLevelCollisionRangeV1 main_mesh_range;
    RacLevelCollisionRangeV1 hero_section_range;
    std::int32_t main_mesh_offset = 0;
    std::int32_t hero_groups_offset = 0;
    std::int16_t z_base = 0;
    std::uint16_t z_count = 0U;
    std::vector<RacLevelCollisionOctantV1> octants;
    std::vector<RacLevelHeroCollisionGroupV1> hero_groups;
    std::uint64_t total_y_slots = 0U;
    std::uint64_t total_x_slots = 0U;
    std::uint64_t total_main_vertices = 0U;
    std::uint64_t total_main_faces = 0U;
    std::uint64_t total_main_quads = 0U;
    std::uint64_t total_hero_vertices = 0U;
    std::uint64_t total_hero_triangles = 0U;
};

class RacLevelCollisionError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses the complete collision asset borrowed from a decoded RAC1 level
// core. Every returned source range is relative to collision_bytes. The
// parser preserves the packed octant topology and surface IDs so compilation
// into a neutral runtime collision mesh remains deterministic and lossless.
[[nodiscard]] RacLevelCollisionV1 parse_rac_level_collision_v1(
    std::span<const std::byte> collision_bytes,
    RacLevelCollisionLimitsV1 limits);

} // namespace openrc
