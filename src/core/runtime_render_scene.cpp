#include "openrc/runtime_render_scene.hpp"

#include <cstdint>
#include <string>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimeRenderSceneError(message);
}

[[nodiscard]] const LevelPackageResourceV1 &
require_render_scene_resource(const ResolvedLevelPackageV1 &package,
                              const std::uint64_t maximum_payload_bytes) {
  const LevelPackageResourceV1 *found = nullptr;
  for (const auto &resource : package.resources) {
    if (resource.resource_id != kRenderSceneResourceIdV1) {
      continue;
    }
    if (found != nullptr) {
      fail("ResolvedLevelPackageV1 repeats required resource " +
           std::string(kRenderSceneResourceIdV1));
    }
    found = &resource;
  }

  if (found == nullptr) {
    fail("ResolvedLevelPackageV1 is missing required resource " +
         std::string(kRenderSceneResourceIdV1));
  }
  if (found->type_id != kRenderSceneResourceTypeIdV1 ||
      found->schema_version != kRenderSceneResourceSchemaVersionV1) {
    fail("Required resource " + std::string(kRenderSceneResourceIdV1) +
         " has an incompatible type or schema");
  }
  if (found->operation != LevelPackageResourceOperationV1::upsert) {
    fail("Required resource " + std::string(kRenderSceneResourceIdV1) +
         " is not a resolved upsert");
  }
  if (found->payload.size() > maximum_payload_bytes) {
    fail("Required resource " + std::string(kRenderSceneResourceIdV1) +
         " exceeds its runtime payload limit");
  }
  if (is_zero_prepared_digest_v1(found->payload_sha256) ||
      prepared_content_sha256_v1(found->payload) != found->payload_sha256) {
    fail("Required resource " + std::string(kRenderSceneResourceIdV1) +
         " has a missing or stale resolved payload digest");
  }
  return *found;
}

} // namespace

RenderSceneV1 load_runtime_render_scene_v1(
    const ResolvedLevelPackageV1 &package,
    const std::uint32_t required_content_api_version,
    const RenderSceneIoLimitsV1 limits) {
  if (required_content_api_version == 0U) {
    fail("Runtime render-scene content API policy must be explicit");
  }
  if (package.content_api_version != required_content_api_version) {
    fail("ResolvedLevelPackageV1 uses an incompatible content API version");
  }

  const auto &resource =
      require_render_scene_resource(package, limits.max_encoded_bytes);
  try {
    return decode_render_scene_v1(resource.payload, limits);
  } catch (const RenderSceneIoError &error) {
    fail("Invalid runtime render-scene resource: " +
         std::string(error.what()));
  }
}

} // namespace openrc::game
