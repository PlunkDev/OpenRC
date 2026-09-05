#pragma once

#include "openrc/actor_library_io.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <span>
#include <stdexcept>
#include <string_view>

namespace openrc {

// Stable logical compiler-pass identity. This is provenance, not a host
// executable name or version-dependent build path.
inline constexpr std::string_view kLevelActorLibraryCompilePassV1 =
    "compiler/openrc/actor-library-v1";

class LevelActorLibraryCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Encodes one neutral ActorLibraryV1 and attaches it to an existing canonical
// base package. Every supplied record must identify real source bytes
// (iso_range or prepared_resource); the compiler-pass record is added here.
// The returned package and actor payload have both survived canonical parsing.
[[nodiscard]] LevelPackageV1 attach_actor_library_to_level_package_v1(
    LevelPackageV1 base_package, const ActorLibraryV1 &library,
    std::span<const LevelPackageProvenanceV1> source_provenance,
    ActorLibraryIoLimitsV1 actor_io_limits,
    LevelPackageV1Limits package_limits);

} // namespace openrc
