#include "openrc/level_actor_behavior_scene_compile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kLevelId = 7U;

constexpr openrc::ActorBehaviorSceneIoLimitsV1 kSceneLimits{
    .max_encoded_bytes = 1U << 20U,
    .scene =
        {
            .max_programs = 16U,
            .max_fields_per_program = 32U,
            .max_total_fields = 128U,
            .max_elements_per_field = 8U,
            .max_total_field_elements = 512U,
            .max_animation_imports_per_program = 16U,
            .max_total_animation_imports = 64U,
            .max_random_imports_per_program = 16U,
            .max_total_random_imports = 64U,
            .max_random_streams = 16U,
            .max_random_state_words_per_stream = 8U,
            .max_total_random_state_words = 128U,
            .max_instances = 128U,
            .max_initial_values_per_instance = 128U,
            .max_total_initial_values = 4096U,
            .max_initial_random_words_per_instance = 32U,
            .max_total_initial_random_words = 4096U,
            .max_initial_animations_per_instance = 4U,
            .max_total_initial_animations = 512U,
            .max_semantic_key_bytes = 128U,
            .max_total_semantic_key_bytes = 32U * 1024U,
            .max_implementation_abi_version = 8U,
            .max_random_algorithm_abi_version = 8U,
            .max_states_per_program = 32U,
            .max_source_updates_per_second = 120U,
            .max_animation_channels_per_program = 4U,
            .max_absolute_initial_float = 100'000.0F,
        },
};

constexpr openrc::LevelPackageV1Limits kPackageLimits{
    2U * 1024U * 1024U, 8U, 8U, 32U, 128U, 1U * 1024U * 1024U,
    2U * 1024U * 1024U, 8U,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_compile_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::LevelActorBehaviorSceneCompileError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::PreparedContentDigestV1
digest_of(const std::string_view value) {
  return openrc::prepared_content_sha256_v1(std::as_bytes(std::span(value)));
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
generated(const std::string_view pass) {
  return {
      openrc::LevelPackageProvenanceKindV1::generated,
      std::string(pass),
      0U,
      0U,
      {},
  };
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
direct_source(const openrc::LevelPackageProvenanceKindV1 kind,
              const std::string_view locator, const std::uint64_t offset,
              const std::string_view identity) {
  return {
      kind, std::string(locator), offset, identity.size(), digest_of(identity),
  };
}

[[nodiscard]] openrc::LevelPackageResourceV1
base_resource(const std::string_view id, const std::string_view type,
              const std::string_view pass, std::vector<std::byte> payload) {
  openrc::LevelPackageResourceV1 result;
  result.resource_id = std::string(id);
  result.type_id = std::string(type);
  result.schema_version = 1U;
  result.operation = openrc::LevelPackageResourceOperationV1::upsert;
  result.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
  result.provenance.push_back(generated(pass));
  result.payload = std::move(payload);
  return result;
}

[[nodiscard]] openrc::LevelPackageV1 make_base() {
  openrc::LevelPackageV1 result;
  result.level_id = kLevelId;
  result.content_api_version = 3U;
  result.build_id = "SCES-50916-PAL-v2.00";
  result.layer_kind = openrc::LevelPackageLayerKindV1::base;
  result.layer_id = "base";
  result.priority = 0;

  // Deliberately reversed. The attach pass must preserve the canonical bytes
  // of both existing resources rather than inheriting caller order.
  result.resources.push_back(base_resource(
      "world/entities", "openrc.entity-scene", "compiler/test/entities-v1",
      {std::byte{0x04U}, std::byte{0x05U}, std::byte{0x06U}}));
  result.resources.push_back(base_resource(
      "actors/animations", "openrc.actor-animation-bank",
      "compiler/test/animations-v1",
      {std::byte{0x01U}, std::byte{0x02U}}));
  return result;
}

[[nodiscard]] openrc::ActorBehaviorInstanceV1
make_instance(const std::uint32_t authored_id,
              const std::uint32_t target_id,
              const std::uint32_t initial_state_id,
              const std::uint32_t initial_animation_import_id,
              const bool enabled, const std::uint32_t random_word) {
  openrc::ActorBehaviorInstanceV1 result;
  result.authored_id = authored_id;
  result.program_id = 0U;
  result.initial_state_id = initial_state_id;
  result.initial_values = {
      openrc::ActorBehaviorInitialValueV1{enabled},
      openrc::ActorBehaviorInitialValueV1{std::int32_t{-4}},
      openrc::ActorBehaviorInitialValueV1{std::int32_t{6}},
      openrc::ActorBehaviorInitialValueV1{std::uint32_t{5U}},
      openrc::ActorBehaviorInitialValueV1{-0.0F},
      openrc::ActorBehaviorInitialValueV1{
          std::array<float, 3U>{1.0F, -0.0F, 3.0F}},
      openrc::ActorBehaviorInitialValueV1{
          openrc::ActorBehaviorEntityReferenceV1{target_id}},
  };
  result.initial_random_words = {random_word};
  result.initial_animations = {
      {0U, initial_animation_import_id, 0U, 0U},
  };
  return result;
}

[[nodiscard]] openrc::ActorBehaviorSceneV1 make_scene() {
  openrc::ActorBehaviorProgramV1 program;
  program.id = 0U;
  program.semantic_key = "actor/veldin/test-enemy";
  program.implementation_key = "openrc.behavior/test-enemy-v1";
  program.required_rig_key = "actor/test-enemy";
  program.required_rig_sha256 = digest_of("synthetic-rig");
  program.required_model_key = "actor/test-enemy/model";
  program.required_model_sha256 = digest_of("synthetic-model");
  program.implementation_abi_version = 1U;
  program.state_count = 13U;
  program.source_updates_per_second = 50U;
  program.animation_channel_count = 1U;
  program.fields = {
      {0U, "state/enabled", openrc::ActorBehaviorValueTypeV1::boolean, 1U,
       0U},
      {1U, "state/counters",
       openrc::ActorBehaviorValueTypeV1::signed_integer, 2U, 0U},
      {2U, "state/current",
       openrc::ActorBehaviorValueTypeV1::unsigned_integer, 1U, 0U},
      {3U, "combat/health", openrc::ActorBehaviorValueTypeV1::scalar_f32,
       1U, 0U},
      {4U, "motion/vector", openrc::ActorBehaviorValueTypeV1::vector3_f32,
       1U, 0U},
      {5U, "target/entity",
       openrc::ActorBehaviorValueTypeV1::entity_reference, 1U, 0U},
  };
  openrc::ActorBehaviorAnimationImportV1 idle_animation;
  idle_animation.id = 0U;
  idle_animation.binding_key = "animation/idle";
  idle_animation.clip_key = "moby/749/slot-4";
  idle_animation.required_clip_sha256 = digest_of("synthetic-idle-clip");
  idle_animation.required_frame_count = 10U;
  openrc::ActorBehaviorAnimationImportV1 death_animation;
  death_animation.id = 1U;
  death_animation.binding_key = "animation/death";
  death_animation.clip_key = "moby/749/slot-6";
  death_animation.required_clip_sha256 = digest_of("synthetic-death-clip");
  death_animation.required_frame_count = 20U;
  program.animation_imports = {std::move(idle_animation),
                               std::move(death_animation)};
  program.random_imports = {
      {0U, "random/death-animation", "instance/death-animation",
       "openrc.rng/lcg32", 1U,
       openrc::ActorBehaviorRandomScopeV1::instance, 1U, 0U},
      {1U, "random/reward-count", "level/veldin", "openrc.rng/lcg32", 1U,
       openrc::ActorBehaviorRandomScopeV1::level_shared, 1U, 0U},
  };

  openrc::ActorBehaviorRandomStreamV1 stream;
  stream.id = 0U;
  stream.semantic_key = "level/veldin";
  stream.algorithm_key = "openrc.rng/lcg32";
  stream.algorithm_abi_version = 1U;
  stream.scope = openrc::ActorBehaviorRandomScopeV1::level_shared;
  stream.initial_state_words = {1234U};

  openrc::ActorBehaviorSceneV1 result;
  result.level_id = kLevelId;
  result.programs = {std::move(program)};
  result.random_streams = {std::move(stream)};
  // Reversed to prove that scene canonicalization is part of attachment.
  result.instances = {
      make_instance(197U, 154U, 5U, 1U, false, UINT32_C(0x87654321)),
      make_instance(154U, 197U, 1U, 0U, true, UINT32_C(0x12345678)),
  };
  return result;
}

[[nodiscard]] std::array<openrc::LevelPackageProvenanceV1, 2U>
make_sources() {
  // Deliberately reversed relative to canonical provenance-kind order.
  return {
      direct_source(openrc::LevelPackageProvenanceKindV1::prepared_resource,
                    "rac1/level/007/entity-scene", 0U,
                    "decoded-actor-behavior-input"),
      direct_source(openrc::LevelPackageProvenanceKindV1::iso_range,
                    "disc/level/007/moby-pvars", 0x9d20U,
                    "complete-moby-pvar-source-range"),
  };
}

[[nodiscard]] const openrc::LevelPackageResourceV1 &
find_resource(const openrc::LevelPackageV1 &package,
              const std::string_view id) {
  const auto found =
      std::find_if(package.resources.begin(), package.resources.end(),
                   [id](const openrc::LevelPackageResourceV1 &resource) {
                     return resource.resource_id == id;
                   });
  if (found == package.resources.end()) {
    throw std::runtime_error("test package is missing resource " +
                             std::string(id));
  }
  return *found;
}

[[nodiscard]] bool
provenance_equal(const openrc::LevelPackageProvenanceV1 &left,
                 const openrc::LevelPackageProvenanceV1 &right) {
  return left.kind == right.kind &&
         left.source_locator == right.source_locator &&
         left.source_offset == right.source_offset &&
         left.source_bytes == right.source_bytes &&
         left.source_sha256 == right.source_sha256;
}

void test_success_is_deterministic_and_preserves_base() {
  expect(openrc::kLevelActorBehaviorSceneCompilePassV1 ==
             std::string_view("compiler/openrc/actor-behavior-scene-v1"),
         "actor-behavior compiler pass identity changed");

  const auto sources = make_sources();
  const auto first =
      openrc::attach_actor_behavior_scene_to_level_package_v1(
          make_base(), make_scene(), sources, kSceneLimits, kPackageLimits);
  const auto second =
      openrc::attach_actor_behavior_scene_to_level_package_v1(
          make_base(), make_scene(), sources, kSceneLimits, kPackageLimits);
  const auto first_bytes =
      openrc::encode_level_package_v1(first, kPackageLimits);
  const auto second_bytes =
      openrc::encode_level_package_v1(second, kPackageLimits);

  expect(first_bytes == second_bytes,
         "actor-behavior attachment is not byte deterministic");
  expect(first.resources.size() == 3U &&
             first.resources[0U].resource_id == "actors/animations" &&
             first.resources[1U].resource_id == "world/actor-behaviors" &&
             first.resources[2U].resource_id == "world/entities",
         "actor-behavior attachment returned non-canonical resource order");

  const auto &resource =
      find_resource(first, openrc::kActorBehaviorSceneResourceIdV1);
  expect(
      resource.type_id == openrc::kActorBehaviorSceneResourceTypeIdV1 &&
          resource.schema_version ==
              openrc::kActorBehaviorSceneResourceSchemaVersionV1 &&
          resource.operation ==
              openrc::LevelPackageResourceOperationV1::upsert &&
          resource.flags == openrc::kLevelPackageResourceOverlayReplaceableV1 &&
          (resource.flags & openrc::kLevelPackageResourceOverlayRemovableV1) ==
              0U &&
          resource.payload_sha256 ==
              openrc::prepared_content_sha256_v1(resource.payload),
      "actor-behavior resource identity, flags, or digest are wrong");

  expect(resource.provenance.size() == 3U &&
             provenance_equal(resource.provenance[0U], sources[1U]) &&
             provenance_equal(resource.provenance[1U], sources[0U]),
         "actor-behavior direct provenance is not exact and canonical");
  const auto &pass = resource.provenance[2U];
  expect(pass.kind == openrc::LevelPackageProvenanceKindV1::generated &&
             pass.source_locator ==
                 openrc::kLevelActorBehaviorSceneCompilePassV1 &&
             pass.source_offset == 0U && pass.source_bytes == 0U &&
             openrc::is_zero_prepared_digest_v1(pass.source_sha256),
         "actor-behavior generated provenance is not exact");

  const auto decoded =
      openrc::decode_actor_behavior_scene_v1(resource.payload, kSceneLimits);
  const auto expected = openrc::canonicalize_actor_behavior_scene_v1(
      make_scene(), kSceneLimits.scene);
  expect(decoded == expected && decoded.level_id == first.level_id &&
             decoded.instances.front().authored_id == 154U &&
             !std::signbit(std::get<float>(
                 decoded.instances.front().initial_values[4U])),
         "attached ActorBehaviorSceneV1 changed neutral scene semantics");

  auto stripped = first;
  std::erase_if(
      stripped.resources, [](const openrc::LevelPackageResourceV1 &candidate) {
        return candidate.resource_id ==
               openrc::kActorBehaviorSceneResourceIdV1;
      });
  expect(openrc::encode_level_package_v1(stripped, kPackageLimits) ==
             openrc::encode_level_package_v1(make_base(), kPackageLimits),
         "actor-behavior attachment rewrote an existing resource");
}

void test_rejects_duplicate_resource_and_bad_provenance() {
  const auto sources = make_sources();

  auto duplicate = make_base();
  duplicate.resources.push_back(base_resource(
      openrc::kActorBehaviorSceneResourceIdV1,
      openrc::kActorBehaviorSceneResourceTypeIdV1,
      "compiler/test/existing-actor-behavior-scene", {std::byte{0x01U}}));
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                duplicate, make_scene(), sources, kSceneLimits,
                kPackageLimits));
      },
      "attachment accepted an existing world/actor-behaviors resource");

  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), make_scene(), {}, kSceneLimits,
                kPackageLimits));
      },
      "attachment accepted empty source provenance");

  auto generated_source = sources;
  generated_source[0U].kind =
      openrc::LevelPackageProvenanceKindV1::generated;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), make_scene(), generated_source, kSceneLimits,
                kPackageLimits));
      },
      "attachment accepted generated source provenance");

  const std::array repeated{sources[0U], sources[0U]};
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), make_scene(), repeated, kSceneLimits,
                kPackageLimits));
      },
      "attachment accepted duplicate source provenance");

  auto incomplete = sources;
  incomplete[0U].source_sha256 = {};
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), make_scene(), incomplete, kSceneLimits,
                kPackageLimits));
      },
      "attachment accepted source provenance without a digest");

  auto overflowing = sources;
  overflowing[0U].source_offset =
      std::numeric_limits<std::uint64_t>::max();
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), make_scene(), overflowing, kSceneLimits,
                kPackageLimits));
      },
      "attachment accepted overflowing source provenance");
}

void test_rejects_wrong_package_or_scene_identity() {
  const auto sources = make_sources();

  auto overlay = make_base();
  overlay.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                overlay, make_scene(), sources, kSceneLimits,
                kPackageLimits));
      },
      "attachment accepted an overlay package");

  auto wrong_level = make_scene();
  wrong_level.level_id = kLevelId + 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), wrong_level, sources, kSceneLimits,
                kPackageLimits));
      },
      "attachment accepted an actor-behavior scene for another level");

  auto invalid_base = make_base();
  invalid_base.resources[0U].operation =
      openrc::LevelPackageResourceOperationV1::remove;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                invalid_base, make_scene(), sources, kSceneLimits,
                kPackageLimits));
      },
      "attachment accepted an invalid base package");
}

void test_rejects_tight_resource_payload_and_scene_limits() {
  const auto sources = make_sources();
  const auto scene_payload =
      openrc::encode_actor_behavior_scene_v1(make_scene(), kSceneLimits);
  constexpr std::uint64_t kBasePayloadBytes = 5U;

  auto package_limits = kPackageLimits;
  package_limits.max_resources = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), make_scene(), sources, kSceneLimits,
                package_limits));
      },
      "attachment ignored the package resource limit");

  package_limits = kPackageLimits;
  package_limits.max_provenance_per_resource = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), make_scene(), sources, kSceneLimits,
                package_limits));
      },
      "attachment ignored the per-resource provenance limit");

  package_limits = kPackageLimits;
  package_limits.max_payload_bytes = scene_payload.size() - 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), make_scene(), sources, kSceneLimits,
                package_limits));
      },
      "attachment ignored the per-resource payload limit");

  package_limits = kPackageLimits;
  package_limits.max_total_payload_bytes =
      kBasePayloadBytes + scene_payload.size() - 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), make_scene(), sources, kSceneLimits,
                package_limits));
      },
      "attachment ignored the aggregate payload limit");

  auto scene_limits = kSceneLimits;
  scene_limits.max_encoded_bytes = scene_payload.size() - 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), make_scene(), sources, scene_limits,
                kPackageLimits));
      },
      "attachment ignored the scene encoded-byte limit");

  scene_limits = kSceneLimits;
  scene_limits.scene.max_instances = 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_behavior_scene_to_level_package_v1(
                make_base(), make_scene(), sources, scene_limits,
                kPackageLimits));
      },
      "attachment ignored a nested actor-behavior scene limit");
}

} // namespace

int main() {
  try {
    test_success_is_deterministic_and_preserves_base();
    test_rejects_duplicate_resource_and_bad_provenance();
    test_rejects_wrong_package_or_scene_identity();
    test_rejects_tight_resource_payload_and_scene_limits();
    std::cout << "level_actor_behavior_scene_compile_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "level_actor_behavior_scene_compile_tests: " << error.what()
              << '\n';
    return 1;
  }
}
