#pragma once

#include "openrc/gameplay_scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kGameplaySceneIoFormatVersionV1 = 1U;
inline constexpr std::uint32_t kGameplaySceneIoPayloadTypeV1 = 1U;
inline constexpr std::uint32_t kGameplaySceneIoHeaderBytesV1 = 0x60U;
inline constexpr std::uint32_t kGameplaySceneIoCollectibleBytesV1 = 48U;

inline constexpr std::string_view kGameplaySceneResourceIdV1 = "world/gameplay";
inline constexpr std::string_view kGameplaySceneResourceTypeIdV1 =
    "openrc.gameplay-scene";
inline constexpr std::uint32_t kGameplaySceneResourceSchemaVersionV1 = 1U;

struct GameplaySceneIoLimitsV1 {
  std::uint64_t max_encoded_bytes = 0U;
  GameplaySceneLimitsV1 scene;

  [[nodiscard]] bool
  operator==(const GameplaySceneIoLimitsV1 &) const = default;
};

class GameplaySceneIoError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// The encoder canonicalizes table order and writes every scalar explicitly in
// little-endian order. Native object layouts are never copied into the payload.
[[nodiscard]] std::vector<std::byte>
encode_gameplay_scene_v1(const GameplaySceneV1 &scene,
                         GameplaySceneIoLimitsV1 limits);

// The decoder checks counts and aggregate key bytes before allocating. Only
// the exact canonical V1 layout, flags, reserved zeros, key partition, and
// final byte size are accepted.
[[nodiscard]] GameplaySceneV1
decode_gameplay_scene_v1(std::span<const std::byte> bytes,
                         GameplaySceneIoLimitsV1 limits);

} // namespace openrc
