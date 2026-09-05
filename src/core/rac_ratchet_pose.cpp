#include "openrc/rac_ratchet_pose.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint64_t kJointQuaternionBytes = 8U;
constexpr std::uint64_t kSparseRecordBytes = 8U;
constexpr std::uint16_t kScaleLocalBit = UINT16_C(0x8000);
constexpr std::uint16_t kSparseJointMask = UINT16_C(0x00ff);
constexpr std::uint16_t kScaleAllowedTagBits =
    static_cast<std::uint16_t>(kScaleLocalBit | kSparseJointMask);

[[noreturn]] void fail(const std::string &message) {
  throw RacRatchetPoseError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(const std::span<const std::byte> bytes,
                                      const std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(byte_value(bytes[offset])) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(byte_value(bytes[offset + 1U])) << 8U);
}

[[nodiscard]] std::int16_t read_le_i16(const std::span<const std::byte> bytes,
                                       const std::size_t offset) noexcept {
  return std::bit_cast<std::int16_t>(read_le16(bytes, offset));
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string("Integer overflow while calculating ") + description);
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(const std::uint64_t left,
                                             const std::uint64_t right,
                                             const char *const description) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string("Integer overflow while calculating ") + description);
  }
  return left * right;
}

[[nodiscard]] std::span<const std::byte>
require_range(const std::span<const std::byte> bytes,
              const RacRatchetSequenceRangeV1 range,
              const char *const description) {
  if (range.offset > bytes.size() || range.size > bytes.size() - range.offset) {
    fail(std::string(description) + " exceeds the owned sequence bytes");
  }
  return bytes.subspan(static_cast<std::size_t>(range.offset),
                       static_cast<std::size_t>(range.size));
}

void validate_limits(const RacRatchetPoseLimitsV1 &limits) {
  if (limits.max_joints == 0U || limits.max_frame_payload_bytes == 0U ||
      limits.max_sparse_scale_records == 0U ||
      limits.max_sparse_translation_records == 0U ||
      !std::isfinite(limits.minimum_quaternion_length) ||
      limits.minimum_quaternion_length <= 0.0) {
    fail("RacRatchetPoseV1 limits must be finite, positive, and explicit");
  }
}

[[nodiscard]] float canonical_float(const double value,
                                    const char *const description) {
  if (!std::isfinite(value) ||
      value > static_cast<double>(std::numeric_limits<float>::max()) ||
      value < -static_cast<double>(std::numeric_limits<float>::max())) {
    fail(std::string("RacRatchetPoseV1 produced an out-of-range ") +
         description);
  }
  const auto result = static_cast<float>(value);
  if (!std::isfinite(result)) {
    fail(std::string("RacRatchetPoseV1 produced a non-finite ") + description);
  }
  return result == 0.0F ? 0.0F : result;
}

void validate_transform(const ActorAffineTransformV1 &transform,
                        const char *const description) {
  for (const auto value : transform.values) {
    if (!std::isfinite(value)) {
      fail(std::string(description) + " contains a non-finite value");
    }
  }
}

[[nodiscard]] ActorAffineTransformV1
compose(const ActorAffineTransformV1 &parent,
        const ActorAffineTransformV1 &local) {
  ActorAffineTransformV1 result;
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      double value = 0.0;
      for (std::size_t inner = 0U; inner < 3U; ++inner) {
        value += static_cast<double>(parent.values[row * 4U + inner]) *
                 local.values[inner * 4U + column];
      }
      result.values[row * 4U + column] =
          canonical_float(value, "composed linear transform");
    }

    double translation = parent.values[row * 4U + 3U];
    for (std::size_t inner = 0U; inner < 3U; ++inner) {
      translation += static_cast<double>(parent.values[row * 4U + inner]) *
                     local.values[inner * 4U + 3U];
    }
    result.values[row * 4U + 3U] =
        canonical_float(translation, "composed translation");
  }
  return result;
}

[[nodiscard]] ActorAffineTransformV1
make_local_transform(const RacRatchetJointPoseV1 &joint) {
  const auto x = static_cast<double>(joint.normalized_rotation_xyzw[0U]);
  const auto y = static_cast<double>(joint.normalized_rotation_xyzw[1U]);
  const auto z = static_cast<double>(joint.normalized_rotation_xyzw[2U]);
  const auto w = static_cast<double>(joint.normalized_rotation_xyzw[3U]);
  const std::array<double, 9U> rotation{
      1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w),
      2.0 * (x * z + y * w),       2.0 * (x * y + z * w),
      1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w),
      2.0 * (x * z - y * w),       2.0 * (y * z + x * w),
      1.0 - 2.0 * (x * x + y * y),
  };

  ActorAffineTransformV1 result;
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      result.values[row * 4U + column] = canonical_float(
          rotation[row * 3U + column] * joint.local_scale[column],
          "local linear transform");
    }
    result.values[row * 4U + 3U] = joint.translation[row];
  }
  return result;
}

[[nodiscard]] ActorAffineTransformV1
apply_terminal_scale(const ActorAffineTransformV1 &hierarchical_global,
                     const std::array<float, 3U> &terminal_scale) {
  auto result = hierarchical_global;
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      result.values[row * 4U + column] = canonical_float(
          static_cast<double>(hierarchical_global.values[row * 4U + column]) *
              terminal_scale[column],
          "terminally scaled global transform");
    }
  }
  return result;
}

void validate_frame_layout(const RacRatchetSequenceV1 &sequence,
                           const RacRatchetSequenceFrameV1 &frame,
                           const RacRatchetPoseLimitsV1 &limits) {
  const auto payload_bytes = checked_multiply(
      frame.regular_payload_qwords, 0x10U, "the regular frame payload size");
  if (payload_bytes == 0U || payload_bytes > limits.max_frame_payload_bytes ||
      frame.opaque_payload_range.size != payload_bytes) {
    fail(
        "RacRatchetPoseV1 received an invalid or caller-limited frame payload");
  }

  const auto supplemental_a_bytes =
      checked_multiply(frame.supplemental_a_count, kSparseRecordBytes,
                       "the sparse scale byte count");
  const auto supplemental_b_bytes =
      checked_multiply(frame.supplemental_b_count, kSparseRecordBytes,
                       "the sparse translation byte count");
  if (frame.supplemental_a_count > limits.max_sparse_scale_records ||
      frame.supplemental_b_count > limits.max_sparse_translation_records ||
      frame.primary_payload_range.size != frame.primary_byte_count ||
      frame.supplemental_a_payload_range.size != supplemental_a_bytes ||
      frame.supplemental_b_payload_range.size != supplemental_b_bytes) {
    fail(
        "RacRatchetPoseV1 received invalid or caller-limited frame partitions");
  }

  const auto primary_end =
      checked_add(frame.primary_payload_range.offset,
                  frame.primary_payload_range.size, "the primary payload end");
  const auto supplemental_a_end = checked_add(
      frame.supplemental_a_payload_range.offset,
      frame.supplemental_a_payload_range.size, "the sparse scale payload end");
  const auto supplemental_b_end =
      checked_add(frame.supplemental_b_payload_range.offset,
                  frame.supplemental_b_payload_range.size,
                  "the sparse translation payload end");
  const auto padding_end =
      checked_add(frame.trailing_alignment_padding_range.offset,
                  frame.trailing_alignment_padding_range.size,
                  "the regular frame padding end");
  const auto payload_end = checked_add(frame.opaque_payload_range.offset,
                                       frame.opaque_payload_range.size,
                                       "the regular frame payload end");
  const auto declared_b_offset =
      checked_add(frame.primary_byte_count, supplemental_a_bytes,
                  "the declared sparse translation offset");

  if (frame.primary_payload_range.offset != frame.opaque_payload_range.offset ||
      frame.supplemental_a_payload_range.offset != primary_end ||
      frame.supplemental_b_payload_range.offset != supplemental_a_end ||
      frame.supplemental_b_byte_offset != declared_b_offset ||
      frame.trailing_alignment_padding_range.offset != supplemental_b_end ||
      padding_end != payload_end) {
    fail("RacRatchetPoseV1 received non-contiguous frame partitions");
  }

  const auto bytes = std::span<const std::byte>(sequence.encoded_bytes);
  static_cast<void>(require_range(bytes, frame.opaque_payload_range,
                                  "The regular frame payload"));
  static_cast<void>(require_range(bytes, frame.primary_payload_range,
                                  "The primary pose payload"));
  static_cast<void>(require_range(bytes, frame.supplemental_a_payload_range,
                                  "The sparse scale payload"));
  static_cast<void>(require_range(bytes, frame.supplemental_b_payload_range,
                                  "The sparse translation payload"));
  static_cast<void>(require_range(bytes, frame.trailing_alignment_padding_range,
                                  "The regular frame padding"));
}

} // namespace

RacRatchetPoseV1 decode_rac_ratchet_regular_pose_v1(
    const RacRatchetSequenceV1 &sequence, const std::uint64_t frame_index,
    const RacMobyBindRigV1 &bind_rig, const float class_scale,
    const RacRatchetPoseLimitsV1 limits) {
  validate_limits(limits);
  if (!std::isfinite(class_scale) || class_scale <= 0.0F) {
    fail("RacRatchetPoseV1 class scale must be finite and positive");
  }
  if (sequence.frame_count != sequence.frames.size() ||
      frame_index >= sequence.frames.size()) {
    fail("RacRatchetPoseV1 frame index or sequence frame table is invalid");
  }

  const auto joint_count = bind_rig.actor_rig.joints.size();
  if (joint_count == 0U || joint_count > limits.max_joints ||
      bind_rig.source_common_translations.size() != joint_count) {
    fail("RacRatchetPoseV1 received an invalid or caller-limited bind rig");
  }
  for (std::size_t index = 0U; index < joint_count; ++index) {
    const auto &joint = bind_rig.actor_rig.joints[index];
    if ((index == 0U && joint.parent_index != -1) ||
        (index != 0U &&
         (joint.parent_index < 0 ||
          static_cast<std::size_t>(joint.parent_index) >= index))) {
      fail("RacRatchetPoseV1 bind rig is not a parent-first hierarchy");
    }
    validate_transform(joint.inverse_bind_transform,
                       "A RacRatchetPoseV1 inverse-bind transform");
  }

  const auto &frame = sequence.frames[static_cast<std::size_t>(frame_index)];
  validate_frame_layout(sequence, frame, limits);
  const auto required_primary_bytes = checked_multiply(
      joint_count, kJointQuaternionBytes, "the joint quaternion byte count");
  if (frame.primary_payload_range.size != required_primary_bytes) {
    fail("RacRatchetPoseV1 primary payload is not one quaternion per joint");
  }

  const auto sequence_bytes =
      std::span<const std::byte>(sequence.encoded_bytes);
  const auto primary = require_range(
      sequence_bytes, frame.primary_payload_range, "The primary pose payload");
  const auto sparse_scales =
      require_range(sequence_bytes, frame.supplemental_a_payload_range,
                    "The sparse scale payload");
  const auto sparse_translations =
      require_range(sequence_bytes, frame.supplemental_b_payload_range,
                    "The sparse translation payload");

  RacRatchetPoseV1 result;
  result.source_joint_poses.resize(joint_count);
  const auto translation_scale = static_cast<double>(class_scale) / 1024.0;
  for (std::size_t index = 0U; index < joint_count; ++index) {
    auto &joint = result.source_joint_poses[index];
    std::array<double, 4U> quaternion{};
    double squared_length = 0.0;
    for (std::size_t component = 0U; component < quaternion.size();
         ++component) {
      quaternion[component] =
          static_cast<double>(read_le_i16(
              primary, index * kJointQuaternionBytes + component * 2U)) /
          32768.0;
      squared_length += quaternion[component] * quaternion[component];
    }
    const auto length = std::sqrt(squared_length);
    if (!std::isfinite(length) || length < limits.minimum_quaternion_length) {
      fail("RacRatchetPoseV1 encountered a zero or too-small quaternion");
    }
    for (std::size_t component = 0U; component < quaternion.size();
         ++component) {
      joint.normalized_rotation_xyzw[component] = canonical_float(
          quaternion[component] / length, "normalized quaternion component");
    }

    for (std::size_t axis = 0U; axis < joint.translation.size(); ++axis) {
      joint.translation[axis] = canonical_float(
          static_cast<double>(
              bind_rig.source_common_translations[index][axis]) *
              translation_scale,
          "scaled common translation");
    }
  }

  std::vector<bool> scale_seen(joint_count, false);
  for (std::size_t record = 0U; record < frame.supplemental_a_count; ++record) {
    const auto offset = record * static_cast<std::size_t>(kSparseRecordBytes);
    const auto tag = read_le16(sparse_scales, offset + 6U);
    if ((tag & static_cast<std::uint16_t>(~kScaleAllowedTagBits)) != 0U) {
      fail("RacRatchetPoseV1 sparse scale tag has unknown bits");
    }
    const auto joint_index = static_cast<std::size_t>(tag & kSparseJointMask);
    if (joint_index >= joint_count || scale_seen[joint_index]) {
      fail("RacRatchetPoseV1 sparse scale has an invalid or duplicate joint");
    }
    scale_seen[joint_index] = true;

    std::array<float, 3U> scale{};
    for (std::size_t axis = 0U; axis < scale.size(); ++axis) {
      scale[axis] = canonical_float(
          static_cast<double>(read_le16(sparse_scales, offset + axis * 2U)) /
              4096.0,
          "sparse scale component");
    }
    if ((tag & kScaleLocalBit) != 0U) {
      result.source_joint_poses[joint_index].local_scale = scale;
    } else {
      result.source_joint_poses[joint_index].terminal_scale = scale;
    }
  }

  std::vector<bool> translation_seen(joint_count, false);
  for (std::size_t record = 0U; record < frame.supplemental_b_count; ++record) {
    const auto offset = record * static_cast<std::size_t>(kSparseRecordBytes);
    const auto tag = read_le16(sparse_translations, offset + 6U);
    if ((tag & static_cast<std::uint16_t>(~kSparseJointMask)) != 0U) {
      fail("RacRatchetPoseV1 sparse translation tag has unknown bits");
    }
    const auto joint_index = static_cast<std::size_t>(tag & kSparseJointMask);
    if (joint_index >= joint_count || translation_seen[joint_index]) {
      fail("RacRatchetPoseV1 sparse translation has an invalid or duplicate "
           "joint");
    }
    translation_seen[joint_index] = true;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
      result.source_joint_poses[joint_index].translation[axis] =
          canonical_float(static_cast<double>(read_le_i16(sparse_translations,
                                                          offset + axis * 2U)) *
                              translation_scale,
                          "scaled sparse translation");
    }
  }

  std::vector<ActorAffineTransformV1> hierarchical_globals;
  hierarchical_globals.reserve(joint_count);
  result.palette.global_joint_transforms.reserve(joint_count);
  result.palette.skin_transforms.reserve(joint_count);
  for (std::size_t index = 0U; index < joint_count; ++index) {
    auto hierarchical_global =
        make_local_transform(result.source_joint_poses[index]);
    const auto parent_index = bind_rig.actor_rig.joints[index].parent_index;
    if (parent_index >= 0) {
      hierarchical_global =
          compose(hierarchical_globals[static_cast<std::size_t>(parent_index)],
                  hierarchical_global);
    }
    validate_transform(hierarchical_global,
                       "A RacRatchetPoseV1 hierarchical transform");
    hierarchical_globals.push_back(hierarchical_global);

    auto global = apply_terminal_scale(
        hierarchical_global, result.source_joint_poses[index].terminal_scale);
    validate_transform(global, "A RacRatchetPoseV1 global transform");
    auto skin = compose(
        global, bind_rig.actor_rig.joints[index].inverse_bind_transform);
    validate_transform(skin, "A RacRatchetPoseV1 skin transform");
    result.palette.global_joint_transforms.push_back(global);
    result.palette.skin_transforms.push_back(skin);
  }
  return result;
}

} // namespace openrc
