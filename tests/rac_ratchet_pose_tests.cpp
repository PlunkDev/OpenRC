#include "openrc/rac_ratchet_pose.hpp"

#include "openrc/actor_animation_io.hpp"
#include "openrc/actor_animation_player.hpp"
#include "openrc/disc_toc.hpp"
#include "openrc/rac_level_core.hpp"
#include "openrc/rac_moby_class.hpp"
#include "openrc/rac_ratchet_animation_compile.hpp"
#include "openrc/wad.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::RacRatchetPoseLimitsV1 kPoseLimits{256U, 1024U * 1024U, 256U,
                                                     256U, 1.0e-8};
constexpr openrc::ActorPoseLimitsV1 kActorPoseLimits{256U, 1024U, 1.0e-8,
                                                     1.0e-8};
constexpr std::uint64_t kRealDataMaximumBytes = UINT64_C(64) * 1024U * 1024U;
constexpr openrc::RacLevelCoreLimitsV1 kRealCoreLimits{kRealDataMaximumBytes,
                                                       kRealDataMaximumBytes,
                                                       kRealDataMaximumBytes,
                                                       4096U,
                                                       255U,
                                                       4096U,
                                                       4096U,
                                                       4096U,
                                                       255U,
                                                       255U};
constexpr openrc::RacRatchetSequenceLimitsV1 kRealSequenceLimits{
    kRealDataMaximumBytes, kRealDataMaximumBytes, 255U, 255U};
constexpr openrc::RacRatchetPoseLimitsV1 kRealPoseLimits{
    255U, kRealDataMaximumBytes, 65535U, 65535U, 1.0e-8};
constexpr openrc::RacMobyBindRigLimitsV1 kRealRigLimits{kRealDataMaximumBytes,
                                                        255U, 1.0e-8};
constexpr openrc::RacMobyBindPoseLimitsV1 kRealBindPoseLimits{
    kRealRigLimits,
    {{kRealDataMaximumBytes, 4096U, 4096U, 4096U, 1'000'000U, 4096U,
      1'000'000U},
     4096U,
     1'000'000U,
     1'000'000U},
    1'000'000U};
constexpr openrc::ActorPoseLimitsV1 kRealSkinningLimits{255U, 1'000'000U,
                                                        1.0e-8, 1.0e-8};
constexpr openrc::ActorAnimationIoLimitsV1 kRealAnimationIoLimits{
    UINT64_C(16) * 1024U * 1024U,
    {3U, 255U, 3U * 255U, 255U, 3U * 255U * 255U, 128U, 512U, 50U,
     1'000'000.0F, 1.0e-8}};
constexpr openrc::RacRatchetAnimationCompileLimitsV1
    kRealAnimationCompileLimits{kRealSequenceLimits, kRealPoseLimits,
                                kRealAnimationIoLimits.bank};
constexpr openrc::ActorAnimationPlaybackLimitsV1 kRealPlaybackLimits{
    8U, 8U, 255U, 1.0e-8, 1'000'000.0F};

struct RawScaleRecord {
  std::array<std::uint16_t, 3U> xyz{};
  std::uint16_t tag = 0U;
};

struct RawTranslationRecord {
  std::array<std::int16_t, 3U> xyz{};
  std::uint16_t tag = 0U;
};

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

void expect_near(const float actual, const float expected,
                 const std::string &message, const float tolerance = 1.0e-5F) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
    fail(message + ": got " + std::to_string(actual) + ", expected " +
         std::to_string(expected));
  }
}

template <typename Function>
void expect_pose_error(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const openrc::RacRatchetPoseError &) {
    return;
  }
  fail(message);
}

void write_le16(std::vector<std::byte> &bytes, const std::size_t offset,
                const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & UINT16_C(0x00ff));
  bytes[offset + 1U] = static_cast<std::byte>(value >> 8U);
}

void write_le_i16(std::vector<std::byte> &bytes, const std::size_t offset,
                  const std::int16_t value) {
  write_le16(bytes, offset, std::bit_cast<std::uint16_t>(value));
}

void write_le32(std::vector<std::byte> &bytes, const std::size_t offset,
                const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT32_C(0xff));
  }
}

[[nodiscard]] std::size_t align_16(const std::size_t value) {
  return (value + 0x0fU) & ~std::size_t{0x0fU};
}

[[nodiscard]] openrc::RacRatchetSequenceV1
make_sequence(const std::span<const std::array<std::int16_t, 4U>> quaternions,
              const std::span<const RawScaleRecord> scales = {},
              const std::span<const RawTranslationRecord> translations = {}) {
  constexpr std::size_t kFrameOffset = 0x20U;
  constexpr std::size_t kPayloadOffset = kFrameOffset + 0x10U;
  const auto primary_bytes = quaternions.size() * 8U;
  const auto scale_bytes = scales.size() * 8U;
  const auto translation_bytes = translations.size() * 8U;
  const auto used_payload_bytes =
      primary_bytes + scale_bytes + translation_bytes;
  const auto payload_bytes = align_16(used_payload_bytes);
  if (primary_bytes > std::numeric_limits<std::uint16_t>::max() ||
      scales.size() > std::numeric_limits<std::uint16_t>::max() ||
      translations.size() > std::numeric_limits<std::uint16_t>::max() ||
      payload_bytes / 0x10U > std::numeric_limits<std::uint16_t>::max()) {
    fail("synthetic pose sequence exceeds its field widths");
  }

  std::vector<std::byte> bytes(kPayloadOffset + payload_bytes, std::byte{0});
  bytes[0x10U] = std::byte{1};
  write_le32(bytes, 0x1cU, kFrameOffset);
  write_le16(bytes, kFrameOffset + 0x06U,
             static_cast<std::uint16_t>(payload_bytes / 0x10U));
  write_le16(bytes, kFrameOffset + 0x08U,
             static_cast<std::uint16_t>(primary_bytes));
  write_le16(bytes, kFrameOffset + 0x0aU,
             static_cast<std::uint16_t>(scales.size()));
  write_le16(bytes, kFrameOffset + 0x0cU,
             static_cast<std::uint16_t>(primary_bytes + scale_bytes));
  write_le16(bytes, kFrameOffset + 0x0eU,
             static_cast<std::uint16_t>(translations.size()));

  auto cursor = kPayloadOffset;
  for (const auto &quaternion : quaternions) {
    for (std::size_t component = 0U; component < quaternion.size();
         ++component) {
      write_le_i16(bytes, cursor + component * 2U, quaternion[component]);
    }
    cursor += 8U;
  }
  for (const auto &scale : scales) {
    for (std::size_t axis = 0U; axis < scale.xyz.size(); ++axis) {
      write_le16(bytes, cursor + axis * 2U, scale.xyz[axis]);
    }
    write_le16(bytes, cursor + 6U, scale.tag);
    cursor += 8U;
  }
  for (const auto &translation : translations) {
    for (std::size_t axis = 0U; axis < translation.xyz.size(); ++axis) {
      write_le_i16(bytes, cursor + axis * 2U, translation.xyz[axis]);
    }
    write_le16(bytes, cursor + 6U, translation.tag);
    cursor += 8U;
  }
  while (cursor < bytes.size()) {
    bytes[cursor++] = std::byte{0x5a};
  }

  return openrc::parse_rac_ratchet_sequence_v1(
      bytes, {0U, bytes.size()}, {bytes.size(), bytes.size(), 8U, 8U});
}

[[nodiscard]] openrc::RacMobyBindRigV1 make_bind_rig(
    const std::span<const std::int32_t> parents,
    const std::span<const std::array<float, 3U>> common_translations) {
  if (parents.size() != common_translations.size()) {
    fail("synthetic rig inputs differ in size");
  }
  openrc::RacMobyBindRigV1 result;
  result.actor_rig.joints.resize(parents.size());
  for (std::size_t index = 0U; index < parents.size(); ++index) {
    result.actor_rig.joints[index].parent_index = parents[index];
  }
  result.source_common_translations.assign(common_translations.begin(),
                                           common_translations.end());
  return result;
}

[[nodiscard]] openrc::RacMobyBindRigV1 two_joint_rig(
    const std::array<float, 3U> root_translation = {},
    const std::array<float, 3U> child_translation = {1.0F, 0.0F, 0.0F}) {
  constexpr std::array<std::int32_t, 2U> kParents{-1, 0};
  const std::array<std::array<float, 3U>, 2U> translations{root_translation,
                                                           child_translation};
  return make_bind_rig(kParents, translations);
}

[[nodiscard]] std::array<std::array<std::int16_t, 4U>, 2U>
identity_quaternions() {
  return {{{0, 0, 0, 32767}, {0, 0, 0, 32767}}};
}

void test_quaternion_order_normalization_and_translation_scale() {
  const std::array<std::array<std::int16_t, 4U>, 2U> quaternions{
      {{23170, 0, 0, 23170}, {0, 0, 0, 16384}}};
  const std::array<RawTranslationRecord, 1U> translations{
      RawTranslationRecord{{40, -20, 10}, 1U}};
  const auto sequence = make_sequence(quaternions, {}, translations);
  const auto bind_rig =
      two_joint_rig({20.0F, -10.0F, 4.0F}, {100.0F, 100.0F, 100.0F});
  const auto pose = openrc::decode_rac_ratchet_regular_pose_v1(
      sequence, 0U, bind_rig, 512.0F, kPoseLimits);

  expect(pose.source_joint_poses.size() == 2U &&
             pose.palette.global_joint_transforms.size() == 2U &&
             pose.palette.skin_transforms.size() == 2U,
         "regular pose has the wrong joint count");
  expect_near(pose.source_joint_poses[0U].normalized_rotation_xyzw[0U],
              0.70710677F, "quaternion X component order is wrong");
  expect_near(pose.source_joint_poses[0U].normalized_rotation_xyzw[3U],
              0.70710677F, "quaternion W component order is wrong");
  expect_near(pose.source_joint_poses[1U].normalized_rotation_xyzw[3U], 1.0F,
              "non-unit source quaternion was not normalized");
  expect_near(pose.source_joint_poses[0U].translation[0U], 10.0F,
              "common translation did not use class_scale / 1024");
  expect_near(pose.source_joint_poses[0U].translation[1U], -5.0F,
              "common translation Y scale is wrong");
  expect_near(pose.source_joint_poses[1U].translation[0U], 20.0F,
              "sparse translation is not an absolute scaled replacement");
  expect_near(pose.source_joint_poses[1U].translation[1U], -10.0F,
              "sparse translation Y replacement is wrong");

  const auto &root = pose.palette.global_joint_transforms[0U];
  expect_near(root.values[0U], 1.0F, "XYZW quaternion did not rotate about X");
  expect_near(root.values[5U], 0.0F,
              "XYZW quaternion rotation diagonal is wrong");
  expect_near(root.values[6U], -1.0F, "XYZW quaternion rotation sign is wrong");
  expect_near(root.values[9U], 1.0F,
              "XYZW quaternion rotation component order is wrong");
}

void test_local_scale_propagates_to_child_and_skinning() {
  const auto quaternions = identity_quaternions();
  const std::array<RawScaleRecord, 1U> scales{
      RawScaleRecord{{8192U, 4096U, 4096U}, UINT16_C(0x8000)}};
  const auto pose = openrc::decode_rac_ratchet_regular_pose_v1(
      make_sequence(quaternions, scales), 0U, two_joint_rig(), 1024.0F,
      kPoseLimits);

  expect_near(pose.source_joint_poses[0U].local_scale[0U], 2.0F,
              "local sparse scale was decoded incorrectly");
  expect_near(pose.palette.global_joint_transforms[0U].values[0U], 2.0F,
              "local scale did not affect its joint");
  expect_near(pose.palette.global_joint_transforms[1U].values[0U], 2.0F,
              "local scale did not propagate to the child linear transform");
  expect_near(pose.palette.global_joint_transforms[1U].values[3U], 2.0F,
              "local scale did not propagate to the child translation");

  openrc::ActorSkinnedMeshV1 mesh;
  mesh.vertices.resize(1U);
  mesh.vertices[0U].x = 1.0F;
  mesh.vertices[0U].skin.influence_count = 1U;
  mesh.vertices[0U].skin.joint_indices[0U] = 1U;
  mesh.vertices[0U].skin.weight_numerators[0U] = 255U;
  mesh.vertices[0U].skin.weight_sum = 255U;
  const auto vertices = openrc::pose_actor_mesh_vertices_v1(
      mesh, pose.palette, openrc::ActorAffineTransformV1{}, kActorPoseLimits);
  expect(vertices.size() == 1U, "existing skinning lost a posed vertex");
  expect_near(vertices[0U].x, 4.0F,
              "existing skinning did not consume the Ratchet palette");
}

void test_terminal_scale_does_not_propagate() {
  const auto quaternions = identity_quaternions();
  const std::array<RawScaleRecord, 1U> scales{
      RawScaleRecord{{8192U, 4096U, 4096U}, 0U}};
  const auto pose = openrc::decode_rac_ratchet_regular_pose_v1(
      make_sequence(quaternions, scales), 0U, two_joint_rig(), 1024.0F,
      kPoseLimits);

  expect_near(pose.source_joint_poses[0U].terminal_scale[0U], 2.0F,
              "terminal sparse scale was decoded incorrectly");
  expect_near(pose.palette.global_joint_transforms[0U].values[0U], 2.0F,
              "terminal scale did not affect its own global transform");
  expect_near(pose.palette.global_joint_transforms[1U].values[0U], 1.0F,
              "terminal scale leaked into the child linear transform");
  expect_near(pose.palette.global_joint_transforms[1U].values[3U], 1.0F,
              "terminal scale leaked into the child translation");
}

void test_zero_scale_is_preserved_in_direct_palette() {
  const auto quaternions = identity_quaternions();
  const std::array<RawScaleRecord, 1U> scales{
      RawScaleRecord{{0U, 4096U, 4096U}, 0U}};
  const auto pose = openrc::decode_rac_ratchet_regular_pose_v1(
      make_sequence(quaternions, scales), 0U, two_joint_rig(), 1024.0F,
      kPoseLimits);
  expect(pose.source_joint_poses[0U].terminal_scale[0U] == 0.0F,
         "authored zero terminal scale was discarded");
  expect(pose.palette.global_joint_transforms[0U].values[0U] == 0.0F,
         "direct palette did not preserve a singular terminal scale");
  expect_near(pose.palette.global_joint_transforms[1U].values[0U], 1.0F,
              "singular terminal scale propagated to a child");
}

void test_rejects_malformed_primary_and_quaternions() {
  const std::array<std::array<std::int16_t, 4U>, 1U> one_quaternion{
      {{0, 0, 0, 32767}}};
  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            make_sequence(one_quaternion), 0U, two_joint_rig(), 1024.0F,
            kPoseLimits));
      },
      "pose decoder accepted a primary payload smaller than joint_count * 8");

  const std::array<std::array<std::int16_t, 4U>, 2U> zero_quaternion{
      {{0, 0, 0, 0}, {0, 0, 0, 32767}}};
  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            make_sequence(zero_quaternion), 0U, two_joint_rig(), 1024.0F,
            kPoseLimits));
      },
      "pose decoder accepted a zero quaternion");

  auto damaged = make_sequence(identity_quaternions());
  --damaged.frames[0U].primary_payload_range.size;
  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            damaged, 0U, two_joint_rig(), 1024.0F, kPoseLimits));
      },
      "pose decoder trusted a damaged primary range");
}

void test_rejects_malformed_sparse_tags_and_duplicates() {
  const auto quaternions = identity_quaternions();
  const auto expect_bad_scale =
      [&](const std::span<const RawScaleRecord> scales,
          const std::string &message) {
        expect_pose_error(
            [&] {
              static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
                  make_sequence(quaternions, scales), 0U, two_joint_rig(),
                  1024.0F, kPoseLimits));
            },
            message);
      };
  const std::array<RawScaleRecord, 1U> unknown_scale_tag{
      RawScaleRecord{{4096U, 4096U, 4096U}, UINT16_C(0x0100)}};
  expect_bad_scale(unknown_scale_tag,
                   "pose decoder accepted unknown sparse scale tag bits");
  const std::array<RawScaleRecord, 1U> out_of_range_scale{
      RawScaleRecord{{4096U, 4096U, 4096U}, 2U}};
  expect_bad_scale(out_of_range_scale,
                   "pose decoder accepted an out-of-range scale joint");
  const std::array<RawScaleRecord, 2U> duplicate_scale{
      RawScaleRecord{{4096U, 4096U, 4096U}, 0U},
      RawScaleRecord{{4096U, 4096U, 4096U}, UINT16_C(0x8000)}};
  expect_bad_scale(duplicate_scale,
                   "pose decoder accepted duplicate scale records");

  const auto expect_bad_translation =
      [&](const std::span<const RawTranslationRecord> translations,
          const std::string &message) {
        expect_pose_error(
            [&] {
              static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
                  make_sequence(quaternions, {}, translations), 0U,
                  two_joint_rig(), 1024.0F, kPoseLimits));
            },
            message);
      };
  const std::array<RawTranslationRecord, 1U> unknown_translation_tag{
      RawTranslationRecord{{1, 2, 3}, UINT16_C(0x0100)}};
  expect_bad_translation(
      unknown_translation_tag,
      "pose decoder accepted unknown sparse translation tag bits");
  const std::array<RawTranslationRecord, 1U> out_of_range_translation{
      RawTranslationRecord{{1, 2, 3}, 2U}};
  expect_bad_translation(
      out_of_range_translation,
      "pose decoder accepted an out-of-range translation joint");
  const std::array<RawTranslationRecord, 2U> duplicate_translation{
      RawTranslationRecord{{1, 2, 3}, 1U}, RawTranslationRecord{{4, 5, 6}, 1U}};
  expect_bad_translation(duplicate_translation,
                         "pose decoder accepted duplicate translations");
}

void test_rejects_invalid_limits_rig_and_numeric_inputs() {
  const auto sequence = make_sequence(identity_quaternions());
  auto small_limits = kPoseLimits;
  small_limits.max_joints = 1U;
  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            sequence, 0U, two_joint_rig(), 1024.0F, small_limits));
      },
      "pose decoder exceeded its joint limit");

  small_limits = kPoseLimits;
  small_limits.max_frame_payload_bytes = 1U;
  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            sequence, 0U, two_joint_rig(), 1024.0F, small_limits));
      },
      "pose decoder exceeded its frame payload limit");

  const std::array<RawScaleRecord, 2U> two_scales{
      RawScaleRecord{{4096U, 4096U, 4096U}, 0U},
      RawScaleRecord{{4096U, 4096U, 4096U}, 1U}};
  const auto sparse_sequence =
      make_sequence(identity_quaternions(), two_scales);
  small_limits = kPoseLimits;
  small_limits.max_sparse_scale_records = 1U;
  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            sparse_sequence, 0U, two_joint_rig(), 1024.0F, small_limits));
      },
      "pose decoder exceeded its sparse scale limit");

  const std::array<RawTranslationRecord, 2U> two_translations{
      RawTranslationRecord{{1, 2, 3}, 0U}, RawTranslationRecord{{4, 5, 6}, 1U}};
  const auto translation_sequence =
      make_sequence(identity_quaternions(), {}, two_translations);
  small_limits = kPoseLimits;
  small_limits.max_sparse_translation_records = 1U;
  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            translation_sequence, 0U, two_joint_rig(), 1024.0F, small_limits));
      },
      "pose decoder exceeded its sparse translation limit");

  auto bad_rig = two_joint_rig();
  bad_rig.actor_rig.joints[1U].parent_index = 1;
  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            sequence, 0U, bad_rig, 1024.0F, kPoseLimits));
      },
      "pose decoder accepted a forward parent reference");

  bad_rig = two_joint_rig();
  bad_rig.source_common_translations.pop_back();
  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            sequence, 0U, bad_rig, 1024.0F, kPoseLimits));
      },
      "pose decoder accepted missing common translations");

  bad_rig = two_joint_rig();
  bad_rig.source_common_translations[0U][0U] =
      std::numeric_limits<float>::quiet_NaN();
  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            sequence, 0U, bad_rig, 1024.0F, kPoseLimits));
      },
      "pose decoder accepted a non-finite common translation");

  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            sequence, 0U, two_joint_rig(),
            std::numeric_limits<float>::infinity(), kPoseLimits));
      },
      "pose decoder accepted a non-finite class scale");
  expect_pose_error(
      [&] {
        static_cast<void>(openrc::decode_rac_ratchet_regular_pose_v1(
            sequence, 1U, two_joint_rig(), 1024.0F, kPoseLimits));
      },
      "pose decoder accepted an out-of-range frame index");
}

struct RealLevelData {
  std::vector<std::byte> decoded_assets;
  openrc::RacLevelCoreIndexV1 core;
};

struct RealPoseAggregate {
  std::uint64_t levels = 0U;
  std::uint64_t sequences = 0U;
  std::uint64_t frames = 0U;
  std::uint64_t matrix_components = 0U;
  std::uint64_t singular_global_matrices = 0U;
  std::uint64_t singular_skin_matrices = 0U;
  std::uint64_t frames_with_singular_global = 0U;
  std::uint64_t frames_with_singular_skin = 0U;
};

[[nodiscard]] std::vector<std::byte>
read_disc_extent(const std::filesystem::path &image_path,
                 const std::uint64_t lba, const std::uint64_t sectors) {
  if (sectors > kRealDataMaximumBytes / openrc::kDiscTocSectorSize) {
    fail("real-data primary extent exceeds the diagnostic limit");
  }
  const auto byte_count = sectors * openrc::kDiscTocSectorSize;
  if (lba > std::numeric_limits<std::uint64_t>::max() /
                openrc::kDiscTocSectorSize ||
      byte_count > std::numeric_limits<std::size_t>::max()) {
    fail("real-data primary extent overflows host limits");
  }
  const auto byte_offset = lba * openrc::kDiscTocSectorSize;
  std::ifstream input(image_path, std::ios::binary | std::ios::ate);
  if (!input) {
    fail("cannot open real-data disc image");
  }
  const auto end_position = input.tellg();
  if (end_position < 0) {
    fail("cannot determine real-data disc size");
  }
  const auto image_bytes = static_cast<std::uint64_t>(end_position);
  if (byte_offset > image_bytes || byte_count > image_bytes - byte_offset) {
    fail("real-data primary extent leaves the disc image");
  }
  input.seekg(static_cast<std::streamoff>(byte_offset), std::ios::beg);
  std::vector<std::byte> result(static_cast<std::size_t>(byte_count));
  input.read(reinterpret_cast<char *>(result.data()),
             static_cast<std::streamsize>(result.size()));
  if (input.gcount() != static_cast<std::streamsize>(result.size())) {
    fail("short read from real-data primary extent");
  }
  return result;
}

[[nodiscard]] RealLevelData
load_real_level(const std::filesystem::path &image_path,
                const openrc::DiscTocAssetReport &report,
                const std::uint32_t level_id) {
  const auto assets = std::find_if(report.levels.begin(), report.levels.end(),
                                   [level_id](const auto &candidate) {
                                     return candidate.level_id == level_id;
                                   });
  const auto layout =
      std::find_if(report.layout.levels.begin(), report.layout.levels.end(),
                   [level_id](const auto &candidate) {
                     return candidate.level_id == level_id;
                   });
  if (assets == report.levels.end() || layout == report.layout.levels.end() ||
      layout->primary_extents.empty()) {
    fail("requested real-data level is absent");
  }

  constexpr std::size_t kIndexSubrange = 2U;
  constexpr std::size_t kAssetSubrange = 10U;
  const auto primary =
      read_disc_extent(image_path, layout->primary_extents.front().lba,
                       layout->primary_extents.front().sectors);
  const auto bounded = [&primary](const openrc::DiscTocSubrange &descriptor) {
    const auto offset = static_cast<std::uint64_t>(descriptor.relative_offset);
    const auto size = static_cast<std::uint64_t>(descriptor.byte_size);
    if (offset > primary.size() || size > primary.size() - offset) {
      fail("real-data subrange leaves its primary extent");
    }
    return std::span<const std::byte>(primary).subspan(
        static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
  };
  const auto index_bytes =
      bounded(assets->primary_extent0.subranges[kIndexSubrange]);
  const auto encoded_assets =
      bounded(assets->primary_extent0.subranges[kAssetSubrange]);
  auto decoded =
      openrc::decode_wad_bytes(encoded_assets, kRealDataMaximumBytes);
  auto core = openrc::parse_rac_level_core_index_v1(
      index_bytes, encoded_assets, decoded.bytes, kRealCoreLimits);
  return {std::move(decoded.bytes), std::move(core)};
}

[[nodiscard]] std::span<const std::byte>
bounded_decoded_range(const RealLevelData &level,
                      const openrc::RacLevelCoreRangeV1 range,
                      const char *const description) {
  if (range.offset > level.decoded_assets.size() ||
      range.size > level.decoded_assets.size() - range.offset) {
    fail(std::string(description) + " leaves the decoded level core");
  }
  return std::span<const std::byte>(level.decoded_assets)
      .subspan(static_cast<std::size_t>(range.offset),
               static_cast<std::size_t>(range.size));
}

[[nodiscard]] double
linear_determinant(const openrc::ActorAffineTransformV1 &transform) {
  const auto &m = transform.values;
  return static_cast<double>(m[0U]) * (static_cast<double>(m[5U]) * m[10U] -
                                       static_cast<double>(m[6U]) * m[9U]) -
         static_cast<double>(m[1U]) * (static_cast<double>(m[4U]) * m[10U] -
                                       static_cast<double>(m[6U]) * m[8U]) +
         static_cast<double>(m[2U]) * (static_cast<double>(m[4U]) * m[9U] -
                                       static_cast<double>(m[5U]) * m[8U]);
}

[[nodiscard]] bool observe_palette(const openrc::ActorPosePaletteV1 &palette,
                                   RealPoseAggregate &aggregate,
                                   const bool globals) {
  const auto &transforms =
      globals ? palette.global_joint_transforms : palette.skin_transforms;
  bool frame_is_singular = false;
  for (const auto &transform : transforms) {
    for (const auto value : transform.values) {
      if (!std::isfinite(value)) {
        fail("real-data pose decoder emitted a non-finite matrix component");
      }
      ++aggregate.matrix_components;
    }
    const auto determinant = linear_determinant(transform);
    if (!std::isfinite(determinant)) {
      fail("real-data pose decoder emitted a non-finite determinant");
    }
    if (std::abs(determinant) < 1.0e-8) {
      frame_is_singular = true;
      if (globals) {
        ++aggregate.singular_global_matrices;
      } else {
        ++aggregate.singular_skin_matrices;
      }
    }
  }
  return frame_is_singular;
}

void print_level_zero_palette_bounds(
    const openrc::ActorPosePaletteV1 &palette) {
  std::array<float, 3U> minimum_translation{
      std::numeric_limits<float>::infinity(),
      std::numeric_limits<float>::infinity(),
      std::numeric_limits<float>::infinity()};
  std::array<float, 3U> maximum_translation{
      -std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity()};
  auto minimum_component = std::numeric_limits<float>::infinity();
  auto maximum_component = -std::numeric_limits<float>::infinity();
  for (const auto &transform : palette.global_joint_transforms) {
    for (const auto value : transform.values) {
      minimum_component = std::min(minimum_component, value);
      maximum_component = std::max(maximum_component, value);
    }
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
      const auto value = transform.values[axis * 4U + 3U];
      minimum_translation[axis] = std::min(minimum_translation[axis], value);
      maximum_translation[axis] = std::max(maximum_translation[axis], value);
    }
  }
  std::cout << "level=0 logical_sequence=0 frame=0 global_translation_min=("
            << minimum_translation[0U] << ',' << minimum_translation[1U] << ','
            << minimum_translation[2U] << ") global_translation_max=("
            << maximum_translation[0U] << ',' << maximum_translation[1U] << ','
            << maximum_translation[2U]
            << ") global_component_min=" << minimum_component
            << " global_component_max=" << maximum_component << '\n';
}

[[nodiscard]] openrc::ActorSkinnedMeshV1
make_real_skinning_mesh(const openrc::RacMobyBindPoseGeometryV1 &bind_pose) {
  if (bind_pose.geometry.vertices.size() !=
      bind_pose.vertex_skin_bindings.size()) {
    fail("real-data geometry and skin bindings are not parallel");
  }
  openrc::ActorSkinnedMeshV1 result;
  result.vertices.reserve(bind_pose.geometry.vertices.size());
  for (std::size_t index = 0U; index < bind_pose.geometry.vertices.size();
       ++index) {
    const auto &source = bind_pose.geometry.vertices[index];
    result.vertices.push_back(openrc::ActorSkinnedVertexV1{
        source.diagnostic_position[0U], source.diagnostic_position[1U],
        source.diagnostic_position[2U], source.diagnostic_normal[0U],
        source.diagnostic_normal[1U], source.diagnostic_normal[2U],
        source.texture_coordinate[0U], source.texture_coordinate[1U],
        UINT32_C(0xffffffff), bind_pose.vertex_skin_bindings[index]});
  }
  return result;
}

void try_real_level_zero_skinning(const std::span<const std::byte> class_bytes,
                                  const openrc::RacMobyClassV1 &moby,
                                  const openrc::ActorPosePaletteV1 &palette) {
  const auto bind_pose = openrc::compile_rac_moby_bind_pose_geometry_v1(
      class_bytes, moby, openrc::RacMobyLodV1::high, kRealBindPoseLimits);
  const auto mesh = make_real_skinning_mesh(bind_pose);
  const auto positions = openrc::pose_actor_mesh_positions_v1(
      mesh, palette, openrc::ActorAffineTransformV1{}, kRealSkinningLimits);
  std::array<float, 3U> minimum{std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::infinity()};
  std::array<float, 3U> maximum{-std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity()};
  for (const auto &position : positions) {
    const std::array<float, 3U> xyz{position.x, position.y, position.z};
    for (std::size_t axis = 0U; axis < xyz.size(); ++axis) {
      if (!std::isfinite(xyz[axis])) {
        fail("real-data position skinning emitted a non-finite value");
      }
      minimum[axis] = std::min(minimum[axis], xyz[axis]);
      maximum[axis] = std::max(maximum[axis], xyz[axis]);
    }
  }
  std::cout << "level=0 logical_sequence=0 frame=0 position_skinning=passed"
            << " vertices=" << positions.size() << " bounds_min=("
            << minimum[0U] << ',' << minimum[1U] << ',' << minimum[2U]
            << ") bounds_max=(" << maximum[0U] << ',' << maximum[1U] << ','
            << maximum[2U] << ")\n";

  try {
    const auto vertices = openrc::pose_actor_mesh_vertices_v1(
        mesh, palette, openrc::ActorAffineTransformV1{}, kRealSkinningLimits);
    fail("full-normal skinning unexpectedly accepted the singular real pose " +
         std::to_string(vertices.size()));
  } catch (const openrc::ActorPoseError &error) {
    std::cout << "level=0 logical_sequence=0 frame=0 full_skinning=rejected"
              << " reason=" << error.what() << '\n';
  }
}

[[nodiscard]] openrc::ActorRigV1
canonical_runtime_rig(openrc::ActorRigV1 rig) {
  for (auto &joint : rig.joints) {
    for (auto &component : joint.local_bind_transform.values) {
      if (component == 0.0F) {
        component = 0.0F;
      }
    }
    for (auto &component : joint.inverse_bind_transform.values) {
      if (component == 0.0F) {
        component = 0.0F;
      }
    }
  }
  return rig;
}

void expect_palettes_near(const openrc::ActorPosePaletteV1 &actual,
                          const openrc::ActorPosePaletteV1 &expected,
                          const std::string &description) {
  expect(actual.global_joint_transforms.size() ==
                 expected.global_joint_transforms.size() &&
             actual.skin_transforms.size() == expected.skin_transforms.size(),
         description + " palette sizes differ");
  const auto compare = [&description](const auto &actual_transforms,
                                      const auto &expected_transforms,
                                      const char *const palette_name) {
    for (std::size_t joint = 0U; joint < actual_transforms.size(); ++joint) {
      for (std::size_t component = 0U;
           component < actual_transforms[joint].values.size(); ++component) {
        expect_near(actual_transforms[joint].values[component],
                    expected_transforms[joint].values[component],
                    description + " " + palette_name + " joint " +
                        std::to_string(joint) + " component " +
                        std::to_string(component),
                    1.0e-4F);
      }
    }
  };
  compare(actual.global_joint_transforms, expected.global_joint_transforms,
          "global");
  compare(actual.skin_transforms, expected.skin_transforms, "skin");
}

void verify_real_level_zero_animation_chain(
    const RealLevelData &level, const openrc::RacMobyClassV1 &moby,
    const openrc::RacMobyBindRigV1 &bind_rig) {
  const std::array profiles{
      openrc::RacRatchetAnimationClipProfileV1{
          0U, 0U, "actors/ratchet/idle",
          openrc::ActorAnimationWrapModeV1::loop},
      openrc::RacRatchetAnimationClipProfileV1{
          1U, 3U, "actors/ratchet/walk",
          openrc::ActorAnimationWrapModeV1::loop},
      openrc::RacRatchetAnimationClipProfileV1{
          2U, 4U, "actors/ratchet/run",
          openrc::ActorAnimationWrapModeV1::loop},
  };
  const auto compiled = openrc::compile_rac_ratchet_animation_bank_v1(
      level.decoded_assets, level.core, bind_rig, moby.scale,
      "actors/ratchet/rig", profiles, 50U, kRealAnimationCompileLimits);
  const auto encoded = openrc::encode_actor_animation_bank_v1(
      compiled, kRealAnimationIoLimits);
  const auto decoded = openrc::decode_actor_animation_bank_v1(
      encoded, kRealAnimationIoLimits);
  const auto rig = canonical_runtime_rig(bind_rig.actor_rig);

  for (const auto &profile : profiles) {
    const auto source_offset = level.core.ratchet_sequence_offsets.at(
        static_cast<std::size_t>(profile.source_slot));
    const auto source = std::find_if(
        level.core.ratchet_sequences.begin(),
        level.core.ratchet_sequences.end(),
        [source_offset](const auto &candidate) {
          return candidate.asset_offset == source_offset;
        });
    expect(source_offset != 0U && source != level.core.ratchet_sequences.end(),
           "Veldin animation profile does not resolve to a source sequence");
    const auto sequence = openrc::parse_rac_ratchet_sequence_v1(
        level.decoded_assets,
        {source->asset_range.offset, source->asset_range.size},
        kRealSequenceLimits);
    const auto source_pose = openrc::decode_rac_ratchet_regular_pose_v1(
        sequence, 0U, bind_rig, moby.scale, kRealPoseLimits);

    const auto *const clip = openrc::find_actor_animation_clip_v1(
        decoded, profile.semantic_key);
    expect(clip != nullptr,
           "round-tripped Veldin animation bank lost a mapped clip");
    const auto state = openrc::start_actor_animation_playback_v1(*clip);
    const auto runtime_pose = openrc::sample_actor_animation_pose_v1(
        *clip, state, "actors/ratchet/rig", rig, kRealPlaybackLimits);
    expect_palettes_near(runtime_pose, source_pose.palette,
                         "Veldin " + profile.semantic_key + " frame 0");
  }
  std::cout << "level=0 animation_chain=passed clips=" << profiles.size()
            << " encoded_bytes=" << encoded.size() << '\n';
}

void scan_real_level(const std::filesystem::path &image_path,
                     const openrc::DiscTocAssetReport &report,
                     const std::uint32_t level_id,
                     RealPoseAggregate &aggregate) {
  const auto level = load_real_level(image_path, report, level_id);
  const auto class_entry = std::find_if(
      level.core.moby_classes.begin(), level.core.moby_classes.end(),
      [](const auto &candidate) {
        return candidate.class_id == 0 && candidate.asset_range.size != 0U;
      });
  if (class_entry == level.core.moby_classes.end()) {
    fail("real-data level has no local class-0 Ratchet asset");
  }
  const auto class_bytes = bounded_decoded_range(
      level, class_entry->asset_range, "The class-0 Ratchet asset");
  const auto moby = openrc::parse_rac_moby_class_v1(
      class_bytes, {kRealDataMaximumBytes, false});
  const auto bind_rig =
      openrc::decode_rac_moby_bind_rig_v1(class_bytes, moby, kRealRigLimits);
  if (level_id == 0U) {
    verify_real_level_zero_animation_chain(level, moby, bind_rig);
  }

  const auto logical_zero_offset = level.core.ratchet_sequence_offsets[0U];
  std::optional<openrc::RacRatchetPoseV1> level_zero_pose;
  std::uint64_t level_frames = 0U;
  std::uint64_t level_singular_global_frames = 0U;
  std::uint64_t level_singular_skin_frames = 0U;
  const auto prior_singular_globals = aggregate.singular_global_matrices;
  const auto prior_singular_skins = aggregate.singular_skin_matrices;
  for (const auto &candidate : level.core.ratchet_sequences) {
    openrc::RacRatchetSequenceV1 sequence;
    try {
      sequence = openrc::parse_rac_ratchet_sequence_v1(
          level.decoded_assets,
          {candidate.asset_range.offset, candidate.asset_range.size},
          kRealSequenceLimits);
    } catch (const std::exception &error) {
      fail("level " + std::to_string(level_id) + " sequence at " +
           std::to_string(candidate.asset_range.offset) +
           " failed structural parsing: " + error.what());
    }
    ++aggregate.sequences;
    for (std::size_t frame_index = 0U; frame_index < sequence.frames.size();
         ++frame_index) {
      openrc::RacRatchetPoseV1 pose;
      try {
        pose = openrc::decode_rac_ratchet_regular_pose_v1(
            sequence, frame_index, bind_rig, moby.scale, kRealPoseLimits);
      } catch (const std::exception &error) {
        fail("level " + std::to_string(level_id) + " sequence at " +
             std::to_string(candidate.asset_range.offset) + " frame " +
             std::to_string(frame_index) +
             " failed pose decoding: " + error.what());
      }
      ++aggregate.frames;
      ++level_frames;
      const auto global_singular =
          observe_palette(pose.palette, aggregate, true);
      const auto skin_singular =
          observe_palette(pose.palette, aggregate, false);
      if (global_singular) {
        ++aggregate.frames_with_singular_global;
        ++level_singular_global_frames;
      }
      if (skin_singular) {
        ++aggregate.frames_with_singular_skin;
        ++level_singular_skin_frames;
      }
      if (level_id == 0U && candidate.asset_offset == logical_zero_offset &&
          frame_index == 0U) {
        level_zero_pose = std::move(pose);
      }
    }
  }
  ++aggregate.levels;
  const auto current_level_singular_globals =
      aggregate.singular_global_matrices - prior_singular_globals;
  const auto current_level_singular_skins =
      aggregate.singular_skin_matrices - prior_singular_skins;
  std::cout << "level=" << level_id
            << " sequences=" << level.core.ratchet_sequences.size()
            << " frames=" << level_frames
            << " singular_global_matrices=" << current_level_singular_globals
            << " singular_skin_matrices=" << current_level_singular_skins
            << " frames_with_singular_global=" << level_singular_global_frames
            << " frames_with_singular_skin=" << level_singular_skin_frames
            << '\n';

  if (level_id == 0U) {
    if (!level_zero_pose) {
      fail("level 0 logical sequence 0 frame 0 is absent");
    }
    print_level_zero_palette_bounds(level_zero_pose->palette);
    try_real_level_zero_skinning(class_bytes, moby, level_zero_pose->palette);
  }
}

void scan_real_disc(const std::filesystem::path &image_path,
                    const bool all_levels) {
  const auto report = openrc::inspect_disc_toc_assets(image_path);
  RealPoseAggregate aggregate;
  const auto level_count = all_levels ? openrc::kDiscTocLevelCount : 1U;
  for (std::uint32_t level_id = 0U; level_id < level_count; ++level_id) {
    scan_real_level(image_path, report, level_id, aggregate);
  }
  std::cout << "aggregate levels=" << aggregate.levels
            << " sequences=" << aggregate.sequences
            << " decoded_frames=" << aggregate.frames
            << " finite_matrix_components=" << aggregate.matrix_components
            << " singular_global_matrices="
            << aggregate.singular_global_matrices
            << " singular_skin_matrices=" << aggregate.singular_skin_matrices
            << " frames_with_singular_global="
            << aggregate.frames_with_singular_global
            << " frames_with_singular_skin="
            << aggregate.frames_with_singular_skin << '\n';
}

} // namespace

int main(const int argc, char **argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--scan-disc") {
      scan_real_disc(argv[2], false);
      return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--scan-disc-all") {
      scan_real_disc(argv[2], true);
      return 0;
    }
    if (argc != 1) {
      fail("usage: openrc-rac-ratchet-pose-tests "
           "[--scan-disc|--scan-disc-all disc.iso]");
    }
    test_quaternion_order_normalization_and_translation_scale();
    test_local_scale_propagates_to_child_and_skinning();
    test_terminal_scale_does_not_propagate();
    test_zero_scale_is_preserved_in_direct_palette();
    test_rejects_malformed_primary_and_quaternions();
    test_rejects_malformed_sparse_tags_and_duplicates();
    test_rejects_invalid_limits_rig_and_numeric_inputs();
    std::cout << "RAC Ratchet pose tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC Ratchet pose tests failed: " << error.what() << '\n';
    return 1;
  }
}
