#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {
struct RacShrubClassLimitsV1 {
  std::uint64_t max_input_bytes=16U*1024U*1024U;
  std::uint32_t max_packets=4096, max_vertices=1'000'000, max_triangles=1'000'000;
};
struct RacShrubVertexV1 {
  std::array<std::int16_t,3> position_words{};
  std::array<std::int16_t,3> sth_words{};
  std::uint16_t normal_and_stop=0;
  std::uint64_t position_source_offset=0, attributes_source_offset=0;
  bool operator==(const RacShrubVertexV1&) const = default;
};
struct RacShrubMaterialV1 {
  std::uint32_t local_texture_index=0;
  std::uint64_t tex1=0, clamp=0, miptbp1=0, tex0=0;
  bool operator==(const RacShrubMaterialV1&) const = default;
};
struct RacShrubPrimitiveV1 {
  std::uint32_t packet_index=0, material_index=0;
  std::uint64_t tag_low=0, tag_high=0;
  std::vector<std::uint32_t> vertex_indices;
  std::vector<std::array<std::uint32_t,3>> triangles;
};
struct RacShrubClassV1 {
  std::array<std::uint32_t,4> bounding_sphere_bits{};
  std::uint32_t scale_bits=0, mip_distance_bits=0;
  std::uint16_t class_id=0, mode_bits=0;
  std::array<std::array<std::int16_t,4>,24> normals{};
  std::vector<std::byte> billboard_source; // original optional64-byte descriptor
  std::vector<RacShrubVertexV1> vertices;
  std::vector<RacShrubMaterialV1> materials;
  std::vector<RacShrubPrimitiveV1> primitives;
  std::uint32_t packet_count=0, duplicate_padding_writes=0;
};
class RacShrubClassError final: public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
// Compiler-side source representation. Uses the existing bounded VIF parser,
// then assembles ordered GS writes from their stored destination offsets.
// Source local texture slots, normals, flags and billboard records remain
// explicit: this parser does not invent lighting, alpha state or LOD policy.
[[nodiscard]] RacShrubClassV1 parse_rac_shrub_class_v1(
    std::span<const std::byte> bytes,RacShrubClassLimitsV1 limits={});
} // namespace openrc
