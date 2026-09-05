#include "openrc/runtime_gameplay_scene_resource.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimeGameplaySceneResourceError(message);
}

[[nodiscard]] const LevelPackageResourceV1 *
find_gameplay_scene_resource(const ResolvedLevelPackageV1 &package,
                             const std::uint64_t maximum_payload_bytes) {
  const LevelPackageResourceV1 *found = nullptr;
  for (const auto &resource : package.resources) {
    if (resource.resource_id != kGameplaySceneResourceIdV1) {
      continue;
    }
    if (found != nullptr) {
      fail("ResolvedLevelPackageV1 repeats optional resource " +
           std::string(kGameplaySceneResourceIdV1));
    }
    found = &resource;
  }

  if (found == nullptr) {
    return nullptr;
  }
  if (found->type_id != kGameplaySceneResourceTypeIdV1 ||
      found->schema_version != kGameplaySceneResourceSchemaVersionV1) {
    fail("Optional resource " + std::string(kGameplaySceneResourceIdV1) +
         " has an incompatible type or schema");
  }
  if (found->operation != LevelPackageResourceOperationV1::upsert) {
    fail("Optional resource " + std::string(kGameplaySceneResourceIdV1) +
         " is not a resolved upsert");
  }
  if (found->payload.size() > maximum_payload_bytes) {
    fail("Optional resource " + std::string(kGameplaySceneResourceIdV1) +
         " exceeds its runtime payload limit");
  }
  if (is_zero_prepared_digest_v1(found->payload_sha256) ||
      prepared_content_sha256_v1(found->payload) != found->payload_sha256) {
    fail("Optional resource " + std::string(kGameplaySceneResourceIdV1) +
         " has a missing or stale resolved payload digest");
  }
  return found;
}

} // namespace

std::optional<GameplaySceneV1> load_optional_runtime_gameplay_scene_resource_v1(
    const ResolvedLevelPackageV1 &package,
    const std::uint32_t required_content_api_version,
    const GameplaySceneIoLimitsV1 limits) {
  if (required_content_api_version == 0U) {
    fail("Runtime gameplay-scene content API policy must be explicit");
  }
  if (package.content_api_version != required_content_api_version) {
    fail("ResolvedLevelPackageV1 uses an incompatible content API version");
  }

  const auto *resource =
      find_gameplay_scene_resource(package, limits.max_encoded_bytes);
  if (resource == nullptr) {
    return std::nullopt;
  }

  GameplaySceneV1 scene;
  try {
    scene = decode_gameplay_scene_v1(resource->payload, limits);
  } catch (const GameplaySceneIoError &error) {
    fail("Invalid runtime gameplay-scene resource: " +
         std::string(error.what()));
  }
  if (scene.level_id != package.level_id) {
    fail("ResolvedLevelPackageV1 level ID disagrees with GameplaySceneV1");
  }
  return scene;
}

} // namespace openrc::game
