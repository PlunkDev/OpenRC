#pragma once

#include "openrc/actor_library_io.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace openrc::game {

class RuntimeActorLibraryError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Mounts an optional neutral actor library from an already resolved level
// package. Absence is retained for compatibility with older three-resource
// packages. Once the resource ID is present its identity, resolved operation,
// payload digest, byte envelope, and complete ActorLibraryV1 body are strict.
// The content API policy is always checked, including on the absence path.
[[nodiscard]] std::optional<ActorLibraryV1>
load_optional_runtime_actor_library_v1(
    const ResolvedLevelPackageV1 &package,
    std::uint32_t required_content_api_version,
    ActorLibraryIoLimitsV1 limits);

} // namespace openrc::game
