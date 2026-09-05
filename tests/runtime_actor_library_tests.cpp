#include "openrc/runtime_actor_library.hpp"

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

constexpr std::uint32_t kLevelId = 7U;
constexpr std::uint32_t kContentApiVersion = 3U;
constexpr openrc::ActorLibraryIoLimitsV1 kLimits{
    1U << 20U,
    {
        1U,
        1U,
        64U,
        256U,
        4U,
        4U,
        4U,
        4U,
        4U,
        4U,
        4U,
        8U,
        16U,
        48U,
        16U,
        16U,
        256U,
        4096U,
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
  } catch (const openrc::game::RuntimeActorLibraryError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::ActorSkinBindingV1 skin() {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 1U;
  result.joint_indices[0U] = 0U;
  result.weight_numerators[0U] = 255U;
  result.weight_sum = 255U;
  return result;
}

[[nodiscard]] openrc::ActorSkinnedVertexV1 vertex(
    const float x, const float y, const float u, const float v) {
  openrc::ActorSkinnedVertexV1 result;
  result.x = x;
  result.y = y;
  result.nz = 1.0F;
  result.u = u;
  result.v = v;
  result.skin = skin();
  return result;
}

[[nodiscard]] openrc::ActorLibraryV1 make_library() {
  openrc::ActorRigAssetV1 rig;
  rig.id = 0U;
  rig.semantic_key = "actors/test/rig";
  rig.rig.joints.push_back({-1, {}, {}});

  openrc::RenderSceneMaterialV1 material;
  material.id = 0U;

  openrc::ActorSkinnedMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      vertex(-1.0F, -1.0F, 0.0F, 0.0F),
      vertex(1.0F, -1.0F, 1.0F, 0.0F),
      vertex(0.0F, 1.0F, 0.5F, 1.0F),
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};

  openrc::ActorModelV1 model;
  model.id = 0U;
  model.semantic_key = "actors/test/high";
  model.rig_key = rig.semantic_key;
  model.materials.push_back(material);
  model.meshes.push_back(std::move(mesh));

  openrc::ActorLibraryV1 result;
  result.rigs.push_back(std::move(rig));
  result.models.push_back(std::move(model));
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
      openrc::kActorLibraryResourceIdV1,
      openrc::kActorLibraryResourceTypeIdV1,
      openrc::kActorLibraryResourceSchemaVersionV1,
      openrc::encode_actor_library_v1(make_library(), kLimits)));
  result.resources.push_back(make_resource(
      "future/unrelated", "openrc.future-resource", 17U,
      {std::byte{0x12U}, std::byte{0x34U}, std::byte{0x56U}}));
  return result;
}

[[nodiscard]] openrc::ResolvedLevelPackageV1 make_package_without_actor() {
  auto result = make_package();
  result.resources.erase(result.resources.begin());
  return result;
}

[[nodiscard]] openrc::LevelPackageResourceV1 &
find_actor_resource(openrc::ResolvedLevelPackageV1 &package) {
  for (auto &resource : package.resources) {
    if (resource.resource_id == openrc::kActorLibraryResourceIdV1) {
      return resource;
    }
  }
  throw std::runtime_error("test actor-library resource is missing");
}

void test_absence_is_compatible_and_extra_resources_are_ignored() {
  const auto absent = openrc::game::load_optional_runtime_actor_library_v1(
      make_package_without_actor(), kContentApiVersion, kLimits);
  expect(!absent,
         "runtime actor loader did not preserve old-package compatibility");

  const auto loaded = openrc::game::load_optional_runtime_actor_library_v1(
      make_package(), kContentApiVersion, kLimits);
  expect(loaded.has_value() &&
             *loaded == openrc::canonicalize_actor_library_v1(
                            make_library(), kLimits.library),
         "runtime actor loader changed data or rejected an unrelated resource");
}

void test_present_resource_requires_exact_resolved_identity() {
  auto duplicate = make_package();
  duplicate.resources.push_back(find_actor_resource(duplicate));
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_library_v1(
            duplicate, kContentApiVersion, kLimits);
      },
      "runtime actor loader accepted a duplicate resource ID");

  auto wrong_type = make_package();
  find_actor_resource(wrong_type).type_id = "openrc.not-actor-library";
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_library_v1(
            wrong_type, kContentApiVersion, kLimits);
      },
      "runtime actor loader accepted the wrong resource type");

  auto wrong_schema = make_package();
  ++find_actor_resource(wrong_schema).schema_version;
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_library_v1(
            wrong_schema, kContentApiVersion, kLimits);
      },
      "runtime actor loader accepted the wrong resource schema");

  auto remove = make_package();
  find_actor_resource(remove).operation =
      openrc::LevelPackageResourceOperationV1::remove;
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_library_v1(
            remove, kContentApiVersion, kLimits);
      },
      "runtime actor loader accepted an unresolved remove");
}

void test_api_policy_is_checked_even_when_actor_is_absent() {
  const auto absent = make_package_without_actor();
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_library_v1(
            absent, 0U, kLimits);
      },
      "runtime actor loader accepted an implicit content API policy");
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_library_v1(
            absent, kContentApiVersion + 1U, kLimits);
      },
      "runtime actor loader accepted a mismatched content API without actor");
}

void test_digest_and_payload_limit_are_strict() {
  auto zero_digest = make_package();
  find_actor_resource(zero_digest).payload_sha256 = {};
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_library_v1(
            zero_digest, kContentApiVersion, kLimits);
      },
      "runtime actor loader accepted a zero payload digest");

  auto stale_digest = make_package();
  find_actor_resource(stale_digest).payload[0U] ^= std::byte{0x01U};
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_library_v1(
            stale_digest, kContentApiVersion, kLimits);
      },
      "runtime actor loader accepted a stale payload digest");

  const auto package = make_package();
  auto small_limits = kLimits;
  small_limits.max_encoded_bytes =
      package.resources.front().payload.size() - 1U;
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_library_v1(
            package, kContentApiVersion, small_limits);
      },
      "runtime actor loader ignored its encoded-byte limit");
}

void test_strict_decode_errors_are_wrapped() {
  auto malformed = make_package();
  auto &resource = find_actor_resource(malformed);
  resource.payload[0U] ^= std::byte{0x01U};
  resource.payload_sha256 =
      openrc::prepared_content_sha256_v1(resource.payload);
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_library_v1(
            malformed, kContentApiVersion, kLimits);
      },
      "runtime actor loader accepted a malformed freshly hashed payload");
}

} // namespace

int main() {
  try {
    test_absence_is_compatible_and_extra_resources_are_ignored();
    test_present_resource_requires_exact_resolved_identity();
    test_api_policy_is_checked_even_when_actor_is_absent();
    test_digest_and_payload_limit_are_strict();
    test_strict_decode_errors_are_wrapped();
    std::cout << "runtime_actor_library_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime_actor_library_tests: " << error.what() << '\n';
    return 1;
  }
}
