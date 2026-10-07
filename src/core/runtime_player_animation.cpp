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

constexpr std::uint32_t kSlowToFullSourceFrameOffset = 1U;
constexpr std::uint32_t kFullToSlowSourceFrameOffset = 5U;
// select_rac_fall_landing_v1's slot-11-phase start frame; the hard landing
// starts earlier at frame 4.
constexpr std::size_t kFallLandingLongSourceFrame = 9U;
// Bounds source_frames() far below any multiplication overflow while still
// covering more than a year of 60 Hz ticks.
constexpr std::uint64_t kMaximumAirborneTicks = 1ULL << 32U;

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
  if (profile_.fixed_ticks_per_second == 0U) {
    fail("Runtime player animation profile has zero runtime cadence");
  }
  idle_ = &require_clip(bank, profile_.idle_clip_key, "idle");
  slow_ = &require_clip(bank, profile_.slow_clip_key(), "slow");
  full_ = &require_clip(bank, profile_.full_clip_key(), "full");
  if (idle_ == slow_ || idle_ == full_ || slow_ == full_) {
    fail("Runtime player locomotion roles must use three distinct clips");
  }
  if (limits_.max_source_updates_per_step == 0U ||
      limits_.max_frame_advances_per_step == 0U) {
    fail("Runtime player animation playback step limits must be non-zero");
  }
  preflight_clip_playback(*idle_, "idle", profile_.fixed_ticks_per_second,
                          limits_);
  preflight_clip_playback(*slow_, "slow", profile_.fixed_ticks_per_second,
                          limits_);
  preflight_clip_playback(*full_, "full", profile_.fixed_ticks_per_second,
                          limits_);
  const auto distinct_from_grounded = [this](const ActorAnimationClipV1 *clip) {
    return clip != idle_ && clip != slow_ && clip != full_;
  };
  if (!profile_.jump_clip_key.empty()) {
    jump_ = &require_clip(bank, profile_.jump_clip_key, "jump");
    if (!distinct_from_grounded(jump_)) {
      fail("Runtime player jump clip must differ from the grounded clips");
    }
    preflight_clip_playback(*jump_, "jump", profile_.fixed_ticks_per_second,
                            limits_);
  }
  if (!profile_.fall_clip_key.empty()) {
    fall_ = &require_clip(bank, profile_.fall_clip_key, "fall");
    if (!distinct_from_grounded(fall_) || fall_ == jump_) {
      fail("Runtime player fall clip must differ from the other roles");
    }
    preflight_clip_playback(*fall_, "fall", profile_.fixed_ticks_per_second,
                            limits_);
  }
  const auto distinct_from_roles = [&](const ActorAnimationClipV1 *clip) {
    return distinct_from_grounded(clip) && clip != jump_ && clip != fall_;
  };
  if (!profile_.long_fall_clip_key.empty() ||
      !profile_.fall_landing_clip_key.empty()) {
    if (fall_ == nullptr) {
      fail("Runtime player fall phase clips require the fall clip");
    }
  }
  if (!profile_.long_fall_clip_key.empty()) {
    long_fall_ = &require_clip(bank, profile_.long_fall_clip_key, "long fall");
    if (!distinct_from_roles(long_fall_)) {
      fail("Runtime player long-fall clip must differ from the other roles");
    }
    preflight_clip_playback(*long_fall_, "long fall",
                            profile_.fixed_ticks_per_second, limits_);
  }
  if (!profile_.fall_landing_clip_key.empty()) {
    fall_landing_ =
        &require_clip(bank, profile_.fall_landing_clip_key, "fall landing");
    if (!distinct_from_roles(fall_landing_) || fall_landing_ == long_fall_) {
      fail("Runtime player fall-landing clip must differ from the other roles");
    }
    // The long-phase landing starts at source frame 9 (T3 0x22e74c).
    if (fall_landing_->frames.size() <= kFallLandingLongSourceFrame) {
      fail("Runtime player fall-landing clip lacks its source start frame");
    }
    preflight_clip_playback(*fall_landing_, "fall landing",
                            profile_.fixed_ticks_per_second, limits_);
  }
  restart(*idle_);
}

std::uint64_t
RuntimePlayerAnimationV1::source_frames(const std::uint64_t runtime_ticks) const {
  // Airborne time uses the fall clip's PAL source cadence and the same exact
  // integer 50-to-60 conversion as playback (adapter mapping).
  return runtime_ticks * fall_->source_updates_per_second /
         profile_.fixed_ticks_per_second;
}

bool RuntimePlayerAnimationV1::begin_fall_landing(
    const AirborneRole landed_role, const std::uint64_t airborne_frames) {
  if (landed_role != AirborneRole::fall || fall_landing_ == nullptr) {
    return false;
  }
  // The neutral snapshot carries no stick sample, planar source speed, hit
  // points or height, so only the frame-count branches of the recovered
  // selector can be evaluated; the short-fall branches fall back to the
  // grounded selection.
  RacFallLandingInputV1 input;
  input.frames_in_state = static_cast<std::uint32_t>(
      std::min<std::uint64_t>(airborne_frames,
                              std::numeric_limits<std::uint32_t>::max()));
  input.long_phase = rac_fall_enters_long_phase_v1(input.frames_in_state, 0.0F);
  const auto landing = select_rac_fall_landing_v1(input);
  if (!landing.sequence_selected || landing.sequence_slot != 12U) {
    return false;
  }
  restart_at(*fall_landing_, landing.sequence_argument);
  landing_active_ = true;
  landing_ticks_ = 0U;
  landing_lock_frames_ =
      landing.input_lock_written ? landing.input_lock_frames : 0U;
  return true;
}

bool RuntimePlayerAnimationV1::continue_fall_landing(
    const PlayerSimulationSnapshotV1 &player) {
  if (landing_ticks_ < kMaximumAirborneTicks) {
    ++landing_ticks_;
  }
  if (playback_.finished || playback_.completed_cycles != 0U) {
    return false;
  }
  const auto horizontal_speed =
      std::hypot(player.character.velocity.x, player.character.velocity.y);
  if (!std::isfinite(horizontal_speed)) {
    fail("Runtime player animation received a non-finite velocity");
  }
  // Adapter policy: motion ends the landing once the source input lock
  // (+0x1c4) written by the landing branch has elapsed.
  return horizontal_speed == 0.0 ||
         source_frames(landing_ticks_) < landing_lock_frames_;
}

void RuntimePlayerAnimationV1::restart_at(const ActorAnimationClipV1 &clip,
                                          const std::uint32_t frame) {
  active_ = &clip;
  try {
    playback_ = start_actor_animation_playback_v1(clip);
    playback_.frame_index = frame;
    resample();
  } catch (const ActorAnimationPlaybackError &error) {
    fail("Cannot start runtime player animation: " +
         std::string(error.what()));
  }
}

const ActorAnimationClipV1 &RuntimePlayerAnimationV1::select_grounded_clip(
    const PlayerSimulationSnapshotV1 &player) const {
  const auto horizontal_speed =
      std::hypot(player.character.velocity.x, player.character.velocity.y);
  if (!std::isfinite(horizontal_speed)) {
    fail("Runtime player animation received a non-finite velocity");
  }

  // PlayerSimulationSnapshotV1 has no RAC source state or previous-pad
  // magnitude. Do not reinterpret either obsolete profile threshold as the
  // original >0.22/<0.17 state hysteresis: zero horizontal motion is the only
  // honest idle fallback available at this boundary.
  if (horizontal_speed == 0.0) {
    return *idle_;
  }

  // State 2 always enters through source slot 3. Once moving, the original
  // helper at 0x2293e8 selects slot 4 only above 2.35 and returns to slot 3
  // only below 1.90. Both comparisons are strict.
  if (active_ == full_) {
    return horizontal_speed < kRuntimePlayerFullToSlowHorizontalSpeedV1
               ? *slow_
               : *full_;
  }
  if (active_ == slow_) {
    return horizontal_speed > kRuntimePlayerSlowToFullHorizontalSpeedV1
               ? *full_
               : *slow_;
  }
  return *slow_;
}

const ActorAnimationClipV1 *RuntimePlayerAnimationV1::select_airborne_clip(
    const PlayerSimulationSnapshotV1 &player) {
  if (airborne_role_ == AirborneRole::undecided) {
    const auto vertical_speed = player.character.velocity.z;
    if (!std::isfinite(vertical_speed)) {
      fail("Runtime player animation received a non-finite velocity");
    }
    // Adapter policy, not a source rule: the snapshot has no RAC state, so
    // upward motion on the first airborne tick stands for the button-driven
    // state 7 and anything else for the unsupported-ground state 6.
    const auto *const chosen = vertical_speed > 0.0 ? jump_ : fall_;
    if (chosen == nullptr) {
      airborne_role_ = AirborneRole::hold;
    } else {
      airborne_role_ =
          chosen == jump_ ? AirborneRole::jump : AirborneRole::fall;
    }
  }
  switch (airborne_role_) {
  case AirborneRole::jump:
    return jump_;
  case AirborneRole::fall:
    // T3 0x22e594..0x22e5e8: slot 11 after frames(18) frames. The height
    // test (+0x2dc > 1.75) has no input in the neutral snapshot.
    if (long_fall_ != nullptr &&
        rac_fall_enters_long_phase_v1(
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                source_frames(airborne_ticks_),
                std::numeric_limits<std::uint32_t>::max())),
            0.0F)) {
      return long_fall_;
    }
    return fall_;
  case AirborneRole::undecided:
  case AirborneRole::hold:
    break;
  }
  return nullptr;
}

void RuntimePlayerAnimationV1::select(const ActorAnimationClipV1 &clip) {
  if (&clip == active_) {
    return;
  }
  if (active_ == slow_ && &clip == full_) {
    remap_moving_phase(clip, kSlowToFullSourceFrameOffset);
    return;
  }
  if (active_ == full_ && &clip == slow_) {
    remap_moving_phase(clip, kFullToSlowSourceFrameOffset);
    return;
  }
  restart(clip);
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

void RuntimePlayerAnimationV1::remap_moving_phase(
    const ActorAnimationClipV1 &clip,
    const std::uint32_t source_frame_offset) {
  if (active_ == nullptr || active_->frames.empty() || clip.frames.empty() ||
      active_->frames.size() >
          static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
      clip.frames.size() >
          static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    fail("Runtime player moving animation has an invalid source frame count");
  }

  // Exact slot-3/slot-4 frame remap recovered from 0x229618..0x2296a4.
  // gp-0x7558 (VA 0x15f7a8) supplies offsets 1 for 3->4 and 5 for 4->3:
  // floor(old_frame * new_count / old_count) + offset, modulo new_count.
  // The source setter clears Moby+0x54, so fractional phase is reset while
  // the mapped source frame (Moby+0x51) is retained.
  const auto old_count = static_cast<std::uint64_t>(active_->frames.size());
  const auto new_count = static_cast<std::uint64_t>(clip.frames.size());
  const auto scaled_frame =
      static_cast<std::uint64_t>(playback_.frame_index) * new_count;
  const auto mapped_frame =
      (scaled_frame / old_count + source_frame_offset) % new_count;

  try {
    auto next = start_actor_animation_playback_v1(clip);
    next.frame_index = static_cast<std::uint32_t>(mapped_frame);
    // This accumulator is the runtime's exact 50-to-60 cadence remainder,
    // not RAC's per-clip fractional phase. Preserve it across the source clip
    // switch so repeated hysteresis transitions cannot create cadence drift.
    next.runtime_ticks_per_second = playback_.runtime_ticks_per_second;
    next.source_update_accumulator = playback_.source_update_accumulator;
    active_ = &clip;
    playback_ = next;
    resample();
  } catch (const ActorAnimationPlaybackError &error) {
    fail("Cannot remap runtime player moving animation: " +
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
    airborne_role_ = AirborneRole::undecided;
    airborne_ticks_ = 0U;
    landing_active_ = false;
    if (!player.character.grounded) {
      restart(*idle_);
    } else {
      const auto horizontal_speed = std::hypot(
          player.character.velocity.x, player.character.velocity.y);
      if (!std::isfinite(horizontal_speed)) {
        fail("Runtime player animation received a non-finite velocity");
      }
      // A reset loses any trustworthy source-state identity. Re-enter the
      // recovered moving state through slot 3, exactly as state-2 init does.
      restart(horizontal_speed == 0.0 ? *idle_ : *slow_);
    }
  } else if (player.character.grounded) {
    const auto landed_role = airborne_role_;
    const auto airborne_frames =
        fall_ == nullptr ? 0U : source_frames(airborne_ticks_);
    airborne_role_ = AirborneRole::undecided;
    airborne_ticks_ = 0U;
    if (!begin_fall_landing(landed_role, airborne_frames) &&
        !(landing_active_ && continue_fall_landing(player))) {
      landing_active_ = false;
      const auto &selected = select_grounded_clip(player);
      select(selected);
    }
  } else {
    landing_active_ = false;
    if (airborne_ticks_ < kMaximumAirborneTicks) {
      ++airborne_ticks_;
    }
    const auto *const airborne = select_airborne_clip(player);
    if (airborne == nullptr) {
      return;
    }
    select(*airborne);
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
