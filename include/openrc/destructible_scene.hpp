#pragma once

#include "openrc/damage.hpp"
#include "openrc/game_world.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kDestructibleSceneSchemaVersionV1 = 1U;

// V1 definitions and drops have no optional flag semantics. Keeping explicit
// fields lets readers reject extensions which they do not understand.
inline constexpr std::uint32_t kDestructibleDefinitionKnownFlagsV1 = 0U;
inline constexpr std::uint32_t kDestructibleDropKnownFlagsV1 = 0U;

// A semantic item grant emitted when its owning destructible is destroyed.
// Within one definition, canonical order is strict lexicographic item_key
// order; duplicate keys are not silently combined.
struct DestructibleDropV1 {
  std::string item_key;
  std::uint32_t amount = 0U;
  std::uint32_t flags = 0U;

  [[nodiscard]] bool operator==(const DestructibleDropV1 &) const = default;
};

// authored_id references the entity with the same sparse authored identifier
// in EntitySceneV1. The hit sphere is expressed in that entity's local space.
struct DestructibleDefinitionV1 {
  std::uint32_t authored_id = 0U;
  std::uint32_t max_health = 0U;
  std::uint32_t accepted_damage_channels = 0U;
  std::array<float, 3U> local_hit_center{};
  float hit_radius = 0.0F;
  std::uint32_t flags = 0U;
  std::vector<DestructibleDropV1> drops;

  [[nodiscard]] bool
  operator==(const DestructibleDefinitionV1 &) const = default;
};

struct DestructibleSceneV1 {
  std::uint32_t schema_version = kDestructibleSceneSchemaVersionV1;
  game::LevelIdV1 level_id = 0U;

  // Canonical order is strictly ascending by referenced authored_id.
  std::vector<DestructibleDefinitionV1> destructibles;

  [[nodiscard]] bool operator==(const DestructibleSceneV1 &) const = default;
};

struct DestructibleSceneLimitsV1 {
  std::uint32_t max_destructibles = 0U;
  std::uint32_t max_total_drops = 0U;
  std::uint32_t max_drops_per_destructible = 0U;
  std::uint32_t max_item_key_bytes = 0U;
  std::uint64_t max_total_key_bytes = 0U;
  std::uint32_t max_health = 0U;
  std::uint32_t max_drop_amount = 0U;
  float max_absolute_local_hit_center = 0.0F;
  float max_hit_radius = 0.0F;

  [[nodiscard]] bool
  operator==(const DestructibleSceneLimitsV1 &) const = default;
};

class DestructibleSceneError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Requires the exact V1 schema, strict authored-ID and per-definition drop
// order, known flags and damage channels, canonical semantic item keys,
// finite canonical and explicitly bounded local hit centers, positive finite
// bounded radii, and positive bounded health/drop amounts. Every caller limit
// is mandatory.
void validate_destructible_scene_v1(const DestructibleSceneV1 &scene,
                                    DestructibleSceneLimitsV1 limits);

// Sorts definitions and their drop tables and maps signed zero in local hit
// centers to positive zero, then applies the same strict validation. Duplicate
// authored IDs and duplicate per-definition item keys are never merged.
[[nodiscard]] DestructibleSceneV1
canonicalize_destructible_scene_v1(DestructibleSceneV1 scene,
                                   DestructibleSceneLimitsV1 limits);

} // namespace openrc
