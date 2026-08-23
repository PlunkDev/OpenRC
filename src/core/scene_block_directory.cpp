#include "openrc/scene_block_directory.hpp"

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

[[noreturn]] void fail(const std::string& message) {
    throw SceneBlockDirectoryError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint32_t low_half(
    const std::uint32_t value) noexcept {
    return value & 0xffffU;
}

[[nodiscard]] std::uint32_t high_half(
    const std::uint32_t value) noexcept {
    return value >> 16U;
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left * right;
}

[[nodiscard]] std::vector<std::byte> copy_range(
    const std::span<const std::byte> bytes,
    const SceneBlockRange range,
    const char* description) {
    if (range.offset > bytes.size() ||
        range.size > static_cast<std::uint64_t>(bytes.size()) - range.offset) {
        fail(std::string(description) + " exceeds the SceneBlockDirectoryV1 input");
    }
    std::vector<std::byte> result;
    if (range.size > result.max_size() ||
        range.size > std::numeric_limits<std::size_t>::max()) {
        fail(std::string(description) + " exceeds the host container limit");
    }
    const auto offset = static_cast<std::size_t>(range.offset);
    const auto size = static_cast<std::size_t>(range.size);
    if (offset > static_cast<std::size_t>(
            std::numeric_limits<std::ptrdiff_t>::max()) ||
        size > static_cast<std::size_t>(
            std::numeric_limits<std::ptrdiff_t>::max()) - offset) {
        fail(std::string(description) + " cannot be addressed by host iterators");
    }
    result.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
    return result;
}

} // namespace

SceneBlockDirectoryV1 parse_scene_block_directory_v1(
    const std::span<const std::byte> bytes,
    const SceneBlockDirectoryLimits limits) {
    if (limits.max_input_bytes == 0U ||
        limits.max_records == 0U ||
        limits.max_owned_bytes == 0U) {
        fail("SceneBlockDirectoryV1 caller limits must all be non-zero");
    }

    const auto input_bytes = static_cast<std::uint64_t>(bytes.size());
    if (input_bytes > limits.max_input_bytes) {
        fail("The SceneBlockDirectoryV1 input exceeds the caller's byte limit");
    }
    if (bytes.size() < kSceneBlockDirectoryV1Stride) {
        fail("The SceneBlockDirectoryV1 input is smaller than its fixed header");
    }

    SceneBlockDirectoryV1 result;
    result.input_bytes = input_bytes;
    for (std::size_t index = 0;
         index < result.raw_header_words.size();
         ++index) {
        result.raw_header_words[index] = read_le32(bytes, index * 4U);
    }
    result.stride_bytes = result.raw_header_words[0];
    result.declared_count = result.raw_header_words[1];
    result.header_float = std::bit_cast<float>(result.raw_header_words[2]);

    if (result.stride_bytes != kSceneBlockDirectoryV1Stride) {
        fail("SceneBlockDirectoryV1 does not use the required 0x40-byte stride");
    }
    if (result.declared_count < 2U) {
        fail("SceneBlockDirectoryV1 must contain at least two descriptors");
    }
    if (!std::isfinite(result.header_float) || result.header_float <= 0.0F) {
        fail("SceneBlockDirectoryV1 header float must be finite and positive");
    }
    if (std::any_of(
            result.raw_header_words.begin() + 3,
            result.raw_header_words.end(),
            [](const std::uint32_t value) { return value != 0U; })) {
        fail("SceneBlockDirectoryV1 reserved header words are non-zero");
    }

    result.directory_bytes = checked_multiply(
        result.declared_count,
        kSceneBlockDirectoryV1Stride,
        "the SceneBlockDirectoryV1 directory size");
    const auto descriptor_span_end = checked_add(
        result.directory_bytes,
        kSceneBlockDirectoryV1Stride,
        "the SceneBlockDirectoryV1 descriptor span");
    if (descriptor_span_end > input_bytes) {
        fail("SceneBlockDirectoryV1 descriptor span exceeds the input");
    }
    result.record_count = result.declared_count;
    if (result.record_count > limits.max_records) {
        fail("SceneBlockDirectoryV1 record count exceeds the caller's limit");
    }
    result.overlapped_entry_range = SceneBlockRange{
        result.directory_bytes,
        kSceneBlockDirectoryV1Stride};
    result.owned_byte_count = input_bytes - result.directory_bytes;
    if (result.owned_byte_count > limits.max_owned_bytes) {
        fail("SceneBlockDirectoryV1 owned bytes exceed the caller's limit");
    }
    if (result.record_count > result.entries.max_size() ||
        result.record_count > std::numeric_limits<std::size_t>::max()) {
        fail("SceneBlockDirectoryV1 record metadata exceeds the host container limit");
    }
    result.entries.reserve(static_cast<std::size_t>(result.record_count));

    std::uint64_t expected_block_offset = result.directory_bytes;
    for (std::uint64_t record_index = 0U;
         record_index < result.record_count;
         ++record_index) {
        const auto directory_entry_offset = checked_add(
            kSceneBlockDirectoryV1Stride,
            checked_multiply(
                record_index,
                kSceneBlockDirectoryV1Stride,
                "a SceneBlockDirectoryV1 entry offset"),
            "a SceneBlockDirectoryV1 entry offset");
        if (directory_entry_offset > descriptor_span_end ||
            descriptor_span_end - directory_entry_offset <
                kSceneBlockDirectoryV1Stride) {
            fail("A SceneBlockDirectoryV1 entry exceeds the descriptor span");
        }
        const auto entry_offset = static_cast<std::size_t>(
            directory_entry_offset);

        SceneBlockDirectoryEntryV1 entry;
        entry.directory_entry_offset = directory_entry_offset;
        for (std::size_t word_index = 0;
             word_index < entry.raw_words.size();
             ++word_index) {
            entry.raw_words[word_index] =
                read_le32(bytes, entry_offset + word_index * 4U);
        }
        for (std::size_t component = 0;
             component < entry.float_values.size();
             ++component) {
            entry.float_values[component] =
                std::bit_cast<float>(entry.raw_words[component]);
            if (!std::isfinite(entry.float_values[component])) {
                fail("A SceneBlockDirectoryV1 entry float is not finite");
            }
        }
        if (entry.float_values[3] <= 0.0F) {
            fail("A SceneBlockDirectoryV1 entry fourth float is not positive");
        }

        entry.block_offset = entry.raw_words[4];
        entry.opaque_size = entry.raw_words[14];
        if (entry.block_offset % 0x10U != 0U) {
            fail("A SceneBlockDirectoryV1 block offset is not 0x10-aligned");
        }
        if (entry.opaque_size % 0x10U != 0U) {
            fail("A SceneBlockDirectoryV1 opaque size is not 0x10-aligned");
        }
        if (entry.block_offset != expected_block_offset) {
            fail("SceneBlockDirectoryV1 block offsets do not form the required exact chain");
        }

        const auto block_size = checked_add(
            kSceneBlockDirectoryV1Stride,
            entry.opaque_size,
            "a SceneBlockDirectoryV1 block envelope size");
        entry.block_end = checked_add(
            entry.block_offset,
            block_size,
            "a SceneBlockDirectoryV1 block end");
        if (entry.block_end > input_bytes) {
            fail("A SceneBlockDirectoryV1 block exceeds the input");
        }
        entry.block_range = SceneBlockRange{entry.block_offset, block_size};
        entry.prefix_range = SceneBlockRange{
            entry.block_offset,
            kSceneBlockDirectoryV1Stride};
        entry.remainder_range = SceneBlockRange{
            checked_add(
                entry.block_offset,
                kSceneBlockDirectoryV1Stride,
                "a SceneBlockDirectoryV1 remainder offset"),
            entry.opaque_size};

        const auto section_tail_bytes = checked_multiply(
            low_half(entry.raw_words[11]),
            kSceneBlockSectionAlignment,
            "a SceneBlockDirectoryV1 final section size");
        if (low_half(entry.raw_words[5]) != 0U) {
            fail("A SceneBlockDirectoryV1 section layout does not begin at zero");
        }
        if (high_half(entry.raw_words[11]) !=
            high_half(entry.raw_words[12])) {
            fail("A SceneBlockDirectoryV1 section layout has inconsistent final boundaries");
        }
        if (entry.raw_words[13] != kSceneBlockSectionMarker) {
            fail("A SceneBlockDirectoryV1 section layout marker is invalid");
        }
        if (checked_add(
                high_half(entry.raw_words[11]),
                section_tail_bytes,
                "a SceneBlockDirectoryV1 final section end") !=
            entry.opaque_size) {
            fail("A SceneBlockDirectoryV1 final section does not end at the remainder boundary");
        }

        entry.section_layout.relative_boundaries = {
            low_half(entry.raw_words[5]),
            high_half(entry.raw_words[5]),
            low_half(entry.raw_words[7]),
            low_half(entry.raw_words[6]),
            high_half(entry.raw_words[6]),
            high_half(entry.raw_words[7]),
            low_half(entry.raw_words[12]),
            high_half(entry.raw_words[11]),
            entry.opaque_size,
        };
        for (std::size_t section_index = 0U;
             section_index < kSceneBlockSectionCount;
             ++section_index) {
            const auto section_begin =
                entry.section_layout.relative_boundaries[section_index];
            const auto section_end =
                entry.section_layout.relative_boundaries[section_index + 1U];
            if (section_begin % kSceneBlockSectionAlignment != 0U ||
                section_end % kSceneBlockSectionAlignment != 0U) {
                fail("A SceneBlockDirectoryV1 section boundary is not 0x10-aligned");
            }
            if (section_begin >= section_end) {
                fail("SceneBlockDirectoryV1 section boundaries are not strictly increasing");
            }
            entry.section_layout.ranges[section_index] = SceneBlockRange{
                checked_add(
                    entry.remainder_range.offset,
                    section_begin,
                    "a SceneBlockDirectoryV1 section offset"),
                section_end - section_begin};
        }
        result.entries.push_back(std::move(entry));
        expected_block_offset = result.entries.back().block_end;
    }

    result.chain_end = expected_block_offset;
    if (result.entries.empty() ||
        result.entries.front().prefix_range != result.overlapped_entry_range ||
        result.entries.back().directory_entry_offset !=
            result.overlapped_entry_range.offset) {
        fail("SceneBlockDirectoryV1 final descriptor overlap is inconsistent");
    }
    result.trailing_range = SceneBlockRange{
        result.chain_end,
        input_bytes - result.chain_end};

    // Allocate and copy only after every directory, range, chain, and aggregate
    // byte limit has passed.
    for (auto& entry : result.entries) {
        entry.block_bytes = copy_range(
            bytes,
            entry.block_range,
            "A SceneBlockDirectoryV1 block envelope");
    }
    result.trailing_bytes = copy_range(
        bytes,
        result.trailing_range,
        "The SceneBlockDirectoryV1 trailing range");
    return result;
}

} // namespace openrc
