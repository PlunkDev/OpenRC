#pragma once

#include "openrc/collision_world.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kCollisionWorldIoFormatVersionV1 = 1U;
inline constexpr std::uint32_t kCollisionWorldIoHeaderBytesV1 = 0x80U;
inline constexpr std::uint32_t kCollisionWorldIoVertexBytesV1 = 12U;
inline constexpr std::uint32_t kCollisionWorldIoTriangleBytesV1 = 20U;
inline constexpr std::uint32_t kCollisionWorldIoGridCellBytesV1 = 24U;
inline constexpr std::uint32_t kCollisionWorldIoGridReferenceBytesV1 = 4U;

inline constexpr std::string_view kCollisionWorldResourceIdV1 =
    "world/collision";
inline constexpr std::string_view kCollisionWorldResourceTypeIdV1 =
    "openrc.collision-world";
inline constexpr std::uint32_t kCollisionWorldResourceSchemaVersionV1 = 1U;

struct CollisionWorldIoLimitsV1 {
    std::uint64_t max_encoded_bytes = 0U;
    CollisionWorldBuildLimitsV1 world;

    [[nodiscard]] bool operator==(
        const CollisionWorldIoLimitsV1&) const = default;
};

class CollisionWorldIoError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The encoded grid is redundant by design. The encoder independently rebuilds
// it from the mesh and rejects a stale or incomplete CollisionWorldV1.
[[nodiscard]] std::vector<std::byte> encode_collision_world_v1(
    const CollisionWorldV1& world,
    CollisionWorldIoLimitsV1 limits);

// The decoder validates every encoded grid record, independently rebuilds the
// grid from the decoded mesh, and returns only the rebuilt CollisionWorldV1.
[[nodiscard]] CollisionWorldV1 decode_collision_world_v1(
    std::span<const std::byte> bytes,
    CollisionWorldIoLimitsV1 limits);

} // namespace openrc
