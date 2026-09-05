#include "openrc/runtime_player_animation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace openrc::game {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RuntimePlayerAnimationError(message);
}

[[nodiscard]] const ActorAnimationClipV1 &
require_clip(const ActorAnimationBankV1 &bank, const std::string_view key,
             const char *const role) {
  if (key.empty()) {
    fail(std::string("Runtime player ") + role + " animation key is empty");
  }
  const auto *const clip = find_actor_animation_clip_v1(bank, key);
  if (clip == nullptr) {
    fail(std::string("Runtime player animation bank has no ") + role +
         " clip");
  }
  return *clip;
}

void preflight_clip_playback(
    const ActorAnimationClipV1 &clip, const char *const role,
    const std::uint32_t runtime_ticks_per_second,
    const ActorAnimationPlaybackLimitsV1 limits) {
  if (clip.source_updates_per_second == 0U) {
    fail(std::string("Runtime player ") + role +
         " animation has zero source cadence");
  }

  const auto maximum_source_updates =
      (static_cast<std::uint64_t>(clip.source_updates_per_second) +
       runtime_ticks_per_second - 1U) /
      runtime_ticks_per_second;
  if (maximum_source_updates > limits.max_source_updates_per_step) {
    fail(std::string("Runtime player ") + role +
         " animation can exceed the per-step source-update limit");
  }

  float maximum_phase_rate = 0.0F;
  for (const auto &frame : clip.frames) {
    if (!std::isfinite(frame.phase_rate) || frame.phase_rate < 0.0F) {
      fail(std::string("Runtime player ") + role +
           " animation has an invalid phase rate");
    }
    maximum_phase_rate = std::max(maximum_phase_rate, frame.phase_rate);
  }
  const auto maximum_boundaries_per_source_update =
      std::ceil(static_cast<double>(maximum_phase_rate));
  if (!std::isfinite(maximum_boundaries_per_source_update) ||
      maximum_boundaries_per_source_update >
          static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    fail(std::string("Runtime player ") + role +
         " animation phase rate exceeds the playback counter range");
  }
  const auto boundaries_per_source_update =
      static_cast<std::uint64_t>(maximum_boundaries_per_source_update);
  if (boundaries_per_source_update != 0U &&
      maximum_source_updates >
          std::numeric_limits<std::uint64_t>::max() /
              boundaries_per_source_update) {
    fail(std::string("Runtime player ") + role +
         " animation playback bound overflows its counter");
  }
  const auto maximum_frame_boundaries =
      maximum_source_updates * boundaries_per_source_update;
  if (maximum_frame_boundaries > limits.max_frame_advances_per_step) {
    fail(std::string("Runtime player ") + role +
         " animation can exceed the per-step frame-advance limit");
  }
}

} // namespace

RuntimePlayerAnimationV1::RuntimePlayerAnimationV1(
    const ActorAnimationBankV1 &bank, const ActorRigAssetV1 &rig,
    RuntimePlayerAnimationProfileV1 profile,
    const ActorAnimationPlaybackLimitsV1 limits)
    : rig_(&rig), profile_(std::move(profile)), limits_(limits) {
  if (!std::isfinite(profile_.idle_max_horizontal_speed) ||
      profile_.idle_max_horizontal_speed < 0.0 ||
      !std::isfinite(profile_.run_min_horizontal_speed) ||
      profile_.run_min_horizontal_speed <= profile_.idle_max_horizontal_speed ||
      profile_.fixed_ticks_per_second == 0U) {
    fail("Runtime player animation profile has invalid locomotion thresholds");
  }
  idle_ = &require_clip(bank, profile_.idle_clip_key, "idle");
  walk_ = &require_clip(bank, profile_.walk_clip_key, "walk");
  run_ = &require_clip(bank, profile_.run_clip_key, "run");
  if (idle_ == walk_ || idle_ == run_ || walk_ == run_) {
    fail("Runtime player locomotion roles must use three distinct clips");
  }
  if (limits_.max_source_updates_per_step == 0U ||
      limits_.max_frame_advances_per_step == 0U) {
    fail("Runtime player animation playback step limits must be non-zero");
  }
  preflight_clip_playback(*idle_, "idle", profile_.fixed_ticks_per_second,
                          limits_);
  preflight_clip_playback(*walk_, "walk", profile_.fixed_ticks_per_second,
                          limits_);
  preflight_clip_playback(*run_, "run", profile_.fixed_ticks_per_second,
                          limits_);
  restart(*idle_);
}

const ActorAnimationClipV1 &RuntimePlayerAnimationV1::select_grounded_clip(
    const PlayerSimulationSnapshotV1 &player) const {
  const auto horizontal_speed =
      std::hypot(player.character.velocity.x, player.character.velocity.y);
  if (!std::isfinite(horizontal_speed)) {
    fail("Runtime player animation received a non-finite velocity");
  }
  if (horizontal_speed <= profile_.idle_max_horizontal_speed) {
    return *idle_;
  }
  if (horizontal_speed < profile_.run_min_horizontal_speed) {
    return *walk_;
  }
  return *run_;
}

void RuntimePlayerAnimationV1::restart(const ActorAnimationClipV1 &clip) {
  active_ = &clip;
  try {
    playback_ = start_actor_animation_playback_v1(clip);
    resample();
  } catch (const ActorAnimationPlaybackError &error) {
    fail("Cannot start runtime player animation: " +
         std::string(error.what()));
  }
}

void RuntimePlayerAnimationV1::resample() {
  try {
    palette_ = sample_actor_animation_pose_v1(
        *active_, playback_, rig_->semantic_key, rig_->rig, limits_);
  } catch (const ActorAnimationPlaybackError &error) {
    fail("Cannot sample runtime player animation: " +
         std::string(error.what()));
  }
}

void RuntimePlayerAnimationV1::fixed_update(
    const PlayerSimulationSnapshotV1 &player) {
  try {
    validate_player_simulation_snapshot_v1(player);
  } catch (const PlayerSimulationError &error) {
    fail("Invalid player state for animation: " + std::string(error.what()));
  }

  if (player.reset_count != observed_reset_count_) {
    observed_reset_count_ = player.reset_count;
    restart(player.character.grounded ? select_grounded_clip(player) : *idle_);
  } else if (player.character.grounded) {
    const auto &selected = select_grounded_clip(player);
    if (&selected != active_) {
      restart(selected);
    }
  } else {
    return;
  }

  try {
    static_cast<void>(advance_actor_animation_fixed_tick_v1(
        *active_, profile_.fixed_ticks_per_second, limits_, playback_));
    resample();
  } catch (const ActorAnimationPlaybackError &error) {
    fail("Cannot advance runtime player animation: " +
         std::string(error.what()));
  }
}

const ActorPosePaletteV1 &RuntimePlayerAnimationV1::palette() const noexcept {
  return palette_;
}

std::string_view RuntimePlayerAnimationV1::active_clip_key() const noexcept {
  return active_ == nullptr ? std::string_view{} : active_->semantic_key;
}

const ActorAnimationPlaybackStateV1 &
RuntimePlayerAnimationV1::playback() const noexcept {
  return playback_;
}

} // namespace openrc::game
