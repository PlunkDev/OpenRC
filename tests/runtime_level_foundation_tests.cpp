#include "openrc/runtime_level_foundation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kLevelId = 7U;
constexpr std::uint32_t kContentApiVersion = 3U;
constexpr openrc::CollisionWorldBuildLimitsV1 kWorldLimits{
    128U, 128U, 4096U, 16'384U, openrc::kCollisionDefaultGridCellSizeQ6V1,
};
constexpr openrc::CollisionWorldIoLimitsV1 kCollisionLimits{
    1U << 20U,
    kWorldLimits,
};
constexpr openrc::LevelBootstrapV1Limits kBootstrapLimits{
    4096U,
    16U,
};
constexpr openrc::game::RuntimeLevelFoundationLimitsV1 kRuntimeLimits{
    kContentApiVersion,
    kCollisionLimits,
    kBootstrapLimits,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_runtime_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::RuntimeLevelFoundationError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::CollisionWorldV1 make_collision_world() {
  openrc::CollisionMeshV1 mesh;
  mesh.vertices = {
      {-640, -640, 0},
      {640, -640, 0},
      {640, 640, 0},
      {-640, 640, 0},
  };
  mesh.triangles = {
      {{0U, 1U, 2U}, {}, openrc::CollisionLayerV1::world},
      {{0U, 2U, 3U}, {}, openrc::CollisionLayerV1::world},
  };
  return openrc::build_collision_world_v1(std::move(mesh), kWorldLimits);
}

[[nodiscard]] openrc::LevelBootstrapV1
make_bootstrap(const std::uint32_t level_id = kLevelId) {
  openrc::LevelBootstrapV1 result;
  result.level_id = level_id;
  result.death_height_world = -5.0;
  result.default_spawn_id = 10U;
  result.spawn_points = {
      {10U, {1.1, -2.2, 2.3}, 0.75},
      {20U, {3.25, 4.5, 6.75}, -0.5},
  };
  return result;
}

[[nodiscard]] openrc::LevelPackageResourceV1 make_resource(
    const std::string_view resource_id, const std::string_view type_id,
    const std::uint32_t schema_version, std::vector<std::byte> payload) {
  openrc::LevelPackageResourceV1 result;
  result.resource_id = std::string(resource_id);
  result.type_id = std::string(type_id);
  result.schema_version = schema_version;
  result.operation = openrc::LevelPackageResourceOperationV1::upsert;
  result.payload = std::move(payload);
  result.payload_sha256 = openrc::prepared_content_sha256_v1(result.payload);
  return result;
}

[[nodiscard]] openrc::ResolvedLevelPackageV1
make_package(const openrc::LevelBootstrapV1 &bootstrap = make_bootstrap()) {
  openrc::ResolvedLevelPackageV1 result;
  result.level_id = kLevelId;
  result.content_api_version = kContentApiVersion;
  result.build_id = "test-build";
  result.resources.push_back(make_resource(
      openrc::kLevelBootstrapResourceIdV1,
      openrc::kLevelBootstrapResourceTypeIdV1,
      openrc::kLevelBootstrapResourceSchemaVersionV1,
      openrc::encode_level_bootstrap_v1(bootstrap, kBootstrapLimits)));
  result.resources.push_back(
      make_resource(openrc::kCollisionWorldResourceIdV1,
                    openrc::kCollisionWorldResourceTypeIdV1,
                    openrc::kCollisionWorldResourceSchemaVersionV1,
                    openrc::encode_collision_world_v1(make_collision_world(),
                                                      kCollisionLimits)));
  result.resources.push_back(make_resource("scene/render",
                                           "openrc.render-scene", 1U,
                                           {std::byte{0x2a}, std::byte{0x55}}));
  return result;
}

[[nodiscard]] openrc::LevelPackageResourceV1 &
find_resource(openrc::ResolvedLevelPackageV1 &package,
              const std::string &resource_id) {
  for (auto &resource : package.resources) {
    if (resource.resource_id == resource_id) {
      return resource;
    }
  }
  throw std::runtime_error("test resource is missing");
}

[[nodiscard]] openrc::game::CharacterControllerProfileV1
make_character_profile() {
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

void test_loads_foundation_and_allows_additional_resources() {
  const auto package = make_package();
  const auto foundation =
      openrc::game::load_runtime_level_foundation_v1(package, kRuntimeLimits);
  expect(foundation.level_id == kLevelId &&
             foundation.content_api_version == kContentApiVersion &&
             foundation.build_id == "test-build" &&
             foundation.bootstrap == make_bootstrap() &&
             foundation.collision_world == make_collision_world(),
         "runtime foundation lost decoded package data");

  const auto player = openrc::game::make_runtime_level_player_simulation_v1(
      foundation, make_character_profile(), 60U, std::nullopt, 91U);
  const auto snapshot = player.snapshot();
  expect(snapshot.checkpoint.checkpoint_id == 10U &&
             snapshot.checkpoint.feet_position ==
                 make_bootstrap().spawn_points[0U].feet_position &&
             snapshot.checkpoint.facing_yaw_radians == 0.75 &&
             snapshot.character.feet_position ==
                 snapshot.checkpoint.feet_position &&
             snapshot.next_tick_index == 91U &&
             player.profile().death_height_world == -5.0,
         "default spawn did not produce an exact deterministic player start");

  const auto alternate = openrc::game::make_runtime_level_player_simulation_v1(
      foundation, make_character_profile(), 60U, 20U);
  expect(alternate.snapshot().checkpoint.checkpoint_id == 20U &&
             alternate.snapshot().checkpoint.feet_position ==
                 make_bootstrap().spawn_points[1U].feet_position,
         "explicit authored spawn selection was ignored");
}

void test_requires_exact_resource_identities() {
  for (const auto &id : {std::string(openrc::kCollisionWorldResourceIdV1),
                         std::string(openrc::kLevelBootstrapResourceIdV1)}) {
    auto missing = make_package();
    const auto iterator = std::find_if(
        missing.resources.begin(), missing.resources.end(),
        [&id](const auto &resource) { return resource.resource_id == id; });
    missing.resources.erase(iterator);
    expect_runtime_error(
        [&] {
          static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
              missing, kRuntimeLimits));
        },
        "runtime loader accepted a missing required resource");
  }

  auto duplicate = make_package();
  duplicate.resources.push_back(find_resource(
      duplicate, std::string(openrc::kCollisionWorldResourceIdV1)));
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
            duplicate, kRuntimeLimits));
      },
      "runtime loader accepted a duplicate required resource ID");

  auto wrong_type = make_package();
  find_resource(wrong_type, std::string(openrc::kCollisionWorldResourceIdV1))
      .type_id = "openrc.not-collision";
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
            wrong_type, kRuntimeLimits));
      },
      "runtime loader accepted a wrong required resource type");

  auto wrong_schema = make_package();
  find_resource(wrong_schema, std::string(openrc::kLevelBootstrapResourceIdV1))
      .schema_version = 2U;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
            wrong_schema, kRuntimeLimits));
      },
      "runtime loader accepted a wrong required resource schema");

  auto remove = make_package();
  find_resource(remove, std::string(openrc::kLevelBootstrapResourceIdV1))
      .operation = openrc::LevelPackageResourceOperationV1::remove;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
            remove, kRuntimeLimits));
      },
      "runtime loader accepted an unresolved remove operation");
}

void test_checks_runtime_policy_digest_and_payloads() {
  auto wrong_api_limits = kRuntimeLimits;
  wrong_api_limits.required_content_api_version = kContentApiVersion + 1U;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
            make_package(), wrong_api_limits));
      },
      "runtime loader accepted an incompatible content API");

  auto absent_api_limits = kRuntimeLimits;
  absent_api_limits.required_content_api_version = 0U;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
            make_package(), absent_api_limits));
      },
      "runtime loader accepted an absent content API policy");

  auto stale = make_package();
  find_resource(stale, std::string(openrc::kCollisionWorldResourceIdV1))
      .payload[0U] ^= std::byte{0x01};
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
            stale, kRuntimeLimits));
      },
      "runtime loader accepted a stale resolved payload digest");

  auto invalid_payload = make_package();
  auto &collision = find_resource(
      invalid_payload, std::string(openrc::kCollisionWorldResourceIdV1));
  collision.payload[0U] ^= std::byte{0x01};
  collision.payload_sha256 =
      openrc::prepared_content_sha256_v1(collision.payload);
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
            invalid_payload, kRuntimeLimits));
      },
      "runtime loader accepted a corrupt collision payload with a fresh hash");

  auto byte_limits = kRuntimeLimits;
  byte_limits.bootstrap.max_input_bytes = 1U;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
            make_package(), byte_limits));
      },
      "runtime loader ignored a required-resource byte limit");
}

void test_checks_cross_resource_start_invariants() {
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
            make_package(make_bootstrap(kLevelId + 1U)), kRuntimeLimits));
      },
      "runtime loader accepted a package/bootstrap level ID mismatch");

  auto far_bootstrap = make_bootstrap();
  far_bootstrap.spawn_points[0U].feet_position.x = 1.0e100;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::load_runtime_level_foundation_v1(
            make_package(far_bootstrap), kRuntimeLimits));
      },
      "runtime loader accepted a spawn outside the collision coordinate range");

  const auto foundation = openrc::game::load_runtime_level_foundation_v1(
      make_package(), kRuntimeLimits);
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::make_runtime_level_player_simulation_v1(
            foundation, make_character_profile(), 60U, 999U));
      },
      "runtime player start accepted an unknown authored spawn ID");
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::make_runtime_level_player_simulation_v1(
            foundation, make_character_profile(), 0U));
      },
      "runtime player start accepted an invalid tick policy");

  auto mismatched = foundation;
  mismatched.bootstrap.level_id += 1U;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::make_runtime_level_player_simulation_v1(
            mismatched, make_character_profile(), 60U));
      },
      "runtime player start ignored a mutated foundation identity");

  auto near_edge = foundation;
  near_edge.bootstrap.spawn_points[0U].feet_position.x =
      static_cast<double>(std::numeric_limits<std::int32_t>::max()) /
          openrc::kCollisionQ6UnitsPerWorldUnitV1 -
      0.1;
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::make_runtime_level_player_simulation_v1(
            near_edge, make_character_profile(), 60U));
      },
      "runtime player start accepted a capsule extending beyond Q6 range");
}

} // namespace

int main() {
  try {
    test_loads_foundation_and_allows_additional_resources();
    test_requires_exact_resource_identities();
    test_checks_runtime_policy_digest_and_payloads();
    test_checks_cross_resource_start_invariants();
    std::cout << "runtime_level_foundation_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime_level_foundation_tests: " << error.what() << '\n';
    return 1;
  }
}
