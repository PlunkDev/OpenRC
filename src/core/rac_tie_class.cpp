#include "openrc/rac_tie_class.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint32_t kPacketControlHeaderBytes = 0x2cU;
constexpr std::uint32_t kTieStripBytes = 0x04U;
constexpr std::uint32_t kTieDinkyVertexBytes = 0x10U;
constexpr std::uint32_t kTieFatVertexBytes = 0x18U;
constexpr std::uint32_t kTiePacketTextureCommandBytes = 0x06U;
constexpr std::uint32_t kTiePacketStripCommandBytes = 0x01U;
constexpr std::uint32_t kTiePacketVertexCommandBytes = 0x03U;
constexpr std::size_t kTiePacketMaterialSlotCount = 4U;

struct PacketEntryV1 {
    std::int32_t data_offset = 0;
    std::uint8_t shader_count = 0U;
    std::uint8_t vertex_offset_qwords = 0U;
    std::uint8_t vertex_size_qwords = 0U;
    std::uint8_t lighting_offset_qwords = 0U;
    std::uint8_t lighting_size_words = 0U;
};

struct StripV1 {
    RacTieRangeV1 source_range;
    std::uint8_t vertex_count = 0U;
    std::uint8_t gif_tag_offset = 0U;
    bool reverse_first_winding = false;
};

struct WorkingVertexV1 {
    RacTieVertexV1 vertex;
};

[[noreturn]] void fail(const std::string& message) {
    throw RacTieClassError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(byte_value(bytes[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(byte_value(bytes[offset + 1U])) << 8U));
}

[[nodiscard]] std::int16_t read_le_i16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return std::bit_cast<std::int16_t>(read_le16(bytes, offset));
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::int32_t read_le_i32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return std::bit_cast<std::int32_t>(read_le32(bytes, offset));
}

[[nodiscard]] float read_le_float(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return std::bit_cast<float>(read_le32(bytes, offset));
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left * right;
}

void require_range(
    const std::uint64_t offset,
    const std::uint64_t size,
    const std::size_t input_size,
    const char* const description) {
    const auto available = static_cast<std::uint64_t>(input_size);
    if (offset > available || size > available - offset) {
        fail(std::string(description) + " exceeds the TIE-class envelope");
    }
}

[[nodiscard]] bool is_zero_range(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const std::uint64_t size) {
    require_range(offset, size, bytes.size(), "A RAC1 TIE zero range");
    for (std::uint64_t index = 0U; index < size; ++index) {
        if (bytes[static_cast<std::size_t>(offset + index)] != std::byte{0}) {
            return false;
        }
    }
    return true;
}

void require_limit(
    const std::uint64_t value,
    const std::uint64_t limit,
    const char* const description) {
    if (value > limit) {
        fail(std::string(description) + " exceeds the caller's limit");
    }
}

void require_append_capacity(
    const std::uint64_t current,
    const std::uint64_t addition,
    const std::uint64_t limit,
    const std::uint64_t host_limit,
    const char* const description) {
    if (current > limit || addition > limit - current || current > host_limit ||
        addition > host_limit - current) {
        fail(std::string(description) + " exceeds an output limit");
    }
}

[[nodiscard]] std::uint32_t checked_u32(
    const std::size_t value,
    const char* const description) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        fail(std::string(description) + " exceeds the 32-bit index domain");
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t decode_material_index(
    const std::int32_t source_offset,
    const std::uint8_t texture_count) {
    if (source_offset < 0 ||
        source_offset % static_cast<std::int32_t>(kRacTieAdGifBytesV1) != 0) {
        fail("A RAC1 TIE packet has an invalid material source offset");
    }
    const auto index = static_cast<std::uint32_t>(source_offset) /
        kRacTieAdGifBytesV1;
    if (index >= texture_count) {
        fail("A RAC1 TIE packet references an absent local material");
    }
    return index;
}

[[nodiscard]] RacTieVertexV1 decode_dinky_vertex(
    const std::span<const std::byte> bytes,
    const std::size_t offset,
    const float scale) {
    RacTieVertexV1 result;
    result.quantized_position = {
        read_le_i16(bytes, offset),
        read_le_i16(bytes, offset + 0x02U),
        read_le_i16(bytes, offset + 0x04U),
    };
    result.gs_packet_write_offset = read_le16(bytes, offset + 0x06U);
    result.stq = {
        read_le16(bytes, offset + 0x08U),
        read_le16(bytes, offset + 0x0aU),
        read_le16(bytes, offset + 0x0cU),
    };
    result.source_range = {offset, kTieDinkyVertexBytes};
    for (std::size_t component = 0U; component < 3U; ++component) {
        result.position[component] =
            static_cast<float>(result.quantized_position[component]) * scale /
            1024.0F;
        if (!std::isfinite(result.position[component])) {
            fail("A RAC1 TIE vertex produces a non-finite position");
        }
    }
    result.texture_coordinate = {
        static_cast<float>(std::bit_cast<std::int16_t>(result.stq[0U])) /
            4096.0F,
        static_cast<float>(std::bit_cast<std::int16_t>(result.stq[1U])) /
            4096.0F,
    };
    return result;
}

[[nodiscard]] RacTieVertexV1 decode_fat_vertex(
    const std::span<const std::byte> bytes,
    const std::size_t offset,
    const float scale) {
    RacTieVertexV1 result;
    result.quantized_morph_delta = {
        read_le_i16(bytes, offset),
        read_le_i16(bytes, offset + 0x02U),
        read_le_i16(bytes, offset + 0x04U),
    };
    result.quantized_position = {
        read_le_i16(bytes, offset + 0x08U),
        read_le_i16(bytes, offset + 0x0aU),
        read_le_i16(bytes, offset + 0x0cU),
    };
    result.gs_packet_write_offset = read_le16(bytes, offset + 0x06U);
    result.stq = {
        read_le16(bytes, offset + 0x10U),
        read_le16(bytes, offset + 0x12U),
        read_le16(bytes, offset + 0x14U),
    };
    result.source_range = {offset, kTieFatVertexBytes};
    for (std::size_t component = 0U; component < 3U; ++component) {
        result.position[component] =
            static_cast<float>(result.quantized_position[component]) * scale /
            1024.0F;
        if (!std::isfinite(result.position[component])) {
            fail("A RAC1 TIE vertex produces a non-finite position");
        }
    }
    result.texture_coordinate = {
        static_cast<float>(std::bit_cast<std::int16_t>(result.stq[0U])) /
            4096.0F,
        static_cast<float>(std::bit_cast<std::int16_t>(result.stq[1U])) /
            4096.0F,
    };
    return result;
}

void append_working_vertex(
    std::vector<WorkingVertexV1>& vertices,
    RacTieVertexV1 vertex,
    const std::uint16_t second_write_offset,
    const RacTieClassLimitsV1 limits) {
    const auto copies = second_write_offset != 0U &&
            second_write_offset != vertex.gs_packet_write_offset
        ? 2U
        : 1U;
    require_append_capacity(
        vertices.size(),
        copies,
        limits.max_output_vertices,
        vertices.max_size(),
        "The expanded RAC1 TIE vertex workspace");
    vertices.push_back(WorkingVertexV1{vertex});
    if (copies == 2U) {
        vertex.gs_packet_write_offset = second_write_offset;
        vertices.push_back(WorkingVertexV1{std::move(vertex)});
    }
}

void append_packet_geometry(
    const std::span<const std::byte> bytes,
    const std::size_t packet_index,
    const std::uint64_t packet_table_offset,
    const std::uint64_t lod_base_offset,
    const PacketEntryV1 entry,
    const std::uint8_t texture_count,
    const float scale,
    const RacTieClassLimitsV1 limits,
    std::uint64_t& total_strip_count,
    std::uint64_t& total_source_vertex_count,
    RacTieClassV1& result) {
    if (entry.data_offset < 0) {
        fail("A RAC1 TIE packet has a negative data offset");
    }
    const auto packet_data_offset = checked_add(
        lod_base_offset,
        static_cast<std::uint32_t>(entry.data_offset),
        "a RAC1 TIE packet data offset");
    require_range(
        packet_data_offset,
        kPacketControlHeaderBytes,
        bytes.size(),
        "A RAC1 TIE packet control header");
    if ((packet_data_offset & 0x0fU) != 0U) {
        fail("A RAC1 TIE packet data offset is not 0x10-aligned");
    }

    std::array<std::int32_t, kTiePacketMaterialSlotCount>
        material_destination_offsets{};
    std::array<std::int32_t, kTiePacketMaterialSlotCount>
        material_source_offsets{};
    for (std::size_t index = 0U; index < kTiePacketMaterialSlotCount; ++index) {
        material_destination_offsets[index] = read_le_i32(
            bytes,
            static_cast<std::size_t>(packet_data_offset) +
                index * sizeof(std::uint32_t));
        material_source_offsets[index] = read_le_i32(
            bytes,
            static_cast<std::size_t>(packet_data_offset) + 0x10U +
                index * sizeof(std::uint32_t));
    }

    const auto strip_count = byte_value(
        bytes[static_cast<std::size_t>(packet_data_offset) + 0x23U]);
    require_append_capacity(
        total_strip_count,
        strip_count,
        limits.max_strips,
        result.primitives.max_size(),
        "The RAC1 TIE strip count");
    total_strip_count += strip_count;
    const auto strip_bytes = checked_multiply(
        strip_count, kTieStripBytes, "a RAC1 TIE strip table size");
    const auto control_bytes = checked_add(
        kPacketControlHeaderBytes,
        strip_bytes,
        "a RAC1 TIE packet control size");
    require_range(
        packet_data_offset,
        control_bytes,
        bytes.size(),
        "A RAC1 TIE packet control block");
    std::vector<StripV1> strips;
    strips.reserve(strip_count);
    for (std::size_t strip_index = 0U; strip_index < strip_count;
         ++strip_index) {
        const auto offset = static_cast<std::size_t>(packet_data_offset) +
            kPacketControlHeaderBytes + strip_index * kTieStripBytes;
        if (byte_value(bytes[offset + 1U]) != 0U) {
            fail("A RAC1 TIE strip has a non-zero reserved byte");
        }
        strips.push_back(StripV1{
            {offset, kTieStripBytes},
            byte_value(bytes[offset]),
            byte_value(bytes[offset + 2U]),
            byte_value(bytes[offset + 3U]) != 0U,
        });
    }

    const auto vertex_relative_offset = checked_multiply(
        entry.vertex_offset_qwords,
        0x10U,
        "a RAC1 TIE vertex-buffer offset");
    const auto vertex_bytes = checked_multiply(
        entry.vertex_size_qwords,
        0x10U,
        "a RAC1 TIE vertex-buffer size");
    if (vertex_relative_offset < control_bytes || vertex_bytes == 0U) {
        fail("A RAC1 TIE packet has an invalid vertex-buffer envelope");
    }
    const auto vertex_offset = checked_add(
        packet_data_offset,
        vertex_relative_offset,
        "a RAC1 TIE vertex-buffer offset");
    require_range(
        vertex_offset,
        vertex_bytes,
        bytes.size(),
        "A RAC1 TIE vertex buffer");

    const auto dinky_size_plus_four = byte_value(
        bytes[static_cast<std::size_t>(packet_data_offset) + 0x28U]);
    if (dinky_size_plus_four < 4U ||
        ((dinky_size_plus_four - 4U) & 1U) != 0U) {
        fail("A RAC1 TIE packet has an invalid dinky-vertex count");
    }
    const auto dinky_count =
        static_cast<std::uint32_t>((dinky_size_plus_four - 4U) / 2U);
    const auto dinky_bytes = checked_multiply(
        dinky_count,
        kTieDinkyVertexBytes,
        "the RAC1 TIE dinky-vertex bytes");
    if (dinky_bytes > vertex_bytes) {
        fail("A RAC1 TIE packet has an invalid dinky/fat vertex split (dinky=" +
             std::to_string(dinky_count) + ", bytes=" +
             std::to_string(vertex_bytes) + ")");
    }
    const auto remaining_vertex_bytes = vertex_bytes - dinky_bytes;
    const auto fat_count = static_cast<std::uint32_t>(
        remaining_vertex_bytes / kTieFatVertexBytes);
    const auto vertex_padding_bytes =
        remaining_vertex_bytes % kTieFatVertexBytes;
    const auto vertex_padding_offset = checked_add(
        vertex_offset,
        checked_add(
            dinky_bytes,
            checked_multiply(
                fat_count,
                kTieFatVertexBytes,
                "the RAC1 TIE fat-vertex bytes"),
            "the RAC1 TIE used vertex bytes"),
        "the RAC1 TIE vertex padding offset");
    if (!is_zero_range(
            bytes, vertex_padding_offset, vertex_padding_bytes)) {
        fail("A RAC1 TIE packet has non-zero vertex-buffer padding");
    }
    const auto source_vertex_count = checked_add(
        dinky_count, fat_count, "a RAC1 TIE source-vertex count");
    require_append_capacity(
        total_source_vertex_count,
        source_vertex_count,
        limits.max_source_vertices,
        std::numeric_limits<std::size_t>::max(),
        "The RAC1 TIE source-vertex count");
    total_source_vertex_count += source_vertex_count;

    RacTieRangeV1 lighting_indices_range;
    RacTieRangeV1 secondary_lighting_indices_range;
    const auto has_lighting_indices = entry.lighting_offset_qwords != 0U ||
        entry.lighting_size_words != 0U;
    const auto aligned_dinky_indices = (dinky_count + 3U) & ~3U;
    if (has_lighting_indices) {
        // EE 237cd4..237d28 uploads two independently aligned streams.
        // Each dinky uses one byte; each fat uses {C0,C1,C2,0xff}.
        const auto index_bytes = aligned_dinky_indices + 4U * fat_count;
        if (entry.lighting_offset_qwords == 0U ||
            entry.lighting_size_words == 0U ||
            4U * entry.lighting_size_words != index_bytes) {
            fail("A RAC1 TIE packet has an invalid lighting-index count");
        }
        lighting_indices_range = {
            checked_add(packet_data_offset,
                16U * entry.lighting_offset_qwords,
                "a RAC1 TIE lighting-index offset"),
            index_bytes,
        };
        secondary_lighting_indices_range = {
            checked_add(lighting_indices_range.offset,
                (index_bytes + 15U) & ~15U,
                "a RAC1 TIE secondary lighting-index offset"),
            index_bytes,
        };
        require_range(lighting_indices_range.offset, index_bytes,
            bytes.size(), "A RAC1 TIE lighting-index stream");
        require_range(secondary_lighting_indices_range.offset, index_bytes,
            bytes.size(), "A RAC1 TIE secondary lighting-index stream");
        for (std::uint32_t index = 0U; index < index_bytes; ++index) {
            const auto primary = byte_value(bytes[static_cast<std::size_t>(
                lighting_indices_range.offset + index)]);
            const auto secondary = byte_value(bytes[static_cast<std::size_t>(
                secondary_lighting_indices_range.offset + index)]);
            const auto dinky_padding = index >= dinky_count &&
                index < aligned_dinky_indices;
            const auto fat_sentinel = index >= aligned_dinky_indices &&
                (index - aligned_dinky_indices) % 4U == 3U;
            if (dinky_padding || fat_sentinel) {
                const auto expected = fat_sentinel ? 0xffU : 0U;
                if (primary != expected || secondary != expected) {
                    fail("A RAC1 TIE lighting-index stream has invalid padding");
                }
            } else if (primary >= 64U || secondary != primary + 64U) {
                fail("A RAC1 TIE lighting index or secondary mapping is invalid");
            }
        }
    }

    std::vector<WorkingVertexV1> working_vertices;
    const auto maximum_expanded = checked_multiply(
        source_vertex_count,
        2U,
        "a RAC1 TIE expanded-vertex count");
    require_limit(
        maximum_expanded,
        limits.max_output_vertices,
        "A RAC1 TIE expanded-vertex count");
    if (maximum_expanded > working_vertices.max_size()) {
        fail("A RAC1 TIE expanded-vertex count exceeds its host container");
    }
    working_vertices.reserve(static_cast<std::size_t>(maximum_expanded));
    for (std::size_t index = 0U; index < dinky_count; ++index) {
        const auto offset = static_cast<std::size_t>(vertex_offset) +
            index * kTieDinkyVertexBytes;
        auto vertex = decode_dinky_vertex(bytes, offset, scale);
        if (has_lighting_indices) {
            vertex.lighting_palette_index_count = 1U;
            vertex.lighting_palette_indices[0U] = byte_value(bytes[
                static_cast<std::size_t>(lighting_indices_range.offset) + index]);
        }
        const auto second_write_offset = read_le16(bytes, offset + 0x0eU);
        append_working_vertex(
            working_vertices,
            std::move(vertex),
            second_write_offset,
            limits);
    }
    for (std::size_t index = 0U; index < fat_count; ++index) {
        const auto offset = static_cast<std::size_t>(
            vertex_offset + dinky_bytes) + index * kTieFatVertexBytes;
        auto vertex = decode_fat_vertex(bytes, offset, scale);
        if (has_lighting_indices) {
            vertex.lighting_palette_index_count = 3U;
            for (std::size_t lane = 0U; lane < 3U; ++lane) {
                vertex.lighting_palette_indices[lane] = byte_value(bytes[
                    static_cast<std::size_t>(lighting_indices_range.offset) +
                    aligned_dinky_indices + 4U * index + lane]);
            }
        }
        const auto second_write_offset = read_le16(bytes, offset + 0x16U);
        append_working_vertex(
            working_vertices,
            std::move(vertex),
            second_write_offset,
            limits);
    }
    std::stable_sort(
        working_vertices.begin(),
        working_vertices.end(),
        [](const WorkingVertexV1& left, const WorkingVertexV1& right) {
            return left.vertex.gs_packet_write_offset <
                right.vertex.gs_packet_write_offset;
        });
    const auto raw_expanded_count = working_vertices.size();
    working_vertices.erase(
        std::unique(
            working_vertices.begin(),
            working_vertices.end(),
            [](const WorkingVertexV1& left, const WorkingVertexV1& right) {
                return left.vertex.gs_packet_write_offset ==
                    right.vertex.gs_packet_write_offset;
            }),
        working_vertices.end());
    if (working_vertices.empty()) {
        fail("A RAC1 TIE packet has no uniquely addressed vertices");
    }
    require_append_capacity(
        result.vertices.size(),
        working_vertices.size(),
        limits.max_output_vertices,
        result.vertices.max_size(),
        "The assembled RAC1 TIE vertex count");

    RacTiePacketV1 packet;
    packet.table_entry_range = {
        packet_table_offset + packet_index * kRacTiePacketEntryBytesV1,
        kRacTiePacketEntryBytesV1,
    };
    packet.data_prefix_range = {packet_data_offset, vertex_relative_offset};
    packet.vertex_data_range = {vertex_offset, vertex_bytes};
    packet.vertex_padding_range = {
        vertex_padding_offset,
        vertex_padding_bytes,
    };
    packet.lighting_indices_range = lighting_indices_range;
    packet.secondary_lighting_indices_range = secondary_lighting_indices_range;
    packet.data_offset = static_cast<std::uint32_t>(entry.data_offset);
    packet.primitive_begin = checked_u32(
        result.primitives.size(), "A RAC1 TIE packet primitive begin");
    packet.vertex_begin =
        checked_u32(result.vertices.size(), "A RAC1 TIE packet vertex begin");
    packet.triangle_begin = checked_u32(
        result.triangles.size(), "A RAC1 TIE packet triangle begin");
    packet.source_dinky_vertex_count = dinky_count;
    packet.source_fat_vertex_count = fat_count;
    packet.discarded_duplicate_write_count = checked_u32(
        raw_expanded_count - working_vertices.size(),
        "A RAC1 TIE discarded-duplicate count");

    auto material_index =
        decode_material_index(material_source_offsets[0U], texture_count);
    std::size_t next_strip = 0U;
    std::size_t next_vertex = 0U;
    std::size_t next_material = 1U;
    std::uint32_t next_gs_offset = kTiePacketTextureCommandBytes;
    while (next_strip < strips.size() || next_vertex < working_vertices.size()) {
        if (next_strip < strips.size() &&
            strips[next_strip].gif_tag_offset == next_gs_offset) {
            const auto& strip = strips[next_strip];
            require_append_capacity(
                result.primitives.size(),
                1U,
                limits.max_strips,
                result.primitives.max_size(),
                "The assembled RAC1 TIE primitive count");
            result.primitives.push_back(RacTiePrimitiveV1{
                strip.source_range,
                material_index,
                checked_u32(
                    result.vertices.size(),
                    "A RAC1 TIE primitive vertex begin"),
                0U,
                0U,
                0U,
                strip.vertex_count,
                strip.gif_tag_offset,
                strip.reverse_first_winding,
            });
            ++next_strip;
            next_gs_offset = static_cast<std::uint32_t>(checked_add(
                next_gs_offset,
                kTiePacketStripCommandBytes,
                "a RAC1 TIE GS write offset"));
            continue;
        }
        if (next_vertex < working_vertices.size() &&
            working_vertices[next_vertex].vertex.gs_packet_write_offset ==
                next_gs_offset) {
            if (result.primitives.size() == packet.primitive_begin) {
                fail("A RAC1 TIE vertex appears before its strip tag");
            }
            result.vertices.push_back(
                std::move(working_vertices[next_vertex].vertex));
            ++result.primitives.back().vertex_count;
            ++next_vertex;
            next_gs_offset = static_cast<std::uint32_t>(checked_add(
                next_gs_offset,
                kTiePacketVertexCommandBytes,
                "a RAC1 TIE GS write offset"));
            continue;
        }
        if (next_material < material_source_offsets.size() &&
            material_destination_offsets[next_material - 1U] >= 0 &&
            static_cast<std::uint32_t>(
                material_destination_offsets[next_material - 1U]) ==
                next_gs_offset) {
            material_index = decode_material_index(
                material_source_offsets[next_material], texture_count);
            ++next_material;
            next_gs_offset = static_cast<std::uint32_t>(checked_add(
                next_gs_offset,
                kTiePacketTextureCommandBytes,
                "a RAC1 TIE GS write offset"));
            continue;
        }
        fail("A RAC1 TIE packet has an impossible GS write schedule");
    }

    const auto packet_primitive_end = result.primitives.size();
    for (std::size_t primitive_index = packet.primitive_begin;
         primitive_index < packet_primitive_end;
         ++primitive_index) {
        auto& primitive = result.primitives[primitive_index];
        if (primitive.vertex_count != primitive.encoded_vertex_count) {
            fail("A RAC1 TIE strip vertex count disagrees with its GS writes");
        }
        primitive.triangle_begin = checked_u32(
            result.triangles.size(), "A RAC1 TIE primitive triangle begin");
        const auto triangle_count = primitive.vertex_count >= 3U
            ? primitive.vertex_count - 2U
            : 0U;
        require_append_capacity(
            result.triangles.size(),
            triangle_count,
            limits.max_output_triangles,
            result.triangles.max_size(),
            "The assembled RAC1 TIE triangle count");
        for (std::uint32_t index = 2U; index < primitive.vertex_count;
             ++index) {
            const auto first = primitive.vertex_begin + index - 2U;
            const auto middle = primitive.vertex_begin + index - 1U;
            const auto last = primitive.vertex_begin + index;
            const auto forward =
                (index % 2U) ==
                static_cast<std::uint32_t>(primitive.reverse_first_winding);
            result.triangles.push_back(RacTieTriangleV1{
                forward
                    ? std::array<std::uint32_t, 3U>{first, middle, last}
                    : std::array<std::uint32_t, 3U>{last, middle, first},
                primitive.local_texture_index,
                checked_u32(packet_index, "A RAC1 TIE packet index"),
            });
        }
        primitive.triangle_count = triangle_count;
    }
    packet.primitive_count = checked_u32(
        packet_primitive_end - packet.primitive_begin,
        "A RAC1 TIE packet primitive count");
    packet.vertex_count = checked_u32(
        result.vertices.size() - packet.vertex_begin,
        "A RAC1 TIE packet vertex count");
    packet.triangle_count = checked_u32(
        result.triangles.size() - packet.triangle_begin,
        "A RAC1 TIE packet triangle count");
    result.packets.push_back(packet);
}

} // namespace

RacTieClassV1 parse_rac_tie_lod_class_v1(
    const std::span<const std::byte> bytes,
    const RacTieClassLimitsV1 limits,
    const std::uint8_t selected_lod) {
    if (selected_lod > 2U) {
        fail("A RAC1 TIE selected LOD is outside the original table domain");
    }
    if (limits.max_input_bytes == 0U || limits.max_packets == 0U ||
        limits.max_strips == 0U || limits.max_source_vertices == 0U ||
        limits.max_output_vertices == 0U ||
        limits.max_output_triangles == 0U || limits.max_materials == 0U) {
        fail("RAC1 TIE-class caller limits must all be non-zero");
    }
    if (bytes.size() > limits.max_input_bytes) {
        fail("The RAC1 TIE class exceeds the caller's input-byte limit");
    }
    if (bytes.size() < kRacTieClassHeaderBytesV1) {
        fail("The RAC1 TIE class has a truncated header");
    }

    RacTieClassV1 result;
    result.input_bytes = bytes.size();
    result.header_range = {0U, kRacTieClassHeaderBytesV1};
    for (std::size_t lod = 0U; lod < result.lod_packet_table_offsets.size();
         ++lod) {
        result.lod_packet_table_offsets[lod] =
            read_le32(bytes, lod * sizeof(std::uint32_t));
        result.lod_packet_counts[lod] = byte_value(bytes[0x20U + lod]);
        if (result.lod_packet_counts[lod] != 0U) {
            if (result.lod_packet_table_offsets[lod] == 0U ||
                (result.lod_packet_table_offsets[lod] & 0x0fU) != 0U) {
                fail("A RAC1 TIE LOD has an invalid packet-table offset");
            }
            const auto table_bytes = checked_multiply(
                result.lod_packet_counts[lod],
                kRacTiePacketEntryBytesV1,
                "a RAC1 TIE packet-table size");
            require_range(
                result.lod_packet_table_offsets[lod],
                table_bytes,
                bytes.size(),
                "A RAC1 TIE packet table");
        }
    }
    result.texture_count = byte_value(bytes[0x23U]);
    for (std::size_t lod = 0U; lod < 3U; ++lod) {
        result.lod_threshold_bits[lod] = read_le32(bytes, 0x10U + lod * 4U);
    }
    result.mode_bits = read_le16(bytes, 0x24U);
    if (result.texture_count == 0U ||
        result.texture_count > limits.max_materials) {
        fail("The RAC1 TIE class has an invalid material count");
    }
    result.ad_gif_offset = read_le32(bytes, 0x2cU);
    const auto ad_gif_bytes = checked_multiply(
        result.texture_count,
        kRacTieAdGifBytesV1,
        "the RAC1 TIE material table size");
    if ((result.ad_gif_offset & 0x0fU) != 0U) {
        fail("The RAC1 TIE material table is not 0x10-aligned");
    }
    require_range(
        result.ad_gif_offset,
        ad_gif_bytes,
        bytes.size(),
        "The RAC1 TIE material table");
    result.ad_gif_range = {result.ad_gif_offset, ad_gif_bytes};
    for (std::size_t component = 0U;
         component < result.bounding_sphere.size();
         ++component) {
        const auto offset = 0x30U + component * sizeof(std::uint32_t);
        result.bounding_sphere_bits[component] = read_le32(bytes, offset);
        result.bounding_sphere[component] = read_le_float(bytes, offset);
        if (!std::isfinite(result.bounding_sphere[component])) {
            fail("The RAC1 TIE class has a non-finite bounding sphere");
        }
    }
    result.scale_bits = read_le32(bytes, 0x40U);
    result.scale = read_le_float(bytes, 0x40U);
    if (!std::isfinite(result.scale) || result.scale <= 0.0F) {
        fail("The RAC1 TIE class has an invalid scale");
    }

    result.selected_lod = selected_lod;
    result.high_lod_packet_table_range = {
        result.lod_packet_table_offsets[0U],
        static_cast<std::uint64_t>(result.lod_packet_counts[0U]) *
            kRacTiePacketEntryBytesV1,
    };
    const auto packet_count = result.lod_packet_counts[selected_lod];
    require_limit(packet_count, limits.max_packets, "The RAC1 TIE packet count");
    const auto packet_table_offset = result.lod_packet_table_offsets[selected_lod];
    const auto packet_table_bytes = checked_multiply(
        packet_count,
        kRacTiePacketEntryBytesV1,
        "the selected RAC1 TIE packet-table size");
    result.selected_lod_packet_table_range = {
        packet_table_offset,
        packet_table_bytes,
    };
    if (packet_count == 0U) {
        return result;
    }
    result.packets.reserve(packet_count);

    std::uint64_t total_strip_count = 0U;
    std::uint64_t total_source_vertex_count = 0U;
    for (std::size_t packet_index = 0U; packet_index < packet_count;
         ++packet_index) {
        const auto entry_offset = static_cast<std::size_t>(packet_table_offset) +
            packet_index * kRacTiePacketEntryBytesV1;
        const PacketEntryV1 entry{
            read_le_i32(bytes, entry_offset),
            byte_value(bytes[entry_offset + 0x04U]),
            byte_value(bytes[entry_offset + 0x08U]),
            byte_value(bytes[entry_offset + 0x09U]),
            byte_value(bytes[entry_offset + 0x0aU]),
            byte_value(bytes[entry_offset + 0x0bU]),
        };
        append_packet_geometry(
            bytes,
            packet_index,
            packet_table_offset,
            packet_table_offset,
            entry,
            result.texture_count,
            result.scale,
            limits,
            total_strip_count,
            total_source_vertex_count,
            result);
    }
    return result;
}

RacTieClassV1 parse_rac_tie_class_v1(
    const std::span<const std::byte> bytes,
    const RacTieClassLimitsV1 limits) {
    return parse_rac_tie_lod_class_v1(bytes, limits, 0U);
}

} // namespace openrc
