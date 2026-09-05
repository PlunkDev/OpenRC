#include "openrc/rac_moby_actor_scene_compile.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kModelKey = "actors/rac1/moby/0749/high";
constexpr std::string_view kRigKey = "actors/rac1/moby/0749/rig";
constexpr std::string_view kArchetypeKey = "rac1/moby/0749";

constexpr openrc::ActorLibraryLimitsV1 kActorLimits{
    4U, 4U, 128U, 1024U, 8U, 16U, 8U, 4U, 16U,
    16U, 8U, 32U, 64U, 192U, 64U, 64U, 4096U, 16'384U};
constexpr openrc::EntitySceneLimitsV1 kEntityLimits{
    32U, 32U, 32U, 32U, 2U, 128U, 128U, 4096U};
constexpr openrc::RacMobyActorSceneCompileLimitsV1 kLimits{
    32U, kActorLimits, kEntityLimits};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] bool near(const float left, const float right) {
  return std::abs(left - right) <= 1.0e-6F;
}

[[nodiscard]] openrc::ActorLibraryV1 make_library() {
  openrc::ActorRigAssetV1 rig;
  rig.id = 0U;
  rig.semantic_key = std::string(kRigKey);
  rig.rig.joints.push_back(openrc::ActorRigJointV1{});

  openrc::ActorSkinnedVertexV1 vertex;
  vertex.skin.influence_count = 1U;
  vertex.skin.joint_indices[0U] = 0U;
  vertex.skin.weight_numerators[0U] = 1U;
  vertex.skin.weight_sum = 1U;
  openrc::ActorSkinnedMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {vertex, vertex, vertex};
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges.push_back({0U, 0U, 3U});

  openrc::ActorModelV1 model;
  model.id = 0U;
  model.semantic_key = std::string(kModelKey);
  model.rig_key = std::string(kRigKey);
  model.materials.push_back(openrc::RenderSceneMaterialV1{});
  model.meshes.push_back(std::move(mesh));

  openrc::ActorLibraryV1 library;
  library.rigs.push_back(std::move(rig));
  library.models.push_back(std::move(model));
  return openrc::canonicalize_actor_library_v1(std::move(library),
                                                kActorLimits);
}

[[nodiscard]] openrc::EntitySceneV1 make_base_scene() {
  openrc::EntitySceneV1 scene;
  scene.level_id = 0U;
  scene.definitions.push_back(
      {0U, "openrc.player/default", openrc::kEntityDefinitionInitiallyEnabledV1,
       openrc::kEntitySceneNoAuthoringGroupIdV1});
  scene.actor_bindings.push_back(
      {0U, "actors/ratchet/high", openrc::ActorAffineTransformV1{}});
  scene.player_bindings.push_back({0U, 0U});
  return openrc::canonicalize_entity_scene_v1(std::move(scene), kEntityLimits);
}

[[nodiscard]] std::vector<openrc::RacGameplayMobyInstanceV1>
make_placements() {
  std::vector<openrc::RacGameplayMobyInstanceV1> result(4U);
  result[0U].class_id = 1U;
  result[0U].scale = 1.0F;
  result[1U].class_id = 749U;
  result[1U].scale = 0.5F;
  result[1U].position = {10.0F, 20.0F, 30.0F};
  result[1U].rotation = {0.0F, 0.0F,
                         std::numbers::pi_v<float> * 0.5F};
  result[2U].class_id = 2U;
  result[2U].scale = 1.0F;
  result[3U].class_id = 749U;
  result[3U].scale = 2.0F;
  result[3U].position = {-4.0F, 5.0F, 6.0F};
  return result;
}

[[nodiscard]] openrc::RacMobyActorSceneCompileProfileV1 make_profile() {
  return {749U, std::string(kModelKey), std::string(kArchetypeKey), {}};
}

template <typename Callback>
void expect_rejected(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::RacMobyActorSceneCompileError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_compiles_source_ordinals_and_transforms() {
  const auto library = make_library();
  const auto base = make_base_scene();
  const auto placements = make_placements();
  const auto result = openrc::compile_rac_moby_actor_scene_v1(
      base, library, placements, make_profile(), kLimits);
  expect(result.authored_ids == std::vector<std::uint32_t>{1U, 3U},
         "Moby actor compiler did not preserve source ordinals");
  const auto &scene = result.entity_scene;
  expect(scene.level_id == 0U && scene.definitions.size() == 3U &&
             scene.transforms.size() == 2U &&
             scene.actor_bindings.size() == 3U &&
             scene.player_bindings == base.player_bindings &&
             scene.render_bindings.empty(),
         "Moby actor compiler produced wrong entity domains");
  expect(scene.definitions[1U].authored_id == 1U &&
             scene.definitions[1U].archetype_key == kArchetypeKey &&
             scene.actor_bindings[1U].authored_id == 1U &&
             scene.actor_bindings[1U].model_key == kModelKey,
         "Moby actor semantic bindings are wrong");
  const auto &first = scene.transforms[0U].transform;
  expect(first.position == std::array<float, 3U>{10.0F, 20.0F, 30.0F} &&
             first.scale == std::array<float, 3U>{0.5F, 0.5F, 0.5F} &&
             near(first.rotation[0U], 0.0F) &&
             near(first.rotation[1U], 0.0F) &&
             near(first.rotation[2U], std::sqrt(0.5F)) &&
             near(first.rotation[3U], std::sqrt(0.5F)),
         "Moby actor Euler placement was not preserved as a world transform");
}

void test_rejects_missing_colliding_and_invalid_sources() {
  const auto library = make_library();
  const auto base = make_base_scene();
  const auto placements = make_placements();
  auto missing_profile = make_profile();
  missing_profile.source_class_id = 999U;
  expect_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_moby_actor_scene_v1(
            base, library, placements, missing_profile, kLimits));
      },
      "Moby actor compiler accepted a profile without placements");

  auto colliding = placements;
  colliding[0U].class_id = 749U;
  expect_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_moby_actor_scene_v1(
            base, library, colliding, make_profile(), kLimits));
      },
      "Moby actor compiler accepted an authored-ID collision");

  auto invalid = placements;
  invalid[1U].scale = 0.0F;
  expect_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_moby_actor_scene_v1(
            base, library, invalid, make_profile(), kLimits));
      },
      "Moby actor compiler accepted an invalid placement transform");

  auto limited = kLimits;
  limited.entity_scene.max_actor_bindings = 2U;
  expect_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_moby_actor_scene_v1(
            base, library, placements, make_profile(), limited));
      },
      "Moby actor compiler ignored the aggregate actor-binding limit");
}

} // namespace

int main() {
  try {
    test_compiles_source_ordinals_and_transforms();
    test_rejects_missing_colliding_and_invalid_sources();
    std::cout << "RAC Moby actor scene compile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC Moby actor scene compile test failure: " << error.what()
              << '\n';
    return 1;
  }
}
