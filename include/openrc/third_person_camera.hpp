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

} // namespace openrc::game
