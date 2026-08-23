#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace openrc {

inline constexpr std::uint32_t kWadBundleV1HeaderSize = 0xc0;
inline constexpr std::size_t kWadBundleV1SlotCount = 23;
inline constexpr std::uint32_t kWadBundleV1RecordAlignment = 0x40;

enum class WadBundleRecordKind {
    empty,
    nested_wad,
    elf,
};

struct WadBundleRecord {
    WadBundleRecordKind kind = WadBundleRecordKind::empty;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct WadBundleV1 {
    std::uint32_t decoded_size = 0;
    std::uint32_t header_size = 0;
    std::uint32_t initial_size = 0;
    WadBundleRecord initial_record;
    std::array<WadBundleRecord, kWadBundleV1SlotCount> slots{};
};

class WadBundleError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The input must contain exactly one complete, decompressed WadBundleV1.
// Reported record offsets are relative to the beginning of this span.
[[nodiscard]] WadBundleV1 parse_wad_bundle_v1(
    std::span<const std::byte> decoded_bytes);

} // namespace openrc
