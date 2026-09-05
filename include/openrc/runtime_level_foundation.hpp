#pragma once

#include "openrc/collision_world_io.hpp"
#include "openrc/level_bootstrap.hpp"
#include "openrc/player_simulation.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace openrc::game {

// Runtime package compatibility is policy rather than a global constant. This
// keeps older native runtimes from silently consuming a newer content API.
struct RuntimeLevelFoundationLimitsV1 {
  std::uint32_t required_content_api_version = 0U;
  CollisionWorldIoLimitsV1 collision;
  LevelBootstrapV1Limits bootstrap;
};

// Source-independent state required before scene, entity, and audio resources
// are mounted. build_id is retained for diagnostics and replay metadata only.
struct RuntimeLevelFoundationV1 {
  std::uint32_t level_id = 0U;
  std::uint32_t content_api_version = 0U;
  std::string build_id;
  CollisionWorldV1 collision_world;
  LevelBootstrapV1 bootstrap;

  [[nodiscard]] bool
  operator==(const RuntimeLevelFoundationV1 &) const = default;
};

class RuntimeLevelFoundationError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Loads only the two mandatory foundation resources. Other resolved resources
// remain valid and are deliberately ignored here so scene/audio schemas can be
// mounted by their own bounded loaders.
[[nodiscard]] RuntimeLevelFoundationV1
load_runtime_level_foundation_v1(const ResolvedLevelPackageV1 &package,
                                 RuntimeLevelFoundationLimitsV1 limits);

// Constructs the first deterministic player state from either the authored
// default spawn or an explicit authored spawn ID. The package owns the death
// height; the runtime owns controller and fixed-tick policy.
[[nodiscard]] PlayerSimulationV1 make_runtime_level_player_simulation_v1(
    const RuntimeLevelFoundationV1 &foundation,
    CharacterControllerProfileV1 character_profile,
    std::uint32_t fixed_ticks_per_second,
    std::optional<std::uint32_t> spawn_point_id = std::nullopt,
    std::uint64_t next_tick_index = 0U);

} // namespace openrc::game
