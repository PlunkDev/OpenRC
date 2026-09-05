#pragma once

#include "openrc/runtime_level_content.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace openrc::game {

// Stable value-only handles into one validated RuntimeLevelContentV1. Actor
// library IDs are canonical dense indices, so no pointer lifetime is exposed.
struct RuntimePlayerActorResolutionV1 {
  std::uint32_t player_authored_id = 0U;
  std::uint32_t actor_rig_index = 0U;
  std::uint32_t actor_model_index = 0U;
  ActorAffineTransformV1 model_to_entity;

  [[nodiscard]] bool
  operator==(const RuntimePlayerActorResolutionV1 &) const = default;
};

class RuntimePlayerActorError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Resolves only through neutral semantic relationships:
// local player slot -> player authored ID -> actor model key -> model rig key.
// A legacy package that has neither optional actor resource returns nullopt.
// Every partial, missing, or ambiguous relationship is a typed error.
[[nodiscard]] std::optional<RuntimePlayerActorResolutionV1>
resolve_runtime_player_actor_v1(const RuntimeLevelContentV1 &content,
                                std::uint32_t local_player_slot);

} // namespace openrc::game
