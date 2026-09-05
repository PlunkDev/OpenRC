#include "openrc/rac_ratchet_animation_compile.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kFirstSequenceOffset = 0x40U;
constexpr std::uint32_t kSecondSequenceOffset = 0xc0U;
constexpr std::uint32_t kSequenceBytes = 0x80U;

constexpr openrc::RacRatchetAnimationCompileLimitsV1 kLimits{
    openrc::RacRatchetSequenceLimitsV1{0x200U, kSequenceBytes, 8U, 8U},
    openrc::RacRatchetPoseLimitsV1{4U, 0x100U, 8U, 8U, 1.0e-8},
    openrc::ActorAnimationLimitsV1{8U, 16U, 32U, 4U, 128U, 128U,
                                   1024U, 120U, 1.0e6F, 1.0e-8}};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void write_u16(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_u32(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void write_f32(std::vector<std::byte> &bytes, const std::size_t offset,
               const float value) {
  write_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void write_sequence(std::vector<std::byte> &bytes, const std::size_t base,
                    const float sequence_rate, const float frame_rate,
                    const std::uint16_t local_z_scale) {
  bytes[base + 0x10U] = std::byte{1U};
  bytes[base + 0x11U] = std::byte{0xffU};
  write_f32(bytes, base + 0x18U, sequence_rate);
  write_u32(bytes, base + 0x1cU, 0x20U);

  write_f32(bytes, base + 0x20U, frame_rate);
  write_u16(bytes, base + 0x26U, 1U);
  write_u16(bytes, base + 0x28U, 8U);
  write_u16(bytes, base + 0x2aU, 1U);
  write_u16(bytes, base + 0x2cU, 16U);
  write_u16(bytes, base + 0x2eU, 0U);

  write_u16(bytes, base + 0x30U, 0U);
  write_u16(bytes, base + 0x32U, 0U);
  write_u16(bytes, base + 0x34U, 0U);
  write_u16(bytes, base + 0x36U, 32767U);
  write_u16(bytes, base + 0x38U, 4096U);
  write_u16(bytes, base + 0x3aU, 4096U);
  write_u16(bytes, base + 0x3cU, local_z_scale);
  write_u16(bytes, base + 0x3eU, 0x8000U);
}

struct Fixture {
  std::vector<std::byte> bytes = std::vector<std::byte>(0x140U);
  openrc::RacLevelCoreIndexV1 core;
  openrc::RacMobyBindRigV1 rig;
  std::array<openrc::RacRatchetAnimationClipProfileV1, 2U> profiles{
      openrc::RacRatchetAnimationClipProfileV1{
          0U, 0U, "actors/ratchet/idle",
          openrc::ActorAnimationWrapModeV1::loop},
      openrc::RacRatchetAnimationClipProfileV1{
          1U, 3U, "actors/ratchet/walk",
          openrc::ActorAnimationWrapModeV1::loop}};

  Fixture() {
    write_sequence(bytes, kFirstSequenceOffset, 0.5F, 0.125F, 0U);
    write_sequence(bytes, kSecondSequenceOffset, 0.0F, 0.25F, 4096U);
    core.decoded_asset_input_bytes = bytes.size();
    core.ratchet_sequence_offsets[0U] = kFirstSequenceOffset;
    core.ratchet_sequence_offsets[3U] = kSecondSequenceOffset;
    core.ratchet_sequences = {
        {kFirstSequenceOffset, {kFirstSequenceOffset, kSequenceBytes}},
        {kSecondSequenceOffset, {kSecondSequenceOffset, kSequenceBytes}},
    };
    rig.actor_rig.joints.push_back(openrc::ActorRigJointV1{});
    rig.actor_rig.joints.front().local_bind_transform.values[1U] = -0.0F;
    rig.actor_rig.joints.front().inverse_bind_transform.values[2U] = -0.0F;
    rig.source_common_translations.push_back({1.0F, 2.0F, 3.0F});
  }

  [[nodiscard]] openrc::ActorAnimationBankV1 compile() const {
    return openrc::compile_rac_ratchet_animation_bank_v1(
        bytes, core, rig, 1024.0F, "actors/ratchet/rig", profiles, 50U,
        kLimits);
  }
};

template <typename Mutation>
void expect_rejected(Mutation &&mutation, const std::string &message) {
  Fixture fixture;
  std::invoke(std::forward<Mutation>(mutation), fixture);
  try {
    static_cast<void>(fixture.compile());
  } catch (const openrc::RacRatchetAnimationCompileError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_compiles_mapped_clips() {
  const Fixture fixture;
  const auto first = fixture.compile();
  const auto second = fixture.compile();
  expect(first == second && first.clips.size() == 2U,
         "RAC Ratchet animation compilation is not deterministic");
  const auto &idle = first.clips[0U];
  const auto &walk = first.clips[1U];
  expect(idle.id == 0U && idle.semantic_key == "actors/ratchet/idle" &&
             idle.rig_key == "actors/ratchet/rig" &&
             idle.source_updates_per_second == 50U &&
             idle.wrap_mode == openrc::ActorAnimationWrapModeV1::loop &&
             idle.frames.size() == 1U && idle.frames[0U].phase_rate == 0.5F,
         "sequence-level phase rate was not compiled into the idle clip");
  auto canonical_rig = fixture.rig.actor_rig;
  canonical_rig.joints.front().local_bind_transform.values[1U] = 0.0F;
  canonical_rig.joints.front().inverse_bind_transform.values[2U] = 0.0F;
  expect(idle.rig_content_sha256 ==
             openrc::actor_rig_content_sha256_v1(canonical_rig),
         "animation compiler did not canonicalize signed-zero rig identity");
  expect(walk.id == 1U && walk.frames.size() == 1U &&
             walk.frames[0U].phase_rate == 0.25F,
         "per-frame phase rate was not compiled into the walk clip");
  const auto &joint = idle.frames[0U].joint_poses[0U];
  expect(joint.normalized_rotation_xyzw[3U] == 1.0F &&
             joint.translation == std::array<float, 3U>{1.0F, 2.0F, 3.0F} &&
             joint.local_scale == std::array<float, 3U>{1.0F, 1.0F, 0.0F} &&
             joint.terminal_scale ==
                 std::array<float, 3U>{1.0F, 1.0F, 1.0F},
         "decoded joint pose was not preserved in the neutral clip");
}

void test_builds_complete_source_addressed_profiles() {
  const Fixture fixture;
  const std::array confirmed{
      openrc::RacRatchetAnimationClipProfileV1{
          0U, 0U, "actors/ratchet/source-sequence/000",
          openrc::ActorAnimationWrapModeV1::loop}};
  const auto profiles =
      openrc::make_rac_ratchet_complete_animation_profiles_v1(
          fixture.core, confirmed, "actors/ratchet/source-sequence/",
          openrc::ActorAnimationWrapModeV1::clamp);
  expect(profiles.size() == 2U && profiles[0U].clip_id == 0U &&
             profiles[0U].source_slot == 0U &&
             profiles[0U].semantic_key ==
                 "actors/ratchet/source-sequence/000" &&
             profiles[0U].wrap_mode ==
                 openrc::ActorAnimationWrapModeV1::loop &&
             profiles[1U].clip_id == 1U &&
             profiles[1U].source_slot == 3U &&
             profiles[1U].semantic_key ==
                 "actors/ratchet/source-sequence/003" &&
             profiles[1U].wrap_mode ==
                 openrc::ActorAnimationWrapModeV1::clamp,
         "complete Ratchet profile did not preserve confirmed mappings and "
         "source-address every remaining slot");

  const auto source_only =
      openrc::make_rac_ratchet_complete_animation_profiles_v1(
          fixture.core,
          std::span<const openrc::RacRatchetAnimationClipProfileV1>{},
          "source/", openrc::ActorAnimationWrapModeV1::clamp);
  expect(source_only.size() == 2U &&
             source_only[0U].semantic_key == "source/000" &&
             source_only[1U].semantic_key == "source/003",
         "source-only Ratchet profile is not canonical by source slot");

  try {
    static_cast<void>(
        openrc::make_rac_ratchet_complete_animation_profiles_v1(
            fixture.core, confirmed, "",
            openrc::ActorAnimationWrapModeV1::clamp));
  } catch (const openrc::RacRatchetAnimationCompileError &) {
    return;
  }
  throw std::runtime_error(
      "complete Ratchet profile accepted an empty source-key prefix");
}

void test_rejects_inconsistent_sources_and_profiles() {
  expect_rejected(
      [](Fixture &fixture) { --fixture.core.decoded_asset_input_bytes; },
      "animation compiler accepted a mismatched decoded source size");
  expect_rejected(
      [](Fixture &fixture) { fixture.core.ratchet_sequence_offsets[0U] = 0U; },
      "animation compiler accepted an empty mapped source slot");
  expect_rejected(
      [](Fixture &fixture) { fixture.profiles[1U].source_slot = 0U; },
      "animation compiler accepted duplicate mapped source slots");
  expect_rejected(
      [](Fixture &fixture) { fixture.profiles[1U].clip_id = 2U; },
      "animation compiler accepted sparse neutral clip IDs");
  expect_rejected(
      [](Fixture &fixture) {
        write_u32(fixture.bytes, kFirstSequenceOffset + 0x20U, 0x7fc00000U);
      },
      "animation compiler accepted a non-finite source phase rate");
}

} // namespace

int main() {
  try {
    test_compiles_mapped_clips();
    test_builds_complete_source_addressed_profiles();
    test_rejects_inconsistent_sources_and_profiles();
    std::cout << "RAC Ratchet animation compile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC Ratchet animation compile test failure: " << error.what()
              << '\n';
    return 1;
  }
}
