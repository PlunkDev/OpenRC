#pragma once

#include "openrc/gameplay_scene_io.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace openrc::game {

class RuntimeGameplaySceneResourceError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Mounts an optional neutral gameplay scene from an already resolved level
// package. Absence remains compatible with older packages. Once world/gameplay
// is present, its unique identity, resolved upsert operation, payload digest,
// bounded byte envelope, complete GameplaySceneV1 body, and level identity are
// strict. The content API policy is checked even on the absence path.
[[nodiscard]] std::optional<GameplaySceneV1>
load_optional_runtime_gameplay_scene_resource_v1(
    const ResolvedLevelPackageV1 &package,
    std::uint32_t required_content_api_version, GameplaySceneIoLimitsV1 limits);

} // namespace openrc::game
