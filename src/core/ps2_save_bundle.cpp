#include "openrc/ps2_save_bundle.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace openrc {
namespace {

constexpr std::size_t kOuterPairSize = 8;
constexpr std::size_t kIconSysTitleOffset = 0xc0;
constexpr std::array<std::size_t, kPs2IconFilenameCount>
    kIconFilenameOffsets{0x104, 0x144, 0x184};
constexpr std::size_t kIconSysReservedTailOffset = 0x1c4;
constexpr std::uint32_t kTlvSentinelKey =
    std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t kSaveTemplateHeaderSize = 8;
constexpr std::size_t kSaveRecordHeaderSize = 8;
constexpr std::size_t kTlvHeaderSize = 8;
constexpr std::uint64_t kTlvAlignment = 4;

static_assert(
    kPs2SaveBundleHeaderSize ==
        kPs2SaveBundleRecordCount * kOuterPairSize,
    "The save-bundle header must contain exactly three offset/size pairs");
static_assert(
    kIconSysReservedTailOffset + kPs2IconSysReservedTailSize ==
        kPs2IconSysSize,
    "The icon.sys reserved tail must end at the fixed record size");

struct TlvCounters {
    std::uint64_t entries = 0;
    std::uint64_t payload_bytes = 0;
};

[[noreturn]] void fail(const std::string& message) {
    throw Ps2SaveBundleError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(byte_value(bytes[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(byte_value(bytes[offset + 1U])) << 8U);
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

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string& description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail("Integer overflow while calculating " + description);
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string& description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail("Integer overflow while calculating " + description);
    }
    return left * right;
}

[[nodiscard]] std::size_t host_size(
    const std::uint64_t value,
    const std::string& description) {
    if (value > std::numeric_limits<std::size_t>::max()) {
        fail(description + " exceeds the host container limit");
    }
    return static_cast<std::size_t>(value);
}

void require_range(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const std::uint64_t size,
    const std::string& description) {
    const auto available = static_cast<std::uint64_t>(bytes.size());
    if (offset > available || size > available - offset) {
        fail(description + " points outside its containing record");
    }
}

void require_zero_bytes(
    const std::span<const std::byte> bytes,
    const std::string& description) {
    if (std::any_of(
            bytes.begin(),
            bytes.end(),
            [](const std::byte value) { return value != std::byte{0}; })) {
        fail(description + " contains non-zero bytes");
    }
}

template <std::size_t Size>
void copy_bytes(
    const std::span<const std::byte> source,
    const std::size_t offset,
    std::array<std::byte, Size>& destination) {
    for (std::size_t index = 0; index < Size; ++index) {
        destination[index] = source[offset + index];
    }
}

[[nodiscard]] std::string read_bounded_c_string(
    const std::span<const std::byte> bytes,
    const std::size_t offset,
    const std::size_t field_size,
    const std::string& description) {
    const auto field = bytes.subspan(offset, field_size);
    const auto terminator = std::find(field.begin(), field.end(), std::byte{0});
    if (terminator == field.end()) {
        fail(description + " does not contain a NUL terminator");
    }

    std::string result;
    result.reserve(static_cast<std::size_t>(terminator - field.begin()));
    for (auto current = field.begin(); current != terminator; ++current) {
        result.push_back(static_cast<char>(byte_value(*current)));
    }
    return result;
}

[[nodiscard]] Ps2IconSys parse_icon_sys(
    const std::span<const std::byte> bytes) {
    if (bytes.size() != kPs2IconSysSize) {
        fail("The icon.sys record does not have its fixed 0x3c4-byte size");
    }
    if (byte_value(bytes[0]) != static_cast<std::uint8_t>('P') ||
        byte_value(bytes[1]) != static_cast<std::uint8_t>('S') ||
        byte_value(bytes[2]) != static_cast<std::uint8_t>('2') ||
        byte_value(bytes[3]) != static_cast<std::uint8_t>('D')) {
        fail("The first save-bundle record does not have a PS2D signature");
    }

    Ps2IconSys result;
    result.reserved_halfword = read_le16(bytes, 0x04);
    result.second_line_offset = read_le16(bytes, 0x06);
    result.reserved_word = read_le32(bytes, 0x08);
    result.background_transparency = read_le32(bytes, 0x0c);
    if (result.reserved_halfword != 0U || result.reserved_word != 0U) {
        fail("The icon.sys fixed reserved header fields are non-zero");
    }
    if (result.second_line_offset > kPs2IconTitleSize) {
        fail("The icon.sys second-line offset lies outside its title field");
    }

    for (std::size_t index = 0; index < result.background_fields.size(); ++index) {
        result.background_fields[index] =
            read_le32(bytes, 0x10U + index * sizeof(std::uint32_t));
    }
    for (std::size_t index = 0; index < result.light_direction_bits.size(); ++index) {
        result.light_direction_bits[index] =
            read_le32(bytes, 0x50U + index * sizeof(std::uint32_t));
    }
    for (std::size_t index = 0; index < result.light_color_bits.size(); ++index) {
        result.light_color_bits[index] =
            read_le32(bytes, 0x80U + index * sizeof(std::uint32_t));
    }
    for (std::size_t index = 0; index < result.ambient_light_bits.size(); ++index) {
        result.ambient_light_bits[index] =
            read_le32(bytes, 0xb0U + index * sizeof(std::uint32_t));
    }

    copy_bytes(bytes, kIconSysTitleOffset, result.title_bytes);
    for (std::size_t index = 0; index < result.icon_filenames.size(); ++index) {
        result.icon_filenames[index] = read_bounded_c_string(
            bytes,
            kIconFilenameOffsets[index],
            kPs2IconFilenameFieldSize,
            "icon.sys filename field " + std::to_string(index));
    }

    copy_bytes(bytes, kIconSysReservedTailOffset, result.reserved_tail);
    require_zero_bytes(result.reserved_tail, "The icon.sys reserved tail");
    return result;
}

[[nodiscard]] Ps2MemoryCardIcon parse_icon_model(
    const std::span<const std::byte> bytes,
    const Ps2SaveBundleLimits limits) {
    constexpr auto minimum_size =
        static_cast<std::uint64_t>(kPs2MemoryCardIconHeaderSize) +
        kPs2MemoryCardIconAnimationSize +
        kPs2MemoryCardIconTextureSize;
    if (bytes.size() < minimum_size) {
        fail("The memory-card icon record is too small");
    }

    Ps2MemoryCardIcon result;
    result.version = read_le32(bytes, 0x00);
    result.shape_count = read_le32(bytes, 0x04);
    result.texture_type = read_le32(bytes, 0x08);
    result.opaque_float_bits = read_le32(bytes, 0x0c);
    result.vertex_count = read_le32(bytes, 0x10);

    if (result.version != kPs2MemoryCardIconVersion) {
        fail("The memory-card icon has an unsupported version");
    }
    if (result.shape_count != kPs2MemoryCardIconShapeCount) {
        fail("The memory-card icon does not have the supported shape count");
    }
    if (result.texture_type != kPs2MemoryCardIconTextureType) {
        fail("The memory-card icon does not have the supported texture type");
    }
    if (result.vertex_count > limits.max_vertex_count) {
        fail("The memory-card icon vertex count exceeds the caller's limit");
    }

    const auto vertex_bytes = checked_multiply(
        result.vertex_count,
        kPs2MemoryCardIconVertexSize,
        "memory-card icon vertex bytes");
    auto expected_size = checked_add(
        kPs2MemoryCardIconHeaderSize,
        vertex_bytes,
        "memory-card icon size");
    expected_size = checked_add(
        expected_size,
        kPs2MemoryCardIconAnimationSize,
        "memory-card icon size");
    expected_size = checked_add(
        expected_size,
        kPs2MemoryCardIconTextureSize,
        "memory-card icon size");
    if (expected_size != bytes.size()) {
        fail("The memory-card icon size does not match its vertex count");
    }

    result.vertices.reserve(host_size(result.vertex_count, "Vertex count"));
    for (std::uint32_t vertex_index = 0;
         vertex_index < result.vertex_count;
         ++vertex_index) {
        const auto vertex_offset =
            static_cast<std::size_t>(kPs2MemoryCardIconHeaderSize) +
            static_cast<std::size_t>(vertex_index) *
                kPs2MemoryCardIconVertexSize;
        Ps2MemoryCardIconVertex vertex;
        for (std::size_t field_index = 0;
             field_index < vertex.signed_fields.size();
             ++field_index) {
            vertex.signed_fields[field_index] =
                read_le_i16(bytes, vertex_offset + field_index * 2U);
        }
        for (std::size_t channel = 0; channel < vertex.rgba.size(); ++channel) {
            vertex.rgba[channel] =
                byte_value(bytes[vertex_offset + 20U + channel]);
        }
        result.vertices.push_back(vertex);
    }

    const auto animation_offset = host_size(
        checked_add(
            kPs2MemoryCardIconHeaderSize,
            vertex_bytes,
            "memory-card icon animation offset"),
        "Memory-card icon animation offset");
    copy_bytes(bytes, animation_offset, result.animation_bytes);

    const auto texture_offset = animation_offset +
        static_cast<std::size_t>(kPs2MemoryCardIconAnimationSize);
    const auto texture = bytes.subspan(
        texture_offset,
        static_cast<std::size_t>(kPs2MemoryCardIconTextureSize));
    result.texture_bytes.assign(texture.begin(), texture.end());
    return result;
}

[[nodiscard]] Ps2SaveTlvRecord parse_tlv_record(
    const std::span<const std::byte> bytes,
    const std::uint32_t record_offset,
    const Ps2SaveBundleLimits limits,
    TlvCounters& counters,
    const std::string& description) {
    if (bytes.size() < kSaveRecordHeaderSize + kTlvHeaderSize) {
        fail(description + " is too small to contain its header and sentinel");
    }
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail(description + " exceeds its 32-bit size field");
    }

    Ps2SaveTlvRecord result;
    result.offset = record_offset;
    result.size = static_cast<std::uint32_t>(bytes.size());
    result.declared_stream_bytes = read_le32(bytes, 0x00);
    result.opaque_kind = read_le32(bytes, 0x04);
    if (result.declared_stream_bytes != bytes.size() - kSaveRecordHeaderSize) {
        fail(description + " stream size does not equal record_size - 8");
    }

    std::size_t position = kSaveRecordHeaderSize;
    bool found_sentinel = false;
    while (position < bytes.size()) {
        if (bytes.size() - position < kTlvHeaderSize) {
            fail(description + " ends in a partial TLV header");
        }

        const auto key = read_le32(bytes, position);
        const auto payload_size = read_le32(bytes, position + 4U);
        if (key == kTlvSentinelKey) {
            if (payload_size != 0U) {
                fail(description + " has a sentinel with a non-zero size");
            }
            if (position + kTlvHeaderSize != bytes.size()) {
                fail(description + " has bytes after its TLV sentinel");
            }
            found_sentinel = true;
            position += kTlvHeaderSize;
            break;
        }

        if (counters.entries >= limits.max_tlv_entry_count) {
            fail("The total TLV entry count exceeds the caller's limit");
        }
        ++counters.entries;
        if (payload_size >
            limits.max_tlv_payload_bytes - counters.payload_bytes) {
            fail("The total TLV payload size exceeds the caller's limit");
        }
        counters.payload_bytes += payload_size;

        const auto payload_offset = position + kTlvHeaderSize;
        const auto payload_end = checked_add(
            payload_offset,
            payload_size,
            description + " TLV payload end");
        if (payload_end > bytes.size()) {
            fail(description + " has a TLV payload outside the record");
        }
        const auto aligned_end = checked_add(
            payload_end,
            kTlvAlignment - 1U,
            description + " TLV alignment") &
            ~(kTlvAlignment - 1U);
        if (aligned_end > bytes.size()) {
            fail(description + " has truncated TLV alignment padding");
        }

        const auto payload = bytes.subspan(
            payload_offset,
            static_cast<std::size_t>(payload_size));
        Ps2SaveTlvEntry entry;
        entry.key = key;
        entry.payload.assign(payload.begin(), payload.end());
        result.entries.push_back(std::move(entry));

        const auto padding_size = aligned_end - payload_end;
        const auto padding = bytes.subspan(
            host_size(payload_end, "TLV padding offset"),
            host_size(padding_size, "TLV padding size"));
        require_zero_bytes(padding, description + " TLV alignment padding");
        result.alignment_padding_bytes += padding_size;
        position = host_size(aligned_end, "Aligned TLV end");
    }

    if (!found_sentinel || position != bytes.size()) {
        fail(description + " does not end with an exact FFFFFFFF/0 sentinel");
    }
    return result;
}

[[nodiscard]] Ps2SaveTemplate parse_save_template(
    const std::span<const std::byte> bytes,
    const Ps2SaveBundleLimits limits) {
    if (bytes.size() < kSaveTemplateHeaderSize) {
        fail("The save-template record is too small to contain its size header");
    }
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail("The save-template record exceeds its 32-bit address space");
    }

    Ps2SaveTemplate result;
    result.primary_record_size = read_le32(bytes, 0x00);
    result.repeated_record_size = read_le32(bytes, 0x04);
    if (result.repeated_record_size == 0U) {
        fail("The save-template repeated-record size is zero");
    }

    require_range(
        bytes,
        kSaveTemplateHeaderSize,
        result.primary_record_size,
        "The save-template primary record");
    const auto repeated_offset = checked_add(
        kSaveTemplateHeaderSize,
        result.primary_record_size,
        "save-template repeated-record offset");
    const auto tail_size =
        static_cast<std::uint64_t>(bytes.size()) - repeated_offset;
    if (tail_size % result.repeated_record_size != 0U) {
        fail("The save-template tail is not divisible by its repeated-record size");
    }

    const auto repeated_count = tail_size / result.repeated_record_size;
    if (repeated_count > limits.max_repeated_record_count) {
        fail("The save-template repeated-record count exceeds the caller's limit");
    }

    TlvCounters counters;
    const auto primary_bytes = bytes.subspan(
        kSaveTemplateHeaderSize,
        result.primary_record_size);
    result.primary_record = parse_tlv_record(
        primary_bytes,
        kSaveTemplateHeaderSize,
        limits,
        counters,
        "The save-template primary record");

    result.repeated_records.reserve(
        host_size(repeated_count, "Repeated-record count"));
    for (std::uint64_t index = 0; index < repeated_count; ++index) {
        const auto offset = checked_add(
            repeated_offset,
            checked_multiply(
                index,
                result.repeated_record_size,
                "save-template repeated-record offset"),
            "save-template repeated-record offset");
        const auto record_bytes = bytes.subspan(
            host_size(offset, "Repeated-record offset"),
            result.repeated_record_size);
        result.repeated_records.push_back(parse_tlv_record(
            record_bytes,
            static_cast<std::uint32_t>(offset),
            limits,
            counters,
            "Save-template repeated record " + std::to_string(index)));
    }
    return result;
}

} // namespace

Ps2SaveBundle parse_ps2_save_bundle(
    const std::span<const std::byte> bytes,
    const Ps2SaveBundleLimits limits) {
    const auto input_size = static_cast<std::uint64_t>(bytes.size());
    if (input_size > limits.max_input_bytes) {
        fail("The PS2 save bundle exceeds the caller's input-size limit");
    }
    if (bytes.size() < kPs2SaveBundleHeaderSize) {
        fail("The input is too small to contain the PS2 save-bundle header");
    }

    Ps2SaveBundle result;
    result.input_bytes = input_size;
    std::uint64_t expected_offset = kPs2SaveBundleHeaderSize;
    for (std::size_t index = 0; index < result.records.size(); ++index) {
        const auto pair_offset = index * kOuterPairSize;
        const auto record_offset = read_le32(bytes, pair_offset);
        const auto record_size = read_le32(bytes, pair_offset + 4U);
        if (record_size == 0U) {
            fail("PS2 save-bundle record " + std::to_string(index) +
                " has a zero size");
        }
        if (record_offset != expected_offset) {
            fail("PS2 save-bundle record " + std::to_string(index) +
                " is not contiguous with the preceding data");
        }

        const auto record_end = checked_add(
            record_offset,
            record_size,
            "PS2 save-bundle record end");
        require_range(
            bytes,
            record_offset,
            record_size,
            "PS2 save-bundle record " + std::to_string(index));
        result.records[index] = Ps2SaveBundleRecord{record_offset, record_size};
        expected_offset = record_end;
    }
    result.logical_bytes = expected_offset;

    const auto rounded_bytes = checked_add(
        result.logical_bytes,
        kPs2SaveBundleSectorSize - 1U,
        "PS2 save-bundle sector envelope");
    const auto minimum_envelope =
        (rounded_bytes / kPs2SaveBundleSectorSize) *
        kPs2SaveBundleSectorSize;
    if (minimum_envelope != input_size) {
        fail("The input is not the minimal 2048-byte sector envelope");
    }

    const auto padding_offset =
        host_size(result.logical_bytes, "Save-bundle logical size");
    const auto padding = bytes.subspan(padding_offset);
    require_zero_bytes(padding, "The PS2 save-bundle sector padding");
    result.padding_bytes = padding.size();
    result.occupied_sectors = input_size / kPs2SaveBundleSectorSize;

    const auto icon_sys_record = result.records[0];
    result.icon_sys = parse_icon_sys(bytes.subspan(
        icon_sys_record.offset,
        icon_sys_record.size));

    const auto icon_model_record = result.records[1];
    result.icon_model = parse_icon_model(
        bytes.subspan(icon_model_record.offset, icon_model_record.size),
        limits);

    const auto save_template_record = result.records[2];
    result.save_template = parse_save_template(
        bytes.subspan(
            save_template_record.offset,
            save_template_record.size),
        limits);
    return result;
}

} // namespace openrc
