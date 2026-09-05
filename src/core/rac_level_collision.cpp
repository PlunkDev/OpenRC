#include "openrc/rac_level_collision.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw RacLevelCollisionError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

void require_range(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const std::uint64_t size,
    const char* const description) {
    if (offset > bytes.size() || size > bytes.size() - offset) {
        fail(std::string(description) + " lies outside the collision asset");
    }
}

[[nodiscard]] std::uint16_t read_le16(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const char* const description) {
    require_range(bytes, offset, 2U, description);
    const auto host_offset = static_cast<std::size_t>(offset);
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(byte_value(bytes[host_offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(byte_value(bytes[host_offset + 1U]))
            << 8U));
}

[[nodiscard]] std::int16_t read_le_i16(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const char* const description) {
    return std::bit_cast<std::int16_t>(read_le16(bytes, offset, description));
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const char* const description) {
    require_range(bytes, offset, 4U, description);
    const auto host_offset = static_cast<std::size_t>(offset);
    return static_cast<std::uint32_t>(byte_value(bytes[host_offset])) |
           (static_cast<std::uint32_t>(byte_value(bytes[host_offset + 1U]))
            << 8U) |
           (static_cast<std::uint32_t>(byte_value(bytes[host_offset + 2U]))
            << 16U) |
           (static_cast<std::uint32_t>(byte_value(bytes[host_offset + 3U]))
            << 24U);
}

[[nodiscard]] std::int32_t read_le_i32(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const char* const description) {
    return std::bit_cast<std::int32_t>(read_le32(bytes, offset, description));
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string(description) + " overflows a host offset");
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string(description) + " overflows a host size");
    }
    return left * right;
}

void add_bounded(
    std::uint64_t& total,
    const std::uint64_t addition,
    const std::uint64_t limit,
    const char* const description) {
    if (total > limit || addition > limit - total) {
        fail(std::string(description) + " exceeds the caller's limit");
    }
    total += addition;
}

void require_relative_range(
    const std::uint64_t relative_offset,
    const std::uint64_t size,
    const std::uint64_t envelope_size,
    const char* const description) {
    if (relative_offset > envelope_size ||
        size > envelope_size - relative_offset) {
        fail(std::string(description) + " lies outside its source section");
    }
}

[[nodiscard]] std::int32_t sign_extend(
    const std::uint32_t value,
    const std::uint32_t bits) {
    const auto sign_bit = UINT32_C(1) << (bits - 1U);
    const auto mask = (UINT32_C(1) << bits) - 1U;
    const auto narrowed = value & mask;
    if ((narrowed & sign_bit) == 0U) {
        return static_cast<std::int32_t>(narrowed);
    }
    return static_cast<std::int32_t>(narrowed | ~mask);
}

[[nodiscard]] RacLevelCollisionVectorV1 decode_local_vertex(
    const std::uint32_t packed) {
    return RacLevelCollisionVectorV1{
        static_cast<float>(sign_extend(packed, 10U)) / 16.0F,
        static_cast<float>(sign_extend(packed >> 10U, 10U)) / 16.0F,
        static_cast<float>(sign_extend(packed >> 20U, 12U)) / 64.0F,
    };
}

[[nodiscard]] RacLevelCollisionVectorV1 add(
    const RacLevelCollisionVectorV1 left,
    const RacLevelCollisionVectorV1 right) {
    return RacLevelCollisionVectorV1{
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

void validate_limits(const RacLevelCollisionLimitsV1& limits) {
    if (limits.max_input_bytes == 0U || limits.max_z_slots == 0U ||
        limits.max_y_slots == 0U || limits.max_x_slots == 0U ||
        limits.max_octants == 0U || limits.max_main_vertices == 0U ||
        limits.max_main_faces == 0U || limits.max_hero_groups == 0U ||
        limits.max_hero_vertices == 0U ||
        limits.max_hero_triangles == 0U) {
        fail("RAC1 level collision caller limits must all be non-zero");
    }
}

void parse_main_mesh(
    RacLevelCollisionV1& result,
    const std::span<const std::byte> bytes,
    const RacLevelCollisionLimitsV1& limits) {
    const auto mesh_begin = result.main_mesh_range.offset;
    const auto mesh_size = result.main_mesh_range.size;
    require_relative_range(0U, 4U, mesh_size, "The collision mesh header");

    result.z_base = read_le_i16(bytes, mesh_begin, "The collision Z base");
    result.z_count = read_le16(bytes, mesh_begin + 2U, "The collision Z count");
    if (result.z_count > limits.max_z_slots) {
        fail("The collision Z-slot count exceeds the caller's limit");
    }
    const auto z_table_bytes = checked_multiply(
        result.z_count, 2U, "The collision Z table size");
    require_relative_range(
        4U, z_table_bytes, mesh_size, "The collision Z table");

    for (std::uint32_t z_slot = 0U; z_slot < result.z_count; ++z_slot) {
        const auto encoded_z_offset = read_le16(
            bytes, mesh_begin + 4U + z_slot * 2U,
            "A collision Z-node offset");
        if (encoded_z_offset == 0U) {
            continue;
        }
        const auto z_node_offset =
            static_cast<std::uint64_t>(encoded_z_offset) * 4U;
        require_relative_range(
            z_node_offset, 4U, mesh_size, "A collision Z node");
        const auto z_node_begin = mesh_begin + z_node_offset;
        const auto y_base = read_le_i16(
            bytes, z_node_begin, "A collision Y base");
        const auto y_count = read_le16(
            bytes, z_node_begin + 2U, "A collision Y count");
        add_bounded(
            result.total_y_slots, y_count, limits.max_y_slots,
            "The aggregate collision Y-slot count");
        const auto y_table_bytes = checked_multiply(
            y_count, 4U, "A collision Y table size");
        require_relative_range(
            z_node_offset + 4U, y_table_bytes, mesh_size,
            "A collision Y table");

        for (std::uint32_t y_slot = 0U; y_slot < y_count; ++y_slot) {
            const auto y_node_offset = read_le32(
                bytes, z_node_begin + 4U + y_slot * 4U,
                "A collision Y-node offset");
            if (y_node_offset == 0U) {
                continue;
            }
            if ((y_node_offset & 3U) != 0U) {
                fail("A collision Y-node offset is not four-byte aligned");
            }
            require_relative_range(
                y_node_offset, 4U, mesh_size, "A collision Y node");
            const auto y_node_begin = mesh_begin + y_node_offset;
            const auto x_base = read_le_i16(
                bytes, y_node_begin, "A collision X base");
            const auto x_count = read_le16(
                bytes, y_node_begin + 2U, "A collision X count");
            add_bounded(
                result.total_x_slots, x_count, limits.max_x_slots,
                "The aggregate collision X-slot count");
            const auto x_table_bytes = checked_multiply(
                x_count, 4U, "A collision X table size");
            require_relative_range(
                static_cast<std::uint64_t>(y_node_offset) + 4U,
                x_table_bytes, mesh_size, "A collision X table");

            for (std::uint32_t x_slot = 0U; x_slot < x_count; ++x_slot) {
                const auto packed_leaf = read_le32(
                    bytes, y_node_begin + 4U + x_slot * 4U,
                    "A collision octant pointer");
                if (packed_leaf == 0U) {
                    continue;
                }
                const auto octant_offset = packed_leaf >> 8U;
                const auto declared_bytes =
                    static_cast<std::uint32_t>(packed_leaf & 0xffU) * 0x10U;
                if (octant_offset == 0U || declared_bytes == 0U ||
                    (octant_offset & 0x0fU) != 0U) {
                    fail("A collision octant pointer has an invalid offset or size");
                }
                require_relative_range(
                    octant_offset, declared_bytes, mesh_size,
                    "A collision octant");
                if (result.octants.size() >= limits.max_octants ||
                    result.octants.size() >= result.octants.max_size()) {
                    fail("The collision octant count exceeds the caller's limit");
                }

                const auto octant_begin = mesh_begin + octant_offset;
                const auto face_count = read_le16(
                    bytes, octant_begin, "A collision octant face count");
                const auto vertex_count = byte_value(
                    bytes[static_cast<std::size_t>(octant_begin + 2U)]);
                const auto quad_count = byte_value(
                    bytes[static_cast<std::size_t>(octant_begin + 3U)]);
                if (quad_count > face_count) {
                    fail(
                        "A collision octant at mesh offset " +
                        std::to_string(octant_offset) +
                        " has invalid face/vertex/quad counts " +
                        std::to_string(face_count) + "/" +
                        std::to_string(vertex_count) + "/" +
                        std::to_string(quad_count));
                }
                const auto vertex_bytes = checked_multiply(
                    vertex_count, 4U, "Collision octant vertex bytes");
                const auto face_bytes = checked_multiply(
                    face_count, 4U, "Collision octant face bytes");
                const auto used_bytes = checked_add(
                    checked_add(
                        checked_add(4U, vertex_bytes,
                                    "Collision octant used bytes"),
                        face_bytes, "Collision octant used bytes"),
                    quad_count, "Collision octant used bytes");
                if (used_bytes > declared_bytes) {
                    fail("A collision octant exceeds its declared envelope");
                }
                for (auto padding = used_bytes;
                     padding < declared_bytes; ++padding) {
                    if (byte_value(bytes[static_cast<std::size_t>(
                            octant_begin + padding)]) != 0U) {
                        fail("A collision octant has non-zero allocation padding");
                    }
                }
                add_bounded(
                    result.total_main_vertices, vertex_count,
                    limits.max_main_vertices,
                    "The aggregate collision vertex count");
                add_bounded(
                    result.total_main_faces, face_count,
                    limits.max_main_faces,
                    "The aggregate collision face count");
                add_bounded(
                    result.total_main_quads, quad_count,
                    limits.max_main_faces,
                    "The aggregate collision quad count");

                RacLevelCollisionOctantV1 octant;
                octant.source_range = {
                    octant_begin,
                    declared_bytes,
                };
                octant.mesh_relative_offset = octant_offset;
                octant.declared_bytes = declared_bytes;
                octant.grid_x = static_cast<std::int32_t>(x_base) +
                                 static_cast<std::int32_t>(x_slot);
                octant.grid_y = static_cast<std::int32_t>(y_base) +
                                 static_cast<std::int32_t>(y_slot);
                octant.grid_z = static_cast<std::int32_t>(result.z_base) +
                                 static_cast<std::int32_t>(z_slot);
                octant.world_center = {
                    static_cast<float>(octant.grid_x * 4 + 2),
                    static_cast<float>(octant.grid_y * 4 + 2),
                    static_cast<float>(octant.grid_z * 4 + 2),
                };
                octant.vertices.reserve(vertex_count);
                octant.faces.reserve(face_count);

                const auto vertex_begin = octant_begin + 4U;
                for (std::uint32_t vertex_index = 0U;
                     vertex_index < vertex_count; ++vertex_index) {
                    const auto source_offset =
                        vertex_begin + vertex_index * 4U;
                    const auto packed = read_le32(
                        bytes, source_offset,
                        "A packed collision octant vertex");
                    const auto local = decode_local_vertex(packed);
                    octant.vertices.push_back(RacLevelCollisionVertexV1{
                        {source_offset, 4U},
                        packed,
                        local,
                        add(local, octant.world_center),
                    });
                }

                const auto face_begin = vertex_begin + vertex_bytes;
                const auto quad_begin = face_begin + face_bytes;
                for (std::uint32_t face_index = 0U;
                     face_index < face_count; ++face_index) {
                    const auto source_offset = face_begin + face_index * 4U;
                    RacLevelCollisionFaceV1 face;
                    face.source_record_range = {source_offset, 4U};
                    const auto host_offset =
                        static_cast<std::size_t>(source_offset);
                    face.packed_vertex_indices[0U] =
                        byte_value(bytes[host_offset]);
                    face.packed_vertex_indices[1U] =
                        byte_value(bytes[host_offset + 1U]);
                    face.packed_vertex_indices[2U] =
                        byte_value(bytes[host_offset + 2U]);
                    face.surface_type = byte_value(bytes[host_offset + 3U]);
                    face.vertex_count = face_index < quad_count ? 4U : 3U;
                    if (face.vertex_count == 4U) {
                        const auto quad_offset = quad_begin + face_index;
                        face.source_quad_index_range = {quad_offset, 1U};
                        face.packed_vertex_indices[3U] = byte_value(
                            bytes[static_cast<std::size_t>(quad_offset)]);
                    }
                    for (std::uint32_t index = 0U;
                         index < face.vertex_count; ++index) {
                        if (face.packed_vertex_indices[index] >= vertex_count) {
                            fail("A collision face references a missing vertex");
                        }
                    }
                    octant.faces.push_back(face);
                }
                result.octants.push_back(std::move(octant));
            }
        }
    }
}

void parse_hero_groups(
    RacLevelCollisionV1& result,
    const std::span<const std::byte> bytes,
    const RacLevelCollisionLimitsV1& limits) {
    if (result.hero_section_range.size == 0U) {
        return;
    }
    const auto hero_begin = result.hero_section_range.offset;
    const auto hero_size = result.hero_section_range.size;
    require_relative_range(0U, 4U, hero_size, "The hero collision header");
    const auto signed_group_count = read_le_i32(
        bytes, hero_begin, "The hero collision group count");
    if (signed_group_count < 0 ||
        static_cast<std::uint64_t>(signed_group_count) >
            limits.max_hero_groups) {
        fail("The hero collision group count is invalid or exceeds its limit");
    }
    const auto group_count = static_cast<std::uint32_t>(signed_group_count);
    const auto table_bytes = checked_multiply(
        group_count, kRacLevelCollisionHeroGroupBytesV1,
        "The hero collision group table size");
    const auto table_end = checked_add(
        kRacLevelCollisionHeroTableOffsetV1, table_bytes,
        "The hero collision group table end");
    require_relative_range(
        kRacLevelCollisionHeroTableOffsetV1, table_bytes, hero_size,
        "The hero collision group table");
    if (group_count > result.hero_groups.max_size()) {
        fail("The hero collision group table exceeds a host container limit");
    }
    result.hero_groups.reserve(group_count);

    for (std::uint32_t group_index = 0U; group_index < group_count;
         ++group_index) {
        const auto table_relative = checked_add(
            kRacLevelCollisionHeroTableOffsetV1,
            checked_multiply(
                group_index, kRacLevelCollisionHeroGroupBytesV1,
                "A hero collision group table offset"),
            "A hero collision group table offset");
        const auto table_offset = hero_begin + table_relative;
        RacLevelHeroCollisionGroupV1 group;
        group.table_entry_range = {
            table_offset,
            kRacLevelCollisionHeroGroupBytesV1,
        };
        for (std::size_t component = 0U;
             component < group.packed_bounding_sphere.size(); ++component) {
            group.packed_bounding_sphere[component] = read_le16(
                bytes, table_offset + component * 2U,
                "A hero collision bounding sphere component");
        }
        group.bounding_sphere_center = {
            static_cast<float>(group.packed_bounding_sphere[0U]) / 64.0F,
            static_cast<float>(group.packed_bounding_sphere[1U]) / 64.0F,
            static_cast<float>(group.packed_bounding_sphere[2U]) / 64.0F,
        };
        group.bounding_sphere_radius =
            static_cast<float>(group.packed_bounding_sphere[3U]) / 64.0F;
        const auto triangle_count = read_le16(
            bytes, table_offset + 8U,
            "A hero collision triangle count");
        const auto vertex_count = read_le16(
            bytes, table_offset + 10U,
            "A hero collision vertex count");
        group.hero_relative_data_offset = read_le32(
            bytes, table_offset + 12U,
            "A hero collision group data offset");
        const auto vertex_bytes = checked_multiply(
            vertex_count, 8U, "Hero collision vertex bytes");
        const auto triangle_bytes = checked_multiply(
            triangle_count, 4U, "Hero collision triangle bytes");
        const auto data_bytes = checked_add(
            vertex_bytes, triangle_bytes, "Hero collision group data bytes");
        if (group.hero_relative_data_offset < table_end) {
            fail("A hero collision group data block overlaps its table");
        }
        require_relative_range(
            group.hero_relative_data_offset, data_bytes, hero_size,
            "A hero collision group data block");
        add_bounded(
            result.total_hero_vertices, vertex_count,
            limits.max_hero_vertices,
            "The aggregate hero collision vertex count");
        add_bounded(
            result.total_hero_triangles, triangle_count,
            limits.max_hero_triangles,
            "The aggregate hero collision triangle count");
        group.data_range = {
            hero_begin + group.hero_relative_data_offset,
            data_bytes,
        };
        group.vertices.reserve(vertex_count);
        group.triangles.reserve(triangle_count);

        const auto vertex_begin = group.data_range.offset;
        for (std::uint32_t vertex_index = 0U;
             vertex_index < vertex_count; ++vertex_index) {
            const auto source_offset = vertex_begin + vertex_index * 8U;
            RacLevelHeroCollisionVertexV1 vertex;
            vertex.source_range = {source_offset, 8U};
            for (std::size_t component = 0U;
                 component < vertex.packed_position.size(); ++component) {
                vertex.packed_position[component] = read_le16(
                    bytes, source_offset + component * 2U,
                    "A hero collision vertex component");
            }
            if (read_le16(
                    bytes, source_offset + 6U,
                    "A hero collision vertex reserved field") != 0U) {
                fail("A hero collision vertex has a non-zero reserved field");
            }
            vertex.world_position = {
                static_cast<float>(vertex.packed_position[0U]) / 64.0F,
                static_cast<float>(vertex.packed_position[1U]) / 64.0F,
                static_cast<float>(vertex.packed_position[2U]) / 64.0F,
            };
            group.vertices.push_back(vertex);
        }

        const auto triangle_begin = vertex_begin + vertex_bytes;
        for (std::uint32_t triangle_index = 0U;
             triangle_index < triangle_count; ++triangle_index) {
            const auto source_offset = triangle_begin + triangle_index * 4U;
            const auto host_offset = static_cast<std::size_t>(source_offset);
            RacLevelHeroCollisionTriangleV1 triangle;
            triangle.source_range = {source_offset, 4U};
            triangle.packed_vertex_indices = {
                byte_value(bytes[host_offset]),
                byte_value(bytes[host_offset + 1U]),
                byte_value(bytes[host_offset + 2U]),
            };
            if (byte_value(bytes[host_offset + 3U]) != 0U) {
                fail("A hero collision triangle has a non-zero reserved field");
            }
            for (const auto vertex_index : triangle.packed_vertex_indices) {
                if (vertex_index >= vertex_count) {
                    fail("A hero collision triangle references a missing vertex");
                }
            }
            group.triangles.push_back(triangle);
        }
        result.hero_groups.push_back(std::move(group));
    }
}

} // namespace

RacLevelCollisionV1 parse_rac_level_collision_v1(
    const std::span<const std::byte> collision_bytes,
    const RacLevelCollisionLimitsV1 limits) {
    validate_limits(limits);
    if (collision_bytes.size() > limits.max_input_bytes) {
        fail("The RAC1 level collision asset exceeds the caller's byte limit");
    }
    if (collision_bytes.size() < kRacLevelCollisionHeaderBytesV1 ||
        collision_bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail("The RAC1 level collision asset has an invalid envelope");
    }

    RacLevelCollisionV1 result;
    result.input_bytes = collision_bytes.size();
    result.header_range = {0U, kRacLevelCollisionHeaderBytesV1};
    result.main_mesh_offset = read_le_i32(
        collision_bytes, 0U, "The main collision mesh offset");
    result.hero_groups_offset = read_le_i32(
        collision_bytes, 4U, "The hero collision section offset");
    if (result.main_mesh_offset <
            static_cast<std::int32_t>(kRacLevelCollisionHeaderBytesV1) ||
        (result.main_mesh_offset &
         static_cast<std::int32_t>(
             kRacLevelCollisionSectionAlignmentV1 - 1U)) != 0 ||
        static_cast<std::uint64_t>(result.main_mesh_offset) >=
            collision_bytes.size()) {
        fail("The main collision mesh offset is invalid");
    }
    if (result.hero_groups_offset < 0 ||
        (result.hero_groups_offset != 0 &&
         ((result.hero_groups_offset &
           static_cast<std::int32_t>(
               kRacLevelCollisionSectionAlignmentV1 - 1U)) != 0 ||
          result.hero_groups_offset <= result.main_mesh_offset ||
          static_cast<std::uint64_t>(result.hero_groups_offset) >=
              collision_bytes.size()))) {
        fail("The hero collision section offset is invalid");
    }

    const auto main_begin =
        static_cast<std::uint64_t>(result.main_mesh_offset);
    const auto main_end = result.hero_groups_offset == 0
                              ? static_cast<std::uint64_t>(collision_bytes.size())
                              : static_cast<std::uint64_t>(
                                    result.hero_groups_offset);
    result.main_mesh_range = {main_begin, main_end - main_begin};
    if (result.hero_groups_offset != 0) {
        const auto hero_begin =
            static_cast<std::uint64_t>(result.hero_groups_offset);
        result.hero_section_range = {
            hero_begin,
            collision_bytes.size() - hero_begin,
        };
    }

    parse_main_mesh(result, collision_bytes, limits);
    parse_hero_groups(result, collision_bytes, limits);
    return result;
}

} // namespace openrc
