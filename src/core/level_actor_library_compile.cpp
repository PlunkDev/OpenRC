#include "openrc/level_actor_library_compile.hpp"

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
  throw LevelActorLibraryCompileError(message);
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
    const std::span<const LevelPackageProvenanceV1> source_provenance) {
  if (source_provenance.empty()) {
    fail("An actor library needs non-empty direct source provenance");
  }

  for (const auto &source : source_provenance) {
    if (source.kind != LevelPackageProvenanceKindV1::iso_range &&
        source.kind != LevelPackageProvenanceKindV1::prepared_resource) {
      fail("Actor-library source provenance must identify source bytes, not "
           "generated or mod data");
    }
    if (source.source_bytes == 0U ||
        is_zero_prepared_digest_v1(source.source_sha256)) {
      fail("Actor-library source provenance is incomplete");
    }
    static_cast<void>(checked_add(source.source_offset, source.source_bytes,
                                  "An actor-library source range"));
  }
}

[[nodiscard]] LevelPackageProvenanceV1 generated_provenance() {
  return {
      LevelPackageProvenanceKindV1::generated,
      std::string(kLevelActorLibraryCompilePassV1),
      0U,
      0U,
      {},
  };
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
canonical_actor_payload(const ActorLibraryV1 &library,
                        const ActorLibraryIoLimitsV1 limits) {
  try {
    auto payload = encode_actor_library_v1(library, limits);
    const auto parsed = decode_actor_library_v1(payload, limits);
    if (encode_actor_library_v1(parsed, limits) != payload) {
      fail("Canonical ActorLibraryV1 payload is not byte deterministic");
    }
    return payload;
  } catch (const ActorLibraryIoError &error) {
    fail("Cannot encode neutral ActorLibraryV1: " + std::string(error.what()));
  }
}

[[nodiscard]] LevelPackageResourceV1 &
require_attached_resource(LevelPackageV1 &package) {
  LevelPackageResourceV1 *result = nullptr;
  for (auto &resource : package.resources) {
    if (resource.resource_id != kActorLibraryResourceIdV1) {
      continue;
    }
    if (result != nullptr) {
      fail("Canonical package unexpectedly repeats its actor-library "
           "resource");
    }
    result = &resource;
  }
  if (result == nullptr) {
    fail("Canonical package unexpectedly lost its actor-library resource");
  }
  return *result;
}

void verify_attached_resource(
    const LevelPackageResourceV1 &resource,
    const std::span<const std::byte> payload,
    const std::span<const LevelPackageProvenanceV1> source_provenance,
    const ActorLibraryIoLimitsV1 actor_io_limits) {
  if (resource.type_id != kActorLibraryResourceTypeIdV1 ||
      resource.schema_version != kActorLibraryResourceSchemaVersionV1 ||
      resource.operation != LevelPackageResourceOperationV1::upsert ||
      resource.flags != kLevelPackageResourceOverlayReplaceableV1 ||
      resource.payload.size() != payload.size() ||
      !std::equal(resource.payload.begin(), resource.payload.end(),
                  payload.begin()) ||
      resource.payload_sha256 != prepared_content_sha256_v1(payload) ||
      resource.provenance.size() != source_provenance.size() + 1U) {
    fail("Canonical package changed the actor-library resource contract");
  }

  const auto generated = std::find_if(
      resource.provenance.begin(), resource.provenance.end(),
      [](const LevelPackageProvenanceV1 &provenance) {
        return provenance.kind == LevelPackageProvenanceKindV1::generated;
      });
  if (generated == resource.provenance.end() ||
      generated->source_locator != kLevelActorLibraryCompilePassV1 ||
      generated->source_offset != 0U || generated->source_bytes != 0U ||
      !is_zero_prepared_digest_v1(generated->source_sha256) ||
      std::count_if(resource.provenance.begin(), resource.provenance.end(),
                    [](const LevelPackageProvenanceV1 &provenance) {
                      return provenance.kind ==
                             LevelPackageProvenanceKindV1::generated;
                    }) != 1) {
    fail("Canonical package changed actor-library compiler provenance");
  }

  for (const auto &expected : source_provenance) {
    if (std::count_if(resource.provenance.begin(), resource.provenance.end(),
                      [&expected](const LevelPackageProvenanceV1 &actual) {
                        return same_provenance(actual, expected);
                      }) != 1) {
      fail("Canonical package changed actor-library source provenance");
    }
  }

  try {
    const auto parsed =
        decode_actor_library_v1(resource.payload, actor_io_limits);
    if (encode_actor_library_v1(parsed, actor_io_limits) != resource.payload) {
      fail("Canonical package changed ActorLibraryV1 payload semantics");
    }
  } catch (const ActorLibraryIoError &error) {
    fail("Canonical package contains an invalid ActorLibraryV1: " +
         std::string(error.what()));
  }
}

} // namespace

LevelPackageV1 attach_actor_library_to_level_package_v1(
    LevelPackageV1 base_package, const ActorLibraryV1 &library,
    const std::span<const LevelPackageProvenanceV1> source_provenance,
    const ActorLibraryIoLimitsV1 actor_io_limits,
    const LevelPackageV1Limits package_limits) {
  if (base_package.layer_kind != LevelPackageLayerKindV1::base) {
    fail("An actor library can only be attached to a base LevelPackageV1");
  }
  if (std::any_of(base_package.resources.begin(), base_package.resources.end(),
                  [](const LevelPackageResourceV1 &resource) {
                    return resource.resource_id == kActorLibraryResourceIdV1;
                  })) {
    fail("LevelPackageV1 already contains actors/library");
  }
  validate_source_provenance(source_provenance);

  std::vector<std::byte> canonical_base_bytes;
  auto canonical_base =
      canonical_package(base_package, package_limits,
                        "Invalid base LevelPackageV1", &canonical_base_bytes);
  auto payload = canonical_actor_payload(library, actor_io_limits);

  const auto added_provenance_count = checked_add(
      source_provenance.size(), 1U, "Actor-library provenance count");
  if (canonical_base.resources.size() >= package_limits.max_resources ||
      added_provenance_count > package_limits.max_provenance_per_resource) {
    fail("LevelPackageV1 limits cannot hold the actor-library resource");
  }
  if (checked_add(total_provenance_records(canonical_base),
                  added_provenance_count, "Level-package provenance count") >
      package_limits.max_total_provenance_records) {
    fail("LevelPackageV1 provenance limits cannot hold the actor library");
  }
  if (payload.size() > package_limits.max_payload_bytes ||
      checked_add(total_payload_bytes(canonical_base), payload.size(),
                  "Level-package payload bytes") >
          package_limits.max_total_payload_bytes) {
    fail("LevelPackageV1 payload limits cannot hold the actor library");
  }

  LevelPackageResourceV1 resource;
  resource.resource_id = std::string(kActorLibraryResourceIdV1);
  resource.type_id = std::string(kActorLibraryResourceTypeIdV1);
  resource.schema_version = kActorLibraryResourceSchemaVersionV1;
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
      "Cannot canonicalize LevelPackageV1 with its actor library");
  verify_attached_resource(require_attached_resource(result), payload,
                           source_provenance, actor_io_limits);

  // Removing the new resource from the canonical result must reproduce the
  // exact canonical input package bytes. This guards against accidentally
  // rewriting existing resources while this package-building step evolves.
  auto without_actor_library = result;
  std::erase_if(without_actor_library.resources,
                [](const LevelPackageResourceV1 &candidate) {
                  return candidate.resource_id == kActorLibraryResourceIdV1;
                });
  std::vector<std::byte> verified_base_bytes;
  static_cast<void>(canonical_package(
      without_actor_library, package_limits,
      "Cannot verify preserved base LevelPackageV1", &verified_base_bytes));
  if (verified_base_bytes != canonical_base_bytes) {
    fail("Attaching an actor library changed an existing package resource");
  }
  return result;
}

} // namespace openrc
