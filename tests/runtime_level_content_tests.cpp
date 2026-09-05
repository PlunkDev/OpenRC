#include "openrc/runtime_level_content.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kLevelId = 9U;
constexpr std::uint32_t kContentApiVersion = 4U;
constexpr openrc::CollisionWorldBuildLimitsV1 kWorldLimits{
    128U, 128U, 4096U, 16'384U, openrc::kCollisionDefaultGridCellSizeQ6V1,
};
constexpr openrc::CollisionWorldIoLimitsV1 kCollisionLimits{
    1U << 20U,
    kWorldLimits,
};
constexpr openrc::LevelBootstrapV1Limits kBootstrapLimits{
    4096U,
    16U,
};
constexpr openrc::RenderSceneIoLimitsV1 kRenderSceneLimits{
    1U << 20U,
    {
        8U,
        8U,
        16U,
        8U,
        8U,
        16U,
        16U,
        64U,
        64U,
        4096U,
        1024U,
        3072U,
        16'384U,
    },
};
constexpr openrc::ActorLibraryIoLimitsV1 kActorLibraryLimits{
    1U << 20U,
    {
        4U,
        4U,
        64U,
        512U,
        16U,
        32U,
        8U,
        8U,
        16U,
        8U,
        8U,
        16U,
        1024U,
        3072U,
        64U,
        64U,
        4096U,
        16'384U,
    },
};
constexpr openrc::EntitySceneIoLimitsV1 kEntitySceneLimits{
    1U << 20U,
    {
        16U,
        16U,
        16U,
        16U,
        4U,
        64U,
        64U,
        1024U,
    },
};
constexpr openrc::game::RuntimeLevelContentLimitsV1 kRuntimeLimits{
    {
        kContentApiVersion,
        kCollisionLimits,
        kBootstrapLimits,
    },
    kRenderSceneLimits,
    kActorLibraryLimits,
    kEntitySceneLimits,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_runtime_error(Callback &&callback,
                          const std::string_view expected_context,
                          const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::RuntimeLevelContentError &error) {
    expect(std::string_view(error.what()).find(expected_context) !=
               std::string_view::npos,
           message + ": error lost its resource context");
    return;
  } catch (const std::exception &) {
    throw std::runtime_error(message + ": wrong exception type escaped");
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::CollisionWorldV1 make_collision_world() {
  openrc::CollisionMeshV1 mesh;
  mesh.vertices = {
      {-640, -640, 0},
      {640, -640, 0},
      {640, 640, 0},
      {-640, 640, 0},
  };
  mesh.triangles = {
      {{0U, 1U, 2U}, {}, openrc::CollisionLayerV1::world},
      {{0U, 2U, 3U}, {}, openrc::CollisionLayerV1::world},
  };
  return openrc::build_collision_world_v1(std::move(mesh), kWorldLimits);
}

[[nodiscard]] openrc::LevelBootstrapV1 make_bootstrap() {
  openrc::LevelBootstrapV1 result;
  result.level_id = kLevelId;
  result.death_height_world = -8.0;
  result.default_spawn_id = 12U;
  result.spawn_points = {
      {12U, {1.5, -2.0, 3.25}, 0.5},
      {27U, {-4.0, 5.5, 2.0}, -0.25},
  };
  return result;
}

[[nodiscard]] openrc::RenderSceneV1 make_render_scene() {
  openrc::RenderSceneMaterialV1 material;
  material.id = 0U;
  material.base_color_rgba8 = UINT32_C(0xffb07030);

  openrc::RenderSceneMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      {-1.0F, -1.0F, 0.0F, 0.0F, 0.0F, UINT32_C(0xffffffff)},
      {1.0F, -1.0F, 0.0F, 1.0F, 0.0F, UINT32_C(0xffffffff)},
      {0.0F, 1.0F, 0.0F, 0.5F, 1.0F, UINT32_C(0xffffffff)},
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};

  openrc::RenderSceneInstanceV1 instance;
  instance.id = 0U;
  instance.mesh_id = 0U;

  openrc::RenderSceneV1 result;
  result.materials.push_back(material);
  result.meshes.push_back(std::move(mesh));
  result.instances.push_back(instance);
  return result;
}

[[nodiscard]] openrc::ActorAffineTransformV1 identity_actor_transform() {
  openrc::ActorAffineTransformV1 result;
  result.values = {
      1.0F, 0.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 0.0F,
  };
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

[[nodiscard]] openrc::ActorSkinnedVertexV1 actor_vertex(
    const float x, const float y, const float u, const float v) {
  openrc::ActorSkinnedVertexV1 result;
  result.x = x;
  result.y = y;
  result.nz = 1.0F;
  result.u = u;
  result.v = v;
  result.skin = rigid_skin();
  return result;
}

[[nodiscard]] openrc::ActorLibraryV1 make_actor_library() {
  openrc::ActorRigAssetV1 rig;
  rig.id = 0U;
  rig.semantic_key = "actors/player/rig";
  rig.rig.joints.push_back(
      {-1, identity_actor_transform(), identity_actor_transform()});

  openrc::RenderSceneMaterialV1 material;
  material.id = 0U;

  openrc::ActorSkinnedMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      actor_vertex(-1.0F, -1.0F, 0.0F, 0.0F),
      actor_vertex(1.0F, -1.0F, 1.0F, 0.0F),
      actor_vertex(0.0F, 1.0F, 0.5F, 1.0F),
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};

  openrc::ActorModelV1 model;
  model.id = 0U;
  model.semantic_key = "actors/player/high";
  model.rig_key = rig.semantic_key;
  model.materials.push_back(material);
  model.meshes.push_back(std::move(mesh));

  openrc::ActorLibraryV1 result;
  result.rigs.push_back(std::move(rig));
  result.models.push_back(std::move(model));
  return result;
}

[[nodiscard]] openrc::EntitySceneV1 make_entity_scene() {
  openrc::EntityDefinitionV1 prop;
  prop.authored_id = 40U;
  prop.archetype_key = "openrc.prop/test";

  openrc::EntityDefinitionV1 player;
  player.authored_id = 5U;
  player.archetype_key = "openrc.player/default";

  openrc::EntityTransformComponentV1 prop_transform;
  prop_transform.authored_id = prop.authored_id;
  prop_transform.transform.position = {1.0F, 2.0F, 3.0F};

  openrc::EntityActorBindingV1 actor;
  actor.authored_id = player.authored_id;
  actor.model_key = "actors/player/high";
  actor.model_to_entity = identity_actor_transform();

  openrc::EntitySceneV1 result;
  result.level_id = kLevelId;
  result.definitions = {prop, player};
  result.transforms = {prop_transform};
  result.render_bindings = {{prop.authored_id, 0U}};
  result.actor_bindings = {actor};
  result.player_bindings = {{player.authored_id, 0U}};
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
  result.build_id = "runtime-content-test-build";
  result.resources.push_back(make_resource(
      openrc::kLevelBootstrapResourceIdV1,
      openrc::kLevelBootstrapResourceTypeIdV1,
      openrc::kLevelBootstrapResourceSchemaVersionV1,
      openrc::encode_level_bootstrap_v1(make_bootstrap(), kBootstrapLimits)));
  result.resources.push_back(
      make_resource(openrc::kCollisionWorldResourceIdV1,
                    openrc::kCollisionWorldResourceTypeIdV1,
                    openrc::kCollisionWorldResourceSchemaVersionV1,
                    openrc::encode_collision_world_v1(make_collision_world(),
                                                      kCollisionLimits)));
  result.resources.push_back(make_resource(
      openrc::kRenderSceneResourceIdV1, openrc::kRenderSceneResourceTypeIdV1,
      openrc::kRenderSceneResourceSchemaVersionV1,
      openrc::encode_render_scene_v1(make_render_scene(), kRenderSceneLimits)));
  result.resources.push_back(make_resource("gameplay/entities",
                                           "openrc.gameplay-scene", 1U,
                                           {std::byte{0x12}, std::byte{0x34}}));
  return result;
}

void add_actor_library_resource(
    openrc::ResolvedLevelPackageV1 &package,
    const openrc::ActorLibraryV1 &library,
    const openrc::ActorLibraryIoLimitsV1 limits) {
  package.resources.push_back(make_resource(
      openrc::kActorLibraryResourceIdV1,
      openrc::kActorLibraryResourceTypeIdV1,
      openrc::kActorLibraryResourceSchemaVersionV1,
      openrc::encode_actor_library_v1(library, limits)));
}

void add_actor_library_resource(openrc::ResolvedLevelPackageV1 &package) {
  add_actor_library_resource(package, make_actor_library(),
                             kActorLibraryLimits);
}

void add_entity_scene_resource(openrc::ResolvedLevelPackageV1 &package,
                               const openrc::EntitySceneV1 &scene) {
  package.resources.push_back(make_resource(
      openrc::kEntitySceneResourceIdV1,
      openrc::kEntitySceneResourceTypeIdV1,
      openrc::kEntitySceneResourceSchemaVersionV1,
      openrc::encode_entity_scene_v1(scene, kEntitySceneLimits)));
}

[[nodiscard]] openrc::ResolvedLevelPackageV1
make_package_with_actor_entities() {
  auto result = make_package();
  add_actor_library_resource(result);
  add_entity_scene_resource(result, make_entity_scene());
  return result;
}

[[nodiscard]] openrc::LevelPackageResourceV1 &
find_resource(openrc::ResolvedLevelPackageV1 &package,
              const std::string_view resource_id) {
  const auto iterator =
      std::find_if(package.resources.begin(), package.resources.end(),
                   [resource_id](const auto &resource) {
                     return resource.resource_id == resource_id;
                   });
  if (iterator == package.resources.end()) {
    throw std::runtime_error("test package resource is missing");
  }
  return *iterator;
}

void corrupt_and_rehash(openrc::LevelPackageResourceV1 &resource) {
  expect(!resource.payload.empty(), "test payload unexpectedly is empty");
  resource.payload.front() ^= std::byte{0x01};
  resource.payload_sha256 =
      openrc::prepared_content_sha256_v1(resource.payload);
}

void test_mounts_complete_content_from_one_package() {
  const auto content = openrc::game::load_runtime_level_content_v1(
      make_package(), kRuntimeLimits);
  expect(content.foundation.level_id == kLevelId &&
             content.foundation.content_api_version == kContentApiVersion &&
             content.foundation.build_id == "runtime-content-test-build" &&
             content.foundation.bootstrap == make_bootstrap() &&
             content.foundation.collision_world == make_collision_world() &&
             content.render_scene == make_render_scene() &&
             !content.actor_library && !content.entity_scene,
         "combined runtime loader changed or disconnected mounted content");
}

void test_mounts_complete_actor_entity_feature_pair() {
  const auto content = openrc::game::load_runtime_level_content_v1(
      make_package_with_actor_entities(), kRuntimeLimits);
  expect(content.actor_library.has_value() &&
             content.entity_scene.has_value() &&
             *content.actor_library == openrc::canonicalize_actor_library_v1(
                                           make_actor_library(),
                                           kActorLibraryLimits.library) &&
             *content.entity_scene == openrc::canonicalize_entity_scene_v1(
                                          make_entity_scene(),
                                          kEntitySceneLimits.scene),
         "combined runtime loader changed the mounted actor/entity pair");
}

void test_rejects_missing_render_scene() {
  auto package = make_package();
  const auto erased =
      std::erase_if(package.resources, [](const auto &resource) {
        return resource.resource_id == openrc::kRenderSceneResourceIdV1;
      });
  expect(erased == 1U, "test failed to remove exactly one render scene");
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            package, kRuntimeLimits));
      },
      "render scene", "combined loader accepted a missing render scene");
}

void test_rejects_implicit_and_incompatible_content_api() {
  auto implicit_limits = kRuntimeLimits;
  implicit_limits.foundation.required_content_api_version = 0U;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            make_package(), implicit_limits));
      },
      "policy", "combined loader accepted an implicit content API policy");

  auto incompatible_limits = kRuntimeLimits;
  incompatible_limits.foundation.required_content_api_version += 1U;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            make_package(), incompatible_limits));
      },
      "foundation", "combined loader accepted an incompatible content API");
}

void test_rejects_corrupt_foundation_resources() {
  for (const auto resource_id : {openrc::kCollisionWorldResourceIdV1,
                                 openrc::kLevelBootstrapResourceIdV1}) {
    auto package = make_package();
    corrupt_and_rehash(find_resource(package, resource_id));
    expect_runtime_error(
        [&] {
          static_cast<void>(openrc::game::load_runtime_level_content_v1(
              package, kRuntimeLimits));
        },
        "foundation",
        "combined loader accepted a corrupt freshly hashed foundation");
  }
}

void test_rejects_corrupt_render_scene() {
  auto package = make_package();
  corrupt_and_rehash(find_resource(package, openrc::kRenderSceneResourceIdV1));
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            package, kRuntimeLimits));
      },
      "render scene",
      "combined loader accepted a corrupt freshly hashed render scene");
}

void test_rejects_incomplete_actor_entity_feature_pair() {
  auto actor_only = make_package();
  add_actor_library_resource(actor_only);
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            actor_only, kRuntimeLimits));
      },
      "feature pair", "combined loader accepted an actor library without entities");

  auto entities_only = make_package();
  add_entity_scene_resource(entities_only, make_entity_scene());
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            entities_only, kRuntimeLimits));
      },
      "feature pair", "combined loader accepted entities without an actor library");
}

void test_rejects_dangling_cross_resource_references() {
  auto missing_model_scene = make_entity_scene();
  missing_model_scene.actor_bindings[0U].model_key = "actors/missing/high";
  auto missing_model = make_package();
  add_actor_library_resource(missing_model);
  add_entity_scene_resource(missing_model, missing_model_scene);
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            missing_model, kRuntimeLimits));
      },
      "actor model", "combined loader accepted a dangling actor model key");

  auto missing_render_scene = make_entity_scene();
  missing_render_scene.render_bindings[0U].render_instance_id = 99U;
  auto missing_render = make_package();
  add_actor_library_resource(missing_render);
  add_entity_scene_resource(missing_render, missing_render_scene);
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            missing_render, kRuntimeLimits));
      },
      "render instance",
      "combined loader accepted a dangling render-instance ID");
}

void test_rejects_wrong_entity_level_and_unusable_player_slot() {
  auto wrong_level_scene = make_entity_scene();
  wrong_level_scene.level_id = kLevelId + 1U;
  auto wrong_level = make_package();
  add_actor_library_resource(wrong_level);
  add_entity_scene_resource(wrong_level, wrong_level_scene);
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            wrong_level, kRuntimeLimits));
      },
      "entity scene", "combined loader accepted a different entity level");

  auto no_slot_zero_scene = make_entity_scene();
  no_slot_zero_scene.player_bindings[0U].local_player_slot = 1U;
  auto no_slot_zero = make_package();
  add_actor_library_resource(no_slot_zero);
  add_entity_scene_resource(no_slot_zero, no_slot_zero_scene);
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            no_slot_zero, kRuntimeLimits));
      },
      "slot 0", "combined loader accepted an absent local-player slot 0");

  auto no_player_actor_scene = make_entity_scene();
  no_player_actor_scene.actor_bindings.clear();
  auto no_player_actor = make_package();
  add_actor_library_resource(no_player_actor);
  add_entity_scene_resource(no_player_actor, no_player_actor_scene);
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            no_player_actor, kRuntimeLimits));
      },
      "actor binding",
      "combined loader accepted a player without an actor binding");
}

void test_rejects_singular_or_ill_conditioned_player_model_transform() {
  for (const auto linear_x : {0.0F, 1.0e-10F}) {
    auto invalid_scene = make_entity_scene();
    invalid_scene.actor_bindings[0U].model_to_entity.values[0U] = linear_x;
    auto package = make_package();
    add_actor_library_resource(package);
    add_entity_scene_resource(package, invalid_scene);
    expect_runtime_error(
        [&] {
          static_cast<void>(openrc::game::load_runtime_level_content_v1(
              package, kRuntimeLimits));
        },
        linear_x == 0.0F ? "singular" : "ill-conditioned",
        "combined loader accepted a singular or ill-conditioned player "
        "model transform");
  }
}

void test_rejects_disabled_player_definition() {
  auto scene = make_entity_scene();
  const auto player =
      std::find_if(scene.definitions.begin(), scene.definitions.end(),
                   [](const openrc::EntityDefinitionV1 &definition) {
                     return definition.authored_id == 5U;
                   });
  expect(player != scene.definitions.end(),
         "test scene is missing its player definition");
  player->flags = 0U;

  auto package = make_package();
  add_actor_library_resource(package);
  add_entity_scene_resource(package, scene);
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            package, kRuntimeLimits));
      },
      "disabled", "combined loader accepted a disabled player definition");
}

void test_preflights_player_actor_pose() {
  auto ill_conditioned = make_actor_library();
  ill_conditioned.rigs[0U]
      .rig.joints[0U]
      .local_bind_transform.values[0U] = 1.0e-10F;
  auto ill_conditioned_package = make_package();
  add_actor_library_resource(ill_conditioned_package, ill_conditioned,
                             kActorLibraryLimits);
  add_entity_scene_resource(ill_conditioned_package, make_entity_scene());
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            ill_conditioned_package, kRuntimeLimits));
      },
      "presentation preflight",
      "combined loader deferred an ill-conditioned rig to the renderer");

  auto zero_normal = make_actor_library();
  auto &vertex = zero_normal.models[0U].meshes[0U].vertices[0U];
  vertex.nx = 0.0F;
  vertex.ny = 0.0F;
  vertex.nz = 0.0F;
  auto zero_normal_package = make_package();
  add_actor_library_resource(zero_normal_package, zero_normal,
                             kActorLibraryLimits);
  add_entity_scene_resource(zero_normal_package, make_entity_scene());
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            zero_normal_package, kRuntimeLimits));
      },
      "presentation preflight",
      "combined loader deferred an unusable actor normal to the renderer");
}

void test_rejects_player_actor_over_presentation_caps() {
  constexpr auto draw_count =
      openrc::game::kRuntimePlayerActorMaximumDrawRangesV1 + 1U;
  auto oversized = make_actor_library();
  auto &mesh = oversized.models[0U].meshes[0U];
  mesh.triangle_indices.clear();
  mesh.draw_ranges.clear();
  mesh.triangle_indices.reserve(static_cast<std::size_t>(draw_count * 3U));
  mesh.draw_ranges.reserve(static_cast<std::size_t>(draw_count));
  for (std::uint64_t index = 0U; index < draw_count; ++index) {
    const auto first_index = index * 3U;
    mesh.triangle_indices.push_back(0U);
    mesh.triangle_indices.push_back(1U);
    mesh.triangle_indices.push_back(2U);
    mesh.draw_ranges.push_back({0U, first_index, 3U});
  }

  auto actor_limits = kActorLibraryLimits;
  actor_limits.max_encoded_bytes = 8U << 20U;
  actor_limits.library.max_draw_ranges = draw_count;
  actor_limits.library.max_triangle_indices = draw_count * 3U;
  auto runtime_limits = kRuntimeLimits;
  runtime_limits.actor_library = actor_limits;

  auto package = make_package();
  add_actor_library_resource(package, oversized, actor_limits);
  add_entity_scene_resource(package, make_entity_scene());
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            package, runtime_limits));
      },
      "draw-range presentation limit",
      "combined loader accepted a player actor above presentation caps");
}

void test_rejects_corrupt_optional_payloads_and_forwards_limits() {
  for (const auto resource_id : {openrc::kActorLibraryResourceIdV1,
                                 openrc::kEntitySceneResourceIdV1}) {
    auto package = make_package_with_actor_entities();
    corrupt_and_rehash(find_resource(package, resource_id));
    expect_runtime_error(
        [&] {
          static_cast<void>(openrc::game::load_runtime_level_content_v1(
              package, kRuntimeLimits));
        },
        resource_id == openrc::kActorLibraryResourceIdV1 ? "actor library"
                                                         : "entity scene",
        "combined loader accepted a corrupt freshly hashed optional payload");
  }

  auto actor_limits = kRuntimeLimits;
  actor_limits.actor_library.max_encoded_bytes =
      openrc::kActorLibraryIoHeaderBytesV1;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            make_package_with_actor_entities(), actor_limits));
      },
      "actor library", "combined loader ignored actor-library limits");

  auto entity_limits = kRuntimeLimits;
  entity_limits.entity_scene.max_encoded_bytes =
      openrc::kEntitySceneIoHeaderBytesV1;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_content_v1(
            make_package_with_actor_entities(), entity_limits));
      },
      "entity scene", "combined loader ignored entity-scene limits");
}

} // namespace

int main() {
  try {
    test_mounts_complete_content_from_one_package();
    test_mounts_complete_actor_entity_feature_pair();
    test_rejects_missing_render_scene();
    test_rejects_implicit_and_incompatible_content_api();
    test_rejects_corrupt_foundation_resources();
    test_rejects_corrupt_render_scene();
    test_rejects_incomplete_actor_entity_feature_pair();
    test_rejects_dangling_cross_resource_references();
    test_rejects_wrong_entity_level_and_unusable_player_slot();
    test_rejects_singular_or_ill_conditioned_player_model_transform();
    test_rejects_disabled_player_definition();
    test_preflights_player_actor_pose();
    test_rejects_player_actor_over_presentation_caps();
    test_rejects_corrupt_optional_payloads_and_forwards_limits();
    std::cout << "runtime_level_content_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime_level_content_tests: " << error.what() << '\n';
    return 1;
  }
}
