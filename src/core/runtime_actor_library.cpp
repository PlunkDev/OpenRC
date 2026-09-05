#include "openrc/runtime_actor_library.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimeActorLibraryError(message);
}

[[nodiscard]] const LevelPackageResourceV1 *find_actor_library_resource(
    const ResolvedLevelPackageV1 &package,
    const std::uint64_t maximum_payload_bytes) {
  const LevelPackageResourceV1 *found = nullptr;
  for (const auto &resource : package.resources) {
    if (resource.resource_id != kActorLibraryResourceIdV1) {
      continue;
    }
    if (found != nullptr) {
      fail("ResolvedLevelPackageV1 repeats optional resource " +
           std::string(kActorLibraryResourceIdV1));
    }
    found = &resource;
  }

  if (found == nullptr) {
    return nullptr;
  }
  if (found->type_id != kActorLibraryResourceTypeIdV1 ||
      found->schema_version != kActorLibraryResourceSchemaVersionV1) {
    fail("Optional resource " + std::string(kActorLibraryResourceIdV1) +
         " has an incompatible type or schema");
  }
  if (found->operation != LevelPackageResourceOperationV1::upsert) {
    fail("Optional resource " + std::string(kActorLibraryResourceIdV1) +
         " is not a resolved upsert");
  }
  if (found->payload.size() > maximum_payload_bytes) {
    fail("Optional resource " + std::string(kActorLibraryResourceIdV1) +
         " exceeds its runtime payload limit");
  }
  if (is_zero_prepared_digest_v1(found->payload_sha256) ||
      prepared_content_sha256_v1(found->payload) != found->payload_sha256) {
    fail("Optional resource " + std::string(kActorLibraryResourceIdV1) +
         " has a missing or stale resolved payload digest");
  }
  return found;
}

} // namespace

std::optional<ActorLibraryV1> load_optional_runtime_actor_library_v1(
    const ResolvedLevelPackageV1 &package,
    const std::uint32_t required_content_api_version,
    const ActorLibraryIoLimitsV1 limits) {
  if (required_content_api_version == 0U) {
    fail("Runtime actor-library content API policy must be explicit");
  }
  if (package.content_api_version != required_content_api_version) {
    fail("ResolvedLevelPackageV1 uses an incompatible content API version");
  }

  const auto *resource =
      find_actor_library_resource(package, limits.max_encoded_bytes);
  if (resource == nullptr) {
    return std::nullopt;
  }
  try {
    return decode_actor_library_v1(resource->payload, limits);
  } catch (const ActorLibraryIoError &error) {
    fail("Invalid runtime actor-library resource: " +
         std::string(error.what()));
  }
}

} // namespace openrc::game
