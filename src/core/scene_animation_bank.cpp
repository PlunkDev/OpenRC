#include "openrc/scene_animation_bank.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint32_t kHeaderPrefixBytes = 0x14U;
constexpr std::uint8_t kSequenceSoundCount = 0U;
constexpr std::uint8_t kSequenceSentinelByte = 0xffU;

[[noreturn]] void fail(const std::string& message) {
    throw SceneAnimationBankError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint16_t>(byte_value(bytes[offset + 1U])) << 8U);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while computing ") + description);
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while computing ") + description);
    }
    return left * right;
}

[[nodiscard]] std::uint64_t align_up(
    const std::uint64_t value,
    const std::uint64_t alignment,
    const char* const description) {
    const auto mask = alignment - 1U;
    return checked_add(value, mask, description) & ~mask;
}

[[nodiscard]] bool is_zero(
    const std::span<const std::byte> bytes,
    const std::size_t begin,
    const std::size_t end) {
    return std::all_of(
        bytes.begin() + static_cast<std::ptrdiff_t>(begin),
        bytes.begin() + static_cast<std::ptrdiff_t>(end),
        [](const std::byte value) { return value == std::byte{0}; });
}

} // namespace

SceneAnimationBankV1 parse_scene_animation_bank_v1(
    const std::span<const std::byte> bytes,
    const SceneAnimationBankLimitsV1 limits) {
    if (limits.max_input_bytes == 0U || limits.max_actor_tracks == 0U ||
        limits.max_total_frame_ranges == 0U ||
        limits.max_subtitle_entries == 0U ||
        limits.max_total_subtitle_text_bytes == 0U) {
        fail("SceneAnimationBankV1 caller limits must all be non-zero");
    }
    if (bytes.size() > limits.max_input_bytes) {
        fail("SceneAnimationBankV1 exceeds the caller's input-byte limit");
    }
    if (bytes.size() < 0x20U ||
        bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
        (bytes.size() & (kSceneAnimationAlignmentV1 - 1U)) != 0U) {
        fail("SceneAnimationBankV1 has an invalid input envelope");
    }
    const auto header_tag = read_le32(bytes, 0x08U);
    if (header_tag != kSceneAnimationBankTagV1 &&
        header_tag != kSceneAnimationBankAlternateTagV1) {
        fail("SceneAnimationBankV1 has an invalid tag");
    }

    SceneAnimationBankV1 result;
    result.input_bytes = bytes.size();
    result.unknown_word_0 = read_le32(bytes, 0x00U);
    result.header_tag = header_tag;
    result.subtitle_table_offset = read_le32(bytes, 0x04U);
    result.actor_track_count = read_le32(bytes, 0x0cU);
    result.header_bytes = read_le32(bytes, 0x10U);
    if (result.actor_track_count == 0U ||
        result.actor_track_count > limits.max_actor_tracks) {
        fail("SceneAnimationBankV1 has an invalid actor-track count");
    }

    const auto offset_table_bytes = checked_multiply(
        result.actor_track_count,
        sizeof(std::uint32_t),
        "the scene actor-offset table");
    const auto unaligned_header_bytes = checked_add(
        kHeaderPrefixBytes,
        offset_table_bytes,
        "the scene header");
    const auto expected_header_bytes = align_up(
        unaligned_header_bytes,
        kSceneAnimationAlignmentV1,
        "the scene header");
    if (expected_header_bytes != result.header_bytes ||
        result.header_bytes > bytes.size()) {
        fail("SceneAnimationBankV1 has an invalid header size");
    }
    if (!is_zero(
            bytes,
            static_cast<std::size_t>(unaligned_header_bytes),
            result.header_bytes)) {
        fail("SceneAnimationBankV1 has non-zero header padding");
    }
    result.header_range = SceneAnimationRangeV1{0U, result.header_bytes};
    result.header_padding_range = SceneAnimationRangeV1{
        unaligned_header_bytes,
        result.header_bytes - unaligned_header_bytes};

    const auto actor_data_end = result.subtitle_table_offset == 0U
        ? static_cast<std::uint32_t>(bytes.size())
        : result.subtitle_table_offset;
    if (actor_data_end > bytes.size() ||
        actor_data_end <= result.header_bytes ||
        (actor_data_end & (kSceneAnimationAlignmentV1 - 1U)) != 0U) {
        fail("SceneAnimationBankV1 has an invalid actor-data boundary");
    }

    result.actor_offsets.reserve(result.actor_track_count);
    std::uint32_t previous_actor_offset = 0U;
    for (std::uint32_t actor_index = 0U;
         actor_index < result.actor_track_count;
         ++actor_index) {
        const auto table_offset = static_cast<std::size_t>(
            kHeaderPrefixBytes + actor_index * sizeof(std::uint32_t));
        const auto actor_offset = read_le32(bytes, table_offset);
        if (actor_offset <= result.header_bytes ||
            actor_offset >= actor_data_end ||
            (actor_offset & (kSceneAnimationAlignmentV1 - 1U)) != 0U ||
            (actor_index != 0U && actor_offset <= previous_actor_offset)) {
            fail("SceneAnimationBankV1 has invalid actor offsets");
        }
        result.actor_offsets.push_back(actor_offset);
        previous_actor_offset = actor_offset;
    }

    const auto camera_bytes =
        result.actor_offsets.front() - result.header_bytes;
    if (camera_bytes == 0U ||
        (camera_bytes % kSceneAnimationCameraRecordBytesV1) != 0U) {
        fail("SceneAnimationBankV1 has an invalid camera-track envelope");
    }
    result.camera_record_count =
        camera_bytes / kSceneAnimationCameraRecordBytesV1;
    result.camera_track_range = SceneAnimationRangeV1{
        result.header_bytes, camera_bytes};

    result.actors.reserve(result.actor_track_count);
    std::uint64_t total_frame_ranges = 0U;
    auto logical_actor_data_end = actor_data_end;
    for (std::uint32_t actor_index = 0U;
         actor_index < result.actor_track_count;
         ++actor_index) {
        const auto section_begin = result.actor_offsets[actor_index];
        const auto section_end = actor_index + 1U < result.actor_track_count
            ? result.actor_offsets[actor_index + 1U]
            : actor_data_end;
        const auto section_bytes = section_end - section_begin;
        if (section_bytes <
            kSceneAnimationActorHeaderBytesV1 +
                kSceneAnimationSequenceHeaderBytesV1 +
                sizeof(std::uint32_t) +
                kSceneAnimationFrameHeaderBytesV1 +
                kSceneAnimationRootTransformBytesV1) {
            fail("SceneAnimationBankV1 has a truncated actor section");
        }

        SceneAnimationActorTrackV1 actor;
        actor.class_id = read_le32(bytes, section_begin);
        actor.scene_record_count = read_le32(bytes, section_begin + 0x04U);
        actor.scene_record_index = read_le32(bytes, section_begin + 0x08U);
        actor.root_transform_offset = read_le32(bytes, section_begin + 0x0cU);
        if (actor.scene_record_count == 0U ||
            actor.scene_record_index >= actor.scene_record_count) {
            fail("SceneAnimationBankV1 has invalid scene-record metadata");
        }
        if (actor_index == 0U) {
            result.scene_record_count = actor.scene_record_count;
            result.scene_record_index = actor.scene_record_index;
        } else if (actor.scene_record_count != result.scene_record_count ||
                   actor.scene_record_index != result.scene_record_index) {
            fail("SceneAnimationBankV1 actor scene-record metadata disagrees");
        }

        const auto sequence_begin = checked_add(
            section_begin,
            kSceneAnimationActorHeaderBytesV1,
            "the scene animation sequence");
        if (actor.root_transform_offset <= sequence_begin ||
            actor.root_transform_offset >= section_end ||
            (actor.root_transform_offset &
             (kSceneAnimationAlignmentV1 - 1U)) != 0U ||
            checked_add(
                sequence_begin,
                kSceneAnimationSequenceHeaderBytesV1,
                "the scene sequence header") > actor.root_transform_offset) {
            fail("SceneAnimationBankV1 has an invalid root-transform offset");
        }

        for (std::size_t word_index = 0U;
             word_index < actor.sequence_prefix_words.size();
             ++word_index) {
            actor.sequence_prefix_words[word_index] = read_le32(
                bytes,
                static_cast<std::size_t>(sequence_begin) +
                    word_index * sizeof(std::uint32_t));
        }
        actor.frame_count = byte_value(bytes[sequence_begin + 0x10U]);
        actor.sequence_control_bytes = {
            byte_value(bytes[sequence_begin + 0x11U]),
            byte_value(bytes[sequence_begin + 0x12U]),
            byte_value(bytes[sequence_begin + 0x13U])};
        actor.sequence_word_14 = read_le32(bytes, sequence_begin + 0x14U);
        actor.sequence_word_18 = read_le32(bytes, sequence_begin + 0x18U);
        if (actor.frame_count == 0U ||
            actor.sequence_control_bytes[0U] != kSequenceSoundCount ||
            actor.sequence_control_bytes[1U] != kSequenceSentinelByte ||
            actor.sequence_control_bytes[2U] != kSequenceSentinelByte ||
            actor.sequence_word_14 != 0U || actor.sequence_word_18 != 0U) {
            fail("SceneAnimationBankV1 has invalid sequence controls");
        }
        if (actor_index == 0U) {
            result.frame_count = actor.frame_count;
        } else if (actor.frame_count != result.frame_count) {
            fail("SceneAnimationBankV1 actor frame counts disagree");
        }

        total_frame_ranges = checked_add(
            total_frame_ranges,
            actor.frame_count,
            "scene animation frame ranges");
        if (total_frame_ranges > limits.max_total_frame_ranges) {
            fail("SceneAnimationBankV1 exceeds the caller's frame-range limit");
        }

        const auto frame_table_bytes = checked_multiply(
            actor.frame_count,
            sizeof(std::uint32_t),
            "the scene frame-offset table");
        const auto frame_table_begin = checked_add(
            sequence_begin,
            kSceneAnimationSequenceHeaderBytesV1,
            "the scene frame-offset table");
        const auto frame_table_end = checked_add(
            frame_table_begin,
            frame_table_bytes,
            "the scene frame-offset table");
        const auto first_frame_relative = align_up(
            kSceneAnimationSequenceHeaderBytesV1 + frame_table_bytes,
            kSceneAnimationAlignmentV1,
            "the first scene frame");
        const auto first_frame_absolute = checked_add(
            sequence_begin,
            first_frame_relative,
            "the first scene frame");
        if (frame_table_end > actor.root_transform_offset ||
            first_frame_absolute > actor.root_transform_offset ||
            !is_zero(
                bytes,
                static_cast<std::size_t>(frame_table_end),
                static_cast<std::size_t>(first_frame_absolute))) {
            fail("SceneAnimationBankV1 has an invalid frame-table envelope");
        }
        actor.sequence_header_range = SceneAnimationRangeV1{
            sequence_begin, kSceneAnimationSequenceHeaderBytesV1};
        actor.frame_offset_table_range = SceneAnimationRangeV1{
            frame_table_begin, frame_table_bytes};

        std::vector<std::uint32_t> raw_offsets;
        std::vector<std::uint32_t> relative_offsets;
        raw_offsets.reserve(actor.frame_count);
        relative_offsets.reserve(actor.frame_count);
        std::uint32_t previous_relative_offset = 0U;
        for (std::uint32_t frame_index = 0U;
             frame_index < actor.frame_count;
             ++frame_index) {
            const auto raw_offset = read_le32(
                bytes,
                static_cast<std::size_t>(frame_table_begin) +
                    frame_index * sizeof(std::uint32_t));
            if ((raw_offset & kSceneAnimationFrameFlagsMaskV1) != 0U) {
                fail("SceneAnimationBankV1 has unsupported frame-offset flags");
            }
            const auto relative_offset = raw_offset;
            const auto absolute_offset = checked_add(
                sequence_begin,
                relative_offset,
                "a scene animation frame offset");
            if ((relative_offset &
                 (kSceneAnimationAlignmentV1 - 1U)) != 0U ||
                absolute_offset >= actor.root_transform_offset ||
                (frame_index == 0U &&
                 relative_offset != first_frame_relative) ||
                (frame_index != 0U &&
                 relative_offset <= previous_relative_offset)) {
                fail("SceneAnimationBankV1 has invalid frame offsets");
            }
            raw_offsets.push_back(raw_offset);
            relative_offsets.push_back(relative_offset);
            previous_relative_offset = relative_offset;
        }

        actor.frames.reserve(actor.frame_count);
        for (std::uint32_t frame_index = 0U;
             frame_index < actor.frame_count;
             ++frame_index) {
            const auto frame_begin = checked_add(
                sequence_begin,
                relative_offsets[frame_index],
                "a scene animation frame");
            const auto frame_end = frame_index + 1U < actor.frame_count
                ? checked_add(
                      sequence_begin,
                      relative_offsets[frame_index + 1U],
                      "a scene animation frame")
                : actor.root_transform_offset;
            if (frame_end <= frame_begin) {
                fail("SceneAnimationBankV1 has an empty animation frame");
            }

            SceneAnimationFrameV1 frame;
            frame.raw_relative_offset = raw_offsets[frame_index];
            frame.relative_offset = relative_offsets[frame_index];
            frame.range = SceneAnimationRangeV1{
                frame_begin, frame_end - frame_begin};
            if (frame.range.size < kSceneAnimationFrameHeaderBytesV1) {
                fail("SceneAnimationBankV1 has a truncated frame");
            }
            frame.unknown_word_0 = read_le32(bytes, frame_begin);
            frame.unknown_4 = read_le16(bytes, frame_begin + 0x04U);
            frame.data_size_qwords = read_le16(bytes, frame_begin + 0x06U);
            frame.joint_data_size = read_le16(bytes, frame_begin + 0x08U);
            frame.thing_1_count = read_le16(bytes, frame_begin + 0x0aU);
            frame.unknown_c = read_le16(bytes, frame_begin + 0x0cU);
            frame.thing_2_count = read_le16(bytes, frame_begin + 0x0eU);
            const auto declared_frame_bytes = checked_add(
                kSceneAnimationFrameHeaderBytesV1,
                checked_multiply(
                    frame.data_size_qwords,
                    kSceneAnimationAlignmentV1,
                    "scene frame data"),
                "a scene frame");
            if (declared_frame_bytes != frame.range.size) {
                fail("SceneAnimationBankV1 frame size disagrees");
            }
            actor.frames.push_back(frame);
        }

        const auto root_transform_bytes = checked_multiply(
            actor.frame_count,
            kSceneAnimationRootTransformBytesV1,
            "scene root transforms");
        const auto root_transform_end = checked_add(
            actor.root_transform_offset,
            root_transform_bytes,
            "scene root transforms");
        const auto must_close_declared_boundary =
            actor_index + 1U < result.actor_track_count ||
            result.subtitle_table_offset != 0U;
        if ((must_close_declared_boundary &&
             root_transform_end != section_end) ||
            (!must_close_declared_boundary &&
             root_transform_end > section_end)) {
            fail("SceneAnimationBankV1 root transforms do not close the actor");
        }
        if (!must_close_declared_boundary) {
            logical_actor_data_end =
                static_cast<std::uint32_t>(root_transform_end);
        }
        for (std::uint32_t frame_index = 0U;
             frame_index < actor.frame_count;
             ++frame_index) {
            const auto transform_offset =
                actor.root_transform_offset +
                frame_index * kSceneAnimationRootTransformBytesV1;
            if (read_le32(bytes, transform_offset + 0x0cU) != 0U) {
                fail("SceneAnimationBankV1 root transform has a non-zero W");
            }
        }
        actor.root_transform_range = SceneAnimationRangeV1{
            actor.root_transform_offset, root_transform_bytes};
        actor.section_range = SceneAnimationRangeV1{
            section_begin, root_transform_end - section_begin};
        result.actors.push_back(std::move(actor));
    }

    const auto expected_camera_records =
        static_cast<std::uint32_t>(result.frame_count) * 2U - 1U;
    const auto expected_trimmed_camera_records = result.frame_count > 1U
        ? static_cast<std::uint32_t>(result.frame_count) * 2U - 3U
        : expected_camera_records;
    if (result.camera_record_count != expected_camera_records &&
        result.camera_record_count != expected_trimmed_camera_records) {
        fail("SceneAnimationBankV1 camera and actor frame counts disagree");
    }

    if (result.subtitle_table_offset != 0U) {
        result.trailing_range = SceneAnimationRangeV1{
            result.subtitle_table_offset,
            bytes.size() - result.subtitle_table_offset};
        try {
            result.localized_subtitles =
                parse_localized_subtitle_directory_v1(
                    bytes,
                    result.subtitle_table_offset,
                    LocalizedSubtitleBankLimitsV1{
                        limits.max_input_bytes,
                        limits.max_subtitle_entries,
                        limits.max_total_subtitle_text_bytes});
        } catch (const LocalizedSubtitleBankError& error) {
            fail(std::string("SceneAnimationBankV1 has an invalid subtitle tail: ") +
                 error.what());
        }
    } else {
        result.trailing_range = SceneAnimationRangeV1{
            logical_actor_data_end,
            bytes.size() - logical_actor_data_end};
    }
    return result;
}

} // namespace openrc
