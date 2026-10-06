#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kPreparedGameFormatVersionV2 = 2U;
inline constexpr std::uint32_t kLevelPackageFormatVersionV1 = 1U;
inline constexpr std::uint32_t kPreparedGameHeaderBytesV2 = 64U;
inline constexpr std::uint32_t kLevelPackageHeaderBytesV1 = 64U;
inline constexpr std::size_t kPreparedContentDigestBytesV1 = 32U;
inline constexpr std::uint32_t kPreparedGameSharedPackageIdV2 = UINT32_MAX;
inline constexpr std::uint32_t kPreparedGameHasSharedPackageV2 = 1U << 0U;

using PreparedContentDigestV1 =
    std::array<std::byte, kPreparedContentDigestBytesV1>;

struct PreparedGameV2Limits {
  std::uint64_t max_input_bytes = 0U;
  std::uint32_t max_levels = 0U;
  std::uint32_t max_overlays = 0U;
  std::uint32_t max_string_bytes = 0U;
  std::uint64_t max_total_referenced_package_bytes = 0U;
  std::uint64_t max_total_overlay_manifest_bytes = 0U;
};

// A content-addressed reference to one planet package. Paths are canonical,
// lower-case, forward-slash relative paths below the prepared-game root.
struct PreparedGameLevelReferenceV2 {
  std::uint32_t level_id = 0U;
  std::string package_path;
  std::uint64_t package_bytes = 0U;
  PreparedContentDigestV1 package_sha256{};
};

// Optional global content uses the existing neutral package container with
// level_id kPreparedGameSharedPackageIdV2. It is not a planet/level reference.
struct PreparedGameSharedReferenceV2 {
  std::string package_path;
  std::uint64_t package_bytes = 0U;
  PreparedContentDigestV1 package_sha256{};
  bool operator==(const PreparedGameSharedReferenceV2 &) const = default;
};

// Optional mod manifests are separate immutable layers. The required digest is
// the SHA-256 of the base PreparedGameV2 bytes before overlays are attached.
// Lower priorities are mounted first; overlay_id breaks priority ties.
struct PreparedGameOverlayReferenceV2 {
  std::string overlay_id;
  std::int32_t priority = 0;
  std::uint32_t content_api_version = 0U;
  std::string manifest_path;
  std::uint64_t manifest_bytes = 0U;
  PreparedContentDigestV1 manifest_sha256{};
  PreparedContentDigestV1 required_base_game_sha256{};
};

struct PreparedGameProvenanceV2 {
  std::string game_id;
  std::string build_id;
  std::string compiler_id;
  std::string compiler_version;
  std::uint64_t source_image_bytes = 0U;
  PreparedContentDigestV1 source_image_sha256{};

  // When V2 is produced beside the existing openrc-prepared-game V1
  // directory, this may bind it to that canonical manifest. Absence keeps V1
  // fully valid and does not make V2 a replacement for its parser or layout.
  std::optional<PreparedContentDigestV1> prepared_game_v1_manifest_sha256;
};

struct PreparedGameV2 {
  std::uint32_t content_api_version = 0U;
  PreparedGameProvenanceV2 provenance;
  std::vector<PreparedGameLevelReferenceV2> levels;
  std::vector<PreparedGameOverlayReferenceV2> overlays;
  std::optional<PreparedGameSharedReferenceV2> shared_package;
};

enum class LevelPackageLayerKindV1 : std::uint32_t {
  base = 0U,
  overlay = 1U,
};

enum class LevelPackageResourceOperationV1 : std::uint32_t {
  upsert = 0U,
  remove = 1U,
};

enum class LevelPackageProvenanceKindV1 : std::uint32_t {
  iso_range = 0U,
  prepared_resource = 1U,
  generated = 2U,
  mod_resource = 3U,
};

inline constexpr std::uint32_t kLevelPackageResourceOverlayReplaceableV1 =
    1U << 0U;
inline constexpr std::uint32_t kLevelPackageResourceOverlayRemovableV1 = 1U
                                                                         << 1U;
inline constexpr std::uint32_t kLevelPackageKnownResourceFlagsV1 =
    kLevelPackageResourceOverlayReplaceableV1 |
    kLevelPackageResourceOverlayRemovableV1;

struct LevelPackageV1Limits {
  std::uint64_t max_input_bytes = 0U;
  std::uint32_t max_resources = 0U;
  std::uint32_t max_provenance_per_resource = 0U;
  std::uint32_t max_total_provenance_records = 0U;
  std::uint32_t max_string_bytes = 0U;
  std::uint64_t max_payload_bytes = 0U;
  std::uint64_t max_total_payload_bytes = 0U;
  std::uint32_t max_overlays_to_resolve = 0U;
};

// Provenance locators are stable logical names rather than host paths. A
// direct source range carries its source digest; generated records may use an
// empty range and zero digest to name the responsible compiler pass.
struct LevelPackageProvenanceV1 {
  LevelPackageProvenanceKindV1 kind = LevelPackageProvenanceKindV1::generated;
  std::string source_locator;
  std::uint64_t source_offset = 0U;
  std::uint64_t source_bytes = 0U;
  PreparedContentDigestV1 source_sha256{};
};

// Resource and type IDs are stable, lower-case ASCII identifiers. type_id is
// intentionally open-ended (for example "openrc.collision-mesh") so future
// core schemas and mods do not require a new container version.
struct LevelPackageResourceV1 {
  std::string resource_id;
  std::string type_id;
  std::uint32_t schema_version = 0U;
  LevelPackageResourceOperationV1 operation =
      LevelPackageResourceOperationV1::upsert;
  std::uint32_t flags = 0U;
  std::vector<LevelPackageProvenanceV1> provenance;
  std::vector<std::byte> payload;

  // Derived from payload. An all-zero value asks the canonical writer to
  // compute it; a non-zero value must already agree with the payload.
  PreparedContentDigestV1 payload_sha256{};
};

// The same format represents an immutable base package and a mod layer. A
// base uses layer_id "base", priority zero, and only upsert operations. An
// overlay binds to the exact canonical base-package digest.
struct LevelPackageV1 {
  std::uint32_t level_id = 0U;
  std::uint32_t content_api_version = 0U;
  std::string build_id;
  LevelPackageLayerKindV1 layer_kind = LevelPackageLayerKindV1::base;
  std::string layer_id = "base";
  std::int32_t priority = 0;
  PreparedContentDigestV1 required_base_package_sha256{};
  std::vector<LevelPackageResourceV1> resources;
};

struct AppliedLevelPackageLayerV1 {
  std::string layer_id;
  std::int32_t priority = 0;
  PreparedContentDigestV1 package_sha256{};
};

struct ResolvedLevelPackageV1 {
  std::uint32_t level_id = 0U;
  std::uint32_t content_api_version = 0U;
  std::string build_id;
  PreparedContentDigestV1 base_package_sha256{};
  std::vector<AppliedLevelPackageLayerV1> applied_overlays;
  std::vector<LevelPackageResourceV1> resources;
};

class PreparedGameV2Error final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class LevelPackageV1Error final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

[[nodiscard]] PreparedContentDigestV1
prepared_content_sha256_v1(std::span<const std::byte> bytes);

[[nodiscard]] bool
is_zero_prepared_digest_v1(const PreparedContentDigestV1 &digest) noexcept;

// Writers canonicalize level/resource/provenance/overlay ordering. Readers
// reject alternate ordering, unknown flags, trailing bytes, bad hashes, and
// every allocation or aggregate that exceeds the caller-provided limits.
[[nodiscard]] std::vector<std::byte>
encode_prepared_game_v2(const PreparedGameV2 &game,
                        PreparedGameV2Limits limits);

[[nodiscard]] PreparedGameV2
parse_prepared_game_v2(std::span<const std::byte> bytes,
                       PreparedGameV2Limits limits);

[[nodiscard]] std::vector<std::byte>
encode_level_package_v1(const LevelPackageV1 &package,
                        LevelPackageV1Limits limits);

[[nodiscard]] LevelPackageV1
parse_level_package_v1(std::span<const std::byte> bytes,
                       LevelPackageV1Limits limits);

[[nodiscard]] PreparedContentDigestV1
level_package_sha256_v1(const LevelPackageV1 &package,
                        LevelPackageV1Limits limits);

[[nodiscard]] const PreparedGameLevelReferenceV2 *
find_prepared_game_level_v2(const PreparedGameV2 &game,
                            std::uint32_t level_id) noexcept;

// Verifies the manifest reference before parsing the package, then requires
// the neutral package's level/build identity to agree with PreparedGameV2.
[[nodiscard]] LevelPackageV1 parse_prepared_game_level_package_v1(
    const PreparedGameV2 &game, std::uint32_t level_id,
    std::span<const std::byte> package_bytes, LevelPackageV1Limits limits);

[[nodiscard]] LevelPackageV1 parse_prepared_game_shared_package_v1(
    const PreparedGameV2 &game, std::span<const std::byte> package_bytes,
    LevelPackageV1Limits limits);

// Applies overlays in canonical (priority, layer_id) order. Replacements and
// removals must be explicitly permitted by the currently visible resource.
[[nodiscard]] ResolvedLevelPackageV1
resolve_level_package_v1(const LevelPackageV1 &base,
                         std::span<const LevelPackageV1> overlays,
                         LevelPackageV1Limits limits);

} // namespace openrc
