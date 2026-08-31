#include "openrc/rac_moby_packet_geometry.hpp"

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

constexpr std::uint32_t kVifBytes = 0x70U;
constexpr std::uint32_t kVertexBytes = 0x90U;
constexpr std::uint32_t kVertexOffset = kVifBytes;
constexpr std::uint32_t kFixtureBytes = kVifBytes + kVertexBytes;
constexpr openrc::RacMobyPacketGeometryLimitsV1 kLimits{
    0x10000U, 64U, 64U, 256U, 256U, 64U, 1024U};

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

[[nodiscard]] openrc::RacMobyPacketV1 make_packet() {
  return {openrc::RacMobyPacketKindV1::high_lod,
          {},
          {0U, kVifBytes},
          {kVertexOffset, kVertexBytes},
          4U,
          9U,
          3U};
}

[[nodiscard]] std::vector<std::byte> make_fixture() {
  std::vector<std::byte> bytes(kFixtureBytes, std::byte{0});

  // Three signed fixed-12 texture coordinates.
  write_le32(bytes, 0x00U, vif_code(0x75U, 3U, 0x00c2U));
  write_le_s16(bytes, 0x04U, 0);
  write_le_s16(bytes, 0x06U, 0);
  write_le_s16(bytes, 0x08U, 4096);
  write_le_s16(bytes, 0x0aU, 0);
  write_le_s16(bytes, 0x0cU, 0);
  write_le_s16(bytes, 0x0eU, 4096);

  // Header plus eight V4-8 lanes. The logical terminator is followed by one
  // zero lane of physical vector padding.
  write_le32(bytes, 0x10U, vif_code(0x6eU, 3U, 0x012dU));
  bytes[0x14U] = std::byte{0xff};
  bytes[0x15U] = std::byte{3};
  bytes[0x16U] = std::byte{0};
  bytes[0x17U] = std::byte{0};
  const std::array<std::uint8_t, 8U> indices{
      0x81U, 0x82U, 0x03U, 0x01U, 0x01U, 0x01U, 0x00U, 0x00U};
  for (std::size_t index = 0U; index < indices.size(); ++index) {
    bytes[0x18U + index] = static_cast<std::byte>(indices[index]);
  }

  // One valid but unused texture primitive. Regular packet recovery is
  // allowed to terminate before consuming every physical AD-GIF primitive.
  write_le32(bytes, 0x20U, vif_code(0x6cU, 4U, 0x0130U));
  write_le32(bytes, 0x44U, 3U);
  // Three VIF NOPs align the bounded command list to a qword.

  const auto vertex = static_cast<std::size_t>(kVertexOffset);
  write_le32(bytes, vertex + 0x00U, 0U);
  write_le32(bytes, vertex + 0x04U, 0U);
  write_le32(bytes, vertex + 0x08U, 0U);
  write_le32(bytes, vertex + 0x0cU, 3U);
  write_le32(bytes, vertex + 0x10U, 0U);
  write_le32(bytes, vertex + 0x14U, 3U);
  write_le32(bytes, vertex + 0x18U, 0x20U);
  write_le32(bytes, vertex + 0x1cU, kVertexBytes);

  const auto records = vertex + 0x20U;
  write_le_s16(bytes, records + 0x0aU, 0);
  write_le_s16(bytes, records + 0x0cU, 0);
  write_le_s16(bytes, records + 0x0eU, 0);
  write_le_s16(bytes, records + 0x1aU, 512);
  write_le_s16(bytes, records + 0x1cU, 0);
  write_le_s16(bytes, records + 0x1eU, 0);
  write_le_s16(bytes, records + 0x2aU, 0);
  write_le_s16(bytes, records + 0x2cU, 512);
  write_le_s16(bytes, records + 0x2eU, 0);

  // RAC1 delays cache indices by seven vertices. With three vertices, the
  // final epilogue record carries all three packed cache destinations.
  const auto last_epilogue = vertex + 0x80U;
  write_le16(bytes, last_epilogue + 0x04U, 5U);
  write_le16(bytes, last_epilogue + 0x06U, 6U);
  write_le16(bytes, last_epilogue + 0x08U, 7U);
  return bytes;
}

[[nodiscard]] openrc::RacMobyPacketGeometryV1 parse(
    const std::vector<std::byte> &bytes,
    const openrc::RacMobyPacketV1 &packet = make_packet(),
    const openrc::RacMobyPacketGeometryLimitsV1 limits = kLimits,
    const float scale = 2.0F) {
  return openrc::parse_rac_moby_packet_geometry_v1(bytes, packet, scale,
                                                    limits);
}

template <typename Mutation>
void expect_rejected(Mutation &&mutation, const std::string &message) {
  auto bytes = make_fixture();
  auto packet = make_packet();
  std::invoke(std::forward<Mutation>(mutation), bytes, packet);
  try {
    (void)parse(bytes, packet);
  } catch (const openrc::RacMobyPacketGeometryError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_valid_packet_with_physical_padding() {
  const auto result = parse(make_fixture());
  expect(result.input_bytes == kFixtureBytes && result.vif_nop_count == 3U &&
             result.texture_coordinates.size() == 3U &&
             result.texture_primitives.size() == 1U &&
             result.consumed_texture_primitive_count == 0U,
         "RAC1 Moby VIF metadata is wrong");
  expect(result.raw_strip_index_range ==
                 openrc::RacMobyPacketGeometryRangeV1{0x18U, 8U} &&
             result.strip_terminator_range ==
                 openrc::RacMobyPacketGeometryRangeV1{0x1eU, 1U} &&
             result.strip_trailing_padding_range ==
                 openrc::RacMobyPacketGeometryRangeV1{0x1fU, 1U},
         "RAC1 Moby logical strip termination ranges are wrong");
  expect(result.vertex_header.main_vertex_count == 3U &&
             result.vertex_header.transfer_vertex_count == 3U &&
             result.vertices.size() == 3U &&
             result.vertices[0U].vertex_cache_index == 5U &&
             result.vertices[1U].vertex_cache_index == 6U &&
             result.vertices[2U].vertex_cache_index == 7U,
         "RAC1 Moby vertex table is wrong");
  expect(result.vertices[1U].diagnostic_position ==
                 std::array<float, 3U>{1.0F, 0.0F, 0.0F} &&
             result.vertices[2U].diagnostic_position ==
                 std::array<float, 3U>{0.0F, 1.0F, 0.0F},
         "RAC1 Moby diagnostic positions are wrong");
  expect(result.strips.size() == 1U &&
             !result.strips[0U].texture_index.has_value() &&
             result.strips[0U].transfer_vertex_indices ==
                 std::vector<std::uint32_t>{0U, 0U, 1U, 2U} &&
             result.triangles.size() == 1U &&
             result.triangles[0U].transfer_vertex_indices ==
                 std::array<std::uint32_t, 3U>{1U, 0U, 2U},
         "RAC1 Moby strip reconstruction is wrong");
}

void test_texture_switch_secret_index() {
  auto bytes = make_fixture();
  bytes[0x16U] = std::byte{0x81};
  bytes[0x18U] = std::byte{0};
  const auto result = parse(bytes);
  expect(result.consumed_texture_primitive_count == 1U &&
             result.strips.size() == 1U &&
             result.strips[0U].texture_index == 3 &&
             result.triangles.size() == 1U &&
             result.triangles[0U].texture_index == 3,
         "RAC1 Moby secret texture switch is wrong");
}

void test_valid_non_drawing_packet() {
  auto bytes = make_fixture();
  for (std::size_t offset = 0x1bU; offset < 0x20U; ++offset) {
    bytes[offset] = std::byte{0};
  }
  const auto result = parse(bytes);
  expect(result.strip_terminator_range ==
                 openrc::RacMobyPacketGeometryRangeV1{0x1bU, 1U} &&
             result.strips.size() == 1U &&
             result.strips[0U].transfer_vertex_indices ==
                 std::vector<std::uint32_t>{0U} &&
             result.triangles.empty(),
         "a valid non-drawing RAC1 Moby packet was reconstructed incorrectly");
}

void test_limits_and_kind_policy() {
  const auto bytes = make_fixture();
  const std::array<openrc::RacMobyPacketGeometryLimitsV1, 7U> limits{
      openrc::RacMobyPacketGeometryLimitsV1{},
      openrc::RacMobyPacketGeometryLimitsV1{
          kFixtureBytes - 1U, 64U, 64U, 256U, 256U, 64U, 1024U},
      openrc::RacMobyPacketGeometryLimitsV1{
          0x10000U, 5U, 64U, 256U, 256U, 64U, 1024U},
      openrc::RacMobyPacketGeometryLimitsV1{
          0x10000U, 64U, 64U, 2U, 256U, 64U, 1024U},
      openrc::RacMobyPacketGeometryLimitsV1{
          0x10000U, 64U, 64U, 256U, 7U, 64U, 1024U},
      openrc::RacMobyPacketGeometryLimitsV1{
          0x10000U, 64U, 64U, 256U, 256U, 0U, 1024U},
      openrc::RacMobyPacketGeometryLimitsV1{
          0x10000U, 64U, 64U, 256U, 256U, 64U, 0U}};
  for (const auto &limit : limits) {
    try {
      (void)parse(bytes, make_packet(), limit);
    } catch (const openrc::RacMobyPacketGeometryError &) {
      continue;
    }
    throw std::runtime_error("a RAC1 Moby geometry limit was ignored");
  }

  auto metal = make_packet();
  metal.kind = openrc::RacMobyPacketKindV1::metal;
  try {
    (void)parse(bytes, metal);
  } catch (const openrc::RacMobyPacketGeometryError &) {
    return;
  }
  throw std::runtime_error("a RAC1 metal packet entered the regular decoder");
}

void test_structural_rejections() {
  expect_rejected(
      [](auto &bytes, auto &) { bytes[0x1fU] = std::byte{1}; },
      "non-zero bytes after a logical strip terminator were accepted");
  expect_rejected(
      [](auto &bytes, auto &) {
        bytes[0x1eU] = std::byte{1};
        bytes[0x1fU] = std::byte{1};
      },
      "a strip without a logical terminator was accepted");
  expect_rejected(
      [](auto &bytes, auto &) { bytes[0x1aU] = std::byte{4}; },
      "an out-of-range strip vertex was accepted");
  expect_rejected(
      [](auto &bytes, auto &) {
        write_le32(bytes, 0x44U,
                   std::bit_cast<std::uint32_t>(std::int32_t{-2}));
      },
      "a metal-only texture index was accepted in a regular packet");
  expect_rejected(
      [](auto &bytes, auto &) {
        write_le32(bytes, kVertexOffset + 0x1cU, 0x30U);
      },
      "a vertex table without delayed cache indices was accepted");
  expect_rejected(
      [](auto &, auto &packet) { packet.vertex_data_qwords = 8U; },
      "a mismatched packet vertex size was accepted");
}

void test_scale_policy() {
  const auto bytes = make_fixture();
  for (const auto scale : {0.0F, -1.0F}) {
    try {
      (void)parse(bytes, make_packet(), kLimits, scale);
    } catch (const openrc::RacMobyPacketGeometryError &) {
      continue;
    }
    throw std::runtime_error("an invalid RAC1 Moby class scale was accepted");
  }
}

} // namespace

int main() {
  try {
    test_valid_packet_with_physical_padding();
    test_texture_switch_secret_index();
    test_valid_non_drawing_packet();
    test_limits_and_kind_policy();
    test_structural_rejections();
    test_scale_policy();
    std::cout << "rac_moby_packet_geometry_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rac_moby_packet_geometry_tests: " << error.what() << '\n';
    return 1;
  }
}
