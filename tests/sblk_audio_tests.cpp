#include "openrc/sblk_audio.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kItemCount = 4;
constexpr std::size_t kSecondarySize = 0xb0;
constexpr openrc::SBlkAudioLimits kValidLimits{
    kItemCount,
    2U,
    kSecondarySize,
};

void write_le32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void write_item_word(
    openrc::SBlkBundleV3& bundle,
    const std::size_t item_index,
    const std::size_t word_index,
    const std::uint32_t value) {
    write_le32(
        bundle.item_bytes,
        item_index * openrc::kSBlkV1ItemSize + word_index * 4U,
        value);
}

openrc::SBlkBundleV3 make_valid_bundle() {
    openrc::SBlkBundleV3 bundle;
    bundle.opaque_a = 2U << 16U;
    bundle.opaque_b = (2U << 16U) | static_cast<std::uint32_t>(kItemCount);
    bundle.descriptors = {
        openrc::SBlkDescriptor{
            0x89abcdefU,
            2U,
            2U,
            0U,
            0U,
        },
        openrc::SBlkDescriptor{
            0x01234567U,
            0x00010002U,
            2U,
            0x00010000U,
            2U * openrc::kSBlkV1ItemSize,
        },
    };
    bundle.item_bytes.resize(
        kItemCount * openrc::kSBlkV1ItemSize,
        std::byte{0});

    // Descriptor 0: a reference followed by an unknown, preserved tag.
    write_item_word(bundle, 0U, 0U, 1U);
    write_item_word(bundle, 0U, 2U, 0x807f335aU);
    write_item_word(bundle, 0U, 8U, 0x1110U);
    write_item_word(bundle, 0U, 9U, 0x40U);
    write_item_word(bundle, 1U, 0U, 0x99U);
    write_item_word(bundle, 1U, 2U, 0xaabbccddU);

    // Descriptor 1: a duplicate reference and a second unique block.
    write_item_word(bundle, 2U, 0U, 1U);
    write_item_word(bundle, 2U, 2U, 0xfe81335aU);
    write_item_word(bundle, 3U, 0U, 1U);
    write_item_word(bundle, 3U, 6U, 0x50U);
    write_item_word(bundle, 3U, 8U, 0x2220U);
    write_item_word(bundle, 3U, 9U, 0x60U);

    bundle.secondary_bytes.resize(kSecondarySize, std::byte{0});
    // One-shot block: lead-in Z, flag 0, an allowed unreferenced full-zero
    // flag-0 frame, flag 1, flag 7.
    bundle.secondary_bytes[0x10] = std::byte{0x0c};
    bundle.secondary_bytes[0x12] = std::byte{0x11};
    bundle.secondary_bytes[0x30] = std::byte{0x1c};
    bundle.secondary_bytes[0x31] = std::byte{0x01};
    bundle.secondary_bytes[0x32] = std::byte{0x22};
    bundle.secondary_bytes[0x40] = std::byte{0x2c};
    bundle.secondary_bytes[0x41] = std::byte{0x07};
    bundle.secondary_bytes[0x42] = std::byte{0x33};

    // Looped block: lead-in Z, pre-loop flag 2, flag 6, post-loop flag 2,
    // flag 3, and one optional full-zero padding frame.
    bundle.secondary_bytes[0x60] = std::byte{0x0c};
    bundle.secondary_bytes[0x61] = std::byte{0x02};
    bundle.secondary_bytes[0x62] = std::byte{0x44};
    bundle.secondary_bytes[0x70] = std::byte{0x1c};
    bundle.secondary_bytes[0x71] = std::byte{0x06};
    bundle.secondary_bytes[0x72] = std::byte{0x55};
    bundle.secondary_bytes[0x80] = std::byte{0x2c};
    bundle.secondary_bytes[0x81] = std::byte{0x02};
    bundle.secondary_bytes[0x82] = std::byte{0x66};
    bundle.secondary_bytes[0x90] = std::byte{0x3c};
    bundle.secondary_bytes[0x91] = std::byte{0x03};
    bundle.secondary_bytes[0x92] = std::byte{0x77};
    return bundle;
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_rejected(
    const openrc::SBlkBundleV3& bundle,
    const openrc::SBlkAudioLimits limits,
    const std::string& message) {
    bool rejected = false;
    try {
        (void)openrc::analyze_sblk_audio_v1(bundle, limits);
    } catch (const openrc::SBlkAudioError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void test_valid_bundle_and_owned_metadata() {
    auto bundle = make_valid_bundle();
    const auto report = openrc::analyze_sblk_audio_v1(bundle, kValidLimits);

    expect(report.descriptor_count == 2U, "descriptor count is wrong");
    expect(report.item_count == 4U, "item count is wrong");
    expect(report.reference_count == 3U, "reference count is wrong");
    expect(report.block_count == 2U, "block count is wrong");
    expect(report.secondary_byte_count == kSecondarySize, "secondary size is wrong");
    expect(report.frame_count == 11U, "frame count is wrong");
    expect(report.items.size() == 4U, "owned item count is wrong");
    expect(report.items[1].tag == 0x99U, "an unknown item tag was not preserved");
    expect(
        report.items[1].payload[1] == 0xaabbccddU,
        "an unknown item payload changed");
    expect(
        report.items[0].payload[1] == 0x807f335aU &&
            report.items[2].payload[1] == 0xfe81335aU,
        "tag-1 word 2 was not preserved");

    expect(report.blocks.size() == 2U, "owned block count is wrong");
    expect(
        report.blocks[0].offset == 0U && report.blocks[0].size == 0x50U,
        "the first block range is wrong");
    expect(
        report.blocks[0].kind == openrc::SBlkAudioBlockKind::one_shot &&
            report.blocks[0].frame_count == 5U &&
            report.blocks[0].content_begin_frame == 1U &&
            report.blocks[0].content_end_frame == 4U &&
            !report.blocks[0].loop_start_frame.has_value() &&
            !report.blocks[0].loop_end_frame.has_value() &&
            report.blocks[0].trailing_padding_frame_count == 0U,
        "the one-shot block grammar metadata is wrong");
    expect(report.blocks[0].references.size() == 2U, "duplicate references were lost");
    expect(
        report.blocks[0].references[0].descriptor_index == 0U &&
            report.blocks[0].references[0].descriptor_item_index == 0U &&
            report.blocks[0].references[0].global_item_index == 0U &&
            report.blocks[0].references[0].bank_offset == 0U &&
            report.blocks[0].references[0].center_note == std::int8_t{127} &&
            report.blocks[0].references[0].center_fine == std::int8_t{-128},
        "the first reference metadata is wrong");
    expect(
        report.blocks[0].references[1].descriptor_index == 1U &&
            report.blocks[0].references[1].descriptor_item_index == 0U &&
            report.blocks[0].references[1].global_item_index == 2U &&
            report.blocks[0].references[1].bank_offset == 0U &&
            report.blocks[0].references[1].center_note == std::int8_t{-127} &&
            report.blocks[0].references[1].center_fine == std::int8_t{-2},
        "the duplicate reference metadata is wrong");
    expect(
        report.blocks[1].offset == 0x50U &&
            report.blocks[1].size == 0x60U &&
            report.blocks[1].references.size() == 1U &&
            report.blocks[1].references[0].global_item_index == 3U,
        "the second block metadata is wrong");
    expect(
        report.blocks[1].kind == openrc::SBlkAudioBlockKind::looped &&
            report.blocks[1].frame_count == 6U &&
            report.blocks[1].content_begin_frame == 1U &&
            report.blocks[1].content_end_frame == 5U &&
            report.blocks[1].loop_start_frame.has_value() &&
            report.blocks[1].loop_start_frame.value() == 2U &&
            report.blocks[1].loop_end_frame.has_value() &&
            report.blocks[1].loop_end_frame.value() == 4U &&
            report.blocks[1].trailing_padding_frame_count == 1U,
        "the looped block grammar metadata is wrong");

    std::fill(bundle.item_bytes.begin(), bundle.item_bytes.end(), std::byte{0});
    std::fill(
        bundle.secondary_bytes.begin(),
        bundle.secondary_bytes.end(),
        std::byte{0xff});
    bundle.descriptors.clear();
    expect(report.items[1].tag == 0x99U, "item metadata borrowed its input");
    expect(
        report.blocks[0].references[0].center_note == std::int8_t{127} &&
            report.blocks[0].references[0].center_fine == std::int8_t{-128} &&
            report.blocks[0].references[1].center_note == std::int8_t{-127} &&
            report.blocks[0].references[1].center_fine == std::int8_t{-2} &&
            report.blocks[1].references[0].global_item_index == 3U,
        "block metadata borrowed its input");
}

void test_loop_without_padding() {
    auto bundle = make_valid_bundle();
    bundle.secondary_bytes.resize(0xa0U);
    const auto report = openrc::analyze_sblk_audio_v1(bundle, kValidLimits);
    expect(report.blocks.size() == 2U, "the no-padding block count is wrong");
    expect(
        report.blocks[1].kind == openrc::SBlkAudioBlockKind::looped &&
            report.blocks[1].size == 0x50U &&
            report.blocks[1].frame_count == 5U &&
            report.blocks[1].content_end_frame == 5U &&
            report.blocks[1].loop_start_frame.value() == 2U &&
            report.blocks[1].loop_end_frame.value() == 4U &&
            report.blocks[1].trailing_padding_frame_count == 0U,
        "a loop without optional padding was not described correctly");
}

void test_mirror_rejections() {
    auto bundle = make_valid_bundle();
    bundle.opaque_a ^= 0x00010000U;
    expect_rejected(bundle, kValidLimits, "a bad descriptor mirror was accepted");

    bundle = make_valid_bundle();
    bundle.opaque_b ^= 1U;
    expect_rejected(bundle, kValidLimits, "a bad item mirror was accepted");

    bundle = make_valid_bundle();
    bundle.opaque_b ^= 0x00010000U;
    expect_rejected(bundle, kValidLimits, "a bad block mirror was accepted");
}

void test_limit_rejections() {
    const auto bundle = make_valid_bundle();
    expect_rejected(
        bundle,
        openrc::SBlkAudioLimits{3U, 2U, kSecondarySize},
        "the item cap was ignored");
    expect_rejected(
        bundle,
        openrc::SBlkAudioLimits{kItemCount, 1U, kSecondarySize},
        "the block cap was ignored");
    expect_rejected(
        bundle,
        openrc::SBlkAudioLimits{kItemCount, 2U, kSecondarySize - 1U},
        "the secondary-byte cap was ignored");
}

void test_reference_rejections() {
    auto bundle = make_valid_bundle();
    write_item_word(bundle, 3U, 6U, 0x51U);
    expect_rejected(bundle, kValidLimits, "an unaligned bank offset was accepted");

    bundle = make_valid_bundle();
    write_item_word(
        bundle,
        3U,
        6U,
        static_cast<std::uint32_t>(kSecondarySize));
    expect_rejected(bundle, kValidLimits, "an out-of-bounds bank offset was accepted");

    bundle = make_valid_bundle();
    bundle.secondary_bytes[0] = std::byte{1};
    expect_rejected(bundle, kValidLimits, "a non-zero referenced lead-in was accepted");

    bundle = make_valid_bundle();
    for (const auto item_index : {0U, 2U, 3U}) {
        write_item_word(bundle, item_index, 6U, 0x50U);
    }
    bundle.opaque_b = (1U << 16U) | static_cast<std::uint32_t>(kItemCount);
    expect_rejected(bundle, kValidLimits, "a block directory missing offset zero was accepted");
}

void test_item_contract_rejections() {
    auto bundle = make_valid_bundle();
    write_item_word(bundle, 0U, 7U, 1U);
    expect_rejected(bundle, kValidLimits, "a non-zero item word 7 was accepted");

    bundle = make_valid_bundle();
    write_item_word(bundle, 1U, 6U, 0x10U);
    expect_rejected(bundle, kValidLimits, "a non-tag-1 pointer word was accepted");

    bundle = make_valid_bundle();
    write_item_word(bundle, 0U, 0U, 0x98U);
    write_item_word(bundle, 0U, 8U, 0U);
    write_item_word(bundle, 0U, 9U, 0U);
    expect_rejected(bundle, kValidLimits, "a descriptor without tag 1 was accepted");

    bundle = make_valid_bundle();
    bundle.descriptors[1].data_offset += 1U;
    expect_rejected(bundle, kValidLimits, "a gap in descriptor item ranges was accepted");

    bundle = make_valid_bundle();
    bundle.item_bytes.push_back(std::byte{0});
    expect_rejected(bundle, kValidLimits, "trailing item data was accepted");

    bundle = make_valid_bundle();
    bundle.descriptors[1].packed_count ^= 1U;
    expect_rejected(bundle, kValidLimits, "a packed item-count disagreement was accepted");
}

void test_secondary_rejections() {
    auto bundle = make_valid_bundle();
    bundle.secondary_bytes.clear();
    expect_rejected(bundle, kValidLimits, "an empty secondary bank was accepted");

    bundle = make_valid_bundle();
    bundle.secondary_bytes.pop_back();
    expect_rejected(bundle, kValidLimits, "a partial ADPCM frame was accepted");

    bundle = make_valid_bundle();
    bundle.secondary_bytes[0x10] = std::byte{0x5c};
    expect_rejected(bundle, kValidLimits, "an invalid ADPCM predictor was accepted");

    bundle = make_valid_bundle();
    bundle.secondary_bytes[0x11] = std::byte{0x04};
    expect_rejected(bundle, kValidLimits, "an unsupported ADPCM flag was accepted");
}

void test_block_grammar_rejections() {
    auto bundle = make_valid_bundle();
    bundle.secondary_bytes[0x41] = std::byte{0x00};
    expect_rejected(bundle, kValidLimits, "a truncated one-shot sequence was accepted");

    bundle = make_valid_bundle();
    bundle.secondary_bytes[0x11] = std::byte{0x02};
    expect_rejected(bundle, kValidLimits, "a bad one-shot flag sequence was accepted");

    bundle = make_valid_bundle();
    bundle.secondary_bytes[0x61] = std::byte{0x06};
    expect_rejected(bundle, kValidLimits, "multiple loop-start markers were accepted");

    bundle = make_valid_bundle();
    bundle.secondary_bytes[0x71] = std::byte{0x02};
    bundle.secondary_bytes[0x81] = std::byte{0x06};
    expect_rejected(bundle, kValidLimits, "a loop without post-loop flag-2 data was accepted");

    bundle = make_valid_bundle();
    bundle.secondary_bytes[0xa2] = std::byte{0x01};
    expect_rejected(bundle, kValidLimits, "a non-zero loop padding frame was accepted");
}

} // namespace

int main() {
    try {
        test_valid_bundle_and_owned_metadata();
        test_loop_without_padding();
        test_mirror_rejections();
        test_limit_rejections();
        test_reference_rejections();
        test_item_contract_rejections();
        test_secondary_rejections();
        test_block_grammar_rejections();
        std::cout << "OpenRC SBlk audio tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC SBlk audio tests failed: " << error.what() << '\n';
        return 1;
    }
}
