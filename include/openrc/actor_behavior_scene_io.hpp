#pragma once

#include "openrc/actor_behavior_scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kActorBehaviorSceneIoFormatVersionV1 = 1U;
inline constexpr std::uint32_t kActorBehaviorSceneIoPayloadTypeV1 = 1U;
inline constexpr std::uint32_t kActorBehaviorSceneIoHeaderBytesV1 = 0x100U;
inline constexpr std::uint32_t kActorBehaviorSceneIoProgramBytesV1 = 208U;
inline constexpr std::uint32_t kActorBehaviorSceneIoFieldBytesV1 = 32U;
inline constexpr std::uint32_t kActorBehaviorSceneIoAnimationImportBytesV1 =
    80U;
inline constexpr std::uint32_t kActorBehaviorSceneIoRandomImportBytesV1 = 64U;
inline constexpr std::uint32_t kActorBehaviorSceneIoRandomStreamBytesV1 = 64U;
inline constexpr std::uint32_t kActorBehaviorSceneIoRandomStateWordBytesV1 = 4U;
inline constexpr std::uint32_t kActorBehaviorSceneIoInstanceBytesV1 = 48U;
inline constexpr std::uint32_t kActorBehaviorSceneIoInitialValueBytesV1 = 24U;
inline constexpr std::uint32_t kActorBehaviorSceneIoInitialAnimationBytesV1 =
    16U;

inline constexpr std::string_view kActorBehaviorSceneResourceIdV1 =
    "world/actor-behaviors";
inline constexpr std::string_view kActorBehaviorSceneResourceTypeIdV1 =
    "openrc.actor-behavior-scene";
inline constexpr std::uint32_t kActorBehaviorSceneResourceSchemaVersionV1 = 1U;

struct ActorBehaviorSceneIoLimitsV1 {
  std::uint64_t max_encoded_bytes = 0U;
  ActorBehaviorSceneLimitsV1 scene;

  [[nodiscard]] bool
  operator==(const ActorBehaviorSceneIoLimitsV1 &) const = default;
};

class ActorBehaviorSceneIoError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// V1 is encoded field-by-field in little-endian order with exact flattened
// table and string partitions. Native C++ object layouts are never copied.
[[nodiscard]] std::vector<std::byte>
encode_actor_behavior_scene_v1(const ActorBehaviorSceneV1 &scene,
                               ActorBehaviorSceneIoLimitsV1 limits);

// Counts and aggregates are bounded before allocation. Alternate layouts,
// unknown values, non-zero reserved data, stale digests, and trailing bytes
// fail closed.
[[nodiscard]] ActorBehaviorSceneV1
decode_actor_behavior_scene_v1(std::span<const std::byte> bytes,
                               ActorBehaviorSceneIoLimitsV1 limits);

} // namespace openrc
