#include "openrc/actor_animation_io.hpp"

#include <algorithm>
#include <array>
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

constexpr std::array<std::byte, 8U> kMagic{
    std::byte{'O'}, std::byte{'R'}, std::byte{'A'}, std::byte{'N'},
    std::byte{'I'}, std::byte{'M'}, std::byte{'B'}, std::byte{'1'},
};

constexpr std::uint64_t kFormatVersionOffset = 0x08U;
constexpr std::uint64_t kHeaderBytesOffset = 0x0cU;
constexpr std::uint64_t kTotalBytesOffset = 0x10U;
constexpr std::uint64_t kPayloadTypeOffset = 0x18U;
constexpr std::uint64_t kSchemaVersionOffset = 0x1cU;
constexpr std::uint64_t kClipCountOffset = 0x20U;
constexpr std::uint64_t kHeaderFlagsOffset = 0x24U;
constexpr std::uint64_t kTotalFrameCountOffset = 0x28U;
constexpr std::uint64_t kTotalJointPoseCountOffset = 0x30U;
constexpr std::uint64_t kTotalKeyBytesOffset = 0x38U;
constexpr std::uint64_t kClipRecordBytesOffset = 0x40U;
constexpr std::uint64_t kFrameRecordBytesOffset = 0x44U;
constexpr std::uint64_t kJointPoseRecordBytesOffset = 0x48U;
constexpr std::uint64_t kHeaderReserved32Offset = 0x4cU;
constexpr std::uint64_t kClipTableOffset = 0x50U;
constexpr std::uint64_t kFrameTableOffset = 0x58U;
constexpr std::uint64_t kJointPoseTableOffset = 0x60U;
constexpr std::uint64_t kKeyDataOffset = 0x68U;
constexpr std::uint64_t kHeaderReservedOffset = 0x70U;

constexpr std::uint64_t kClipIdOffset = 0x00U;
constexpr std::uint64_t kClipWrapModeOffset = 0x04U;
constexpr std::uint64_t kClipSourceRateOffset = 0x08U;
constexpr std::uint64_t kClipSemanticKeyBytesOffset = 0x0cU;
constexpr std::uint64_t kClipRigKeyBytesOffset = 0x10U;
constexpr std::uint64_t kClipFrameCountOffset = 0x14U;
constexpr std::uint64_t kClipJointCountOffset = 0x18U;
constexpr std::uint64_t kClipReserved32Offset = 0x1cU;
constexpr std::uint64_t kClipFirstFrameOffset = 0x20U;
constexpr std::uint64_t kClipSemanticKeyOffset = 0x28U;
constexpr std::uint64_t kClipRigKeyOffset = 0x30U;
constexpr std::uint64_t kClipRigContentDigestOffset = 0x38U;
constexpr std::uint64_t kClipReserved64Offset = 0x58U;

constexpr std::uint64_t kFramePhaseRateOffset = 0x00U;
constexpr std::uint64_t kFrameJointCountOffset = 0x04U;
constexpr std::uint64_t kFrameFirstJointPoseOffset = 0x08U;
constexpr std::uint64_t kFrameReservedOffset = 0x10U;

constexpr std::uint64_t kPoseRotationOffset = 0x00U;
constexpr std::uint64_t kPoseTranslationOffset = 0x10U;
constexpr std::uint64_t kPoseLocalScaleOffset = 0x1cU;
constexpr std::uint64_t kPoseTerminalScaleOffset = 0x28U;
constexpr std::uint64_t kPoseReservedOffset = 0x34U;

static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<float>::radix == 2);
static_assert(std::numeric_limits<float>::digits == 24);

[[noreturn]] void fail(const std::string &message) {
  throw ActorAnimationIoError(message);
}

void validate_limits(const ActorAnimationIoLimitsV1 &limits) {
  if (limits.max_encoded_bytes < kActorAnimationIoHeaderBytesV1 ||
      limits.bank.max_clips == 0U || limits.bank.max_frames_per_clip == 0U ||
      limits.bank.max_total_frames == 0U ||
      limits.bank.max_joints_per_frame == 0U ||
      limits.bank.max_total_joint_poses == 0U ||
      limits.bank.max_semantic_key_bytes == 0U ||
      limits.bank.max_total_semantic_key_bytes == 0U ||
      limits.bank.max_source_updates_per_second == 0U ||
      !std::isfinite(limits.bank.max_absolute_component) ||
      !(limits.bank.max_absolute_component > 0.0F) ||
      !std::isfinite(limits.bank.minimum_quaternion_length) ||
      !(limits.bank.minimum_quaternion_length > 0.0)) {
    fail("ActorAnimationBankV1 I/O limits must all be positive and finite");
  }
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(const std::uint64_t left,
                                             const std::uint64_t right,
                                             const char *const description) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left * right;
}

[[nodiscard]] std::size_t host_size(const std::uint64_t value,
                                    const std::size_t maximum,
                                    const char *const description) {
  if (value > maximum || value > static_cast<std::uint64_t>(
                                     std::numeric_limits<std::size_t>::max())) {
    fail(std::string(description) + " exceeds the host container domain");
  }
  return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint32_t format_count(const std::uint64_t value,
                                         const char *const description) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    fail(std::string(description) + " exceeds the format count width");
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::byte> bytes,
                                     const std::uint64_t offset) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  return static_cast<std::uint32_t>(byte_value(bytes[begin])) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(const std::span<const std::byte> bytes,
                                     const std::uint64_t offset) noexcept {
  std::uint64_t result = 0U;
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < sizeof(result); ++index) {
    result |= static_cast<std::uint64_t>(byte_value(bytes[begin + index]))
              << (index * 8U);
  }
  return result;
}

[[nodiscard]] float read_f32(const std::span<const std::byte> bytes,
                             const std::uint64_t offset,
                             const char *const description) {
  const auto value = std::bit_cast<float>(read_u32(bytes, offset));
  if (!std::isfinite(value)) {
    fail(std::string("ActorAnimationBankV1 has a non-finite encoded ") +
         description);
  }
  if (value == 0.0F && std::signbit(value)) {
    fail(std::string("ActorAnimationBankV1 has non-canonical signed zero in ") +
         description);
  }
  return value;
}

void write_u32(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const std::uint32_t value) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT32_C(0xff));
  }
}

void write_u64(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const std::uint64_t value) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
  }
}

void write_f32(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const float value) noexcept {
  write_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] bool all_zero(const std::span<const std::byte> bytes,
                            const std::uint64_t begin,
                            const std::uint64_t end) noexcept {
  for (auto offset = begin; offset < end; ++offset) {
    if (bytes[static_cast<std::size_t>(offset)] != std::byte{0U}) {
      return false;
    }
  }
  return true;
}

void write_string(std::vector<std::byte> &bytes, const std::uint64_t offset,
                  const std::string &value) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < value.size(); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>(static_cast<unsigned char>(value[index]));
  }
}

[[nodiscard]] std::string read_string(const std::span<const std::byte> bytes,
                                      const std::uint64_t offset,
                                      const std::uint32_t length,
                                      const std::uint32_t maximum_length,
                                      const char *const description) {
  if (length == 0U || length > maximum_length) {
    fail(std::string(description) + " has an invalid byte length");
  }
  std::string result;
  result.reserve(host_size(length, result.max_size(), description));
  const auto begin = static_cast<std::size_t>(offset);
  for (std::uint32_t index = 0U; index < length; ++index) {
    result.push_back(static_cast<char>(byte_value(bytes[begin + index])));
  }
  return result;
}

struct Counts {
  std::uint32_t clips = 0U;
  std::uint64_t frames = 0U;
  std::uint64_t joint_poses = 0U;
  std::uint64_t key_bytes = 0U;
};

[[nodiscard]] Counts bank_counts(const ActorAnimationBankV1 &bank) {
  Counts result;
  result.clips = format_count(bank.clips.size(), "Animation clip count");
  for (const auto &clip : bank.clips) {
    result.frames = checked_add(result.frames, clip.frames.size(),
                                "ActorAnimationBankV1 frame count");
    result.key_bytes = checked_add(result.key_bytes, clip.semantic_key.size(),
                                   "ActorAnimationBankV1 key bytes");
    result.key_bytes = checked_add(result.key_bytes, clip.rig_key.size(),
                                   "ActorAnimationBankV1 key bytes");
    for (const auto &frame : clip.frames) {
      result.joint_poses =
          checked_add(result.joint_poses, frame.joint_poses.size(),
                      "ActorAnimationBankV1 joint-pose count");
    }
  }
  return result;
}

struct Layout {
  std::uint64_t clips = 0U;
  std::uint64_t frames = 0U;
  std::uint64_t joint_poses = 0U;
  std::uint64_t keys = 0U;
  std::uint64_t total = 0U;
};

[[nodiscard]] Layout canonical_layout(const Counts &counts) {
  Layout result;
  result.clips = kActorAnimationIoHeaderBytesV1;
  result.frames = checked_add(
      result.clips,
      checked_multiply(counts.clips, kActorAnimationIoClipRecordBytesV1,
                       "ActorAnimationBankV1 clip table"),
      "ActorAnimationBankV1 clip table");
  result.joint_poses = checked_add(
      result.frames,
      checked_multiply(counts.frames, kActorAnimationIoFrameRecordBytesV1,
                       "ActorAnimationBankV1 frame table"),
      "ActorAnimationBankV1 frame table");
  result.keys =
      checked_add(result.joint_poses,
                  checked_multiply(counts.joint_poses,
                                   kActorAnimationIoJointPoseRecordBytesV1,
                                   "ActorAnimationBankV1 joint-pose table"),
                  "ActorAnimationBankV1 joint-pose table");
  result.total = checked_add(result.keys, counts.key_bytes,
                             "ActorAnimationBankV1 key data");
  return result;
}

[[nodiscard]] ActorAnimationBankV1
canonical_bank(const ActorAnimationBankV1 &bank,
               const ActorAnimationLimitsV1 limits) {
  try {
    return canonicalize_actor_animation_bank_v1(bank, limits);
  } catch (const ActorAnimationError &error) {
    fail("Cannot encode ActorAnimationBankV1: " + std::string(error.what()));
  }
}

void require_key_partition(const std::uint64_t declared_offset,
                           const std::uint32_t length,
                           std::uint64_t &expected_offset,
                           const std::uint64_t end,
                           const char *const description) {
  if (expected_offset > end || declared_offset != expected_offset ||
      length > end - expected_offset) {
    fail(std::string(description) +
         " is not part of the exact canonical key partition");
  }
  expected_offset += length;
}

} // namespace

std::vector<std::byte>
encode_actor_animation_bank_v1(const ActorAnimationBankV1 &bank,
                               const ActorAnimationIoLimitsV1 limits) {
  validate_limits(limits);
  const auto canonical = canonical_bank(bank, limits.bank);
  const auto counts = bank_counts(canonical);
  const auto layout = canonical_layout(counts);
  if (layout.total > limits.max_encoded_bytes) {
    fail("ActorAnimationBankV1 encoded bytes exceed the caller limit");
  }

  std::vector<std::byte> result(host_size(layout.total,
                                          std::vector<std::byte>{}.max_size(),
                                          "ActorAnimationBankV1 encoded size"),
                                std::byte{0U});
  std::copy(kMagic.begin(), kMagic.end(), result.begin());
  write_u32(result, kFormatVersionOffset, kActorAnimationIoFormatVersionV1);
  write_u32(result, kHeaderBytesOffset, kActorAnimationIoHeaderBytesV1);
  write_u64(result, kTotalBytesOffset, layout.total);
  write_u32(result, kPayloadTypeOffset, kActorAnimationIoPayloadTypeV1);
  write_u32(result, kSchemaVersionOffset, canonical.schema_version);
  write_u32(result, kClipCountOffset, counts.clips);
  write_u64(result, kTotalFrameCountOffset, counts.frames);
  write_u64(result, kTotalJointPoseCountOffset, counts.joint_poses);
  write_u64(result, kTotalKeyBytesOffset, counts.key_bytes);
  write_u32(result, kClipRecordBytesOffset, kActorAnimationIoClipRecordBytesV1);
  write_u32(result, kFrameRecordBytesOffset,
            kActorAnimationIoFrameRecordBytesV1);
  write_u32(result, kJointPoseRecordBytesOffset,
            kActorAnimationIoJointPoseRecordBytesV1);
  write_u64(result, kClipTableOffset, layout.clips);
  write_u64(result, kFrameTableOffset, layout.frames);
  write_u64(result, kJointPoseTableOffset, layout.joint_poses);
  write_u64(result, kKeyDataOffset, layout.keys);

  std::uint64_t first_frame = 0U;
  std::uint64_t first_joint_pose = 0U;
  auto key_offset = layout.keys;
  for (std::size_t clip_index = 0U; clip_index < canonical.clips.size();
       ++clip_index) {
    const auto clip_offset =
        layout.clips + static_cast<std::uint64_t>(clip_index) *
                           kActorAnimationIoClipRecordBytesV1;
    const auto &clip = canonical.clips[clip_index];
    const auto frame_count =
        format_count(clip.frames.size(), "Per-clip animation frame count");
    const auto joint_count = format_count(
        clip.frames.front().joint_poses.size(), "Animation rig joint count");
    write_u32(result, clip_offset + kClipIdOffset, clip.id);
    write_u32(result, clip_offset + kClipWrapModeOffset,
              static_cast<std::uint32_t>(clip.wrap_mode));
    write_u32(result, clip_offset + kClipSourceRateOffset,
              clip.source_updates_per_second);
    write_u32(result, clip_offset + kClipSemanticKeyBytesOffset,
              format_count(clip.semantic_key.size(),
                           "Animation semantic-key length"));
    write_u32(result, clip_offset + kClipRigKeyBytesOffset,
              format_count(clip.rig_key.size(), "Animation rig-key length"));
    write_u32(result, clip_offset + kClipFrameCountOffset, frame_count);
    write_u32(result, clip_offset + kClipJointCountOffset, joint_count);
    write_u64(result, clip_offset + kClipFirstFrameOffset, first_frame);
    write_u64(result, clip_offset + kClipSemanticKeyOffset, key_offset);
    write_string(result, key_offset, clip.semantic_key);
    key_offset = checked_add(key_offset, clip.semantic_key.size(),
                             "ActorAnimationBankV1 key partition");
    write_u64(result, clip_offset + kClipRigKeyOffset, key_offset);
    write_string(result, key_offset, clip.rig_key);
    key_offset = checked_add(key_offset, clip.rig_key.size(),
                             "ActorAnimationBankV1 key partition");
    for (std::size_t digest_byte = 0U;
         digest_byte < clip.rig_content_sha256.size(); ++digest_byte) {
      result[static_cast<std::size_t>(
          clip_offset + kClipRigContentDigestOffset + digest_byte)] =
          clip.rig_content_sha256[digest_byte];
    }

    for (std::size_t local_frame = 0U; local_frame < clip.frames.size();
         ++local_frame) {
      const auto frame_index =
          first_frame + static_cast<std::uint64_t>(local_frame);
      const auto frame_offset =
          layout.frames + frame_index * kActorAnimationIoFrameRecordBytesV1;
      const auto &frame = clip.frames[local_frame];
      write_f32(result, frame_offset + kFramePhaseRateOffset, frame.phase_rate);
      write_u32(result, frame_offset + kFrameJointCountOffset, joint_count);
      write_u64(result, frame_offset + kFrameFirstJointPoseOffset,
                first_joint_pose);

      for (std::size_t local_pose = 0U; local_pose < frame.joint_poses.size();
           ++local_pose) {
        const auto pose_index =
            first_joint_pose + static_cast<std::uint64_t>(local_pose);
        const auto pose_offset =
            layout.joint_poses +
            pose_index * kActorAnimationIoJointPoseRecordBytesV1;
        const auto &pose = frame.joint_poses[local_pose];
        for (std::size_t component = 0U;
             component < pose.normalized_rotation_xyzw.size(); ++component) {
          write_f32(result, pose_offset + kPoseRotationOffset + component * 4U,
                    pose.normalized_rotation_xyzw[component]);
        }
        for (std::size_t component = 0U; component < pose.translation.size();
             ++component) {
          write_f32(result,
                    pose_offset + kPoseTranslationOffset + component * 4U,
                    pose.translation[component]);
        }
        for (std::size_t component = 0U; component < pose.local_scale.size();
             ++component) {
          write_f32(result,
                    pose_offset + kPoseLocalScaleOffset + component * 4U,
                    pose.local_scale[component]);
        }
        for (std::size_t component = 0U; component < pose.terminal_scale.size();
             ++component) {
          write_f32(result,
                    pose_offset + kPoseTerminalScaleOffset + component * 4U,
                    pose.terminal_scale[component]);
        }
      }
      first_joint_pose =
          checked_add(first_joint_pose, joint_count,
                      "ActorAnimationBankV1 joint-pose partition");
    }
    first_frame = checked_add(first_frame, frame_count,
                              "ActorAnimationBankV1 frame partition");
  }
  if (first_frame != counts.frames || first_joint_pose != counts.joint_poses ||
      key_offset != layout.total) {
    fail("ActorAnimationBankV1 internal table partition is inconsistent");
  }
  return result;
}

ActorAnimationBankV1
decode_actor_animation_bank_v1(const std::span<const std::byte> bytes,
                               const ActorAnimationIoLimitsV1 limits) {
  validate_limits(limits);
  if (bytes.size() > limits.max_encoded_bytes) {
    fail("ActorAnimationBankV1 encoded bytes exceed the caller limit");
  }
  if (bytes.size() < kActorAnimationIoHeaderBytesV1) {
    fail("ActorAnimationBankV1 header is truncated");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    fail("ActorAnimationBankV1 magic is invalid");
  }
  if (read_u32(bytes, kFormatVersionOffset) !=
      kActorAnimationIoFormatVersionV1) {
    fail("ActorAnimationBankV1 format version is unknown");
  }
  if (read_u32(bytes, kPayloadTypeOffset) != kActorAnimationIoPayloadTypeV1) {
    fail("ActorAnimationBankV1 payload type is unknown");
  }
  if (read_u32(bytes, kHeaderBytesOffset) != kActorAnimationIoHeaderBytesV1 ||
      read_u32(bytes, kSchemaVersionOffset) !=
          kActorAnimationBankSchemaVersionV1 ||
      read_u32(bytes, kClipRecordBytesOffset) !=
          kActorAnimationIoClipRecordBytesV1 ||
      read_u32(bytes, kFrameRecordBytesOffset) !=
          kActorAnimationIoFrameRecordBytesV1 ||
      read_u32(bytes, kJointPoseRecordBytesOffset) !=
          kActorAnimationIoJointPoseRecordBytesV1 ||
      read_u32(bytes, kHeaderFlagsOffset) != 0U ||
      read_u32(bytes, kHeaderReserved32Offset) != 0U ||
      !all_zero(bytes, kHeaderReservedOffset, kActorAnimationIoHeaderBytesV1)) {
    fail("ActorAnimationBankV1 header schema, record sizes, flags, or "
         "reserved data are invalid");
  }

  Counts counts;
  counts.clips = read_u32(bytes, kClipCountOffset);
  counts.frames = read_u64(bytes, kTotalFrameCountOffset);
  counts.joint_poses = read_u64(bytes, kTotalJointPoseCountOffset);
  counts.key_bytes = read_u64(bytes, kTotalKeyBytesOffset);
  if (counts.clips > limits.bank.max_clips ||
      counts.frames > limits.bank.max_total_frames ||
      counts.joint_poses > limits.bank.max_total_joint_poses ||
      counts.key_bytes > limits.bank.max_total_semantic_key_bytes) {
    fail("ActorAnimationBankV1 header exceeds a caller limit");
  }

  const auto layout = canonical_layout(counts);
  if (read_u64(bytes, kClipTableOffset) != layout.clips ||
      read_u64(bytes, kFrameTableOffset) != layout.frames ||
      read_u64(bytes, kJointPoseTableOffset) != layout.joint_poses ||
      read_u64(bytes, kKeyDataOffset) != layout.keys ||
      read_u64(bytes, kTotalBytesOffset) != layout.total ||
      layout.total != bytes.size()) {
    fail("ActorAnimationBankV1 table layout or exact byte size is invalid");
  }

  ActorAnimationBankV1 result;
  result.schema_version = read_u32(bytes, kSchemaVersionOffset);
  result.clips.reserve(
      host_size(counts.clips, result.clips.max_size(), "Animation clip count"));

  std::uint64_t expected_first_frame = 0U;
  std::uint64_t expected_first_joint_pose = 0U;
  auto expected_key_offset = layout.keys;
  for (std::uint32_t clip_index = 0U; clip_index < counts.clips; ++clip_index) {
    const auto clip_offset =
        layout.clips + static_cast<std::uint64_t>(clip_index) *
                           kActorAnimationIoClipRecordBytesV1;
    if (read_u32(bytes, clip_offset + kClipReserved32Offset) != 0U ||
        read_u64(bytes, clip_offset + kClipReserved64Offset) != 0U) {
      fail("ActorAnimationBankV1 clip reserved data is non-zero");
    }
    const auto frame_count =
        read_u32(bytes, clip_offset + kClipFrameCountOffset);
    const auto joint_count =
        read_u32(bytes, clip_offset + kClipJointCountOffset);
    const auto first_frame =
        read_u64(bytes, clip_offset + kClipFirstFrameOffset);
    if (frame_count == 0U || frame_count > limits.bank.max_frames_per_clip ||
        joint_count == 0U || joint_count > limits.bank.max_joints_per_frame ||
        first_frame != expected_first_frame ||
        expected_first_frame > counts.frames ||
        frame_count > counts.frames - expected_first_frame) {
      fail("ActorAnimationBankV1 clip frame range or joint count is invalid");
    }

    const auto semantic_key_length =
        read_u32(bytes, clip_offset + kClipSemanticKeyBytesOffset);
    const auto rig_key_length =
        read_u32(bytes, clip_offset + kClipRigKeyBytesOffset);
    const auto semantic_key_offset =
        read_u64(bytes, clip_offset + kClipSemanticKeyOffset);
    const auto rig_key_offset =
        read_u64(bytes, clip_offset + kClipRigKeyOffset);
    require_key_partition(semantic_key_offset, semantic_key_length,
                          expected_key_offset, layout.total,
                          "An animation semantic key");
    require_key_partition(rig_key_offset, rig_key_length, expected_key_offset,
                          layout.total, "An animation rig key");

    ActorAnimationClipV1 clip;
    clip.id = read_u32(bytes, clip_offset + kClipIdOffset);
    clip.wrap_mode = static_cast<ActorAnimationWrapModeV1>(
        read_u32(bytes, clip_offset + kClipWrapModeOffset));
    clip.source_updates_per_second =
        read_u32(bytes, clip_offset + kClipSourceRateOffset);
    for (std::size_t digest_byte = 0U;
         digest_byte < clip.rig_content_sha256.size(); ++digest_byte) {
      clip.rig_content_sha256[digest_byte] = bytes[static_cast<std::size_t>(
          clip_offset + kClipRigContentDigestOffset + digest_byte)];
    }
    clip.semantic_key = read_string(
        bytes, semantic_key_offset, semantic_key_length,
        limits.bank.max_semantic_key_bytes, "An animation semantic key");
    clip.rig_key =
        read_string(bytes, rig_key_offset, rig_key_length,
                    limits.bank.max_semantic_key_bytes, "An animation rig key");
    clip.frames.reserve(host_size(frame_count, clip.frames.max_size(),
                                  "Per-clip animation frame count"));

    for (std::uint32_t local_frame = 0U; local_frame < frame_count;
         ++local_frame) {
      const auto frame_index = expected_first_frame + local_frame;
      const auto frame_offset =
          layout.frames + frame_index * kActorAnimationIoFrameRecordBytesV1;
      if (!all_zero(bytes, frame_offset + kFrameReservedOffset,
                    frame_offset + kActorAnimationIoFrameRecordBytesV1)) {
        fail("ActorAnimationBankV1 frame reserved data is non-zero");
      }
      const auto frame_joint_count =
          read_u32(bytes, frame_offset + kFrameJointCountOffset);
      const auto first_joint_pose =
          read_u64(bytes, frame_offset + kFrameFirstJointPoseOffset);
      if (frame_joint_count != joint_count ||
          first_joint_pose != expected_first_joint_pose ||
          expected_first_joint_pose > counts.joint_poses ||
          frame_joint_count > counts.joint_poses - expected_first_joint_pose) {
        fail("ActorAnimationBankV1 frame joint-pose range is not canonical");
      }

      ActorAnimationFrameV1 frame;
      frame.phase_rate = read_f32(bytes, frame_offset + kFramePhaseRateOffset,
                                  "frame phase rate");
      frame.joint_poses.reserve(
          host_size(frame_joint_count, frame.joint_poses.max_size(),
                    "Per-frame animation joint-pose count"));
      for (std::uint32_t local_pose = 0U; local_pose < frame_joint_count;
           ++local_pose) {
        const auto pose_index = expected_first_joint_pose + local_pose;
        const auto pose_offset =
            layout.joint_poses +
            pose_index * kActorAnimationIoJointPoseRecordBytesV1;
        if (!all_zero(bytes, pose_offset + kPoseReservedOffset,
                      pose_offset + kActorAnimationIoJointPoseRecordBytesV1)) {
          fail("ActorAnimationBankV1 joint-pose reserved data is non-zero");
        }

        ActorJointPoseV1 pose;
        for (std::size_t component = 0U;
             component < pose.normalized_rotation_xyzw.size(); ++component) {
          pose.normalized_rotation_xyzw[component] = read_f32(
              bytes, pose_offset + kPoseRotationOffset + component * 4U,
              "joint quaternion component");
        }
        for (std::size_t component = 0U; component < pose.translation.size();
             ++component) {
          pose.translation[component] = read_f32(
              bytes, pose_offset + kPoseTranslationOffset + component * 4U,
              "joint translation component");
        }
        for (std::size_t component = 0U; component < pose.local_scale.size();
             ++component) {
          pose.local_scale[component] = read_f32(
              bytes, pose_offset + kPoseLocalScaleOffset + component * 4U,
              "joint local-scale component");
        }
        for (std::size_t component = 0U; component < pose.terminal_scale.size();
             ++component) {
          pose.terminal_scale[component] = read_f32(
              bytes, pose_offset + kPoseTerminalScaleOffset + component * 4U,
              "joint terminal-scale component");
        }
        frame.joint_poses.push_back(std::move(pose));
      }
      expected_first_joint_pose =
          checked_add(expected_first_joint_pose, frame_joint_count,
                      "ActorAnimationBankV1 joint-pose partition");
      clip.frames.push_back(std::move(frame));
    }
    expected_first_frame = checked_add(expected_first_frame, frame_count,
                                       "ActorAnimationBankV1 frame partition");
    result.clips.push_back(std::move(clip));
  }
  if (expected_first_frame != counts.frames ||
      expected_first_joint_pose != counts.joint_poses ||
      expected_key_offset != layout.total) {
    fail("ActorAnimationBankV1 records do not consume their declared table "
         "partitions");
  }

  try {
    validate_actor_animation_bank_v1(result, limits.bank);
  } catch (const ActorAnimationError &error) {
    fail("Decoded ActorAnimationBankV1 is invalid: " +
         std::string(error.what()));
  }

  return result;
}

} // namespace openrc
