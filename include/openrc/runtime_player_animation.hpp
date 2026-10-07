#pragma once

#include "openrc/actor_animation_player.hpp"
#include "openrc/actor_library.hpp"
#include "openrc/player_simulation.hpp"
#include "openrc/rac_player_locomotion.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace openrc::game {

// RAC1 PAL v2.00 state-2 animation hysteresis. These are the unscaled
// horizontal pace values used by the original game for source slots 3 and 4.
// Equality deliberately retains the current moving slot.
inline constexpr double kRuntimePlayerSlowToFullHorizontalSpeedV1 =
    static_cast<double>(kRacPlayerSlowToFullAnimationSpeedV1);
inline constexpr double kRuntimePlayerFullToSlowHorizontalSpeedV1 =
    static_cast<double>(kRacPlayerFullToSlowAnimationSpeedV1);

struct RuntimePlayerAnimationProfileV1 {
  std::string idle_clip_key;
  // These two storage names are retained for source compatibility with the
  // existing Windows V1 profile. RuntimePlayerAnimationV1 treats them as the
  // original slot-3 slow and slot-4 full clips through the accessors below.
  std::string walk_clip_key;
  std::string run_clip_key;
  // Retained only to preserve the six-field V1 aggregate layout. The old
  // thresholds were preview guesses and are not used by the recovered RAC1
  // selector.
  double legacy_idle_max_horizontal_speed = 0.0;
  double legacy_run_min_horizontal_speed = 0.0;
  std::uint32_t fixed_ticks_per_second = 0U;
  // Optional airborne roles appended after the V1 fields. Empty keys keep the
  // V1 behavior of holding the last sampled pose while airborne. The intended
  // clips are source slot 7 (state-7 jump entry at 0x224d1c) and source slot
  // 10 (default state-6 fall entry at 0x224aa4). Choosing between them from
  // the neutral snapshot is an OpenRC adapter policy, see
  // docs/RAC_PLAYER_AIRBORNE_V1.md.
  std::string jump_clip_key{};
  std::string fall_clip_key{};
  // Optional state-6 phase roles, used only together with fall_clip_key:
  // source slot 11 (the fall switches to it after frames(18) = 15 PAL frames)
  // and source slot 12 (the landing after that phase starts at source frame
  // 9, or 4 after frames(90) = 75 PAL frames; T3 0x22e584). Empty keys keep
  // the immediate return to the grounded selection.
  std::string long_fall_clip_key{};
  std::string fall_landing_clip_key{};

  [[nodiscard]] std::string_view slow_clip_key() const noexcept {
    return walk_clip_key;
  }
  [[nodiscard]] std::string_view full_clip_key() const noexcept {
    return run_clip_key;
  }
};

class RuntimePlayerAnimationError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Presentation-only deterministic locomotion player. The bank and rig remain
// owned by RuntimeLevelContentV1 and must outlive this object. It reproduces
// the clean-room-confirmed state-0/slot-0 idle and state-2 slot-3/slot-4
// slow/full animation contract. The snapshot does not yet expose the source
// state or previous pad magnitude, so exact state-0 entry (> 0.22) and state-2
// exit (< 0.17) cannot be selected here. A truly stopped horizontal velocity
// is the explicit fallback to idle. When the optional airborne keys are
// empty, airborne ticks hold the last sampled pose instead of guessing a
// source sequence. Otherwise the first airborne tick after leaving the ground
// picks the jump clip for upward vertical velocity and the fall clip for
// none; that clip plays until the player is grounded again, which re-enters
// the grounded selection through slot 3 or idle. With the optional state-6
// phase keys a long fall switches to the slot-11 clip and its landing plays
// the slot-12 clip from the recovered source frame before the grounded
// selection resumes. Airborne time is counted in source updates through the
// same 50-to-60 cadence conversion as playback; that mapping, the missing
// height test and the landing exit are adapter policy.
class RuntimePlayerAnimationV1 final {
public:
  RuntimePlayerAnimationV1(
      const ActorAnimationBankV1 &bank,
      const ActorRigAssetV1 &rig,
      RuntimePlayerAnimationProfileV1 profile,
      ActorAnimationPlaybackLimitsV1 limits);

  void fixed_update(const PlayerSimulationSnapshotV1 &player);

  [[nodiscard]] const ActorPosePaletteV1 &palette() const noexcept;
  [[nodiscard]] std::string_view active_clip_key() const noexcept;
  [[nodiscard]] const ActorAnimationPlaybackStateV1 &playback() const noexcept;

private:
  enum class AirborneRole : std::uint8_t { undecided, hold, jump, fall };

  [[nodiscard]] const ActorAnimationClipV1 &
  select_grounded_clip(const PlayerSimulationSnapshotV1 &player) const;
  [[nodiscard]] const ActorAnimationClipV1 *
  select_airborne_clip(const PlayerSimulationSnapshotV1 &player);
  [[nodiscard]] std::uint64_t source_frames(std::uint64_t runtime_ticks) const;
  [[nodiscard]] bool begin_fall_landing(AirborneRole landed_role,
                                        std::uint64_t airborne_frames);
  [[nodiscard]] bool continue_fall_landing(
      const PlayerSimulationSnapshotV1 &player);
  void restart_at(const ActorAnimationClipV1 &clip, std::uint32_t frame);
  void select(const ActorAnimationClipV1 &clip);
  void restart(const ActorAnimationClipV1 &clip);
  void remap_moving_phase(const ActorAnimationClipV1 &clip,
                          std::uint32_t source_frame_offset);
  void resample();

  const ActorRigAssetV1 *rig_ = nullptr;
  const ActorAnimationClipV1 *idle_ = nullptr;
  const ActorAnimationClipV1 *slow_ = nullptr;
  const ActorAnimationClipV1 *full_ = nullptr;
  const ActorAnimationClipV1 *jump_ = nullptr;
  const ActorAnimationClipV1 *fall_ = nullptr;
  const ActorAnimationClipV1 *long_fall_ = nullptr;
  const ActorAnimationClipV1 *fall_landing_ = nullptr;
  const ActorAnimationClipV1 *active_ = nullptr;
  AirborneRole airborne_role_ = AirborneRole::undecided;
  std::uint64_t airborne_ticks_ = 0U;
  bool landing_active_ = false;
  std::uint64_t landing_ticks_ = 0U;
  std::uint32_t landing_lock_frames_ = 0U;
  RuntimePlayerAnimationProfileV1 profile_;
  ActorAnimationPlaybackLimitsV1 limits_;
  ActorAnimationPlaybackStateV1 playback_;
  ActorPosePaletteV1 palette_;
  std::uint64_t observed_reset_count_ = 0U;
};

} // namespace openrc::game
