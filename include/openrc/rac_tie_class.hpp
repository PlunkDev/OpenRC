#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kRacTieClassHeaderBytesV1 = 0x70U;
inline constexpr std::uint32_t kRacTiePacketEntryBytesV1 = 0x10U;
inline constexpr std::uint32_t kRacTieAdGifBytesV1 = 0x50U;

struct RacTieClassLimitsV1 {
    std::uint64_t max_input_bytes = 0U;
    std::uint64_t max_packets = 0U;
    std::uint64_t max_strips = 0U;
    std::uint64_t max_source_vertices = 0U;
    std::uint64_t max_output_vertices = 0U;
    std::uint64_t max_output_triangles = 0U;
    std::uint64_t max_materials = 0U;
};

struct RacTieRangeV1 {
    std::uint64_t offset = 0U;
    std::uint64_t size = 0U;

    [[nodiscard]] bool operator==(const RacTieRangeV1&) const = default;
};

struct RacTieVertexV1 {
    std::array<std::int16_t, 3U> quantized_position{};
    std::array<float, 3U> position{};
    std::array<std::uint16_t, 3U> stq{};
    std::array<float, 2U> texture_coordinate{};
    std::uint16_t gs_packet_write_offset = 0U;
    RacTieRangeV1 source_range;
};

struct RacTieTriangleV1 {
    std::array<std::uint32_t, 3U> vertex_indices{};
    std::uint32_t local_texture_index = 0U;
    std::uint32_t packet_index = 0U;
};

struct RacTiePrimitiveV1 {
    RacTieRangeV1 strip_source_range;
    std::uint32_t local_texture_index = 0U;
    std::uint32_t vertex_begin = 0U;
    std::uint32_t vertex_count = 0U;
    std::uint32_t triangle_begin = 0U;
    std::uint32_t triangle_count = 0U;
    std::uint8_t encoded_vertex_count = 0U;
    std::uint8_t gif_tag_offset = 0U;
    bool reverse_first_winding = false;
};

struct RacTiePacketV1 {
    RacTieRangeV1 table_entry_range;
    RacTieRangeV1 data_prefix_range;
    RacTieRangeV1 vertex_data_range;
    RacTieRangeV1 vertex_padding_range;
    std::uint32_t data_offset = 0U;
    std::uint32_t primitive_begin = 0U;
    std::uint32_t primitive_count = 0U;
    std::uint32_t vertex_begin = 0U;
    std::uint32_t vertex_count = 0U;
    std::uint32_t triangle_begin = 0U;
    std::uint32_t triangle_count = 0U;
    std::uint32_t source_dinky_vertex_count = 0U;
    std::uint32_t source_fat_vertex_count = 0U;
    std::uint32_t discarded_duplicate_write_count = 0U;
};

struct RacTieClassV1 {
    std::uint64_t input_bytes = 0U;
    RacTieRangeV1 header_range;
    std::array<std::uint32_t, 3U> lod_packet_table_offsets{};
    std::array<std::uint8_t, 3U> lod_packet_counts{};
    std::uint8_t texture_count = 0U;
    std::uint32_t ad_gif_offset = 0U;
    RacTieRangeV1 ad_gif_range;
    std::array<std::uint32_t, 4U> bounding_sphere_bits{};
    std::array<float, 4U> bounding_sphere{};
    std::uint32_t scale_bits = 0U;
    float scale = 0.0F;
    RacTieRangeV1 high_lod_packet_table_range;
    std::vector<RacTiePacketV1> packets;
    std::vector<RacTiePrimitiveV1> primitives;
    std::vector<RacTieVertexV1> vertices;
    std::vector<RacTieTriangleV1> triangles;
};

class RacTieClassError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses and assembles the high-detail geometry of one RAC1 TIE class. The
// source is the exact level-core class envelope. Positions remain model-local
// and are scaled by the class header; local texture indices still require the
// corresponding level-core TIE-class slot table before rendering.
[[nodiscard]] RacTieClassV1 parse_rac_tie_class_v1(
    std::span<const std::byte> bytes,
    RacTieClassLimitsV1 limits);

} // namespace openrc
