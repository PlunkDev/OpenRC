#pragma once

#include "openrc/localized_subtitle_bank.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kSceneAnimationBankTagV1 = 0xfffffffaU;
inline constexpr std::uint32_t kSceneAnimationBankAlternateTagV1 = 0xfffffff8U;
inline constexpr std::uint32_t kSceneAnimationAlignmentV1 = 0x10U;
inline constexpr std::uint32_t kSceneAnimationCameraRecordBytesV1 = 0x20U;
inline constexpr std::uint32_t kSceneAnimationActorHeaderBytesV1 = 0x10U;
inline constexpr std::uint32_t kSceneAnimationSequenceHeaderBytesV1 = 0x1cU;
inline constexpr std::uint32_t kSceneAnimationFrameHeaderBytesV1 = 0x10U;
inline constexpr std::uint32_t kSceneAnimationRootTransformBytesV1 = 0x10U;
inline constexpr std::uint32_t kSceneAnimationFrameFlagsMaskV1 = 0xf0000000U;

struct SceneAnimationBankLimitsV1 {
    std::uint64_t max_input_bytes = 0U;
    std::uint64_t max_actor_tracks = 0U;
    std::uint64_t max_total_frame_ranges = 0U;
    std::uint64_t max_subtitle_entries = 0U;
    std::uint64_t max_total_subtitle_text_bytes = 0U;
};

struct SceneAnimationRangeV1 {
    std::uint64_t offset = 0U;
    std::uint64_t size = 0U;

    [[nodiscard]] bool operator==(const SceneAnimationRangeV1&) const = default;
};

struct SceneAnimationFrameV1 {
    std::uint32_t raw_relative_offset = 0U;
    std::uint32_t relative_offset = 0U;
    SceneAnimationRangeV1 range;

    // The remaining payload stays borrowed from the caller and is represented
    // by range.
    std::uint32_t unknown_word_0 = 0U;
    std::uint16_t unknown_4 = 0U;
    std::uint16_t data_size_qwords = 0U;
    std::uint16_t joint_data_size = 0U;
    std::uint16_t thing_1_count = 0U;
    std::uint16_t unknown_c = 0U;
    std::uint16_t thing_2_count = 0U;
};

struct SceneAnimationActorTrackV1 {
    SceneAnimationRangeV1 section_range;
    std::uint32_t class_id = 0U;
    std::uint32_t scene_record_count = 0U;
    std::uint32_t scene_record_index = 0U;
    std::uint32_t root_transform_offset = 0U;

    std::array<std::uint32_t, 4U> sequence_prefix_words{};
    std::uint8_t frame_count = 0U;
    std::array<std::uint8_t, 3U> sequence_control_bytes{};
    std::uint32_t sequence_word_14 = 0U;
    std::uint32_t sequence_word_18 = 0U;

    SceneAnimationRangeV1 sequence_header_range;
    SceneAnimationRangeV1 frame_offset_table_range;
    std::vector<SceneAnimationFrameV1> frames;
    SceneAnimationRangeV1 root_transform_range;
};

struct SceneAnimationBankV1 {
    std::uint64_t input_bytes = 0U;
    std::uint32_t unknown_word_0 = 0U;
    std::uint32_t header_tag = 0U;
    std::uint32_t subtitle_table_offset = 0U;
    std::uint32_t actor_track_count = 0U;
    std::uint32_t header_bytes = 0U;

    SceneAnimationRangeV1 header_range;
    SceneAnimationRangeV1 header_padding_range;
    SceneAnimationRangeV1 camera_track_range;
    std::uint32_t camera_record_count = 0U;
    std::uint8_t frame_count = 0U;
    std::uint32_t scene_record_count = 0U;
    std::uint32_t scene_record_index = 0U;

    std::vector<std::uint32_t> actor_offsets;
    std::vector<SceneAnimationActorTrackV1> actors;
    SceneAnimationRangeV1 trailing_range;
    std::optional<LocalizedSubtitleDirectoryV1> localized_subtitles;
};

class SceneAnimationBankError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses one complete decoded local-scene payload. Camera samples, actor
// sequence frames, root transforms, and the optional PAL subtitle tail are
// structurally validated; large animation bodies remain zero-copy ranges.
[[nodiscard]] SceneAnimationBankV1 parse_scene_animation_bank_v1(
    std::span<const std::byte> bytes,
    SceneAnimationBankLimitsV1 limits);

} // namespace openrc
