#pragma once

#include "openrc/destructible_scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kDestructibleSceneIoFormatVersionV1 = 1U;
inline constexpr std::uint32_t kDestructibleSceneIoPayloadTypeV1 = 1U;
inline constexpr std::uint32_t kDestructibleSceneIoHeaderBytesV1 = 0x80U;
inline constexpr std::uint32_t kDestructibleSceneIoDestructibleBytesV1 = 48U;
inline constexpr std::uint32_t kDestructibleSceneIoDropBytesV1 = 32U;

inline constexpr std::string_view kDestructibleSceneResourceIdV1 =
    "world/destructibles";
inline constexpr std::string_view kDestructibleSceneResourceTypeIdV1 =
    "openrc.destructible-scene";
inline constexpr std::uint32_t kDestructibleSceneResourceSchemaVersionV1 = 1U;

struct DestructibleSceneIoLimitsV1 {
  std::uint64_t max_encoded_bytes = 0U;
  DestructibleSceneLimitsV1 scene;

  [[nodiscard]] bool
  operator==(const DestructibleSceneIoLimitsV1 &) const = default;
};

class DestructibleSceneIoError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// The encoder canonicalizes definition/drop order and local signed zero.
// Every scalar is written explicitly in little-endian order; native object
// layouts are never copied into the payload.
[[nodiscard]] std::vector<std::byte>
encode_destructible_scene_v1(const DestructibleSceneV1 &scene,
                             DestructibleSceneIoLimitsV1 limits);

// The decoder bounds counts and aggregate key bytes before allocating. Only
// the exact canonical V1 layout, flags, reserved zeros, nested drop and key
// partitions, and final byte size are accepted.
[[nodiscard]] DestructibleSceneV1
decode_destructible_scene_v1(std::span<const std::byte> bytes,
                             DestructibleSceneIoLimitsV1 limits);

} // namespace openrc
