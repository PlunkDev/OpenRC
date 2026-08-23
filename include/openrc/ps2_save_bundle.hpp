#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kPs2SaveBundleSectorSize = 2048;
inline constexpr std::size_t kPs2SaveBundleRecordCount = 3;
inline constexpr std::uint32_t kPs2SaveBundleHeaderSize = 0x18;

inline constexpr std::uint32_t kPs2IconSysSize = 0x3c4;
inline constexpr std::size_t kPs2IconTitleSize = 68;
inline constexpr std::size_t kPs2IconFilenameCount = 3;
inline constexpr std::size_t kPs2IconFilenameFieldSize = 64;
inline constexpr std::size_t kPs2IconSysReservedTailSize = 512;

inline constexpr std::uint32_t kPs2MemoryCardIconVersion = 0x00010000;
inline constexpr std::uint32_t kPs2MemoryCardIconShapeCount = 1;
inline constexpr std::uint32_t kPs2MemoryCardIconTextureType = 7;
inline constexpr std::uint32_t kPs2MemoryCardIconHeaderSize = 0x14;
inline constexpr std::uint32_t kPs2MemoryCardIconVertexSize = 0x18;
inline constexpr std::uint32_t kPs2MemoryCardIconAnimationSize = 0x24;
inline constexpr std::uint32_t kPs2MemoryCardIconTextureSize = 0x8000;

struct Ps2SaveBundleLimits {
    std::uint64_t max_input_bytes = 0;
    std::uint64_t max_vertex_count = 0;
    std::uint64_t max_repeated_record_count = 0;
    std::uint64_t max_tlv_entry_count = 0;
    std::uint64_t max_tlv_payload_bytes = 0;
};

struct Ps2SaveBundleRecord {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct Ps2IconSys {
    std::uint16_t reserved_halfword = 0;
    std::uint16_t second_line_offset = 0;
    std::uint32_t reserved_word = 0;
    std::uint32_t background_transparency = 0;
    std::array<std::uint32_t, 16> background_fields{};
    std::array<std::uint32_t, 12> light_direction_bits{};
    std::array<std::uint32_t, 12> light_color_bits{};
    std::array<std::uint32_t, 4> ambient_light_bits{};
    std::array<std::byte, kPs2IconTitleSize> title_bytes{};
    std::array<std::string, kPs2IconFilenameCount> icon_filenames{};
    std::array<std::byte, kPs2IconSysReservedTailSize> reserved_tail{};
};

struct Ps2MemoryCardIconVertex {
    // Ten neutral signed 16-bit fields preserve the complete non-color part
    // of the 24-byte vertex without assigning unconfirmed semantics.
    std::array<std::int16_t, 10> signed_fields{};
    std::array<std::uint8_t, 4> rgba{};
};

struct Ps2MemoryCardIcon {
    std::uint32_t version = 0;
    std::uint32_t shape_count = 0;
    std::uint32_t texture_type = 0;
    std::uint32_t opaque_float_bits = 0;
    std::uint32_t vertex_count = 0;
    std::vector<Ps2MemoryCardIconVertex> vertices;
    std::array<std::byte, kPs2MemoryCardIconAnimationSize> animation_bytes{};
    std::vector<std::byte> texture_bytes;
};

struct Ps2SaveTlvEntry {
    std::uint32_t key = 0;
    std::vector<std::byte> payload;
};

struct Ps2SaveTlvRecord {
    // Offset is relative to the beginning of the save-template record.
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t declared_stream_bytes = 0;
    std::uint32_t opaque_kind = 0;
    std::uint64_t alignment_padding_bytes = 0;
    std::vector<Ps2SaveTlvEntry> entries;
};

struct Ps2SaveTemplate {
    std::uint32_t primary_record_size = 0;
    std::uint32_t repeated_record_size = 0;
    Ps2SaveTlvRecord primary_record;
    std::vector<Ps2SaveTlvRecord> repeated_records;
};

struct Ps2SaveBundle {
    std::uint64_t input_bytes = 0;
    std::uint64_t logical_bytes = 0;
    std::uint64_t padding_bytes = 0;
    std::uint64_t occupied_sectors = 0;
    std::array<Ps2SaveBundleRecord, kPs2SaveBundleRecordCount> records{};
    Ps2IconSys icon_sys;
    Ps2MemoryCardIcon icon_model;
    Ps2SaveTemplate save_template;
};

class Ps2SaveBundleError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses the sector envelope of the three-record PS2 save bundle observed in
// the supported reference build. The returned report owns all copied strings,
// vertices, TLV payloads, animation bytes, and texture bytes.
[[nodiscard]] Ps2SaveBundle parse_ps2_save_bundle(
    std::span<const std::byte> bytes,
    Ps2SaveBundleLimits limits);

} // namespace openrc
