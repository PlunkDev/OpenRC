#pragma once

#include "openrc/rac_moby_class.hpp"
#include "openrc/rac_moby_packet_geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

enum class RacMobyLodV1 : std::uint8_t {
  high,
  low,
};

struct RacMobyModelGeometryLimitsV1 {
  RacMobyPacketGeometryLimitsV1 packet_limits;
  std::uint64_t max_packets = 0U;
  std::uint64_t max_output_vertices = 0U;
  std::uint64_t max_output_triangles = 0U;
};

struct RacMobyModelVertexV1 {
  std::array<float, 3U> diagnostic_position{};
  std::array<float, 3U> diagnostic_normal{};
  std::array<float, 2U> texture_coordinate{};
  std::uint16_t vertex_cache_index = 0U;

  // The transfer vertex belongs to class_packet_index. A duplicate retains
  // the earlier packet/local record that supplied its position and normal.
  std::uint32_t class_packet_index = 0U;
  std::uint32_t transfer_vertex_index = 0U;
  std::uint32_t source_class_packet_index = 0U;
  std::uint32_t source_vertex_index = 0U;
  bool duplicate = false;
  RacMobyPacketGeometryRangeV1 transfer_source_range;
  RacMobyPacketGeometryRangeV1 position_source_range;
};

struct RacMobyModelTriangleV1 {
  std::array<std::uint32_t, 3U> vertex_indices{};
  std::int32_t texture_index = 0;
  std::uint32_t class_packet_index = 0U;
};

struct RacMobyModelPacketV1 {
  std::uint32_t class_packet_index = 0U;
  RacMobyPacketKindV1 kind = RacMobyPacketKindV1::high_lod;
  std::uint32_t vertex_begin = 0U;
  std::uint32_t vertex_count = 0U;
  std::uint32_t triangle_begin = 0U;
  std::uint32_t triangle_count = 0U;
  std::uint32_t local_vertex_count = 0U;
  std::uint32_t duplicate_vertex_count = 0U;
  std::uint32_t inherited_duplicate_count = 0U;
  std::int32_t entry_texture_index = 0;
  std::int32_t final_texture_index = 0;
};

struct RacMobyModelGeometryV1 {
  std::uint64_t input_bytes = 0U;
  RacMobyLodV1 lod = RacMobyLodV1::high;
  bool requires_bind_transforms = false;
  std::uint64_t inherited_duplicate_count = 0U;
  std::int32_t final_texture_index = 0;
  std::vector<RacMobyModelPacketV1> packets;
  std::vector<RacMobyModelVertexV1> vertices;
  std::vector<RacMobyModelTriangleV1> triangles;
};

class RacMobyModelGeometryError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Reconstructs one complete regular high- or low-LOD mesh. The RAC1 VU1
// vertex cache and current texture are carried across packets, but reset for
// each call/LOD. Positions remain model-local diagnostics; animated classes
// are explicitly marked because skeletal bind transforms are not applied.
[[nodiscard]] RacMobyModelGeometryV1 assemble_rac_moby_model_geometry_v1(
    std::span<const std::byte> class_bytes, const RacMobyClassV1 &moby,
    RacMobyLodV1 lod, RacMobyModelGeometryLimitsV1 limits);

} // namespace openrc
