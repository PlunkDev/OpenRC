#pragma once

#include "openrc/entity_scene_io.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace openrc::game {

class RuntimeEntitySceneError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Mounts an optional neutral entity scene from an already resolved level
// package. Absence remains compatible with older packages. Once world/entities
// is present, its type/schema, resolved upsert operation, payload digest,
// bounded byte envelope, complete EntitySceneV1 body, and level identity are
// strict. The content API policy is checked even on the absence path.
[[nodiscard]] std::optional<EntitySceneV1>
load_optional_runtime_entity_scene_v1(
    const ResolvedLevelPackageV1 &package,
    std::uint32_t required_content_api_version,
    EntitySceneIoLimitsV1 limits);

} // namespace openrc::game
