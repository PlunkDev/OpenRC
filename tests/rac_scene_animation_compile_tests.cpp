#include "openrc/rac_scene_animation_compile.hpp"
#include "openrc/actor_animation_player.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr openrc::RacSceneAnimationCompileLimitsV1 kLimits{
    {0x1000U, 8U, 32U, 8U, 0x100U},
    {0x1000U, 0x1000U, 32U, 8U},
    {4U, 0x100U, 8U, 8U, 1.0e-8},
    {8U, 16U, 32U, 4U, 128U, 128U, 1024U, 120U, 1.0e6F, 1.0e-8}};
constexpr openrc::ActorAnimationPlaybackLimitsV1 kPlayback{
    16U, 16U, 4U, 1.0e-8, 1.0e6F};

void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void u16(std::vector<std::byte> &bytes, std::size_t at, std::uint16_t value) {
  for (unsigned i = 0U; i != 2U; ++i)
    bytes[at+i] = static_cast<std::byte>((value >> (i*8U)) & 255U);
}

void u32(std::vector<std::byte> &bytes, std::size_t at, std::uint32_t value) {
  for (unsigned i = 0U; i != 4U; ++i)
    bytes[at+i] = static_cast<std::byte>((value >> (i*8U)) & 255U);
}

void f32(std::vector<std::byte> &bytes, std::size_t at, float value) {
  u32(bytes, at, std::bit_cast<std::uint32_t>(value));
}

struct Fixture {
  // Two different scene actors deliberately share a class and rig.
  std::vector<std::byte> bytes = std::vector<std::byte>(0x280U);
  std::array<openrc::RacSceneAnimationActorBindingV1, 2U> bindings;
  Fixture() {
    u32(bytes, 8U, openrc::kSceneAnimationBankTagV1);
    u32(bytes, 12U, 2U);
    u32(bytes, 16U, 0x20U);
    for (unsigned sample = 0U; sample < 5U; ++sample) {
      f32(bytes, 0x20U+sample*32U, 10.0F+sample);
      f32(bytes, 0x30U+sample*32U, 0.25F);
      f32(bytes, 0x3cU+sample*32U, 0.625F);
    }
    for (std::uint32_t actor = 0U; actor < 2U; ++actor) {
      const auto begin = 0xc0U + actor*0xe0U;
      u32(bytes, 0x14U + actor*4U, begin);
      u32(bytes, begin, 7U);
      u32(bytes, begin+4U, 2U);
      u32(bytes, begin+8U, 1U);
      u32(bytes, begin+12U, begin+0xb0U);
      const auto sequence = begin+16U;
      bytes[sequence+0x10U] = std::byte{3U};
      bytes[sequence+0x12U] = std::byte{255U};
      bytes[sequence+0x13U] = std::byte{255U};
      for (unsigned frame = 0U; frame < 3U; ++frame) {
        f32(bytes, begin+0xb0U+frame*16U, 100.0F+actor*20U+frame*8U);
        const auto offset = 0x30U + frame*0x20U;
        u32(bytes, sequence+0x1cU+frame*4U, offset);
        const auto base = sequence+offset;
        f32(bytes, base, 0.5F);
        u16(bytes, base+6U, frame == 2U ? 2U : 1U);
        u16(bytes, base+8U, 8U);
        u16(bytes, base+12U, 8U);
        u16(bytes, base+14U, 1U);
        u16(bytes, base+0x16U, 32767U);
        u16(bytes, base+0x18U, static_cast<std::uint16_t>(actor*10U+frame*2U));
      }
      // Last frame's envelope is 0x30 bytes to meet the root boundary;
      // its extra qword contains a source-authored local-scale record.
      const auto last = sequence+0x70U;
      u16(bytes, last+0xaU, 1U);
      u16(bytes, last+0xcU, 16U);
      u16(bytes, last+0x18U, 4096U);
      u16(bytes, last+0x1aU, 4096U);
      u16(bytes, last+0x1cU, 4096U);
      u16(bytes, last+0x1eU, 0x8000U);
      u16(bytes, last+0x20U, static_cast<std::uint16_t>(actor*10U+4U));
      auto &binding = bindings[actor];
      binding.actor_index = actor;
      binding.expected_class_id = 7U;
      binding.semantic_key = "scene/actor/" + std::to_string(actor);
      binding.rig_key = "scene/rig";
      binding.class_scale = 1024.0F;
      binding.bind_rig.actor_rig.joints.push_back({});
      binding.bind_rig.source_common_translations.push_back({});
    }
  }
  openrc::ActorAnimationBankV1 compile(
      openrc::RacSceneAnimationCompileLimitsV1 limits = kLimits) const {
    return openrc::compile_rac_scene_animation_bank_v1(bytes, bindings, 50U, limits);
  }
};

template<class Mutation> void reject(Mutation mutate) {
  Fixture fixture;
  mutate(fixture);
  try { (void)fixture.compile(); }
  catch (const openrc::RacSceneAnimationCompileError &) { return; }
  throw std::runtime_error("Invalid scene animation was accepted");
}

void test_original_tick_selection_and_roots() {
  Fixture fixture;
  const auto bank = fixture.compile();
  const auto first = openrc::sample_rac_scene_animation_tick_v1(
      fixture.bytes, 0U, kLimits.scene);
  const auto half = openrc::sample_rac_scene_animation_tick_v1(
      fixture.bytes, 1U, kLimits.scene);
  expect(first.current_frame == 0U && first.next_frame == 1U &&
         first.interpolation == 0.0F && first.actor_world_positions[0U][0U] == 100.0F,
         "Even source update did not select the authored current frame");
  expect(half.current_frame == 0U && half.next_frame == 1U &&
         half.interpolation == 0.5F && half.actor_world_positions[0U][0U] == 104.0F &&
         half.actor_world_positions[1U][0U] == 124.0F,
         "Odd source update did not interpolate separate actor roots");
  expect(half.camera.position[0U] == 11.0F &&
         half.camera.rotation_xyz_radians[0U] == 0.25F &&
         half.camera.projection_parameter == 0.625F,
         "Source camera update was interpolated or read from the wrong record");
  fixture.bytes[0x2cU] = std::byte{1U};
  const auto flagged_even = openrc::sample_rac_scene_animation_tick_v1(
      fixture.bytes, 0U, kLimits.scene);
  expect(flagged_even.interpolation == 0.0F,
         "Camera byte executed the annulled BNEL slot on an even update");
  fixture.bytes[0x4cU] = std::byte{1U};
  const auto snap = openrc::sample_rac_scene_animation_tick_v1(
      fixture.bytes, 1U, kLimits.scene);
  expect(snap.interpolation == 1.0F && snap.actor_world_positions[0U][0U] == 108.0F,
         "Camera byte did not select the next actor frame at an odd update");
  auto state = openrc::rac_scene_actor_playback_state_v1(bank.clips[0U], snap);
  const auto pose = openrc::sample_actor_animation_pose_v1(
      bank.clips[0U], state, "scene/rig",
      fixture.bindings[0U].bind_rig.actor_rig, kPlayback);
  expect(state.frame_index == 1U && state.phase == 0.0 &&
         pose.global_joint_transforms[0U].values[3U] == 2.0F,
         "Exact next-frame selection was not projected into the neutral player");
  try {
    (void)openrc::sample_rac_scene_animation_tick_v1(fixture.bytes, 4U, kLimits.scene);
  } catch (const openrc::RacSceneAnimationCompileError &) { return; }
  throw std::runtime_error("Chunk guard sample with no next actor frame was accepted");
}

void test_pose_compile_and_existing_player() {
  const Fixture fixture;
  const auto bank = fixture.compile();
  expect(bank == fixture.compile(), "Scene compilation changed between runs");
  expect(bank.clips.size() == 2U, "Actors sharing a class were merged");
  expect(bank.clips[0U].frames[0U].joint_poses[0U].translation[0U] == 0.0F &&
         bank.clips[1U].frames[0U].joint_poses[0U].translation[0U] == 10.0F,
         "Distinct actor tracks lost authored translations");
  auto state = openrc::start_actor_animation_playback_v1(bank.clips[0U]);
  (void)openrc::advance_actor_animation_playback_v1(
      bank.clips[0U], 1U, kPlayback, state);
  const auto pose = openrc::sample_actor_animation_pose_v1(
      bank.clips[0U], state, "scene/rig",
      fixture.bindings[0U].bind_rig.actor_rig, kPlayback);
  expect(state.frame_index == 0U && state.phase == 0.5,
         "Compiled source phase rate does not reach the existing player");
  expect(pose.global_joint_transforms[0U].values[3U] == 1.0F,
         "Existing pose sampling did not interpolate the compiled scene clip");
}

void test_source_rig_signed_zero_projection() {
  Fixture fixture;
  const auto canonical = fixture.compile();
  for (auto &binding : fixture.bindings) {
    binding.bind_rig.actor_rig.joints[0U].local_bind_transform.values[1U] = -0.0F;
    binding.bind_rig.actor_rig.joints[0U].inverse_bind_transform.values[1U] = -0.0F;
  }
  expect(fixture.compile() == canonical,
         "Source bind signed zero changed the neutral rig pin or scene poses");
}

void test_frontend_background_source_clock_and_sampler() {
  Fixture fixture;
  fixture.bytes[0x4cU] = std::byte{1U};
  const auto sample = openrc::sample_rac_frontend_background_tick_v1(
      fixture.bytes, 1U, kLimits.scene);
  expect(sample.interpolation == 0.5F &&
         sample.actor_world_positions[0U][0U] == 104.0F &&
         !sample.camera.select_next_actor_frame_on_odd_update &&
         std::bit_cast<std::uint32_t>(sample.camera.projection_parameter) == 0x3f2147aeU,
         "Frontend background used general cutscene camera controls");
  openrc::RacFrontendBackgroundClockV1 clock(1398U);
  expect(clock.step() == openrc::RacFrontendBackgroundPositionV1{1U,0U,1U,false,false},
         "Frontend background first update skipped the source increment");
  for (unsigned tick = 2U; tick < 96U; ++tick) (void)clock.step();
  expect(clock.step() == openrc::RacFrontendBackgroundPositionV1{96U,1U,0U,true,false},
         "Frontend background did not reset the chunk counter before sampling");
  for (unsigned tick = 97U; tick < 1397U; ++tick) (void)clock.step();
  expect(clock.step() == openrc::RacFrontendBackgroundPositionV1{1397U,14U,53U,false,false},
         "Frontend background last short chunk selected the wrong update");
  expect(clock.step() == openrc::RacFrontendBackgroundPositionV1{0U,0U,0U,true,true},
         "Frontend background did not reload and sample its first chunk at loop");
}

void test_frontend_background_directory_decode() {
  Fixture fixture;
  u32(fixture.bytes, 0U, 4U);
  for (unsigned actor = 0U; actor < 2U; ++actor) {
    const auto begin = 0xc0U+actor*0xe0U;
    u32(fixture.bytes, begin+4U, 1U);
    u32(fixture.bytes, begin+8U, 0U);
  }
  // A synthetic literal-only WAD. Dummy records separate successive literal
  // runs, exercising the existing decoder without incorporating source data.
  std::vector<std::byte> wad(16U);
  wad[0U]=std::byte{'W'}; wad[1U]=std::byte{'A'}; wad[2U]=std::byte{'D'};
  for (std::size_t at = 0U; at < fixture.bytes.size();) {
    if (at != 0U) wad.insert(wad.end(), {std::byte{0x11U},std::byte{0U},std::byte{0U}});
    const auto count = std::min<std::size_t>(273U, fixture.bytes.size()-at);
    wad.push_back(std::byte{0U});
    wad.push_back(static_cast<std::byte>(count-18U));
    wad.insert(wad.end(), fixture.bytes.begin()+static_cast<std::ptrdiff_t>(at),
               fixture.bytes.begin()+static_cast<std::ptrdiff_t>(at+count));
    at += count;
  }
  u32(wad, 3U, static_cast<std::uint32_t>(wad.size()));
  constexpr std::size_t bank = 0x180U, chunk = bank+0x800U;
  std::vector<std::byte> frontend(chunk+wad.size());
  u32(frontend, 4U, 0x100U);
  u32(frontend, 0x80U, 0x80U);
  u32(frontend, bank+4U, static_cast<std::uint32_t>(wad.size()));
  std::copy(wad.begin(), wad.end(), frontend.begin()+chunk);
  const auto decoded = openrc::decode_rac_frontend_background_v1(
      frontend, kLimits.scene, fixture.bytes.size());
  expect(decoded.source_duration == 4U && decoded.decoded_chunks.size() == 1U &&
         decoded.decoded_chunks[0U] == fixture.bytes,
         "Frontend source directory lost its offsets, loop or decoded bytes");
  bool limited = false;
  try { (void)openrc::decode_rac_frontend_background_v1(
      frontend, kLimits.scene, fixture.bytes.size()-1U); }
  catch (const openrc::RacSceneAnimationCompileError &) { limited = true; }
  expect(limited, "Frontend background ignored aggregate decoded-byte limit");
  frontend.pop_back();
  try { (void)openrc::decode_rac_frontend_background_v1(
      frontend, kLimits.scene, fixture.bytes.size()); }
  catch (const openrc::RacSceneAnimationCompileError &) { return; }
  throw std::runtime_error("Frontend background accepted a truncated source extent");
}

void test_scene_dialect_keeps_source_bytes() {
  const Fixture fixture;
  const auto scene = openrc::parse_scene_animation_bank_v1(fixture.bytes, kLimits.scene);
  const auto &actor = scene.actors[0U];
  const openrc::RacRatchetSequenceRangeV1 range{
      actor.sequence_header_range.offset,
      actor.root_transform_offset-actor.sequence_header_range.offset};
  const auto sequence = openrc::parse_rac_scene_sequence_v1(
      fixture.bytes, range, kLimits.sequence);
  expect(sequence.trigger_count == 0U && sequence.trigger_words.empty(),
         "Scene sentinels were decoded as 255 triggers");
  expect(sequence.encoded_bytes == std::vector<std::byte>(
      fixture.bytes.begin()+static_cast<std::ptrdiff_t>(range.offset),
      fixture.bytes.begin()+static_cast<std::ptrdiff_t>(range.offset+range.size)),
      "Scene sequence adapter modified original bytes");
  try {
    (void)openrc::parse_rac_ratchet_sequence_v1(fixture.bytes, range, kLimits.sequence);
  } catch (const openrc::RacRatchetSequenceError &) { return; }
  throw std::runtime_error("Scene dialect leaked into ordinary sequence parsing");
}

void test_limits_and_mismatches() {
  reject([](Fixture &f) { f.bindings[1U].actor_index = 0U; });
  reject([](Fixture &f) { f.bindings[1U].expected_class_id = 8U; });
  reject([](Fixture &f) { f.bindings[1U].class_scale = 0.0F; });
  reject([](Fixture &f) { f.bytes[0xe2U] = std::byte{0U}; });
  reject([](Fixture &f) { u16(f.bytes, 0x108U, 16U); });
  const Fixture fixture;
  auto limits = kLimits;
  limits.animation.max_total_joint_poses = 5U;
  try { (void)fixture.compile(limits); }
  catch (const openrc::RacSceneAnimationCompileError &) { return; }
  throw std::runtime_error("Aggregate scene pose limit was ignored");
}

} // namespace

int main() {
  try {
    test_pose_compile_and_existing_player();
    test_source_rig_signed_zero_projection();
    test_frontend_background_source_clock_and_sampler();
    test_frontend_background_directory_decode();
    test_scene_dialect_keeps_source_bytes();
    test_original_tick_selection_and_roots();
    test_limits_and_mismatches();
    std::cout << "RAC scene animation compile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC scene animation compile test failure: " << error.what() << '\n';
    return 1;
  }
}
