#pragma once

#include "openrc/collision_world.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace openrc::game {

struct CharacterControllerProfileV1 {
  double capsule_radius = 0.0;
  double capsule_height = 0.0;
  double skin_width = 0.0;
  double ground_probe_distance = 0.0;
  double step_height = 0.0;
  double maximum_slope_degrees = 0.0;

  double maximum_ground_speed = 0.0;
  double ground_acceleration = 0.0;
  double ground_deceleration = 0.0;
  double air_acceleration = 0.0;
  double gravity = 0.0;
  double jump_speed = 0.0;
  double maximum_fall_speed = 0.0;

  double maximum_substep_distance = 0.0;
  std::uint32_t maximum_motion_substeps = 0U;
  std::uint32_t maximum_slide_iterations = 0U;
  std::uint32_t maximum_depenetration_iterations = 0U;
  CollisionLayerMaskV1 collision_layers = 0U;
  CollisionQueryLimitsV1 query_limits;

  [[nodiscard]] bool
  operator==(const CharacterControllerProfileV1 &) const = default;
};

// feet_position is the logical contact point below an upright Z-axis capsule.
// Its lower and upper sphere centers are at Z + radius and
// Z + height - radius respectively.
struct CharacterControllerStateV1 {
  CollisionVectorV1 feet_position;
  CollisionVectorV1 velocity;
  CollisionVectorV1 ground_normal{0.0, 0.0, 1.0};
  std::optional<std::uint32_t> ground_triangle_index;
  bool grounded = false;

  [[nodiscard]] bool
  operator==(const CharacterControllerStateV1 &) const = default;
};

// The movement vector is expressed in canonical world XY and is limited to a
// unit circle. Player camera-relative mapping happens before this boundary;
// NPCs can feed the same controller without fabricating device input.
struct CharacterMotionV1 {
  double move_x = 0.0;
  double move_y = 0.0;
  bool jump_pressed = false;
  // When present, move_x/move_y provide direction only and this value is the
  // requested horizontal-speed target. The controller still applies its
  // configured ground/air acceleration policy while approaching that target.
  std::optional<double> target_horizontal_speed = std::nullopt;

  [[nodiscard]] bool operator==(const CharacterMotionV1 &) const = default;
};

struct CharacterStepResultV1 {
  bool landed = false;
  bool left_ground = false;
  bool hit_wall = false;
  bool stepped_up = false;
  std::uint32_t collision_count = 0U;

  [[nodiscard]] bool operator==(const CharacterStepResultV1 &) const = default;
};

class CharacterControllerError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void validate_character_controller_profile_v1(
    const CharacterControllerProfileV1 &profile);
void validate_character_controller_state_v1(
    const CharacterControllerStateV1 &state);

[[nodiscard]] std::uint64_t
hash_character_controller_state_v1(const CharacterControllerStateV1 &state);

class CharacterControllerV1 final {
public:
  CharacterControllerV1(CharacterControllerProfileV1 profile,
                        CharacterControllerStateV1 initial_state);

  [[nodiscard]] CharacterStepResultV1
  fixed_update(const CollisionWorldV1 &collision_world,
               CharacterMotionV1 motion, double fixed_delta_seconds);

  void set_state(const CharacterControllerStateV1 &state);

  [[nodiscard]] const CharacterControllerProfileV1 &profile() const noexcept;
  [[nodiscard]] const CharacterControllerStateV1 &state() const noexcept;

private:
  CharacterControllerProfileV1 profile_;
  CharacterControllerStateV1 state_;
};

} // namespace openrc::game
