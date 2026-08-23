#include "openrc/wad_bundle.hpp"

#include "openrc/elf.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace openrc {
namespace {

constexpr std::size_t kWadV1HeaderSize = 0x10;

static_assert(
    kWadBundleV1HeaderSize == 8U + kWadBundleV1SlotCount * 8U,
    "WadBundleV1 header must contain exactly 23 offset/size pairs");

[[noreturn]] void fail(const std::string& message) {
    throw WadBundleError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

void require_range(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const std::uint64_t size,
    const std::string& description) {
    const auto available = static_cast<std::uint64_t>(bytes.size());
    if (offset > available || size > available - offset) {
        fail(description + " points outside its record");
    }
}

[[nodiscard]] std::size_t align_record_offset(const std::size_t value) {
    constexpr auto alignment = static_cast<std::size_t>(kWadBundleV1RecordAlignment);
    constexpr auto mask = alignment - 1U;
    static_assert((alignment & mask) == 0U);
    if (value > std::numeric_limits<std::size_t>::max() - mask) {
        fail("Integer overflow while aligning a WadBundleV1 record");
    }
    return (value + mask) & ~mask;
}

[[nodiscard]] bool has_wad_magic(const std::span<const std::byte> bytes) {
    return bytes.size() >= 3U &&
        byte_value(bytes[0]) == static_cast<std::uint8_t>('W') &&
        byte_value(bytes[1]) == static_cast<std::uint8_t>('A') &&
        byte_value(bytes[2]) == static_cast<std::uint8_t>('D');
}

[[nodiscard]] bool has_elf_magic(const std::span<const std::byte> bytes) {
    return bytes.size() >= 4U && byte_value(bytes[0]) == 0x7fU &&
        byte_value(bytes[1]) == static_cast<std::uint8_t>('E') &&
        byte_value(bytes[2]) == static_cast<std::uint8_t>('L') &&
        byte_value(bytes[3]) == static_cast<std::uint8_t>('F');
}

void validate_nested_wad(
    const std::span<const std::byte> bytes,
    const std::string& description) {
    if (bytes.size() < kWadV1HeaderSize) {
        fail(description + " is too small to contain a WadV1 header");
    }
    if (!has_wad_magic(bytes)) {
        fail(description + " does not have a WadV1 signature");
    }

    const auto total_size = read_le32(bytes, 3U);
    if (total_size < kWadV1HeaderSize) {
        fail(description + " has a logical size smaller than its WadV1 header");
    }
    if (total_size != bytes.size()) {
        fail(description + " has padding or bytes outside its WadV1 logical size");
    }
}

[[nodiscard]] WadBundleRecordKind validate_active_record(
    const std::span<const std::byte> bytes,
    const std::string& description) {
    if (has_wad_magic(bytes)) {
        validate_nested_wad(bytes, description);
        return WadBundleRecordKind::nested_wad;
    }
    if (has_elf_magic(bytes)) {
        try {
            (void)inspect_elf(bytes);
        } catch (const ElfError& error) {
            fail(description + " is not a valid supported ELF: " + error.what());
        }
        return WadBundleRecordKind::elf;
    }
    fail(description + " has neither a WadV1 nor an ELF signature");
}

} // namespace

WadBundleV1 parse_wad_bundle_v1(const std::span<const std::byte> decoded_bytes) {
    if (decoded_bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail("The decoded WadBundleV1 exceeds its 32-bit address space");
    }
    if (decoded_bytes.size() < kWadBundleV1HeaderSize) {
        fail("The decoded data is too small to contain a WadBundleV1 header");
    }

    const auto header_size = read_le32(decoded_bytes, 0U);
    const auto initial_size = read_le32(decoded_bytes, 4U);
    if (header_size != kWadBundleV1HeaderSize) {
        fail("The decoded data has an unsupported WadBundleV1 header size");
    }

    require_range(decoded_bytes, header_size, initial_size, "Initial WadBundleV1 record");
    const auto initial_offset = static_cast<std::size_t>(header_size);
    const auto initial_bytes = decoded_bytes.subspan(initial_offset, initial_size);
    validate_nested_wad(initial_bytes, "Initial WadBundleV1 record");

    WadBundleV1 result;
    result.decoded_size = static_cast<std::uint32_t>(decoded_bytes.size());
    result.header_size = header_size;
    result.initial_size = initial_size;
    result.initial_record = WadBundleRecord{
        WadBundleRecordKind::nested_wad,
        header_size,
        initial_size,
    };

    std::size_t previous_end = initial_offset + initial_size;
    for (std::size_t index = 0; index < kWadBundleV1SlotCount; ++index) {
        const auto pair_offset = 8U + index * 8U;
        const auto record_offset = read_le32(decoded_bytes, pair_offset);
        const auto record_size = read_le32(decoded_bytes, pair_offset + 4U);

        if (record_offset == 0U && record_size == 0U) {
            result.slots[index] = WadBundleRecord{};
            continue;
        }
        if (record_offset == 0U || record_size == 0U) {
            fail("WadBundleV1 slot " + std::to_string(index) +
                " has a partially empty offset/size pair");
        }
        if (record_offset % kWadBundleV1RecordAlignment != 0U) {
            fail("WadBundleV1 slot " + std::to_string(index) +
                " is not aligned to 0x40 bytes");
        }

        const auto expected_offset = align_record_offset(previous_end);
        if (record_offset != expected_offset) {
            fail("WadBundleV1 slot " + std::to_string(index) +
                " does not begin at the next aligned record offset");
        }
        require_range(
            decoded_bytes,
            record_offset,
            record_size,
            "WadBundleV1 slot " + std::to_string(index));

        const auto alignment_padding = decoded_bytes.subspan(
            previous_end,
            static_cast<std::size_t>(record_offset) - previous_end);
        if (std::any_of(
                alignment_padding.begin(),
                alignment_padding.end(),
                [](const std::byte value) { return value != std::byte{0}; })) {
            fail("WadBundleV1 slot " + std::to_string(index) +
                " has non-zero alignment padding before it");
        }

        const auto record_bytes = decoded_bytes.subspan(record_offset, record_size);
        const auto description = "WadBundleV1 slot " + std::to_string(index);
        const auto kind = validate_active_record(record_bytes, description);
        result.slots[index] = WadBundleRecord{kind, record_offset, record_size};
        previous_end = static_cast<std::size_t>(record_offset) + record_size;
    }

    if (previous_end != decoded_bytes.size()) {
        fail("The decoded WadBundleV1 has trailing bytes outside its final active record");
    }
    return result;
}

} // namespace openrc
