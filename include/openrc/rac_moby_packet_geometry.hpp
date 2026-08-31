#pragma once

#include "openrc/rac_moby_class.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kRacMobyVertexHeaderBytesV1 = 0x20U;
inline constexpr std::uint32_t kRacMobyVertexRecordBytesV1 = 0x10U;
inline constexpr std::uint32_t kRacMobyTexturePrimitiveBytesV1 = 0x40U;
inline constexpr std::uint16_t kRacMobyVertexCacheEntriesV1 = 0x200U;

struct RacMobyPacketGeometryLimitsV1 {
  std::uint64_t max_input_bytes = 0U;
  std::uint64_t max_vif_commands = 0U;
  std::uint64_t max_matrix_transfers = 0U;
  std::uint64_t max_vertices = 0U;
  std::uint64_t max_strip_indices = 0U;
  std::uint64_t max_texture_primitives = 0U;
  std::uint64_t max_triangles = 0U;
};

struct RacMobyPacketGeometryRangeV1 {
  // Offsets are relative to the complete decoded MobyClass input.
  std::uint64_t offset = 0U;
  std::uint64_t size = 0U;

  [[nodiscard]] bool
  operator==(const RacMobyPacketGeometryRangeV1 &) const = default;
};

enum class RacMobyPacketUnpackKindV1 : std::uint8_t {
  texture_coordinates,
  strip_indices,
  texture_primitives,
};

struct RacMobyPacketUnpackV1 {
  RacMobyPacketUnpackKindV1 kind =
      RacMobyPacketUnpackKindV1::texture_coordinates;
  RacMobyPacketGeometryRangeV1 code_range;
  RacMobyPacketGeometryRangeV1 payload_range;
  RacMobyPacketGeometryRangeV1 packet_range;
  std::uint32_t raw_code = 0U;
  std::uint16_t vector_count = 0U;
  std::uint16_t destination_qword = 0U;
  bool signed_data = false;
  bool use_tops = false;
};

struct RacMobyTexCoordV1 {
  RacMobyPacketGeometryRangeV1 source_range;
  std::int16_t s_fixed12 = 0;
  std::int16_t t_fixed12 = 0;
  float s = 0.0F;
  float t = 0.0F;
};

struct RacMobyMatrixTransferV1 {
  RacMobyPacketGeometryRangeV1 source_range;
  std::uint8_t scratchpad_joint_index = 0U;
  std::uint8_t vu0_destination = 0U;
};

struct RacMobyPacketLocalVertexV1 {
  RacMobyPacketGeometryRangeV1 source_range;
  std::array<std::uint8_t, 8U> control_bytes{};
  std::uint8_t normal_azimuth = 0U;
  std::uint8_t normal_elevation = 0U;
  std::array<std::int16_t, 3U> quantized_position{};
  // This is the packet-local quantized position multiplied by class scale.
  // It has not been transformed into bind, instance, or world space.
  std::array<float, 3U> diagnostic_position{};
  std::uint16_t vertex_cache_index = 0U;
};

struct RacMobyDuplicateVertexV1 {
  RacMobyPacketGeometryRangeV1 source_range;
  std::uint16_t encoded_cache_index = 0U;
  std::uint16_t vertex_cache_index = 0U;
  // Empty when the duplicate refers to cache state inherited from an earlier
  // packet. A one-packet diagnostic must retain, rather than guess, that case.
  std::optional<std::uint32_t> packet_local_source_vertex;
};

struct RacMobyTexturePrimitiveV1 {
  RacMobyPacketGeometryRangeV1 source_range;
  std::int32_t texture_index = 0;
  std::array<std::uint32_t, 4U> secret_index_words{};
};

struct RacMobyPacketStripV1 {
  std::optional<std::int32_t> texture_index;
  // Indices address the transfer-vertex domain: main records followed by
  // duplicate records. Restart suppression is expanded to zero-area entries.
  std::vector<std::uint32_t> transfer_vertex_indices;
};

struct RacMobyPacketTriangleV1 {
  std::array<std::uint32_t, 3U> transfer_vertex_indices{};
  std::optional<std::int32_t> texture_index;
};

struct RacMobyVertexTableHeaderV1 {
  RacMobyPacketGeometryRangeV1 header_range;
  RacMobyPacketGeometryRangeV1 matrix_transfer_range;
  RacMobyPacketGeometryRangeV1 duplicate_range;
  RacMobyPacketGeometryRangeV1 vertex_record_range;
  RacMobyPacketGeometryRangeV1 epilogue_range;
  RacMobyPacketGeometryRangeV1 trailing_range;
  std::uint32_t matrix_transfer_count = 0U;
  std::uint32_t two_way_blend_vertex_count = 0U;
  std::uint32_t three_way_blend_vertex_count = 0U;
  std::uint32_t main_vertex_count = 0U;
  std::uint32_t duplicate_vertex_count = 0U;
  std::uint32_t transfer_vertex_count = 0U;
  std::uint32_t vertex_table_offset = 0U;
  std::uint32_t epilogue_end_offset = 0U;
};

struct RacMobyPacketGeometryV1 {
  std::uint64_t input_bytes = 0U;
  RacMobyPacketUnpackV1 texture_coordinate_unpack;
  RacMobyPacketUnpackV1 strip_index_unpack;
  std::optional<RacMobyPacketUnpackV1> texture_primitive_unpack;
  std::uint64_t vif_nop_count = 0U;
  RacMobyPacketGeometryRangeV1 index_header_range;
  RacMobyPacketGeometryRangeV1 raw_strip_index_range;
  RacMobyPacketGeometryRangeV1 strip_terminator_range;
  RacMobyPacketGeometryRangeV1 strip_trailing_padding_range;
  std::uint8_t index_header_unknown = 0U;
  std::uint8_t texture_unpack_relative_qwords = 0U;

  RacMobyVertexTableHeaderV1 vertex_header;
  std::vector<RacMobyTexCoordV1> texture_coordinates;
  std::vector<RacMobyMatrixTransferV1> matrix_transfers;
  std::vector<RacMobyPacketLocalVertexV1> vertices;
  std::vector<RacMobyDuplicateVertexV1> duplicate_vertices;
  std::vector<RacMobyTexturePrimitiveV1> texture_primitives;
  std::vector<RacMobyPacketStripV1> strips;
  std::vector<RacMobyPacketTriangleV1> triangles;
  std::uint64_t consumed_texture_primitive_count = 0U;
  std::uint64_t unresolved_duplicate_count = 0U;
};

class RacMobyPacketGeometryError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Decodes one regular RAC1 high/low-LOD packet using ranges already exposed by
// RacMobyClassV1. Positions are intentionally packet-local diagnostics. Metal
// packets and skeletal bind/world transforms are outside this V1 contract.
[[nodiscard]] RacMobyPacketGeometryV1 parse_rac_moby_packet_geometry_v1(
    std::span<const std::byte> class_bytes, const RacMobyPacketV1 &packet,
    float class_scale, RacMobyPacketGeometryLimitsV1 limits);

} // namespace openrc
