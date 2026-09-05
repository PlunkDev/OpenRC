#include "openrc/runtime_destructible_scene_resource.hpp"

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
constexpr openrc::DestructibleSceneIoLimitsV1 kLimits{
    1U << 20U,
    {
        8U,
        32U,
        8U,
        64U,
        512U,
        10'000U,
        10'000U,
        1000.0F,
        1000.0F,
    },
};

constexpr std::size_t kDeclaredTotalBytesOffset = 0x10U;
constexpr std::size_t kSceneLevelIdOffset = 0x20U;
constexpr std::size_t kFirstDestructibleFlagsOffset = 0x84U;

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_runtime_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::RuntimeDestructibleSceneResourceError &) {
    return;
  }
  throw std::runtime_error(message);
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

[[nodiscard]] openrc::DestructibleSceneV1 make_scene() {
  openrc::DestructibleDefinitionV1 crate;
  crate.authored_id = 40U;
  crate.max_health = 100U;
  crate.accepted_damage_channels =
      openrc::game::kDamageChannelProjectileV1 |
      openrc::game::kDamageChannelExplosiveV1;
  crate.local_hit_center = {1.0F, 2.0F, 3.0F};
  crate.hit_radius = 1.5F;
  crate.drops = {{"ammo/standard", 4U, 0U}};

  openrc::DestructibleDefinitionV1 statue;
  statue.authored_id = 5U;
  statue.max_health = 250U;
  statue.accepted_damage_channels =
      openrc::game::kDamageChannelMeleeV1 |
      openrc::game::kDamageChannelExplosiveV1;
  statue.local_hit_center = {0.0F, 0.5F, 1.0F};
  statue.hit_radius = 2.25F;
  statue.drops = {
      {"progress/gold-bolt", 1U, 0U},
      {"currency/bolt", 25U, 0U},
  };

  openrc::DestructibleSceneV1 result;
  result.level_id = kLevelId;
  result.destructibles = {crate, statue};
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
      openrc::kDestructibleSceneResourceIdV1,
      openrc::kDestructibleSceneResourceTypeIdV1,
      openrc::kDestructibleSceneResourceSchemaVersionV1,
      openrc::encode_destructible_scene_v1(make_scene(), kLimits)));
  result.resources.push_back(
      make_resource("future/unrelated", "openrc.future-resource", 17U,
                    {std::byte{0x12U}, std::byte{0x34U}, std::byte{0x56U}}));
  return result;
}

[[nodiscard]] openrc::ResolvedLevelPackageV1 make_package_without_scene() {
  auto result = make_package();
  result.resources.erase(result.resources.begin());
  return result;
}

[[nodiscard]] openrc::LevelPackageResourceV1 &
find_scene_resource(openrc::ResolvedLevelPackageV1 &package) {
  for (auto &resource : package.resources) {
    if (resource.resource_id == openrc::kDestructibleSceneResourceIdV1) {
      return resource;
    }
  }
  throw std::runtime_error("test destructible-scene resource is missing");
}

void refresh_digest(openrc::LevelPackageResourceV1 &resource) {
  resource.payload_sha256 =
      openrc::prepared_content_sha256_v1(resource.payload);
}

void test_absence_is_compatible_and_extra_resources_are_ignored() {
  const auto absent =
      openrc::game::load_optional_runtime_destructible_scene_resource_v1(
          make_package_without_scene(), kContentApiVersion, kLimits);
  expect(!absent,
         "runtime destructible loader did not preserve old-package "
         "compatibility");

  const auto loaded =
      openrc::game::load_optional_runtime_destructible_scene_resource_v1(
          make_package(), kContentApiVersion, kLimits);
  expect(loaded.has_value() &&
             *loaded == openrc::canonicalize_destructible_scene_v1(
                            make_scene(), kLimits.scene),
         "runtime destructible loader changed data or rejected an unrelated "
         "resource");
}

void test_present_resource_requires_exact_resolved_identity() {
  auto duplicate = make_package();
  duplicate.resources.push_back(find_scene_resource(duplicate));
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                duplicate, kContentApiVersion, kLimits));
      },
      "runtime destructible loader accepted a duplicate resource ID");

  auto wrong_type = make_package();
  find_scene_resource(wrong_type).type_id = "openrc.not-destructible-scene";
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                wrong_type, kContentApiVersion, kLimits));
      },
      "runtime destructible loader accepted the wrong resource type");

  auto wrong_schema = make_package();
  ++find_scene_resource(wrong_schema).schema_version;
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                wrong_schema, kContentApiVersion, kLimits));
      },
      "runtime destructible loader accepted the wrong resource schema");

  auto remove = make_package();
  find_scene_resource(remove).operation =
      openrc::LevelPackageResourceOperationV1::remove;
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                remove, kContentApiVersion, kLimits));
      },
      "runtime destructible loader accepted an unresolved remove");
}

void test_api_policy_is_checked_even_when_scene_is_absent() {
  const auto absent = make_package_without_scene();
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                absent, 0U, kLimits));
      },
      "runtime destructible loader accepted an implicit content API policy");
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                absent, kContentApiVersion + 1U, kLimits));
      },
      "runtime destructible loader accepted a mismatched API without the "
      "resource");
}

void test_digest_payload_and_nested_limits_are_strict() {
  auto zero_digest = make_package();
  find_scene_resource(zero_digest).payload_sha256 = {};
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                zero_digest, kContentApiVersion, kLimits));
      },
      "runtime destructible loader accepted a zero payload digest");

  auto stale_digest = make_package();
  find_scene_resource(stale_digest).payload[0U] ^= std::byte{0x01U};
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                stale_digest, kContentApiVersion, kLimits));
      },
      "runtime destructible loader accepted a stale payload digest");

  const auto package = make_package();
  auto small_payload_limit = kLimits;
  small_payload_limit.max_encoded_bytes =
      package.resources.front().payload.size() - 1U;
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                package, kContentApiVersion, small_payload_limit));
      },
      "runtime destructible loader ignored its encoded-byte limit");

  auto small_scene_limit = kLimits;
  small_scene_limit.scene.max_destructibles = 1U;
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                package, kContentApiVersion, small_scene_limit));
      },
      "runtime destructible loader did not forward nested scene limits");
}

void test_declared_size_decode_and_level_identity_are_strict() {
  auto false_size = make_package();
  auto &false_size_resource = find_scene_resource(false_size);
  write_u64(false_size_resource.payload, kDeclaredTotalBytesOffset,
            false_size_resource.payload.size() - 1U);
  refresh_digest(false_size_resource);
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                false_size, kContentApiVersion, kLimits));
      },
      "runtime destructible loader accepted a freshly hashed false declared "
      "size");

  auto malformed = make_package();
  auto &malformed_resource = find_scene_resource(malformed);
  write_u32(malformed_resource.payload, kFirstDestructibleFlagsOffset, 1U);
  refresh_digest(malformed_resource);
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                malformed, kContentApiVersion, kLimits));
      },
      "runtime destructible loader accepted a freshly hashed invalid scene");

  auto wrong_level = make_package();
  auto &wrong_level_resource = find_scene_resource(wrong_level);
  write_u32(wrong_level_resource.payload, kSceneLevelIdOffset, kLevelId + 1U);
  refresh_digest(wrong_level_resource);
  expect_runtime_error(
      [&] {
        static_cast<void>(
            openrc::game::load_optional_runtime_destructible_scene_resource_v1(
                wrong_level, kContentApiVersion, kLimits));
      },
      "runtime destructible loader accepted a scene for a different level");
}

} // namespace

int main() {
  try {
    test_absence_is_compatible_and_extra_resources_are_ignored();
    test_present_resource_requires_exact_resolved_identity();
    test_api_policy_is_checked_even_when_scene_is_absent();
    test_digest_payload_and_nested_limits_are_strict();
    test_declared_size_decode_and_level_identity_are_strict();
    std::cout << "runtime_destructible_scene_resource_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime_destructible_scene_resource_tests: " << error.what()
              << '\n';
    return 1;
  }
}
