#pragma once

#include "openrc/collision_world.hpp"

#include <cstdint>

namespace openrc::game {

using DamageChannelMaskV1 = std::uint32_t;

inline constexpr DamageChannelMaskV1 kDamageChannelMeleeV1 = UINT32_C(1) << 0U;
inline constexpr DamageChannelMaskV1 kDamageChannelProjectileV1 = UINT32_C(1)
                                                                  << 1U;
inline constexpr DamageChannelMaskV1 kDamageChannelExplosiveV1 = UINT32_C(1)
                                                                 << 2U;
inline constexpr DamageChannelMaskV1 kDamageChannelEnvironmentV1 = UINT32_C(1)
                                                                   << 3U;
inline constexpr DamageChannelMaskV1 kDamageChannelKnownMaskV1 =
    kDamageChannelMeleeV1 | kDamageChannelProjectileV1 |
    kDamageChannelExplosiveV1 | kDamageChannelEnvironmentV1;

// Damage geometry uses binary64 point-to-segment arithmetic at runtime. The
// maximum is twice the full signed-int32 Q6 collision-world magnitude, and the
// minimum radius is one Q6 unit. At the maximum coordinate, binary64 still has
// over one million representable steps across the minimum radius. Producers,
// transformed world spheres, and consumers share this exact domain so their
// bounded overlap arithmetic remains deterministic and mutually consumable.
inline constexpr double kGameplayDamageMaximumGeometryMagnitudeV1 =
    67'108'864.0;
inline constexpr double kGameplayDamageMinimumRadiusV1 = 1.0 / 64.0;

[[nodiscard]] constexpr bool
is_single_damage_channel_v1(const DamageChannelMaskV1 channel) noexcept {
  return channel != 0U && (channel & ~kDamageChannelKnownMaskV1) == 0U &&
         (channel & (channel - 1U)) == 0U;
}

// A source-independent damage volume emitted by one deterministic gameplay
// tick. attack_sequence identifies the originating attack and starts at one;
// multiple active ticks of that attack deliberately carry the same sequence.
struct GameplayDamagePulseV1 {
  std::uint64_t attack_sequence = 0U;
  std::uint32_t source_authored_id = 0U;
  DamageChannelMaskV1 damage_channel = 0U;
  std::uint32_t damage = 0U;
  CollisionVectorV1 capsule_start;
  CollisionVectorV1 capsule_end;
  double radius = 0.0;

  [[nodiscard]] bool operator==(const GameplayDamagePulseV1 &) const = default;
};

[[nodiscard]] constexpr bool
is_gameplay_damage_geometry_value_v1(const double value) noexcept {
  return value >= -kGameplayDamageMaximumGeometryMagnitudeV1 &&
         value <= kGameplayDamageMaximumGeometryMagnitudeV1;
}

[[nodiscard]] constexpr bool
is_gameplay_damage_radius_v1(const double radius) noexcept {
  return radius >= kGameplayDamageMinimumRadiusV1 &&
         radius <= kGameplayDamageMaximumGeometryMagnitudeV1;
}

[[nodiscard]] constexpr bool
is_valid_gameplay_damage_pulse_v1(
    const GameplayDamagePulseV1 &pulse) noexcept {
  return pulse.attack_sequence != 0U &&
         is_single_damage_channel_v1(pulse.damage_channel) &&
         pulse.damage != 0U &&
         is_gameplay_damage_geometry_value_v1(pulse.capsule_start.x) &&
         is_gameplay_damage_geometry_value_v1(pulse.capsule_start.y) &&
         is_gameplay_damage_geometry_value_v1(pulse.capsule_start.z) &&
         is_gameplay_damage_geometry_value_v1(pulse.capsule_end.x) &&
         is_gameplay_damage_geometry_value_v1(pulse.capsule_end.y) &&
         is_gameplay_damage_geometry_value_v1(pulse.capsule_end.z) &&
         is_gameplay_damage_radius_v1(pulse.radius);
}

} // namespace openrc::game
