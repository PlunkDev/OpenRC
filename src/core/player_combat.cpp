#include "openrc/player_combat.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const char *const message) {
  throw PlayerCombatError(message);
}

[[nodiscard]] bool key_character(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('_') ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('/');
}

void validate_semantic_key(const std::string_view value) {
  if (value.empty() || value.size() > kPlayerCombatMaximumSemanticKeyBytesV1 ||
      value.front() == '/' || value.back() == '/') {
    fail("A player-combat semantic key is not canonical");
  }
  for (const char character : value) {
    if (!key_character(static_cast<unsigned char>(character))) {
      fail("A player-combat semantic key contains a non-canonical character");
    }
  }

  std::size_t component_begin = 0U;
  while (component_begin < value.size()) {
    const auto separator = value.find('/', component_begin);
    const auto component_end =
        separator == std::string_view::npos ? value.size() : separator;
    const auto component =
        value.substr(component_begin, component_end - component_begin);
    if (component.empty() || component == "." || component == "..") {
      fail("A player-combat semantic key contains an unsafe component");
    }
    if (separator == std::string_view::npos) {
      break;
    }
    component_begin = separator + 1U;
  }
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail("The player-combat phase duration overflows uint64_t");
  }
  return left + right;
}

[[nodiscard]] bool valid_phase(const PlayerCombatPhaseV1 phase) noexcept {
  switch (phase) {
  case PlayerCombatPhaseV1::idle:
  case PlayerCombatPhaseV1::startup:
  case PlayerCombatPhaseV1::active:
  case PlayerCombatPhaseV1::recovery:
    return true;
  }
  return false;
}

void validate_snapshot_for_profile(const PlayerCombatSnapshotV1 &snapshot,
                                   const PlayerCombatProfileV1 &profile) {
  validate_player_combat_snapshot_v1(snapshot);
  switch (snapshot.phase) {
  case PlayerCombatPhaseV1::idle:
    return;
  case PlayerCombatPhaseV1::startup:
    if (profile.startup_ticks == 0U ||
        snapshot.phase_ticks_remaining > profile.startup_ticks) {
      fail("A player-combat snapshot exceeds the startup phase");
    }
    return;
  case PlayerCombatPhaseV1::active:
    if (snapshot.phase_ticks_remaining > profile.active_ticks) {
      fail("A player-combat snapshot exceeds the active phase");
    }
    return;
  case PlayerCombatPhaseV1::recovery:
    if (profile.recovery_ticks == 0U ||
        snapshot.phase_ticks_remaining > profile.recovery_ticks) {
      fail("A player-combat snapshot exceeds the recovery phase");
    }
    return;
  }
  fail("A player-combat snapshot has an unknown phase");
}

[[nodiscard]] PlayerCombatProfileV1
validated_profile(PlayerCombatProfileV1 profile) {
  validate_player_combat_profile_v1(profile);
  return profile;
}

[[nodiscard]] double canonical_number(const double value) noexcept {
  return value == 0.0 ? 0.0 : value;
}

[[nodiscard]] CollisionVectorV1
capsule_point(const CollisionVectorV1 feet_position, const double forward_x,
              const double forward_y, const double forward_distance,
              const double vertical_offset) {
  CollisionVectorV1 result{
      feet_position.x + forward_x * forward_distance,
      feet_position.y + forward_y * forward_distance,
      feet_position.z + vertical_offset,
  };
  if (!std::isfinite(result.x) || !std::isfinite(result.y) ||
      !std::isfinite(result.z)) {
    fail("A player-combat damage capsule exceeds the finite world domain");
  }
  result.x = canonical_number(result.x);
  result.y = canonical_number(result.y);
  result.z = canonical_number(result.z);
  return result;
}

[[nodiscard]] GameplayDamagePulseV1
damage_pulse(const PlayerCombatProfileV1 &profile,
             const PlayerSimulationSnapshotV1 &player,
             const std::uint64_t attack_sequence) {
  const auto forward_x = std::cos(player.facing_yaw_radians);
  const auto forward_y = std::sin(player.facing_yaw_radians);
  if (!std::isfinite(forward_x) || !std::isfinite(forward_y)) {
    fail("A player-combat facing direction is not finite");
  }

  GameplayDamagePulseV1 result;
  result.attack_sequence = attack_sequence;
  result.source_authored_id = profile.source_authored_id;
  result.damage_channel = profile.damage_channel;
  result.damage = profile.damage;
  result.capsule_start =
      capsule_point(player.character.feet_position, forward_x, forward_y,
                    profile.forward_start, profile.vertical_offset);
  result.capsule_end =
      capsule_point(player.character.feet_position, forward_x, forward_y,
                    profile.forward_end, profile.vertical_offset);
  result.radius = profile.radius;
  if (!is_valid_gameplay_damage_pulse_v1(result)) {
    fail("A player-combat damage pulse exceeds the deterministic geometry "
         "domain");
  }
  return result;
}

void hash_byte(std::uint64_t &hash, const std::uint8_t value) noexcept {
  hash ^= value;
  hash *= UINT64_C(1099511628211);
}

void hash_u32(std::uint64_t &hash, const std::uint32_t value) noexcept {
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
    hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

void hash_u64(std::uint64_t &hash, const std::uint64_t value) noexcept {
  for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
    hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

} // namespace

void validate_player_combat_profile_v1(const PlayerCombatProfileV1 &profile) {
  validate_semantic_key(profile.semantic_key);
  if (!is_single_damage_channel_v1(profile.damage_channel)) {
    fail("A player-combat profile requires exactly one known damage channel");
  }
  if (profile.damage == 0U || profile.active_ticks == 0U) {
    fail("A player-combat profile requires positive damage and active ticks");
  }

  auto total_ticks = static_cast<std::uint64_t>(profile.startup_ticks);
  total_ticks = checked_add(total_ticks, profile.active_ticks);
  total_ticks = checked_add(total_ticks, profile.recovery_ticks);
  if (total_ticks > kPlayerCombatMaximumAttackTicksV1) {
    fail("A player-combat profile exceeds the hard attack-duration limit");
  }

  if (!is_gameplay_damage_geometry_value_v1(profile.forward_start) ||
      !is_gameplay_damage_geometry_value_v1(profile.forward_end) ||
      !is_gameplay_damage_geometry_value_v1(profile.vertical_offset) ||
      !is_gameplay_damage_radius_v1(profile.radius) ||
      profile.forward_start < 0.0 ||
      profile.forward_end < profile.forward_start) {
    fail("A player-combat profile has invalid deterministic capsule limits");
  }
}

void validate_player_combat_snapshot_v1(
    const PlayerCombatSnapshotV1 &snapshot) {
  if (!valid_phase(snapshot.phase)) {
    fail("A player-combat snapshot has an unknown phase");
  }
  if (snapshot.phase == PlayerCombatPhaseV1::idle) {
    if (snapshot.phase_ticks_remaining != 0U) {
      fail("An idle player-combat snapshot has remaining phase ticks");
    }
    return;
  }
  if (snapshot.attack_sequence == 0U || snapshot.phase_ticks_remaining == 0U ||
      snapshot.phase_ticks_remaining > kPlayerCombatMaximumAttackTicksV1) {
    fail("An attacking player-combat snapshot has incomplete phase state");
  }
}

std::uint64_t
hash_player_combat_snapshot_v1(const PlayerCombatSnapshotV1 &snapshot) {
  validate_player_combat_snapshot_v1(snapshot);
  std::uint64_t hash = UINT64_C(14695981039346656037);
  hash_u64(hash, snapshot.next_tick_index);
  hash_u64(hash, snapshot.attack_sequence);
  hash_byte(hash, static_cast<std::uint8_t>(snapshot.phase));
  hash_u32(hash, snapshot.phase_ticks_remaining);
  return hash;
}

PlayerCombatV1::PlayerCombatV1(PlayerCombatProfileV1 profile,
                               const std::uint64_t next_tick_index)
    : profile_(validated_profile(std::move(profile))),
      next_tick_index_(next_tick_index) {}

PlayerCombatV1::PlayerCombatV1(PlayerCombatProfileV1 profile,
                               const PlayerCombatSnapshotV1 &snapshot)
    : profile_(validated_profile(std::move(profile))),
      next_tick_index_(snapshot.next_tick_index),
      attack_sequence_(snapshot.attack_sequence), phase_(snapshot.phase),
      phase_ticks_remaining_(snapshot.phase_ticks_remaining) {
  validate_snapshot_for_profile(snapshot, profile_);
}

PlayerCombatStepV1 PlayerCombatV1::fixed_update(
    const GameInputCommandV1 &input,
    const PlayerSimulationSnapshotV1 &post_movement_player) {
  try {
    validate_game_input_command_v1(input);
  } catch (const GameInputError &error) {
    throw PlayerCombatError(error.what());
  }
  try {
    validate_player_simulation_snapshot_v1(post_movement_player);
  } catch (const PlayerSimulationError &error) {
    throw PlayerCombatError(error.what());
  }
  if (input.tick_index != next_tick_index_) {
    fail("Player combat input must be consumed in exact fixed-tick order");
  }
  if (next_tick_index_ == std::numeric_limits<std::uint64_t>::max()) {
    fail("The player-combat tick sequence is exhausted");
  }
  if (post_movement_player.next_tick_index != input.tick_index + 1U) {
    fail(
        "Player combat requires the post-movement snapshot for its input tick");
  }

  auto next_attack_sequence = attack_sequence_;
  auto next_phase = phase_;
  auto next_phase_ticks_remaining = phase_ticks_remaining_;
  PlayerCombatStepV1 result;

  const auto primary_action_pressed =
      (input.pressed_buttons &
       game_button_mask_v1(GameButtonV1::primary_action)) != 0U;
  if (next_phase == PlayerCombatPhaseV1::idle && primary_action_pressed) {
    if (next_attack_sequence == std::numeric_limits<std::uint64_t>::max()) {
      fail("The player-combat attack sequence is exhausted");
    }
    ++next_attack_sequence;
    result.attack_started = true;
    if (profile_.startup_ticks != 0U) {
      next_phase = PlayerCombatPhaseV1::startup;
      next_phase_ticks_remaining = profile_.startup_ticks;
    } else {
      next_phase = PlayerCombatPhaseV1::active;
      next_phase_ticks_remaining = profile_.active_ticks;
    }
  }

  switch (next_phase) {
  case PlayerCombatPhaseV1::idle:
    break;
  case PlayerCombatPhaseV1::startup:
    --next_phase_ticks_remaining;
    if (next_phase_ticks_remaining == 0U) {
      next_phase = PlayerCombatPhaseV1::active;
      next_phase_ticks_remaining = profile_.active_ticks;
    }
    break;
  case PlayerCombatPhaseV1::active:
    result.damage_pulse =
        damage_pulse(profile_, post_movement_player, next_attack_sequence);
    --next_phase_ticks_remaining;
    if (next_phase_ticks_remaining == 0U) {
      if (profile_.recovery_ticks != 0U) {
        next_phase = PlayerCombatPhaseV1::recovery;
        next_phase_ticks_remaining = profile_.recovery_ticks;
      } else {
        next_phase = PlayerCombatPhaseV1::idle;
      }
    }
    break;
  case PlayerCombatPhaseV1::recovery:
    --next_phase_ticks_remaining;
    if (next_phase_ticks_remaining == 0U) {
      next_phase = PlayerCombatPhaseV1::idle;
    }
    break;
  }

  // Everything that can fail, including damage-volume arithmetic, has
  // completed. Commit the replay state together so rejected ticks are atomic.
  attack_sequence_ = next_attack_sequence;
  phase_ = next_phase;
  phase_ticks_remaining_ = next_phase_ticks_remaining;
  ++next_tick_index_;
  return result;
}

PlayerCombatSnapshotV1 PlayerCombatV1::snapshot() const noexcept {
  return {next_tick_index_, attack_sequence_, phase_, phase_ticks_remaining_};
}

const PlayerCombatProfileV1 &PlayerCombatV1::profile() const noexcept {
  return profile_;
}

} // namespace openrc::game
