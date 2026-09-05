#pragma once

#include "openrc/collision_world.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kLevelBootstrapFormatVersionV1 = 1U;
inline constexpr std::uint32_t kLevelBootstrapHeaderBytesV1 = 64U;
inline constexpr std::uint32_t kLevelBootstrapSpawnPointBytesV1 = 40U;

// Stable LevelPackageV1 identity for the neutral bootstrap payload.
inline constexpr std::string_view kLevelBootstrapResourceIdV1 =
    "world/bootstrap";
inline constexpr std::string_view kLevelBootstrapResourceTypeIdV1 =
    "openrc.level-bootstrap";
inline constexpr std::uint32_t kLevelBootstrapResourceSchemaVersionV1 = 1U;

struct LevelBootstrapV1Limits {
  std::uint64_t max_input_bytes = 0U;
  std::uint32_t max_spawn_points = 0U;
};

// Neutral coordinates are right-handed and Z-up. feet_position is the world
// contact point below the player capsule; facing_yaw_radians rotates about +Z.
struct LevelSpawnPointV1 {
  std::uint32_t id = 0U;
  CollisionVectorV1 feet_position;
  double facing_yaw_radians = 0.0;

  [[nodiscard]] bool operator==(const LevelSpawnPointV1 &) const = default;
};

struct LevelBootstrapV1 {
  std::uint32_t level_id = 0U;
  // Absolute world-space Z. It is not relative to any spawn or level origin.
  double death_height_world = 0.0;
  std::uint32_t default_spawn_id = 0U;
  // Canonical serialized order is strictly ascending by id.
  std::vector<LevelSpawnPointV1> spawn_points;

  [[nodiscard]] bool operator==(const LevelBootstrapV1 &) const = default;
};

class LevelBootstrapV1Error final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// The writer sorts spawn points by id and canonicalizes signed zero to +0.
// The reader accepts only the resulting canonical order and representation.
[[nodiscard]] std::vector<std::byte>
encode_level_bootstrap_v1(const LevelBootstrapV1 &bootstrap,
                          LevelBootstrapV1Limits limits);

[[nodiscard]] LevelBootstrapV1
parse_level_bootstrap_v1(std::span<const std::byte> bytes,
                         LevelBootstrapV1Limits limits);

[[nodiscard]] const LevelSpawnPointV1 *
find_level_spawn_point_v1(const LevelBootstrapV1 &bootstrap,
                          std::uint32_t spawn_id) noexcept;

} // namespace openrc
