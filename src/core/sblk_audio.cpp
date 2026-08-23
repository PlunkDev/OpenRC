#include "openrc/sblk_audio.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint64_t kMaximumMirroredCount = 0xffffU;
constexpr std::uint64_t kAdpcmFrameSize = 0x10U;

[[noreturn]] void fail(const std::string& message) {
    throw SBlkAudioError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::int8_t signed_word_byte(
    const std::uint32_t word,
    const unsigned int shift) {
    const auto byte = static_cast<std::uint8_t>((word >> shift) & 0xffU);
    const auto value = static_cast<std::int32_t>(byte);
    return static_cast<std::int8_t>(
        value <= 0x7f ? value : value - 0x100);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::vector<std::byte>& bytes,
    const std::size_t offset) {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
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
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left * right;
}

[[nodiscard]] bool is_supported_adpcm_flag(const std::uint8_t flag) {
    switch (flag) {
    case 0U:
    case 1U:
    case 2U:
    case 3U:
    case 6U:
    case 7U:
        return true;
    default:
        return false;
    }
}

struct AudioBlockGrammar {
    SBlkAudioBlockKind kind = SBlkAudioBlockKind::one_shot;
    std::uint32_t frame_count = 0;
    std::uint32_t content_begin_frame = 0;
    std::uint32_t content_end_frame = 0;
    std::optional<std::uint32_t> loop_start_frame;
    std::optional<std::uint32_t> loop_end_frame;
    std::uint32_t trailing_padding_frame_count = 0;
};

[[nodiscard]] std::size_t frame_byte_offset(
    const std::uint32_t block_offset,
    const std::uint32_t frame_index) {
    const auto offset =
        static_cast<std::uint64_t>(block_offset) +
        static_cast<std::uint64_t>(frame_index) * kAdpcmFrameSize;
    return static_cast<std::size_t>(offset);
}

[[nodiscard]] std::uint8_t frame_flag(
    const std::vector<std::byte>& secondary,
    const std::uint32_t block_offset,
    const std::uint32_t frame_index) {
    return byte_value(
        secondary[frame_byte_offset(block_offset, frame_index) + 1U]);
}

[[nodiscard]] bool is_zero_frame(
    const std::vector<std::byte>& secondary,
    const std::uint32_t block_offset,
    const std::uint32_t frame_index) {
    const auto offset = frame_byte_offset(block_offset, frame_index);
    for (std::size_t index = 0; index < 0x10U; ++index) {
        if (secondary[offset + index] != std::byte{0}) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] AudioBlockGrammar analyze_block_grammar(
    const std::vector<std::byte>& secondary,
    const std::uint32_t block_offset,
    const std::uint32_t block_size) {
    const auto frame_count = block_size / 0x10U;
    if (frame_count < 3U) {
        fail("An SBlk audio block is too short to contain a complete frame sequence");
    }
    if (!is_zero_frame(secondary, block_offset, 0U)) {
        fail("An SBlk audio block does not begin with a full zero frame");
    }

    const auto last_frame = frame_count - 1U;
    if (frame_flag(secondary, block_offset, last_frame) == 7U) {
        if (frame_flag(secondary, block_offset, last_frame - 1U) != 1U) {
            fail("An SBlk one-shot block does not end with flag 1 followed by flag 7");
        }
        for (std::uint32_t frame = 1U; frame < last_frame - 1U; ++frame) {
            if (frame_flag(secondary, block_offset, frame) != 0U) {
                fail("An SBlk one-shot block has a non-zero content-frame flag");
            }
        }
        return AudioBlockGrammar{
            SBlkAudioBlockKind::one_shot,
            frame_count,
            1U,
            last_frame,
            std::nullopt,
            std::nullopt,
            0U,
        };
    }

    std::uint32_t trailing_padding_frame_count = 0;
    auto terminal_frame = last_frame;
    if (is_zero_frame(secondary, block_offset, last_frame)) {
        trailing_padding_frame_count = 1U;
        --terminal_frame;
    }
    if (terminal_frame < 3U ||
        frame_flag(secondary, block_offset, terminal_frame) != 3U) {
        fail("An SBlk looped block does not end with flag 3 and optional zero padding");
    }

    std::optional<std::uint32_t> loop_start_frame;
    for (std::uint32_t frame = 1U; frame < terminal_frame; ++frame) {
        const auto flag = frame_flag(secondary, block_offset, frame);
        if (!loop_start_frame.has_value()) {
            if (flag == 2U) {
                continue;
            }
            if (flag == 6U) {
                loop_start_frame = frame;
                continue;
            }
            fail("An SBlk looped block has an invalid pre-loop frame flag");
        }
        if (flag != 2U) {
            fail("An SBlk looped block has an invalid post-loop frame flag");
        }
    }
    if (!loop_start_frame.has_value()) {
        fail("An SBlk looped block does not contain a flag-6 loop start");
    }
    if (loop_start_frame.value() + 1U >= terminal_frame) {
        fail("An SBlk looped block has no flag-2 frame after its loop start");
    }

    return AudioBlockGrammar{
        SBlkAudioBlockKind::looped,
        frame_count,
        1U,
        terminal_frame + 1U,
        loop_start_frame,
        terminal_frame,
        trailing_padding_frame_count,
    };
}

void validate_secondary_frames(const std::vector<std::byte>& secondary) {
    for (std::size_t offset = 0; offset < secondary.size(); offset += 0x10U) {
        const auto parameter = byte_value(secondary[offset]);
        const auto predictor = static_cast<std::uint8_t>(parameter >> 4U);
        const auto shift = static_cast<std::uint8_t>(parameter & 0x0fU);
        if (predictor > 4U || shift > 12U) {
            fail("An SBlk secondary frame has an invalid ADPCM predictor or shift");
        }
        if (!is_supported_adpcm_flag(byte_value(secondary[offset + 1U]))) {
            fail("An SBlk secondary frame has an unsupported ADPCM flag");
        }
    }
}

void require_zero_frame(
    const std::vector<std::byte>& secondary,
    const std::size_t offset) {
    for (std::size_t index = 0; index < 0x10U; ++index) {
        if (secondary[offset + index] != std::byte{0}) {
            fail("An SBlk audio reference does not target a zero lead-in frame");
        }
    }
}

} // namespace

SBlkAudioReportV1 analyze_sblk_audio_v1(
    const SBlkBundleV3& bundle,
    const SBlkAudioLimits limits) {
    const auto descriptor_count =
        static_cast<std::uint64_t>(bundle.descriptors.size());
    if (descriptor_count > kMaximumMirroredCount) {
        fail("The SBlk descriptor count does not fit its 16-bit mirror");
    }
    const auto descriptor_count32 = static_cast<std::uint32_t>(descriptor_count);
    if (bundle.opaque_a != (descriptor_count32 << 16U)) {
        fail("The SBlk descriptor-count mirror is inconsistent");
    }

    const auto secondary_size =
        static_cast<std::uint64_t>(bundle.secondary_bytes.size());
    if (secondary_size > limits.max_secondary_bytes) {
        fail("The SBlk secondary data exceeds the caller's size limit");
    }
    if (secondary_size == 0U) {
        fail("The SBlk secondary audio bank is empty");
    }
    if (secondary_size > std::numeric_limits<std::uint32_t>::max()) {
        fail("The SBlk secondary audio bank exceeds its 32-bit address space");
    }
    if (secondary_size % kAdpcmFrameSize != 0U) {
        fail("The SBlk secondary audio bank is not a whole number of ADPCM frames");
    }
    validate_secondary_frames(bundle.secondary_bytes);

    std::uint64_t expected_data_offset = 0;
    std::uint64_t total_items = 0;
    for (const auto& descriptor : bundle.descriptors) {
        if (descriptor.item_count > kMaximumMirroredCount) {
            fail("An SBlk descriptor item count does not fit in 16 bits");
        }
        if ((descriptor.packed_count & 0xffffU) != descriptor.item_count ||
            (descriptor.packed_count & 0xffff0000U) != descriptor.flags) {
            fail("An SBlk descriptor disagrees with its packed item count");
        }
        if (static_cast<std::uint64_t>(descriptor.data_offset) !=
            expected_data_offset) {
            fail("The SBlk descriptor item ranges are not exactly contiguous");
        }
        const auto descriptor_bytes = checked_multiply(
            descriptor.item_count,
            kSBlkV1ItemSize,
            "an SBlk descriptor item range");
        expected_data_offset = checked_add(
            expected_data_offset,
            descriptor_bytes,
            "the SBlk descriptor item-data end");
        if (expected_data_offset > bundle.item_bytes.size()) {
            fail("An SBlk descriptor item range exceeds the item data");
        }
        total_items = checked_add(
            total_items,
            descriptor.item_count,
            "the total SBlk audio item count");
        if (total_items > limits.max_items) {
            fail("The SBlk audio item count exceeds the caller's limit");
        }
        if (total_items > kMaximumMirroredCount) {
            fail("The SBlk audio item count does not fit its 16-bit mirror");
        }
    }
    if (expected_data_offset != bundle.item_bytes.size()) {
        fail("The SBlk descriptor ranges do not exactly fill the item data");
    }

    const auto total_items32 = static_cast<std::uint32_t>(total_items);
    if ((bundle.opaque_b & 0xffffU) != total_items32) {
        fail("The SBlk item-count mirror is inconsistent");
    }

    SBlkAudioReportV1 result;
    result.descriptor_count = descriptor_count32;
    result.item_count = total_items32;
    result.secondary_byte_count = static_cast<std::uint32_t>(secondary_size);
    result.frame_count = static_cast<std::uint32_t>(
        secondary_size / kAdpcmFrameSize);
    result.items.reserve(static_cast<std::size_t>(total_items));

    std::map<std::uint32_t, std::vector<SBlkAudioReference>>
        references_by_offset;
    std::uint32_t global_item_index = 0;
    std::uint64_t reference_count = 0;

    for (std::size_t descriptor_index = 0;
         descriptor_index < bundle.descriptors.size();
         ++descriptor_index) {
        const auto& descriptor = bundle.descriptors[descriptor_index];
        bool has_audio_reference = false;

        for (std::uint32_t local_index = 0;
             local_index < descriptor.item_count;
             ++local_index) {
            const auto local_byte_offset = checked_multiply(
                local_index,
                kSBlkV1ItemSize,
                "an SBlk item offset");
            const auto item_byte_offset = checked_add(
                descriptor.data_offset,
                local_byte_offset,
                "an SBlk item address");
            const auto host_item_offset =
                static_cast<std::size_t>(item_byte_offset);

            SBlkItemV1 item;
            item.tag = read_le32(bundle.item_bytes, host_item_offset);
            for (std::size_t payload_index = 0;
                 payload_index < item.payload.size();
                 ++payload_index) {
                item.payload[payload_index] = read_le32(
                    bundle.item_bytes,
                    host_item_offset + (payload_index + 1U) * 4U);
            }

            const auto word2 = item.payload[1];
            const auto word6 = item.payload[5];
            const auto word7 = item.payload[6];
            const auto word8 = item.payload[7];
            const auto word9 = item.payload[8];
            if (word7 != 0U) {
                fail("An SBlk item has a non-zero reserved word 7");
            }
            if (item.tag != 1U &&
                (word6 != 0U || word8 != 0U || word9 != 0U)) {
                fail("A non-reference SBlk item uses a reference-only word");
            }

            if (item.tag == 1U) {
                has_audio_reference = true;
                if (word6 % kAdpcmFrameSize != 0U) {
                    fail("An SBlk audio-bank offset is not 16-byte aligned");
                }
                const auto reference_end = checked_add(
                    word6,
                    kAdpcmFrameSize,
                    "an SBlk audio-reference lead-in frame");
                if (reference_end > secondary_size) {
                    fail("An SBlk audio-bank offset lies outside the secondary data");
                }
                require_zero_frame(
                    bundle.secondary_bytes,
                    static_cast<std::size_t>(word6));

                auto iterator = references_by_offset.find(word6);
                if (iterator == references_by_offset.end()) {
                    if (static_cast<std::uint64_t>(
                            references_by_offset.size()) >= limits.max_blocks) {
                        fail("The SBlk audio block count exceeds the caller's limit");
                    }
                    iterator = references_by_offset.try_emplace(word6).first;
                }
                iterator->second.push_back(SBlkAudioReference{
                    static_cast<std::uint32_t>(descriptor_index),
                    local_index,
                    global_item_index,
                    word6,
                    signed_word_byte(word2, 16U),
                    signed_word_byte(word2, 24U),
                });
                reference_count = checked_add(
                    reference_count,
                    1U,
                    "the SBlk audio-reference count");
            }

            result.items.push_back(item);
            ++global_item_index;
        }

        if (!has_audio_reference) {
            fail("An SBlk descriptor does not contain a tag-1 audio reference");
        }
    }

    if (global_item_index != total_items32 ||
        result.items.size() != static_cast<std::size_t>(total_items)) {
        fail("The parsed SBlk item count is inconsistent");
    }
    if (references_by_offset.empty() ||
        references_by_offset.begin()->first != 0U) {
        fail("The SBlk audio block directory does not begin at offset zero");
    }

    const auto block_count =
        static_cast<std::uint64_t>(references_by_offset.size());
    if (block_count > limits.max_blocks) {
        fail("The SBlk audio block count exceeds the caller's limit");
    }
    if (block_count > kMaximumMirroredCount) {
        fail("The SBlk audio block count does not fit its 16-bit mirror");
    }
    const auto block_count32 = static_cast<std::uint32_t>(block_count);
    if ((bundle.opaque_b >> 16U) != block_count32) {
        fail("The SBlk audio-block-count mirror is inconsistent");
    }

    result.reference_count = static_cast<std::uint32_t>(reference_count);
    result.block_count = block_count32;
    result.blocks.reserve(static_cast<std::size_t>(block_count));

    for (auto iterator = references_by_offset.begin();
         iterator != references_by_offset.end();
         ++iterator) {
        auto next = iterator;
        ++next;
        const auto begin = static_cast<std::uint64_t>(iterator->first);
        const auto end = next == references_by_offset.end()
            ? secondary_size
            : static_cast<std::uint64_t>(next->first);
        if (end <= begin) {
            fail("An SBlk audio block has an empty or descending range");
        }
        const auto block_size = end - begin;
        if (begin % kAdpcmFrameSize != 0U ||
            block_size % kAdpcmFrameSize != 0U) {
            fail("An SBlk audio block range is not 16-byte aligned");
        }
        if (block_size > std::numeric_limits<std::uint32_t>::max()) {
            fail("An SBlk audio block size exceeds its 32-bit representation");
        }
        const auto block_size32 = static_cast<std::uint32_t>(block_size);
        const auto grammar = analyze_block_grammar(
            bundle.secondary_bytes,
            iterator->first,
            block_size32);
        SBlkAudioBlock block;
        block.offset = iterator->first;
        block.size = block_size32;
        block.frame_count = grammar.frame_count;
        block.content_begin_frame = grammar.content_begin_frame;
        block.content_end_frame = grammar.content_end_frame;
        block.kind = grammar.kind;
        block.loop_start_frame = grammar.loop_start_frame;
        block.loop_end_frame = grammar.loop_end_frame;
        block.trailing_padding_frame_count =
            grammar.trailing_padding_frame_count;
        block.references = std::move(iterator->second);
        result.blocks.push_back(std::move(block));
    }

    return result;
}

} // namespace openrc
