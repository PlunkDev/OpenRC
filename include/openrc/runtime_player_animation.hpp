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
// is the explicit fallback to idle. Unsupported airborne states hold the last
// sampled pose instead of guessing a source sequence.
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
  [[nodiscard]] const ActorAnimationClipV1 &
  select_grounded_clip(const PlayerSimulationSnapshotV1 &player) const;
  void select(const ActorAnimationClipV1 &clip);
  void restart(const ActorAnimationClipV1 &clip);
  void remap_moving_phase(const ActorAnimationClipV1 &clip,
                          std::uint32_t source_frame_offset);
  void resample();

  const ActorRigAssetV1 *rig_ = nullptr;
  const ActorAnimationClipV1 *idle_ = nullptr;
  const ActorAnimationClipV1 *slow_ = nullptr;
  const ActorAnimationClipV1 *full_ = nullptr;
  const ActorAnimationClipV1 *active_ = nullptr;
  RuntimePlayerAnimationProfileV1 profile_;
  ActorAnimationPlaybackLimitsV1 limits_;
  ActorAnimationPlaybackStateV1 playback_;
  ActorPosePaletteV1 palette_;
  std::uint64_t observed_reset_count_ = 0U;
};

} // namespace openrc::game
