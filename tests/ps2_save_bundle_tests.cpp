#include "openrc/ps2_save_bundle.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kVertexCount = 2;
constexpr openrc::Ps2SaveBundleLimits kValidLimits{
    1024U * 1024U,
    16U,
    8U,
    64U,
    1024U * 1024U,
};

struct EntrySpec {
    std::uint32_t key = 0;
    std::vector<std::byte> payload;
};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::uint32_t read_le32(
    const std::vector<std::byte>& bytes,
    const std::size_t offset) {
    return static_cast<std::uint32_t>(
               std::to_integer<std::uint8_t>(bytes[offset])) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 1U]))
         << 8U) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 2U]))
         << 16U) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 3U]))
         << 24U);
}

void write_le16(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_le32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void append_le32(
    std::vector<std::byte>& bytes,
    const std::uint32_t value) {
    const auto offset = bytes.size();
    bytes.resize(offset + sizeof(value), std::byte{0});
    write_le32(bytes, offset, value);
}

void append_bytes(
    std::vector<std::byte>& destination,
    const std::vector<std::byte>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

void write_c_string(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::size_t field_size,
    const std::string& value) {
    expect(value.size() < field_size, "test string does not fit its field");
    for (std::size_t index = 0; index < value.size(); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(static_cast<std::uint8_t>(value[index]));
    }
}

[[nodiscard]] std::vector<std::byte> make_icon_sys() {
    std::vector<std::byte> bytes(openrc::kPs2IconSysSize, std::byte{0});
    bytes[0] = std::byte{'P'};
    bytes[1] = std::byte{'S'};
    bytes[2] = std::byte{'2'};
    bytes[3] = std::byte{'D'};
    write_le16(bytes, 0x06, 0x20U);
    write_le32(bytes, 0x0c, 0x01020304U);
    write_le32(bytes, 0x10, 0x11223344U);
    write_le32(bytes, 0x50, 0x3f000000U);
    write_le32(bytes, 0x80, 0x3e800000U);
    write_le32(bytes, 0xb0, 0x3e4ccccdU);
    bytes[0xc0] = std::byte{0x82};
    bytes[0xc1] = std::byte{0x71};
    bytes[0xc2] = std::byte{0};
    write_c_string(bytes, 0x104, 64U, "view.bin");
    write_c_string(bytes, 0x144, 64U, "copy.bin");
    write_c_string(bytes, 0x184, 64U, "delete.bin");
    return bytes;
}

[[nodiscard]] std::vector<std::byte> make_icon_model(
    const std::uint32_t vertex_count = kVertexCount) {
    const auto size =
        static_cast<std::size_t>(openrc::kPs2MemoryCardIconHeaderSize) +
        static_cast<std::size_t>(vertex_count) *
            openrc::kPs2MemoryCardIconVertexSize +
        openrc::kPs2MemoryCardIconAnimationSize +
        openrc::kPs2MemoryCardIconTextureSize;
    std::vector<std::byte> bytes(size, std::byte{0});
    write_le32(bytes, 0x00, openrc::kPs2MemoryCardIconVersion);
    write_le32(bytes, 0x04, openrc::kPs2MemoryCardIconShapeCount);
    write_le32(bytes, 0x08, openrc::kPs2MemoryCardIconTextureType);
    write_le32(bytes, 0x0c, 0xdeadbeefU);
    write_le32(bytes, 0x10, vertex_count);

    for (std::uint32_t vertex_index = 0;
         vertex_index < vertex_count;
         ++vertex_index) {
        const auto offset =
            static_cast<std::size_t>(openrc::kPs2MemoryCardIconHeaderSize) +
            static_cast<std::size_t>(vertex_index) *
                openrc::kPs2MemoryCardIconVertexSize;
        for (std::size_t field_index = 0; field_index < 10U; ++field_index) {
            const auto signed_value =
                vertex_index == 0U && field_index == 0U
                ? static_cast<std::int16_t>(-123)
                : static_cast<std::int16_t>(
                      static_cast<std::int32_t>(vertex_index) * 100 +
                      static_cast<std::int32_t>(field_index));
            write_le16(
                bytes,
                offset + field_index * 2U,
                static_cast<std::uint16_t>(signed_value));
        }
        bytes[offset + 20U] = static_cast<std::byte>(10U + vertex_index);
        bytes[offset + 21U] = static_cast<std::byte>(20U + vertex_index);
        bytes[offset + 22U] = static_cast<std::byte>(30U + vertex_index);
        bytes[offset + 23U] = static_cast<std::byte>(40U + vertex_index);
    }

    const auto animation_offset =
        static_cast<std::size_t>(openrc::kPs2MemoryCardIconHeaderSize) +
        static_cast<std::size_t>(vertex_count) *
            openrc::kPs2MemoryCardIconVertexSize;
    for (std::size_t index = 0;
         index < openrc::kPs2MemoryCardIconAnimationSize;
         ++index) {
        bytes[animation_offset + index] =
            static_cast<std::byte>(static_cast<std::uint8_t>(index));
    }
    const auto texture_offset =
        animation_offset + openrc::kPs2MemoryCardIconAnimationSize;
    bytes[texture_offset] = std::byte{0x21};
    bytes.back() = std::byte{0x7f};
    return bytes;
}

[[nodiscard]] std::vector<std::byte> make_tlv_record(
    const std::uint32_t opaque_kind,
    const std::vector<EntrySpec>& entries) {
    std::vector<std::byte> bytes(8U, std::byte{0});
    write_le32(bytes, 4U, opaque_kind);
    for (const auto& entry : entries) {
        append_le32(bytes, entry.key);
        append_le32(
            bytes,
            static_cast<std::uint32_t>(entry.payload.size()));
        append_bytes(bytes, entry.payload);
        while (bytes.size() % 4U != 0U) {
            bytes.push_back(std::byte{0});
        }
    }
    append_le32(bytes, std::numeric_limits<std::uint32_t>::max());
    append_le32(bytes, 0U);
    write_le32(
        bytes,
        0U,
        static_cast<std::uint32_t>(bytes.size() - 8U));
    return bytes;
}

[[nodiscard]] std::vector<std::byte> make_save_template() {
    const auto primary = make_tlv_record(
        0x1234U,
        {
            EntrySpec{7U, {std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}}},
            EntrySpec{2U, {
                std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}},
        });
    const auto repeated_zero = make_tlv_record(
        0x5678U,
        {EntrySpec{0x100U, {std::byte{0}}}});
    const auto repeated_one = make_tlv_record(
        0x5678U,
        {EntrySpec{0x100U, {std::byte{1}}}});
    expect(
        repeated_zero.size() == repeated_one.size(),
        "test repeated records must have equal sizes");

    std::vector<std::byte> bytes(8U, std::byte{0});
    write_le32(
        bytes,
        0U,
        static_cast<std::uint32_t>(primary.size()));
    write_le32(
        bytes,
        4U,
        static_cast<std::uint32_t>(repeated_zero.size()));
    append_bytes(bytes, primary);
    append_bytes(bytes, repeated_zero);
    append_bytes(bytes, repeated_one);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> assemble_bundle(
    const std::vector<std::byte>& icon_sys,
    const std::vector<std::byte>& icon_model,
    const std::vector<std::byte>& save_template) {
    const std::array<const std::vector<std::byte>*, 3> records{
        &icon_sys,
        &icon_model,
        &save_template,
    };
    std::size_t logical_size = openrc::kPs2SaveBundleHeaderSize;
    for (const auto* record : records) {
        logical_size += record->size();
    }
    const auto envelope_size =
        (logical_size + openrc::kPs2SaveBundleSectorSize - 1U) /
        openrc::kPs2SaveBundleSectorSize *
        openrc::kPs2SaveBundleSectorSize;
    std::vector<std::byte> bytes(envelope_size, std::byte{0});

    std::size_t offset = openrc::kPs2SaveBundleHeaderSize;
    for (std::size_t index = 0; index < records.size(); ++index) {
        write_le32(
            bytes,
            index * 8U,
            static_cast<std::uint32_t>(offset));
        write_le32(
            bytes,
            index * 8U + 4U,
            static_cast<std::uint32_t>(records[index]->size()));
        std::copy(
            records[index]->begin(),
            records[index]->end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        offset += records[index]->size();
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> make_valid_bundle() {
    return assemble_bundle(
        make_icon_sys(),
        make_icon_model(),
        make_save_template());
}

void expect_rejected(
    const std::vector<std::byte>& bytes,
    const openrc::Ps2SaveBundleLimits limits,
    const std::string& message) {
    bool rejected = false;
    try {
        (void)openrc::parse_ps2_save_bundle(bytes, limits);
    } catch (const openrc::Ps2SaveBundleError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void test_valid_bundle() {
    const auto bytes = make_valid_bundle();
    const auto report =
        openrc::parse_ps2_save_bundle(bytes, kValidLimits);

    expect(report.input_bytes == bytes.size(), "input size was not reported");
    expect(
        report.logical_bytes + report.padding_bytes == report.input_bytes,
        "logical size and padding do not cover the input");
    expect(
        report.occupied_sectors * openrc::kPs2SaveBundleSectorSize ==
            report.input_bytes,
        "occupied sector count is wrong");
    expect(
        report.records[0].offset == openrc::kPs2SaveBundleHeaderSize,
        "first outer record offset is wrong");
    expect(
        report.records[0].size == openrc::kPs2IconSysSize,
        "icon.sys outer size is wrong");
    expect(
        report.records[1].offset ==
            report.records[0].offset + report.records[0].size,
        "icon model is not contiguous");
    expect(
        report.records[2].offset ==
            report.records[1].offset + report.records[1].size,
        "save template is not contiguous");

    expect(
        report.icon_sys.second_line_offset == 0x20U,
        "icon.sys second-line offset changed");
    expect(
        report.icon_sys.background_transparency == 0x01020304U,
        "icon.sys raw header word changed");
    expect(
        report.icon_sys.background_fields[0] == 0x11223344U,
        "icon.sys background field changed");
    expect(
        report.icon_sys.light_direction_bits[0] == 0x3f000000U,
        "icon.sys light-direction bits changed");
    expect(
        report.icon_sys.title_bytes[0] == std::byte{0x82} &&
            report.icon_sys.title_bytes[1] == std::byte{0x71},
        "icon.sys title bytes changed");
    expect(
        report.icon_sys.icon_filenames ==
            std::array<std::string, 3>{"view.bin", "copy.bin", "delete.bin"},
        "icon.sys filenames changed or were enforced");

    expect(
        report.icon_model.version == openrc::kPs2MemoryCardIconVersion &&
            report.icon_model.shape_count ==
                openrc::kPs2MemoryCardIconShapeCount &&
            report.icon_model.texture_type ==
                openrc::kPs2MemoryCardIconTextureType,
        "memory-card icon fixed header changed");
    expect(
        report.icon_model.opaque_float_bits == 0xdeadbeefU,
        "opaque float bits were interpreted or changed");
    expect(
        report.icon_model.vertices.size() == kVertexCount,
        "vertex count is wrong");
    expect(
        report.icon_model.vertices[0].signed_fields[0] == -123,
        "signed vertex field changed");
    expect(
        report.icon_model.vertices[1].rgba ==
            std::array<std::uint8_t, 4>{11U, 21U, 31U, 41U},
        "vertex color bytes changed");
    expect(
        report.icon_model.animation_bytes[35] == std::byte{35},
        "animation bytes changed");
    expect(
        report.icon_model.texture_bytes.size() ==
            openrc::kPs2MemoryCardIconTextureSize &&
            report.icon_model.texture_bytes.front() == std::byte{0x21} &&
            report.icon_model.texture_bytes.back() == std::byte{0x7f},
        "texture bytes changed");

    expect(
        report.save_template.primary_record.entries.size() == 2U,
        "primary TLV entry count is wrong");
    expect(
        report.save_template.primary_record.entries[0].key == 7U &&
            report.save_template.primary_record.entries[1].key == 2U,
        "non-monotonic TLV keys were reordered or rejected");
    expect(
        report.save_template.primary_record.alignment_padding_bytes == 1U,
        "primary TLV alignment padding count is wrong");
    expect(
        report.save_template.repeated_records.size() == 2U,
        "derived repeated-record count is wrong");
    expect(
        report.save_template.repeated_records[0].entries[0].payload[0] ==
                std::byte{0} &&
            report.save_template.repeated_records[1].entries[0].payload[0] ==
                std::byte{1},
        "non-identical repeated records were rejected or merged");
}

void test_outer_layout_rejections() {
    auto bytes = make_valid_bundle();
    bytes.resize(openrc::kPs2SaveBundleHeaderSize - 1U);
    expect_rejected(bytes, kValidLimits, "a truncated outer header was accepted");

    bytes = make_valid_bundle();
    auto limits = kValidLimits;
    limits.max_input_bytes = bytes.size() - 1U;
    expect_rejected(bytes, limits, "the input-size cap was ignored");

    bytes = make_valid_bundle();
    const auto second_offset = read_le32(bytes, 8U);
    write_le32(bytes, 8U, second_offset - 1U);
    expect_rejected(bytes, kValidLimits, "overlapping outer records were accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, 8U, second_offset + 1U);
    expect_rejected(bytes, kValidLimits, "a gap between outer records was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, 20U, std::numeric_limits<std::uint32_t>::max());
    expect_rejected(bytes, kValidLimits, "an out-of-bounds outer record was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, 4U, 0U);
    expect_rejected(bytes, kValidLimits, "a zero-sized outer record was accepted");

    bytes = make_valid_bundle();
    bytes.pop_back();
    expect_rejected(bytes, kValidLimits, "a non-minimal truncated envelope was accepted");

    bytes = make_valid_bundle();
    bytes.resize(
        bytes.size() + openrc::kPs2SaveBundleSectorSize,
        std::byte{0});
    expect_rejected(bytes, kValidLimits, "an extra zero sector was accepted");

    bytes = make_valid_bundle();
    bytes.back() = std::byte{1};
    expect_rejected(bytes, kValidLimits, "non-zero sector padding was accepted");
}

void test_icon_sys_rejections() {
    auto bytes = make_valid_bundle();
    const auto icon_offset = read_le32(bytes, 0U);
    bytes[icon_offset] = std::byte{'X'};
    expect_rejected(bytes, kValidLimits, "bad PS2D magic was accepted");

    bytes = make_valid_bundle();
    write_le16(bytes, icon_offset + 4U, 1U);
    expect_rejected(bytes, kValidLimits, "non-zero reserved halfword was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, icon_offset + 8U, 1U);
    expect_rejected(bytes, kValidLimits, "non-zero reserved word was accepted");

    bytes = make_valid_bundle();
    write_le16(bytes, icon_offset + 6U, 0xffffU);
    expect_rejected(
        bytes,
        kValidLimits,
        "an out-of-range icon.sys second-line offset was accepted");

    bytes = make_valid_bundle();
    bytes[icon_offset + 0x1c4U] = std::byte{1};
    expect_rejected(bytes, kValidLimits, "non-zero icon.sys reserved tail was accepted");

    constexpr std::array<std::size_t, 3> filename_offsets{
        0x104U, 0x144U, 0x184U};
    for (const auto filename_offset : filename_offsets) {
        bytes = make_valid_bundle();
        std::fill_n(
            bytes.begin() +
                static_cast<std::ptrdiff_t>(icon_offset + filename_offset),
            64,
            std::byte{'X'});
        expect_rejected(
            bytes,
            kValidLimits,
            "a non-terminated icon.sys filename was accepted");
    }

    auto icon_sys = make_icon_sys();
    icon_sys.push_back(std::byte{0});
    bytes = assemble_bundle(
        icon_sys,
        make_icon_model(),
        make_save_template());
    expect_rejected(bytes, kValidLimits, "a non-0x3c4 icon.sys was accepted");
}

void test_icon_model_rejections() {
    auto bytes = make_valid_bundle();
    const auto model_offset = read_le32(bytes, 8U);
    write_le32(bytes, model_offset, 0x00010001U);
    expect_rejected(bytes, kValidLimits, "bad icon-model version was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, model_offset + 4U, 2U);
    expect_rejected(bytes, kValidLimits, "bad icon-model shape count was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, model_offset + 8U, 8U);
    expect_rejected(bytes, kValidLimits, "bad icon-model texture type was accepted");

    bytes = make_valid_bundle();
    auto limits = kValidLimits;
    limits.max_vertex_count = kVertexCount - 1U;
    expect_rejected(bytes, limits, "the vertex-count cap was ignored");

    bytes = make_valid_bundle();
    write_le32(
        bytes,
        model_offset + 0x10U,
        std::numeric_limits<std::uint32_t>::max());
    limits = kValidLimits;
    limits.max_vertex_count = std::numeric_limits<std::uint32_t>::max();
    expect_rejected(
        bytes,
        limits,
        "an impossible vertex-derived record size was accepted");

    auto icon_model = make_icon_model();
    icon_model.push_back(std::byte{0});
    bytes = assemble_bundle(
        make_icon_sys(),
        icon_model,
        make_save_template());
    expect_rejected(bytes, kValidLimits, "bytes after the exact icon model were accepted");
}

void test_save_template_layout_rejections() {
    auto bytes = make_valid_bundle();
    const auto save_offset = read_le32(bytes, 16U);
    const auto primary_size = read_le32(bytes, save_offset);
    const auto repeated_size = read_le32(bytes, save_offset + 4U);
    const auto primary_offset = save_offset + 8U;

    write_le32(bytes, save_offset + 4U, 0U);
    expect_rejected(bytes, kValidLimits, "a zero repeated-record size was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, save_offset + 4U, repeated_size + 1U);
    expect_rejected(bytes, kValidLimits, "a non-divisible repeated tail was accepted");

    bytes = make_valid_bundle();
    write_le32(
        bytes,
        save_offset,
        std::numeric_limits<std::uint32_t>::max());
    expect_rejected(bytes, kValidLimits, "an out-of-bounds primary record was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, primary_offset, primary_size - 9U);
    expect_rejected(bytes, kValidLimits, "a mismatched TLV stream size was accepted");

    bytes = make_valid_bundle();
    auto limits = kValidLimits;
    limits.max_repeated_record_count = 1U;
    expect_rejected(bytes, limits, "the repeated-record cap was ignored");

    limits = kValidLimits;
    limits.max_tlv_entry_count = 3U;
    expect_rejected(bytes, limits, "the total TLV-entry cap was ignored");

    limits = kValidLimits;
    limits.max_tlv_payload_bytes = 8U;
    expect_rejected(bytes, limits, "the total TLV-payload cap was ignored");
}

void test_tlv_rejections() {
    auto bytes = make_valid_bundle();
    const auto save_offset = read_le32(bytes, 16U);
    const auto primary_size = read_le32(bytes, save_offset);
    const auto primary_offset = save_offset + 8U;
    const auto first_entry_offset = primary_offset + 8U;
    const auto sentinel_offset = primary_offset + primary_size - 8U;

    write_le32(bytes, first_entry_offset + 4U, primary_size);
    expect_rejected(bytes, kValidLimits, "a TLV payload overrun was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, sentinel_offset, 0U);
    expect_rejected(bytes, kValidLimits, "a missing TLV sentinel was accepted");

    bytes = make_valid_bundle();
    write_le32(bytes, sentinel_offset + 4U, 1U);
    expect_rejected(bytes, kValidLimits, "a non-zero sentinel size was accepted");

    bytes = make_valid_bundle();
    const auto first_payload_padding =
        first_entry_offset + 8U + 3U;
    bytes[first_payload_padding] = std::byte{1};
    expect_rejected(bytes, kValidLimits, "non-zero TLV alignment padding was accepted");

    bytes = make_valid_bundle();
    const auto first_repeat_offset = primary_offset + primary_size;
    write_le32(bytes, first_repeat_offset, 0U);
    expect_rejected(bytes, kValidLimits, "a repeated TLV stream-size mismatch was accepted");
}

} // namespace

int main() {
    try {
        test_valid_bundle();
        test_outer_layout_rejections();
        test_icon_sys_rejections();
        test_icon_model_rejections();
        test_save_template_layout_rejections();
        test_tlv_rejections();
        std::cout << "OpenRC PS2 save-bundle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC PS2 save-bundle tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
