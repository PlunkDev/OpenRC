#include "openrc/runtime_actor_animation.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimeActorAnimationError(message);
}

[[nodiscard]] const LevelPackageResourceV1 *
find_actor_animation_resource(const ResolvedLevelPackageV1 &package,
                              const std::uint64_t maximum_payload_bytes) {
  const LevelPackageResourceV1 *found = nullptr;
  for (const auto &resource : package.resources) {
    if (resource.resource_id != kActorAnimationResourceIdV1) {
      continue;
    }
    if (found != nullptr) {
      fail("ResolvedLevelPackageV1 repeats optional resource " +
           std::string(kActorAnimationResourceIdV1));
    }
    found = &resource;
  }

  if (found == nullptr) {
    return nullptr;
  }
  if (found->type_id != kActorAnimationResourceTypeIdV1 ||
      found->schema_version != kActorAnimationResourceSchemaVersionV1) {
    fail("Optional resource " + std::string(kActorAnimationResourceIdV1) +
         " has an incompatible type or schema");
  }
  if (found->operation != LevelPackageResourceOperationV1::upsert) {
    fail("Optional resource " + std::string(kActorAnimationResourceIdV1) +
         " is not a resolved upsert");
  }
  if (found->payload.size() > maximum_payload_bytes) {
    fail("Optional resource " + std::string(kActorAnimationResourceIdV1) +
         " exceeds its runtime payload limit");
  }
  if (is_zero_prepared_digest_v1(found->payload_sha256) ||
      prepared_content_sha256_v1(found->payload) != found->payload_sha256) {
    fail("Optional resource " + std::string(kActorAnimationResourceIdV1) +
         " has a missing or stale resolved payload digest");
  }
  return found;
}

} // namespace

std::optional<ActorAnimationBankV1>
load_optional_runtime_actor_animation_bank_v1(
    const ResolvedLevelPackageV1 &package,
    const std::uint32_t required_content_api_version,
    const ActorAnimationIoLimitsV1 limits) {
  if (required_content_api_version == 0U) {
    fail("Runtime actor-animation content API policy must be explicit");
  }
  if (package.content_api_version != required_content_api_version) {
    fail("ResolvedLevelPackageV1 uses an incompatible content API version");
  }

  const auto *resource =
      find_actor_animation_resource(package, limits.max_encoded_bytes);
  if (resource == nullptr) {
    return std::nullopt;
  }
  try {
    return decode_actor_animation_bank_v1(resource->payload, limits);
  } catch (const ActorAnimationIoError &error) {
    fail("Invalid runtime actor-animation resource: " +
         std::string(error.what()));
  }
}

} // namespace openrc::game
