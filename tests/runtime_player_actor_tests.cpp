#include "openrc/runtime_player_actor.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr openrc::ActorLibraryLimitsV1 kActorLimits{
    4U,
    4U,
    64U,
    512U,
    8U,
    16U,
    8U,
    8U,
    16U,
    8U,
    8U,
    16U,
    128U,
    384U,
    64U,
    64U,
    4096U,
    16'384U,
};

constexpr openrc::EntitySceneLimitsV1 kEntityLimits{
    8U,
    8U,
    8U,
    8U,
    4U,
    64U,
    64U,
    512U,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_resolve_error(Callback &&callback, const std::string_view needle,
                          const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::RuntimePlayerActorError &error) {
    if (std::string_view(error.what()).find(needle) != std::string_view::npos) {
      return;
    }
    throw std::runtime_error(message + ": wrong diagnostic: " + error.what());
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::ActorAffineTransformV1
actor_transform(const float translation_x = 0.0F) {
  openrc::ActorAffineTransformV1 result;
  result.values[3U] = translation_x;
  result.values[7U] = 2.0F;
  result.values[11U] = 3.0F;
  return result;
}

[[nodiscard]] openrc::ActorSkinBindingV1 rigid_skin() {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 1U;
  result.joint_indices[0U] = 0U;
  result.weight_numerators[0U] = 255U;
  result.weight_sum = 255U;
  return result;
}

[[nodiscard]] openrc::ActorSkinnedVertexV1 vertex(const float x,
                                                   const float y) {
  openrc::ActorSkinnedVertexV1 result;
  result.x = x;
  result.y = y;
  result.skin = rigid_skin();
  return result;
}

[[nodiscard]] openrc::ActorRigAssetV1
make_rig(const std::uint32_t id, const std::string_view semantic_key) {
  openrc::ActorRigAssetV1 result;
  result.id = id;
  result.semantic_key = std::string(semantic_key);
  const openrc::ActorAffineTransformV1 identity;
  result.rig.joints.push_back({-1, identity, identity});
  return result;
}

[[nodiscard]] openrc::ActorModelV1
make_model(const std::uint32_t id, const std::string_view semantic_key,
           const std::string_view rig_key) {
  openrc::RenderSceneMaterialV1 material;
  material.id = 0U;

  openrc::ActorSkinnedMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      vertex(-1.0F, -1.0F),
      vertex(1.0F, -1.0F),
      vertex(0.0F, 1.0F),
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};

  openrc::ActorModelV1 result;
  result.id = id;
  result.semantic_key = std::string(semantic_key);
  result.rig_key = std::string(rig_key);
  result.materials.push_back(material);
  result.meshes.push_back(std::move(mesh));
  return result;
}

[[nodiscard]] openrc::ActorLibraryV1 make_actor_library() {
  openrc::ActorLibraryV1 result;
  result.rigs.push_back(make_rig(0U, "actors/test/primary-rig"));
  result.rigs.push_back(make_rig(1U, "actors/test/secondary-rig"));
  result.models.push_back(make_model(0U, "actors/test/primary-model",
                                     "actors/test/primary-rig"));
  result.models.push_back(make_model(1U, "actors/test/secondary-model",
                                     "actors/test/secondary-rig"));
  return openrc::canonicalize_actor_library_v1(std::move(result),
                                                kActorLimits);
}

[[nodiscard]] openrc::EntitySceneV1 make_entity_scene() {
  openrc::EntityDefinitionV1 primary;
  primary.authored_id = 500U;
  primary.archetype_key = "openrc.player/primary";

  openrc::EntityDefinitionV1 secondary;
  secondary.authored_id = 42U;
  secondary.archetype_key = "openrc.player/secondary";

  openrc::EntityActorBindingV1 primary_actor;
  primary_actor.authored_id = primary.authored_id;
  primary_actor.model_key = "actors/test/primary-model";
  primary_actor.model_to_entity = actor_transform(4.0F);

  openrc::EntityActorBindingV1 secondary_actor;
  secondary_actor.authored_id = secondary.authored_id;
  secondary_actor.model_key = "actors/test/secondary-model";
  secondary_actor.model_to_entity = actor_transform(9.0F);

  openrc::EntitySceneV1 result;
  result.definitions = {primary, secondary};
  result.actor_bindings = {primary_actor, secondary_actor};
  result.player_bindings = {
      {primary.authored_id, 0U},
      {secondary.authored_id, 3U},
  };
  return openrc::canonicalize_entity_scene_v1(std::move(result),
                                               kEntityLimits);
}

[[nodiscard]] openrc::game::RuntimeLevelContentV1 make_content() {
  openrc::game::RuntimeLevelContentV1 result;
  result.actor_library = make_actor_library();
  result.entity_scene = make_entity_scene();
  return result;
}

template <typename Scene>
[[nodiscard]] decltype(auto) secondary_actor_binding(Scene &scene) {
  const auto found = std::find_if(
      scene.actor_bindings.begin(), scene.actor_bindings.end(),
      [](const openrc::EntityActorBindingV1 &binding) {
        return binding.authored_id == 42U;
      });
  if (found == scene.actor_bindings.end()) {
    throw std::runtime_error("secondary actor test fixture is missing");
  }
  return *found;
}

void test_resolves_semantic_chain_to_stable_value_handles() {
  const auto content = make_content();
  const auto first =
      openrc::game::resolve_runtime_player_actor_v1(content, 3U);
  const auto second =
      openrc::game::resolve_runtime_player_actor_v1(content, 3U);

  expect(first.has_value() && first == second &&
             first->player_authored_id == 42U &&
             first->actor_rig_index == 1U &&
             first->actor_model_index == 1U &&
             first->model_to_entity == actor_transform(9.0F),
         "runtime player actor did not resolve to stable value handles");
  expect(content.actor_library->models[first->actor_model_index].semantic_key ==
                 secondary_actor_binding(*content.entity_scene).model_key &&
             content.actor_library->rigs[first->actor_rig_index].semantic_key ==
                 content.actor_library->models[first->actor_model_index]
                     .rig_key,
         "runtime player actor bypassed the semantic actor/rig chain");

  const auto primary =
      openrc::game::resolve_runtime_player_actor_v1(content, 0U);
  expect(primary.has_value() && primary->player_authored_id == 500U &&
             primary->actor_rig_index == 0U &&
             primary->actor_model_index == 0U,
         "runtime player actor resolver hardcoded one player or model");
}

void test_legacy_absence_is_the_only_empty_result() {
  const openrc::game::RuntimeLevelContentV1 legacy;
  expect(!openrc::game::resolve_runtime_player_actor_v1(legacy, 0U) &&
             !openrc::game::resolve_runtime_player_actor_v1(legacy, 99U),
         "legacy actor/entity absence did not return nullopt");

  auto missing_library = make_content();
  missing_library.actor_library.reset();
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            missing_library, 0U));
      },
      "incomplete", "partial actor/entity content returned nullopt");

  auto missing_scene = make_content();
  missing_scene.entity_scene.reset();
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            missing_scene, 0U));
      },
      "incomplete", "partial actor/entity content returned nullopt");

  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            make_content(), 99U));
      },
      "player slot", "a missing player slot returned nullopt");
}

void test_rejects_ambiguous_player_and_actor_bindings() {
  auto ambiguous_player = make_content();
  ambiguous_player.entity_scene->player_bindings[1U].local_player_slot = 3U;
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            ambiguous_player, 3U));
      },
      "ambiguously binds", "resolver accepted an ambiguous player slot");

  auto missing_actor = make_content();
  std::erase_if(missing_actor.entity_scene->actor_bindings,
                [](const openrc::EntityActorBindingV1 &binding) {
                  return binding.authored_id == 42U;
                });
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            missing_actor, 3U));
      },
      "no actor binding", "resolver accepted a missing player actor binding");

  auto ambiguous_actor = make_content();
  const auto repeated_actor =
      secondary_actor_binding(*ambiguous_actor.entity_scene);
  ambiguous_actor.entity_scene->actor_bindings.push_back(repeated_actor);
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            ambiguous_actor, 3U));
      },
      "multiple actor bindings",
      "resolver accepted ambiguous player actor bindings");
}

void test_rejects_missing_or_ambiguous_model_and_rig_keys() {
  auto missing_model = make_content();
  secondary_actor_binding(*missing_model.entity_scene).model_key =
      "actors/test/missing-model";
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            missing_model, 3U));
      },
      "missing actor model", "resolver accepted a missing actor model key");

  auto ambiguous_model = make_content();
  auto repeated_model = ambiguous_model.actor_library->models[1U];
  repeated_model.id = 2U;
  ambiguous_model.actor_library->models.push_back(std::move(repeated_model));
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            ambiguous_model, 3U));
      },
      "multiple actor models",
      "resolver accepted an ambiguous actor model key");

  auto missing_rig = make_content();
  missing_rig.actor_library->models[1U].rig_key = "actors/test/missing-rig";
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            missing_rig, 3U));
      },
      "missing actor rig", "resolver accepted a missing actor rig key");

  auto ambiguous_rig = make_content();
  auto repeated_rig = ambiguous_rig.actor_library->rigs[1U];
  repeated_rig.id = 2U;
  ambiguous_rig.actor_library->rigs.push_back(std::move(repeated_rig));
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            ambiguous_rig, 3U));
      },
      "multiple actor rigs", "resolver accepted an ambiguous actor rig key");
}

void test_rejects_noncanonical_dense_actor_ids() {
  auto bad_model_id = make_content();
  bad_model_id.actor_library->models[1U].id = 7U;
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            bad_model_id, 3U));
      },
      "non-canonical model index",
      "resolver exposed a non-canonical model ID as an index");

  auto bad_rig_id = make_content();
  bad_rig_id.actor_library->rigs[1U].id = 7U;
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            bad_rig_id, 3U));
      },
      "non-canonical rig index",
      "resolver exposed a non-canonical rig ID as an index");
}

void test_rejects_disabled_player_definition() {
  auto disabled = make_content();
  const auto definition = std::find_if(
      disabled.entity_scene->definitions.begin(),
      disabled.entity_scene->definitions.end(),
      [](const openrc::EntityDefinitionV1 &candidate) {
        return candidate.authored_id == 42U;
      });
  expect(definition != disabled.entity_scene->definitions.end(),
         "resolver fixture is missing its secondary player definition");
  definition->flags = 0U;

  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_player_actor_v1(
            disabled, 3U));
      },
      "disabled", "resolver accepted a disabled player definition");
}

} // namespace

int main() {
  try {
    test_resolves_semantic_chain_to_stable_value_handles();
    test_legacy_absence_is_the_only_empty_result();
    test_rejects_ambiguous_player_and_actor_bindings();
    test_rejects_missing_or_ambiguous_model_and_rig_keys();
    test_rejects_noncanonical_dense_actor_ids();
    test_rejects_disabled_player_definition();
    std::cout << "runtime player-actor tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime player-actor tests failed: " << error.what() << '\n';
    return 1;
  }
}
