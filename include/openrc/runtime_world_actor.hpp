#pragma once

#include "openrc/runtime_level_content.hpp"

#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc::game {

inline constexpr std::uint32_t kRuntimeWorldActorMaximumInstancesV1 = 65'536U;
inline constexpr std::string_view
    kRuntimeWorldActorInitialAnimationKeySegmentV1 = "/initial/";

// Stable value-only handles for one non-player actor instance in a mounted
// neutral entity scene. Asset IDs are canonical dense indices; no pointer
// lifetime or RAC source identity crosses this runtime boundary.
struct RuntimeWorldActorResolutionV1 {
  std::uint32_t authored_id = 0U;
  std::uint32_t actor_rig_index = 0U;
  std::uint32_t actor_model_index = 0U;
  ActorAffineTransformV1 model_to_entity;
  WorldTransformV1 entity_to_world;
  bool initially_enabled = false;

  [[nodiscard]] bool
  operator==(const RuntimeWorldActorResolutionV1 &) const = default;
};

class RuntimeWorldActorError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Resolves every non-player actor binding through neutral relationships:
// authored entity -> world transform -> actor model key -> model rig key.
// Results retain canonical authored-ID order. A legacy package with neither
// optional actor nor entity resource returns an empty list; partial or
// ambiguous relationships fail closed.
[[nodiscard]] std::vector<RuntimeWorldActorResolutionV1>
resolve_runtime_world_actors_v1(const RuntimeLevelContentV1 &content);

// Returns the one neutral clip explicitly classified as the actor rig's
// source state-0 entry fallback. The compiler records that classification with
// the reserved `/initial/` semantic-key segment; the runtime never parses or
// dispatches on RAC class IDs or source sequence numbers retained as opaque
// provenance. No classified clip means bind pose. Multiple matches fail
// closed. This fallback is not a substitute for per-instance behavior state.
[[nodiscard]] const ActorAnimationClipV1 *
find_runtime_world_actor_initial_animation_v1(
    const RuntimeLevelContentV1 &content,
    const RuntimeWorldActorResolutionV1 &actor);

} // namespace openrc::game
