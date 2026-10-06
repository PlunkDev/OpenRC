#pragma once

#include "openrc/collision_world.hpp"

#include <cstdint>
#include <stdexcept>

namespace openrc::game {

struct ThirdPersonCameraProfileV1 {
  double minimum_distance = 0.0;
  double maximum_distance = 0.0;
  double target_height = 0.0;

  double minimum_pitch_radians = 0.0;
  double maximum_pitch_radians = 0.0;
  double yaw_radians_per_second = 0.0;
  double pitch_radians_per_second = 0.0;
  double zoom_world_units_per_second = 0.0;

  double vertical_field_of_view_radians = 0.0;
  double aspect_ratio = 0.0;
  double near_plane_distance = 0.0;
  double far_plane_distance = 0.0;

  [[nodiscard]] bool
  operator==(const ThirdPersonCameraProfileV1 &) const = default;
};

// Yaw is the ground-plane direction from the eye towards the target. Pitch is
// positive when the eye is above the target. Yaw is kept in [-pi, pi].
struct ThirdPersonCameraStateV1 {
  double yaw_radians = 0.0;
  double pitch_radians = 0.0;
  double distance = 0.0;

  [[nodiscard]] bool
  operator==(const ThirdPersonCameraStateV1 &) const = default;
};

// Camera controls remain quantized at the deterministic input boundary.
// Positive zoom moves the eye towards the target.
struct ThirdPersonCameraInputV1 {
  std::int16_t look_x = 0;
  std::int16_t look_y = 0;
  std::int16_t zoom = 0;

  [[nodiscard]] bool
  operator==(const ThirdPersonCameraInputV1 &) const = default;
};

struct ThirdPersonCameraViewV1 {
  CollisionVectorV1 eye;
  CollisionVectorV1 target;
  CollisionVectorV1 up{0.0, 0.0, 1.0};

  double vertical_field_of_view_radians = 0.0;
  double aspect_ratio = 0.0;
  double near_plane_distance = 0.0;
  double far_plane_distance = 0.0;

  [[nodiscard]] bool
  operator==(const ThirdPersonCameraViewV1 &) const = default;
};

struct CameraRelativeMovementV1 {
  double move_x = 0.0;
  double move_y = 0.0;

  [[nodiscard]] bool
  operator==(const CameraRelativeMovementV1 &) const = default;
};

class ThirdPersonCameraError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void validate_third_person_camera_profile_v1(
    const ThirdPersonCameraProfileV1 &profile);
void validate_third_person_camera_state_v1(
    const ThirdPersonCameraProfileV1 &profile,
    const ThirdPersonCameraStateV1 &state);
void validate_third_person_camera_input_v1(
    const ThirdPersonCameraInputV1 &input);

class ThirdPersonCameraV1 final {
public:
  ThirdPersonCameraV1(ThirdPersonCameraProfileV1 profile,
                      ThirdPersonCameraStateV1 initial_state);

  // This is a simulation-tick operation. The caller supplies its explicit
  // fixed duration; presentation frames must not advance camera state.
  void fixed_update(ThirdPersonCameraInputV1 input, double fixed_delta_seconds);

  void set_state(const ThirdPersonCameraStateV1 &state);

  [[nodiscard]] ThirdPersonCameraViewV1
  view(CollisionVectorV1 focus_position) const;

  // move_y is forward/back and move_x is screen-right/left. The returned
  // world-space XY vector is normalized to the same unit-circle domain.
  [[nodiscard]] CameraRelativeMovementV1
  map_movement(std::int16_t move_x, std::int16_t move_y) const;

  // Source-response movement already lives in the unit-circle domain. This
  // overload avoids an int16 round trip before the runtime applies an exact
  // source-authored target speed.
  [[nodiscard]] CameraRelativeMovementV1 map_unit_movement(double move_x,
                                                           double move_y) const;

  [[nodiscard]] const ThirdPersonCameraProfileV1 &profile() const noexcept;
  [[nodiscard]] const ThirdPersonCameraStateV1 &state() const noexcept;

private:
  ThirdPersonCameraProfileV1 profile_;
  ThirdPersonCameraStateV1 state_;
};

// Original R&C1 PAL v2.00 gameplay camera primitives recovered from the boot
// executable and the Veldin level overlay (docs/RAC_GAMEPLAY_CAMERA_V1.md).
// Values are single precision in the source operation order, evaluated with
// host IEEE arithmetic. They are not an EE COP1 MUL.S/DIV.S bit-exact model.
// The developer ThirdPersonCameraV1 above is unrelated and unchanged.

// 1f7d00 selects its vertical tangent factor from the word at 15ee80:
// nonzero is the PAL selector, zero the other selector.
enum class RacProjectionSelectorV1 : std::uint8_t {
  pal,
  other,
};

inline constexpr std::uint32_t kRacPalVerticalTangentFactorBitsV1 = 0x3f418937U;
inline constexpr std::uint32_t kRacOtherVerticalTangentFactorBitsV1 =
    0x3f466666U;
// 1f7bc8, called by level entry 2465f8 through 23d9c0 before 1f7d00.
inline constexpr std::uint32_t kRacGameplayHorizontalTangentBitsV1 =
    0x3f2147aeU;
inline constexpr std::uint32_t kRacGameplayNearBitsV1 = 0x42000000U;
inline constexpr std::uint32_t kRacGameplayFarBitsV1 = 0x49360000U;
// 1f7d28: the depth row multiplier.
inline constexpr std::uint32_t kRacProjectionDepthScaleBitsV1 = 0xcafffbe0U;

struct RacGameplayProjectionInputV1 {
  RacProjectionSelectorV1 selector = RacProjectionSelectorV1::pal;
  // Owner 16cb40 fields +b0, +a0 and +a4.
  float horizontal_tangent = 0.0F;
  float near_distance = 0.0F;
  float far_distance = 0.0F;
  // +200 and +204: half of the signed halfwords at 1519d0 and 1519d2. The
  // display dimensions are runtime state and are supplied by the caller.
  float viewport_half_width = 0.0F;
  float viewport_half_height = 0.0F;

  [[nodiscard]] bool
  operator==(const RacGameplayProjectionInputV1 &) const = default;
};

// The 16cb40+c0 matrix entries written by 1f7d00. The remaining matrix
// entries are either zero or depend on fog/depth-range state (+210) that this
// model does not claim.
struct RacGameplayProjectionV1 {
  float horizontal_tangent = 0.0F; // +b0
  float vertical_tangent = 0.0F;   // +b4
  float scale_x = 0.0F;            // +c0 = half width / (+b0 * near)
  float scale_y = 0.0F;            // +d4 = half height / (+b4 * near)
  float depth_z = 0.0F;            // +e8
  float depth_w = 0.0F;            // +f8
  static constexpr bool ee_bit_exact = false;

  [[nodiscard]] bool
  operator==(const RacGameplayProjectionV1 &) const = default;
};

[[nodiscard]] float
rac_vertical_tangent_factor_v1(RacProjectionSelectorV1 selector) noexcept;

// The scalars installed by 1f7bc8 for the supplied viewport.
[[nodiscard]] RacGameplayProjectionInputV1
rac_gameplay_projection_defaults_v1(RacProjectionSelectorV1 selector,
                                    float viewport_half_width,
                                    float viewport_half_height) noexcept;

[[nodiscard]] RacGameplayProjectionV1
rac_gameplay_projection_v1(const RacGameplayProjectionInputV1 &input);

// Helper 1eb5c0. maximum_step zero disables the optional step clamp.
struct RacCameraSpringV1 {
  float stiffness = 0.0F;
  float damping = 0.0F;
  float maximum_step = 0.0F;

  [[nodiscard]] bool operator==(const RacCameraSpringV1 &) const = default;
};

// velocity += stiffness * (target - current) - damping * velocity, then the
// optional +/-maximum_step clamp, then a clamp to +/-|target - current|.
// Returns current + velocity.
[[nodiscard]] float rac_camera_spring_step_v1(float current, float target,
                                              float &velocity,
                                              const RacCameraSpringV1 &spring);

struct RacVector3fV1 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;

  [[nodiscard]] bool operator==(const RacVector3fV1 &) const = default;
};

// Camera type 0 (lvl.camvtbl entry 0: init 2e8210, update 2eb0d8). The
// default-mode focus follow of stage 2e6ce0 uses stiffness 0.015 and damping
// 0.2 for both its horizontal (+220/+224) and vertical (+228/+232) springs,
// restored every update by 2e72e8.
struct RacGameplayCameraProfileV1 {
  RacCameraSpringV1 focus_spring;

  [[nodiscard]] bool
  operator==(const RacGameplayCameraProfileV1 &) const = default;
};

// focus is camera +160. With the source up vector equal to +Z, velocity X/Y
// are the horizontal slots +176/+180 and velocity Z the vertical slot +200.
struct RacGameplayCameraStateV1 {
  RacVector3fV1 focus;
  RacVector3fV1 focus_velocity;

  [[nodiscard]] bool
  operator==(const RacGameplayCameraStateV1 &) const = default;
};

[[nodiscard]] RacGameplayCameraProfileV1 rac_gameplay_camera_profile_v1();

// Initialization 2e58e0 when the player word at 13f75c is zero: the focus
// equals the player position and both velocity slots are cleared.
[[nodiscard]] RacGameplayCameraStateV1
rac_gameplay_camera_initial_state_v1(RacVector3fV1 player_position);

// Right-stick pitch response at 2ea0dc..2ea154: dead zone 0.3, then the
// remaining travel scaled by 1.4285714. The caller supplies the already
// orientation-selected pad axis.
[[nodiscard]] float rac_gameplay_camera_pitch_response_v1(float axis);

class RacGameplayCameraV1 final {
public:
  RacGameplayCameraV1(RacGameplayCameraProfileV1 profile,
                      RacGameplayCameraStateV1 initial_state);

  // One original camera update (one PAL frame). Only the default-mode focus
  // follow is modeled; eye placement, look input and collision are not.
  void step_pal_frame(RacVector3fV1 player_position);

  [[nodiscard]] const RacGameplayCameraProfileV1 &profile() const noexcept;
  [[nodiscard]] const RacGameplayCameraStateV1 &state() const noexcept;

private:
  RacGameplayCameraProfileV1 profile_;
  RacGameplayCameraStateV1 state_;
};

} // namespace openrc::game
