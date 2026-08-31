#include "openrc/rac_moby_model_geometry.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kFirstVifOffset = 0x000U;
constexpr std::uint32_t kFirstVifBytes = 0x070U;
constexpr std::uint32_t kFirstVertexOffset = 0x070U;
constexpr std::uint32_t kFirstVertexBytes = 0x090U;
constexpr std::uint32_t kSecondVifOffset = 0x100U;
constexpr std::uint32_t kSecondVifBytes = 0x030U;
constexpr std::uint32_t kSecondVertexOffset = 0x130U;
constexpr std::uint32_t kSecondVertexBytes = 0x0a0U;
constexpr std::uint32_t kFixtureBytes = 0x1d0U;

constexpr openrc::RacMobyPacketGeometryLimitsV1 kPacketLimits{
    0x10000U, 64U, 64U, 256U, 256U, 64U, 1024U};
constexpr openrc::RacMobyModelGeometryLimitsV1 kLimits{
    kPacketLimits, 16U, 1024U, 4096U};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void write_le16(std::vector<std::byte> &bytes, const std::size_t offset,
                const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_le_s16(std::vector<std::byte> &bytes, const std::size_t offset,
                  const std::int16_t value) {
  write_le16(bytes, offset, std::bit_cast<std::uint16_t>(value));
}

void write_le32(std::vector<std::byte> &bytes, const std::size_t offset,
                const std::uint32_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
  bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
  bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

[[nodiscard]] std::uint32_t vif_code(const std::uint8_t opcode,
                                     const std::uint8_t vector_count,
                                     const std::uint16_t destination) {
  return (static_cast<std::uint32_t>(opcode) << 24U) |
         (static_cast<std::uint32_t>(vector_count) << 16U) |
         UINT32_C(0x8000) | destination;
}

void write_vertex_record(std::vector<std::byte> &bytes,
                         const std::size_t offset, const std::int16_t x,
                         const std::int16_t y, const std::int16_t z,
                         const std::uint8_t azimuth = 0U,
                         const std::uint8_t elevation = 0U) {
  bytes[offset + 0x08U] = static_cast<std::byte>(azimuth);
  bytes[offset + 0x09U] = static_cast<std::byte>(elevation);
  write_le_s16(bytes, offset + 0x0aU, x);
  write_le_s16(bytes, offset + 0x0cU, y);
  write_le_s16(bytes, offset + 0x0eU, z);
}

void write_first_packet(std::vector<std::byte> &bytes) {
  const auto vif = static_cast<std::size_t>(kFirstVifOffset);
  write_le32(bytes, vif + 0x00U, vif_code(0x75U, 3U, 0x00c2U));
  write_le_s16(bytes, vif + 0x04U, 0);
  write_le_s16(bytes, vif + 0x06U, 0);
  write_le_s16(bytes, vif + 0x08U, 4096);
  write_le_s16(bytes, vif + 0x0aU, 0);
  write_le_s16(bytes, vif + 0x0cU, 0);
  write_le_s16(bytes, vif + 0x0eU, 4096);

  write_le32(bytes, vif + 0x10U, vif_code(0x6eU, 3U, 0x012dU));
  bytes[vif + 0x14U] = std::byte{0xff};
  bytes[vif + 0x15U] = std::byte{3};
  bytes[vif + 0x16U] = std::byte{0x81};
  const std::array<std::uint8_t, 8U> indices{
      0x00U, 0x82U, 0x03U, 0x01U, 0x01U, 0x01U, 0x00U, 0x00U};
  for (std::size_t index = 0U; index < indices.size(); ++index) {
    bytes[vif + 0x18U + index] = static_cast<std::byte>(indices[index]);
  }

  write_le32(bytes, vif + 0x20U, vif_code(0x6cU, 4U, 0x0130U));
  write_le32(bytes, vif + 0x44U, 7U);

  const auto vertex = static_cast<std::size_t>(kFirstVertexOffset);
  write_le32(bytes, vertex + 0x0cU, 3U);
  write_le32(bytes, vertex + 0x14U, 3U);
  write_le32(bytes, vertex + 0x18U, 0x20U);
  write_le32(bytes, vertex + 0x1cU, kFirstVertexBytes);
  write_vertex_record(bytes, vertex + 0x20U, 0, 0, 0);
  write_vertex_record(bytes, vertex + 0x30U, 512, 0, 0, 64U, 0U);
  write_vertex_record(bytes, vertex + 0x40U, 0, 512, 0, 0U, 64U);
  write_le16(bytes, vertex + 0x84U, 5U);
  write_le16(bytes, vertex + 0x86U, 6U);
  write_le16(bytes, vertex + 0x88U, 7U);
}

void write_second_packet(std::vector<std::byte> &bytes) {
  const auto vif = static_cast<std::size_t>(kSecondVifOffset);
  write_le32(bytes, vif + 0x00U, vif_code(0x75U, 4U, 0x00c2U));
  write_le_s16(bytes, vif + 0x04U, 0);
  write_le_s16(bytes, vif + 0x06U, 0);
  write_le_s16(bytes, vif + 0x08U, 4096);
  write_le_s16(bytes, vif + 0x0aU, 0);
  write_le_s16(bytes, vif + 0x0cU, 0);
  write_le_s16(bytes, vif + 0x0eU, 4096);
  write_le_s16(bytes, vif + 0x10U, 2048);
  write_le_s16(bytes, vif + 0x12U, 1024);

  write_le32(bytes, vif + 0x14U, vif_code(0x6eU, 3U, 0x012dU));
  bytes[vif + 0x18U] = std::byte{0xff};
  const std::array<std::uint8_t, 8U> indices{
      0x81U, 0x82U, 0x04U, 0x01U, 0x01U, 0x01U, 0x00U, 0x00U};
  for (std::size_t index = 0U; index < indices.size(); ++index) {
    bytes[vif + 0x1cU + index] = static_cast<std::byte>(indices[index]);
  }

  const auto vertex = static_cast<std::size_t>(kSecondVertexOffset);
  write_le32(bytes, vertex + 0x0cU, 3U);
  write_le32(bytes, vertex + 0x10U, 1U);
  write_le32(bytes, vertex + 0x14U, 4U);
  write_le32(bytes, vertex + 0x18U, 0x30U);
  write_le32(bytes, vertex + 0x1cU, kSecondVertexBytes);
  write_le16(bytes, vertex + 0x20U, 5U << 7U);
  write_vertex_record(bytes, vertex + 0x30U, 0, 0, 512);
  write_vertex_record(bytes, vertex + 0x40U, 512, 0, 512);
  write_vertex_record(bytes, vertex + 0x50U, 0, 512, 512);
  write_le16(bytes, vertex + 0x94U, 8U);
  write_le16(bytes, vertex + 0x96U, 9U);
  write_le16(bytes, vertex + 0x98U, 10U);
}

[[nodiscard]] std::vector<std::byte> make_fixture_bytes() {
  std::vector<std::byte> bytes(kFixtureBytes, std::byte{0});
  write_first_packet(bytes);
  write_second_packet(bytes);
  return bytes;
}

[[nodiscard]] openrc::RacMobyClassV1 make_fixture_class() {
  openrc::RacMobyClassV1 moby;
  moby.input_bytes = kFixtureBytes;
  moby.high_lod_packet_count = 2U;
  moby.scale = 2.0F;
  moby.packets = {
      {openrc::RacMobyPacketKindV1::high_lod,
       {},
       {kFirstVifOffset, kFirstVifBytes},
       {kFirstVertexOffset, kFirstVertexBytes},
       4U,
       9U,
       3U},
      {openrc::RacMobyPacketKindV1::high_lod,
       {},
       {kSecondVifOffset, kSecondVifBytes},
       {kSecondVertexOffset, kSecondVertexBytes},
       0U,
       10U,
       4U}};
  return moby;
}

template <typename Mutation>
void expect_rejected(Mutation &&mutation, const std::string &message) {
  auto bytes = make_fixture_bytes();
  auto moby = make_fixture_class();
  auto limits = kLimits;
  std::invoke(std::forward<Mutation>(mutation), bytes, moby, limits);
  try {
    (void)openrc::assemble_rac_moby_model_geometry_v1(
        bytes, moby, openrc::RacMobyLodV1::high, limits);
  } catch (const openrc::RacMobyModelGeometryError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_cross_packet_cache_and_texture_state() {
  const auto bytes = make_fixture_bytes();
  const auto moby = make_fixture_class();
  const auto result = openrc::assemble_rac_moby_model_geometry_v1(
      bytes, moby, openrc::RacMobyLodV1::high, kLimits);

  expect(result.input_bytes == kFixtureBytes &&
             result.lod == openrc::RacMobyLodV1::high &&
             !result.requires_bind_transforms && result.packets.size() == 2U &&
             result.vertices.size() == 7U && result.triangles.size() == 2U &&
             result.inherited_duplicate_count == 1U &&
             result.final_texture_index == 7,
         "the assembled RAC1 Moby model summary is wrong");
  expect(result.packets[0U].entry_texture_index == 0 &&
             result.packets[0U].final_texture_index == 7 &&
             result.packets[1U].entry_texture_index == 7 &&
             result.packets[1U].final_texture_index == 7 &&
             result.packets[1U].inherited_duplicate_count == 1U,
         "RAC1 Moby cross-packet texture/cache state was not retained");
  expect(result.triangles[0U].texture_index == 7 &&
             result.triangles[1U].texture_index == 7 &&
             result.triangles[1U].vertex_indices ==
                 std::array<std::uint32_t, 3U>{4U, 3U, 6U},
         "RAC1 Moby model triangles or inherited material are wrong");

  const auto &duplicate = result.vertices[6U];
  expect(duplicate.duplicate && duplicate.class_packet_index == 1U &&
             duplicate.transfer_vertex_index == 3U &&
             duplicate.source_class_packet_index == 0U &&
             duplicate.source_vertex_index == 0U &&
             duplicate.vertex_cache_index == 5U &&
             duplicate.diagnostic_position ==
                 std::array<float, 3U>{0.0F, 0.0F, 0.0F} &&
             duplicate.diagnostic_normal ==
                 std::array<float, 3U>{0.0F, 1.0F, 0.0F} &&
             duplicate.texture_coordinate ==
                 std::array<float, 2U>{0.5F, 0.25F} &&
             duplicate.position_source_range ==
                 openrc::RacMobyPacketGeometryRangeV1{
                     kFirstVertexOffset + 0x20U, 0x10U} &&
             duplicate.transfer_source_range ==
                 openrc::RacMobyPacketGeometryRangeV1{
                     kSecondVertexOffset + 0x20U, 0x02U},
         "the inherited RAC1 Moby duplicate lost geometry, UV, or provenance");
}

void test_vertex_cache_last_write_wins() {
  auto bytes = make_fixture_bytes();
  write_le16(bytes, kFirstVertexOffset + 0x86U, 5U);
  const auto moby = make_fixture_class();
  const auto result = openrc::assemble_rac_moby_model_geometry_v1(
      bytes, moby, openrc::RacMobyLodV1::high, kLimits);
  const auto &duplicate = result.vertices[6U];
  expect(duplicate.source_class_packet_index == 0U &&
             duplicate.source_vertex_index == 1U &&
             duplicate.diagnostic_position ==
                 std::array<float, 3U>{1.0F, 0.0F, 0.0F},
         "a repeated RAC1 Moby cache write did not retain the last source");
}

void test_lod_cache_is_isolated() {
  const auto bytes = make_fixture_bytes();
  const auto moby = make_fixture_class();
  const auto low = openrc::assemble_rac_moby_model_geometry_v1(
      bytes, moby, openrc::RacMobyLodV1::low, kLimits);
  expect(low.packets.empty() && low.vertices.empty() && low.triangles.empty() &&
             low.final_texture_index == 0,
         "an absent low LOD inherited high-LOD state");

  auto low_moby = make_fixture_class();
  low_moby.high_lod_packet_count = 0U;
  low_moby.low_lod_packet_count = 2U;
  for (auto &packet : low_moby.packets) {
    packet.kind = openrc::RacMobyPacketKindV1::low_lod;
  }
  const auto assembled_low = openrc::assemble_rac_moby_model_geometry_v1(
      bytes, low_moby, openrc::RacMobyLodV1::low, kLimits);
  expect(assembled_low.packets.size() == 2U &&
             assembled_low.vertices.size() == 7U &&
             assembled_low.triangles.size() == 2U &&
             assembled_low.inherited_duplicate_count == 1U,
         "a populated RAC1 Moby low LOD was not assembled independently");

  auto split_moby = make_fixture_class();
  split_moby.high_lod_packet_count = 1U;
  split_moby.low_lod_packet_count = 1U;
  split_moby.packets[1U].kind = openrc::RacMobyPacketKindV1::low_lod;
  try {
    (void)openrc::assemble_rac_moby_model_geometry_v1(
        bytes, split_moby, openrc::RacMobyLodV1::low, kLimits);
    throw std::runtime_error(
        "a low LOD inherited the high-LOD vertex cache");
  } catch (const openrc::RacMobyModelGeometryError &) {
  }

  expect_rejected(
      [](auto &, auto &candidate, auto &) {
        candidate.packets.erase(candidate.packets.begin());
        candidate.high_lod_packet_count = 1U;
      },
      "a duplicate inherited from an absent packet/cache was accepted");
}

void test_limits_and_contract_validation() {
  expect_rejected(
      [](auto &, auto &, auto &limits) { limits = {}; },
      "zero model-geometry limits were accepted");
  expect_rejected(
      [](auto &, auto &, auto &limits) { limits.packet_limits = {}; },
      "zero packet-geometry limits were accepted by the model assembler");
  auto empty_lod_limits = kLimits;
  empty_lod_limits.packet_limits.max_input_bytes = kFixtureBytes - 1U;
  try {
    (void)openrc::assemble_rac_moby_model_geometry_v1(
        make_fixture_bytes(), make_fixture_class(),
        openrc::RacMobyLodV1::low, empty_lod_limits);
    throw std::runtime_error(
        "an empty LOD bypassed the RAC1 Moby input-size limit");
  } catch (const openrc::RacMobyModelGeometryError &) {
  }
  expect_rejected(
      [](auto &, auto &, auto &limits) { limits.max_packets = 1U; },
      "the RAC1 Moby model packet limit was ignored");
  expect_rejected(
      [](auto &, auto &, auto &limits) { limits.max_output_vertices = 6U; },
      "the RAC1 Moby model vertex limit was ignored");
  expect_rejected(
      [](auto &, auto &, auto &limits) { limits.max_output_triangles = 1U; },
      "the RAC1 Moby model triangle limit was ignored");
  expect_rejected(
      [](auto &, auto &candidate, auto &) {
        candidate.high_lod_packet_count = 1U;
      },
      "a mismatched declared RAC1 Moby LOD packet count was accepted");
  expect_rejected(
      [](auto &bytes, auto &, auto &) { bytes.push_back(std::byte{0}); },
      "a RAC1 Moby model input-size mismatch was accepted");
}

void test_animated_marker() {
  const auto bytes = make_fixture_bytes();
  auto moby = make_fixture_class();
  moby.joint_count = 3U;
  const auto result = openrc::assemble_rac_moby_model_geometry_v1(
      bytes, moby, openrc::RacMobyLodV1::high, kLimits);
  expect(result.requires_bind_transforms,
         "an animated RAC1 Moby model was presented as bind-ready");
}

} // namespace

int main() {
  try {
    test_cross_packet_cache_and_texture_state();
    test_vertex_cache_last_write_wins();
    test_lod_cache_is_isolated();
    test_limits_and_contract_validation();
    test_animated_marker();
    std::cout << "rac_moby_model_geometry_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rac_moby_model_geometry_tests: " << error.what() << '\n';
    return 1;
  }
}
