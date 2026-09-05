#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr openrc::PreparedGameV2Limits kGameLimits{
    4U * 1024U * 1024U, 64U, 32U, 256U, 64U * 1024U * 1024U, 4U * 1024U * 1024U,
};

constexpr openrc::LevelPackageV1Limits kLevelLimits{
    4U * 1024U * 1024U, 1024U, 16U, 4096U, 256U, 1024U * 1024U,
    2U * 1024U * 1024U, 32U,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] std::vector<std::byte> bytes_of(const std::string_view value) {
  return std::vector<std::byte>(
      reinterpret_cast<const std::byte *>(value.data()),
      reinterpret_cast<const std::byte *>(value.data() + value.size()));
}

[[nodiscard]] openrc::PreparedContentDigestV1
digest_of(const std::string_view value) {
  const auto bytes = bytes_of(value);
  return openrc::prepared_content_sha256_v1(bytes);
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
iso_provenance(std::string locator, const std::uint64_t offset,
               const std::string_view content) {
  return openrc::LevelPackageProvenanceV1{
      openrc::LevelPackageProvenanceKindV1::iso_range,
      std::move(locator),
      offset,
      content.size(),
      digest_of(content),
  };
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
mod_provenance(std::string locator, const std::string_view content) {
  return openrc::LevelPackageProvenanceV1{
      openrc::LevelPackageProvenanceKindV1::mod_resource,
      std::move(locator),
      0U,
      content.size(),
      digest_of(content),
  };
}

[[nodiscard]] openrc::LevelPackageV1 make_base_package() {
  openrc::LevelPackageResourceV1 gameplay;
  gameplay.resource_id = "world/gameplay";
  gameplay.type_id = "openrc.gameplay-scene";
  gameplay.schema_version = 1U;
  gameplay.flags = openrc::kLevelPackageResourceOverlayReplaceableV1 |
                   openrc::kLevelPackageResourceOverlayRemovableV1;
  gameplay.provenance.push_back(
      iso_provenance("disc/level/0/gameplay", 0x2000U, "gameplay-source"));
  gameplay.payload = bytes_of("typed-gameplay");

  openrc::LevelPackageResourceV1 collision;
  collision.resource_id = "world/collision";
  collision.type_id = "openrc.collision-mesh";
  collision.schema_version = 1U;
  collision.flags = openrc::kLevelPackageResourceOverlayReplaceableV1 |
                    openrc::kLevelPackageResourceOverlayRemovableV1;
  // Deliberately unsorted. The canonical writer must normalize this order.
  collision.provenance.push_back(openrc::LevelPackageProvenanceV1{
      openrc::LevelPackageProvenanceKindV1::generated,
      "compiler/collision-v1",
      0U,
      0U,
      {},
  });
  collision.provenance.push_back(
      iso_provenance("disc/level/0/core", 0x1000U, "collision-source"));
  collision.payload = bytes_of("typed-collision");

  openrc::LevelPackageV1 result;
  result.level_id = 0U;
  result.content_api_version = 1U;
  result.build_id = "SCES-50916-PAL-v2.00";
  // Also deliberately unsorted.
  result.resources.push_back(std::move(gameplay));
  result.resources.push_back(std::move(collision));
  return result;
}

template <typename Exception, typename Function>
void expect_rejected(Function &&function, const std::string &message) {
  try {
    function();
  } catch (const Exception &) {
    return;
  }
  throw std::runtime_error(message);
}

void overwrite_body_digest(std::vector<std::byte> &bytes) {
  constexpr std::size_t kDigestOffset = 32U;
  const auto digest = openrc::prepared_content_sha256_v1(
      std::span<const std::byte>(bytes).subspan(
          openrc::kLevelPackageHeaderBytesV1));
  std::copy(digest.begin(), digest.end(), bytes.begin() + kDigestOffset);
}

void test_level_package_round_trip_and_canonicalization() {
  const auto base = make_base_package();
  const auto encoded = openrc::encode_level_package_v1(base, kLevelLimits);

  auto sorted = base;
  std::ranges::sort(sorted.resources, {},
                    &openrc::LevelPackageResourceV1::resource_id);
  for (auto &resource : sorted.resources) {
    std::ranges::sort(
        resource.provenance, [](const openrc::LevelPackageProvenanceV1 &left,
                                const openrc::LevelPackageProvenanceV1 &right) {
          return std::tie(left.kind, left.source_locator, left.source_offset,
                          left.source_bytes) <
                 std::tie(right.kind, right.source_locator, right.source_offset,
                          right.source_bytes);
        });
  }
  expect(encoded == openrc::encode_level_package_v1(sorted, kLevelLimits),
         "LevelPackageV1 serialization depends on input ordering");

  const auto parsed = openrc::parse_level_package_v1(encoded, kLevelLimits);
  expect(parsed.level_id == 0U &&
             parsed.layer_kind == openrc::LevelPackageLayerKindV1::base &&
             parsed.resources.size() == 2U &&
             parsed.resources[0U].resource_id == "world/collision" &&
             parsed.resources[1U].resource_id == "world/gameplay" &&
             !openrc::is_zero_prepared_digest_v1(
                 parsed.resources[0U].payload_sha256),
         "LevelPackageV1 round trip lost canonical metadata");
  expect(openrc::encode_level_package_v1(parsed, kLevelLimits) == encoded,
         "A parsed LevelPackageV1 is not byte-stable");
}

void test_level_package_integrity_and_limits() {
  auto base = make_base_package();
  const auto encoded = openrc::encode_level_package_v1(base, kLevelLimits);

  auto corrupt_body = encoded;
  corrupt_body.back() ^= std::byte{1U};
  expect_rejected<openrc::LevelPackageV1Error>(
      [&] {
        static_cast<void>(
            openrc::parse_level_package_v1(corrupt_body, kLevelLimits));
      },
      "LevelPackageV1 accepted a corrupt body digest");

  auto corrupt_payload = encoded;
  corrupt_payload.back() ^= std::byte{1U};
  overwrite_body_digest(corrupt_payload);
  expect_rejected<openrc::LevelPackageV1Error>(
      [&] {
        static_cast<void>(
            openrc::parse_level_package_v1(corrupt_payload, kLevelLimits));
      },
      "LevelPackageV1 accepted a corrupt resource payload digest");

  auto tight_limits = kLevelLimits;
  tight_limits.max_total_payload_bytes = 4U;
  expect_rejected<openrc::LevelPackageV1Error>(
      [&] {
        static_cast<void>(
            openrc::parse_level_package_v1(encoded, tight_limits));
      },
      "LevelPackageV1 ignored the aggregate payload limit");

  base.resources.front().payload_sha256 = digest_of("stale");
  expect_rejected<openrc::LevelPackageV1Error>(
      [&] {
        static_cast<void>(openrc::encode_level_package_v1(base, kLevelLimits));
      },
      "LevelPackageV1 accepted a stale in-memory payload digest");

  base = make_base_package();
  base.resources.front().resource_id = "World/Gameplay";
  expect_rejected<openrc::LevelPackageV1Error>(
      [&] {
        static_cast<void>(openrc::encode_level_package_v1(base, kLevelLimits));
      },
      "LevelPackageV1 accepted a non-canonical resource ID");
}

[[nodiscard]] openrc::LevelPackageResourceV1
replacement_resource(const std::string_view payload) {
  openrc::LevelPackageResourceV1 result;
  result.resource_id = "world/collision";
  result.type_id = "openrc.collision-mesh";
  result.schema_version = 1U;
  result.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
  result.provenance.push_back(mod_provenance("mod/better-collision", payload));
  result.payload = bytes_of(payload);
  return result;
}

void test_overlay_resolution() {
  const auto base = make_base_package();
  const auto base_digest = openrc::level_package_sha256_v1(base, kLevelLimits);

  openrc::LevelPackageV1 replace;
  replace.level_id = base.level_id;
  replace.content_api_version = base.content_api_version;
  replace.build_id = base.build_id;
  replace.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  replace.layer_id = "mods/better-collision";
  replace.priority = 20;
  replace.required_base_package_sha256 = base_digest;
  replace.resources.push_back(replacement_resource("modded-collision"));

  openrc::LevelPackageResourceV1 remove_gameplay;
  remove_gameplay.resource_id = "world/gameplay";
  remove_gameplay.type_id = "openrc.gameplay-scene";
  remove_gameplay.schema_version = 1U;
  remove_gameplay.operation = openrc::LevelPackageResourceOperationV1::remove;
  remove_gameplay.provenance.push_back(
      mod_provenance("mod/remove-gameplay", "remove-directive"));

  openrc::LevelPackageV1 remove;
  remove.level_id = base.level_id;
  remove.content_api_version = base.content_api_version;
  remove.build_id = base.build_id;
  remove.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  remove.layer_id = "mods/remove-gameplay";
  remove.priority = 10;
  remove.required_base_package_sha256 = base_digest;
  remove.resources.push_back(std::move(remove_gameplay));

  // Input order is irrelevant; priority and layer ID define the order.
  const std::vector overlays{replace, remove};
  const auto resolved =
      openrc::resolve_level_package_v1(base, overlays, kLevelLimits);
  expect(resolved.resources.size() == 1U &&
             resolved.resources.front().resource_id == "world/collision" &&
             resolved.resources.front().payload ==
                 bytes_of("modded-collision") &&
             resolved.applied_overlays.size() == 2U &&
             resolved.applied_overlays[0U].layer_id == "mods/remove-gameplay" &&
             resolved.applied_overlays[1U].layer_id == "mods/better-collision",
         "LevelPackageV1 overlay resolution is wrong");

  auto wrong_base = replace;
  wrong_base.required_base_package_sha256 = digest_of("wrong-base");
  expect_rejected<openrc::LevelPackageV1Error>(
      [&] {
        static_cast<void>(openrc::resolve_level_package_v1(
            base, std::span<const openrc::LevelPackageV1>(&wrong_base, 1U),
            kLevelLimits));
      },
      "LevelPackageV1 accepted an overlay for another base");

  auto locked_base = base;
  for (auto &resource : locked_base.resources) {
    if (resource.resource_id == "world/collision") {
      resource.flags = 0U;
    }
  }
  auto locked_overlay = replace;
  locked_overlay.required_base_package_sha256 =
      openrc::level_package_sha256_v1(locked_base, kLevelLimits);
  expect_rejected<openrc::LevelPackageV1Error>(
      [&] {
        static_cast<void>(openrc::resolve_level_package_v1(
            locked_base,
            std::span<const openrc::LevelPackageV1>(&locked_overlay, 1U),
            kLevelLimits));
      },
      "LevelPackageV1 replaced a resource without explicit permission");
}

[[nodiscard]] openrc::PreparedGameV2
make_prepared_game(const std::span<const std::byte> level_zero_package) {
  openrc::PreparedGameV2 result;
  result.content_api_version = 1U;
  result.provenance.game_id = "openrc-rac-2002";
  result.provenance.build_id = "SCES-50916-PAL-v2.00";
  result.provenance.compiler_id = "openrc-asset-compiler";
  result.provenance.compiler_version = "0.1.0";
  result.provenance.source_image_bytes = 4'700'000'000ULL;
  result.provenance.source_image_sha256 = digest_of("retail-disc-image");
  result.provenance.prepared_game_v1_manifest_sha256 =
      digest_of("legacy-manifest-v1");

  // Deliberately unsorted.
  result.levels.push_back(openrc::PreparedGameLevelReferenceV2{
      1U,
      "levels/001.orlvl",
      1234U,
      digest_of("level-one-package"),
  });
  result.levels.push_back(openrc::PreparedGameLevelReferenceV2{
      0U,
      "levels/000.orlvl",
      level_zero_package.size(),
      openrc::prepared_content_sha256_v1(level_zero_package),
  });

  result.overlays.push_back(openrc::PreparedGameOverlayReferenceV2{
      "mods/visuals",
      20,
      1U,
      "mods/visuals/manifest.ormod",
      200U,
      digest_of("visual-mod-manifest"),
      digest_of("base-prepared-game"),
  });
  result.overlays.push_back(openrc::PreparedGameOverlayReferenceV2{
      "mods/gameplay",
      10,
      1U,
      "mods/gameplay/manifest.ormod",
      100U,
      digest_of("gameplay-mod-manifest"),
      digest_of("base-prepared-game"),
  });
  return result;
}

void test_prepared_game_round_trip_and_reference_binding() {
  const auto base = make_base_package();
  const auto level_bytes = openrc::encode_level_package_v1(base, kLevelLimits);
  const auto game = make_prepared_game(level_bytes);
  const auto encoded = openrc::encode_prepared_game_v2(game, kGameLimits);

  auto sorted = game;
  std::ranges::sort(sorted.levels, {},
                    &openrc::PreparedGameLevelReferenceV2::level_id);
  std::ranges::sort(sorted.overlays,
                    [](const openrc::PreparedGameOverlayReferenceV2 &left,
                       const openrc::PreparedGameOverlayReferenceV2 &right) {
                      return std::tie(left.priority, left.overlay_id) <
                             std::tie(right.priority, right.overlay_id);
                    });
  expect(encoded == openrc::encode_prepared_game_v2(sorted, kGameLimits),
         "PreparedGameV2 serialization depends on input ordering");

  const auto parsed = openrc::parse_prepared_game_v2(encoded, kGameLimits);
  expect(parsed.levels.size() == 2U && parsed.levels[0U].level_id == 0U &&
             parsed.levels[1U].level_id == 1U &&
             parsed.overlays[0U].overlay_id == "mods/gameplay" &&
             parsed.provenance.prepared_game_v1_manifest_sha256.has_value() &&
             openrc::encode_prepared_game_v2(parsed, kGameLimits) == encoded,
         "PreparedGameV2 round trip lost canonical metadata");

  const auto parsed_level = openrc::parse_prepared_game_level_package_v1(
      parsed, 0U, level_bytes, kLevelLimits);
  expect(parsed_level.level_id == 0U &&
             parsed_level.build_id == parsed.provenance.build_id,
         "PreparedGameV2 failed to bind its LevelPackageV1");

  auto corrupt_level = level_bytes;
  corrupt_level.back() ^= std::byte{1U};
  expect_rejected<openrc::PreparedGameV2Error>(
      [&] {
        static_cast<void>(openrc::parse_prepared_game_level_package_v1(
            parsed, 0U, corrupt_level, kLevelLimits));
      },
      "PreparedGameV2 accepted level bytes with the wrong digest");
}

void test_prepared_game_integrity_and_paths() {
  const auto level_bytes =
      openrc::encode_level_package_v1(make_base_package(), kLevelLimits);
  auto game = make_prepared_game(level_bytes);
  const auto encoded = openrc::encode_prepared_game_v2(game, kGameLimits);

  auto corrupt = encoded;
  corrupt.back() ^= std::byte{1U};
  expect_rejected<openrc::PreparedGameV2Error>(
      [&] {
        static_cast<void>(openrc::parse_prepared_game_v2(corrupt, kGameLimits));
      },
      "PreparedGameV2 accepted corrupt body bytes");

  game.levels.front().package_path = "../outside.orlvl";
  expect_rejected<openrc::PreparedGameV2Error>(
      [&] {
        static_cast<void>(openrc::encode_prepared_game_v2(game, kGameLimits));
      },
      "PreparedGameV2 accepted an unsafe package path");

  game = make_prepared_game(level_bytes);
  game.overlays.back().required_base_game_sha256 = digest_of("another-base");
  expect_rejected<openrc::PreparedGameV2Error>(
      [&] {
        static_cast<void>(openrc::encode_prepared_game_v2(game, kGameLimits));
      },
      "PreparedGameV2 accepted overlays for different base manifests");

  game = make_prepared_game(level_bytes);
  auto tight_limits = kGameLimits;
  tight_limits.max_levels = 1U;
  expect_rejected<openrc::PreparedGameV2Error>(
      [&] {
        static_cast<void>(
            openrc::parse_prepared_game_v2(encoded, tight_limits));
      },
      "PreparedGameV2 ignored its level count limit");
}

} // namespace

int main() {
  try {
    test_level_package_round_trip_and_canonicalization();
    test_level_package_integrity_and_limits();
    test_overlay_resolution();
    test_prepared_game_round_trip_and_reference_binding();
    test_prepared_game_integrity_and_paths();
    std::cout << "OpenRC PreparedGameV2/LevelPackageV1 tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OpenRC PreparedGameV2/LevelPackageV1 tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
