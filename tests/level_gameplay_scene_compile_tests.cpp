#include "openrc/level_gameplay_scene_compile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr openrc::GameplaySceneIoLimitsV1 kSceneLimits{
    1U << 20U,
    {
        16U,
        64U,
        1024U,
    },
};

constexpr openrc::LevelPackageV1Limits kPackageLimits{
    2U * 1024U * 1024U, 8U, 8U, 32U, 128U, 1U * 1024U * 1024U,
    2U * 1024U * 1024U, 8U,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_compile_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::LevelGameplaySceneCompileError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::PreparedContentDigestV1
digest_of(const std::string_view value) {
  return openrc::prepared_content_sha256_v1(std::as_bytes(std::span(value)));
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
generated(const std::string_view pass) {
  return {
      openrc::LevelPackageProvenanceKindV1::generated,
      std::string(pass),
      0U,
      0U,
      {},
  };
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
direct_source(const openrc::LevelPackageProvenanceKindV1 kind,
              const std::string_view locator, const std::uint64_t offset,
              const std::string_view identity) {
  return {
      kind, std::string(locator), offset, identity.size(), digest_of(identity),
  };
}

[[nodiscard]] openrc::LevelPackageResourceV1
base_resource(const std::string_view id, const std::string_view type,
              const std::string_view pass, std::vector<std::byte> payload) {
  openrc::LevelPackageResourceV1 result;
  result.resource_id = std::string(id);
  result.type_id = std::string(type);
  result.schema_version = 1U;
  result.operation = openrc::LevelPackageResourceOperationV1::upsert;
  result.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
  result.provenance.push_back(generated(pass));
  result.payload = std::move(payload);
  // The canonical package writer derives this digest.
  return result;
}

[[nodiscard]] openrc::LevelPackageV1 make_base() {
  openrc::LevelPackageV1 result;
  result.level_id = 0U;
  result.content_api_version = 1U;
  result.build_id = "SCES-50916-PAL-v2.00";
  result.layer_kind = openrc::LevelPackageLayerKindV1::base;
  result.layer_id = "base";
  result.priority = 0;

  // Reversed input order proves that existing resources survive attachment in
  // their exact canonical representation.
  result.resources.push_back(base_resource(
      "world/collision", "openrc.collision-world", "compiler/test/collision-v1",
      {std::byte{0x04}, std::byte{0x05}, std::byte{0x06}}));
  result.resources.push_back(base_resource(
      "world/bootstrap", "openrc.level-bootstrap", "compiler/test/bootstrap-v1",
      {std::byte{0x01}, std::byte{0x02}}));
  return result;
}

[[nodiscard]] openrc::GameplaySceneV1 make_scene() {
  openrc::GameplayCollectibleV1 common;
  common.authored_id = 900U;
  common.item_key = "currency/bolt";
  common.local_center = {0.25F, -0.5F, 1.0F};
  common.amount = 25U;
  common.collection_radius = 1.5F;

  openrc::GameplayCollectibleV1 progression;
  progression.authored_id = 7U;
  progression.item_key = "progress/gold-bolt";
  progression.local_center = {1.0F, -0.0F, 2.0F};
  progression.amount = 1U;
  progression.collection_radius = 2.25F;

  openrc::GameplaySceneV1 result;
  result.level_id = 0U;
  result.collectibles = {common, progression};
  return result;
}

[[nodiscard]] std::array<openrc::LevelPackageProvenanceV1, 2U> make_sources() {
  // Deliberately reversed relative to canonical provenance-kind order.
  return {
      direct_source(openrc::LevelPackageProvenanceKindV1::prepared_resource,
                    "rac1/level/000/gameplay", 0U, "decoded-gameplay-scene"),
      direct_source(openrc::LevelPackageProvenanceKindV1::iso_range,
                    "disc/level/000/gameplay-block", 0x123400U,
                    "complete-gameplay-source-range"),
  };
}

[[nodiscard]] const openrc::LevelPackageResourceV1 &
find_resource(const openrc::LevelPackageV1 &package,
              const std::string_view id) {
  const auto found =
      std::find_if(package.resources.begin(), package.resources.end(),
                   [id](const openrc::LevelPackageResourceV1 &resource) {
                     return resource.resource_id == id;
                   });
  if (found == package.resources.end()) {
    throw std::runtime_error("test package is missing resource " +
                             std::string(id));
  }
  return *found;
}

[[nodiscard]] bool
provenance_equal(const openrc::LevelPackageProvenanceV1 &left,
                 const openrc::LevelPackageProvenanceV1 &right) {
  return left.kind == right.kind &&
         left.source_locator == right.source_locator &&
         left.source_offset == right.source_offset &&
         left.source_bytes == right.source_bytes &&
         left.source_sha256 == right.source_sha256;
}

void test_success_determinism_identity_and_preservation() {
  expect(openrc::kLevelGameplaySceneCompilePassV1 ==
             std::string_view("compiler/openrc/gameplay-scene-v1"),
         "gameplay-scene compiler pass identity changed");

  const auto sources = make_sources();
  const auto first = openrc::attach_gameplay_scene_to_level_package_v1(
      make_base(), make_scene(), sources, kSceneLimits, kPackageLimits);
  const auto second = openrc::attach_gameplay_scene_to_level_package_v1(
      make_base(), make_scene(), sources, kSceneLimits, kPackageLimits);
  const auto first_bytes =
      openrc::encode_level_package_v1(first, kPackageLimits);
  const auto second_bytes =
      openrc::encode_level_package_v1(second, kPackageLimits);

  expect(first_bytes == second_bytes,
         "gameplay-scene package attachment is not byte deterministic");
  expect(first.resources.size() == 3U &&
             first.resources[0U].resource_id == "world/bootstrap" &&
             first.resources[1U].resource_id == "world/collision" &&
             first.resources[2U].resource_id == "world/gameplay",
         "gameplay-scene attachment returned non-canonical resource order");

  const auto &resource =
      find_resource(first, openrc::kGameplaySceneResourceIdV1);
  expect(
      resource.type_id == openrc::kGameplaySceneResourceTypeIdV1 &&
          resource.schema_version ==
              openrc::kGameplaySceneResourceSchemaVersionV1 &&
          resource.operation ==
              openrc::LevelPackageResourceOperationV1::upsert &&
          resource.flags == openrc::kLevelPackageResourceOverlayReplaceableV1 &&
          (resource.flags & openrc::kLevelPackageResourceOverlayRemovableV1) ==
              0U &&
          resource.payload_sha256 ==
              openrc::prepared_content_sha256_v1(resource.payload),
      "gameplay-scene resource identity, flags, or digest are wrong");

  expect(resource.provenance.size() == 3U &&
             provenance_equal(resource.provenance[0U], sources[1U]) &&
             provenance_equal(resource.provenance[1U], sources[0U]),
         "gameplay-scene direct provenance is not exact and canonical");
  const auto &pass = resource.provenance[2U];
  expect(pass.kind == openrc::LevelPackageProvenanceKindV1::generated &&
             pass.source_locator == openrc::kLevelGameplaySceneCompilePassV1 &&
             pass.source_offset == 0U && pass.source_bytes == 0U &&
             openrc::is_zero_prepared_digest_v1(pass.source_sha256),
         "gameplay-scene compiler-pass provenance is not exact");

  const auto decoded =
      openrc::decode_gameplay_scene_v1(resource.payload, kSceneLimits);
  expect(decoded == openrc::canonicalize_gameplay_scene_v1(
                        make_scene(), kSceneLimits.scene) &&
             decoded.level_id == first.level_id &&
             !std::signbit(decoded.collectibles[0U].local_center[1U]),
         "attached GameplaySceneV1 payload changed neutral scene semantics");
  const auto parsed =
      openrc::parse_level_package_v1(first_bytes, kPackageLimits);
  expect(openrc::encode_level_package_v1(parsed, kPackageLimits) == first_bytes,
         "attached package did not survive a canonical round trip");

  auto stripped = first;
  std::erase_if(
      stripped.resources, [](const openrc::LevelPackageResourceV1 &candidate) {
        return candidate.resource_id == openrc::kGameplaySceneResourceIdV1;
      });
  expect(openrc::encode_level_package_v1(stripped, kPackageLimits) ==
             openrc::encode_level_package_v1(make_base(), kPackageLimits),
         "gameplay-scene attachment rewrote an existing resource");
}

void test_rejects_wrong_package_identity_and_bad_provenance() {
  const auto sources = make_sources();

  auto duplicate = make_base();
  duplicate.resources.push_back(base_resource(
      openrc::kGameplaySceneResourceIdV1,
      openrc::kGameplaySceneResourceTypeIdV1,
      "compiler/test/existing-gameplay-scene", {std::byte{0x01}}));
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            duplicate, make_scene(), sources, kSceneLimits, kPackageLimits));
      },
      "attachment accepted an existing world/gameplay resource");

  auto overlay = make_base();
  overlay.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            overlay, make_scene(), sources, kSceneLimits, kPackageLimits));
      },
      "attachment accepted an overlay package");

  auto wrong_level = make_scene();
  wrong_level.level_id = 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), wrong_level, sources, kSceneLimits, kPackageLimits));
      },
      "attachment accepted a GameplaySceneV1 for another level");

  auto invalid_base = make_base();
  invalid_base.resources[0U].operation =
      openrc::LevelPackageResourceOperationV1::remove;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            invalid_base, make_scene(), sources, kSceneLimits, kPackageLimits));
      },
      "attachment accepted an invalid base package");

  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), {}, kSceneLimits, kPackageLimits));
      },
      "attachment accepted empty source provenance");

  auto generated_source = sources;
  generated_source[0U].kind = openrc::LevelPackageProvenanceKindV1::generated;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), generated_source, kSceneLimits,
            kPackageLimits));
      },
      "attachment accepted generated source provenance");

  auto zero_bytes = sources;
  zero_bytes[0U].source_bytes = 0U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), zero_bytes, kSceneLimits,
            kPackageLimits));
      },
      "attachment accepted zero-byte source provenance");

  auto zero_digest = sources;
  zero_digest[0U].source_sha256 = {};
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), zero_digest, kSceneLimits,
            kPackageLimits));
      },
      "attachment accepted source provenance without a digest");

  auto overflowing = sources;
  overflowing[0U].source_offset = std::numeric_limits<std::uint64_t>::max();
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), overflowing, kSceneLimits,
            kPackageLimits));
      },
      "attachment accepted an overflowing source range");

  const std::array repeated{sources[0U], sources[0U]};
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), repeated, kSceneLimits, kPackageLimits));
      },
      "attachment accepted duplicate source provenance");
}

void test_rejects_tight_limits_and_invalid_scene() {
  const auto sources = make_sources();
  const auto scene_payload =
      openrc::encode_gameplay_scene_v1(make_scene(), kSceneLimits);
  const auto canonical_base_bytes =
      openrc::encode_level_package_v1(make_base(), kPackageLimits);
  constexpr std::uint64_t kBasePayloadBytes = 5U;

  auto limits = kPackageLimits;
  limits.max_resources = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its package resource limit");

  limits = kPackageLimits;
  limits.max_provenance_per_resource = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its per-resource provenance limit");

  limits = kPackageLimits;
  limits.max_total_provenance_records = 4U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its aggregate provenance limit");

  limits = kPackageLimits;
  limits.max_string_bytes = 30U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its package string limit");

  limits = kPackageLimits;
  limits.max_payload_bytes = scene_payload.size() - 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its per-resource payload limit");

  limits = kPackageLimits;
  limits.max_total_payload_bytes =
      kBasePayloadBytes + scene_payload.size() - 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its aggregate payload limit");

  limits = kPackageLimits;
  limits.max_input_bytes = canonical_base_bytes.size();
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its encoded package byte limit");

  auto scene_limits = kSceneLimits;
  scene_limits.max_encoded_bytes = openrc::kGameplaySceneIoHeaderBytesV1;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), sources, scene_limits, kPackageLimits));
      },
      "attachment ignored its GameplaySceneV1 encoded-byte limit");

  scene_limits = kSceneLimits;
  scene_limits.scene.max_collectibles = 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), make_scene(), sources, scene_limits, kPackageLimits));
      },
      "attachment ignored its GameplaySceneV1 collectible limit");

  auto invalid = make_scene();
  invalid.collectibles[0U].item_key = "Currency/Bad";
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_gameplay_scene_to_level_package_v1(
            make_base(), invalid, sources, kSceneLimits, kPackageLimits));
      },
      "attachment accepted an invalid GameplaySceneV1");
}

} // namespace

int main() {
  try {
    test_success_determinism_identity_and_preservation();
    test_rejects_wrong_package_identity_and_bad_provenance();
    test_rejects_tight_limits_and_invalid_scene();
    std::cout << "level gameplay-scene compile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "level gameplay-scene compile tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
