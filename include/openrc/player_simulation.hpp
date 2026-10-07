#pragma once

#include "openrc/character_controller.hpp"
#include "openrc/game_input.hpp"

#include <cstdint>
#include <stdexcept>

namespace openrc::game {

struct PlayerCheckpointV1 {
  std::uint64_t checkpoint_id = 0U;
  CollisionVectorV1 feet_position;
  double facing_yaw_radians = 0.0;

  [[nodiscard]] bool operator==(const PlayerCheckpointV1 &) const = default;
};

struct PlayerSimulationProfileV1 {
  CharacterControllerProfileV1 character;
  std::uint32_t fixed_ticks_per_second = 0U;
  // Absolute world-space Z supplied by the active level package. It is not
  // derived from a level ID or from the checkpoint, because both values are
  // authored independently by the source game.
  double death_height_world = 0.0;

  [[nodiscard]] bool
  operator==(const PlayerSimulationProfileV1 &) const = default;
};

enum class PlayerResetReasonV1 : std::uint8_t {
  none = 0U,
  manual,
  checkpoint_activated,
  fell_below_death_height,
};

struct PlayerSimulationSnapshotV1 {
  CharacterControllerStateV1 character;
  PlayerCheckpointV1 checkpoint;
  double facing_yaw_radians = 0.0;
  std::uint64_t next_tick_index = 0U;
  std::uint64_t reset_count = 0U;
  PlayerResetReasonV1 last_reset_reason = PlayerResetReasonV1::none;

  [[nodiscard]] bool
  operator==(const PlayerSimulationSnapshotV1 &) const = default;
};

struct PlayerSimulationStepV1 {
  CharacterStepResultV1 character;
  PlayerResetReasonV1 reset_reason = PlayerResetReasonV1::none;

  [[nodiscard]] bool operator==(const PlayerSimulationStepV1 &) const = default;
};

class PlayerSimulationError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void validate_player_checkpoint_v1(const PlayerCheckpointV1 &checkpoint);
void validate_player_simulation_profile_v1(
    const PlayerSimulationProfileV1 &profile);
void validate_player_simulation_snapshot_v1(
    const PlayerSimulationSnapshotV1 &snapshot);

[[nodiscard]] std::uint64_t
hash_player_simulation_snapshot_v1(const PlayerSimulationSnapshotV1 &snapshot);

class PlayerSimulationV1 final {
public:
  PlayerSimulationV1(PlayerSimulationProfileV1 profile,
                     PlayerCheckpointV1 initial_checkpoint,
                     std::uint64_t next_tick_index = 0U);
  PlayerSimulationV1(PlayerSimulationProfileV1 profile,
                     const PlayerSimulationSnapshotV1 &snapshot);

  [[nodiscard]] PlayerSimulationStepV1
  fixed_update(const CollisionWorldV1 &collision_world,
               const GameInputCommandV1 &input);
  [[nodiscard]] PlayerSimulationStepV1
  fixed_update(const CollisionWorldV1 &collision_world,
               const GameInputCommandV1 &input, CharacterMotionV1 motion);

  void set_checkpoint(PlayerCheckpointV1 checkpoint, bool reset_immediately);
  void reset_to_checkpoint(PlayerResetReasonV1 reason);

  [[nodiscard]] PlayerSimulationSnapshotV1 snapshot() const noexcept;
  [[nodiscard]] const PlayerSimulationProfileV1 &profile() const noexcept;

private:
  PlayerSimulationProfileV1 profile_;
  CharacterControllerV1 character_;
  PlayerCheckpointV1 checkpoint_;
  double facing_yaw_radians_ = 0.0;
  std::uint64_t next_tick_index_ = 0U;
  std::uint64_t reset_count_ = 0U;
  PlayerResetReasonV1 last_reset_reason_ = PlayerResetReasonV1::none;
};

// RAC1 PAL v2.00 standard-jump (source state 7) vertical axis, recovered
// clean-room from the Veldin overlay; see docs/RAC_PLAYER_AIRBORNE_V1.md.
// The model is independent of PlayerSimulationV1 and does not change its
// default behavior, profile layout, snapshot layout, or snapshot hash.
//
// Every value is in source units per PAL 50 Hz frame. The constants are the
// exact binary32 encodings read by the original instructions. Results are
// host IEEE binary32 evaluations of the source operation order; neither the
// EE FPU rounding mode nor the VU0 square root is hardware-qualified here.
inline constexpr std::uint32_t kRacPalFrameStepBitsV1 = 0x3ca3d70bU;
inline constexpr std::uint32_t kRacPalFrameStepSquaredBitsV1 = 0x39d1b718U;
inline constexpr std::uint32_t kRacJumpGravityScaleBitsV1 = 0x41ed999aU;
inline constexpr std::uint32_t kRacJumpApexGravityFactorBitsV1 = 0x3f95c28fU;
inline constexpr std::uint32_t kRacJumpInitialHeightBitsV1 = 0x3fbc28f6U;
inline constexpr std::uint32_t kRacJumpMaximumHeightBitsV1 = 0x4027ae14U;
inline constexpr std::uint32_t kRacJumpTakeoffPressScaleBitsV1 = 0x42400000U;
inline constexpr std::uint32_t kRacJumpTerminalSpeedScaleBitsV1 = 0x42480000U;
inline constexpr std::uint32_t kRacJumpMeasuredDropLimitBitsV1 = 0x3dcccccdU;
inline constexpr std::uint32_t kRacJumpApexVelocityThresholdBitsV1 =
    0x3a83126fU;
// frames(5) from 0x1feed0 on PAL: (int)(0.5 + 5 * 0.8333333) == 4 under both
// round-to-nearest and truncating evaluation.
inline constexpr std::uint32_t kRacJumpTakeoffFramesPalV1 = 4U;

// frames(15) lands on 12.9999997 before CVT.W.S, so the PAL height-ramp
// length is 13 under host rounding and 12 under truncation. It stays an
// explicit caller choice until the EE MADD.S result is hardware-qualified.
struct RacJumpVerticalProfileV1 {
  std::uint32_t takeoff_frames = kRacJumpTakeoffFramesPalV1;
  std::uint32_t height_ramp_frames = 0U;

  [[nodiscard]] bool
  operator==(const RacJumpVerticalProfileV1 &) const = default;
};

// Source fields of the player block 0x13f450 touched by the vertical axis.
struct RacJumpVerticalStateV1 {
  float vertical_velocity = 0.0F;  // +0xe0.z
  float pending_impulse = 0.0F;    // +0x428
  float applied_impulse = 0.0F;    // +0x42c
  float target_height = 0.0F;      // +0x430
  float gravity = 0.0F;            // +0x4a0
  float apex_gravity = 0.0F;       // +0x3f8
  bool apex_reached = false;       // +0x41e
  std::uint32_t frames_in_state = 0U;  // +0x198 as read by the state update

  [[nodiscard]] bool operator==(const RacJumpVerticalStateV1 &) const = default;
};

struct RacJumpVerticalInputV1 {
  // Pad word 0x13cbe0 bit 0x40 for the current frame.
  bool jump_held = false;
  // +0x110.z: the vertical displacement measured by the previous collision
  // step. The collision itself is outside this model.
  float measured_vertical_displacement = 0.0F;
};

void validate_rac_jump_vertical_profile_v1(
    const RacJumpVerticalProfileV1 &profile);

// State-7 entry writes from 0x224d1c..0x224f74 that feed the vertical axis.
// 0x224d68..0x224d78 copies the measured displacement +0x110 over the
// velocity +0xe0 (LQ/SQ), so the entry vertical velocity is +0x110.z, not
// the previous commanded +0xe0.z.
[[nodiscard]] RacJumpVerticalStateV1
enter_rac_jump_vertical_v1(float measured_vertical_displacement = 0.0F);

// One PAL frame: apex check 0x21f830..0x21f8ac, then 0x2147c0's state-7 path
// (impulse ramp, impulse application, takeoff press or gravity and clamps),
// then the +0x198 increment performed by 0x221d50.
[[nodiscard]] RacJumpVerticalStateV1
step_rac_jump_vertical_v1(const RacJumpVerticalStateV1 &state,
                          const RacJumpVerticalProfileV1 &profile,
                          const RacJumpVerticalInputV1 &input);

// RAC1 PAL v2.00 unsupported fall (source state 6). Entry 0x224aa4 copies
// the measured displacement +0x110 into +0xe0, so the first fall velocity is
// the caller's measured vertical displacement.
inline constexpr std::uint32_t kRacFallGravityScaleBitsV1 = 0x41c00000U;
inline constexpr std::uint32_t kRacFallLandingSpeedScaleBitsV1 = 0xc1100000U;
inline constexpr std::uint32_t kRacFallLongPhaseHeightBitsV1 = 0x3fe00000U;
inline constexpr std::uint32_t kRacFallWalkMagnitudeBitsV1 = 0x3f000000U;
inline constexpr std::uint32_t kRacFallSkidSpeedScaleBitsV1 = 0x40400000U;
// PAL frames(n) values used by state 6. Each is far enough from an integer
// boundary to be independent of the EE MADD.S rounding mode.
inline constexpr std::uint32_t kRacFallLongPhaseFramesPalV1 = 15U;     // 18
inline constexpr std::uint32_t kRacFallHardLandingFramesPalV1 = 75U;   // 90
inline constexpr std::uint32_t kRacFallHardLockFramesPalV1 = 18U;      // 22
inline constexpr std::uint32_t kRacFallLongLockFramesPalV1 = 6U;       // 7
inline constexpr std::uint32_t kRacFallLongLockFromState45PalV1 = 8U;  // 10

// 0x21ed5c..0x21eda4: v = v - dt2 * 24, then not below -(dt * 50).
[[nodiscard]] float step_rac_fall_vertical_v1(float vertical_velocity);

// 0x22e640..0x22e684 on ground contact: v is raised to dt * -9.
[[nodiscard]] float land_rac_fall_vertical_v1(float vertical_velocity);

// 0x22e594..0x22e5e8: while the fall is in its first phase, it switches to
// source slot 11 once frames_in_state (+0x198 as read by the transitions)
// reaches frames(18) or the height above ground (+0x2dc) exceeds 1.75.
[[nodiscard]] bool rac_fall_enters_long_phase_v1(std::uint32_t frames_in_state,
                                                 float height_above_ground);

struct RacFallLandingInputV1 {
  // +0x22a8 > 0. INFERRED: the player's hit points (decremented by 0x2056d8).
  bool hit_points_positive = true;
  std::uint32_t frames_in_state = 0U;  // +0x198 as read by the transitions
  bool long_phase = false;             // substate +0x2088 == 1 (slot 11)
  // T3 0x22e584 is shared by states 6 and 45. After an accepted set_state,
  // +0x2090 holds the state being left, so this selects the frames(10) lock.
  bool state_is_45 = false;
  bool lock_extension_allowed = false; // +0x2fc == 0, meaning UNKNOWN
  float previous_stick_magnitude = 0.0F;  // +0x229c
  std::uint32_t input_lock_frames = 0U;   // +0x1c4 countdown
  float planar_speed = 0.0F;              // +0x164, units per PAL frame
};

struct RacFallLandingV1 {
  std::uint32_t next_state = 0U;
  // set_state's a1. With 1 the target state's own entry chooses the slot.
  bool entry_selects_sequence = false;
  // Explicit 0x232c10 call after set_state, when present.
  bool sequence_selected = false;
  std::uint32_t sequence_slot = 0U;
  // Stored at Moby +0x51, the source frame index (the 0x229618 slot remap
  // passes its mapped frame through the same argument).
  std::uint32_t sequence_argument = 0U;
  float sequence_blend_frames = 0.0F;    // negative values use 0x232c10's
                                         // table path (UNKNOWN meaning)
  bool input_lock_written = false;
  std::uint32_t input_lock_frames = 0U;  // new +0x1c4
};

// Ground-contact branch of T3 0x22e584 (0x22e620..0x22e8bc) for state 6.
// The caller has already observed +0x30e == 0 (no unsupported frames).
// Every set_state call is assumed to be accepted. Its tail (0x227db0) zeroes
// +0x198 before 0x22e7bc reads it again, so the frames(18) lock extension
// after frames(50) is only reachable when set_state(0, 0) is vetoed, which
// this selector does not model.
[[nodiscard]] RacFallLandingV1
select_rac_fall_landing_v1(const RacFallLandingInputV1 &input);

} // namespace openrc::game
