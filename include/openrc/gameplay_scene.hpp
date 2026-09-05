#pragma once

#include "openrc/game_world.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kGameplaySceneSchemaVersionV1 = 1U;

// V1 collectibles are consumed by overlap only. No flags are currently
// defined; retaining the field makes future semantics explicit and lets V1
// readers reject extensions they do not understand.
inline constexpr std::uint32_t kGameplayCollectibleKnownFlagsV1 = 0U;

// authored_id references the entity with the same sparse authored identifier
// in EntitySceneV1. item_key names neutral inventory/progression content and
// does not encode a game-specific object class.
struct GameplayCollectibleV1 {
  std::uint32_t authored_id = 0U;
  std::string item_key;
  // Center of the overlap sphere in the referenced entity's local space.
  std::array<float, 3U> local_center{};
  std::uint32_t amount = 0U;
  float collection_radius = 0.0F;
  std::uint32_t flags = 0U;

  [[nodiscard]] bool operator==(const GameplayCollectibleV1 &) const = default;
};

struct GameplaySceneV1 {
  std::uint32_t schema_version = kGameplaySceneSchemaVersionV1;
  game::LevelIdV1 level_id = 0U;

  // Canonical order is strictly ascending by referenced authored_id.
  std::vector<GameplayCollectibleV1> collectibles;

  [[nodiscard]] bool operator==(const GameplaySceneV1 &) const = default;
};

struct GameplaySceneLimitsV1 {
  std::uint32_t max_collectibles = 0U;
  std::uint32_t max_item_key_bytes = 0U;
  std::uint64_t max_total_key_bytes = 0U;

  [[nodiscard]] bool operator==(const GameplaySceneLimitsV1 &) const = default;
};

class GameplaySceneError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Requires the exact V1 schema, strict authored-ID order, known flags,
// canonical semantic item keys, a finite canonical local center, a non-zero
// amount, and a finite collection radius greater than zero. All caller limits
// are mandatory.
void validate_gameplay_scene_v1(const GameplaySceneV1 &scene,
                                GameplaySceneLimitsV1 limits);

// Sorts by authored_id and canonicalizes signed zero in local centers, then
// applies the same strict validation. Duplicate authored IDs are never
// silently merged.
[[nodiscard]] GameplaySceneV1
canonicalize_gameplay_scene_v1(GameplaySceneV1 scene,
                               GameplaySceneLimitsV1 limits);

} // namespace openrc
