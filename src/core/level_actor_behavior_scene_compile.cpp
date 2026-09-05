#include "openrc/level_actor_behavior_scene_compile.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw LevelActorBehaviorSceneCompileError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows");
  }
  return left + right;
}

[[nodiscard]] bool
same_provenance(const LevelPackageProvenanceV1 &left,
                const LevelPackageProvenanceV1 &right) noexcept {
  return left.kind == right.kind &&
         left.source_locator == right.source_locator &&
         left.source_offset == right.source_offset &&
         left.source_bytes == right.source_bytes &&
         left.source_sha256 == right.source_sha256;
}

void validate_source_provenance(
    const std::span<const LevelPackageProvenanceV1> sources) {
  if (sources.empty()) {
    fail("An actor-behavior scene needs non-empty direct source provenance");
  }
  for (std::size_t index = 0U; index < sources.size(); ++index) {
    const auto &source = sources[index];
    if (source.kind != LevelPackageProvenanceKindV1::iso_range &&
        source.kind != LevelPackageProvenanceKindV1::prepared_resource) {
      fail("Actor-behavior-scene source provenance must identify source "
           "bytes");
    }
    if (source.source_bytes == 0U ||
        is_zero_prepared_digest_v1(source.source_sha256)) {
      fail("Actor-behavior-scene source provenance is incomplete");
    }
    static_cast<void>(checked_add(source.source_offset, source.source_bytes,
                                  "An actor-behavior-scene source range"));
    for (std::size_t previous = 0U; previous < index; ++previous) {
      if (same_provenance(source, sources[previous])) {
        fail("Actor-behavior-scene source provenance contains a duplicate "
             "record");
      }
    }
  }
}

[[nodiscard]] LevelPackageProvenanceV1 generated_provenance() {
  return {LevelPackageProvenanceKindV1::generated,
          std::string(kLevelActorBehaviorSceneCompilePassV1), 0U, 0U, {}};
}

[[nodiscard]] std::uint64_t total_payload_bytes(const LevelPackageV1 &package) {
  std::uint64_t result = 0U;
  for (const auto &resource : package.resources) {
    result = checked_add(result, resource.payload.size(),
                         "Level-package payload bytes");
  }
  return result;
}

[[nodiscard]] std::uint64_t
total_provenance_records(const LevelPackageV1 &package) {
  std::uint64_t result = 0U;
  for (const auto &resource : package.resources) {
    result = checked_add(result, resource.provenance.size(),
                         "Level-package provenance count");
  }
  return result;
}

[[nodiscard]] LevelPackageV1
canonical_package(const LevelPackageV1 &package,
                  const LevelPackageV1Limits limits, const char *const context,
                  std::vector<std::byte> *const encoded = nullptr) {
  try {
    auto bytes = encode_level_package_v1(package, limits);
    auto parsed = parse_level_package_v1(bytes, limits);
    if (encoded != nullptr) {
      *encoded = std::move(bytes);
    }
    return parsed;
  } catch (const LevelPackageV1Error &error) {
    fail(std::string(context) + ": " + error.what());
  }
}

[[nodiscard]] std::vector<std::byte>
canonical_payload(const ActorBehaviorSceneV1 &scene,
                  const ActorBehaviorSceneIoLimitsV1 limits) {
  try {
    auto payload = encode_actor_behavior_scene_v1(scene, limits);
    const auto parsed = decode_actor_behavior_scene_v1(payload, limits);
    if (encode_actor_behavior_scene_v1(parsed, limits) != payload) {
      fail("Canonical ActorBehaviorSceneV1 payload is not byte "
           "deterministic");
    }
    return payload;
  } catch (const ActorBehaviorSceneIoError &error) {
    fail("Cannot encode neutral ActorBehaviorSceneV1: " +
         std::string(error.what()));
  }
}

[[nodiscard]] LevelPackageResourceV1 &
require_attached_resource(LevelPackageV1 &package) {
  LevelPackageResourceV1 *result = nullptr;
  for (auto &resource : package.resources) {
    if (resource.resource_id != kActorBehaviorSceneResourceIdV1) {
      continue;
    }
    if (result != nullptr) {
      fail("Canonical package unexpectedly repeats its actor-behavior-scene "
           "resource");
    }
    result = &resource;
  }
  if (result == nullptr) {
    fail("Canonical package unexpectedly lost its actor-behavior-scene "
         "resource");
  }
  return *result;
}

void verify_attached_resource(
    const LevelPackageResourceV1 &resource,
    const std::span<const std::byte> payload,
    const std::span<const LevelPackageProvenanceV1> source_provenance,
    const ActorBehaviorSceneIoLimitsV1 actor_behavior_io_limits) {
  if (resource.type_id != kActorBehaviorSceneResourceTypeIdV1 ||
      resource.schema_version != kActorBehaviorSceneResourceSchemaVersionV1 ||
      resource.operation != LevelPackageResourceOperationV1::upsert ||
      resource.flags != kLevelPackageResourceOverlayReplaceableV1 ||
      resource.payload.size() != payload.size() ||
      !std::equal(resource.payload.begin(), resource.payload.end(),
                  payload.begin()) ||
      resource.payload_sha256 != prepared_content_sha256_v1(payload) ||
      resource.provenance.size() != source_provenance.size() + 1U) {
    fail("Canonical package changed the actor-behavior-scene resource "
         "contract");
  }

  const auto generated_count = std::count_if(
      resource.provenance.begin(), resource.provenance.end(),
      [](const LevelPackageProvenanceV1 &provenance) {
        return provenance.kind == LevelPackageProvenanceKindV1::generated &&
               provenance.source_locator ==
                   kLevelActorBehaviorSceneCompilePassV1 &&
               provenance.source_offset == 0U &&
               provenance.source_bytes == 0U &&
               is_zero_prepared_digest_v1(provenance.source_sha256);
      });
  if (generated_count != 1) {
    fail("Canonical package changed actor-behavior-scene compiler "
         "provenance");
  }
  for (const auto &expected : source_provenance) {
    if (std::count_if(resource.provenance.begin(), resource.provenance.end(),
                      [&expected](const LevelPackageProvenanceV1 &actual) {
                        return same_provenance(actual, expected);
                      }) != 1) {
      fail("Canonical package changed actor-behavior-scene source "
           "provenance");
    }
  }
  try {
    const auto parsed = decode_actor_behavior_scene_v1(
        resource.payload, actor_behavior_io_limits);
    if (encode_actor_behavior_scene_v1(parsed, actor_behavior_io_limits) !=
        resource.payload) {
      fail("Canonical package changed ActorBehaviorSceneV1 payload "
           "semantics");
    }
  } catch (const ActorBehaviorSceneIoError &error) {
    fail("Canonical package contains an invalid ActorBehaviorSceneV1: " +
         std::string(error.what()));
  }
}

} // namespace

LevelPackageV1 attach_actor_behavior_scene_to_level_package_v1(
    LevelPackageV1 base_package, const ActorBehaviorSceneV1 &scene,
    const std::span<const LevelPackageProvenanceV1> source_provenance,
    const ActorBehaviorSceneIoLimitsV1 actor_behavior_io_limits,
    const LevelPackageV1Limits package_limits) {
  if (base_package.layer_kind != LevelPackageLayerKindV1::base) {
    fail("An actor-behavior scene can only be attached to a base "
         "LevelPackageV1");
  }
  if (scene.level_id != base_package.level_id) {
    fail("ActorBehaviorSceneV1 belongs to a different level package");
  }
  if (std::any_of(base_package.resources.begin(), base_package.resources.end(),
                  [](const LevelPackageResourceV1 &resource) {
                    return resource.resource_id ==
                           kActorBehaviorSceneResourceIdV1;
                  })) {
    fail("LevelPackageV1 already contains world/actor-behaviors");
  }
  validate_source_provenance(source_provenance);

  std::vector<std::byte> canonical_base_bytes;
  auto canonical_base = canonical_package(
      base_package, package_limits, "Invalid base LevelPackageV1",
      &canonical_base_bytes);
  auto payload = canonical_payload(scene, actor_behavior_io_limits);

  const auto added_provenance = checked_add(
      source_provenance.size(), 1U, "Actor-behavior-scene provenance count");
  if (canonical_base.resources.size() >= package_limits.max_resources ||
      added_provenance > package_limits.max_provenance_per_resource ||
      checked_add(total_provenance_records(canonical_base), added_provenance,
                  "Level-package provenance count") >
          package_limits.max_total_provenance_records) {
    fail("LevelPackageV1 limits cannot hold the actor-behavior-scene "
         "provenance");
  }
  if (payload.size() > package_limits.max_payload_bytes ||
      checked_add(total_payload_bytes(canonical_base), payload.size(),
                  "Level-package payload bytes") >
          package_limits.max_total_payload_bytes) {
    fail("LevelPackageV1 payload limits cannot hold the actor-behavior "
         "scene");
  }

  LevelPackageResourceV1 resource;
  resource.resource_id = std::string(kActorBehaviorSceneResourceIdV1);
  resource.type_id = std::string(kActorBehaviorSceneResourceTypeIdV1);
  resource.schema_version = kActorBehaviorSceneResourceSchemaVersionV1;
  resource.operation = LevelPackageResourceOperationV1::upsert;
  resource.flags = kLevelPackageResourceOverlayReplaceableV1;
  resource.provenance.assign(source_provenance.begin(),
                             source_provenance.end());
  resource.provenance.push_back(generated_provenance());
  resource.payload = payload;
  resource.payload_sha256 = prepared_content_sha256_v1(resource.payload);
  canonical_base.resources.push_back(std::move(resource));

  auto result = canonical_package(
      canonical_base, package_limits,
      "Cannot canonicalize LevelPackageV1 with its actor-behavior scene");
  verify_attached_resource(require_attached_resource(result), payload,
                           source_provenance, actor_behavior_io_limits);

  auto without_actor_behaviors = result;
  std::erase_if(without_actor_behaviors.resources,
                [](const LevelPackageResourceV1 &candidate) {
                  return candidate.resource_id ==
                         kActorBehaviorSceneResourceIdV1;
                });
  std::vector<std::byte> verified_base_bytes;
  static_cast<void>(canonical_package(
      without_actor_behaviors, package_limits,
      "Cannot verify preserved base LevelPackageV1", &verified_base_bytes));
  if (verified_base_bytes != canonical_base_bytes) {
    fail("Attaching an actor-behavior scene changed an existing package "
         "resource");
  }
  return result;
}

} // namespace openrc
