#pragma once

#include "openrc/actor_animation_io.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace openrc::game {

class RuntimeActorAnimationError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Mounts an optional neutral actor-animation bank from an already resolved
// level package. Absence is retained for compatibility with older packages.
// Once the resource ID is present its identity, resolved operation, payload
// digest, byte envelope, and complete ActorAnimationBankV1 body are strict.
// The content API policy is always checked, including on the absence path.
[[nodiscard]] std::optional<ActorAnimationBankV1>
load_optional_runtime_actor_animation_bank_v1(
    const ResolvedLevelPackageV1 &package,
    std::uint32_t required_content_api_version,
    ActorAnimationIoLimitsV1 limits);

} // namespace openrc::game
