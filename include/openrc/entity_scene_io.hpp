#pragma once

#include "openrc/entity_scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kEntitySceneIoFormatVersionV1 = 1U;
inline constexpr std::uint32_t kEntitySceneIoPayloadTypeV1 = 1U;
inline constexpr std::uint32_t kEntitySceneIoHeaderBytesV1 = 0xa0U;
inline constexpr std::uint32_t kEntitySceneIoDefinitionBytesV1 = 32U;
inline constexpr std::uint32_t kEntitySceneIoTransformBytesV1 = 48U;
inline constexpr std::uint32_t kEntitySceneIoRenderBindingBytesV1 = 16U;
inline constexpr std::uint32_t kEntitySceneIoActorBindingBytesV1 = 64U;
inline constexpr std::uint32_t kEntitySceneIoPlayerBindingBytesV1 = 16U;

inline constexpr std::string_view kEntitySceneResourceIdV1 = "world/entities";
inline constexpr std::string_view kEntitySceneResourceTypeIdV1 =
    "openrc.entity-scene";
inline constexpr std::uint32_t kEntitySceneResourceSchemaVersionV1 = 1U;

struct EntitySceneIoLimitsV1 {
  std::uint64_t max_encoded_bytes = 0U;
  EntitySceneLimitsV1 scene;

  [[nodiscard]] bool operator==(const EntitySceneIoLimitsV1 &) const = default;
};

class EntitySceneIoError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Encoding canonicalizes table order, signed zero, and quaternion sign. Every
// scalar is written explicitly in little-endian order; native object layouts
// are never copied into the payload.
[[nodiscard]] std::vector<std::byte>
encode_entity_scene_v1(const EntitySceneV1 &scene,
                       EntitySceneIoLimitsV1 limits);

// Decoding checks counts and aggregate string bytes against caller limits
// before allocating. Only the exact canonical V1 layout, type, record sizes,
// reserved zeros, table ordering, key partitions, and final byte size pass.
[[nodiscard]] EntitySceneV1
decode_entity_scene_v1(std::span<const std::byte> bytes,
                       EntitySceneIoLimitsV1 limits);

} // namespace openrc
