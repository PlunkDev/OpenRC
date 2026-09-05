#pragma once

#include "openrc/damage.hpp"
#include "openrc/game_input.hpp"
#include "openrc/player_simulation.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace openrc::game {

inline constexpr std::uint32_t kPlayerCombatMaximumSemanticKeyBytesV1 = 256U;
inline constexpr std::uint32_t kPlayerCombatMaximumAttackTicksV1 = 1'000'000U;

struct PlayerCombatProfileV1 {
  std::string semantic_key;
  std::uint32_t source_authored_id = 0U;
  DamageChannelMaskV1 damage_channel = 0U;
  std::uint32_t damage = 0U;
  std::uint32_t startup_ticks = 0U;
  std::uint32_t active_ticks = 0U;
  std::uint32_t recovery_ticks = 0U;
  double forward_start = 0.0;
  double forward_end = 0.0;
  double vertical_offset = 0.0;
  double radius = 0.0;

  [[nodiscard]] bool operator==(const PlayerCombatProfileV1 &) const = default;
};

enum class PlayerCombatPhaseV1 : std::uint8_t {
  idle = 0U,
  startup,
  active,
  recovery,
};

// phase_ticks_remaining describes the state at the beginning of the next
// fixed tick. It is zero exactly while idle. attack_sequence is the latest
// assigned attack identity, so zero means that no attack has started yet.
struct PlayerCombatSnapshotV1 {
  std::uint64_t next_tick_index = 0U;
  std::uint64_t attack_sequence = 0U;
  PlayerCombatPhaseV1 phase = PlayerCombatPhaseV1::idle;
  std::uint32_t phase_ticks_remaining = 0U;

  [[nodiscard]] bool operator==(const PlayerCombatSnapshotV1 &) const = default;
};

struct PlayerCombatStepV1 {
  bool attack_started = false;
  std::optional<GameplayDamagePulseV1> damage_pulse;

  [[nodiscard]] bool operator==(const PlayerCombatStepV1 &) const = default;
};

class PlayerCombatError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void validate_player_combat_profile_v1(const PlayerCombatProfileV1 &profile);
void validate_player_combat_snapshot_v1(const PlayerCombatSnapshotV1 &snapshot);

[[nodiscard]] std::uint64_t
hash_player_combat_snapshot_v1(const PlayerCombatSnapshotV1 &snapshot);

// Phase semantics are inclusive of the tick being processed:
// - a primary-action pressed edge starts an attack only when the tick begins
//   idle; held state alone never starts or repeats an attack;
// - the starting tick consumes the first startup tick, or, when startup is
//   zero, the first active tick and emits its pulse immediately;
// - every active tick emits exactly one pulse, then recovery ticks are
//   consumed; a press observed while non-idle is not buffered;
// - after the last active tick (when recovery is zero), or the last recovery
//   tick, the snapshot for the next tick is idle.
class PlayerCombatV1 final {
public:
  explicit PlayerCombatV1(PlayerCombatProfileV1 profile,
                          std::uint64_t next_tick_index = 0U);
  PlayerCombatV1(PlayerCombatProfileV1 profile,
                 const PlayerCombatSnapshotV1 &snapshot);

  // post_movement_player must be the PlayerSimulationV1 snapshot produced by
  // input's tick. Its next_tick_index is therefore input.tick_index + 1.
  [[nodiscard]] PlayerCombatStepV1
  fixed_update(const GameInputCommandV1 &input,
               const PlayerSimulationSnapshotV1 &post_movement_player);

  [[nodiscard]] PlayerCombatSnapshotV1 snapshot() const noexcept;
  [[nodiscard]] const PlayerCombatProfileV1 &profile() const noexcept;

private:
  PlayerCombatProfileV1 profile_;
  std::uint64_t next_tick_index_ = 0U;
  std::uint64_t attack_sequence_ = 0U;
  PlayerCombatPhaseV1 phase_ = PlayerCombatPhaseV1::idle;
  std::uint32_t phase_ticks_remaining_ = 0U;
};

} // namespace openrc::game
