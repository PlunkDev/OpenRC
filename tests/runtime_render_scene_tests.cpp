#include "openrc/runtime_render_scene.hpp"

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

constexpr std::uint32_t kLevelId = 4U;
constexpr std::uint32_t kContentApiVersion = 3U;
constexpr openrc::RenderSceneIoLimitsV1 kLimits{
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

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_runtime_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::RuntimeRenderSceneError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::RenderSceneV1 make_scene() {
  openrc::RenderSceneMaterialV1 material;
  material.id = 0U;

  openrc::RenderSceneMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      {-1.0F, -1.0F, 0.0F, 0.0F, 0.0F, UINT32_C(0xff0000ff)},
      {1.0F, -1.0F, 0.0F, 1.0F, 0.0F, UINT32_C(0xff00ff00)},
      {0.0F, 1.0F, 0.0F, 0.5F, 1.0F, UINT32_C(0xffff0000)},
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
  result.build_id = "test-build";
  result.resources.push_back(make_resource(
      openrc::kRenderSceneResourceIdV1,
      openrc::kRenderSceneResourceTypeIdV1,
      openrc::kRenderSceneResourceSchemaVersionV1,
      openrc::encode_render_scene_v1(make_scene(), kLimits)));

  // A future independently mounted package resource must not make the
  // render-scene loader reject an otherwise compatible package.
  result.resources.push_back(make_resource(
      "gameplay/entities", "openrc.gameplay-scene", 17U,
      {std::byte{0x12}, std::byte{0x34}, std::byte{0x56}}));
  return result;
}

[[nodiscard]] openrc::LevelPackageResourceV1 &
find_scene_resource(openrc::ResolvedLevelPackageV1 &package) {
  for (auto &resource : package.resources) {
    if (resource.resource_id == openrc::kRenderSceneResourceIdV1) {
      return resource;
    }
  }
  throw std::runtime_error("test render-scene resource is missing");
}

void test_loads_scene_and_ignores_extra_resource() {
  const auto loaded = openrc::game::load_runtime_render_scene_v1(
      make_package(), kContentApiVersion, kLimits);
  expect(loaded == make_scene(),
         "runtime render-scene loader changed decoded scene data");
}

void test_requires_exact_resource_identity() {
  auto missing = make_package();
  missing.resources.erase(missing.resources.begin());
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_render_scene_v1(
            missing, kContentApiVersion, kLimits));
      },
      "runtime render-scene loader accepted a missing resource");

  auto duplicate = make_package();
  duplicate.resources.push_back(find_scene_resource(duplicate));
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_render_scene_v1(
            duplicate, kContentApiVersion, kLimits));
      },
      "runtime render-scene loader accepted a duplicate resource ID");

  auto wrong_type = make_package();
  find_scene_resource(wrong_type).type_id = "openrc.not-render-scene";
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_render_scene_v1(
            wrong_type, kContentApiVersion, kLimits));
      },
      "runtime render-scene loader accepted the wrong resource type");

  auto wrong_schema = make_package();
  find_scene_resource(wrong_schema).schema_version += 1U;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_render_scene_v1(
            wrong_schema, kContentApiVersion, kLimits));
      },
      "runtime render-scene loader accepted the wrong resource schema");

  auto remove = make_package();
  find_scene_resource(remove).operation =
      openrc::LevelPackageResourceOperationV1::remove;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_render_scene_v1(
            remove, kContentApiVersion, kLimits));
      },
      "runtime render-scene loader accepted an unresolved remove");
}

void test_checks_content_api_digest_and_payload_limit() {
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_render_scene_v1(
            make_package(), 0U, kLimits));
      },
      "runtime render-scene loader accepted an implicit content API policy");
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_render_scene_v1(
            make_package(), kContentApiVersion + 1U, kLimits));
      },
      "runtime render-scene loader accepted a mismatched content API");

  auto zero_digest = make_package();
  find_scene_resource(zero_digest).payload_sha256 = {};
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_render_scene_v1(
            zero_digest, kContentApiVersion, kLimits));
      },
      "runtime render-scene loader accepted a missing payload digest");

  auto stale_digest = make_package();
  find_scene_resource(stale_digest).payload[0U] ^= std::byte{0x01};
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_render_scene_v1(
            stale_digest, kContentApiVersion, kLimits));
      },
      "runtime render-scene loader accepted a stale payload digest");

  const auto package = make_package();
  const auto &payload = package.resources.front().payload;
  auto small_limits = kLimits;
  small_limits.max_encoded_bytes = payload.size() - 1U;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_render_scene_v1(
            package, kContentApiVersion, small_limits));
      },
      "runtime render-scene loader ignored its payload byte limit");
}

void test_wraps_strict_render_scene_decode_errors() {
  auto malformed = make_package();
  auto &resource = find_scene_resource(malformed);
  resource.payload[0U] ^= std::byte{0x01};
  resource.payload_sha256 = openrc::prepared_content_sha256_v1(resource.payload);
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_render_scene_v1(
            malformed, kContentApiVersion, kLimits));
      },
      "runtime render-scene loader accepted a malformed freshly hashed "
      "payload");
}

} // namespace

int main() {
  try {
    test_loads_scene_and_ignores_extra_resource();
    test_requires_exact_resource_identity();
    test_checks_content_api_digest_and_payload_limit();
    test_wraps_strict_render_scene_decode_errors();
    std::cout << "runtime_render_scene_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime_render_scene_tests: " << error.what() << '\n';
    return 1;
  }
}
