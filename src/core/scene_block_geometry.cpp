#include "openrc/scene_block_geometry.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

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
  if (word.bits >= kSceneBlockTaskInputBankQwords) {
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

[[nodiscard]] bool exact_index_stream_matches(
    const DvpVuExecutionStateV1 &input_state,
    const SceneBlockVuSnapshotV1 &snapshot,
    const std::uint64_t command_index,
    const std::uint16_t index_first_qword,
    const GifGsDecodeResultV1 &gs) {
  for (std::size_t vertex_index = 0U; vertex_index < gs.vertices.size();
       ++vertex_index) {
    const auto index_qword_wide =
        static_cast<std::uint64_t>(index_first_qword) +
        static_cast<std::uint64_t>(vertex_index / kDvpVuLaneCount);
    if (index_qword_wide >= kSceneBlockTaskInputBankQwords) {
      return false;
    }
    const auto index_qword = static_cast<std::uint16_t>(index_qword_wide);
    const auto index_write = snapshot.memory[index_qword].last_write_index;
    if (!index_write || *index_write >= snapshot.writes.size()) {
      return false;
    }
    const auto &write = snapshot.writes[static_cast<std::size_t>(*index_write)];
    const auto expected_output_vector = static_cast<std::uint16_t>(
        vertex_index / kDvpVuLaneCount);
    if (write.destination_qword != index_qword ||
        write.command_index != command_index ||
        write.output_vector_index != expected_output_vector) {
      return false;
    }

    const auto index_lane = vertex_index % kDvpVuLaneCount;
    const auto &descriptor_word =
        input_state.data_memory[index_qword].lanes[index_lane];
    if (descriptor_word.known_mask != kFullyKnown ||
        descriptor_word.bits >= kSceneBlockTaskInputBankQwords) {
      return false;
    }
    const auto descriptor_qword =
        static_cast<std::uint16_t>(descriptor_word.bits);
    const auto &position_word = input_state.data_memory[descriptor_qword]
                                    .lanes[kSceneBlockSourcePositionPointerLaneV1];
    if (position_word.known_mask != kFullyKnown ||
        position_word.bits >= kSceneBlockTaskInputBankQwords - 1U) {
      return false;
    }
    const auto position_qword =
        static_cast<std::uint16_t>(position_word.bits);
    const auto &position = input_state.data_memory[position_qword];
    for (std::size_t lane = 0U; lane < 3U; ++lane) {
      if (position.lanes[lane].known_mask != kFullyKnown) {
        return false;
      }
    }

    const auto &decoded = gs.vertices[vertex_index];
    if (!decoded.color.r || !decoded.color.g || !decoded.color.b ||
        !decoded.color.a) {
      return false;
    }
    const std::array<std::uint8_t, 4U> expected{
        *decoded.color.r, *decoded.color.g, *decoded.color.b, *decoded.color.a};
    const auto &color = input_state.data_memory[position_qword + 1U];
    for (std::size_t lane = 0U; lane < expected.size(); ++lane) {
      if (color.lanes[lane].known_mask != kFullyKnown ||
          color.lanes[lane].bits > std::numeric_limits<std::uint8_t>::max() ||
          color.lanes[lane].bits != expected[lane]) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] std::uint16_t find_exact_index_stream(
    const DvpVuExecutionStateV1 &input_state,
    const SceneBlockVuSnapshotV1 &snapshot,
    const GifGsDecodeResultV1 &gs) {
  const auto required_qwords =
      (static_cast<std::uint64_t>(gs.vertices.size()) - 1U) /
          kDvpVuLaneCount +
      1U;
  if (required_qwords > kSceneBlockTaskInputBankQwords) {
    fail("SceneBlock source geometry index stream exceeds the first input "
         "bank");
  }

  std::vector<const SceneBlockVuVectorWriteV1 *> first_output_writes(
      snapshot.stream.commands.size(), nullptr);
  std::vector<bool> duplicate_first_output_writes(
      snapshot.stream.commands.size(), false);
  for (const auto &write : snapshot.writes) {
    if (write.command_index >= first_output_writes.size() ||
        write.output_vector_index != 0U) {
      continue;
    }
    const auto command_offset =
        static_cast<std::size_t>(write.command_index);
    auto &first_write = first_output_writes[command_offset];
    if (first_write != nullptr) {
      duplicate_first_output_writes[command_offset] = true;
      continue;
    }
    first_write = &write;
  }

  std::optional<std::uint16_t> match;
  for (std::size_t command_index = 0U;
       command_index < snapshot.stream.commands.size(); ++command_index) {
    const auto &command = snapshot.stream.commands[command_index];
    if (command.opcode != SceneBlockVifOpcode::unpack_v4_8 ||
        !command.unsigned_data || !command.use_tops ||
        command.output_vector_count != required_qwords) {
      continue;
    }

    const auto *const first_write = first_output_writes[command_index];
    if (first_write == nullptr ||
        duplicate_first_output_writes[command_index]) {
      continue;
    }
    const auto base = first_write->destination_qword;
    if (static_cast<std::uint64_t>(base) + required_qwords >
        kSceneBlockTaskInputBankQwords) {
      continue;
    }
    if (!exact_index_stream_matches(input_state, snapshot, command_index, base,
                                    gs)) {
      continue;
    }
    if (match && *match != base) {
      fail("SceneBlock source geometry has multiple exact index streams");
    }
    match = base;
  }
  if (!match) {
    fail("SceneBlock source geometry has no exact V4-8 index stream");
  }
  return *match;
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
  std::array<bool, kSceneBlockTaskInputBankQwords> seen_descriptors{};
  std::array<bool, kSceneBlockTaskInputBankQwords> seen_positions{};
  const auto index_first_qword =
      find_exact_index_stream(input_state, record.vif_execution, gs);
  result.descriptor_index_first_qword = index_first_qword;

  for (std::size_t vertex_index = 0U; vertex_index < gs.vertices.size();
       ++vertex_index) {
    const auto index_qword_wide =
        static_cast<std::uint64_t>(index_first_qword) +
        static_cast<std::uint64_t>(vertex_index / kDvpVuLaneCount);
    if (index_qword_wide >= kSceneBlockTaskInputBankQwords) {
      fail("SceneBlock source geometry index stream exceeds the first input "
           "bank");
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
    if (position_qword + 1U >= kSceneBlockTaskInputBankQwords) {
      fail("SceneBlock source geometry color qword exceeds the first input "
           "bank");
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
