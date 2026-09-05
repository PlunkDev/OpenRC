#include "openrc/actor_behavior_scene.hpp"
#include "openrc/actor_behavior_scene_io.hpp"

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
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kFormatVersionOffset = 0x08U;
constexpr std::size_t kHeaderBytesOffset = 0x0cU;
constexpr std::size_t kTotalBytesOffset = 0x10U;
constexpr std::size_t kPayloadTypeOffset = 0x18U;
constexpr std::size_t kSchemaVersionOffset = 0x1cU;
constexpr std::size_t kLevelIdOffset = 0x20U;
constexpr std::size_t kHeaderFlagsOffset = 0x24U;
constexpr std::size_t kProgramCountOffset = 0x28U;
constexpr std::size_t kFieldCountOffset = 0x2cU;
constexpr std::size_t kAnimationCountOffset = 0x30U;
constexpr std::size_t kRandomImportCountOffset = 0x34U;
constexpr std::size_t kRandomStreamCountOffset = 0x38U;
constexpr std::size_t kInstanceCountOffset = 0x3cU;
constexpr std::size_t kValueCountOffset = 0x40U;
constexpr std::size_t kRandomWordCountOffset = 0x44U;
constexpr std::size_t kInitialAnimationCountOffset = 0x48U;
constexpr std::size_t kStringBytesOffset = 0x50U;
constexpr std::size_t kProgramRecordBytesOffset = 0x58U;
constexpr std::size_t kProgramTableOffset = 0x80U;
constexpr std::size_t kFieldTableOffset = 0x88U;
constexpr std::size_t kAnimationTableOffset = 0x90U;
constexpr std::size_t kRandomImportTableOffset = 0x98U;
constexpr std::size_t kRandomStreamTableOffset = 0xa0U;
constexpr std::size_t kInstanceTableOffset = 0xa8U;
constexpr std::size_t kValueTableOffset = 0xb0U;
constexpr std::size_t kRandomWordTableOffset = 0xb8U;
constexpr std::size_t kInitialAnimationTableOffset = 0xc0U;
constexpr std::size_t kStringDataOffset = 0xc8U;
constexpr std::size_t kHeaderReservedOffset = 0xd0U;

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

template <typename Function>
void expect_scene_error(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const openrc::ActorBehaviorSceneError &) {
    return;
  }
  fail(message);
}

template <typename Function>
void expect_io_error(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const openrc::ActorBehaviorSceneIoError &) {
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

[[nodiscard]] openrc::ActorBehaviorSceneIoLimitsV1 make_limits() {
  openrc::ActorBehaviorSceneIoLimitsV1 result;
  result.max_encoded_bytes = 1024U * 1024U;
  result.scene.max_programs = 16U;
  result.scene.max_fields_per_program = 64U;
  result.scene.max_total_fields = 256U;
  result.scene.max_elements_per_field = 16U;
  result.scene.max_total_field_elements = 1024U;
  result.scene.max_animation_imports_per_program = 64U;
  result.scene.max_total_animation_imports = 256U;
  result.scene.max_random_imports_per_program = 16U;
  result.scene.max_total_random_imports = 64U;
  result.scene.max_random_streams = 16U;
  result.scene.max_random_state_words_per_stream = 16U;
  result.scene.max_total_random_state_words = 256U;
  result.scene.max_instances = 256U;
  result.scene.max_initial_values_per_instance = 256U;
  result.scene.max_total_initial_values = 65'536U;
  result.scene.max_initial_random_words_per_instance = 64U;
  result.scene.max_total_initial_random_words = 16'384U;
  result.scene.max_initial_animations_per_instance = 8U;
  result.scene.max_total_initial_animations = 2048U;
  result.scene.max_semantic_key_bytes = 128U;
  result.scene.max_total_semantic_key_bytes = 65'536U;
  result.scene.max_implementation_abi_version = 16U;
  result.scene.max_random_algorithm_abi_version = 16U;
  result.scene.max_states_per_program = 64U;
  result.scene.max_source_updates_per_second = 60U;
  result.scene.max_animation_channels_per_program = 8U;
  result.scene.max_absolute_initial_float = 100'000.0F;
  return result;
}

const auto kLimits = make_limits();

[[nodiscard]] openrc::PreparedContentDigestV1 fixture_rig_digest() {
  openrc::PreparedContentDigestV1 result{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = static_cast<std::byte>(index + 1U);
  }
  return result;
}

[[nodiscard]] std::vector<openrc::ActorBehaviorInitialValueV1>
fixture_values(const std::uint32_t base) {
  // Matches the intentionally non-canonical field order in make_scene:
  // vector[1], unsigned[0] x2, entity[2], scalar[3], bool[4], signed[5].
  return {
      std::array{1.0F + static_cast<float>(base), -0.0F, 3.0F},
      base + 10U,
      base + 20U,
      openrc::ActorBehaviorEntityReferenceV1{},
      -0.0F,
      true,
      static_cast<std::int32_t>(-4 - static_cast<std::int32_t>(base)),
  };
}

[[nodiscard]] openrc::ActorBehaviorSceneV1 make_scene() {
  openrc::ActorBehaviorProgramV1 program;
  program.id = 0U;
  program.semantic_key = "actors/veldin/toothy/variant-a";
  program.implementation_key = "openrc.behavior.rac1.toothy";
  program.required_rig_key = "actors/rac1/moby/0749/rig";
  program.required_rig_sha256 = fixture_rig_digest();
  program.required_model_key = "actors/rac1/moby/0749/model";
  program.required_model_sha256 = fixture_rig_digest();
  program.implementation_abi_version = 3U;
  program.state_count = 13U;
  program.source_updates_per_second = 50U;
  program.animation_channel_count = 2U;
  program.fields = {
      {1U, "movement/target", openrc::ActorBehaviorValueTypeV1::vector3_f32, 1U,
       0U},
      {0U, "timers/value", openrc::ActorBehaviorValueTypeV1::unsigned_integer,
       2U, 0U},
      {2U, "links/target", openrc::ActorBehaviorValueTypeV1::entity_reference,
       1U, 0U},
      {3U, "motion/speed", openrc::ActorBehaviorValueTypeV1::scalar_f32, 1U,
       0U},
      {4U, "status/active", openrc::ActorBehaviorValueTypeV1::boolean, 1U, 0U},
      {5U, "status/counter", openrc::ActorBehaviorValueTypeV1::signed_integer,
       1U, 0U},
  };
  program.animation_imports = {
      {1U, "animation/move", "actors/rac1/moby/0749/sequence/004",
       fixture_rig_digest(), 8U, 0U},
      {0U, "animation/entry", "actors/rac1/moby/0749/sequence/001",
       fixture_rig_digest(), 6U, 0U},
  };
  program.random_imports = {
      {1U, "random/local-secondary", "random/toothy-local-secondary",
       "openrc.random.lcg32", 1U, openrc::ActorBehaviorRandomScopeV1::instance,
       1U, 0U},
      {2U, "random/level", "random/rac1-level", "openrc.random.lcg32", 1U,
       openrc::ActorBehaviorRandomScopeV1::level_shared, 2U, 0U},
      {0U, "random/local-primary", "random/toothy-local-primary",
       "openrc.random.lcg32", 1U, openrc::ActorBehaviorRandomScopeV1::instance,
       2U, 0U},
  };

  openrc::ActorBehaviorRandomStreamV1 shared;
  shared.id = 0U;
  shared.semantic_key = "random/rac1-level";
  shared.algorithm_key = "openrc.random.lcg32";
  shared.algorithm_abi_version = 1U;
  shared.scope = openrc::ActorBehaviorRandomScopeV1::level_shared;
  shared.initial_state_words = {1234U, UINT32_C(0xfeedbeef)};

  openrc::ActorBehaviorInstanceV1 late;
  late.authored_id = 200U;
  late.program_id = 0U;
  late.initial_state_id = 1U;
  late.initial_values = fixture_values(100U);
  late.initial_random_words = {202U, 110U, 111U};
  late.initial_animations = {{1U, 1U, 3U, 0U}, {0U, 0U, 0U, 0U}};

  openrc::ActorBehaviorInstanceV1 early;
  early.authored_id = 100U;
  early.program_id = 0U;
  early.initial_state_id = 5U;
  early.initial_values = fixture_values(0U);
  early.initial_random_words = {102U, 10U, 11U};
  early.initial_animations = {{1U, 1U, 4U, 0U}, {0U, 0U, 0U, 0U}};

  openrc::ActorBehaviorSceneV1 result;
  result.level_id = UINT32_C(0x12345678);
  result.programs = {std::move(program)};
  result.random_streams = {std::move(shared)};
  result.instances = {std::move(late), std::move(early)};
  return result;
}

[[nodiscard]] openrc::ActorBehaviorSceneV1 canonical_scene() {
  return openrc::canonicalize_actor_behavior_scene_v1(make_scene(),
                                                      kLimits.scene);
}

void refresh_layout_digest(openrc::ActorBehaviorSceneV1 &scene) {
  for (auto &program : scene.programs) {
    program.state_layout_sha256 =
        openrc::actor_behavior_program_layout_sha256_v1(program);
  }
}

void test_identity_round_trip_empty_and_canonicalization() {
  expect(openrc::kActorBehaviorSceneResourceIdV1 ==
                 std::string_view("world/actor-behaviors") &&
             openrc::kActorBehaviorSceneResourceTypeIdV1 ==
                 std::string_view("openrc.actor-behavior-scene") &&
             openrc::kActorBehaviorSceneResourceSchemaVersionV1 == 1U,
         "ActorBehaviorSceneV1 resource identity is wrong");

  const auto expected = canonical_scene();
  expect(expected.instances[0U].authored_id == 100U &&
             expected.instances[1U].authored_id == 200U &&
             expected.programs[0U].fields[0U].id == 0U &&
             expected.programs[0U].fields[1U].id == 1U &&
             expected.programs[0U].animation_imports[0U].id == 0U &&
             expected.programs[0U].random_imports[0U].id == 0U,
         "ActorBehaviorSceneV1 canonicalizer did not sort ID-bearing tables");
  expect(std::get<std::uint32_t>(expected.instances[0U].initial_values[0U]) ==
                 10U &&
             std::get<std::uint32_t>(
                 expected.instances[0U].initial_values[1U]) == 20U &&
             std::get<std::array<float, 3U>>(
                 expected.instances[0U].initial_values[2U])[0U] == 1.0F &&
             expected.instances[0U].initial_random_words ==
                 std::vector<std::uint32_t>({10U, 11U, 102U}) &&
             expected.instances[0U].initial_state_id == 5U &&
             expected.instances[1U].initial_state_id == 1U &&
             expected.instances[0U].initial_animations[0U].channel_id == 0U &&
             expected.instances[0U].initial_animations[1U].channel_id == 1U,
         "ActorBehaviorSceneV1 canonicalizer detached positional state from "
         "its field or random-import IDs");
  const auto &vector = std::get<std::array<float, 3U>>(
      expected.instances[0U].initial_values[2U]);
  expect(vector[1U] == 0.0F && !std::signbit(vector[1U]) &&
             std::get<float>(expected.instances[0U].initial_values[4U]) ==
                 0.0F &&
             !std::signbit(
                 std::get<float>(expected.instances[0U].initial_values[4U])),
         "ActorBehaviorSceneV1 canonicalizer retained floating signed zero");
  expect(!openrc::is_zero_prepared_digest_v1(
             expected.programs[0U].state_layout_sha256),
         "ActorBehaviorSceneV1 canonicalizer did not derive a layout digest");

  const auto bytes =
      openrc::encode_actor_behavior_scene_v1(make_scene(), kLimits);
  const auto decoded = openrc::decode_actor_behavior_scene_v1(bytes, kLimits);
  expect(decoded == expected,
         "ActorBehaviorSceneV1 round trip changed logical content");
  expect(openrc::encode_actor_behavior_scene_v1(decoded, kLimits) == bytes,
         "ActorBehaviorSceneV1 re-encoding is not byte deterministic");

  expect(read_u32(bytes, kFormatVersionOffset) == 1U &&
             read_u32(bytes, kHeaderBytesOffset) == 0x100U &&
             read_u64(bytes, kTotalBytesOffset) == bytes.size() &&
             read_u32(bytes, kPayloadTypeOffset) == 1U &&
             read_u32(bytes, kSchemaVersionOffset) == 1U &&
             read_u32(bytes, kLevelIdOffset) == UINT32_C(0x12345678) &&
             read_u32(bytes, kProgramCountOffset) == 1U &&
             read_u32(bytes, kFieldCountOffset) == 6U &&
             read_u32(bytes, kAnimationCountOffset) == 2U &&
             read_u32(bytes, kRandomImportCountOffset) == 3U &&
             read_u32(bytes, kRandomStreamCountOffset) == 1U &&
             read_u32(bytes, kInstanceCountOffset) == 2U &&
             read_u32(bytes, kValueCountOffset) == 14U &&
             read_u32(bytes, kRandomWordCountOffset) == 8U &&
             read_u32(bytes, kInitialAnimationCountOffset) == 4U,
         "ActorBehaviorSceneV1 header counts or identity are wrong");
  expect(read_u64(bytes, kProgramTableOffset) == 0x100U &&
             read_u64(bytes, kFieldTableOffset) == 0x1d0U &&
             read_u64(bytes, kAnimationTableOffset) == 0x290U &&
             read_u64(bytes, kRandomImportTableOffset) == 0x330U &&
             read_u64(bytes, kRandomStreamTableOffset) == 0x3f0U &&
             read_u64(bytes, kInstanceTableOffset) == 0x430U &&
             read_u64(bytes, kValueTableOffset) == 0x490U &&
             read_u64(bytes, kRandomWordTableOffset) == 0x5e0U &&
             read_u64(bytes, kInitialAnimationTableOffset) == 0x600U &&
             read_u64(bytes, kStringDataOffset) == 0x640U,
         "ActorBehaviorSceneV1 canonical table layout changed unexpectedly");
  expect(byte_value(bytes[kLevelIdOffset]) == 0x78U &&
             byte_value(bytes[kLevelIdOffset + 3U]) == 0x12U,
         "ActorBehaviorSceneV1 integers are not little-endian");
  const auto program_record =
      static_cast<std::size_t>(read_u64(bytes, kProgramTableOffset));
  const auto animation_record =
      static_cast<std::size_t>(read_u64(bytes, kAnimationTableOffset));
  expect(read_u32(bytes, program_record + 0x0cU) == 50U &&
             read_u32(bytes, animation_record + 0x14U) == 6U,
         "ActorBehaviorSceneV1 source cadence or exact frame count was not "
         "encoded at its specified offset");

  const openrc::ActorBehaviorSceneV1 empty;
  const auto empty_bytes =
      openrc::encode_actor_behavior_scene_v1(empty, kLimits);
  expect(empty_bytes.size() == openrc::kActorBehaviorSceneIoHeaderBytesV1 &&
             openrc::decode_actor_behavior_scene_v1(empty_bytes, kLimits) ==
                 empty,
         "An empty ActorBehaviorSceneV1 did not round-trip canonically");
}

void test_layout_digest_scope_and_stale_rejection() {
  const auto scene = canonical_scene();
  const auto &program = scene.programs[0U];
  const auto digest = openrc::actor_behavior_program_layout_sha256_v1(program);
  expect(digest == program.state_layout_sha256,
         "ActorBehaviorSceneV1 layout digest is not current");

  auto identity_only = program;
  identity_only.id = 9U;
  identity_only.semantic_key = "actors/another-variant";
  identity_only.implementation_key = "openrc.behavior.other";
  identity_only.state_layout_sha256 = {};
  expect(openrc::actor_behavior_program_layout_sha256_v1(identity_only) ==
             digest,
         "ActorBehaviorSceneV1 layout digest included registry/package "
         "identity");

  auto changed = program;
  changed.state_count += 1U;
  expect(openrc::actor_behavior_program_layout_sha256_v1(changed) != digest,
         "ActorBehaviorSceneV1 digest ignored state-machine shape");
  changed = program;
  changed.source_updates_per_second = 60U;
  expect(openrc::actor_behavior_program_layout_sha256_v1(changed) != digest,
         "ActorBehaviorSceneV1 digest ignored source update cadence");
  changed = program;
  changed.fields[0U].element_count += 1U;
  expect(openrc::actor_behavior_program_layout_sha256_v1(changed) != digest,
         "ActorBehaviorSceneV1 digest ignored field shape");
  changed = program;
  changed.animation_imports[0U].clip_key += "-other";
  expect(openrc::actor_behavior_program_layout_sha256_v1(changed) != digest,
         "ActorBehaviorSceneV1 digest ignored an animation contract");
  changed = program;
  changed.animation_imports[0U].required_clip_sha256[0U] ^= std::byte{1U};
  expect(openrc::actor_behavior_program_layout_sha256_v1(changed) != digest,
         "ActorBehaviorSceneV1 digest ignored an exact clip pin");
  changed = program;
  changed.animation_imports[0U].required_frame_count += 1U;
  expect(openrc::actor_behavior_program_layout_sha256_v1(changed) != digest,
         "ActorBehaviorSceneV1 digest ignored the exact frame range");
  changed = program;
  changed.random_imports[0U].state_word_count += 1U;
  expect(openrc::actor_behavior_program_layout_sha256_v1(changed) != digest,
         "ActorBehaviorSceneV1 digest ignored a random contract");
  changed = program;
  changed.required_rig_sha256[0U] ^= std::byte{1U};
  expect(openrc::actor_behavior_program_layout_sha256_v1(changed) != digest,
         "ActorBehaviorSceneV1 digest ignored the required rig contract");
  changed = program;
  changed.required_model_sha256[0U] ^= std::byte{1U};
  expect(openrc::actor_behavior_program_layout_sha256_v1(changed) != digest,
         "ActorBehaviorSceneV1 digest ignored the required model contract");

  auto data_only = scene;
  data_only.random_streams[0U].initial_state_words[0U] += 1U;
  data_only.instances[0U].initial_random_words[0U] += 1U;
  data_only.instances[0U].initial_values[0U] = UINT32_C(999);
  data_only.instances[0U].initial_state_id = 2U;
  data_only.instances[0U].initial_animations[0U].first_frame_index += 1U;
  expect(openrc::actor_behavior_program_layout_sha256_v1(
             data_only.programs[0U]) == digest,
         "ActorBehaviorSceneV1 digest included initial runtime data");

  auto stale = scene;
  stale.programs[0U].state_layout_sha256[0U] ^= std::byte{1U};
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(stale, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a stale layout digest");
  expect_scene_error(
      [&] {
        static_cast<void>(
            openrc::canonicalize_actor_behavior_scene_v1(stale, kLimits.scene));
      },
      "ActorBehaviorSceneV1 canonicalizer replaced a stale supplied digest");
}

void test_dense_order_typed_state_and_rng_validation() {
  auto scene = canonical_scene();
  scene.programs[0U].id = 1U;
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a sparse program ID");

  scene = canonical_scene();
  scene.programs[0U].fields[1U].id = 9U;
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a sparse field ID");

  scene = canonical_scene();
  std::swap(scene.instances[0U], scene.instances[1U]);
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted non-canonical instance order");

  scene = canonical_scene();
  scene.instances[0U].initial_values[0U] = 1.0F;
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted an initial value with the wrong type");

  scene = canonical_scene();
  scene.instances[0U].initial_values.pop_back();
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted incomplete initial field state");

  scene = canonical_scene();
  scene.instances[0U].initial_random_words.pop_back();
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted incomplete instance RNG state");

  scene = canonical_scene();
  scene.instances[0U].initial_state_id = scene.programs[0U].state_count;
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted an out-of-range per-instance state");

  scene = canonical_scene();
  std::swap(scene.instances[0U].initial_animations[0U],
            scene.instances[0U].initial_animations[1U]);
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted non-canonical initial animations");

  scene = canonical_scene();
  scene.instances[0U].initial_animations[0U].animation_import_id = 99U;
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a missing initial animation import");

  scene = canonical_scene();
  scene.random_streams[0U].algorithm_key = "openrc.random.other";
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a mismatched shared RNG algorithm");

  scene = canonical_scene();
  scene.random_streams[0U].scope = openrc::ActorBehaviorRandomScopeV1::instance;
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted an instance-scoped scene stream");

  scene = canonical_scene();
  scene.random_streams.clear();
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted an unresolved shared RNG import");

  scene = canonical_scene();
  openrc::ActorBehaviorRandomStreamV1 orphan;
  orphan.id = 1U;
  orphan.semantic_key = "random/orphan";
  orphan.algorithm_key = "openrc.random.lcg32";
  orphan.algorithm_abi_version = 1U;
  orphan.scope = openrc::ActorBehaviorRandomScopeV1::level_shared;
  orphan.initial_state_words = {1U};
  scene.random_streams.push_back(std::move(orphan));
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted an orphan shared RNG stream");

  scene = canonical_scene();
  scene.programs[0U].random_imports[1U].stream_key =
      scene.programs[0U].random_imports[0U].stream_key;
  scene.programs[0U].random_imports[1U].state_word_count = 1U;
  expect_scene_error(
      [&] { refresh_layout_digest(scene); },
      "ActorBehaviorSceneV1 hashed duplicate instance RNG stream keys");
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted duplicate instance RNG stream keys");

  scene = canonical_scene();
  scene.instances[0U].initial_values[4U] =
      std::numeric_limits<float>::infinity();
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a non-finite initial float");

  scene = canonical_scene();
  scene.programs[0U].fields[2U].flags =
      openrc::kActorBehaviorEntityReferenceRequiresTransformV1 |
      openrc::kActorBehaviorEntityReferenceRequiresActorV1 |
      openrc::kActorBehaviorEntityReferenceRequiresBehaviorV1;
  refresh_layout_digest(scene);
  openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene);

  scene = canonical_scene();
  scene.programs[0U].fields[0U].flags =
      openrc::kActorBehaviorEntityReferenceRequiresActorV1;
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted entity requirements on a scalar field");

  scene = canonical_scene();
  scene.programs[0U].fields[2U].flags = 1U << 8U;
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted unknown entity-reference flags");

  scene = canonical_scene();
  scene.programs[0U].required_model_sha256 = {};
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a missing model digest");

  scene = canonical_scene();
  scene.programs[0U].animation_imports[0U].required_clip_sha256 = {};
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a missing clip digest");

  scene = canonical_scene();
  scene.programs[0U].semantic_key = "Actors/Uppercase";
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a non-canonical semantic key");
}

void test_exact_frame_range_and_source_cadence() {
  auto scene = canonical_scene();
  auto &initial = scene.instances[0U].initial_animations[0U];
  const auto frame_count = scene.programs[0U]
                               .animation_imports[initial.animation_import_id]
                               .required_frame_count;
  initial.first_frame_index = frame_count - 1U;
  openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene);
  expect(openrc::decode_actor_behavior_scene_v1(
             openrc::encode_actor_behavior_scene_v1(scene, kLimits), kLimits) ==
             scene,
         "ActorBehaviorSceneV1 rejected the final valid initial frame");
  initial.first_frame_index = frame_count;
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted the first out-of-range initial frame");

  scene = canonical_scene();
  scene.programs[0U].animation_imports[0U].required_frame_count = 0U;
  expect_scene_error(
      [&] { refresh_layout_digest(scene); },
      "ActorBehaviorSceneV1 hashed a zero frame count");
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a zero frame count");

  scene = canonical_scene();
  scene.programs[0U].animation_imports[0U].required_frame_count += 1U;
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a stale layout after a frame-range "
      "change");

  scene = canonical_scene();
  scene.programs[0U].source_updates_per_second = 0U;
  expect_scene_error(
      [&] { refresh_layout_digest(scene); },
      "ActorBehaviorSceneV1 hashed zero source cadence");
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted zero source cadence");

  scene = canonical_scene();
  scene.programs[0U].source_updates_per_second = 60U;
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 accepted a stale layout after a cadence change");
  refresh_layout_digest(scene);
  openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene);
  expect(openrc::decode_actor_behavior_scene_v1(
             openrc::encode_actor_behavior_scene_v1(scene, kLimits), kLimits) ==
             scene,
         "ActorBehaviorSceneV1 did not preserve a 60 Hz source contract");

  scene.programs[0U].source_updates_per_second = 61U;
  refresh_layout_digest(scene);
  expect_scene_error(
      [&] { openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene); },
      "ActorBehaviorSceneV1 ignored the source-cadence caller limit");
}

void test_instance_rng_identity_is_program_local_and_shared_imports_alias() {
  auto scene = canonical_scene();
  auto alias = scene.programs[0U].random_imports[2U];
  alias.id = 3U;
  alias.binding_key = "random/level-alias";
  scene.programs[0U].random_imports.push_back(std::move(alias));
  refresh_layout_digest(scene);
  openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene);

  auto second_program = scene.programs[0U];
  second_program.id = 1U;
  second_program.semantic_key = "actors/veldin/toothy/variant-b";
  second_program.random_imports[0U].algorithm_key = "openrc.random.other";
  second_program.random_imports[0U].state_word_count = 3U;
  scene.programs.push_back(std::move(second_program));
  refresh_layout_digest(scene);
  openrc::validate_actor_behavior_scene_v1(scene, kLimits.scene);
  expect(openrc::decode_actor_behavior_scene_v1(
             openrc::encode_actor_behavior_scene_v1(scene, kLimits), kLimits) ==
             scene,
         "ActorBehaviorSceneV1 confused program-local RNG identities or "
         "shared aliases");
}

template <typename Mutation>
void expect_corrupt_decode(Mutation &&mutation, const std::string &message) {
  auto bytes = openrc::encode_actor_behavior_scene_v1(make_scene(), kLimits);
  std::forward<Mutation>(mutation)(bytes);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_actor_behavior_scene_v1(bytes, kLimits));
      },
      message);
}

void test_decoder_rejects_corrupt_layout_reserved_and_trailing_data() {
  expect_corrupt_decode([](auto &bytes) { bytes[0U] ^= std::byte{1U}; },
                        "ActorBehaviorSceneV1 decoder accepted bad magic");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFormatVersionOffset, 2U); },
      "ActorBehaviorSceneV1 decoder accepted a future format version");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kHeaderBytesOffset, 0xdcU); },
      "ActorBehaviorSceneV1 decoder accepted the wrong header size");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kPayloadTypeOffset, 2U); },
      "ActorBehaviorSceneV1 decoder accepted an unknown payload type");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kSchemaVersionOffset, 2U); },
      "ActorBehaviorSceneV1 decoder accepted an unknown schema version");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kHeaderFlagsOffset, 1U); },
      "ActorBehaviorSceneV1 decoder accepted unknown header flags");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kProgramRecordBytesOffset, 156U); },
      "ActorBehaviorSceneV1 decoder accepted a wrong record width");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kHeaderReservedOffset] = std::byte{1U}; },
      "ActorBehaviorSceneV1 decoder accepted header reserved data");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, kProgramTableOffset, 0U); },
      "ActorBehaviorSceneV1 decoder accepted a bad program-table offset");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, kStringDataOffset, 0U); },
      "ActorBehaviorSceneV1 decoder accepted a bad string-table offset");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto program =
            static_cast<std::size_t>(read_u64(bytes, kProgramTableOffset));
        bytes[program + 0x24U] = std::byte{1U};
      },
      "ActorBehaviorSceneV1 decoder accepted program reserved data");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto program =
            static_cast<std::size_t>(read_u64(bytes, kProgramTableOffset));
        write_u32(bytes, program + 0x0cU, 0U);
      },
      "ActorBehaviorSceneV1 decoder accepted zero source cadence");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto program =
            static_cast<std::size_t>(read_u64(bytes, kProgramTableOffset));
        write_u32(bytes, program + 0x0cU, 60U);
      },
      "ActorBehaviorSceneV1 decoder accepted cadence inconsistent with its "
      "layout digest");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto program =
            static_cast<std::size_t>(read_u64(bytes, kProgramTableOffset));
        write_u32(bytes, program + 0x0cU, 61U);
      },
      "ActorBehaviorSceneV1 decoder ignored the source-cadence limit");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto animation =
            static_cast<std::size_t>(read_u64(bytes, kAnimationTableOffset));
        write_u32(bytes, animation + 0x14U, 0U);
      },
      "ActorBehaviorSceneV1 decoder accepted zero imported frame count");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto animation =
            static_cast<std::size_t>(read_u64(bytes, kAnimationTableOffset));
        write_u32(bytes, animation + 0x14U, 7U);
      },
      "ActorBehaviorSceneV1 decoder accepted frame count inconsistent with "
      "its layout digest");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto animation =
            static_cast<std::size_t>(read_u64(bytes, kAnimationTableOffset));
        bytes[animation + 0x28U] = std::byte{1U};
      },
      "ActorBehaviorSceneV1 decoder accepted animation reserved data");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto animation =
            static_cast<std::size_t>(read_u64(bytes, kAnimationTableOffset));
        bytes[animation + 0x30U] ^= std::byte{1U};
      },
      "ActorBehaviorSceneV1 decoder accepted a corrupted clip digest pin");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto program =
            static_cast<std::size_t>(read_u64(bytes, kProgramTableOffset));
        bytes[program + 0xb0U] ^= std::byte{1U};
      },
      "ActorBehaviorSceneV1 decoder accepted a corrupted model digest pin");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto stream =
            static_cast<std::size_t>(read_u64(bytes, kRandomStreamTableOffset));
        bytes[stream + 0x30U] = std::byte{1U};
      },
      "ActorBehaviorSceneV1 decoder accepted stream reserved data");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto instance =
            static_cast<std::size_t>(read_u64(bytes, kInstanceTableOffset));
        bytes[instance + 0x28U] = std::byte{1U};
      },
      "ActorBehaviorSceneV1 decoder accepted instance reserved data");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto animation = static_cast<std::size_t>(
            read_u64(bytes, kInitialAnimationTableOffset));
        write_u32(bytes, animation + 0x0cU, 1U);
      },
      "ActorBehaviorSceneV1 decoder accepted initial-animation flags");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto animation = static_cast<std::size_t>(
            read_u64(bytes, kInitialAnimationTableOffset));
        write_u32(bytes, animation + 0x08U, 6U);
      },
      "ActorBehaviorSceneV1 decoder accepted an out-of-range initial frame");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto value =
            static_cast<std::size_t>(read_u64(bytes, kValueTableOffset));
        write_u32(bytes, value, 99U);
      },
      "ActorBehaviorSceneV1 decoder accepted an unknown value type");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto value =
            static_cast<std::size_t>(read_u64(bytes, kValueTableOffset));
        bytes[value + 12U] = std::byte{1U};
      },
      "ActorBehaviorSceneV1 decoder accepted non-canonical value padding");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto program =
            static_cast<std::size_t>(read_u64(bytes, kProgramTableOffset));
        write_u32(bytes, program + 0x80U, 1U);
      },
      "ActorBehaviorSceneV1 decoder accepted a broken field partition");
  expect_corrupt_decode(
      [](auto &bytes) {
        const auto program =
            static_cast<std::size_t>(read_u64(bytes, kProgramTableOffset));
        const auto key_offset = read_u64(bytes, program + 0x28U);
        write_u64(bytes, program + 0x28U, key_offset + 1U);
      },
      "ActorBehaviorSceneV1 decoder accepted a broken string partition");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u64(bytes, kTotalBytesOffset, bytes.size() - 1U);
      },
      "ActorBehaviorSceneV1 decoder accepted a wrong exact byte size");
  expect_corrupt_decode(
      [](auto &bytes) { bytes.pop_back(); },
      "ActorBehaviorSceneV1 decoder accepted truncated string data");
  expect_corrupt_decode([](auto &bytes) { bytes.push_back(std::byte{0U}); },
                        "ActorBehaviorSceneV1 decoder accepted trailing data");
}

void test_limits_fail_closed_before_allocation() {
  const auto bytes =
      openrc::encode_actor_behavior_scene_v1(make_scene(), kLimits);

  auto limited = kLimits;
  limited.max_encoded_bytes = bytes.size() - 1U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_actor_behavior_scene_v1(make_scene(), limited));
      },
      "ActorBehaviorSceneV1 encoder ignored its byte limit");
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_actor_behavior_scene_v1(bytes, limited));
      },
      "ActorBehaviorSceneV1 decoder ignored its byte limit");

  limited = kLimits;
  limited.scene.max_instances = 1U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_actor_behavior_scene_v1(bytes, limited));
      },
      "ActorBehaviorSceneV1 decoder ignored its instance-count limit");

  limited = kLimits;
  limited.scene.max_semantic_key_bytes = 8U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_actor_behavior_scene_v1(make_scene(), limited));
      },
      "ActorBehaviorSceneV1 encoder ignored its per-key limit");

  limited = kLimits;
  limited.scene.max_total_initial_values = 13U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_actor_behavior_scene_v1(bytes, limited));
      },
      "ActorBehaviorSceneV1 decoder ignored aggregate initial-value limits");

  limited = kLimits;
  limited.scene.max_total_initial_animations = 3U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_actor_behavior_scene_v1(bytes, limited));
      },
      "ActorBehaviorSceneV1 decoder ignored initial-animation limits");

  limited = kLimits;
  limited.scene.max_source_updates_per_second = 0U;
  expect_scene_error(
      [&] {
        openrc::validate_actor_behavior_scene_v1(
            openrc::ActorBehaviorSceneV1{}, limited.scene);
      },
      "ActorBehaviorSceneV1 accepted an absent source-cadence limit");

  // A supplied stale digest would be rejected while processing the first
  // program. Resource bounds must win before that processing copies tables
  // or allocates canonical instance values.
  const auto expect_preflight_limit = [](const auto &scene_limits,
                                         const std::string_view expected) {
    auto scene = make_scene();
    scene.programs[0U].state_layout_sha256 = fixture_rig_digest();
    try {
      static_cast<void>(openrc::canonicalize_actor_behavior_scene_v1(
          std::move(scene), scene_limits));
    } catch (const openrc::ActorBehaviorSceneError &error) {
      expect(std::string_view(error.what()).find(expected) !=
                 std::string_view::npos,
             "ActorBehaviorSceneV1 allocated canonical tables before "
             "checking the full resource limits");
      return;
    }
    fail("ActorBehaviorSceneV1 canonicalizer ignored a resource bound");
  };
  limited = kLimits;
  limited.scene.max_fields_per_program = 1U;
  expect_preflight_limit(limited.scene, "per-program field count");
  limited = kLimits;
  limited.scene.max_animation_imports_per_program = 1U;
  expect_preflight_limit(limited.scene, "per-program animation import count");
  limited = kLimits;
  limited.scene.max_random_imports_per_program = 1U;
  expect_preflight_limit(limited.scene, "per-program random import count");
  limited = kLimits;
  limited.scene.max_total_fields = 1U;
  expect_preflight_limit(limited.scene, "aggregate fields");
  limited = kLimits;
  limited.scene.max_total_initial_values = 1U;
  expect_preflight_limit(limited.scene, "aggregate initial values");
  limited = kLimits;
  limited.scene.max_total_initial_random_words = 1U;
  expect_preflight_limit(limited.scene, "aggregate instance random words");
  limited = kLimits;
  limited.scene.max_total_initial_animations = 1U;
  expect_preflight_limit(limited.scene, "aggregate initial animations");
  limited = kLimits;
  limited.scene.max_total_semantic_key_bytes = 1U;
  expect_preflight_limit(limited.scene, "aggregate semantic-key limit");

  expect_io_error(
      [&] {
        static_cast<void>(openrc::decode_actor_behavior_scene_v1(
            bytes, openrc::ActorBehaviorSceneIoLimitsV1{}));
      },
      "ActorBehaviorSceneV1 decoder accepted absent caller limits");

  auto excessive = bytes;
  write_u32(excessive, kProgramCountOffset, kLimits.scene.max_programs + 1U);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_actor_behavior_scene_v1(excessive, kLimits));
      },
      "ActorBehaviorSceneV1 decoder allocated from an excessive header "
      "program count");
  excessive = bytes;
  write_u64(excessive, kStringBytesOffset,
            kLimits.scene.max_total_semantic_key_bytes + 1U);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_actor_behavior_scene_v1(excessive, kLimits));
      },
      "ActorBehaviorSceneV1 decoder allocated from excessive header strings");
}

} // namespace

int main() {
  try {
    test_identity_round_trip_empty_and_canonicalization();
    test_layout_digest_scope_and_stale_rejection();
    test_dense_order_typed_state_and_rng_validation();
    test_exact_frame_range_and_source_cadence();
    test_instance_rng_identity_is_program_local_and_shared_imports_alias();
    test_decoder_rejects_corrupt_layout_reserved_and_trailing_data();
    test_limits_fail_closed_before_allocation();
    std::cout << "ActorBehaviorSceneV1 tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ActorBehaviorSceneV1 tests failed: " << error.what() << '\n';
    return 1;
  }
}
