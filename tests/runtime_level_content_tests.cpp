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
constexpr openrc::game::RuntimeLevelContentLimitsV1 kRuntimeLimits{
    {
        kContentApiVersion,
        kCollisionLimits,
        kBootstrapLimits,
    },
    kRenderSceneLimits,
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
             content.render_scene == make_render_scene(),
         "combined runtime loader changed or disconnected mounted content");
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

} // namespace

int main() {
  try {
    test_mounts_complete_content_from_one_package();
    test_rejects_missing_render_scene();
    test_rejects_implicit_and_incompatible_content_api();
    test_rejects_corrupt_foundation_resources();
    test_rejects_corrupt_render_scene();
    std::cout << "runtime_level_content_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime_level_content_tests: " << error.what() << '\n';
    return 1;
  }
}
