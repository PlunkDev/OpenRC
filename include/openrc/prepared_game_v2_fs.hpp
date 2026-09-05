#pragma once

#include "openrc/prepared_game_v2.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>

namespace openrc {

inline constexpr char kPreparedGameV2ManifestFileName[] = "prepared-v2.orpg";

struct PreparedGameV2FilesystemLimitsV1 {
  PreparedGameV2Limits manifest;
  LevelPackageV1Limits level_package;
  std::uint64_t max_total_explicit_overlay_bytes = 0U;
};

// Owns the validated manifest and the normalized explicit root from which it
// was loaded. The root is revalidated whenever a referenced file is opened so
// callers cannot turn one successful load into permission for later traversal.
struct PreparedGameV2RootV1 {
  std::filesystem::path root;
  PreparedContentDigestV1 manifest_sha256{};
  PreparedGameV2 manifest;
};

// Overlay bytes must be supplied by the caller. The filesystem reader never
// scans or opens the optional overlay-manifest paths on its own.
struct ExplicitLevelPackageOverlayBytesV1 {
  std::span<const std::byte> bytes;
};

class PreparedGameV2FilesystemError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Loads exactly <prepared_root>/prepared-v2.orpg. prepared_root must be an
// absolute plain directory path; symlinks and Windows reparse points are
// rejected for every traversed component.
[[nodiscard]] PreparedGameV2RootV1
load_prepared_game_v2_root_v1(const std::filesystem::path &prepared_root,
                              PreparedGameV2FilesystemLimitsV1 limits);

// Reads the manifest-selected package only after checking its exact expected
// size. The package SHA-256, level/build/content identity, and nested hashes
// are then verified by the existing neutral parsers.
[[nodiscard]] LevelPackageV1
load_prepared_game_level_package_v1(const PreparedGameV2RootV1 &prepared,
                                    std::uint32_t level_id,
                                    PreparedGameV2FilesystemLimitsV1 limits);

// Loads the immutable base from prepared.root and resolves only the overlay
// package bytes present in explicit_overlays. Manifest overlay references are
// metadata for a future mod-manifest loader and are never auto-discovered.
[[nodiscard]] ResolvedLevelPackageV1
load_resolved_prepared_game_level_package_v1(
    const PreparedGameV2RootV1 &prepared, std::uint32_t level_id,
    std::span<const ExplicitLevelPackageOverlayBytesV1> explicit_overlays,
    PreparedGameV2FilesystemLimitsV1 limits);

} // namespace openrc
