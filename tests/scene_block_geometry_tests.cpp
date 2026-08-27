#include "openrc/scene_block_geometry.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_source_geometry_error(Callback &&callback,
                                  const std::string &message) {
  try {
    callback();
  } catch (const openrc::SceneBlockSourceGeometryError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] std::uint32_t signed_bits(const std::int32_t value) {
  return std::bit_cast<std::uint32_t>(value);
}

void append_write(openrc::SceneBlockVuSnapshotV1 &snapshot,
                  const std::uint16_t qword,
                  const std::array<std::uint32_t, 4U> values) {
  openrc::SceneBlockVuVectorWriteV1 write;
  write.destination_qword = qword;
  for (std::size_t lane = 0U; lane < values.size(); ++lane) {
    write.lanes[lane].written_value = {
        openrc::SceneBlockVuValueState::known, values[lane]};
    snapshot.memory[qword].lanes[lane] = write.lanes[lane].written_value;
  }
  const auto write_index = static_cast<std::uint64_t>(snapshot.writes.size());
  snapshot.writes.push_back(write);
  snapshot.memory[qword].last_write_index = write_index;
  ++snapshot.memory[qword].write_count;
  ++snapshot.total_vector_writes;
}

void set_write_lane(openrc::SceneBlockVuSnapshotV1 &snapshot,
                    const std::uint16_t qword, const std::size_t lane,
                    const openrc::SceneBlockVuValueV1 value) {
  const auto write_index = snapshot.memory[qword].last_write_index;
  if (!write_index) {
    throw std::runtime_error("test fixture has no requested write");
  }
  snapshot.writes.at(static_cast<std::size_t>(*write_index))
      .lanes.at(lane)
      .written_value = value;
  snapshot.memory[qword].lanes.at(lane) = value;
}

[[nodiscard]] openrc::GifGsVertexV1
gs_vertex(const std::array<std::uint8_t, 4U> rgba) {
  openrc::GifGsVertexV1 result;
  result.color.r = rgba[0U];
  result.color.g = rgba[1U];
  result.color.b = rgba[2U];
  result.color.a = rgba[3U];
  return result;
}

struct Fixture {
  openrc::SceneBlockTaskExecutionStateV1 initialized;
  openrc::SceneBlockTaskRecordExecutionV1 record;
  openrc::GifGsDecodeResultV1 gs;
};

[[nodiscard]] Fixture make_fixture() {
  Fixture result;
  result.initialized.vu_state = openrc::make_dvp_vu_execution_state_v1();
  result.initialized.vif_state.tops_qword = 0U;
  result.initialized.base_qword = 0U;
  result.initialized.double_buffer = false;
  result.initialized.completed_record_invocations = 0U;

  result.record.invocation.entrypoint_address =
      openrc::kSceneBlockSourceGeometryEntrypointV1;
  result.record.top_qword = 0U;
  result.record.vif_execution.initial_state.tops_qword = 0U;

  append_write(result.record.vif_execution, 24U,
               {signed_bits(-10), signed_bits(20), signed_bits(-30), 999U});
  append_write(result.record.vif_execution, 25U, {1U, 2U, 3U, 4U});
  append_write(result.record.vif_execution, 26U,
               {signed_bits(100), signed_bits(200), signed_bits(300), 888U});
  append_write(result.record.vif_execution, 27U, {5U, 6U, 7U, 8U});

  append_write(result.record.vif_execution, 200U, {10U, 11U, 12U, 24U});
  append_write(result.record.vif_execution, 201U, {13U, 14U, 15U, 26U});
  append_write(result.record.vif_execution, 202U, {16U, 17U, 18U, 24U});
  append_write(result.record.vif_execution, 250U,
               {200U, 201U, 200U, 202U});

  result.gs.vertices = {
      gs_vertex({1U, 2U, 3U, 4U}),
      gs_vertex({5U, 6U, 7U, 8U}),
      gs_vertex({1U, 2U, 3U, 4U}),
      gs_vertex({1U, 2U, 3U, 4U}),
  };
  return result;
}

[[nodiscard]] openrc::SceneBlockSourceGeometryLimitsV1 limits() {
  return {64U, {64U}};
}

void test_reorder_duplicates_and_provenance_are_preserved() {
  const auto fixture = make_fixture();
  const auto geometry = openrc::recover_scene_block_source_geometry_v1(
      fixture.initialized, fixture.record, fixture.gs, limits());

  expect(geometry.vertices.size() == 4U,
         "source vertices did not retain GS order");
  expect(geometry.unique_descriptor_qword_count == 3U,
         "unique descriptor count is incorrect");
  expect(geometry.unique_position_qword_count == 2U,
         "duplicate position references were not retained");

  const auto &first = geometry.vertices[0U];
  expect(first.gs_vertex_index == 0U && first.x == -10 && first.y == 20 &&
             first.z == -30,
         "the first signed source position is incorrect");
  expect(first.rgba == std::array<std::uint8_t, 4U>{1U, 2U, 3U, 4U},
         "the first source color is incorrect");
  expect(first.descriptor_index_source.qword == 250U &&
             first.descriptor_index_source.lane == 0U &&
             first.descriptor_index_source.last_write_index == 7U,
         "descriptor-index provenance is incorrect");
  expect(first.position_pointer_source.qword == 200U &&
             first.position_pointer_source.lane == 3U &&
             first.position_pointer_source.last_write_index == 4U,
         "position-pointer provenance is incorrect");
  expect(first.position_source.qword == 24U &&
             first.position_source.last_write_index == 0U &&
             first.color_source.qword == 25U &&
             first.color_source.last_write_index == 1U,
         "position/color qword provenance is incorrect");

  expect(geometry.vertices[1U].position_source.qword == 26U &&
             geometry.vertices[1U].x == 100,
         "the reordered second source position is incorrect");
  expect(geometry.vertices[2U].position_source.qword == 24U &&
             geometry.vertices[2U].position_pointer_source.qword == 200U,
         "a repeated descriptor was not retained");
  expect(geometry.vertices[3U].position_source.qword == 24U &&
             geometry.vertices[3U].position_pointer_source.qword == 202U,
         "two descriptors sharing a position were collapsed");
}

void test_invalid_descriptor_and_position_references_are_rejected() {
  auto descriptor = make_fixture();
  set_write_lane(descriptor.record.vif_execution, 250U, 0U,
                 {openrc::SceneBlockVuValueState::known, 1024U});
  expect_source_geometry_error(
      [&] {
        static_cast<void>(openrc::recover_scene_block_source_geometry_v1(
            descriptor.initialized, descriptor.record, descriptor.gs,
            limits()));
      },
      "an out-of-range descriptor reference was accepted");

  auto position = make_fixture();
  set_write_lane(position.record.vif_execution, 200U, 3U,
                 {openrc::SceneBlockVuValueState::known, 1023U});
  expect_source_geometry_error(
      [&] {
        static_cast<void>(openrc::recover_scene_block_source_geometry_v1(
            position.initialized, position.record, position.gs, limits()));
      },
      "a position without an adjacent color qword was accepted");
}

void test_unknown_words_and_color_mismatches_are_rejected() {
  auto unknown = make_fixture();
  set_write_lane(unknown.record.vif_execution, 24U, 2U,
                 {openrc::SceneBlockVuValueState::indeterminate, 0U});
  expect_source_geometry_error(
      [&] {
        static_cast<void>(openrc::recover_scene_block_source_geometry_v1(
            unknown.initialized, unknown.record, unknown.gs, limits()));
      },
      "an indeterminate source position was accepted");

  auto color = make_fixture();
  color.gs.vertices[2U].color.r = 99U;
  expect_source_geometry_error(
      [&] {
        static_cast<void>(openrc::recover_scene_block_source_geometry_v1(
            color.initialized, color.record, color.gs, limits()));
      },
      "a source/GS color mismatch was accepted");
}

void test_unused_lanes_may_remain_indeterminate() {
  auto fixture = make_fixture();
  fixture.gs.vertices.resize(1U);
  set_write_lane(fixture.record.vif_execution, 250U, 1U,
                 {openrc::SceneBlockVuValueState::indeterminate, 0U});
  set_write_lane(fixture.record.vif_execution, 250U, 2U,
                 {openrc::SceneBlockVuValueState::indeterminate, 0U});
  set_write_lane(fixture.record.vif_execution, 250U, 3U,
                 {openrc::SceneBlockVuValueState::indeterminate, 0U});
  set_write_lane(fixture.record.vif_execution, 200U, 0U,
                 {openrc::SceneBlockVuValueState::indeterminate, 0U});
  set_write_lane(fixture.record.vif_execution, 200U, 1U,
                 {openrc::SceneBlockVuValueState::indeterminate, 0U});
  set_write_lane(fixture.record.vif_execution, 200U, 2U,
                 {openrc::SceneBlockVuValueState::indeterminate, 0U});
  set_write_lane(fixture.record.vif_execution, 24U, 3U,
                 {openrc::SceneBlockVuValueState::indeterminate, 0U});

  const auto geometry = openrc::recover_scene_block_source_geometry_v1(
      fixture.initialized, fixture.record, fixture.gs, limits());
  expect(geometry.vertices.size() == 1U &&
             geometry.vertices[0U].x == -10 &&
             geometry.vertices[0U].y == 20 &&
             geometry.vertices[0U].z == -30,
         "unused indeterminate lanes blocked recoverable source XYZ");
}

void test_profile_and_provenance_inconsistency_are_rejected() {
  auto profile = make_fixture();
  profile.record.invocation.entrypoint_address = 14U;
  expect_source_geometry_error(
      [&] {
        static_cast<void>(openrc::recover_scene_block_source_geometry_v1(
            profile.initialized, profile.record, profile.gs, limits()));
      },
      "a non-entry-16 record was accepted");

  auto provenance = make_fixture();
  provenance.record.vif_execution.memory[250U].last_write_index = 0U;
  expect_source_geometry_error(
      [&] {
        static_cast<void>(openrc::recover_scene_block_source_geometry_v1(
            provenance.initialized, provenance.record, provenance.gs,
            limits()));
      },
      "inconsistent write provenance was accepted");
}

} // namespace

int main() {
  try {
    test_reorder_duplicates_and_provenance_are_preserved();
    test_invalid_descriptor_and_position_references_are_rejected();
    test_unknown_words_and_color_mismatches_are_rejected();
    test_unused_lanes_may_remain_indeterminate();
    test_profile_and_provenance_inconsistency_are_rejected();
    std::cout << "scene block source geometry tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "scene block source geometry tests failed: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
