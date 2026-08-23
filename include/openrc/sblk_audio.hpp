#pragma once

#include "openrc/sblk.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace openrc {

struct SBlkAudioLimits {
    std::uint64_t max_items = 0;
    std::uint64_t max_blocks = 0;
    std::uint64_t max_secondary_bytes = 0;
};

struct SBlkItemV1 {
    std::uint32_t tag = 0;
    std::array<std::uint32_t, 9> payload{};
};

struct SBlkAudioReference {
    std::uint32_t descriptor_index = 0;
    std::uint32_t descriptor_item_index = 0;
    std::uint32_t global_item_index = 0;
    std::uint32_t bank_offset = 0;
    // Signed per-reference arguments to the runtime note-to-pitch conversion.
    // They are playback tuning, not a block sample rate in Hz.
    std::int8_t center_note = 0;
    std::int8_t center_fine = 0;
};

enum class SBlkAudioBlockKind {
    one_shot,
    looped,
};

struct SBlkAudioBlock {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t frame_count = 0;
    // Frame indices are relative to this block. The content range is
    // [content_begin_frame, content_end_frame).
    std::uint32_t content_begin_frame = 0;
    std::uint32_t content_end_frame = 0;
    SBlkAudioBlockKind kind = SBlkAudioBlockKind::one_shot;
    std::optional<std::uint32_t> loop_start_frame;
    std::optional<std::uint32_t> loop_end_frame;
    std::uint32_t trailing_padding_frame_count = 0;
    std::vector<SBlkAudioReference> references;
};

struct SBlkAudioReportV1 {
    std::uint32_t descriptor_count = 0;
    std::uint32_t item_count = 0;
    std::uint32_t reference_count = 0;
    std::uint32_t block_count = 0;
    std::uint32_t secondary_byte_count = 0;
    std::uint32_t frame_count = 0;
    std::vector<SBlkItemV1> items;
    std::vector<SBlkAudioBlock> blocks;
};

class SBlkAudioError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Applies the strict SBlk audio-bank V1 contract to an already parsed bundle.
// The returned report owns all item and block metadata. It never borrows or
// copies the secondary audio bytes.
[[nodiscard]] SBlkAudioReportV1 analyze_sblk_audio_v1(
    const SBlkBundleV3& bundle,
    SBlkAudioLimits limits);

} // namespace openrc
