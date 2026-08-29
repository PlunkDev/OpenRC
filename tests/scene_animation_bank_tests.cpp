#include "openrc/scene_animation_bank.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::SceneAnimationBankLimitsV1 kLimits{
    0x10000U,
    32U,
    4096U,
    128U,
    0x10000U,
};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

[[nodiscard]] std::uint32_t align_16(const std::uint32_t value) {
    return (value + 0x0fU) & ~0x0fU;
}

void write_actor(
    std::vector<std::byte>& bytes,
    const std::uint32_t actor_offset,
    const std::uint32_t class_id,
    const std::uint32_t record_count,
    const std::uint32_t record_index) {
    constexpr std::uint8_t kFrameCount = 3U;
    constexpr std::uint32_t kFirstFrameRelative = 0x30U;
    constexpr std::uint32_t kSecondFrameRelative = 0x50U;
    constexpr std::uint32_t kThirdFrameRelative = 0x80U;
    constexpr std::uint32_t kRootRelative = 0xb0U;
    const auto sequence_offset = actor_offset + 0x10U;
    const auto root_offset = actor_offset + kRootRelative;

    write_le32(bytes, actor_offset, class_id);
    write_le32(bytes, actor_offset + 0x04U, record_count);
    write_le32(bytes, actor_offset + 0x08U, record_index);
    write_le32(bytes, actor_offset + 0x0cU, root_offset);
    bytes[sequence_offset + 0x10U] = static_cast<std::byte>(kFrameCount);
    bytes[sequence_offset + 0x12U] = std::byte{0xff};
    bytes[sequence_offset + 0x13U] = std::byte{0xff};
    write_le32(bytes, sequence_offset + 0x1cU, kFirstFrameRelative);
    write_le32(bytes, sequence_offset + 0x20U, kSecondFrameRelative);
    write_le32(bytes, sequence_offset + 0x24U, kThirdFrameRelative);

    write_le16(
        bytes,
        sequence_offset + kFirstFrameRelative + 0x06U,
        1U);
    write_le16(
        bytes,
        sequence_offset + kSecondFrameRelative + 0x06U,
        2U);
    write_le16(
        bytes,
        sequence_offset + kThirdFrameRelative + 0x06U,
        1U);
}

void write_subtitles(
    std::vector<std::byte>& bytes,
    const std::uint32_t table_offset) {
    write_le16(bytes, table_offset, 10U);
    write_le16(bytes, table_offset + 0x02U, 20U);
    for (std::size_t language = 0U;
         language < openrc::kLocalizedSubtitleLanguageCountV1;
         ++language) {
        const auto relative = static_cast<std::uint16_t>(
            0x20U + language * 4U);
        write_le16(
            bytes,
            table_offset + 0x04U + language * sizeof(std::uint16_t),
            relative);
        bytes[table_offset + relative] = static_cast<std::byte>(
            static_cast<unsigned char>('A' + language));
    }
    write_le32(bytes, table_offset + 0x10U, 0xffffffffU);
}

[[nodiscard]] std::vector<std::byte> make_bank(
    const std::uint32_t actor_count,
    const bool with_subtitles,
    const bool trimmed_camera = false) {
    constexpr std::uint32_t kFrameCount = 3U;
    const auto camera_records = trimmed_camera
        ? 2U * kFrameCount - 3U
        : 2U * kFrameCount - 1U;
    constexpr std::uint32_t kActorBytes = 0xe0U;
    constexpr std::uint32_t kSubtitleBytes = 0x40U;
    const auto header_bytes = align_16(0x14U + actor_count * 4U);
    const auto first_actor =
        header_bytes +
        camera_records * openrc::kSceneAnimationCameraRecordBytesV1;
    const auto actor_data_end = first_actor + actor_count * kActorBytes;
    const auto input_bytes = actor_data_end +
        (with_subtitles ? kSubtitleBytes : 0U);
    std::vector<std::byte> bytes(input_bytes, std::byte{0});

    write_le32(bytes, 0x00U, 0x1234U);
    write_le32(bytes, 0x04U, with_subtitles ? actor_data_end : 0U);
    write_le32(bytes, 0x08U, openrc::kSceneAnimationBankTagV1);
    write_le32(bytes, 0x0cU, actor_count);
    write_le32(bytes, 0x10U, header_bytes);
    for (std::uint32_t actor_index = 0U;
         actor_index < actor_count;
         ++actor_index) {
        const auto actor_offset = first_actor + actor_index * kActorBytes;
        write_le32(bytes, 0x14U + actor_index * 4U, actor_offset);
        write_actor(
            bytes,
            actor_offset,
            actor_index == 0U ? 0U : 10U + actor_index,
            7U,
            2U);
    }
    if (with_subtitles) {
        write_subtitles(bytes, actor_data_end);
    }
    return bytes;
}

template <typename Mutation>
void expect_rejected(
    const std::uint32_t actor_count,
    const bool with_subtitles,
    Mutation&& mutation,
    const std::string& message) {
    auto bytes = make_bank(actor_count, with_subtitles);
    std::invoke(std::forward<Mutation>(mutation), bytes);
    try {
        (void)openrc::parse_scene_animation_bank_v1(bytes, kLimits);
    } catch (const openrc::SceneAnimationBankError&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_valid_two_actor_bank() {
    const auto source = make_bank(2U, false);
    const auto result = openrc::parse_scene_animation_bank_v1(source, kLimits);
    expect(
        result.input_bytes == 0x280U && result.unknown_word_0 == 0x1234U &&
            result.subtitle_table_offset == 0U &&
            result.actor_track_count == 2U && result.header_bytes == 0x20U &&
            result.camera_record_count == 5U && result.frame_count == 3U &&
            result.scene_record_count == 7U && result.scene_record_index == 2U,
        "scene animation bank header metadata is wrong");
    expect(
        result.header_padding_range ==
                openrc::SceneAnimationRangeV1{0x1cU, 4U} &&
            result.camera_track_range ==
                openrc::SceneAnimationRangeV1{0x20U, 0xa0U} &&
            result.actor_offsets == std::vector<std::uint32_t>{0xc0U, 0x1a0U} &&
            !result.localized_subtitles.has_value(),
        "scene animation bank top-level ranges are wrong");

    const auto& first = result.actors.at(0U);
    expect(
        first.class_id == 0U && first.frames.size() == 3U &&
            first.frame_offset_table_range ==
                openrc::SceneAnimationRangeV1{0xecU, 0x0cU} &&
            first.frames[0U].range ==
                openrc::SceneAnimationRangeV1{0x100U, 0x20U} &&
            first.frames[1U].range ==
                openrc::SceneAnimationRangeV1{0x120U, 0x30U} &&
            first.frames[2U].range ==
                openrc::SceneAnimationRangeV1{0x150U, 0x20U} &&
            first.root_transform_range ==
                openrc::SceneAnimationRangeV1{0x170U, 0x30U},
        "scene animation actor ranges are wrong");
    expect(
        first.frames[0U].data_size_qwords == 1U &&
            first.frames[1U].data_size_qwords == 2U,
        "scene frame headers are wrong");
}

void test_subtitle_tail() {
    auto source = make_bank(1U, true);
    const auto result = openrc::parse_scene_animation_bank_v1(source, kLimits);
    expect(
        result.input_bytes == 0x1e0U &&
            result.trailing_range ==
                openrc::SceneAnimationRangeV1{0x1a0U, 0x40U} &&
            result.localized_subtitles.has_value() &&
            result.localized_subtitles->entries.size() == 1U &&
            result.localized_subtitles->total_text_bytes == 5U,
        "scene subtitle tail was not parsed");
}

void test_trimmed_camera_and_opaque_actor_suffix() {
    const auto trimmed = openrc::parse_scene_animation_bank_v1(
        make_bank(1U, false, true), kLimits);
    expect(
        trimmed.camera_record_count == 3U && trimmed.frame_count == 3U,
        "the observed endpoint-trimmed camera variant was rejected");

    auto alternate_tag = make_bank(1U, false);
    write_le32(
        alternate_tag,
        0x08U,
        openrc::kSceneAnimationBankAlternateTagV1);
    const auto alternate = openrc::parse_scene_animation_bank_v1(
        alternate_tag, kLimits);
    expect(
        alternate.header_tag == openrc::kSceneAnimationBankAlternateTagV1,
        "the structurally identical F8 scene variant was rejected");

    auto with_suffix = make_bank(2U, false);
    with_suffix.resize(0x2a0U, std::byte{0});
    with_suffix[0x280U] = std::byte{0x5a};
    const auto suffix = openrc::parse_scene_animation_bank_v1(
        with_suffix, kLimits);
    expect(
        suffix.actors.back().section_range ==
                openrc::SceneAnimationRangeV1{0x1a0U, 0xe0U} &&
            suffix.trailing_range ==
                openrc::SceneAnimationRangeV1{0x280U, 0x20U},
        "opaque bytes after the final actor were not isolated");
}

void test_limits() {
    const auto bytes = make_bank(2U, false);
    for (const auto limits : {
             openrc::SceneAnimationBankLimitsV1{0U, 2U, 6U, 2U, 2U},
             openrc::SceneAnimationBankLimitsV1{0x1000U, 0U, 6U, 2U, 2U},
             openrc::SceneAnimationBankLimitsV1{0x1000U, 2U, 0U, 2U, 2U},
             openrc::SceneAnimationBankLimitsV1{0x1000U, 2U, 6U, 0U, 2U},
             openrc::SceneAnimationBankLimitsV1{0x1000U, 2U, 6U, 2U, 0U}}) {
        try {
            (void)openrc::parse_scene_animation_bank_v1(bytes, limits);
        } catch (const openrc::SceneAnimationBankError&) {
            continue;
        }
        throw std::runtime_error("a zero scene animation limit was accepted");
    }
    for (const auto limits : {
             openrc::SceneAnimationBankLimitsV1{0x27fU, 2U, 6U, 2U, 2U},
             openrc::SceneAnimationBankLimitsV1{0x1000U, 1U, 6U, 2U, 2U},
             openrc::SceneAnimationBankLimitsV1{0x1000U, 2U, 5U, 2U, 2U}}) {
        try {
            (void)openrc::parse_scene_animation_bank_v1(bytes, limits);
        } catch (const openrc::SceneAnimationBankError&) {
            continue;
        }
        throw std::runtime_error("a scene animation bound was ignored");
    }
}

void test_structural_rejections() {
    expect_rejected(
        2U, false,
        [](auto& bytes) { write_le32(bytes, 0x08U, 0U); },
        "a bad scene animation tag was accepted");
    expect_rejected(
        2U, false,
        [](auto& bytes) { write_le32(bytes, 0x0cU, 0U); },
        "a zero scene actor count was accepted");
    expect_rejected(
        2U, false,
        [](auto& bytes) { write_le32(bytes, 0x10U, 0x30U); },
        "a bad scene header size was accepted");
    expect_rejected(
        2U, false,
        [](auto& bytes) { bytes[0x1cU] = std::byte{1}; },
        "non-zero scene header padding was accepted");
    expect_rejected(
        2U, false,
        [](auto& bytes) { write_le32(bytes, 0x14U, 0xc1U); },
        "an unaligned scene actor was accepted");
    expect_rejected(
        2U, false,
        [](auto& bytes) { write_le32(bytes, 0xc8U, 7U); },
        "an out-of-range scene record index was accepted");
    expect_rejected(
        2U, false,
        [](auto& bytes) { write_le32(bytes, 0x1a4U, 8U); },
        "disagreeing actor record metadata was accepted");
    expect_rejected(
        2U, false,
        [](auto& bytes) { bytes[0xe2U] = std::byte{0}; },
        "a bad scene sequence sentinel was accepted");
    expect_rejected(
        2U, false,
        [](auto& bytes) { write_le32(bytes, 0xecU, 0x40U); },
        "a bad first-frame offset was accepted");
    expect_rejected(
        2U, false,
        [](auto& bytes) { write_le32(bytes, 0xecU, 0x10000030U); },
        "unproven frame-offset flags were accepted");
    expect_rejected(
        2U, false,
        [](auto& bytes) { write_le16(bytes, 0x106U, 2U); },
        "a bad frame size was accepted");
    expect_rejected(
        2U, false,
        [](auto& bytes) { write_le32(bytes, 0x17cU, 1U); },
        "a non-zero root-transform W was accepted");
    expect_rejected(
        1U, true,
        [](auto& bytes) { write_le32(bytes, 0x1b0U, 0U); },
        "a malformed scene subtitle sentinel was accepted");
}

} // namespace

int main() {
    try {
        test_valid_two_actor_bank();
        test_subtitle_tail();
        test_trimmed_camera_and_opaque_actor_suffix();
        test_limits();
        test_structural_rejections();
        std::cout << "OpenRC SceneAnimationBankV1 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC SceneAnimationBankV1 tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
