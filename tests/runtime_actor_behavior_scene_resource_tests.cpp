#include "openrc/runtime_actor_behavior_scene_resource.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kLevelId = 7U;
constexpr std::uint32_t kContentApiVersion = 3U;

constexpr openrc::ActorBehaviorSceneIoLimitsV1 kLimits{
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

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_runtime_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::RuntimeActorBehaviorSceneResourceError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::PreparedContentDigestV1
digest_of(const std::string_view value) {
  return openrc::prepared_content_sha256_v1(std::as_bytes(std::span(value)));
}

[[nodiscard]] openrc::ActorBehaviorInstanceV1
make_instance(const std::uint32_t authored_id,
              const std::uint32_t target_id,
              const std::uint32_t initial_state_id,
              const std::uint32_t initial_animation_import_id,
              const std::uint32_t random_word) {
  openrc::ActorBehaviorInstanceV1 result;
  result.authored_id = authored_id;
  result.program_id = 0U;
  result.initial_state_id = initial_state_id;
  result.initial_values = {
      openrc::ActorBehaviorInitialValueV1{std::uint32_t{5U}},
      openrc::ActorBehaviorInitialValueV1{1.0F},
      openrc::ActorBehaviorInitialValueV1{
          std::array<float, 3U>{1.0F, 2.0F, 3.0F}},
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
      {0U, "state/current",
       openrc::ActorBehaviorValueTypeV1::unsigned_integer, 1U, 0U},
      {1U, "combat/health", openrc::ActorBehaviorValueTypeV1::scalar_f32,
       1U, 0U},
      {2U, "motion/vector", openrc::ActorBehaviorValueTypeV1::vector3_f32,
       1U, 0U},
      {3U, "target/entity",
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
  result.instances = {
      make_instance(154U, 197U, 1U, 0U, UINT32_C(0x12345678)),
      make_instance(197U, 154U, 5U, 1U, UINT32_C(0x87654321)),
  };
  return result;
}

[[nodiscard]] openrc::LevelPackageResourceV1 make_resource(
    const std::string_view resource_id, const std::string_view type_id,
    const std::uint32_t schema_version, std::vector<std::byte> payload) {
  openrc::LevelPackageResourceV1 result;
  result.resource_id = std::string(resource_id);
  result.type_id = std::string(type_id);
  result.schema_version = schema_version;
  result.operation = openrc::LevelPackageResourceOperationV1::upsert;
  result.payload = std::move(payload);
  result.payload_sha256 = openrc::prepared_content_sha256_v1(result.payload);
  return result;
}

[[nodiscard]] openrc::ResolvedLevelPackageV1 make_package() {
  openrc::ResolvedLevelPackageV1 result;
  result.level_id = kLevelId;
  result.content_api_version = kContentApiVersion;
  result.build_id = "test-build";
  result.resources.push_back(make_resource(
      openrc::kActorBehaviorSceneResourceIdV1,
      openrc::kActorBehaviorSceneResourceTypeIdV1,
      openrc::kActorBehaviorSceneResourceSchemaVersionV1,
      openrc::encode_actor_behavior_scene_v1(make_scene(), kLimits)));
  result.resources.push_back(
      make_resource("future/unrelated", "openrc.future-resource", 17U,
                    {std::byte{0x12U}, std::byte{0x34U}, std::byte{0x56U}}));
  return result;
}

[[nodiscard]] openrc::ResolvedLevelPackageV1 make_package_without_scene() {
  auto result = make_package();
  std::erase_if(
      result.resources, [](const openrc::LevelPackageResourceV1 &resource) {
        return resource.resource_id ==
               openrc::kActorBehaviorSceneResourceIdV1;
      });
  return result;
}

[[nodiscard]] openrc::LevelPackageResourceV1 &
find_scene_resource(openrc::ResolvedLevelPackageV1 &package) {
  for (auto &resource : package.resources) {
    if (resource.resource_id == openrc::kActorBehaviorSceneResourceIdV1) {
      return resource;
    }
  }
  throw std::runtime_error("test actor-behavior resource is missing");
}

void refresh_digest(openrc::LevelPackageResourceV1 &resource) {
  resource.payload_sha256 =
      openrc::prepared_content_sha256_v1(resource.payload);
}

void test_absence_is_compatible_and_valid_scene_is_exact() {
  const auto absent =
      openrc::game::load_optional_runtime_actor_behavior_scene_resource_v1(
          make_package_without_scene(), kContentApiVersion, kLimits);
  expect(!absent,
         "runtime actor-behavior loader rejected an older package without "
         "the optional resource");

  auto package = make_package();
  const auto loaded =
      openrc::game::load_optional_runtime_actor_behavior_scene_resource_v1(
          package, kContentApiVersion, kLimits);
  const auto expected = openrc::canonicalize_actor_behavior_scene_v1(
      make_scene(), kLimits.scene);
  expect(loaded.has_value() && *loaded == expected,
         "runtime actor-behavior loader changed the canonical scene or "
         "rejected an unrelated resource");
  expect(openrc::encode_actor_behavior_scene_v1(*loaded, kLimits) ==
             find_scene_resource(package).payload,
         "runtime actor-behavior loader did not preserve canonical payload "
         "semantics");
}

void test_api_policy_is_exact_even_when_scene_is_absent() {
  const auto absent = make_package_without_scene();
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    absent, 0U, kLimits));
      },
      "runtime actor-behavior loader accepted an implicit API policy");
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    absent, kContentApiVersion + 1U, kLimits));
      },
      "runtime actor-behavior loader accepted a mismatched API without the "
      "resource");
}

void test_present_resource_requires_exact_resolved_identity() {
  auto duplicate = make_package();
  const auto duplicate_resource = find_scene_resource(duplicate);
  duplicate.resources.push_back(duplicate_resource);
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    duplicate, kContentApiVersion, kLimits));
      },
      "runtime actor-behavior loader accepted a duplicate resource ID");

  auto wrong_type = make_package();
  find_scene_resource(wrong_type).type_id = "openrc.not-actor-behavior-scene";
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    wrong_type, kContentApiVersion, kLimits));
      },
      "runtime actor-behavior loader accepted the wrong resource type");

  auto wrong_schema = make_package();
  ++find_scene_resource(wrong_schema).schema_version;
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    wrong_schema, kContentApiVersion, kLimits));
      },
      "runtime actor-behavior loader accepted the wrong resource schema");

  auto remove = make_package();
  find_scene_resource(remove).operation =
      openrc::LevelPackageResourceOperationV1::remove;
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    remove, kContentApiVersion, kLimits));
      },
      "runtime actor-behavior loader accepted an unresolved remove");
}

void test_digest_payload_and_nested_limits_are_strict() {
  auto zero_digest = make_package();
  find_scene_resource(zero_digest).payload_sha256 = {};
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    zero_digest, kContentApiVersion, kLimits));
      },
      "runtime actor-behavior loader accepted a zero payload digest");

  auto stale_digest = make_package();
  find_scene_resource(stale_digest).payload[0U] ^= std::byte{0x01U};
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    stale_digest, kContentApiVersion, kLimits));
      },
      "runtime actor-behavior loader accepted a stale payload digest");

  const auto package = make_package();
  auto payload_limits = kLimits;
  payload_limits.max_encoded_bytes = package.resources.front().payload.size() -
                                     1U;
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    package, kContentApiVersion, payload_limits));
      },
      "runtime actor-behavior loader ignored its payload byte limit");

  auto nested_limits = kLimits;
  nested_limits.scene.max_instances = 1U;
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    package, kContentApiVersion, nested_limits));
      },
      "runtime actor-behavior loader did not forward nested scene limits");
}

void test_level_identity_and_canonical_decode_are_strict() {
  auto wrong_level = make_package();
  wrong_level.level_id = kLevelId + 1U;
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    wrong_level, kContentApiVersion, kLimits));
      },
      "runtime actor-behavior loader accepted a scene for another level");

  auto trailing_data = make_package();
  auto &resource = find_scene_resource(trailing_data);
  resource.payload.push_back(std::byte{0U});
  refresh_digest(resource);
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::
                load_optional_runtime_actor_behavior_scene_resource_v1(
                    trailing_data, kContentApiVersion, kLimits));
      },
      "runtime actor-behavior loader accepted a freshly hashed non-canonical "
      "payload");
}

} // namespace

int main() {
  try {
    test_absence_is_compatible_and_valid_scene_is_exact();
    test_api_policy_is_exact_even_when_scene_is_absent();
    test_present_resource_requires_exact_resolved_identity();
    test_digest_payload_and_nested_limits_are_strict();
    test_level_identity_and_canonical_decode_are_strict();
    std::cout << "runtime_actor_behavior_scene_resource_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime_actor_behavior_scene_resource_tests: "
              << error.what() << '\n';
    return 1;
  }
}
