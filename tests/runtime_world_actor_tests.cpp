#include "openrc/runtime_world_actor.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr openrc::ActorLibraryLimitsV1 kActorLimits{
    4U, 4U, 64U, 512U, 8U, 16U, 8U, 8U, 16U,
    8U, 8U, 16U, 128U, 384U, 64U, 64U, 4096U, 16'384U};
constexpr openrc::EntitySceneLimitsV1 kEntityLimits{
    8U, 8U, 8U, 8U, 4U, 64U, 64U, 512U};

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
  } catch (const openrc::game::RuntimeWorldActorError &error) {
    if (std::string_view(error.what()).find(needle) != std::string_view::npos) {
      return;
    }
    throw std::runtime_error(message + ": wrong diagnostic: " + error.what());
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::ActorSkinBindingV1 rigid_skin() {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 1U;
  result.joint_indices[0U] = 0U;
  result.weight_numerators[0U] = 1U;
  result.weight_sum = 1U;
  return result;
}

[[nodiscard]] openrc::ActorRigAssetV1
make_rig(const std::uint32_t id, const std::string_view key) {
  openrc::ActorRigAssetV1 result;
  result.id = id;
  result.semantic_key = std::string(key);
  result.rig.joints.push_back(openrc::ActorRigJointV1{});
  return result;
}

[[nodiscard]] openrc::ActorModelV1
make_model(const std::uint32_t id, const std::string_view key,
           const std::string_view rig_key) {
  openrc::ActorSkinnedVertexV1 vertex;
  vertex.skin = rigid_skin();
  openrc::ActorSkinnedMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {vertex, vertex, vertex};
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};
  openrc::ActorModelV1 result;
  result.id = id;
  result.semantic_key = std::string(key);
  result.rig_key = std::string(rig_key);
  result.materials.push_back(openrc::RenderSceneMaterialV1{});
  result.meshes.push_back(std::move(mesh));
  return result;
}

[[nodiscard]] openrc::ActorAffineTransformV1
model_transform(const float x) {
  openrc::ActorAffineTransformV1 result;
  result.values[3U] = x;
  return result;
}

[[nodiscard]] openrc::game::WorldTransformV1
world_transform(const float x) {
  openrc::game::WorldTransformV1 result;
  result.position = {x, x + 1.0F, x + 2.0F};
  result.scale = {2.0F, 2.0F, 2.0F};
  return result;
}

[[nodiscard]] openrc::game::RuntimeLevelContentV1 make_content(
    const std::string_view enemy_rig_key = "actors/enemy/rig") {
  openrc::ActorLibraryV1 library;
  library.rigs.push_back(make_rig(0U, "actors/player/rig"));
  library.rigs.push_back(make_rig(1U, enemy_rig_key));
  library.models.push_back(
      make_model(0U, "actors/player/high", "actors/player/rig"));
  library.models.push_back(
      make_model(1U, "actors/enemy/high", enemy_rig_key));
  library = openrc::canonicalize_actor_library_v1(std::move(library),
                                                   kActorLimits);

  openrc::EntitySceneV1 scene;
  scene.level_id = 7U;
  scene.definitions = {
      {0U, "openrc.player/default"},
      {10U, "openrc.enemy/test"},
      {20U, "openrc.enemy/test", 0U},
  };
  scene.transforms = {
      {10U, world_transform(10.0F)},
      {20U, world_transform(20.0F)},
  };
  scene.actor_bindings = {
      {0U, "actors/player/high", model_transform(0.0F)},
      {10U, "actors/enemy/high", model_transform(1.0F)},
      {20U, "actors/enemy/high", model_transform(2.0F)},
  };
  scene.player_bindings = {{0U, 0U}};
  scene = openrc::canonicalize_entity_scene_v1(std::move(scene),
                                                kEntityLimits);

  openrc::game::RuntimeLevelContentV1 result;
  result.actor_library = std::move(library);
  result.entity_scene = std::move(scene);
  return result;
}

void test_resolves_non_player_actors_in_authored_order() {
  const auto content = make_content();
  const auto actors =
      openrc::game::resolve_runtime_world_actors_v1(content);
  expect(actors.size() == 2U && actors[0U].authored_id == 10U &&
             actors[1U].authored_id == 20U &&
             actors[0U].actor_rig_index == 1U &&
             actors[0U].actor_model_index == 1U &&
             actors[0U].model_to_entity == model_transform(1.0F) &&
             actors[1U].model_to_entity == model_transform(2.0F) &&
             actors[0U].entity_to_world == world_transform(10.0F) &&
             actors[1U].entity_to_world == world_transform(20.0F) &&
             actors[0U].initially_enabled && !actors[1U].initially_enabled,
         "world actor resolver lost neutral identity, transforms, or flags");
}

void test_legacy_and_partial_feature_pairs() {
  const openrc::game::RuntimeLevelContentV1 legacy;
  expect(openrc::game::resolve_runtime_world_actors_v1(legacy).empty(),
         "legacy actor/entity absence did not return an empty actor list");

  auto missing_library = make_content();
  missing_library.actor_library.reset();
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_world_actors_v1(
            missing_library));
      },
      "incomplete", "partial actor/entity content was accepted");
}

void test_rejects_broken_world_relationships() {
  auto missing_transform = make_content();
  missing_transform.entity_scene->transforms.erase(
      missing_transform.entity_scene->transforms.begin());
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_world_actors_v1(
            missing_transform));
      },
      "no world transform", "world actor without a transform was accepted");

  auto missing_model = make_content();
  missing_model.entity_scene->actor_bindings[1U].model_key =
      "actors/missing/high";
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_world_actors_v1(
            missing_model));
      },
      "missing actor model", "missing world actor model was accepted");

  auto bad_rig_id = make_content();
  bad_rig_id.actor_library->rigs[1U].id = 9U;
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_world_actors_v1(
            bad_rig_id));
      },
      "non-canonical rig", "non-canonical world actor rig was accepted");

  auto unordered = make_content();
  std::swap(unordered.entity_scene->actor_bindings[1U],
            unordered.entity_scene->actor_bindings[2U]);
  expect_resolve_error(
      [&] {
        static_cast<void>(openrc::game::resolve_runtime_world_actors_v1(
            unordered));
      },
      "canonical authored-ID order",
      "unordered world actor bindings were accepted");
}

void test_initial_animation_is_neutral_and_optional() {
  constexpr std::string_view kOpaqueRigKey = "neutral.enemy.skeleton";
  auto content = make_content(kOpaqueRigKey);
  const auto actors = openrc::game::resolve_runtime_world_actors_v1(content);
  expect(actors.size() == 2U,
         "initial-animation fixture did not resolve its world actors");
  expect(openrc::game::find_runtime_world_actor_initial_animation_v1(
             content, actors.front()) == nullptr,
         "an absent animation bank did not preserve bind pose");

  openrc::ActorAnimationClipV1 unrelated;
  unrelated.semantic_key = "animations/idle/rest";
  unrelated.rig_key = std::string(kOpaqueRigKey);
  openrc::ActorAnimationClipV1 initial;
  initial.semantic_key = "animations/initial/source-pose";
  initial.rig_key = std::string(kOpaqueRigKey);
  openrc::ActorAnimationBankV1 animations;
  animations.clips = {unrelated, initial};
  content.actor_animation_bank = animations;

  const auto* const resolved =
      openrc::game::find_runtime_world_actor_initial_animation_v1(
          content, actors.front());
  expect(resolved != nullptr && resolved->semantic_key == initial.semantic_key,
         "neutral initial-animation classification was not resolved by rig");

  auto duplicate = initial;
  duplicate.semantic_key = "alternate/initial/source-pose";
  content.actor_animation_bank->clips.push_back(std::move(duplicate));
  expect_resolve_error(
      [&] {
        static_cast<void>(
            openrc::game::find_runtime_world_actor_initial_animation_v1(
                content, actors.front()));
      },
      "multiple classified initial animations",
      "ambiguous initial world-actor animations were accepted");
}

void test_rejects_actor_count_before_relationship_scans() {
  auto content = make_content();
  content.entity_scene->actor_bindings.resize(
      static_cast<std::size_t>(
          openrc::game::kRuntimeWorldActorMaximumInstancesV1) +
      1U);
  expect_resolve_error(
      [&] {
        static_cast<void>(
            openrc::game::resolve_runtime_world_actors_v1(content));
      },
      "instance limit", "oversized world-actor table was accepted");
}

} // namespace

int main() {
  try {
    test_resolves_non_player_actors_in_authored_order();
    test_legacy_and_partial_feature_pairs();
    test_rejects_broken_world_relationships();
    test_initial_animation_is_neutral_and_optional();
    test_rejects_actor_count_before_relationship_scans();
    std::cout << "runtime world-actor tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime world-actor tests failed: " << error.what() << '\n';
    return 1;
  }
}
