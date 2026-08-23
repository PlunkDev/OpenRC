#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kSceneBlockDirectoryV1Stride = 0x40;
inline constexpr std::size_t kSceneBlockDirectoryV1WordCount = 16;

struct SceneBlockDirectoryLimits {
    std::uint64_t max_input_bytes = 0;
    std::uint64_t max_records = 0;
    // Limits the total copied bytes in all block envelopes plus the trailing
    // opaque range. Directory words are stored as fixed-size metadata.
    std::uint64_t max_owned_bytes = 0;
};

struct SceneBlockRange {
    // Offsets are relative to the beginning of the decoded input.
    std::uint64_t offset = 0;
    std::uint64_t size = 0;

    [[nodiscard]] bool operator==(const SceneBlockRange&) const = default;
};

struct SceneBlockDirectoryEntryV1 {
    std::uint64_t directory_entry_offset = 0;
    std::array<std::uint32_t, kSceneBlockDirectoryV1WordCount> raw_words{};
    std::array<float, 4> float_values{};
    std::uint32_t block_offset = 0;
    std::uint32_t opaque_size = 0;
    std::uint64_t block_end = 0;

    // block_bytes owns the complete neutral envelope: the fixed 0x40-byte
    // prefix followed by opaque_size bytes. No semantics are assigned to
    // either subrange yet.
    SceneBlockRange block_range;
    SceneBlockRange prefix_range;
    SceneBlockRange remainder_range;
    std::vector<std::byte> block_bytes;
};

struct SceneBlockDirectoryV1 {
    std::uint64_t input_bytes = 0;
    std::array<std::uint32_t, kSceneBlockDirectoryV1WordCount> raw_header_words{};
    std::uint32_t stride_bytes = 0;
    std::uint32_t declared_count = 0;
    float header_float = 0.0F;
    std::uint64_t directory_bytes = 0;
    std::uint64_t record_count = 0;
    std::uint64_t owned_byte_count = 0;
    std::vector<SceneBlockDirectoryEntryV1> entries;
    std::uint64_t chain_end = 0;
    SceneBlockRange trailing_range;
    std::vector<std::byte> trailing_bytes;
};

class SceneBlockDirectoryError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses the bounded SceneBlockDirectoryV1 contract observed in the decoded
// primary-extent0/subrange10 WadV1 payload. Every dynamic byte sequence in the
// returned report is owned; it never borrows from the input.
[[nodiscard]] SceneBlockDirectoryV1 parse_scene_block_directory_v1(
    std::span<const std::byte> bytes,
    SceneBlockDirectoryLimits limits);

} // namespace openrc
