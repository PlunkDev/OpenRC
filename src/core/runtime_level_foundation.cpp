#include "openrc/runtime_level_foundation.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimeLevelFoundationError(message);
}

[[nodiscard]] const LevelPackageResourceV1 &require_resource(
    const ResolvedLevelPackageV1 &package, const std::string_view resource_id,
    const std::string_view type_id, const std::uint32_t schema_version,
    const std::uint64_t maximum_payload_bytes) {
  const LevelPackageResourceV1 *found = nullptr;
  for (const auto &resource : package.resources) {
    if (resource.resource_id != resource_id) {
      continue;
    }
    if (found != nullptr) {
      fail("ResolvedLevelPackageV1 repeats required resource " +
           std::string(resource_id));
    }
    found = &resource;
  }
  if (found == nullptr) {
    fail("ResolvedLevelPackageV1 is missing required resource " +
         std::string(resource_id));
  }
  if (found->type_id != type_id || found->schema_version != schema_version) {
    fail("Required resource " + std::string(resource_id) +
         " has an incompatible type or schema");
  }
  if (found->operation != LevelPackageResourceOperationV1::upsert) {
    fail("Required resource " + std::string(resource_id) +
         " is not a resolved upsert");
  }
  if (found->payload.size() > maximum_payload_bytes) {
    fail("Required resource " + std::string(resource_id) +
         " exceeds its runtime payload limit");
  }
  if (is_zero_prepared_digest_v1(found->payload_sha256) ||
      prepared_content_sha256_v1(found->payload) != found->payload_sha256) {
    fail("Required resource " + std::string(resource_id) +
         " has a missing or stale resolved payload digest");
  }
  return *found;
}

void validate_spawn_coordinate_range(const LevelBootstrapV1 &bootstrap) {
  for (const auto &spawn : bootstrap.spawn_points) {
    try {
      // Authored float coordinates need not fall exactly on the Q6 lattice.
      // This is solely a checked range conversion for the collision-query
      // boundary; the original neutral double values remain authoritative.
      static_cast<void>(collision_world_position_to_q6_v1(spawn.feet_position));
    } catch (const CollisionWorldError &error) {
      fail("LevelBootstrapV1 spawn " + std::to_string(spawn.id) +
           " is outside the collision coordinate range: " + error.what());
    }
  }
}

[[nodiscard]] const LevelSpawnPointV1 &
select_spawn(const RuntimeLevelFoundationV1 &foundation,
             const std::optional<std::uint32_t> requested_spawn_id) {
  if (foundation.bootstrap.level_id != foundation.level_id) {
    fail("Runtime level foundation identity was modified after loading");
  }
  const auto spawn_id =
      requested_spawn_id.value_or(foundation.bootstrap.default_spawn_id);
  const auto *const spawn =
      find_level_spawn_point_v1(foundation.bootstrap, spawn_id);
  if (spawn == nullptr) {
    fail("The requested runtime spawn ID does not exist");
  }
  if (!(spawn->feet_position.z > foundation.bootstrap.death_height_world)) {
    fail("The requested runtime spawn is not above the death height");
  }
  try {
    static_cast<void>(collision_world_position_to_q6_v1(spawn->feet_position));
  } catch (const CollisionWorldError &error) {
    fail("The requested runtime spawn is outside the collision coordinate "
         "range: " +
         std::string(error.what()));
  }
  return *spawn;
}

void validate_initial_capsule_range(
    const LevelSpawnPointV1 &spawn,
    const CharacterControllerProfileV1 &character_profile) {
  const auto effective_radius =
      character_profile.capsule_radius + character_profile.skin_width;
  try {
    static_cast<void>(collision_world_aabb_to_q6_v1(
        {spawn.feet_position.x - effective_radius,
         spawn.feet_position.y - effective_radius,
         spawn.feet_position.z - character_profile.skin_width -
             character_profile.ground_probe_distance},
        {spawn.feet_position.x + effective_radius,
         spawn.feet_position.y + effective_radius,
         spawn.feet_position.z + character_profile.capsule_height +
             character_profile.skin_width}));
  } catch (const CollisionWorldError &error) {
    fail("The initial player capsule is outside the collision coordinate "
         "range: " +
         std::string(error.what()));
  }
}

} // namespace

RuntimeLevelFoundationV1
load_runtime_level_foundation_v1(const ResolvedLevelPackageV1 &package,
                                 const RuntimeLevelFoundationLimitsV1 limits) {
  if (limits.required_content_api_version == 0U) {
    fail("Runtime foundation content API policy must be explicit");
  }
  if (package.content_api_version != limits.required_content_api_version) {
    fail("ResolvedLevelPackageV1 uses an incompatible content API version");
  }

  const auto &collision_resource = require_resource(
      package, kCollisionWorldResourceIdV1, kCollisionWorldResourceTypeIdV1,
      kCollisionWorldResourceSchemaVersionV1,
      limits.collision.max_encoded_bytes);
  const auto &bootstrap_resource = require_resource(
      package, kLevelBootstrapResourceIdV1, kLevelBootstrapResourceTypeIdV1,
      kLevelBootstrapResourceSchemaVersionV1, limits.bootstrap.max_input_bytes);

  RuntimeLevelFoundationV1 result;
  result.level_id = package.level_id;
  result.content_api_version = package.content_api_version;
  result.build_id = package.build_id;
  try {
    result.collision_world =
        decode_collision_world_v1(collision_resource.payload, limits.collision);
  } catch (const CollisionWorldIoError &error) {
    fail("Invalid runtime collision resource: " + std::string(error.what()));
  }
  try {
    result.bootstrap =
        parse_level_bootstrap_v1(bootstrap_resource.payload, limits.bootstrap);
  } catch (const LevelBootstrapV1Error &error) {
    fail("Invalid runtime bootstrap resource: " + std::string(error.what()));
  }

  if (result.bootstrap.level_id != result.level_id) {
    fail("ResolvedLevelPackageV1 level ID disagrees with LevelBootstrapV1");
  }
  validate_spawn_coordinate_range(result.bootstrap);
  return result;
}

PlayerSimulationV1 make_runtime_level_player_simulation_v1(
    const RuntimeLevelFoundationV1 &foundation,
    CharacterControllerProfileV1 character_profile,
    const std::uint32_t fixed_ticks_per_second,
    const std::optional<std::uint32_t> spawn_point_id,
    const std::uint64_t next_tick_index) {
  const auto &spawn = select_spawn(foundation, spawn_point_id);
  PlayerSimulationProfileV1 profile{
      std::move(character_profile),
      fixed_ticks_per_second,
      foundation.bootstrap.death_height_world,
  };
  try {
    validate_player_simulation_profile_v1(profile);
  } catch (const PlayerSimulationError &error) {
    fail("Invalid runtime player policy: " + std::string(error.what()));
  }
  validate_initial_capsule_range(spawn, profile.character);

  const PlayerCheckpointV1 checkpoint{
      spawn.id,
      spawn.feet_position,
      spawn.facing_yaw_radians,
  };
  try {
    return PlayerSimulationV1(std::move(profile), checkpoint, next_tick_index);
  } catch (const PlayerSimulationError &error) {
    fail("Cannot start player simulation from the level foundation: " +
         std::string(error.what()));
  }
}

} // namespace openrc::game
