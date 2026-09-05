#include "openrc/rac_level_foundation_compile.hpp"

#include "openrc/rac_level_bootstrap_compile.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RacLevelFoundationCompileError(message);
}

[[nodiscard]] bool
logical_locator_character(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('_') ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('/');
}

void validate_logical_locator(const std::string_view value,
                              const std::uint32_t maximum_bytes,
                              const char *const description) {
  if (value.empty() || value.size() > maximum_bytes || value.front() == '/' ||
      value.back() == '/') {
    fail(std::string(description) +
         " is not a canonical relative logical locator");
  }
  for (const auto character : value) {
    if (!logical_locator_character(
            static_cast<unsigned char>(character))) {
      fail(std::string(description) +
           " contains a host-path or non-canonical character");
    }
  }

  std::size_t component_begin = 0U;
  while (component_begin < value.size()) {
    const auto separator = value.find('/', component_begin);
    const auto component_end =
        separator == std::string_view::npos ? value.size() : separator;
    const auto component =
        value.substr(component_begin, component_end - component_begin);
    if (component.empty() || component == "." || component == "..") {
      fail(std::string(description) + " contains an unsafe component");
    }
    if (separator == std::string_view::npos) {
      return;
    }
    component_begin = separator + 1U;
  }
}

void validate_visible_ascii(const std::string_view value,
                            const std::uint32_t maximum_bytes,
                            const char *const description) {
  if (value.empty() || value.size() > maximum_bytes) {
    fail(std::string(description) + " has an invalid length");
  }
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x21U || byte > 0x7eU) {
      fail(std::string(description) + " is not canonical visible ASCII");
    }
  }
}

[[nodiscard]] std::uint64_t source_byte_count(
    const std::span<const std::byte> bytes,
    const char *const description) {
  if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max()) {
      fail(std::string(description) + " exceeds the provenance range width");
    }
  }
  return static_cast<std::uint64_t>(bytes.size());
}

[[nodiscard]] LevelPackageProvenanceV1 source_provenance(
    const RacLevelFoundationSourceBytesV1 &source) {
  return LevelPackageProvenanceV1{
      LevelPackageProvenanceKindV1::prepared_resource,
      source.logical_locator,
      0U,
      source_byte_count(source.bytes, "A RAC1 foundation source"),
      prepared_content_sha256_v1(source.bytes),
  };
}

[[nodiscard]] LevelPackageProvenanceV1 generated_provenance(
    const std::string_view compiler_pass) {
  return LevelPackageProvenanceV1{
      LevelPackageProvenanceKindV1::generated,
      std::string(compiler_pass),
      0U,
      0U,
      {},
  };
}

void validate_request(const RacLevelFoundationCompileRequestV1 &request,
                      const RacLevelFoundationCompileLimitsV1 &limits) {
  if (request.content_api_version == 0U) {
    fail("A RAC1 foundation package needs a non-zero content API version");
  }
  if (limits.package.max_string_bytes == 0U) {
    fail("RAC1 foundation package string limits must be explicit");
  }
  validate_visible_ascii(request.build_id, limits.package.max_string_bytes,
                         "The RAC1 foundation build ID");
  validate_logical_locator(request.collision.logical_locator,
                           limits.package.max_string_bytes,
                           "The RAC1 collision source locator");
  validate_logical_locator(request.gameplay.logical_locator,
                           limits.package.max_string_bytes,
                           "The RAC1 gameplay source locator");
  if (request.collision.logical_locator == request.gameplay.logical_locator) {
    fail("RAC1 collision and gameplay need distinct logical source locators");
  }
  if (request.collision.bytes.empty() || request.gameplay.bytes.empty()) {
    fail("RAC1 foundation source assets must be complete and non-empty");
  }
  if (limits.collision_compile.world != limits.collision_payload.world) {
    fail("RAC1 collision compile and payload policies must match exactly");
  }
  if (limits.package.max_resources < 2U ||
      limits.package.max_provenance_per_resource < 2U ||
      limits.package.max_total_provenance_records < 4U) {
    fail("RAC1 foundation package limits cannot hold its required resources");
  }
}

[[nodiscard]] LevelPackageResourceV1 make_resource(
    const std::string_view resource_id, const std::string_view type_id,
    const std::uint32_t schema_version,
    const RacLevelFoundationSourceBytesV1 &source,
    const std::string_view compiler_pass, std::vector<std::byte> payload) {
  LevelPackageResourceV1 result;
  result.resource_id = resource_id;
  result.type_id = type_id;
  result.schema_version = schema_version;
  result.operation = LevelPackageResourceOperationV1::upsert;
  result.flags = kLevelPackageResourceOverlayReplaceableV1;
  result.provenance.push_back(source_provenance(source));
  result.provenance.push_back(generated_provenance(compiler_pass));
  result.payload = std::move(payload);
  result.payload_sha256 = prepared_content_sha256_v1(result.payload);
  return result;
}

} // namespace

LevelPackageV1 compile_rac_level_foundation_package_v1(
    const RacLevelFoundationCompileRequestV1 &request,
    const RacLevelFoundationCompileLimitsV1 limits) {
  validate_request(request, limits);

  RacLevelCollisionV1 source_collision;
  try {
    source_collision = parse_rac_level_collision_v1(
        request.collision.bytes, limits.collision_source);
  } catch (const RacLevelCollisionError &error) {
    fail("Invalid RAC1 collision source: " + std::string(error.what()));
  }

  RacGameplayBankV1 source_gameplay;
  try {
    source_gameplay = parse_rac_gameplay_bank_v1(
        request.gameplay.bytes, limits.gameplay_source);
  } catch (const RacGameplayBankError &error) {
    fail("Invalid RAC1 gameplay source: " + std::string(error.what()));
  }

  CollisionWorldV1 collision;
  try {
    collision = compile_rac_level_collision_world_v1(
        source_collision, limits.collision_compile);
  } catch (const CollisionWorldError &error) {
    fail("Cannot compile neutral collision: " + std::string(error.what()));
  }

  std::vector<std::byte> collision_payload;
  try {
    collision_payload =
        encode_collision_world_v1(collision, limits.collision_payload);
  } catch (const CollisionWorldIoError &error) {
    fail("Cannot encode neutral collision: " + std::string(error.what()));
  }

  LevelBootstrapV1 bootstrap;
  try {
    bootstrap =
        compile_rac_level_bootstrap_v1(source_gameplay, request.level_id);
  } catch (const RacLevelBootstrapCompileError &error) {
    fail("Cannot compile neutral bootstrap: " + std::string(error.what()));
  }

  std::vector<std::byte> bootstrap_payload;
  try {
    bootstrap_payload =
        encode_level_bootstrap_v1(bootstrap, limits.bootstrap_payload);
  } catch (const LevelBootstrapV1Error &error) {
    fail("Cannot encode neutral bootstrap: " + std::string(error.what()));
  }

  LevelPackageV1 package;
  package.level_id = request.level_id;
  package.content_api_version = request.content_api_version;
  package.build_id = request.build_id;
  package.layer_kind = LevelPackageLayerKindV1::base;
  package.layer_id = "base";
  package.priority = 0;

  // Resource IDs are already in canonical lexicographic order.
  package.resources.push_back(make_resource(
      kLevelBootstrapResourceIdV1, kLevelBootstrapResourceTypeIdV1,
      kLevelBootstrapResourceSchemaVersionV1, request.gameplay,
      kRacLevelBootstrapCompilePassV1, std::move(bootstrap_payload)));
  package.resources.push_back(make_resource(
      kCollisionWorldResourceIdV1, kCollisionWorldResourceTypeIdV1,
      kCollisionWorldResourceSchemaVersionV1, request.collision,
      kRacCollisionWorldCompilePassV1, std::move(collision_payload)));

  try {
    const auto canonical_bytes = encode_level_package_v1(package, limits.package);
    return parse_level_package_v1(canonical_bytes, limits.package);
  } catch (const LevelPackageV1Error &error) {
    fail("Cannot canonicalize RAC1 foundation package: " +
         std::string(error.what()));
  }
}

} // namespace openrc
