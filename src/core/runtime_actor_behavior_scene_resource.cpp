#include "openrc/runtime_actor_behavior_scene_resource.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimeActorBehaviorSceneResourceError(message);
}

[[nodiscard]] const LevelPackageResourceV1 *find_resource(
    const ResolvedLevelPackageV1 &package,
    const std::uint64_t maximum_payload_bytes) {
  const LevelPackageResourceV1 *found = nullptr;
  for (const auto &resource : package.resources) {
    if (resource.resource_id != kActorBehaviorSceneResourceIdV1) {
      continue;
    }
    if (found != nullptr) {
      fail("ResolvedLevelPackageV1 repeats optional resource " +
           std::string(kActorBehaviorSceneResourceIdV1));
    }
    found = &resource;
  }
  if (found == nullptr) {
    return nullptr;
  }
  if (found->type_id != kActorBehaviorSceneResourceTypeIdV1 ||
      found->schema_version != kActorBehaviorSceneResourceSchemaVersionV1) {
    fail("Optional resource " +
         std::string(kActorBehaviorSceneResourceIdV1) +
         " has an incompatible type or schema");
  }
  if (found->operation != LevelPackageResourceOperationV1::upsert) {
    fail("Optional resource " +
         std::string(kActorBehaviorSceneResourceIdV1) +
         " is not a resolved upsert");
  }
  if (found->payload.size() > maximum_payload_bytes) {
    fail("Optional resource " +
         std::string(kActorBehaviorSceneResourceIdV1) +
         " exceeds its runtime payload limit");
  }
  if (is_zero_prepared_digest_v1(found->payload_sha256) ||
      prepared_content_sha256_v1(found->payload) != found->payload_sha256) {
    fail("Optional resource " +
         std::string(kActorBehaviorSceneResourceIdV1) +
         " has a missing or stale resolved payload digest");
  }
  return found;
}

} // namespace

std::optional<ActorBehaviorSceneV1>
load_optional_runtime_actor_behavior_scene_resource_v1(
    const ResolvedLevelPackageV1 &package,
    const std::uint32_t required_content_api_version,
    const ActorBehaviorSceneIoLimitsV1 limits) {
  if (required_content_api_version == 0U) {
    fail("Runtime actor-behavior-scene content API policy must be explicit");
  }
  if (package.content_api_version != required_content_api_version) {
    fail("ResolvedLevelPackageV1 uses an incompatible content API version");
  }

  const auto *resource = find_resource(package, limits.max_encoded_bytes);
  if (resource == nullptr) {
    return std::nullopt;
  }

  ActorBehaviorSceneV1 scene;
  try {
    scene = decode_actor_behavior_scene_v1(resource->payload, limits);
  } catch (const ActorBehaviorSceneIoError &error) {
    fail("Invalid runtime actor-behavior-scene resource: " +
         std::string(error.what()));
  }
  if (scene.level_id != package.level_id) {
    fail("ResolvedLevelPackageV1 level ID disagrees with "
         "ActorBehaviorSceneV1");
  }
  return scene;
}

} // namespace openrc::game
