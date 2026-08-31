#include "openrc/rac_moby_model_geometry.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RacMobyModelGeometryError(message);
}

void require_limit(const std::uint64_t value, const std::uint64_t limit,
                   const char *const description) {
  if (value > limit) {
    fail(std::string(description) + " exceeds the caller's limit");
  }
}

void require_append_capacity(const std::uint64_t current,
                             const std::uint64_t additional,
                             const std::uint64_t limit,
                             const std::size_t host_limit,
                             const char *const description) {
  if (current > limit || additional > limit - current ||
      current > static_cast<std::uint64_t>(host_limit) ||
      additional > static_cast<std::uint64_t>(host_limit) - current ||
      current > UINT32_MAX || additional > UINT32_MAX - current) {
    fail(std::string(description) + " exceeds its bounded output capacity");
  }
}

[[nodiscard]] RacMobyPacketKindV1 packet_kind_for_lod(
    const RacMobyLodV1 lod) noexcept {
  return lod == RacMobyLodV1::high ? RacMobyPacketKindV1::high_lod
                                   : RacMobyPacketKindV1::low_lod;
}

[[nodiscard]] std::array<float, 3U>
decode_normal(const RacMobyPacketLocalVertexV1 &vertex) {
  const auto azimuth = static_cast<float>(vertex.normal_azimuth) *
                       (std::numbers::pi_v<float> / 128.0F);
  const auto elevation = static_cast<float>(vertex.normal_elevation) *
                         (std::numbers::pi_v<float> / 128.0F);
  const auto cos_elevation = std::cos(elevation);
  return {std::sin(azimuth) * cos_elevation,
          std::cos(azimuth) * cos_elevation, std::sin(elevation)};
}

} // namespace

RacMobyModelGeometryV1 assemble_rac_moby_model_geometry_v1(
    const std::span<const std::byte> class_bytes, const RacMobyClassV1 &moby,
    const RacMobyLodV1 lod, const RacMobyModelGeometryLimitsV1 limits) {
  const auto &packet_limits = limits.packet_limits;
  if (limits.max_packets == 0U || limits.max_output_vertices == 0U ||
      limits.max_output_triangles == 0U ||
      packet_limits.max_input_bytes == 0U ||
      packet_limits.max_vif_commands == 0U ||
      packet_limits.max_matrix_transfers == 0U ||
      packet_limits.max_vertices == 0U ||
      packet_limits.max_strip_indices == 0U ||
      packet_limits.max_texture_primitives == 0U ||
      packet_limits.max_triangles == 0U) {
    fail("RacMobyModelGeometryV1 caller limits must be non-zero");
  }
  if (class_bytes.size() != moby.input_bytes) {
    fail("RacMobyModelGeometryV1 input size disagrees with RacMobyClassV1");
  }
  require_limit(class_bytes.size(), packet_limits.max_input_bytes,
                "The RAC1 Moby model input size");

  RacMobyModelGeometryV1 result;
  result.input_bytes = class_bytes.size();
  result.lod = lod;
  result.requires_bind_transforms = moby.joint_count != 0U;

  const auto selected_kind = packet_kind_for_lod(lod);
  std::uint64_t selected_packet_count = 0U;
  for (const auto &packet : moby.packets) {
    if (packet.kind == selected_kind) {
      ++selected_packet_count;
    }
  }
  const auto declared_packet_count =
      lod == RacMobyLodV1::high ? moby.high_lod_packet_count
                                : moby.low_lod_packet_count;
  if (selected_packet_count != declared_packet_count) {
    fail("RacMobyModelGeometryV1 packet kinds disagree with RacMobyClassV1");
  }
  require_limit(selected_packet_count, limits.max_packets,
                "The RAC1 Moby model packet count");
  if (selected_packet_count > result.packets.max_size()) {
    fail("The RAC1 Moby model packet count exceeds the host container");
  }
  result.packets.reserve(static_cast<std::size_t>(selected_packet_count));

  std::array<std::optional<std::uint32_t>, kRacMobyVertexCacheEntriesV1>
      vertex_cache{};
  std::int32_t current_texture_index = 0;

  for (std::size_t class_packet_index = 0U;
       class_packet_index < moby.packets.size(); ++class_packet_index) {
    const auto &packet = moby.packets[class_packet_index];
    if (packet.kind != selected_kind) {
      continue;
    }
    if (class_packet_index > UINT32_MAX) {
      fail("A RAC1 Moby class packet index exceeds the V1 output domain");
    }

    RacMobyPacketGeometryV1 geometry;
    try {
      geometry = parse_rac_moby_packet_geometry_v1(
          class_bytes, packet, moby.scale, limits.packet_limits);
    } catch (const RacMobyPacketGeometryError &error) {
      fail("RAC1 Moby packet " + std::to_string(class_packet_index) +
           " failed geometry reconstruction: " + error.what());
    }

    const auto packet_vertex_count = geometry.vertex_header.transfer_vertex_count;
    require_append_capacity(result.vertices.size(), packet_vertex_count,
                            limits.max_output_vertices,
                            result.vertices.max_size(),
                            "The RAC1 Moby model vertex count");
    require_append_capacity(result.triangles.size(), geometry.triangles.size(),
                            limits.max_output_triangles,
                            result.triangles.max_size(),
                            "The RAC1 Moby model triangle count");

    const auto packet_vertex_begin =
        static_cast<std::uint32_t>(result.vertices.size());
    const auto packet_triangle_begin =
        static_cast<std::uint32_t>(result.triangles.size());
    const auto packet_index = static_cast<std::uint32_t>(class_packet_index);
    std::vector<std::uint32_t> transfer_vertices;
    transfer_vertices.reserve(packet_vertex_count);

    for (std::size_t local_index = 0U;
         local_index < geometry.vertices.size(); ++local_index) {
      const auto &source = geometry.vertices[local_index];
      const auto output_index = static_cast<std::uint32_t>(result.vertices.size());
      const auto transfer_index = static_cast<std::uint32_t>(local_index);
      const auto &st = geometry.texture_coordinates[local_index];
      result.vertices.push_back(RacMobyModelVertexV1{
          source.diagnostic_position,
          decode_normal(source),
          {st.s, st.t},
          source.vertex_cache_index,
          packet_index,
          transfer_index,
          packet_index,
          static_cast<std::uint32_t>(local_index),
          false,
          source.source_range,
          source.source_range});
      transfer_vertices.push_back(output_index);
      vertex_cache[source.vertex_cache_index] = output_index;
    }

    std::uint32_t inherited_duplicate_count = 0U;
    for (std::size_t duplicate_index = 0U;
         duplicate_index < geometry.duplicate_vertices.size();
         ++duplicate_index) {
      const auto &duplicate = geometry.duplicate_vertices[duplicate_index];
      const auto cached = vertex_cache[duplicate.vertex_cache_index];
      if (!cached || *cached >= result.vertices.size()) {
        fail("RAC1 Moby packet " + std::to_string(class_packet_index) +
             " references an unavailable inherited vertex-cache entry " +
             std::to_string(duplicate.vertex_cache_index));
      }
      const auto transfer_index = static_cast<std::uint32_t>(
          geometry.vertices.size() + duplicate_index);
      const auto &st = geometry.texture_coordinates[transfer_index];
      auto output = result.vertices[*cached];
      output.texture_coordinate = {st.s, st.t};
      output.class_packet_index = packet_index;
      output.transfer_vertex_index = transfer_index;
      output.duplicate = true;
      output.transfer_source_range = duplicate.source_range;
      const auto output_index = static_cast<std::uint32_t>(result.vertices.size());
      result.vertices.push_back(output);
      transfer_vertices.push_back(output_index);

      if (!duplicate.packet_local_source_vertex) {
        ++inherited_duplicate_count;
        ++result.inherited_duplicate_count;
      } else {
        const auto expected = static_cast<std::uint64_t>(packet_vertex_begin) +
                              *duplicate.packet_local_source_vertex;
        if (expected != *cached) {
          fail("A RAC1 Moby packet-local duplicate disagrees with its cache state");
        }
      }
    }
    if (transfer_vertices.size() != packet_vertex_count) {
      fail("A RAC1 Moby packet produced an incomplete transfer-vertex table");
    }

    const auto entry_texture_index = current_texture_index;
    for (const auto &triangle : geometry.triangles) {
      RacMobyModelTriangleV1 output;
      output.class_packet_index = packet_index;
      output.texture_index =
          triangle.texture_index.value_or(entry_texture_index);
      for (std::size_t corner = 0U; corner < output.vertex_indices.size();
           ++corner) {
        const auto transfer_index = triangle.transfer_vertex_indices[corner];
        if (transfer_index >= transfer_vertices.size()) {
          fail("A RAC1 Moby triangle exceeds the assembled transfer table");
        }
        output.vertex_indices[corner] = transfer_vertices[transfer_index];
      }
      result.triangles.push_back(output);
    }

    if (geometry.consumed_texture_primitive_count != 0U) {
      if (geometry.consumed_texture_primitive_count >
          geometry.texture_primitives.size()) {
        fail("A RAC1 Moby packet consumed an unavailable texture primitive");
      }
      current_texture_index =
          geometry
              .texture_primitives[static_cast<std::size_t>(
                  geometry.consumed_texture_primitive_count - 1U)]
              .texture_index;
    }

    result.packets.push_back(RacMobyModelPacketV1{
        packet_index,
        packet.kind,
        packet_vertex_begin,
        packet_vertex_count,
        packet_triangle_begin,
        static_cast<std::uint32_t>(geometry.triangles.size()),
        static_cast<std::uint32_t>(geometry.vertices.size()),
        static_cast<std::uint32_t>(geometry.duplicate_vertices.size()),
        inherited_duplicate_count,
        entry_texture_index,
        current_texture_index});
  }

  result.final_texture_index = current_texture_index;
  return result;
}

} // namespace openrc
