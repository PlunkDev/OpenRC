#include "openrc/rac_level_foundation_compile.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kCollisionBytes = 0x180U;
constexpr std::size_t kMeshOffset = 0x40U;
constexpr std::size_t kHeroOffset = 0x100U;
constexpr std::size_t kLeafOffset = 0x20U;

constexpr std::uint32_t kGameplayBytes = 0x510U;
constexpr std::uint32_t kLevelSettingsOffset = 0xb0U;
constexpr std::uint32_t kMobyClassesOffset = 0x1c0U;
constexpr std::uint32_t kMobyInstancesOffset = 0x1d0U;
constexpr std::array<std::uint32_t, openrc::kRacGameplayBlockCountV1>
    kPhysicalPointerSlots{
        0x88U, 0x00U, 0x10U, 0x14U, 0x18U, 0x1cU, 0x20U, 0x24U, 0x28U,
        0x2cU, 0x04U, 0x80U, 0x08U, 0x0cU, 0x40U, 0x44U, 0x54U, 0x58U,
        0x50U, 0x5cU, 0x48U, 0x4cU, 0x30U, 0x34U, 0x38U, 0x3cU, 0x70U,
        0x60U, 0x64U, 0x68U, 0x6cU, 0x84U, 0x7cU, 0x78U, 0x74U, 0x8cU};

constexpr openrc::CollisionWorldBuildLimitsV1 kWorldLimits{
    4096U,
    4096U,
    4096U,
    65'536U,
    openrc::kCollisionDefaultGridCellSizeQ6V1,
};

constexpr openrc::RacLevelFoundationCompileLimitsV1 kLimits{
    {
        0x1000U,
        64U,
        64U,
        64U,
        64U,
        1024U,
        1024U,
        64U,
        1024U,
        1024U,
    },
    {0x10000U},
    {
        64U,
        1024U,
        1024U,
        64U,
        1024U,
        1024U,
        kWorldLimits,
    },
    {1024U * 1024U, kWorldLimits},
    {16U * 1024U, 16U},
    {
        4U * 1024U * 1024U,
        8U,
        8U,
        32U,
        256U,
        2U * 1024U * 1024U,
        4U * 1024U * 1024U,
        16U,
    },
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Function>
void expect_compile_rejected(Function &&function, const std::string &message) {
  try {
    std::invoke(std::forward<Function>(function));
  } catch (const openrc::RacLevelFoundationCompileError &) {
    return;
  }
  throw std::runtime_error(message);
}

template <typename Function>
void expect_package_rejected(Function &&function, const std::string &message) {
  try {
    std::invoke(std::forward<Function>(function));
  } catch (const openrc::LevelPackageV1Error &) {
    return;
  }
  throw std::runtime_error(message);
}

void write_u16(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint16_t value) {
  bytes.at(offset) = static_cast<std::byte>(value & 0xffU);
  bytes.at(offset + 1U) = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_i16(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::int16_t value) {
  write_u16(bytes, offset, std::bit_cast<std::uint16_t>(value));
}

void write_u32(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint32_t value) {
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes.at(offset + index) =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void write_i32(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::int32_t value) {
  write_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] std::uint32_t packed_signed(const std::int32_t value,
                                          const std::uint32_t bits) {
  return static_cast<std::uint32_t>(value) &
         ((UINT32_C(1) << bits) - 1U);
}

[[nodiscard]] std::uint32_t pack_vertex(const std::int32_t x_sixteenths,
                                        const std::int32_t y_sixteenths,
                                        const std::int32_t z_sixty_fourths) {
  return packed_signed(x_sixteenths, 10U) |
         (packed_signed(y_sixteenths, 10U) << 10U) |
         (packed_signed(z_sixty_fourths, 12U) << 20U);
}

[[nodiscard]] std::vector<std::byte> make_collision() {
  std::vector<std::byte> bytes(kCollisionBytes, std::byte{0});
  write_i32(bytes, 0x00U, static_cast<std::int32_t>(kMeshOffset));
  write_i32(bytes, 0x04U, static_cast<std::int32_t>(kHeroOffset));

  write_i16(bytes, kMeshOffset + 0x00U, -2);
  write_u16(bytes, kMeshOffset + 0x02U, 2U);
  write_u16(bytes, kMeshOffset + 0x04U, 0U);
  write_u16(bytes, kMeshOffset + 0x06U, 2U);
  write_i16(bytes, kMeshOffset + 0x08U, 3);
  write_u16(bytes, kMeshOffset + 0x0aU, 1U);
  write_u32(bytes, kMeshOffset + 0x0cU, 0x10U);
  write_i16(bytes, kMeshOffset + 0x10U, -1);
  write_u16(bytes, kMeshOffset + 0x12U, 2U);
  write_u32(bytes, kMeshOffset + 0x14U, 0U);
  write_u32(bytes, kMeshOffset + 0x18U,
            (static_cast<std::uint32_t>(kLeafOffset) << 8U) | 2U);

  const auto leaf = kMeshOffset + kLeafOffset;
  write_u16(bytes, leaf, 2U);
  bytes.at(leaf + 0x02U) = std::byte{4U};
  bytes.at(leaf + 0x03U) = std::byte{1U};
  write_u32(bytes, leaf + 0x04U, pack_vertex(-16, -16, 0));
  write_u32(bytes, leaf + 0x08U, pack_vertex(16, -16, 0));
  write_u32(bytes, leaf + 0x0cU, pack_vertex(16, 16, 0));
  write_u32(bytes, leaf + 0x10U, pack_vertex(-16, 16, 0));
  bytes.at(leaf + 0x14U) = std::byte{0U};
  bytes.at(leaf + 0x15U) = std::byte{1U};
  bytes.at(leaf + 0x16U) = std::byte{2U};
  bytes.at(leaf + 0x17U) = std::byte{7U};
  bytes.at(leaf + 0x18U) = std::byte{0U};
  bytes.at(leaf + 0x19U) = std::byte{2U};
  bytes.at(leaf + 0x1aU) = std::byte{3U};
  bytes.at(leaf + 0x1bU) = std::byte{0x81U};
  bytes.at(leaf + 0x1cU) = std::byte{3U};

  write_i32(bytes, kHeroOffset, 1);
  const auto hero_table = kHeroOffset + 0x10U;
  write_u16(bytes, hero_table + 0x00U, 64U);
  write_u16(bytes, hero_table + 0x02U, 128U);
  write_u16(bytes, hero_table + 0x04U, 192U);
  write_u16(bytes, hero_table + 0x06U, 256U);
  write_u16(bytes, hero_table + 0x08U, 1U);
  write_u16(bytes, hero_table + 0x0aU, 3U);
  write_u32(bytes, hero_table + 0x0cU, 0x20U);

  const auto hero_data = kHeroOffset + 0x20U;
  constexpr std::array<std::array<std::uint16_t, 3U>, 3U> kHeroVertices{{
      {64U, 128U, 192U},
      {128U, 128U, 192U},
      {64U, 192U, 192U},
  }};
  for (std::size_t vertex = 0U; vertex < kHeroVertices.size(); ++vertex) {
    for (std::size_t component = 0U; component < 3U; ++component) {
      write_u16(bytes, hero_data + vertex * 8U + component * 2U,
                kHeroVertices[vertex][component]);
    }
  }
  bytes.at(hero_data + 0x18U) = std::byte{0U};
  bytes.at(hero_data + 0x19U) = std::byte{1U};
  bytes.at(hero_data + 0x1aU) = std::byte{2U};
  return bytes;
}

[[nodiscard]] std::vector<std::byte> make_gameplay() {
  std::vector<std::byte> bytes(kGameplayBytes, std::byte{0});
  auto block_offset = openrc::kRacGameplayFirstBlockOffsetV1;
  for (std::size_t index = 0U; index < kPhysicalPointerSlots.size(); ++index) {
    write_u32(bytes, kPhysicalPointerSlots[index], block_offset);
    if (index == 1U) {
      block_offset += 0x50U;
    } else if (index == 15U) {
      block_offset += 0x90U;
    } else if (index == 23U) {
      block_offset += 0x100U;
    } else if (index == 25U) {
      block_offset += 0x90U;
    } else {
      block_offset += 0x10U;
    }
  }

  write_u32(bytes, kLevelSettingsOffset + 0x18U,
            std::bit_cast<std::uint32_t>(20.0F));
  write_u32(bytes, kLevelSettingsOffset + 0x1cU,
            std::bit_cast<std::uint32_t>(120.0F));
  write_u32(bytes, kLevelSettingsOffset + 0x20U,
            std::bit_cast<std::uint32_t>(0.25F));
  write_u32(bytes, kLevelSettingsOffset + 0x24U,
            std::bit_cast<std::uint32_t>(0.75F));
  write_u32(bytes, kLevelSettingsOffset + 0x28U,
            std::bit_cast<std::uint32_t>(27.0F));
  write_u32(bytes, kLevelSettingsOffset + 0x2cU,
            std::bit_cast<std::uint32_t>(90.0F));
  write_u32(bytes, kLevelSettingsOffset + 0x30U,
            std::bit_cast<std::uint32_t>(91.0F));
  write_u32(bytes, kLevelSettingsOffset + 0x34U,
            std::bit_cast<std::uint32_t>(92.0F));
  write_u32(bytes, kLevelSettingsOffset + 0x38U,
            std::bit_cast<std::uint32_t>(0.5F));

  write_u32(bytes, kMobyClassesOffset, 1U);
  write_u32(bytes, kMobyClassesOffset + 4U, 0U);
  write_u32(bytes, kMobyInstancesOffset, 1U);
  write_u32(bytes, kMobyInstancesOffset + 4U, 0U);
  write_u32(bytes, kMobyInstancesOffset + 0x10U,
            openrc::kRacGameplayMobyRecordBytesV1);
  const auto player = kMobyInstancesOffset + 0x10U;
  write_u32(bytes, player + 0x18U, 0U);
  write_u32(bytes, player + 0x1cU,
            std::bit_cast<std::uint32_t>(1.0F));
  write_u32(bytes, player + 0x30U,
            std::bit_cast<std::uint32_t>(132.0F));
  write_u32(bytes, player + 0x34U,
            std::bit_cast<std::uint32_t>(115.5F));
  write_u32(bytes, player + 0x38U,
            std::bit_cast<std::uint32_t>(31.25F));
  write_u32(bytes, player + 0x3cU,
            std::bit_cast<std::uint32_t>(0.1F));
  write_u32(bytes, player + 0x40U,
            std::bit_cast<std::uint32_t>(0.2F));
  write_u32(bytes, player + 0x44U,
            std::bit_cast<std::uint32_t>(0.625F));
  return bytes;
}

struct Fixture final {
  std::vector<std::byte> collision = make_collision();
  std::vector<std::byte> gameplay = make_gameplay();

  [[nodiscard]] openrc::RacLevelFoundationCompileRequestV1
  request(const std::uint32_t level_id = 18U) const {
    return {
        level_id,
        1U,
        "SCES-50916-PAL-v2.00",
        {"rac1/level/018/core/collision", collision},
        {"rac1/level/018/gameplay", gameplay},
    };
  }
};

[[nodiscard]] const openrc::LevelPackageResourceV1 &
resource(const openrc::LevelPackageV1 &package,
         const std::string_view resource_id) {
  for (const auto &candidate : package.resources) {
    if (candidate.resource_id == resource_id) {
      return candidate;
    }
  }
  throw std::runtime_error("compiled package is missing a required resource");
}

void expect_full_provenance(
    const openrc::LevelPackageResourceV1 &compiled,
    const openrc::RacLevelFoundationSourceBytesV1 &source,
    const std::string_view pass) {
  expect(compiled.provenance.size() == 2U,
         "foundation resource does not have exact source/pass provenance");
  const auto &direct = compiled.provenance[0U];
  const auto &generated = compiled.provenance[1U];
  expect(direct.kind ==
                 openrc::LevelPackageProvenanceKindV1::prepared_resource &&
             direct.source_locator == source.logical_locator &&
             direct.source_offset == 0U &&
             direct.source_bytes == source.bytes.size() &&
             direct.source_sha256 ==
                 openrc::prepared_content_sha256_v1(source.bytes),
         "foundation direct-source provenance is incomplete");
  expect(generated.kind ==
                 openrc::LevelPackageProvenanceKindV1::generated &&
             generated.source_locator == pass &&
             generated.source_offset == 0U && generated.source_bytes == 0U &&
             openrc::is_zero_prepared_digest_v1(generated.source_sha256),
         "foundation generated-pass provenance is incomplete");
}

void test_deterministic_canonical_package_and_neutral_payloads() {
  const Fixture fixture;
  const auto request = fixture.request();
  const auto first =
      openrc::compile_rac_level_foundation_package_v1(request, kLimits);
  const auto second =
      openrc::compile_rac_level_foundation_package_v1(request, kLimits);
  const auto first_bytes = openrc::encode_level_package_v1(first, kLimits.package);
  const auto second_bytes =
      openrc::encode_level_package_v1(second, kLimits.package);

  expect(first_bytes == second_bytes,
         "foundation package bytes are not deterministic");
  expect(first.level_id == 18U && first.content_api_version == 1U &&
             first.build_id == "SCES-50916-PAL-v2.00" &&
             first.layer_kind == openrc::LevelPackageLayerKindV1::base &&
             first.layer_id == "base" && first.priority == 0 &&
             openrc::is_zero_prepared_digest_v1(
                 first.required_base_package_sha256) &&
             first.resources.size() == 2U &&
             first.resources[0U].resource_id == "world/bootstrap" &&
             first.resources[1U].resource_id == "world/collision",
         "foundation package identity or canonical resource order is wrong");

  const auto &bootstrap_resource =
      resource(first, openrc::kLevelBootstrapResourceIdV1);
  const auto &collision_resource =
      resource(first, openrc::kCollisionWorldResourceIdV1);
  for (const auto *const compiled :
       std::array{&bootstrap_resource, &collision_resource}) {
    expect(compiled->operation ==
                   openrc::LevelPackageResourceOperationV1::upsert &&
               compiled->flags ==
                   openrc::kLevelPackageResourceOverlayReplaceableV1 &&
               (compiled->flags &
                openrc::kLevelPackageResourceOverlayRemovableV1) == 0U &&
               compiled->payload_sha256 ==
                   openrc::prepared_content_sha256_v1(compiled->payload),
           "foundation resource has wrong operation, flags, or digest");
  }
  expect(bootstrap_resource.type_id == "openrc.level-bootstrap" &&
             bootstrap_resource.schema_version == 1U &&
             collision_resource.type_id == "openrc.collision-world" &&
             collision_resource.schema_version == 1U,
         "foundation resource schema identity is wrong");
  expect_full_provenance(bootstrap_resource, request.gameplay,
                         openrc::kRacLevelBootstrapCompilePassV1);
  expect_full_provenance(collision_resource, request.collision,
                         openrc::kRacCollisionWorldCompilePassV1);

  const auto bootstrap = openrc::parse_level_bootstrap_v1(
      bootstrap_resource.payload, kLimits.bootstrap_payload);
  expect(bootstrap.level_id == 18U && bootstrap.death_height_world == 27.0 &&
             bootstrap.default_spawn_id == 0U &&
             bootstrap.spawn_points.size() == 1U &&
             bootstrap.spawn_points.front().feet_position ==
                 openrc::CollisionVectorV1{132.0, 115.5, 31.25} &&
             bootstrap.spawn_points.front().facing_yaw_radians == 0.625,
         "foundation bootstrap payload lost typed RAC1 semantics");

  const auto collision = openrc::decode_collision_world_v1(
      collision_resource.payload, kLimits.collision_payload);
  expect(!collision.mesh.vertices.empty() &&
             collision.mesh.triangles.size() == 4U &&
             !collision.grid.cells.empty(),
         "foundation collision payload is not a usable neutral world");
}

void test_level_agnostic_identity() {
  const Fixture fixture;
  auto first_request = fixture.request(0U);
  first_request.collision.logical_locator =
      "rac1/level/000/core/collision";
  first_request.gameplay.logical_locator = "rac1/level/000/gameplay";
  const auto first = openrc::compile_rac_level_foundation_package_v1(
      first_request, kLimits);
  const auto last = openrc::compile_rac_level_foundation_package_v1(
      fixture.request(18U), kLimits);
  const auto first_bootstrap = openrc::parse_level_bootstrap_v1(
      resource(first, openrc::kLevelBootstrapResourceIdV1).payload,
      kLimits.bootstrap_payload);
  const auto last_bootstrap = openrc::parse_level_bootstrap_v1(
      resource(last, openrc::kLevelBootstrapResourceIdV1).payload,
      kLimits.bootstrap_payload);
  expect(first.level_id == 0U && first_bootstrap.level_id == 0U &&
             last.level_id == 18U && last_bootstrap.level_id == 18U,
         "foundation compiler contains a level-specific identity branch");
}

void test_complete_source_digest_is_part_of_identity() {
  const Fixture fixture;
  const auto original = openrc::compile_rac_level_foundation_package_v1(
      fixture.request(), kLimits);

  auto changed_gameplay = fixture.gameplay;
  // Background colour is valid typed source data but is intentionally not a
  // field in the bootstrap payload yet.
  write_u32(changed_gameplay, kLevelSettingsOffset, 1U);
  auto changed_request = fixture.request();
  changed_request.gameplay.bytes = changed_gameplay;
  const auto changed = openrc::compile_rac_level_foundation_package_v1(
      changed_request, kLimits);

  const auto &original_bootstrap =
      resource(original, openrc::kLevelBootstrapResourceIdV1);
  const auto &changed_bootstrap =
      resource(changed, openrc::kLevelBootstrapResourceIdV1);
  expect(original_bootstrap.payload == changed_bootstrap.payload &&
             original_bootstrap.provenance[0U].source_sha256 !=
                 changed_bootstrap.provenance[0U].source_sha256 &&
             openrc::encode_level_package_v1(original, kLimits.package) !=
                 openrc::encode_level_package_v1(changed, kLimits.package),
         "foundation package identity does not bind the complete source asset");
}

[[nodiscard]] openrc::LevelPackageProvenanceV1 mod_provenance() {
  constexpr std::array<std::byte, 3U> kDirective{
      std::byte{1U}, std::byte{2U}, std::byte{3U}};
  return {
      openrc::LevelPackageProvenanceKindV1::mod_resource,
      "mods/foundation-test/source",
      0U,
      kDirective.size(),
      openrc::prepared_content_sha256_v1(kDirective),
  };
}

void test_replaceable_but_not_removable_overlay_contract() {
  const Fixture fixture;
  const auto base = openrc::compile_rac_level_foundation_package_v1(
      fixture.request(), kLimits);
  const auto base_digest =
      openrc::level_package_sha256_v1(base, kLimits.package);

  openrc::LevelPackageV1 replacement;
  replacement.level_id = base.level_id;
  replacement.content_api_version = base.content_api_version;
  replacement.build_id = base.build_id;
  replacement.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  replacement.layer_id = "mods/foundation-test";
  replacement.priority = 10;
  replacement.required_base_package_sha256 = base_digest;
  for (const auto &base_resource : base.resources) {
    auto updated = base_resource;
    updated.provenance = {mod_provenance()};
    updated.payload_sha256 = {};
    replacement.resources.push_back(std::move(updated));
  }
  const std::array replacements{replacement};
  const auto resolved = openrc::resolve_level_package_v1(
      base, replacements, kLimits.package);
  expect(resolved.resources.size() == 2U &&
             resolved.applied_overlays.size() == 1U,
         "foundation resources could not be replaced by an explicit overlay");

  for (const auto &base_resource : base.resources) {
    auto removal = replacement;
    removal.layer_id = "mods/foundation-remove-test";
    removal.resources = {base_resource};
    auto &removed = removal.resources.front();
    removed.operation = openrc::LevelPackageResourceOperationV1::remove;
    removed.flags = 0U;
    removed.provenance = {mod_provenance()};
    removed.payload.clear();
    removed.payload_sha256 = {};
    const std::array removals{removal};
    expect_package_rejected(
        [&] {
          static_cast<void>(openrc::resolve_level_package_v1(
              base, removals, kLimits.package));
        },
        "a required foundation resource was removable through an overlay");
  }
}

void test_source_and_policy_rejections() {
  Fixture fixture;
  auto request = fixture.request();

  request.collision.logical_locator = R"(C:\games\collision.bin)";
  expect_compile_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_level_foundation_package_v1(
            request, kLimits));
      },
      "foundation compiler accepted a host source path");

  request = fixture.request();
  request.gameplay.logical_locator = "rac1/level/../gameplay";
  expect_compile_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_level_foundation_package_v1(
            request, kLimits));
      },
      "foundation compiler accepted source traversal");

  request = fixture.request();
  request.gameplay.logical_locator = request.collision.logical_locator;
  expect_compile_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_level_foundation_package_v1(
            request, kLimits));
      },
      "foundation compiler accepted ambiguous source locators");

  request = fixture.request();
  request.content_api_version = 0U;
  expect_compile_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_level_foundation_package_v1(
            request, kLimits));
      },
      "foundation compiler accepted a zero content API version");

  request = fixture.request();
  auto malformed_collision = fixture.collision;
  malformed_collision.at(0U) = std::byte{4U};
  request.collision.bytes = malformed_collision;
  expect_compile_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_level_foundation_package_v1(
            request, kLimits));
      },
      "foundation compiler skipped collision source validation");

  request = fixture.request();
  auto missing_player = fixture.gameplay;
  write_u32(missing_player, kMobyInstancesOffset + 0x10U + 0x18U, 7U);
  request.gameplay.bytes = missing_player;
  expect_compile_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_level_foundation_package_v1(
            request, kLimits));
      },
      "foundation compiler accepted gameplay without a player spawn");

  request = fixture.request();
  auto mismatched = kLimits;
  mismatched.collision_payload.world.grid_cell_size_q6 *= 2;
  expect_compile_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_level_foundation_package_v1(
            request, mismatched));
      },
      "foundation compiler accepted mismatched collision policies");

  auto source_limited = kLimits;
  source_limited.gameplay_source.max_input_bytes = fixture.gameplay.size() - 1U;
  expect_compile_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_level_foundation_package_v1(
            request, source_limited));
      },
      "foundation compiler ignored source byte limits");

  auto package_limited = kLimits;
  package_limited.package.max_total_payload_bytes = 1U;
  expect_compile_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_level_foundation_package_v1(
            request, package_limited));
      },
      "foundation compiler ignored package payload limits");
}

} // namespace

int main() {
  try {
    test_deterministic_canonical_package_and_neutral_payloads();
    test_level_agnostic_identity();
    test_complete_source_digest_is_part_of_identity();
    test_replaceable_but_not_removable_overlay_contract();
    test_source_and_policy_rejections();
    std::cout << "OpenRC RAC1 level foundation compile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OpenRC RAC1 level foundation compile tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
