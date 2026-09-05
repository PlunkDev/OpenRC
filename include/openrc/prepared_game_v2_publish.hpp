#pragma once

#include "openrc/prepared_game_v2_fs.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>

namespace openrc {

// Bytes are supplied explicitly by the compiler. The publisher never searches
// the filesystem for level packages or optional mod content.
struct PreparedGameV2LevelPackageBytesV1 {
    std::uint32_t level_id = 0U;
    std::span<const std::byte> bytes;
};

enum class PreparedGameV2PublishCheckpointV1 : std::uint32_t {
    staged_and_verified = 0U,
    destination_backed_up = 1U,
};

// A caller may cancel a long publication without weakening the transaction.
// Cancellation after the old destination has been backed up exercises the
// same rollback path as a failed final rename. The callback must only observe
// its arguments; it must not mutate the destination tree.
using PreparedGameV2PublishCancelV1 = bool (*)(
    PreparedGameV2PublishCheckpointV1 checkpoint, void *context) noexcept;

struct PreparedGameV2PublishControlV1 {
    PreparedGameV2PublishCancelV1 cancel_requested = nullptr;
    void *context = nullptr;
};

struct PublishedPreparedGameV2V1 {
    std::filesystem::path root;
    PreparedContentDigestV1 manifest_sha256{};
    std::uint32_t level_count = 0U;
    std::uint64_t package_bytes = 0U;
};

class PreparedGameV2PublishError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Publishes one complete base PreparedGameV2 tree below an explicit absolute
// destination. Every supplied package must match exactly one manifest level
// reference and must be a canonical base LevelPackageV1 with matching
// level/build/content identity. Package files are written to a new plain
// sibling staging directory, the manifest is written last, and the complete
// tree is re-read through the hardened filesystem reader before commit.
//
// An existing destination is replaceable only when it is itself a complete,
// verified PreparedGameV2 publication under the same limits and contains
// exactly the manifest-owned base files/directories; unrelated, damaged, or
// extended directories are never moved or deleted. Replacement uses same-parent
// atomic renames. If promotion or post-promotion verification fails, the
// previous destination is restored. Once the new destination has passed
// verification, backup cleanup cannot roll it back; a cleanup failure is
// reported while the verified new publication remains installed. Optional
// overlay references remain metadata only: this function neither discovers nor
// publishes mod files. The caller must hold exclusive ownership of the
// destination parent for the duration of this call; concurrent out-of-band
// filesystem mutation is outside this portable path-based transaction contract.
[[nodiscard]] PublishedPreparedGameV2V1 publish_prepared_game_v2_v1(
    const std::filesystem::path &destination_root,
    const PreparedGameV2 &manifest,
    std::span<const PreparedGameV2LevelPackageBytesV1> level_packages,
    PreparedGameV2FilesystemLimitsV1 limits,
    PreparedGameV2PublishControlV1 control = {});

} // namespace openrc
