#include "openrc/rac_moby_packet_geometry.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint8_t kVifNop = 0x00U;
constexpr std::uint8_t kVifMaskedV2_16 = 0x75U;
constexpr std::uint8_t kVifV4_32 = 0x6cU;
constexpr std::uint8_t kVifV4_8 = 0x6eU;
constexpr std::uint16_t kStDestinationQword = 0x0c2U;
constexpr std::uint16_t kIndexDestinationQword = 0x12dU;
constexpr std::uint32_t kVifCodeBytes = 4U;
constexpr std::uint32_t kFixed12Denominator = 4096U;

[[noreturn]] void fail(const std::string &message) {
  throw RacMobyPacketGeometryError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(const std::span<const std::byte> bytes,
                                      const std::uint64_t offset) {
  const auto host = static_cast<std::size_t>(offset);
  return static_cast<std::uint16_t>(byte_value(bytes[host])) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(byte_value(bytes[host + 1U])) << 8U);
}

[[nodiscard]] std::int16_t read_le_s16(
    const std::span<const std::byte> bytes, const std::uint64_t offset) {
  return std::bit_cast<std::int16_t>(read_le16(bytes, offset));
}

[[nodiscard]] std::uint32_t read_le32(const std::span<const std::byte> bytes,
                                      const std::uint64_t offset) {
  const auto host = static_cast<std::size_t>(offset);
  return static_cast<std::uint32_t>(byte_value(bytes[host])) |
         (static_cast<std::uint32_t>(byte_value(bytes[host + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[host + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[host + 3U])) << 24U);
}

[[nodiscard]] std::int32_t read_le_s32(
    const std::span<const std::byte> bytes, const std::uint64_t offset) {
  return std::bit_cast<std::int32_t>(read_le32(bytes, offset));
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string("Integer overflow while calculating ") + description);
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left, const std::uint64_t right,
    const char *const description) {
  if (left != 0U &&
      right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string("Integer overflow while calculating ") + description);
  }
  return left * right;
}

[[nodiscard]] std::uint64_t align_up_8(const std::uint64_t value) {
  return checked_add(value, 7U, "an eight-byte alignment") & ~UINT64_C(7);
}

[[nodiscard]] std::uint64_t align_up_16(const std::uint64_t value) {
  return checked_add(value, 15U, "a sixteen-byte alignment") & ~UINT64_C(15);
}

void require_range(const std::uint64_t offset, const std::uint64_t size,
                   const std::size_t input_size,
                   const char *const description) {
  const auto available = static_cast<std::uint64_t>(input_size);
  if (offset > available || size > available - offset) {
    fail(std::string(description) + " exceeds the MobyClass input");
  }
}

void require_zero(const std::span<const std::byte> bytes,
                  const std::uint64_t begin, const std::uint64_t end,
                  const char *const description) {
  if (end < begin) {
    fail(std::string(description) + " has a reversed range");
  }
  for (auto offset = begin; offset < end; ++offset) {
    if (bytes[static_cast<std::size_t>(offset)] != std::byte{0}) {
      fail(std::string(description) + " contains non-zero padding");
    }
  }
}

[[nodiscard]] RacMobyPacketGeometryRangeV1
make_range(const std::uint64_t offset, const std::uint64_t size) noexcept {
  return {offset, size};
}

void require_limit(const std::uint64_t value, const std::uint64_t limit,
                   const char *const description) {
  if (value > limit) {
    fail(std::string(description) + " exceeds the caller's limit");
  }
}

[[nodiscard]] std::uint16_t decoded_vif_count(
    const std::uint32_t raw_code) noexcept {
  const auto raw = static_cast<std::uint8_t>((raw_code >> 16U) & 0xffU);
  return raw == 0U ? 256U : raw;
}

[[nodiscard]] RacMobyPacketUnpackV1 parse_unpack(
    const std::span<const std::byte> bytes, const std::uint64_t position,
    const std::uint64_t vif_end, const RacMobyPacketUnpackKindV1 kind,
    const std::uint8_t expected_opcode, const std::uint64_t bytes_per_vector) {
  const auto raw = read_le32(bytes, position);
  const auto opcode = static_cast<std::uint8_t>(raw >> 24U);
  if (opcode != expected_opcode) {
    fail("A RAC1 Moby packet has an unexpected VIF UNPACK order or format");
  }
  const auto immediate = static_cast<std::uint16_t>(raw & 0xffffU);
  if ((immediate & 0x3c00U) != 0U) {
    fail("A RAC1 Moby UNPACK uses non-zero reserved immediate bits");
  }
  const auto count = decoded_vif_count(raw);
  const auto payload_bytes = checked_multiply(
      count, bytes_per_vector, "a RAC1 Moby UNPACK payload size");
  const auto payload_offset =
      checked_add(position, kVifCodeBytes, "a RAC1 Moby UNPACK payload");
  const auto packet_end = checked_add(payload_offset, payload_bytes,
                                      "a RAC1 Moby UNPACK end");
  if (packet_end > vif_end) {
    fail("A RAC1 Moby UNPACK exceeds its bounded VIF list");
  }

  RacMobyPacketUnpackV1 result;
  result.kind = kind;
  result.code_range = make_range(position, kVifCodeBytes);
  result.payload_range = make_range(payload_offset, payload_bytes);
  result.packet_range = make_range(position, packet_end - position);
  result.raw_code = raw;
  result.vector_count = count;
  result.destination_qword = immediate & 0x03ffU;
  result.signed_data = (immediate & 0x4000U) == 0U;
  result.use_tops = (immediate & 0x8000U) != 0U;
  if (!result.signed_data || !result.use_tops) {
    fail("A RAC1 Moby UNPACK is not signed TOPS-relative data");
  }
  return result;
}

void require_host_container_size(const std::uint64_t count,
                                 const std::size_t maximum,
                                 const char *const description) {
  if (count > maximum) {
    fail(std::string(description) + " exceeds the host container");
  }
}

} // namespace

RacMobyPacketGeometryV1 parse_rac_moby_packet_geometry_v1(
    const std::span<const std::byte> class_bytes,
    const RacMobyPacketV1 &packet, const float class_scale,
    const RacMobyPacketGeometryLimitsV1 limits) {
  if (limits.max_input_bytes == 0U || limits.max_vif_commands == 0U ||
      limits.max_matrix_transfers == 0U || limits.max_vertices == 0U ||
      limits.max_strip_indices == 0U ||
      limits.max_texture_primitives == 0U || limits.max_triangles == 0U) {
    fail("RacMobyPacketGeometryV1 caller limits must be non-zero");
  }
  require_limit(class_bytes.size(), limits.max_input_bytes,
                "The RAC1 MobyClass input");
  if (!std::isfinite(class_scale) || class_scale <= 0.0F) {
    fail("RacMobyPacketGeometryV1 requires a finite positive class scale");
  }
  if (packet.kind == RacMobyPacketKindV1::metal) {
    fail("RacMobyPacketGeometryV1 does not decode metal packets");
  }
  if ((packet.vif_range.offset & 0x0fU) != 0U ||
      (packet.vif_range.size & 0x0fU) != 0U ||
      (packet.vertex_range.offset & 0x0fU) != 0U ||
      (packet.vertex_range.size & 0x0fU) != 0U ||
      packet.vif_range.size == 0U || packet.vertex_range.size == 0U) {
    fail("A RAC1 Moby packet has an invalid aligned data envelope");
  }
  require_range(packet.vif_range.offset, packet.vif_range.size,
                class_bytes.size(), "The RAC1 Moby VIF list");
  require_range(packet.vertex_range.offset, packet.vertex_range.size,
                class_bytes.size(), "The RAC1 Moby vertex table");
  if (checked_multiply(packet.vertex_data_qwords, 0x10U,
                       "the packet vertex byte size") !=
      packet.vertex_range.size) {
    fail("A RAC1 Moby packet vertex size disagrees with its directory entry");
  }

  RacMobyPacketGeometryV1 result;
  result.input_bytes = class_bytes.size();

  const auto vif_end = checked_add(packet.vif_range.offset,
                                   packet.vif_range.size,
                                   "the RAC1 Moby VIF-list end");
  std::uint64_t position = packet.vif_range.offset;
  std::uint64_t command_count = 0U;
  std::uint32_t unpack_count = 0U;
  while (position < vif_end) {
    if (vif_end - position < kVifCodeBytes) {
      fail("A RAC1 Moby VIF list ends with a truncated VIFcode");
    }
    ++command_count;
    require_limit(command_count, limits.max_vif_commands,
                  "The RAC1 Moby VIF command count");
    const auto raw = read_le32(class_bytes, position);
    const auto opcode = static_cast<std::uint8_t>(raw >> 24U);
    if (opcode == kVifNop) {
      if (raw != 0U) {
        fail("A RAC1 Moby VIF NOP has non-zero fields");
      }
      ++result.vif_nop_count;
      position += kVifCodeBytes;
      continue;
    }

    RacMobyPacketUnpackV1 unpack;
    if (unpack_count == 0U) {
      unpack = parse_unpack(
          class_bytes, position, vif_end,
          RacMobyPacketUnpackKindV1::texture_coordinates,
          kVifMaskedV2_16, 4U);
      result.texture_coordinate_unpack = unpack;
    } else if (unpack_count == 1U) {
      unpack = parse_unpack(class_bytes, position, vif_end,
                            RacMobyPacketUnpackKindV1::strip_indices,
                            kVifV4_8, 4U);
      result.strip_index_unpack = unpack;
    } else if (unpack_count == 2U) {
      unpack = parse_unpack(
          class_bytes, position, vif_end,
          RacMobyPacketUnpackKindV1::texture_primitives,
          kVifV4_32, 16U);
      result.texture_primitive_unpack = unpack;
    } else {
      fail("A regular RAC1 Moby packet contains more than three UNPACKs");
    }
    ++unpack_count;
    position = checked_add(unpack.packet_range.offset, unpack.packet_range.size,
                           "a RAC1 Moby VIF packet end");
  }
  if (unpack_count < 2U || position != vif_end) {
    fail("A regular RAC1 Moby packet lacks its required VIF UNPACKs");
  }
  if (result.texture_coordinate_unpack.destination_qword !=
          kStDestinationQword ||
      result.strip_index_unpack.destination_qword !=
          kIndexDestinationQword) {
    fail("A regular RAC1 Moby packet uses unexpected VU1 destinations");
  }
  if (result.texture_coordinate_unpack.vector_count !=
      packet.transfer_vertex_count) {
    fail("A RAC1 Moby ST UNPACK disagrees with the transfer-vertex count");
  }

  require_host_container_size(
      result.texture_coordinate_unpack.vector_count,
      result.texture_coordinates.max_size(),
      "The RAC1 Moby texture-coordinate count");
  require_limit(result.texture_coordinate_unpack.vector_count,
                limits.max_vertices,
                "The RAC1 Moby texture-coordinate count");
  result.texture_coordinates.reserve(
      result.texture_coordinate_unpack.vector_count);
  for (std::uint32_t index = 0U;
       index < result.texture_coordinate_unpack.vector_count; ++index) {
    const auto offset = checked_add(
        result.texture_coordinate_unpack.payload_range.offset,
        checked_multiply(index, 4U, "a RAC1 Moby ST offset"),
        "a RAC1 Moby ST record");
    const auto s = read_le_s16(class_bytes, offset);
    const auto t = read_le_s16(class_bytes, offset + 2U);
    result.texture_coordinates.push_back(RacMobyTexCoordV1{
        make_range(offset, 4U), s, t,
        static_cast<float>(s) / static_cast<float>(kFixed12Denominator),
        static_cast<float>(t) / static_cast<float>(kFixed12Denominator)});
  }

  if (result.strip_index_unpack.payload_range.size < 4U) {
    fail("A RAC1 Moby index UNPACK lacks its four-byte header");
  }
  const auto index_payload = result.strip_index_unpack.payload_range.offset;
  result.index_header_range = make_range(index_payload, 4U);
  result.index_header_unknown =
      byte_value(class_bytes[static_cast<std::size_t>(index_payload)]);
  result.texture_unpack_relative_qwords = byte_value(
      class_bytes[static_cast<std::size_t>(index_payload + 1U)]);
  const auto first_secret_index = byte_value(
      class_bytes[static_cast<std::size_t>(index_payload + 2U)]);
  if (class_bytes[static_cast<std::size_t>(index_payload + 3U)] !=
      std::byte{0}) {
    fail("A RAC1 Moby index header has a non-zero padding byte");
  }
  result.raw_strip_index_range = make_range(
      index_payload + 4U, result.strip_index_unpack.payload_range.size - 4U);
  require_limit(result.raw_strip_index_range.size, limits.max_strip_indices,
                "The RAC1 Moby raw strip-index count");
  if (result.raw_strip_index_range.size == 0U) {
    fail("A RAC1 Moby index UNPACK contains no strip indices");
  }

  std::vector<std::uint8_t> secret_indices;
  secret_indices.push_back(first_secret_index);
  if (result.texture_primitive_unpack) {
    const auto &textures = *result.texture_primitive_unpack;
    if ((textures.vector_count & 3U) != 0U) {
      fail("A RAC1 Moby texture UNPACK does not contain four-qword primitives");
    }
    const auto texture_count =
        static_cast<std::uint64_t>(textures.vector_count / 4U);
    require_limit(texture_count, limits.max_texture_primitives,
                  "The RAC1 Moby texture-primitive count");
    require_host_container_size(texture_count,
                                result.texture_primitives.max_size(),
                                "The RAC1 Moby texture-primitive count");
    if (texture_count == 0U ||
        result.texture_unpack_relative_qwords !=
            result.strip_index_unpack.vector_count ||
        textures.destination_qword !=
            static_cast<std::uint16_t>(
                kIndexDestinationQword +
                result.texture_unpack_relative_qwords) ||
        packet.texture_unpack_offset_qwords != textures.vector_count) {
      fail("A RAC1 Moby texture UNPACK disagrees with its packet metadata");
    }
    result.texture_primitives.reserve(static_cast<std::size_t>(texture_count));
    secret_indices.reserve(static_cast<std::size_t>(texture_count + 1U));
    for (std::uint64_t index = 0U; index < texture_count; ++index) {
      const auto primitive_offset = checked_add(
          textures.payload_range.offset,
          checked_multiply(index, kRacMobyTexturePrimitiveBytesV1,
                           "a RAC1 Moby texture-primitive offset"),
          "a RAC1 Moby texture primitive");
      RacMobyTexturePrimitiveV1 primitive;
      primitive.source_range =
          make_range(primitive_offset, kRacMobyTexturePrimitiveBytesV1);
      primitive.texture_index = read_le_s32(class_bytes,
                                            primitive_offset + 0x20U);
      if (primitive.texture_index < -1) {
        fail("A regular RAC1 Moby packet uses a metal-only texture index");
      }
      for (std::size_t word = 0U; word < primitive.secret_index_words.size();
           ++word) {
        primitive.secret_index_words[word] = read_le32(
            class_bytes, primitive_offset + word * 0x10U + 0x0cU);
      }
      result.texture_primitives.push_back(primitive);
      // Secret indices after the header occupy consecutive qword W lanes,
      // not necessarily the first qword of each 0x40-byte primitive.
      const auto secret_offset = checked_add(
          textures.payload_range.offset,
          checked_add(checked_multiply(index, 0x10U,
                                       "a RAC1 Moby secret-index offset"),
                      0x0cU, "a RAC1 Moby secret-index lane"),
          "a RAC1 Moby secret index");
      secret_indices.push_back(byte_value(
          class_bytes[static_cast<std::size_t>(secret_offset)]));
    }
  } else {
    if (result.texture_unpack_relative_qwords != 0U ||
        packet.texture_unpack_offset_qwords != 0U) {
      fail("A RAC1 Moby packet references a missing texture UNPACK");
    }
  }

  const auto vertex_base = packet.vertex_range.offset;
  if (packet.vertex_range.size < kRacMobyVertexHeaderBytesV1) {
    fail("A RAC1 Moby vertex block is shorter than its RAC1 header");
  }
  auto &header = result.vertex_header;
  header.header_range = make_range(vertex_base, kRacMobyVertexHeaderBytesV1);
  header.matrix_transfer_count = read_le32(class_bytes, vertex_base + 0x00U);
  header.two_way_blend_vertex_count =
      read_le32(class_bytes, vertex_base + 0x04U);
  header.three_way_blend_vertex_count =
      read_le32(class_bytes, vertex_base + 0x08U);
  header.main_vertex_count = read_le32(class_bytes, vertex_base + 0x0cU);
  header.duplicate_vertex_count = read_le32(class_bytes, vertex_base + 0x10U);
  header.transfer_vertex_count = read_le32(class_bytes, vertex_base + 0x14U);
  header.vertex_table_offset = read_le32(class_bytes, vertex_base + 0x18U);
  header.epilogue_end_offset = read_le32(class_bytes, vertex_base + 0x1cU);

  require_limit(header.matrix_transfer_count, limits.max_matrix_transfers,
                "The RAC1 Moby matrix-transfer count");
  const auto file_vertex_count = checked_add(
      checked_add(header.two_way_blend_vertex_count,
                  header.three_way_blend_vertex_count,
                  "the RAC1 Moby blended-vertex count"),
      header.main_vertex_count, "the RAC1 Moby file-vertex count");
  const auto computed_transfer_count = checked_add(
      file_vertex_count, header.duplicate_vertex_count,
      "the RAC1 Moby transfer-vertex count");
  if (file_vertex_count == 0U ||
      computed_transfer_count != header.transfer_vertex_count ||
      header.transfer_vertex_count != packet.transfer_vertex_count) {
    fail("A RAC1 Moby vertex header has inconsistent vertex counts");
  }
  require_limit(file_vertex_count, limits.max_vertices,
                "The RAC1 Moby file-vertex count");
  require_limit(header.transfer_vertex_count, limits.max_vertices,
                "The RAC1 Moby transfer-vertex count");

  const auto matrix_bytes = checked_multiply(
      header.matrix_transfer_count, 2U,
      "the RAC1 Moby matrix-transfer byte size");
  const auto matrix_begin =
      checked_add(vertex_base, kRacMobyVertexHeaderBytesV1,
                  "the RAC1 Moby matrix-transfer table");
  const auto matrix_end = checked_add(matrix_begin, matrix_bytes,
                                      "the RAC1 Moby matrix-transfer end");
  const auto duplicate_begin = align_up_8(matrix_end);
  const auto duplicate_bytes = checked_multiply(
      header.duplicate_vertex_count, 2U,
      "the RAC1 Moby duplicate-table byte size");
  const auto duplicate_end = checked_add(
      duplicate_begin, duplicate_bytes, "the RAC1 Moby duplicate-table end");
  const auto vertex_record_begin = checked_add(
      vertex_base, header.vertex_table_offset,
      "the RAC1 Moby main vertex-table offset");
  const auto vertex_record_bytes = checked_multiply(
      file_vertex_count, kRacMobyVertexRecordBytesV1,
      "the RAC1 Moby vertex-record byte size");
  const auto vertex_record_end = checked_add(
      vertex_record_begin, vertex_record_bytes,
      "the RAC1 Moby vertex-record end");
  const auto epilogue_end = checked_add(
      vertex_base, header.epilogue_end_offset,
      "the RAC1 Moby vertex epilogue end");
  const auto vertex_block_end = checked_add(
      vertex_base, packet.vertex_range.size,
      "the RAC1 Moby vertex-block end");

  if ((header.vertex_table_offset & 0x0fU) != 0U ||
      header.vertex_table_offset < kRacMobyVertexHeaderBytesV1 ||
      duplicate_end > vertex_record_begin || vertex_record_end > epilogue_end ||
      epilogue_end > vertex_block_end ||
      ((epilogue_end - vertex_record_end) & 0x0fU) != 0U) {
    fail("A RAC1 Moby vertex header has invalid bounded table offsets");
  }
  const auto epilogue_count =
      (epilogue_end - vertex_record_end) / kRacMobyVertexRecordBytesV1;
  if (epilogue_count >= 7U) {
    fail("A RAC1 Moby vertex table has too many epilogue records");
  }
  require_zero(class_bytes, matrix_end, duplicate_begin,
               "The RAC1 Moby matrix-transfer alignment");
  require_zero(class_bytes, duplicate_end, vertex_record_begin,
               "The RAC1 Moby pre-vertex padding");

  header.matrix_transfer_range = make_range(matrix_begin, matrix_bytes);
  header.duplicate_range = make_range(duplicate_begin, duplicate_bytes);
  header.vertex_record_range =
      make_range(vertex_record_begin, vertex_record_bytes);
  header.epilogue_range =
      make_range(vertex_record_end, epilogue_end - vertex_record_end);
  header.trailing_range =
      make_range(epilogue_end, vertex_block_end - epilogue_end);

  require_host_container_size(header.matrix_transfer_count,
                              result.matrix_transfers.max_size(),
                              "The RAC1 Moby matrix-transfer count");
  result.matrix_transfers.reserve(header.matrix_transfer_count);
  for (std::uint32_t index = 0U; index < header.matrix_transfer_count;
       ++index) {
    const auto offset = matrix_begin + static_cast<std::uint64_t>(index) * 2U;
    result.matrix_transfers.push_back(RacMobyMatrixTransferV1{
        make_range(offset, 2U),
        byte_value(class_bytes[static_cast<std::size_t>(offset)]),
        byte_value(class_bytes[static_cast<std::size_t>(offset + 1U)])});
  }

  require_host_container_size(file_vertex_count, result.vertices.max_size(),
                              "The RAC1 Moby file-vertex count");
  require_host_container_size(header.duplicate_vertex_count,
                              result.duplicate_vertices.max_size(),
                              "The RAC1 Moby duplicate-vertex count");
  result.vertices.reserve(static_cast<std::size_t>(file_vertex_count));
  result.duplicate_vertices.reserve(header.duplicate_vertex_count);

  std::vector<std::optional<std::uint16_t>> decoded_cache_indices(
      static_cast<std::size_t>(file_vertex_count));
  for (std::uint64_t source_index = 7U; source_index < file_vertex_count;
       ++source_index) {
    decoded_cache_indices[static_cast<std::size_t>(source_index - 7U)] =
        read_le16(class_bytes,
                  vertex_record_begin +
                      source_index * kRacMobyVertexRecordBytesV1) &
        0x01ffU;
  }
  const auto skipped_epilogue =
      file_vertex_count < 7U ? 7U - file_vertex_count : 0U;
  if (skipped_epilogue > epilogue_count) {
    fail("A RAC1 Moby vertex epilogue cannot supply delayed cache indices");
  }
  auto epilogue_cursor = checked_add(
      vertex_record_end,
      checked_multiply(skipped_epilogue, kRacMobyVertexRecordBytesV1,
                       "the RAC1 Moby epilogue skip"),
      "the RAC1 Moby delayed-index epilogue cursor");
  for (auto index = skipped_epilogue; index < epilogue_count; ++index) {
    const auto destination = static_cast<std::int64_t>(file_vertex_count) +
                             static_cast<std::int64_t>(index) - 7;
    if (destination < 0 ||
        static_cast<std::uint64_t>(destination) >= file_vertex_count) {
      fail("A RAC1 Moby epilogue cache index has no destination vertex");
    }
    decoded_cache_indices[static_cast<std::size_t>(destination)] =
        read_le16(class_bytes, epilogue_cursor) & 0x01ffU;
    epilogue_cursor += kRacMobyVertexRecordBytesV1;
  }
  if (epilogue_cursor < vertex_base + kRacMobyVertexRecordBytesV1 ||
      epilogue_cursor > epilogue_end) {
    fail("A RAC1 Moby delayed-index epilogue cursor is out of range");
  }
  const auto last_epilogue_record =
      epilogue_cursor - kRacMobyVertexRecordBytesV1;
  const auto packed_index_begin =
      file_vertex_count + epilogue_count < 7U
          ? 7U - file_vertex_count - epilogue_count
          : 0U;
  for (auto index = packed_index_begin; index < 6U; ++index) {
    const auto destination = static_cast<std::int64_t>(file_vertex_count) +
                             static_cast<std::int64_t>(epilogue_count) +
                             static_cast<std::int64_t>(index) - 7;
    if (destination >= 0 &&
        static_cast<std::uint64_t>(destination) < file_vertex_count) {
      decoded_cache_indices[static_cast<std::size_t>(destination)] =
          read_le16(class_bytes,
                    last_epilogue_record + 4U + index * 2U) &
          0x01ffU;
    }
  }
  if (std::any_of(decoded_cache_indices.begin(), decoded_cache_indices.end(),
                  [](const auto &value) { return !value.has_value(); })) {
    fail("A RAC1 Moby vertex epilogue leaves cache indices indeterminate");
  }

  std::array<std::optional<std::uint32_t>, kRacMobyVertexCacheEntriesV1>
      local_cache{};
  for (std::uint32_t index = 0U; index < file_vertex_count; ++index) {
    const auto offset = vertex_record_begin +
                        static_cast<std::uint64_t>(index) *
                            kRacMobyVertexRecordBytesV1;
    RacMobyPacketLocalVertexV1 vertex;
    vertex.source_range = make_range(offset, kRacMobyVertexRecordBytesV1);
    for (std::size_t byte = 0U; byte < vertex.control_bytes.size(); ++byte) {
      vertex.control_bytes[byte] =
          byte_value(class_bytes[static_cast<std::size_t>(offset + byte)]);
    }
    vertex.normal_azimuth =
        byte_value(class_bytes[static_cast<std::size_t>(offset + 0x08U)]);
    vertex.normal_elevation =
        byte_value(class_bytes[static_cast<std::size_t>(offset + 0x09U)]);
    for (std::size_t axis = 0U; axis < vertex.quantized_position.size();
         ++axis) {
      vertex.quantized_position[axis] =
          read_le_s16(class_bytes, offset + 0x0aU + axis * 2U);
      vertex.diagnostic_position[axis] =
          static_cast<float>(vertex.quantized_position[axis]) *
          (class_scale / 1024.0F);
    }
    vertex.vertex_cache_index =
        *decoded_cache_indices[static_cast<std::size_t>(index)];
    local_cache[vertex.vertex_cache_index] = index;
    result.vertices.push_back(vertex);
  }

  for (std::uint32_t index = 0U; index < header.duplicate_vertex_count;
       ++index) {
    const auto offset = duplicate_begin + static_cast<std::uint64_t>(index) * 2U;
    const auto encoded = read_le16(class_bytes, offset);
    if ((encoded & 0x007fU) != 0U) {
      fail("A RAC1 Moby duplicate cache index has non-zero low bits");
    }
    const auto cache_index = static_cast<std::uint16_t>(encoded >> 7U);
    const auto local_source = local_cache[cache_index];
    if (!local_source) {
      ++result.unresolved_duplicate_count;
    }
    result.duplicate_vertices.push_back(RacMobyDuplicateVertexV1{
        make_range(offset, 2U), encoded, cache_index, local_source});
  }

  std::optional<std::size_t> current_strip;
  std::optional<std::int32_t> current_texture;
  std::uint64_t expanded_strip_indices = 0U;
  std::size_t texture_cursor = 0U;
  bool terminated = false;
  const auto raw_index_begin = result.raw_strip_index_range.offset;
  const auto raw_index_count = result.raw_strip_index_range.size;
  for (std::uint64_t source_index = 0U; source_index < raw_index_count;
       ++source_index) {
    const auto raw = byte_value(class_bytes[static_cast<std::size_t>(
        raw_index_begin + source_index)]);
    auto signed_index = std::bit_cast<std::int8_t>(raw);
    std::int32_t effective_index = signed_index;
    if (raw == 0U) {
      if (texture_cursor >= secret_indices.size()) {
        fail("A RAC1 Moby strip consumes a missing secret index");
      }
      const auto secret = secret_indices[texture_cursor];
      if (secret == 0U) {
        if (!current_strip ||
            result.strips[*current_strip].transfer_vertex_indices.size() <
                3U) {
          fail("A RAC1 Moby strip has an invalid termination marker");
        }
        const auto trailing_begin =
            checked_add(raw_index_begin, source_index + 1U,
                        "the RAC1 Moby strip padding");
        const auto raw_index_end =
            checked_add(raw_index_begin, raw_index_count,
                        "the RAC1 Moby strip-index end");
        result.strip_terminator_range =
            make_range(raw_index_begin + source_index, 1U);
        result.strip_trailing_padding_range =
            make_range(trailing_begin, raw_index_end - trailing_begin);
        require_zero(class_bytes, trailing_begin, raw_index_end,
                     "The RAC1 Moby strip trailing padding");
        auto &indices = result.strips[*current_strip].transfer_vertex_indices;
        indices.resize(indices.size() - 3U);
        expanded_strip_indices -= 3U;
        result.consumed_texture_primitive_count = texture_cursor;
        terminated = true;
        break;
      }
      if (texture_cursor >= result.texture_primitives.size() ||
          (secret & 0x7fU) == 0U) {
        fail("A RAC1 Moby texture switch has an invalid secret index");
      }
      effective_index =
          static_cast<std::int32_t>(std::bit_cast<std::int8_t>(secret)) -
          0x80;
      current_texture = result.texture_primitives[texture_cursor].texture_index;
      ++texture_cursor;
    }

    const auto encoded_index =
        static_cast<std::uint32_t>(effective_index) & 0x7fU;
    if (encoded_index == 0U) {
      fail("A RAC1 Moby strip contains a zero one-based vertex index");
    }
    const auto transfer_index = encoded_index - 1U;
    if (transfer_index >= header.transfer_vertex_count) {
      fail("A RAC1 Moby strip index exceeds its transfer-vertex table");
    }

    const auto suppress_draw = effective_index <= 0;
    if (suppress_draw) {
      const auto next_is_suppressed =
          source_index + 1U < raw_index_count &&
          std::bit_cast<std::int8_t>(byte_value(class_bytes[
              static_cast<std::size_t>(raw_index_begin + source_index + 1U)])) <=
              0;
      if (next_is_suppressed) {
        result.strips.push_back(
            RacMobyPacketStripV1{current_texture, {}});
        current_strip = result.strips.size() - 1U;
      } else {
        if (!current_strip ||
            result.strips[*current_strip].transfer_vertex_indices.empty()) {
          fail("A RAC1 Moby single strip restart has no previous vertex");
        }
        auto &indices = result.strips[*current_strip].transfer_vertex_indices;
        indices.push_back(indices.back());
        ++expanded_strip_indices;
        require_limit(expanded_strip_indices, limits.max_strip_indices,
                      "The RAC1 Moby expanded strip-index count");
      }
    }
    if (!current_strip) {
      fail("A RAC1 Moby strip begins without a suppressed drawing kick");
    }
    result.strips[*current_strip].transfer_vertex_indices.push_back(
        transfer_index);
    ++expanded_strip_indices;
    require_limit(expanded_strip_indices, limits.max_strip_indices,
                  "The RAC1 Moby expanded strip-index count");
  }
  if (!terminated) {
    fail("A RAC1 Moby strip lacks its secret-index terminator");
  }

  for (const auto &strip : result.strips) {
    for (std::size_t index = 2U; index < strip.transfer_vertex_indices.size();
         ++index) {
      auto a = strip.transfer_vertex_indices[index - 2U];
      auto b = strip.transfer_vertex_indices[index - 1U];
      const auto c = strip.transfer_vertex_indices[index];
      if ((index & 1U) != 0U) {
        std::swap(a, b);
      }
      if (a == b || b == c || a == c) {
        continue;
      }
      require_limit(result.triangles.size() + 1U, limits.max_triangles,
                    "The RAC1 Moby triangle count");
      result.triangles.push_back(
          RacMobyPacketTriangleV1{{a, b, c}, strip.texture_index});
    }
  }
  return result;
}

RacMobyMetalPacketGeometryV1 parse_rac_moby_metal_packet_geometry_v1(
    const std::span<const std::byte> class_bytes,
    const RacMobyPacketV1 &packet, const float class_scale,
    const RacMobyPacketGeometryLimitsV1 limits) {
  if (limits.max_input_bytes == 0U || limits.max_vif_commands == 0U ||
      limits.max_matrix_transfers == 0U || limits.max_vertices == 0U ||
      limits.max_strip_indices == 0U ||
      limits.max_texture_primitives == 0U || limits.max_triangles == 0U) {
    fail("RacMobyMetalPacketGeometryV1 caller limits must be non-zero");
  }
  require_limit(class_bytes.size(), limits.max_input_bytes,
                "The RAC1 MobyClass input");
  if (!std::isfinite(class_scale) || class_scale <= 0.0F) {
    fail("RacMobyMetalPacketGeometryV1 requires a finite positive class "
         "scale");
  }
  if (packet.kind != RacMobyPacketKindV1::metal) {
    fail("RacMobyMetalPacketGeometryV1 requires a metal packet");
  }
  if ((packet.vif_range.offset & 0x0fU) != 0U ||
      (packet.vif_range.size & 0x0fU) != 0U ||
      (packet.vertex_range.offset & 0x0fU) != 0U ||
      (packet.vertex_range.size & 0x0fU) != 0U ||
      packet.vif_range.size == 0U || packet.vertex_range.size == 0U) {
    fail("A RAC1 Moby metal packet has an invalid aligned data envelope");
  }
  require_range(packet.vif_range.offset, packet.vif_range.size,
                class_bytes.size(), "The RAC1 Moby metal VIF list");
  require_range(packet.vertex_range.offset, packet.vertex_range.size,
                class_bytes.size(), "The RAC1 Moby metal vertex table");
  if (checked_multiply(packet.vertex_data_qwords, 0x10U,
                       "the metal-packet vertex byte size") !=
      packet.vertex_range.size) {
    fail("A RAC1 Moby metal-packet vertex size disagrees with its directory "
         "entry");
  }

  RacMobyMetalPacketGeometryV1 result;
  result.input_bytes = class_bytes.size();

  const auto vif_end = checked_add(packet.vif_range.offset,
                                   packet.vif_range.size,
                                   "the RAC1 Moby metal VIF-list end");
  std::uint64_t position = packet.vif_range.offset;
  std::uint64_t command_count = 0U;
  std::uint32_t unpack_count = 0U;
  while (position < vif_end) {
    if (vif_end - position < kVifCodeBytes) {
      fail("A RAC1 Moby metal VIF list ends with a truncated VIFcode");
    }
    ++command_count;
    require_limit(command_count, limits.max_vif_commands,
                  "The RAC1 Moby metal VIF command count");
    const auto raw = read_le32(class_bytes, position);
    const auto opcode = static_cast<std::uint8_t>(raw >> 24U);
    if (opcode == kVifNop) {
      if (raw != 0U) {
        fail("A RAC1 Moby metal VIF NOP has non-zero fields");
      }
      ++result.vif_nop_count;
      position += kVifCodeBytes;
      continue;
    }

    RacMobyPacketUnpackV1 unpack;
    if (unpack_count == 0U) {
      unpack = parse_unpack(class_bytes, position, vif_end,
                            RacMobyPacketUnpackKindV1::strip_indices,
                            kVifV4_8, 4U);
      result.strip_index_unpack = unpack;
    } else if (unpack_count == 1U) {
      unpack = parse_unpack(
          class_bytes, position, vif_end,
          RacMobyPacketUnpackKindV1::texture_primitives,
          kVifV4_32, 16U);
      result.texture_primitive_unpack = unpack;
    } else {
      fail("A RAC1 Moby metal packet contains more than two UNPACKs");
    }
    ++unpack_count;
    position = checked_add(unpack.packet_range.offset, unpack.packet_range.size,
                           "a RAC1 Moby metal VIF packet end");
  }
  if (unpack_count == 0U || position != vif_end) {
    fail("A RAC1 Moby metal packet lacks its required index UNPACK");
  }
  if (result.strip_index_unpack.destination_qword !=
      kIndexDestinationQword) {
    fail("A RAC1 Moby metal packet uses an unexpected VU1 index "
         "destination");
  }

  if (result.strip_index_unpack.payload_range.size < 4U) {
    fail("A RAC1 Moby metal index UNPACK lacks its four-byte header");
  }
  const auto index_payload = result.strip_index_unpack.payload_range.offset;
  result.index_header_range = make_range(index_payload, 4U);
  result.index_header_unknown =
      byte_value(class_bytes[static_cast<std::size_t>(index_payload)]);
  result.texture_unpack_relative_qwords = byte_value(
      class_bytes[static_cast<std::size_t>(index_payload + 1U)]);
  const auto first_secret_index = byte_value(
      class_bytes[static_cast<std::size_t>(index_payload + 2U)]);
  if (class_bytes[static_cast<std::size_t>(index_payload + 3U)] !=
      std::byte{0}) {
    fail("A RAC1 Moby metal index header has a non-zero padding byte");
  }
  result.raw_strip_index_range = make_range(
      index_payload + 4U, result.strip_index_unpack.payload_range.size - 4U);
  require_limit(result.raw_strip_index_range.size, limits.max_strip_indices,
                "The RAC1 Moby metal raw strip-index count");
  if (result.raw_strip_index_range.size == 0U) {
    fail("A RAC1 Moby metal index UNPACK contains no strip indices");
  }

  std::vector<std::uint8_t> secret_indices;
  secret_indices.push_back(first_secret_index);
  if (result.texture_primitive_unpack) {
    const auto &textures = *result.texture_primitive_unpack;
    if ((textures.vector_count & 3U) != 0U) {
      fail("A RAC1 Moby metal texture UNPACK does not contain four-qword "
           "primitives");
    }
    const auto texture_count =
        static_cast<std::uint64_t>(textures.vector_count / 4U);
    require_limit(texture_count, limits.max_texture_primitives,
                  "The RAC1 Moby metal texture-primitive count");
    require_host_container_size(texture_count,
                                result.texture_primitives.max_size(),
                                "The RAC1 Moby metal texture-primitive count");
    if (texture_count == 0U ||
        result.texture_unpack_relative_qwords !=
            result.strip_index_unpack.vector_count ||
        textures.destination_qword !=
            static_cast<std::uint16_t>(
                kIndexDestinationQword +
                result.texture_unpack_relative_qwords) ||
        packet.texture_unpack_offset_qwords != textures.vector_count) {
      fail("A RAC1 Moby metal texture UNPACK disagrees with its packet "
           "metadata");
    }
    result.texture_primitives.reserve(static_cast<std::size_t>(texture_count));
    secret_indices.reserve(static_cast<std::size_t>(texture_count + 1U));
    for (std::uint64_t index = 0U; index < texture_count; ++index) {
      const auto primitive_offset = checked_add(
          textures.payload_range.offset,
          checked_multiply(index, kRacMobyTexturePrimitiveBytesV1,
                           "a RAC1 Moby metal texture-primitive offset"),
          "a RAC1 Moby metal texture primitive");
      RacMobyTexturePrimitiveV1 primitive;
      primitive.source_range =
          make_range(primitive_offset, kRacMobyTexturePrimitiveBytesV1);
      primitive.texture_index =
          read_le_s32(class_bytes, primitive_offset + 0x20U);
      if (primitive.texture_index != -2 && primitive.texture_index != -3) {
        fail("A RAC1 Moby metal packet uses an unproven material sentinel");
      }
      for (std::size_t word = 0U; word < primitive.secret_index_words.size();
           ++word) {
        primitive.secret_index_words[word] = read_le32(
            class_bytes, primitive_offset + word * 0x10U + 0x0cU);
      }
      result.texture_primitives.push_back(primitive);
      const auto secret_offset = checked_add(
          textures.payload_range.offset,
          checked_add(checked_multiply(index, 0x10U,
                                       "a RAC1 Moby metal secret-index offset"),
                      0x0cU, "a RAC1 Moby metal secret-index lane"),
          "a RAC1 Moby metal secret index");
      secret_indices.push_back(byte_value(
          class_bytes[static_cast<std::size_t>(secret_offset)]));
    }
  } else if (result.texture_unpack_relative_qwords != 0U ||
             packet.texture_unpack_offset_qwords != 0U) {
    fail("A RAC1 Moby metal packet references a missing texture UNPACK");
  }

  const auto vertex_base = packet.vertex_range.offset;
  constexpr std::uint64_t kMetalHeaderBytes = 0x10U;
  const auto vertex_record_bytes = checked_multiply(
      packet.transfer_vertex_count, kRacMobyVertexRecordBytesV1,
      "the RAC1 Moby metal vertex-record byte size");
  const auto expected_vertex_bytes = checked_add(
      kMetalHeaderBytes, vertex_record_bytes,
      "the RAC1 Moby metal vertex-block byte size");
  if (packet.vertex_range.size != expected_vertex_bytes) {
    fail("A RAC1 Moby metal vertex block is not one header plus its direct "
         "vertex records");
  }
  auto &header = result.vertex_header;
  header.header_range = make_range(vertex_base, kMetalHeaderBytes);
  header.vertex_record_range =
      make_range(vertex_base + kMetalHeaderBytes, vertex_record_bytes);
  for (std::size_t word = 0U; word < header.raw_words.size(); ++word) {
    header.raw_words[word] = read_le32(class_bytes, vertex_base + word * 4U);
  }
  const auto first_layout_bytes = align_up_16(checked_multiply(
      packet.transfer_vertex_count, 4U,
      "the first RAC1 Moby metal layout word"));
  const auto second_layout_bytes = align_up_16(checked_add(
      first_layout_bytes,
      checked_multiply(packet.transfer_vertex_count, 6U,
                       "the second RAC1 Moby metal layout word"),
      "the second RAC1 Moby metal layout word"));
  const auto third_layout_bytes = checked_add(
      second_layout_bytes, first_layout_bytes,
      "the third RAC1 Moby metal layout word");
  if (header.raw_words[0U] != packet.transfer_vertex_count ||
      header.raw_words[1U] != first_layout_bytes ||
      header.raw_words[2U] != second_layout_bytes ||
      header.raw_words[3U] != third_layout_bytes) {
    fail("A RAC1 Moby metal vertex header has an unproven layout");
  }

  require_host_container_size(packet.transfer_vertex_count,
                              result.vertices.max_size(),
                              "The RAC1 Moby metal vertex count");
  require_limit(packet.transfer_vertex_count, limits.max_vertices,
                "The RAC1 Moby metal vertex count");
  result.vertices.reserve(packet.transfer_vertex_count);
  for (std::uint32_t index = 0U; index < packet.transfer_vertex_count;
       ++index) {
    const auto offset = checked_add(
        header.vertex_record_range.offset,
        checked_multiply(index, kRacMobyVertexRecordBytesV1,
                         "a RAC1 Moby metal vertex-record offset"),
        "a RAC1 Moby metal vertex record");
    RacMobyMetalPacketVertexV1 vertex;
    vertex.source_range =
        make_range(offset, kRacMobyVertexRecordBytesV1);
    for (std::size_t axis = 0U; axis < vertex.quantized_position.size();
         ++axis) {
      vertex.quantized_position[axis] =
          read_le_s16(class_bytes, offset + axis * 2U);
      vertex.diagnostic_position[axis] =
          static_cast<float>(vertex.quantized_position[axis]) *
          (class_scale / 1024.0F);
    }
    vertex.normal_azimuth =
        byte_value(class_bytes[static_cast<std::size_t>(offset + 6U)]);
    vertex.normal_elevation =
        byte_value(class_bytes[static_cast<std::size_t>(offset + 7U)]);
    for (std::size_t influence = 0U;
         influence < vertex.joint_indices.size(); ++influence) {
      vertex.joint_indices[influence] = byte_value(class_bytes[
          static_cast<std::size_t>(offset + 8U + influence)]);
      vertex.weight_numerators[influence] = byte_value(class_bytes[
          static_cast<std::size_t>(offset + 12U + influence)]);
    }
    vertex.influence_count =
        byte_value(class_bytes[static_cast<std::size_t>(offset + 11U)]);
    if (vertex.influence_count > vertex.joint_indices.size() ||
        class_bytes[static_cast<std::size_t>(offset + 15U)] != std::byte{0}) {
      fail("A RAC1 Moby metal vertex has an invalid influence envelope");
    }
    std::uint32_t stored_weight_sum = 0U;
    for (std::size_t influence = 0U;
         influence < vertex.joint_indices.size(); ++influence) {
      if (influence < vertex.influence_count) {
        if (vertex.influence_count == 1U) {
          if (vertex.weight_numerators[influence] != 0U) {
            fail("A single-joint RAC1 Moby metal vertex stores an unexpected "
                 "explicit weight");
          }
        } else {
          if (vertex.weight_numerators[influence] == 0U) {
            fail("A blended RAC1 Moby metal vertex has a zero active weight");
          }
          stored_weight_sum += vertex.weight_numerators[influence];
        }
      } else if (vertex.joint_indices[influence] != 0U ||
                 vertex.weight_numerators[influence] != 0U) {
        fail("A RAC1 Moby metal vertex has non-zero unused influences");
      }
    }
    if (vertex.influence_count == 0U) {
      if (stored_weight_sum != 0U) {
        fail("A rigid RAC1 Moby metal vertex stores skin weights");
      }
    } else if (vertex.influence_count > 1U && stored_weight_sum != 256U) {
      fail("A blended RAC1 Moby metal vertex's weights do not sum to 256");
    }
    result.vertices.push_back(vertex);
  }

  std::optional<std::size_t> current_strip;
  std::optional<std::int32_t> current_texture;
  std::uint64_t expanded_strip_indices = 0U;
  std::size_t texture_cursor = 0U;
  bool terminated = false;
  const auto raw_index_begin = result.raw_strip_index_range.offset;
  const auto raw_index_count = result.raw_strip_index_range.size;
  for (std::uint64_t source_index = 0U; source_index < raw_index_count;
       ++source_index) {
    const auto raw = byte_value(class_bytes[static_cast<std::size_t>(
        raw_index_begin + source_index)]);
    const auto signed_index = std::bit_cast<std::int8_t>(raw);
    std::int32_t effective_index = signed_index;
    if (raw == 0U) {
      if (texture_cursor >= secret_indices.size()) {
        fail("A RAC1 Moby metal strip consumes a missing secret index");
      }
      const auto secret = secret_indices[texture_cursor];
      if (secret == 0U) {
        if (!current_strip ||
            result.strips[*current_strip].transfer_vertex_indices.size() <
                3U) {
          fail("A RAC1 Moby metal strip has an invalid termination marker");
        }
        const auto trailing_begin = checked_add(
            raw_index_begin, source_index + 1U,
            "the RAC1 Moby metal strip padding");
        const auto raw_index_end = checked_add(
            raw_index_begin, raw_index_count,
            "the RAC1 Moby metal strip-index end");
        result.strip_terminator_range =
            make_range(raw_index_begin + source_index, 1U);
        result.strip_trailing_padding_range =
            make_range(trailing_begin, raw_index_end - trailing_begin);
        require_zero(class_bytes, trailing_begin, raw_index_end,
                     "The RAC1 Moby metal strip trailing padding");
        auto &indices = result.strips[*current_strip].transfer_vertex_indices;
        indices.resize(indices.size() - 3U);
        expanded_strip_indices -= 3U;
        result.consumed_texture_primitive_count = texture_cursor;
        terminated = true;
        break;
      }
      if (texture_cursor >= result.texture_primitives.size() ||
          (secret & 0x7fU) == 0U) {
        fail("A RAC1 Moby metal texture switch has an invalid secret index");
      }
      effective_index =
          static_cast<std::int32_t>(std::bit_cast<std::int8_t>(secret)) -
          0x80;
      current_texture = result.texture_primitives[texture_cursor].texture_index;
      ++texture_cursor;
    }

    const auto encoded_index =
        static_cast<std::uint32_t>(effective_index) & 0x7fU;
    if (encoded_index == 0U) {
      fail("A RAC1 Moby metal strip contains a zero one-based vertex index");
    }
    const auto transfer_index = encoded_index - 1U;
    if (transfer_index >= packet.transfer_vertex_count) {
      fail("A RAC1 Moby metal strip index exceeds its transfer-vertex table");
    }

    const auto suppress_draw = effective_index <= 0;
    if (suppress_draw) {
      const auto next_is_suppressed =
          source_index + 1U < raw_index_count &&
          std::bit_cast<std::int8_t>(byte_value(class_bytes[
              static_cast<std::size_t>(raw_index_begin + source_index + 1U)])) <=
              0;
      if (next_is_suppressed) {
        result.strips.push_back(
            RacMobyPacketStripV1{current_texture, {}});
        current_strip = result.strips.size() - 1U;
      } else {
        if (!current_strip ||
            result.strips[*current_strip].transfer_vertex_indices.empty()) {
          fail("A RAC1 Moby metal single strip restart has no previous "
               "vertex");
        }
        auto &indices = result.strips[*current_strip].transfer_vertex_indices;
        indices.push_back(indices.back());
        ++expanded_strip_indices;
        require_limit(expanded_strip_indices, limits.max_strip_indices,
                      "The RAC1 Moby metal expanded strip-index count");
      }
    }
    if (!current_strip) {
      fail("A RAC1 Moby metal strip begins without a suppressed drawing "
           "kick");
    }
    result.strips[*current_strip].transfer_vertex_indices.push_back(
        transfer_index);
    ++expanded_strip_indices;
    require_limit(expanded_strip_indices, limits.max_strip_indices,
                  "The RAC1 Moby metal expanded strip-index count");
  }
  if (!terminated) {
    fail("A RAC1 Moby metal strip lacks its secret-index terminator");
  }

  for (const auto &strip : result.strips) {
    for (std::size_t index = 2U; index < strip.transfer_vertex_indices.size();
         ++index) {
      auto a = strip.transfer_vertex_indices[index - 2U];
      auto b = strip.transfer_vertex_indices[index - 1U];
      const auto c = strip.transfer_vertex_indices[index];
      if ((index & 1U) != 0U) {
        std::swap(a, b);
      }
      if (a == b || b == c || a == c) {
        continue;
      }
      require_limit(result.triangles.size() + 1U, limits.max_triangles,
                    "The RAC1 Moby metal triangle count");
      result.triangles.push_back(
          RacMobyPacketTriangleV1{{a, b, c}, strip.texture_index});
    }
  }
  return result;
}

} // namespace openrc
