#include "openrc/character_controller.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace openrc::game {
namespace {

constexpr double kGeometryEpsilon = 1.0e-9;
constexpr double kContactEpsilon = 1.0e-7;
constexpr double kPi = 3.1415926535897932384626433832795;

using Vec3 = CollisionVectorV1;

struct ClosestPair {
  Vec3 segment_point;
  Vec3 triangle_point;
  double distance_squared = std::numeric_limits<double>::infinity();
};

struct Contact {
  Vec3 normal;
  double penetration = 0.0;
  std::uint32_t triangle_index = 0U;
};

struct MotionOutcome {
  Vec3 position;
  Vec3 velocity;
  bool hit_wall = false;
  bool hit_walkable_ground = false;
  bool hit_ceiling = false;
  std::uint32_t collision_count = 0U;
};

struct GroundProbe {
  double feet_z = 0.0;
  Vec3 normal{0.0, 0.0, 1.0};
  std::uint32_t triangle_index = 0U;
};

[[nodiscard]] bool finite(const Vec3 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] Vec3 add(const Vec3 a, const Vec3 b) noexcept {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] Vec3 subtract(const Vec3 a, const Vec3 b) noexcept {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3 multiply(const Vec3 value, const double scale) noexcept {
  return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] double dot(const Vec3 a, const Vec3 b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Vec3 cross(const Vec3 a, const Vec3 b) noexcept {
  return {
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
  };
}

[[nodiscard]] double length_squared(const Vec3 value) noexcept {
  return dot(value, value);
}

[[nodiscard]] double length(const Vec3 value) noexcept {
  return std::sqrt(length_squared(value));
}

[[nodiscard]] Vec3 normalized(const Vec3 value) noexcept {
  const auto value_length = length(value);
  return value_length > kGeometryEpsilon ? multiply(value, 1.0 / value_length)
                                         : Vec3{};
}

[[nodiscard]] Vec3 canonicalized(Vec3 value) noexcept {
  if (value.x == 0.0) {
    value.x = 0.0;
  }
  if (value.y == 0.0) {
    value.y = 0.0;
  }
  if (value.z == 0.0) {
    value.z = 0.0;
  }
  return value;
}

[[nodiscard]] Vec3 closest_point_on_triangle(const Vec3 point, const Vec3 a,
                                             const Vec3 b,
                                             const Vec3 c) noexcept {
  const auto ab = subtract(b, a);
  const auto ac = subtract(c, a);
  const auto ap = subtract(point, a);
  const auto d1 = dot(ab, ap);
  const auto d2 = dot(ac, ap);
  if (d1 <= 0.0 && d2 <= 0.0) {
    return a;
  }

  const auto bp = subtract(point, b);
  const auto d3 = dot(ab, bp);
  const auto d4 = dot(ac, bp);
  if (d3 >= 0.0 && d4 <= d3) {
    return b;
  }

  const auto vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
    const auto denominator = d1 - d3;
    const auto interpolation = denominator != 0.0 ? d1 / denominator : 0.0;
    return add(a, multiply(ab, interpolation));
  }

  const auto cp = subtract(point, c);
  const auto d5 = dot(ab, cp);
  const auto d6 = dot(ac, cp);
  if (d6 >= 0.0 && d5 <= d6) {
    return c;
  }

  const auto vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
    const auto denominator = d2 - d6;
    const auto interpolation = denominator != 0.0 ? d2 / denominator : 0.0;
    return add(a, multiply(ac, interpolation));
  }

  const auto va = d3 * d6 - d5 * d4;
  if (va <= 0.0 && d4 >= d3 && d5 >= d6) {
    const auto edge = subtract(c, b);
    const auto denominator = (d4 - d3) + (d5 - d6);
    const auto interpolation =
        denominator != 0.0 ? (d4 - d3) / denominator : 0.0;
    return add(b, multiply(edge, interpolation));
  }

  const auto denominator = va + vb + vc;
  if (std::abs(denominator) <= kGeometryEpsilon) {
    return a;
  }
  const auto inverse = 1.0 / denominator;
  return add(a, add(multiply(ab, vb * inverse), multiply(ac, vc * inverse)));
}

[[nodiscard]] ClosestPair closest_segment_segment(const Vec3 p1, const Vec3 q1,
                                                  const Vec3 p2,
                                                  const Vec3 q2) noexcept {
  const auto d1 = subtract(q1, p1);
  const auto d2 = subtract(q2, p2);
  const auto r = subtract(p1, p2);
  const auto a = dot(d1, d1);
  const auto e = dot(d2, d2);
  const auto f = dot(d2, r);
  double s = 0.0;
  double t = 0.0;

  if (a <= kGeometryEpsilon && e <= kGeometryEpsilon) {
    const auto difference = subtract(p1, p2);
    return {p1, p2, length_squared(difference)};
  }
  if (a <= kGeometryEpsilon) {
    t = std::clamp(f / e, 0.0, 1.0);
  } else {
    const auto c = dot(d1, r);
    if (e <= kGeometryEpsilon) {
      s = std::clamp(-c / a, 0.0, 1.0);
    } else {
      const auto b = dot(d1, d2);
      const auto denominator = a * e - b * b;
      if (std::abs(denominator) > kGeometryEpsilon) {
        s = std::clamp((b * f - c * e) / denominator, 0.0, 1.0);
      }
      t = (b * s + f) / e;
      if (t < 0.0) {
        t = 0.0;
        s = std::clamp(-c / a, 0.0, 1.0);
      } else if (t > 1.0) {
        t = 1.0;
        s = std::clamp((b - c) / a, 0.0, 1.0);
      }
    }
  }

  const auto first = add(p1, multiply(d1, s));
  const auto second = add(p2, multiply(d2, t));
  return {first, second, length_squared(subtract(first, second))};
}

void retain_closer(ClosestPair &current, const ClosestPair candidate) noexcept {
  if (candidate.distance_squared < current.distance_squared) {
    current = candidate;
  }
}

[[nodiscard]] bool point_inside_triangle(const Vec3 point, const Vec3 a,
                                         const Vec3 b, const Vec3 c,
                                         const Vec3 normal) noexcept {
  const auto first = dot(cross(subtract(b, a), subtract(point, a)), normal);
  const auto second = dot(cross(subtract(c, b), subtract(point, b)), normal);
  const auto third = dot(cross(subtract(a, c), subtract(point, c)), normal);
  return first >= -kContactEpsilon && second >= -kContactEpsilon &&
         third >= -kContactEpsilon;
}

[[nodiscard]] ClosestPair closest_segment_triangle(const Vec3 segment_start,
                                                   const Vec3 segment_end,
                                                   const Vec3 a, const Vec3 b,
                                                   const Vec3 c) noexcept {
  ClosestPair result;
  const auto start_on_triangle =
      closest_point_on_triangle(segment_start, a, b, c);
  result = {
      segment_start,
      start_on_triangle,
      length_squared(subtract(segment_start, start_on_triangle)),
  };
  const auto end_on_triangle = closest_point_on_triangle(segment_end, a, b, c);
  retain_closer(result,
                {segment_end, end_on_triangle,
                 length_squared(subtract(segment_end, end_on_triangle))});
  retain_closer(result,
                closest_segment_segment(segment_start, segment_end, a, b));
  retain_closer(result,
                closest_segment_segment(segment_start, segment_end, b, c));
  retain_closer(result,
                closest_segment_segment(segment_start, segment_end, c, a));

  const auto triangle_normal = cross(subtract(b, a), subtract(c, a));
  const auto segment_direction = subtract(segment_end, segment_start);
  const auto denominator = dot(triangle_normal, segment_direction);
  if (length_squared(triangle_normal) > kGeometryEpsilon &&
      std::abs(denominator) > kGeometryEpsilon) {
    const auto interpolation =
        dot(triangle_normal, subtract(a, segment_start)) / denominator;
    if (interpolation >= 0.0 && interpolation <= 1.0) {
      const auto intersection =
          add(segment_start, multiply(segment_direction, interpolation));
      if (point_inside_triangle(intersection, a, b, c, triangle_normal)) {
        return {intersection, intersection, 0.0};
      }
    }
  }
  return result;
}

[[nodiscard]] double
slope_cosine(const CharacterControllerProfileV1 &profile) noexcept {
  return std::cos(profile.maximum_slope_degrees * kPi / 180.0);
}

[[nodiscard]] std::pair<Vec3, Vec3>
capsule_segment(const Vec3 feet_position,
                const CharacterControllerProfileV1 &profile) noexcept {
  return {
      add(feet_position, {0.0, 0.0, profile.capsule_radius}),
      add(feet_position,
          {0.0, 0.0, profile.capsule_height - profile.capsule_radius}),
  };
}

[[nodiscard]] std::vector<Contact>
collect_contacts(const CollisionWorldV1 &world,
                 const CharacterControllerProfileV1 &profile,
                 const Vec3 feet_position, const Vec3 motion) {
  const auto effective_radius = profile.capsule_radius + profile.skin_width;
  const auto bounds = collision_world_aabb_to_q6_v1(
      {feet_position.x - effective_radius, feet_position.y - effective_radius,
       feet_position.z - profile.skin_width},
      {feet_position.x + effective_radius, feet_position.y + effective_radius,
       feet_position.z + profile.capsule_height + profile.skin_width});
  const auto candidates = query_collision_candidates_v1(
      world, bounds, profile.collision_layers, profile.query_limits);
  const auto [segment_start, segment_end] =
      capsule_segment(feet_position, profile);

  std::vector<Contact> contacts;
  contacts.reserve(candidates.size());
  for (const auto triangle_index : candidates) {
    const auto &triangle = world.mesh.triangles[triangle_index];
    const auto a = collision_q6_position_to_world_v1(
        world.mesh.vertices[triangle.vertex_indices[0U]]);
    const auto b = collision_q6_position_to_world_v1(
        world.mesh.vertices[triangle.vertex_indices[1U]]);
    const auto c = collision_q6_position_to_world_v1(
        world.mesh.vertices[triangle.vertex_indices[2U]]);
    const auto triangle_cross = cross(subtract(b, a), subtract(c, a));
    if (length_squared(triangle_cross) <= kGeometryEpsilon) {
      continue;
    }

    const auto closest =
        closest_segment_triangle(segment_start, segment_end, a, b, c);
    if (closest.distance_squared >=
        effective_radius * effective_radius - kContactEpsilon) {
      continue;
    }

    const auto distance_value =
        std::sqrt(std::max(0.0, closest.distance_squared));
    auto normal =
        distance_value > kGeometryEpsilon
            ? multiply(subtract(closest.segment_point, closest.triangle_point),
                       1.0 / distance_value)
            : normalized(triangle_cross);
    if (distance_value <= kGeometryEpsilon) {
      const auto capsule_center =
          multiply(add(segment_start, segment_end), 0.5);
      const auto triangle_center = multiply(add(add(a, b), c), 1.0 / 3.0);
      const auto center_direction = subtract(capsule_center, triangle_center);
      if (dot(normal, center_direction) < 0.0 ||
          (std::abs(dot(normal, center_direction)) <= kGeometryEpsilon &&
           dot(normal, motion) > 0.0)) {
        normal = multiply(normal, -1.0);
      }
    }
    if (length_squared(normal) <= kGeometryEpsilon) {
      continue;
    }
    contacts.push_back({
        normal,
        effective_radius - distance_value,
        triangle_index,
    });
  }
  return contacts;
}

void resolve_overlaps(const CollisionWorldV1 &world,
                      const CharacterControllerProfileV1 &profile,
                      const Vec3 motion, MotionOutcome &outcome) {
  const auto walkable_cosine = slope_cosine(profile);
  for (std::uint32_t iteration = 0U;
       iteration < profile.maximum_depenetration_iterations; ++iteration) {
    const auto contacts =
        collect_contacts(world, profile, outcome.position, motion);
    if (contacts.empty()) {
      return;
    }
    const auto selected =
        std::max_element(contacts.begin(), contacts.end(),
                         [](const Contact &left, const Contact &right) {
                           if (left.penetration != right.penetration) {
                             return left.penetration < right.penetration;
                           }
                           return left.triangle_index > right.triangle_index;
                         });
    outcome.position =
        add(outcome.position, multiply(selected->normal, selected->penetration +
                                                             kContactEpsilon));
    const auto velocity_into_surface = dot(outcome.velocity, selected->normal);
    if (velocity_into_surface < 0.0) {
      outcome.velocity = subtract(
          outcome.velocity, multiply(selected->normal, velocity_into_surface));
    }
    if (selected->normal.z >= walkable_cosine) {
      outcome.hit_walkable_ground = true;
    } else if (selected->normal.z <= -kContactEpsilon) {
      outcome.hit_ceiling = true;
    } else {
      outcome.hit_wall = true;
    }
    ++outcome.collision_count;
  }
  if (!collect_contacts(world, profile, outcome.position, motion).empty()) {
    throw CharacterControllerError(
        "Character depenetration exceeded its explicit iteration budget");
  }
}

[[nodiscard]] MotionOutcome
move_capsule(const CollisionWorldV1 &world,
             const CharacterControllerProfileV1 &profile, const Vec3 start,
             const Vec3 displacement, const Vec3 velocity) {
  MotionOutcome outcome;
  outcome.position = start;
  outcome.velocity = velocity;
  const auto distance_value = length(displacement);
  const auto requested_substeps = std::max(
      1.0, std::ceil(distance_value / profile.maximum_substep_distance));
  if (requested_substeps >
      static_cast<double>(profile.maximum_motion_substeps)) {
    throw CharacterControllerError(
        "Character motion exceeded its explicit substep budget");
  }
  const auto substep_count = static_cast<std::uint32_t>(requested_substeps);
  const auto substep = multiply(displacement, 1.0 / substep_count);
  for (std::uint32_t index = 0U; index < substep_count; ++index) {
    outcome.position = add(outcome.position, substep);
    for (std::uint32_t slide = 0U; slide < profile.maximum_slide_iterations;
         ++slide) {
      const auto before = outcome.position;
      resolve_overlaps(world, profile, substep, outcome);
      if (length_squared(subtract(outcome.position, before)) <=
          kContactEpsilon * kContactEpsilon) {
        break;
      }
    }
  }
  return outcome;
}

[[nodiscard]] std::optional<GroundProbe>
probe_ground(const CollisionWorldV1 &world,
             const CharacterControllerProfileV1 &profile,
             const Vec3 feet_position, const double maximum_down,
             const double maximum_up, const Vec3 horizontal_offset = {}) {
  const auto ray = CollisionRayV1{
      add(feet_position,
          {
              horizontal_offset.x,
              horizontal_offset.y,
              maximum_up,
          }),
      {0.0, 0.0, -1.0},
      maximum_up + maximum_down,
  };
  const auto hit = raycast_collision_world_v1(
      world, ray, profile.collision_layers, profile.query_limits);
  if (!hit) {
    return std::nullopt;
  }
  auto normal = normalized(hit->normal);
  if (normal.z < 0.0) {
    normal = multiply(normal, -1.0);
  }
  const auto walkable_cosine = slope_cosine(profile);
  if (normal.z < walkable_cosine) {
    return std::nullopt;
  }
  const auto effective_radius = profile.capsule_radius + profile.skin_width;
  const auto surface_height_at_capsule_center =
      hit->position.z +
      (normal.x * horizontal_offset.x + normal.y * horizontal_offset.y) /
          std::max(normal.z, kGeometryEpsilon);
  const auto support_height =
      effective_radius / std::max(normal.z, kGeometryEpsilon) -
      profile.capsule_radius;
  const auto feet_z = surface_height_at_capsule_center + support_height;
  const auto height_delta = feet_z - feet_position.z;
  if (height_delta > maximum_up + kContactEpsilon ||
      height_delta < -maximum_down - kContactEpsilon) {
    return std::nullopt;
  }
  return GroundProbe{feet_z, normal, hit->triangle_index};
}

[[nodiscard]] Vec3 move_towards_xy(const Vec3 current, const Vec3 target,
                                   const double maximum_delta) noexcept {
  const Vec3 difference{target.x - current.x, target.y - current.y, 0.0};
  const auto difference_length = std::hypot(difference.x, difference.y);
  if (difference_length <= maximum_delta ||
      difference_length <= kGeometryEpsilon) {
    return {target.x, target.y, current.z};
  }
  const auto scale = maximum_delta / difference_length;
  return {
      current.x + difference.x * scale,
      current.y + difference.y * scale,
      current.z,
  };
}

void combine_result(CharacterStepResultV1 &result,
                    const MotionOutcome &motion) noexcept {
  result.hit_wall = result.hit_wall || motion.hit_wall;
  const auto remaining =
      std::numeric_limits<std::uint32_t>::max() - result.collision_count;
  result.collision_count += std::min(remaining, motion.collision_count);
}

void hash_byte(std::uint64_t &hash, const std::uint8_t value) noexcept {
  hash ^= value;
  hash *= UINT64_C(1099511628211);
}

void hash_u32(std::uint64_t &hash, const std::uint32_t value) noexcept {
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
    hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

void hash_u64(std::uint64_t &hash, const std::uint64_t value) noexcept {
  for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
    hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

void hash_double(std::uint64_t &hash, const double value) noexcept {
  const auto canonical = value == 0.0 ? 0.0 : value;
  hash_u64(hash, std::bit_cast<std::uint64_t>(canonical));
}

} // namespace

void validate_character_controller_profile_v1(
    const CharacterControllerProfileV1 &profile) {
  const std::array floating_values{
      profile.capsule_radius,
      profile.capsule_height,
      profile.skin_width,
      profile.ground_probe_distance,
      profile.step_height,
      profile.maximum_slope_degrees,
      profile.maximum_ground_speed,
      profile.ground_acceleration,
      profile.ground_deceleration,
      profile.air_acceleration,
      profile.gravity,
      profile.jump_speed,
      profile.maximum_fall_speed,
      profile.maximum_substep_distance,
  };
  if (std::ranges::any_of(
          floating_values,
          [](const double value) { return !std::isfinite(value); }) ||
      !(profile.capsule_radius > 0.0) ||
      profile.capsule_height < 2.0 * profile.capsule_radius ||
      profile.skin_width < 0.0 ||
      profile.skin_width >= profile.capsule_radius ||
      profile.ground_probe_distance < 0.0 || profile.step_height < 0.0 ||
      !(profile.maximum_slope_degrees > 0.0) ||
      !(profile.maximum_slope_degrees < 90.0) ||
      profile.maximum_ground_speed < 0.0 || profile.ground_acceleration < 0.0 ||
      profile.ground_deceleration < 0.0 || profile.air_acceleration < 0.0 ||
      !(profile.gravity > 0.0) || profile.jump_speed < 0.0 ||
      !(profile.maximum_fall_speed > 0.0) ||
      !(profile.maximum_substep_distance > 0.0) ||
      profile.maximum_motion_substeps == 0U ||
      profile.maximum_slide_iterations == 0U ||
      profile.maximum_depenetration_iterations == 0U ||
      profile.query_limits.max_cells_to_visit == 0U ||
      profile.query_limits.max_candidates == 0U ||
      profile.collision_layers == 0U ||
      (profile.collision_layers & ~kCollisionAllLayersMaskV1) != 0U) {
    throw CharacterControllerError(
        "A character-controller profile has invalid limits or policy");
  }
  const auto counter_maximum =
      static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
  const auto motion_substeps =
      static_cast<std::uint64_t>(profile.maximum_motion_substeps);
  const auto slide_iterations =
      static_cast<std::uint64_t>(profile.maximum_slide_iterations);
  const auto depenetration_iterations =
      static_cast<std::uint64_t>(profile.maximum_depenetration_iterations);
  if (motion_substeps > counter_maximum / slide_iterations ||
      motion_substeps * slide_iterations >
          counter_maximum / depenetration_iterations) {
    throw CharacterControllerError(
        "A character-controller contact budget exceeds its counter domain");
  }
}

void validate_character_controller_state_v1(
    const CharacterControllerStateV1 &state) {
  const auto ground_normal_length_squared = length_squared(state.ground_normal);
  if (!finite(state.feet_position) || !finite(state.velocity) ||
      !finite(state.ground_normal) ||
      std::abs(ground_normal_length_squared - 1.0) > 1.0e-6 ||
      (state.grounded && (!state.ground_triangle_index.has_value() ||
                          !(state.ground_normal.z > 0.0))) ||
      (!state.grounded && state.ground_triangle_index.has_value())) {
    throw CharacterControllerError(
        "A character-controller state contains invalid vectors");
  }
}

std::uint64_t
hash_character_controller_state_v1(const CharacterControllerStateV1 &state) {
  validate_character_controller_state_v1(state);
  std::uint64_t hash = UINT64_C(14695981039346656037);
  hash_double(hash, state.feet_position.x);
  hash_double(hash, state.feet_position.y);
  hash_double(hash, state.feet_position.z);
  hash_double(hash, state.velocity.x);
  hash_double(hash, state.velocity.y);
  hash_double(hash, state.velocity.z);
  hash_double(hash, state.ground_normal.x);
  hash_double(hash, state.ground_normal.y);
  hash_double(hash, state.ground_normal.z);
  hash_byte(hash, state.ground_triangle_index ? 1U : 0U);
  if (state.ground_triangle_index) {
    hash_u32(hash, *state.ground_triangle_index);
  }
  hash_byte(hash, state.grounded ? 1U : 0U);
  return hash;
}

CharacterControllerV1::CharacterControllerV1(
    CharacterControllerProfileV1 profile,
    CharacterControllerStateV1 initial_state)
    : profile_(std::move(profile)), state_(std::move(initial_state)) {
  validate_character_controller_profile_v1(profile_);
  validate_character_controller_state_v1(state_);
}

CharacterStepResultV1
CharacterControllerV1::fixed_update(const CollisionWorldV1 &collision_world,
                                    CharacterMotionV1 motion,
                                    const double fixed_delta_seconds) {
  if (!std::isfinite(motion.move_x) || !std::isfinite(motion.move_y) ||
      !std::isfinite(fixed_delta_seconds) || !(fixed_delta_seconds > 0.0)) {
    throw CharacterControllerError(
        "A character fixed update has invalid motion or duration");
  }
  const auto input_length = std::hypot(motion.move_x, motion.move_y);
  if (input_length > 1.0 + kContactEpsilon) {
    throw CharacterControllerError(
        "A character movement input exceeds the unit circle");
  }
  if (motion.target_horizontal_speed &&
      (!std::isfinite(*motion.target_horizontal_speed) ||
       *motion.target_horizontal_speed < 0.0 ||
       *motion.target_horizontal_speed > profile_.maximum_ground_speed)) {
    throw CharacterControllerError(
        "A character target horizontal speed is outside its profile");
  }
  if (motion.target_horizontal_speed && *motion.target_horizontal_speed > 0.0 &&
      input_length == 0.0) {
    throw CharacterControllerError(
        "A positive character target horizontal speed requires a direction");
  }
  if (input_length > 1.0) {
    motion.move_x /= input_length;
    motion.move_y /= input_length;
  }
  const auto direction_length = std::hypot(motion.move_x, motion.move_y);

  CharacterStepResultV1 result;
  auto state = state_;
  const auto was_grounded = state.grounded;
  auto initial_probe = probe_ground(
      collision_world, profile_, state.feet_position,
      profile_.ground_probe_distance, profile_.skin_width + kContactEpsilon);
  const auto prior_horizontal_speed =
      std::hypot(state.velocity.x, state.velocity.y);
  if (!initial_probe && state.grounded &&
      prior_horizontal_speed > kGeometryEpsilon) {
    const auto footprint_offset =
        multiply({state.velocity.x, state.velocity.y, 0.0},
                 (profile_.capsule_radius + profile_.skin_width) /
                     prior_horizontal_speed);
    initial_probe =
        probe_ground(collision_world, profile_, state.feet_position,
                     profile_.ground_probe_distance,
                     profile_.skin_width + kContactEpsilon, footprint_offset);
  }
  if (initial_probe && state.velocity.z <= 0.0) {
    state.feet_position.z = initial_probe->feet_z;
    state.ground_normal = initial_probe->normal;
    state.ground_triangle_index = initial_probe->triangle_index;
    state.grounded = true;
    state.velocity.z = 0.0;
  } else {
    state.grounded = false;
    state.ground_triangle_index.reset();
    state.ground_normal = {0.0, 0.0, 1.0};
  }

  auto target_velocity = Vec3{
      motion.move_x * profile_.maximum_ground_speed,
      motion.move_y * profile_.maximum_ground_speed,
      state.velocity.z,
  };
  auto has_movement = input_length > kGeometryEpsilon;
  if (motion.target_horizontal_speed) {
    target_velocity.x = 0.0;
    target_velocity.y = 0.0;
    if (*motion.target_horizontal_speed > 0.0) {
      target_velocity.x =
          motion.move_x / direction_length * *motion.target_horizontal_speed;
      target_velocity.y =
          motion.move_y / direction_length * *motion.target_horizontal_speed;
    }
    has_movement = *motion.target_horizontal_speed > 0.0;
  }
  const auto acceleration = state.grounded
                                ? (has_movement ? profile_.ground_acceleration
                                                : profile_.ground_deceleration)
                                : profile_.air_acceleration;
  state.velocity = move_towards_xy(state.velocity, target_velocity,
                                   acceleration * fixed_delta_seconds);

  if (motion.jump_pressed && state.grounded) {
    state.velocity.z = profile_.jump_speed;
    state.grounded = false;
    state.ground_triangle_index.reset();
  } else if (!state.grounded) {
    state.velocity.z =
        std::max(-profile_.maximum_fall_speed,
                 state.velocity.z - profile_.gravity * fixed_delta_seconds);
  } else {
    state.velocity.z = 0.0;
  }

  Vec3 horizontal_displacement{
      state.velocity.x * fixed_delta_seconds,
      state.velocity.y * fixed_delta_seconds,
      0.0,
  };
  if (state.grounded && state.ground_normal.z > kGeometryEpsilon) {
    horizontal_displacement.z =
        -(state.ground_normal.x * horizontal_displacement.x +
          state.ground_normal.y * horizontal_displacement.y) /
        state.ground_normal.z;
  }

  const auto horizontal_start = state.feet_position;
  auto horizontal = move_capsule(collision_world, profile_, horizontal_start,
                                 horizontal_displacement, state.velocity);
  if (horizontal.hit_wall && state.grounded && !motion.jump_pressed &&
      profile_.step_height > 0.0 &&
      std::hypot(horizontal_displacement.x, horizontal_displacement.y) >
          kGeometryEpsilon) {
    auto raised_start = horizontal_start;
    raised_start.z += profile_.step_height;
    const auto horizontal_length =
        std::hypot(horizontal_displacement.x, horizontal_displacement.y);
    const auto footprint_offset = multiply(
        {
            horizontal_displacement.x,
            horizontal_displacement.y,
            0.0,
        },
        (profile_.capsule_radius + profile_.skin_width) / horizontal_length);
    auto stepped = move_capsule(
        collision_world, profile_, raised_start,
        {horizontal_displacement.x, horizontal_displacement.y, 0.0},
        state.velocity);
    const auto landing =
        probe_ground(collision_world, profile_, stepped.position,
                     profile_.step_height + profile_.ground_probe_distance,
                     profile_.skin_width + kContactEpsilon, footprint_offset);
    if (landing) {
      const auto normal_progress =
          std::hypot(horizontal.position.x - horizontal_start.x,
                     horizontal.position.y - horizontal_start.y);
      const auto stepped_progress =
          std::hypot(stepped.position.x - horizontal_start.x,
                     stepped.position.y - horizontal_start.y);
      const auto landing_rise = landing->feet_z - horizontal_start.z;
      if (stepped_progress > normal_progress + kContactEpsilon &&
          landing_rise > kContactEpsilon &&
          landing_rise <= profile_.step_height + kContactEpsilon) {
        stepped.position.z = landing->feet_z;
        if (collect_contacts(collision_world, profile_, stepped.position,
                             horizontal_displacement)
                .empty()) {
          horizontal = stepped;
          state.ground_normal = landing->normal;
          state.ground_triangle_index = landing->triangle_index;
          state.grounded = true;
          result.stepped_up = true;
        }
      }
    }
  }
  state.feet_position = horizontal.position;
  state.velocity = horizontal.velocity;
  if (state.grounded && !motion.jump_pressed) {
    // Ground-tangent collision projection can contain a positive Z
    // component. Vertical gameplay velocity remains zero while supported;
    // the tangent displacement above already performed the climb.
    state.velocity.z = 0.0;
  }
  combine_result(result, horizontal);

  if (!state.grounded || motion.jump_pressed) {
    const auto vertical = move_capsule(
        collision_world, profile_, state.feet_position,
        {0.0, 0.0, state.velocity.z * fixed_delta_seconds}, state.velocity);
    state.feet_position = vertical.position;
    state.velocity = vertical.velocity;
    combine_result(result, vertical);
  }

  const auto final_probe = probe_ground(
      collision_world, profile_, state.feet_position,
      profile_.ground_probe_distance, profile_.skin_width + kContactEpsilon);
  if (final_probe && state.velocity.z <= 0.0) {
    state.feet_position.z = final_probe->feet_z;
    state.velocity.z = 0.0;
    state.ground_normal = final_probe->normal;
    state.ground_triangle_index = final_probe->triangle_index;
    state.grounded = true;
  } else if (!result.stepped_up) {
    state.grounded = false;
    state.ground_triangle_index.reset();
    state.ground_normal = {0.0, 0.0, 1.0};
  }

  result.landed = !was_grounded && state.grounded;
  result.left_ground = was_grounded && !state.grounded;
  state.feet_position = canonicalized(state.feet_position);
  state.velocity = canonicalized(state.velocity);
  state.ground_normal = canonicalized(state.ground_normal);
  validate_character_controller_state_v1(state);
  state_ = state;
  return result;
}

void CharacterControllerV1::set_state(const CharacterControllerStateV1 &state) {
  validate_character_controller_state_v1(state);
  state_ = state;
}

const CharacterControllerProfileV1 &
CharacterControllerV1::profile() const noexcept {
  return profile_;
}

const CharacterControllerStateV1 &
CharacterControllerV1::state() const noexcept {
  return state_;
}

} // namespace openrc::game
