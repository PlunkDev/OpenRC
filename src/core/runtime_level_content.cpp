#include "openrc/runtime_level_content.hpp"

#include <string>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimeLevelContentError(message);
}

} // namespace

RuntimeLevelContentV1
load_runtime_level_content_v1(const ResolvedLevelPackageV1 &package,
                              const RuntimeLevelContentLimitsV1 limits) {
  if (limits.foundation.required_content_api_version == 0U) {
    fail("Runtime level content API policy must be explicit");
  }

  RuntimeLevelContentV1 result;
  try {
    result.foundation =
        load_runtime_level_foundation_v1(package, limits.foundation);
  } catch (const RuntimeLevelFoundationError &error) {
    fail("Cannot mount the runtime level foundation: " +
         std::string(error.what()));
  }

  try {
    result.render_scene = load_runtime_render_scene_v1(
        package, limits.foundation.required_content_api_version,
        limits.render_scene);
  } catch (const RuntimeRenderSceneError &error) {
    fail("Cannot mount the runtime render scene: " + std::string(error.what()));
  }

  if (result.foundation.level_id != package.level_id ||
      result.foundation.content_api_version != package.content_api_version) {
    fail("Mounted runtime level content has inconsistent package identity");
  }
  return result;
}

} // namespace openrc::game
