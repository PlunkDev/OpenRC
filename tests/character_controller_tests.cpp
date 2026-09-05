#include "openrc/character_controller.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using openrc::CollisionLayerV1;
using openrc::CollisionMeshV1;
using openrc::CollisionTriangleV1;
using openrc::CollisionVectorV1;
using openrc::CollisionWorldV1;

constexpr openrc::CollisionWorldBuildLimitsV1 kBuildLimits{
    4096U,
    4096U,
    100'000U,
    1'000'000U,
    openrc::kCollisionDefaultGridCellSizeQ6V1,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_near(const double actual, const double expected,
                 const double tolerance, const std::string &message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_character_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::CharacterControllerError &) {
    return;
  }
  throw std::runtime_error(message);
}

class MeshBuilder final {
public:
  void triangle(const CollisionVectorV1 a, const CollisionVectorV1 b,
                const CollisionVectorV1 c) {
    const auto first = static_cast<std::uint32_t>(mesh_.vertices.size());
    mesh_.vertices.push_back(openrc::collision_world_position_to_q6_v1(a));
    mesh_.vertices.push_back(openrc::collision_world_position_to_q6_v1(b));
    mesh_.vertices.push_back(openrc::collision_world_position_to_q6_v1(c));
    mesh_.triangles.push_back(CollisionTriangleV1{
        {first, first + 1U, first + 2U},
        {},
        CollisionLayerV1::world,
    });
  }

  void floor(const double minimum_x, const double maximum_x,
             const double minimum_y, const double maximum_y, const double z) {
    const CollisionVectorV1 a{minimum_x, minimum_y, z};
    const CollisionVectorV1 b{maximum_x, minimum_y, z};
    const CollisionVectorV1 c{maximum_x, maximum_y, z};
    const CollisionVectorV1 d{minimum_x, maximum_y, z};
    triangle(a, b, c);
    triangle(a, c, d);
  }

  void wall_x(const double x, const double minimum_y, const double maximum_y,
              const double minimum_z, const double maximum_z) {
    const CollisionVectorV1 a{x, minimum_y, minimum_z};
    const CollisionVectorV1 b{x, minimum_y, maximum_z};
    const CollisionVectorV1 c{x, maximum_y, maximum_z};
    const CollisionVectorV1 d{x, maximum_y, minimum_z};
    triangle(a, b, c);
    triangle(a, c, d);
  }

  void ramp_x(const double minimum_x, const double maximum_x,
              const double minimum_y, const double maximum_y,
              const double minimum_z, const double maximum_z) {
    const CollisionVectorV1 a{minimum_x, minimum_y, minimum_z};
    const CollisionVectorV1 b{maximum_x, minimum_y, maximum_z};
    const CollisionVectorV1 c{maximum_x, maximum_y, maximum_z};
    const CollisionVectorV1 d{minimum_x, maximum_y, minimum_z};
    triangle(a, b, c);
    triangle(a, c, d);
  }

  [[nodiscard]] CollisionWorldV1 build() && {
    return openrc::build_collision_world_v1(std::move(mesh_), kBuildLimits);
  }

private:
  CollisionMeshV1 mesh_;
};

[[nodiscard]] openrc::game::CharacterControllerProfileV1 profile() {
  openrc::game::CharacterControllerProfileV1 result;
  result.capsule_radius = 0.3;
  result.capsule_height = 1.8;
  result.skin_width = 0.01;
  result.ground_probe_distance = 0.14;
  result.step_height = 0.5;
  result.maximum_slope_degrees = 45.0;
  result.maximum_ground_speed = 4.0;
  result.ground_acceleration = 80.0;
  result.ground_deceleration = 80.0;
  result.air_acceleration = 12.0;
  result.gravity = 20.0;
  result.jump_speed = 7.0;
  result.maximum_fall_speed = 30.0;
  result.maximum_substep_distance = 0.04;
  result.maximum_motion_substeps = 256U;
  result.maximum_slide_iterations = 4U;
  result.maximum_depenetration_iterations = 8U;
  result.collision_layers = openrc::kCollisionAllLayersMaskV1;
  result.query_limits = {4096U, 4096U};
  return result;
}

[[nodiscard]] openrc::game::CharacterControllerV1
character_at(const CollisionVectorV1 feet_position) {
  openrc::game::CharacterControllerStateV1 state;
  state.feet_position = feet_position;
  return {profile(), state};
}

void settle(openrc::game::CharacterControllerV1 &character,
            const CollisionWorldV1 &world) {
  for (std::uint32_t tick = 0U; tick < 180U && !character.state().grounded;
       ++tick) {
    static_cast<void>(character.fixed_update(world, {}, 1.0 / 60.0));
  }
  expect(character.state().grounded, "the character did not reach the floor");
}

void test_floor_landing_and_jump() {
  MeshBuilder builder;
  builder.floor(-10.0, 10.0, -10.0, 10.0, 0.0);
  const auto world = std::move(builder).build();
  auto character = character_at({0.0, 0.0, 1.5});

  bool landed = false;
  for (std::uint32_t tick = 0U; tick < 180U; ++tick) {
    const auto step = character.fixed_update(world, {}, 1.0 / 60.0);
    landed = landed || step.landed;
    if (character.state().grounded) {
      break;
    }
  }
  expect(landed && character.state().grounded,
         "falling onto a synthetic floor did not produce a landing");
  expect_near(character.state().feet_position.z, 0.01, 0.001,
              "the grounded capsule did not preserve its skin width");

  const auto jump = character.fixed_update(world, {0.0, 0.0, true}, 1.0 / 60.0);
  expect(!character.state().grounded && character.state().velocity.z > 0.0 &&
             character.state().feet_position.z > 0.01 && jump.left_ground,
         "a jump edge did not leave the walkable floor");
}

void test_wall_slide() {
  MeshBuilder builder;
  builder.floor(-10.0, 10.0, -10.0, 10.0, 0.0);
  builder.wall_x(2.0, -10.0, 10.0, 0.0, 4.0);
  const auto world = std::move(builder).build();
  auto character = character_at({0.0, 0.0, 1.0});
  settle(character, world);

  bool hit_wall = false;
  constexpr double diagonal = 0.7071067811865475;
  for (std::uint32_t tick = 0U; tick < 120U; ++tick) {
    const auto step =
        character.fixed_update(world, {diagonal, diagonal, false}, 1.0 / 60.0);
    hit_wall = hit_wall || step.hit_wall;
  }
  expect(hit_wall, "diagonal motion never reported the synthetic wall");
  expect(
      character.state().feet_position.x <= 1.691 &&
          character.state().feet_position.y > 2.0 && character.state().grounded,
      "the capsule did not stop across the wall normal and slide tangentially");
}

void test_step_up() {
  MeshBuilder builder;
  builder.floor(-5.0, 0.0, -4.0, 4.0, 0.0);
  builder.floor(0.0, 6.0, -4.0, 4.0, 0.375);
  builder.wall_x(0.0, -4.0, 4.0, 0.0, 0.375);
  const auto world = std::move(builder).build();
  auto character = character_at({-1.5, 0.0, 1.0});
  settle(character, world);

  bool stepped_up = false;
  for (std::uint32_t tick = 0U; tick < 90U; ++tick) {
    const auto step =
        character.fixed_update(world, {1.0, 0.0, false}, 1.0 / 60.0);
    stepped_up = stepped_up || step.stepped_up;
  }
  expect(stepped_up && character.state().grounded &&
             character.state().feet_position.x > 1.0,
         "a walkable authored step blocked forward motion");
  expect_near(character.state().feet_position.z, 0.385, 0.002,
              "the capsule did not settle on the raised floor");
}

void test_slope_limit() {
  MeshBuilder walkable_builder;
  walkable_builder.floor(-5.0, 0.0, -4.0, 4.0, 0.0);
  walkable_builder.ramp_x(0.0, 4.0, -4.0, 4.0, 0.0, 2.0);
  walkable_builder.floor(4.0, 8.0, -4.0, 4.0, 2.0);
  const auto walkable_world = std::move(walkable_builder).build();
  auto walker = character_at({-1.5, 0.0, 1.0});
  settle(walker, walkable_world);
  for (std::uint32_t tick = 0U; tick < 75U; ++tick) {
    static_cast<void>(
        walker.fixed_update(walkable_world, {1.0, 0.0, false}, 1.0 / 60.0));
  }
  expect(walker.state().feet_position.x > 1.0 &&
             walker.state().feet_position.z > 0.4 && walker.state().grounded,
         "a slope below the configured limit was not traversable");

  MeshBuilder steep_builder;
  steep_builder.floor(-5.0, 0.0, -4.0, 4.0, 0.0);
  steep_builder.ramp_x(0.0, 2.0, -4.0, 4.0, 0.0, 4.0);
  const auto steep_world = std::move(steep_builder).build();
  auto blocked = character_at({-1.5, 0.0, 1.0});
  settle(blocked, steep_world);
  bool hit_wall = false;
  for (std::uint32_t tick = 0U; tick < 75U; ++tick) {
    const auto step =
        blocked.fixed_update(steep_world, {1.0, 0.0, false}, 1.0 / 60.0);
    hit_wall = hit_wall || step.hit_wall;
  }
  expect(hit_wall && blocked.state().feet_position.x < 0.1 &&
             blocked.state().feet_position.z < 0.2,
         "a slope above the configured limit was treated as walkable ground");
}

void test_explicit_target_horizontal_speed() {
  using namespace openrc::game;

  MeshBuilder builder;
  builder.floor(-10.0, 10.0, -10.0, 10.0, 0.0);
  const auto world = std::move(builder).build();

  auto exact_profile = profile();
  exact_profile.maximum_ground_speed = 5.7;
  CharacterControllerStateV1 initial_state;
  initial_state.feet_position = {0.0, 0.0, 1.0};
  CharacterControllerV1 character(exact_profile, initial_state);
  settle(character, world);

  const CharacterMotionV1 slow_motion{0.3, 0.4, false, 0.9};
  static_cast<void>(character.fixed_update(world, slow_motion, 1.0 / 60.0));
  expect_near(character.state().velocity.x, 0.54, 1.0e-12,
              "an explicit speed did not use the normalized X direction");
  expect_near(character.state().velocity.y, 0.72, 1.0e-12,
              "an explicit speed did not use the normalized Y direction");
  expect_near(
      std::hypot(character.state().velocity.x, character.state().velocity.y),
      0.9, 1.0e-12,
      "the controller did not preserve the exact requested target");

  const auto before_invalid = character.state();
  const auto expect_invalid_speed = [&](const CharacterMotionV1 motion,
                                        const std::string &message) {
    expect_character_error(
        [&] {
          static_cast<void>(character.fixed_update(world, motion, 1.0 / 60.0));
        },
        message);
    expect(character.state() == before_invalid,
           "a rejected explicit speed partially mutated character state");
  };
  expect_invalid_speed(
      {1.0, 0.0, false, std::numeric_limits<double>::quiet_NaN()},
      "a non-finite explicit target speed was accepted");
  expect_invalid_speed({1.0, 0.0, false, -0.01},
                       "a negative explicit target speed was accepted");
  expect_invalid_speed({1.0, 0.0, false, 5.700001},
                       "an explicit target above the profile was accepted");
  expect_invalid_speed({0.0, 0.0, false, 0.9},
                       "a positive target without a direction was accepted");
}

void test_validation_hash_and_failure_atomicity() {
  using namespace openrc::game;

  CharacterControllerStateV1 positive_zero;
  CharacterControllerStateV1 negative_zero;
  negative_zero.feet_position.x = -0.0;
  negative_zero.velocity.y = -0.0;
  expect(hash_character_controller_state_v1(positive_zero) ==
             hash_character_controller_state_v1(negative_zero),
         "semantic zero values produced different character hashes");

  auto invalid_state = positive_zero;
  invalid_state.grounded = true;
  expect_character_error(
      [&] { validate_character_controller_state_v1(invalid_state); },
      "a grounded state without a supporting triangle was accepted");

  auto unbounded_profile = profile();
  unbounded_profile.maximum_motion_substeps =
      std::numeric_limits<std::uint32_t>::max();
  unbounded_profile.maximum_slide_iterations =
      std::numeric_limits<std::uint32_t>::max();
  unbounded_profile.maximum_depenetration_iterations =
      std::numeric_limits<std::uint32_t>::max();
  expect_character_error(
      [&] { validate_character_controller_profile_v1(unbounded_profile); },
      "an overflowing controller contact budget was accepted");

  MeshBuilder builder;
  builder.floor(-10.0, 10.0, -10.0, 10.0, 0.0);
  const auto world = std::move(builder).build();
  auto constrained_profile = profile();
  constrained_profile.maximum_motion_substeps = 2U;
  CharacterControllerStateV1 falling_state;
  falling_state.feet_position = {0.0, 0.0, 10.0};
  CharacterControllerV1 constrained(constrained_profile, falling_state);
  const auto before = constrained.state();
  expect_character_error(
      [&] { static_cast<void>(constrained.fixed_update(world, {}, 10.0)); },
      "motion beyond the explicit substep budget was silently accepted");
  expect(constrained.state() == before,
         "a failed controller update partially mutated replay state");
}

} // namespace

int main() {
  try {
    test_floor_landing_and_jump();
    test_wall_slide();
    test_step_up();
    test_slope_limit();
    test_explicit_target_horizontal_speed();
    test_validation_hash_and_failure_atomicity();
    std::cout << "character_controller_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "character_controller_tests: " << error.what() << '\n';
    return 1;
  }
}
