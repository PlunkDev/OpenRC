#include "openrc/scene_block_geometry.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint32_t kFullyKnown = 0xffffffffU;

[[noreturn]] void fail(const std::string &message) {
  throw SceneBlockSourceGeometryError(message);
}

void require_position_xyz_known(const DvpVuVectorV1 &qword) {
  for (std::size_t lane = 0U; lane < 3U; ++lane) {
    if (qword.lanes[lane].known_mask != kFullyKnown) {
      fail("SceneBlock source geometry has an indeterminate position XYZ");
    }
  }
}

[[nodiscard]] std::optional<std::uint64_t>
last_write_index(const SceneBlockVuSnapshotV1 &snapshot,
                 const std::uint16_t qword) {
  const auto result = snapshot.memory[qword].last_write_index;
  if (!result) {
    return std::nullopt;
  }
  if (*result >= snapshot.writes.size() ||
      snapshot.writes[static_cast<std::size_t>(*result)].destination_qword !=
          qword) {
    fail("SceneBlock source geometry has inconsistent VIF write provenance");
  }
  return result;
}

[[nodiscard]] std::uint16_t
known_qword_address(const DvpVuWordV1 &word, const char *const description) {
  if (word.known_mask != kFullyKnown) {
    fail(std::string("SceneBlock source geometry has an indeterminate ") +
         description);
  }
  if (word.bits >= kDvpVuDataMemoryQwordCount) {
    fail(std::string("SceneBlock source geometry has an out-of-range ") +
         description);
  }
  return static_cast<std::uint16_t>(word.bits);
}

[[nodiscard]] std::uint8_t known_color(const DvpVuWordV1 &word,
                                       const char *const description) {
  if (word.known_mask != kFullyKnown) {
    fail(std::string("SceneBlock source geometry has an indeterminate ") +
         description);
  }
  if (word.bits > std::numeric_limits<std::uint8_t>::max()) {
    fail(std::string("SceneBlock source geometry has an out-of-range ") +
         description);
  }
  return static_cast<std::uint8_t>(word.bits);
}

[[nodiscard]] std::array<std::uint8_t, 4U>
decoded_color(const GifGsVertexV1 &vertex) {
  if (!vertex.color.r || !vertex.color.g || !vertex.color.b ||
      !vertex.color.a) {
    fail("SceneBlock source geometry cannot match an indeterminate GS color");
  }
  return {*vertex.color.r, *vertex.color.g, *vertex.color.b, *vertex.color.a};
}

} // namespace

SceneBlockSourceGeometryV1 recover_scene_block_source_geometry_v1(
    const SceneBlockTaskExecutionStateV1 &initialized_state,
    const SceneBlockTaskRecordExecutionV1 &record,
    const GifGsDecodeResultV1 &gs,
    const SceneBlockSourceGeometryLimitsV1 limits) {
  if (limits.max_vertices == 0U || limits.bridge.max_replayed_writes == 0U) {
    fail("SceneBlock source geometry limits must be non-zero");
  }
  if (record.invocation.entrypoint_address !=
      kSceneBlockSourceGeometryEntrypointV1) {
    fail("SceneBlock source geometry requires entrypoint 16");
  }
  if (record.top_qword != 0U || initialized_state.base_qword != 0U ||
      initialized_state.double_buffer ||
      initialized_state.vif_state.tops_qword != 0U ||
      initialized_state.completed_record_invocations != 0U) {
    fail("SceneBlock source geometry V1 requires the initial TOP=0 input bank");
  }
  if (record.vif_execution.initial_state.tops_qword != record.top_qword) {
    fail("SceneBlock source geometry record TOP disagrees with its VIF state");
  }
  if (gs.vertices.empty()) {
    fail("SceneBlock source geometry requires at least one GS vertex");
  }
  if (gs.vertices.size() > limits.max_vertices) {
    fail("SceneBlock source geometry exceeds the caller's vertex limit");
  }

  auto input_state = initialized_state.vu_state;
  try {
    apply_scene_block_dvp_vu_writes_v1(
        input_state, record.vif_execution, 0U,
        static_cast<std::uint64_t>(record.vif_execution.writes.size()),
        limits.bridge);
  } catch (const DvpVuExecutionError &error) {
    fail("Cannot reconstruct SceneBlock source geometry input RAM: " +
         std::string(error.what()));
  }

  SceneBlockSourceGeometryV1 result;
  if (gs.vertices.size() > result.vertices.max_size()) {
    fail("SceneBlock source geometry exceeds the host vertex container");
  }
  result.vertices.reserve(gs.vertices.size());
  std::array<bool, kDvpVuDataMemoryQwordCount> seen_descriptors{};
  std::array<bool, kDvpVuDataMemoryQwordCount> seen_positions{};

  for (std::size_t vertex_index = 0U; vertex_index < gs.vertices.size();
       ++vertex_index) {
    const auto index_qword_wide =
        static_cast<std::uint64_t>(kSceneBlockSourceIndexFirstQwordV1) +
        static_cast<std::uint64_t>(vertex_index / kDvpVuLaneCount);
    if (index_qword_wide >= kDvpVuDataMemoryQwordCount) {
      fail("SceneBlock source geometry index stream exceeds VU1 memory");
    }
    const auto index_qword = static_cast<std::uint16_t>(index_qword_wide);
    const auto index_lane =
        static_cast<std::uint8_t>(vertex_index % kDvpVuLaneCount);
    const auto &index_vector = input_state.data_memory[index_qword];
    const auto descriptor_qword = known_qword_address(
        index_vector.lanes[index_lane], "descriptor qword reference");

    const auto &descriptor = input_state.data_memory[descriptor_qword];
    const auto position_qword = known_qword_address(
        descriptor.lanes[kSceneBlockSourcePositionPointerLaneV1],
        "position qword reference");
    if (position_qword + 1U >= kDvpVuDataMemoryQwordCount) {
      fail("SceneBlock source geometry color qword exceeds VU1 memory");
    }
    const auto color_qword = static_cast<std::uint16_t>(position_qword + 1U);

    const auto &position = input_state.data_memory[position_qword];
    const auto &color = input_state.data_memory[color_qword];
    require_position_xyz_known(position);

    SceneBlockSourceVertexV1 vertex;
    vertex.gs_vertex_index = static_cast<std::uint64_t>(vertex_index);
    vertex.x = std::bit_cast<std::int32_t>(position.lanes[0U].bits);
    vertex.y = std::bit_cast<std::int32_t>(position.lanes[1U].bits);
    vertex.z = std::bit_cast<std::int32_t>(position.lanes[2U].bits);
    for (std::size_t lane = 0U; lane < vertex.rgba.size(); ++lane) {
      vertex.rgba[lane] = known_color(color.lanes[lane], "source color lane");
    }
    if (vertex.rgba != decoded_color(gs.vertices[vertex_index])) {
      fail("SceneBlock source color disagrees with its decoded GS vertex");
    }

    const auto index_write = last_write_index(record.vif_execution,
                                              index_qword);
    const auto descriptor_write = last_write_index(record.vif_execution,
                                                   descriptor_qword);
    const auto position_write = last_write_index(record.vif_execution,
                                                 position_qword);
    const auto color_write = last_write_index(record.vif_execution,
                                              color_qword);
    vertex.descriptor_index_source =
        {index_qword, index_lane, index_write};
    vertex.position_pointer_source = {
        descriptor_qword, kSceneBlockSourcePositionPointerLaneV1,
        descriptor_write};
    vertex.position_source = {position_qword, position_write};
    vertex.color_source = {color_qword, color_write};

    if (!seen_descriptors[descriptor_qword]) {
      seen_descriptors[descriptor_qword] = true;
      ++result.unique_descriptor_qword_count;
    }
    if (!seen_positions[position_qword]) {
      seen_positions[position_qword] = true;
      ++result.unique_position_qword_count;
    }
    result.vertices.push_back(std::move(vertex));
  }

  return result;
}

} // namespace openrc
