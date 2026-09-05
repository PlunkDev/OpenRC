#include "openrc/rac_moby_animation_compile.hpp"

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
constexpr std::uint32_t kPacketTableOffset = 0x140U;
constexpr std::uint32_t kSequenceBytes = 0x80U;

constexpr openrc::RacMobyAnimationCompileLimitsV1 kLimits{
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
  // Ordinary Moby frame addresses are relative to the whole class asset.
  write_u32(bytes, base + 0x1cU, static_cast<std::uint32_t>(base + 0x20U));

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
  std::vector<std::byte> bytes = std::vector<std::byte>(0x180U);
  openrc::RacMobyClassV1 source_class;
  openrc::RacMobyBindRigV1 rig;
  std::array<openrc::RacMobyAnimationClipProfileV1, 2U> profiles{
      openrc::RacMobyAnimationClipProfileV1{
          0U, 0U, "actors/enemy/source-sequence/000",
          openrc::ActorAnimationWrapModeV1::loop},
      openrc::RacMobyAnimationClipProfileV1{
          1U, 3U, "actors/enemy/source-sequence/003",
          openrc::ActorAnimationWrapModeV1::clamp}};

  Fixture() {
    write_sequence(bytes, kFirstSequenceOffset, 0.5F, 0.125F, 0U);
    write_sequence(bytes, kSecondSequenceOffset, 0.0F, 0.25F, 4096U);
    source_class.input_bytes = bytes.size();
    source_class.packet_table_offset = kPacketTableOffset;
    source_class.joint_count = 1U;
    source_class.sequence_count = 4U;
    source_class.scale = 1024.0F;
    source_class.sequence_offsets = {kFirstSequenceOffset, 0U, 0U,
                                     kSecondSequenceOffset};
    rig.actor_rig.joints.push_back(openrc::ActorRigJointV1{});
    rig.actor_rig.joints.front().local_bind_transform.values[1U] = -0.0F;
    rig.actor_rig.joints.front().inverse_bind_transform.values[2U] = -0.0F;
    rig.source_common_translations.push_back({1.0F, 2.0F, 3.0F});
  }

  [[nodiscard]] openrc::ActorAnimationBankV1 compile() const {
    return openrc::compile_rac_moby_animation_bank_v1(
        bytes, source_class, rig, "actors/enemy/rig", profiles, 50U, kLimits);
  }
};

template <typename Mutation>
void expect_rejected(Mutation &&mutation, const std::string &message) {
  Fixture fixture;
  std::invoke(std::forward<Mutation>(mutation), fixture);
  try {
    static_cast<void>(fixture.compile());
  } catch (const openrc::RacMobyAnimationCompileError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_compiles_class_relative_moby_clips() {
  const Fixture fixture;
  const auto first = fixture.compile();
  const auto second = fixture.compile();
  expect(first == second && first.clips.size() == 2U,
         "RAC Moby animation compilation is not deterministic");
  const auto &idle = first.clips[0U];
  const auto &other = first.clips[1U];
  expect(idle.id == 0U &&
             idle.semantic_key == "actors/enemy/source-sequence/000" &&
             idle.rig_key == "actors/enemy/rig" &&
             idle.source_updates_per_second == 50U &&
             idle.wrap_mode == openrc::ActorAnimationWrapModeV1::loop &&
             idle.frames.size() == 1U && idle.frames[0U].phase_rate == 0.5F,
         "sequence-level phase rate was not compiled from the Moby class");
  expect(other.id == 1U && other.frames.size() == 1U &&
             other.frames[0U].phase_rate == 0.25F,
         "per-frame phase rate was not compiled from the Moby class");

  auto canonical_rig = fixture.rig.actor_rig;
  canonical_rig.joints.front().local_bind_transform.values[1U] = 0.0F;
  canonical_rig.joints.front().inverse_bind_transform.values[2U] = 0.0F;
  expect(idle.rig_content_sha256 ==
             openrc::actor_rig_content_sha256_v1(canonical_rig),
         "Moby animation compiler did not canonicalize rig identity");
  const auto &joint = idle.frames[0U].joint_poses[0U];
  expect(joint.normalized_rotation_xyzw[3U] == 1.0F &&
             joint.translation == std::array<float, 3U>{1.0F, 2.0F, 3.0F} &&
             joint.local_scale == std::array<float, 3U>{1.0F, 1.0F, 0.0F} &&
             joint.terminal_scale ==
                 std::array<float, 3U>{1.0F, 1.0F, 1.0F},
         "decoded Moby joint pose was not preserved in the neutral clip");
}

void test_builds_complete_source_addressed_profiles() {
  const Fixture fixture;
  const std::array confirmed{
      openrc::RacMobyAnimationClipProfileV1{
          0U, 0U, "actors/enemy/source-sequence/000",
          openrc::ActorAnimationWrapModeV1::loop}};
  const auto profiles = openrc::make_rac_moby_complete_animation_profiles_v1(
      fixture.source_class, confirmed, "actors/enemy/source-sequence/",
      openrc::ActorAnimationWrapModeV1::clamp);
  expect(profiles.size() == 2U && profiles[0U].source_slot == 0U &&
             profiles[0U].wrap_mode ==
                 openrc::ActorAnimationWrapModeV1::loop &&
             profiles[1U].clip_id == 1U && profiles[1U].source_slot == 3U &&
             profiles[1U].semantic_key ==
                 "actors/enemy/source-sequence/003" &&
             profiles[1U].wrap_mode ==
                 openrc::ActorAnimationWrapModeV1::clamp,
         "complete Moby profile did not preserve and fill source slots");

  const auto source_only =
      openrc::make_rac_moby_complete_animation_profiles_v1(
          fixture.source_class,
          std::span<const openrc::RacMobyAnimationClipProfileV1>{},
          "source/", openrc::ActorAnimationWrapModeV1::clamp);
  expect(source_only.size() == 2U &&
             source_only[0U].semantic_key == "source/000" &&
             source_only[1U].semantic_key == "source/003",
         "source-only Moby profile is not canonical by source slot");

  try {
    static_cast<void>(openrc::make_rac_moby_complete_animation_profiles_v1(
        fixture.source_class, confirmed, "",
        openrc::ActorAnimationWrapModeV1::clamp));
  } catch (const openrc::RacMobyAnimationCompileError &) {
    return;
  }
  throw std::runtime_error(
      "complete Moby profile accepted an empty source-key prefix");
}

void test_rejects_inconsistent_sources_profiles_and_rigs() {
  expect_rejected(
      [](Fixture &fixture) { --fixture.source_class.input_bytes; },
      "Moby animation compiler accepted a mismatched source size");
  expect_rejected(
      [](Fixture &fixture) { fixture.source_class.sequence_offsets[0U] = 0U; },
      "Moby animation compiler accepted an empty mapped source slot");
  expect_rejected(
      [](Fixture &fixture) { fixture.profiles[1U].source_slot = 0U; },
      "Moby animation compiler accepted duplicate mapped source slots");
  expect_rejected(
      [](Fixture &fixture) { fixture.profiles[1U].clip_id = 2U; },
      "Moby animation compiler accepted sparse neutral clip IDs");
  expect_rejected(
      [](Fixture &fixture) {
        write_u32(fixture.bytes, kFirstSequenceOffset + 0x20U, 0x7fc00000U);
      },
      "Moby animation compiler accepted a non-finite source phase rate");
  expect_rejected(
      [](Fixture &fixture) { fixture.source_class.joint_count = 2U; },
      "Moby animation compiler accepted a mismatched source rig");
  expect_rejected(
      [](Fixture &fixture) {
        fixture.source_class.packet_table_offset = kSecondSequenceOffset + 0x10U;
      },
      "Moby animation compiler accepted a truncated final sequence boundary");
}

} // namespace

int main() {
  try {
    test_compiles_class_relative_moby_clips();
    test_builds_complete_source_addressed_profiles();
    test_rejects_inconsistent_sources_profiles_and_rigs();
    std::cout << "RAC Moby animation compile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC Moby animation compile test failure: " << error.what()
              << '\n';
    return 1;
  }
}
