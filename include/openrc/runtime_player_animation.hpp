#pragma once

#include "openrc/actor_animation_player.hpp"
#include "openrc/actor_library.hpp"
#include "openrc/player_simulation.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace openrc::game {

struct RuntimePlayerAnimationProfileV1 {
  std::string idle_clip_key;
  std::string walk_clip_key;
  std::string run_clip_key;
  double idle_max_horizontal_speed = 0.0;
  double run_min_horizontal_speed = 0.0;
  std::uint32_t fixed_ticks_per_second = 0U;
};

class RuntimePlayerAnimationError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Presentation-only deterministic locomotion player. The bank and rig remain
// owned by RuntimeLevelContentV1 and must outlive this object. Only the three
// clean-room-confirmed idle/walk/run semantics are selected; unsupported
// airborne states hold the last sampled pose instead of guessing a source
// sequence.
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
  void restart(const ActorAnimationClipV1 &clip);
  void resample();

  const ActorRigAssetV1 *rig_ = nullptr;
  const ActorAnimationClipV1 *idle_ = nullptr;
  const ActorAnimationClipV1 *walk_ = nullptr;
  const ActorAnimationClipV1 *run_ = nullptr;
  const ActorAnimationClipV1 *active_ = nullptr;
  RuntimePlayerAnimationProfileV1 profile_;
  ActorAnimationPlaybackLimitsV1 limits_;
  ActorAnimationPlaybackStateV1 playback_;
  ActorPosePaletteV1 palette_;
  std::uint64_t observed_reset_count_ = 0U;
};

} // namespace openrc::game
