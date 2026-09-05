#include "openrc/actor_animation.hpp"
#include "openrc/actor_animation_io.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::ActorAnimationIoLimitsV1 kLimits{
    4U * 1024U * 1024U,
    {
        8U,
        256U,
        1024U,
        128U,
        16'384U,
        64U,
        1024U,
        120U,
        10'000.0F,
        1.0e-8,
    },
};

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

template <typename Function>
void expect_io_error(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const openrc::ActorAnimationIoError &) {
    return;
  }
  fail(message);
}

template <typename Function>
void expect_animation_error(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const openrc::ActorAnimationError &) {
    return;
  }
  fail(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::byte> bytes,
                                     const std::size_t offset) {
  return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(const std::span<const std::byte> bytes,
                                     const std::size_t offset) {
  std::uint64_t result = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    result |= static_cast<std::uint64_t>(byte_value(bytes[offset + index]))
              << (index * 8U);
  }
  return result;
}

void write_u32(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT32_C(0xff));
  }
}

void write_u64(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint64_t value) {
  for (std::size_t index = 0U; index < 8U; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
  }
}

[[nodiscard]] openrc::PreparedContentDigestV1 digest(const std::uint8_t seed) {
  openrc::PreparedContentDigestV1 result{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = static_cast<std::byte>(
        static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index)));
  }
  return result;
}

[[nodiscard]] openrc::ActorJointPoseV1 pose(const float offset,
                                            const bool singular) {
  openrc::ActorJointPoseV1 result;
  result.normalized_rotation_xyzw = {0.0F, 0.0F, 0.0F, -2.0F};
  result.translation = {offset, -0.0F, -offset};
  result.local_scale = {1.0F, singular ? 0.0F : 1.0F, 1.0F};
  result.terminal_scale = {1.0F, 1.0F, singular ? -0.0F : 1.0F};
  return result;
}

[[nodiscard]] openrc::ActorAnimationFrameV1
frame(const float phase_rate, const float offset, const bool singular) {
  openrc::ActorAnimationFrameV1 result;
  result.phase_rate = phase_rate;
  result.joint_poses = {pose(offset, singular), pose(offset + 1.0F, false)};
  result.joint_poses[1U].normalized_rotation_xyzw = {0.0F, -3.0F, 0.0F, -0.0F};
  return result;
}

[[nodiscard]] openrc::ActorAnimationClipV1
clip(const std::uint32_t id, std::string key,
     const openrc::ActorAnimationWrapModeV1 wrap_mode,
     const std::uint8_t digest_seed, const bool two_frames) {
  openrc::ActorAnimationClipV1 result;
  result.id = id;
  result.semantic_key = std::move(key);
  result.rig_key = "actors/ratchet/rig";
  result.rig_content_sha256 = digest(digest_seed);
  result.source_updates_per_second = 50U;
  result.wrap_mode = wrap_mode;
  result.frames.push_back(
      frame(two_frames ? -0.0F : 0.25F, static_cast<float>(id), two_frames));
  if (two_frames) {
    result.frames.push_back(frame(0.5F, static_cast<float>(id) + 2.0F, false));
  }
  return result;
}

[[nodiscard]] openrc::ActorAnimationBankV1
make_bank(const bool reversed = true) {
  auto idle = clip(0U, "actors/ratchet/idle",
                   openrc::ActorAnimationWrapModeV1::clamp, 0x10U, true);
  auto run = clip(1U, "actors/ratchet/run",
                  openrc::ActorAnimationWrapModeV1::loop, 0x70U, false);
  openrc::ActorAnimationBankV1 result;
  if (reversed) {
    result.clips = {std::move(run), std::move(idle)};
  } else {
    result.clips = {std::move(idle), std::move(run)};
  }
  return result;
}

template <typename Mutation>
void expect_corrupt_decode(Mutation &&mutation, const std::string &message) {
  auto bytes = openrc::encode_actor_animation_bank_v1(make_bank(), kLimits);
  std::forward<Mutation>(mutation)(bytes);
  expect_io_error(
      [&] { (void)openrc::decode_actor_animation_bank_v1(bytes, kLimits); },
      message);
}

void test_identity_round_trip_and_determinism() {
  expect(openrc::kActorAnimationResourceIdV1 == "actors/animations" &&
             openrc::kActorAnimationResourceTypeIdV1 ==
                 "openrc.actor-animation-bank" &&
             openrc::kActorAnimationResourceSchemaVersionV1 == 1U,
         "ActorAnimationBankV1 prepared-resource identity changed");

  const auto canonical =
      openrc::canonicalize_actor_animation_bank_v1(make_bank(), kLimits.bank);
  const auto bytes =
      openrc::encode_actor_animation_bank_v1(make_bank(), kLimits);
  const auto ordered_bytes =
      openrc::encode_actor_animation_bank_v1(make_bank(false), kLimits);
  expect(bytes == ordered_bytes,
         "ActorAnimationBankV1 clip order changed canonical bytes");
  expect(bytes.size() == 873U,
         "ActorAnimationBankV1 exact encoded size changed unexpectedly");
  expect(
      read_u32(bytes, 0x08U) == 1U && read_u32(bytes, 0x0cU) == 0x80U &&
          read_u64(bytes, 0x10U) == bytes.size() &&
          read_u32(bytes, 0x18U) == 1U && read_u32(bytes, 0x1cU) == 1U &&
          read_u32(bytes, 0x20U) == 2U && read_u64(bytes, 0x28U) == 3U &&
          read_u64(bytes, 0x30U) == 6U && read_u64(bytes, 0x38U) == 73U &&
          read_u32(bytes, 0x40U) == 0x60U && read_u32(bytes, 0x44U) == 0x20U &&
          read_u32(bytes, 0x48U) == 0x40U && read_u64(bytes, 0x50U) == 0x80U &&
          read_u64(bytes, 0x58U) == 0x140U &&
          read_u64(bytes, 0x60U) == 0x1a0U && read_u64(bytes, 0x68U) == 0x320U,
      "ActorAnimationBankV1 envelope or aggregate fields are wrong");
  expect(std::equal(canonical.clips[0U].rig_content_sha256.begin(),
                    canonical.clips[0U].rig_content_sha256.end(),
                    bytes.begin() + 0xb8),
         "ActorAnimationBankV1 did not serialize the pinned rig digest");

  const auto decoded = openrc::decode_actor_animation_bank_v1(bytes, kLimits);
  expect(decoded == canonical,
         "ActorAnimationBankV1 round trip changed logical content");
  expect(decoded.clips[0U].frames[0U].phase_rate == 0.0F &&
             !std::signbit(decoded.clips[0U].frames[0U].phase_rate) &&
             decoded.clips[0U].frames[0U].joint_poses[0U].local_scale[1U] ==
                 0.0F &&
             decoded.clips[0U].frames[0U].joint_poses[0U].terminal_scale[2U] ==
                 0.0F,
         "ActorAnimationBankV1 lost a held frame or authored zero scale");
  const auto &identity =
      decoded.clips[0U].frames[0U].joint_poses[0U].normalized_rotation_xyzw;
  const auto &half_turn =
      decoded.clips[0U].frames[0U].joint_poses[1U].normalized_rotation_xyzw;
  expect(identity[0U] == 0.0F && identity[1U] == 0.0F && identity[2U] == 0.0F &&
             identity[3U] == 1.0F && half_turn[0U] == 0.0F &&
             half_turn[1U] == 1.0F && half_turn[2U] == 0.0F &&
             half_turn[3U] == 0.0F,
         "ActorAnimationBankV1 quaternion normalization/sign is wrong");
  expect(openrc::encode_actor_animation_bank_v1(decoded, kLimits) == bytes,
         "ActorAnimationBankV1 re-encoding is not byte deterministic");

  auto opposite_sign = make_bank();
  for (auto &source_clip : opposite_sign.clips) {
    for (auto &source_frame : source_clip.frames) {
      for (auto &source_pose : source_frame.joint_poses) {
        for (auto &component : source_pose.normalized_rotation_xyzw) {
          component = -component;
        }
      }
    }
  }
  expect(openrc::encode_actor_animation_bank_v1(opposite_sign, kLimits) ==
             bytes,
         "Equivalent quaternion signs changed canonical bytes");

  const openrc::ActorAnimationBankV1 empty;
  const auto empty_bytes =
      openrc::encode_actor_animation_bank_v1(empty, kLimits);
  expect(empty_bytes.size() == openrc::kActorAnimationIoHeaderBytesV1 &&
             openrc::decode_actor_animation_bank_v1(empty_bytes, kLimits) ==
                 empty,
         "Canonical empty ActorAnimationBankV1 did not round-trip");
}

void test_bounded_envelope_offsets_and_reserved_rejection() {
  expect_corrupt_decode([](auto &bytes) { bytes[0U] = std::byte{'X'}; },
                        "decoder accepted corrupt animation magic");
  expect_corrupt_decode([](auto &bytes) { write_u32(bytes, 0x08U, 2U); },
                        "decoder accepted an unknown format version");
  expect_corrupt_decode([](auto &bytes) { write_u32(bytes, 0x40U, 1U); },
                        "decoder accepted a wrong clip-record size");
  expect_corrupt_decode([](auto &bytes) { bytes[0x70U] = std::byte{1U}; },
                        "decoder accepted non-zero header reserved data");
  expect_corrupt_decode([](auto &bytes) { bytes[0x9cU] = std::byte{1U}; },
                        "decoder accepted non-zero clip reserved data");
  expect_corrupt_decode([](auto &bytes) { bytes[0x150U] = std::byte{1U}; },
                        "decoder accepted non-zero frame reserved data");
  expect_corrupt_decode([](auto &bytes) { bytes[0x1d4U] = std::byte{1U}; },
                        "decoder accepted non-zero pose reserved data");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 0x20U, UINT32_MAX); },
      "decoder accepted a clip count beyond its caller limit");
  expect_corrupt_decode([](auto &bytes) { write_u64(bytes, 0x58U, 0U); },
                        "decoder accepted a non-canonical table offset");
  expect_corrupt_decode([](auto &bytes) { write_u64(bytes, 0xa0U, 1U); },
                        "decoder accepted a non-canonical first-frame index");
  expect_corrupt_decode([](auto &bytes) { write_u64(bytes, 0xa8U, 0U); },
                        "decoder accepted an overlapping key partition");
  expect_corrupt_decode([](auto &bytes) { bytes.push_back(std::byte{0U}); },
                        "decoder accepted trailing animation data");

  auto small = kLimits;
  small.max_encoded_bytes = 512U;
  const auto bytes =
      openrc::encode_actor_animation_bank_v1(make_bank(), kLimits);
  expect_io_error(
      [&] { (void)openrc::decode_actor_animation_bank_v1(bytes, small); },
      "decoder ignored its encoded-byte envelope");
  expect_io_error(
      [&] { (void)openrc::encode_actor_animation_bank_v1(make_bank(), small); },
      "encoder ignored its encoded-byte envelope");
}

void test_malformed_encoded_values_are_rejected() {
  expect_corrupt_decode([](auto &bytes) { write_u32(bytes, 0x84U, 3U); },
                        "decoder accepted an unknown wrap mode");
  expect_corrupt_decode([](auto &bytes) { write_u32(bytes, 0x88U, 0U); },
                        "decoder accepted a zero source cadence");
  expect_corrupt_decode(
      [](auto &bytes) {
        std::fill(bytes.begin() + 0xb8, bytes.begin() + 0xd8, std::byte{0U});
      },
      "decoder accepted a zero rig content digest");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 0x140U, UINT32_C(0x80000000)); },
      "decoder accepted negative zero phase rate");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 0x140U, UINT32_C(0x7f800000)); },
      "decoder accepted a non-finite phase rate");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 0x1acU, UINT32_C(0x3f000000)); },
      "decoder accepted a non-normalized quaternion");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 0x1acU, UINT32_C(0xbf800000)); },
      "decoder accepted a non-canonical quaternion sign");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 0x1b0U, UINT32_C(0x80000000)); },
      "decoder accepted signed zero in a pose component");
}

void test_encoder_output_with_non_idempotent_float_normalization_decodes() {
  auto source = make_bank(false);
  source.clips[0U].frames[0U].joint_poses[0U].normalized_rotation_xyzw = {
      0.5968679785728455F,
      0.5825540423393250F,
      -0.014720414765179157F,
      0.5515095591545105F,
  };
  const auto once =
      openrc::canonicalize_actor_animation_bank_v1(source, kLimits.bank);
  const auto twice =
      openrc::canonicalize_actor_animation_bank_v1(once, kLimits.bank);
  expect(
      once.clips[0U].frames[0U].joint_poses[0U].normalized_rotation_xyzw !=
          twice.clips[0U].frames[0U].joint_poses[0U].normalized_rotation_xyzw,
      "Rounding-sensitive quaternion fixture stopped exercising repeated "
      "normalization");
  const auto first = openrc::encode_actor_animation_bank_v1(source, kLimits);
  const auto second = openrc::encode_actor_animation_bank_v1(source, kLimits);
  expect(first == second,
         "A rare quaternion made same-input encoding non-deterministic");
  const auto decoded = openrc::decode_actor_animation_bank_v1(first, kLimits);
  expect(decoded == once,
         "Rounding-sensitive quaternion round trip changed canonical data");
  const auto &quaternion =
      decoded.clips[0U].frames[0U].joint_poses[0U].normalized_rotation_xyzw;
  const auto length =
      std::hypot(std::hypot(static_cast<double>(quaternion[0U]),
                            static_cast<double>(quaternion[1U])),
                 std::hypot(static_cast<double>(quaternion[2U]),
                            static_cast<double>(quaternion[3U])));
  expect(std::isfinite(length) && std::fabs(length - 1.0) <= 1.0e-6,
         "Encoder output with a rounding-sensitive quaternion did not "
         "decode as normalized data");
}

void test_model_keys_counts_and_numeric_validation() {
  auto sparse = make_bank();
  sparse.clips[0U].id = 4U;
  expect_io_error(
      [&] { (void)openrc::encode_actor_animation_bank_v1(sparse, kLimits); },
      "encoder accepted sparse clip IDs");

  auto duplicate_key = make_bank(false);
  duplicate_key.clips[1U].semantic_key = duplicate_key.clips[0U].semantic_key;
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(duplicate_key, kLimits);
      },
      "encoder accepted duplicate semantic keys");

  auto bad_key = make_bank();
  bad_key.clips[0U].rig_key = "Actors/ratchet/rig";
  expect_io_error(
      [&] { (void)openrc::encode_actor_animation_bank_v1(bad_key, kLimits); },
      "encoder accepted a non-canonical rig key");

  auto zero_digest = make_bank();
  zero_digest.clips[0U].rig_content_sha256 = {};
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(zero_digest, kLimits);
      },
      "encoder accepted a zero rig digest");

  auto zero_rate = make_bank();
  zero_rate.clips[0U].source_updates_per_second = 0U;
  expect_io_error(
      [&] { (void)openrc::encode_actor_animation_bank_v1(zero_rate, kLimits); },
      "encoder accepted a zero source cadence");

  auto empty_frames = make_bank();
  empty_frames.clips[0U].frames.clear();
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(empty_frames, kLimits);
      },
      "encoder accepted an empty clip");

  auto mismatched_joints = make_bank(false);
  mismatched_joints.clips[0U].frames[1U].joint_poses.pop_back();
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(mismatched_joints,
                                                     kLimits);
      },
      "encoder accepted inconsistent frame joint counts");

  auto negative_phase = make_bank();
  negative_phase.clips[0U].frames[0U].phase_rate = -0.25F;
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(negative_phase, kLimits);
      },
      "encoder accepted a negative phase rate");

  auto non_finite = make_bank();
  non_finite.clips[0U].frames[0U].joint_poses[0U].translation[0U] =
      std::numeric_limits<float>::infinity();
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(non_finite, kLimits);
      },
      "encoder accepted a non-finite pose component");

  auto too_large = make_bank();
  too_large.clips[0U].frames[0U].joint_poses[0U].local_scale[0U] =
      kLimits.bank.max_absolute_component + 1.0F;
  expect_io_error(
      [&] { (void)openrc::encode_actor_animation_bank_v1(too_large, kLimits); },
      "encoder ignored the absolute component limit");

  auto short_quaternion = make_bank();
  short_quaternion.clips[0U]
      .frames[0U]
      .joint_poses[0U]
      .normalized_rotation_xyzw = {};
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(short_quaternion, kLimits);
      },
      "encoder accepted a zero-length quaternion");

  auto canonical =
      openrc::canonicalize_actor_animation_bank_v1(make_bank(), kLimits.bank);
  canonical.clips[0U].frames[0U].joint_poses[0U].translation[0U] = -0.0F;
  expect_animation_error(
      [&] {
        openrc::validate_actor_animation_bank_v1(canonical, kLimits.bank);
      },
      "validator accepted non-canonical signed zero");
}

void test_animation_bank_composition_for_multiple_runtime_actors() {
  const auto source = openrc::canonicalize_actor_animation_bank_v1(
      make_bank(false), kLimits.bank);
  openrc::ActorAnimationBankV1 ratchet;
  ratchet.clips.push_back(source.clips[0U]);
  ratchet = openrc::canonicalize_actor_animation_bank_v1(std::move(ratchet),
                                                          kLimits.bank);

  openrc::ActorAnimationBankV1 toad;
  auto toad_clip = source.clips[1U];
  toad_clip.id = 0U;
  toad_clip.semantic_key = "actors/horny-toad/idle";
  toad_clip.rig_key = "actors/horny-toad/rig";
  toad.clips.push_back(std::move(toad_clip));
  toad = openrc::canonicalize_actor_animation_bank_v1(std::move(toad),
                                                       kLimits.bank);

  const std::vector sources{ratchet, toad};
  const auto combined = openrc::compose_actor_animation_banks_v1(
      std::span<const openrc::ActorAnimationBankV1>(sources), kLimits.bank);
  expect(combined.clips.size() == 2U && combined.clips[0U].id == 0U &&
             combined.clips[1U].id == 1U &&
             combined.clips[0U].semantic_key == "actors/ratchet/idle" &&
             combined.clips[1U].semantic_key == "actors/horny-toad/idle" &&
             combined.clips[1U].rig_key == "actors/horny-toad/rig",
         "animation-bank composition did not retain two actor domains");

  const std::vector duplicate_sources{ratchet, ratchet};
  expect_animation_error(
      [&] {
        (void)openrc::compose_actor_animation_banks_v1(
            std::span<const openrc::ActorAnimationBankV1>(duplicate_sources),
            kLimits.bank);
      },
      "animation-bank composition accepted a repeated clip key");

  auto one_clip_limit = kLimits.bank;
  one_clip_limit.max_clips = 1U;
  expect_animation_error(
      [&] {
        (void)openrc::compose_actor_animation_banks_v1(
            std::span<const openrc::ActorAnimationBankV1>(sources),
            one_clip_limit);
      },
      "animation-bank composition ignored aggregate caller limits");
}

void test_explicit_limits() {
  auto too_few_clips = kLimits;
  too_few_clips.bank.max_clips = 1U;
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(make_bank(),
                                                     too_few_clips);
      },
      "encoder ignored the clip limit");

  auto too_few_frames = kLimits;
  too_few_frames.bank.max_total_frames = 2U;
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(make_bank(),
                                                     too_few_frames);
      },
      "encoder ignored the aggregate frame limit");

  auto too_few_frames_per_clip = kLimits;
  too_few_frames_per_clip.bank.max_frames_per_clip = 1U;
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(make_bank(),
                                                     too_few_frames_per_clip);
      },
      "encoder ignored the per-clip frame limit");

  auto too_few_joints = kLimits;
  too_few_joints.bank.max_joints_per_frame = 1U;
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(make_bank(),
                                                     too_few_joints);
      },
      "encoder ignored the per-frame joint limit");

  auto too_few_poses = kLimits;
  too_few_poses.bank.max_total_joint_poses = 5U;
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(make_bank(),
                                                     too_few_poses);
      },
      "encoder ignored the aggregate joint-pose limit");

  auto short_keys = kLimits;
  short_keys.bank.max_semantic_key_bytes = 8U;
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(make_bank(), short_keys);
      },
      "encoder ignored the per-key length limit");

  auto too_few_key_bytes = kLimits;
  too_few_key_bytes.bank.max_total_semantic_key_bytes = 72U;
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(make_bank(),
                                                     too_few_key_bytes);
      },
      "encoder ignored the aggregate key-byte limit");

  auto slow_source = kLimits;
  slow_source.bank.max_source_updates_per_second = 49U;
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(make_bank(), slow_source);
      },
      "encoder ignored the source-cadence limit");

  auto long_quaternion_required = kLimits;
  long_quaternion_required.bank.minimum_quaternion_length = 2.1;
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_animation_bank_v1(make_bank(),
                                                     long_quaternion_required);
      },
      "encoder ignored the minimum quaternion-length limit");

  auto invalid_limits = kLimits;
  invalid_limits.bank.minimum_quaternion_length = 0.0;
  expect_io_error(
      [&] {
        (void)openrc::decode_actor_animation_bank_v1(
            openrc::encode_actor_animation_bank_v1(make_bank(), kLimits),
            invalid_limits);
      },
      "decoder accepted an invalid zero-valued limit policy");
}

} // namespace

int main() {
  try {
    test_identity_round_trip_and_determinism();
    test_bounded_envelope_offsets_and_reserved_rejection();
    test_malformed_encoded_values_are_rejected();
    test_encoder_output_with_non_idempotent_float_normalization_decodes();
    test_model_keys_counts_and_numeric_validation();
    test_animation_bank_composition_for_multiple_runtime_actors();
    test_explicit_limits();
    std::cout << "ActorAnimationBankV1 tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ActorAnimationBankV1 tests failed: " << error.what() << '\n';
    return 1;
  }
}
