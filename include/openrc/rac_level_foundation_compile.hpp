#pragma once

#include "openrc/collision_world_io.hpp"
#include "openrc/level_bootstrap.hpp"
#include "openrc/prepared_game_v2.hpp"
#include "openrc/rac_gameplay_bank.hpp"
#include "openrc/rac_level_collision.hpp"
#include "openrc/rac_level_collision_compile.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace openrc {

// These identifiers are stable provenance records, not host executable names.
inline constexpr std::string_view kRacCollisionWorldCompilePassV1 =
    "compiler/openrc/rac1/collision-world-v1";
inline constexpr std::string_view kRacLevelBootstrapCompilePassV1 =
    "compiler/openrc/rac1/level-bootstrap-v1";

// One complete decoded source asset. The locator names the logical asset in
// the extraction pipeline (for example "rac1/level/000/core/collision"). It
// is never a host path. The complete span is hashed and recorded at offset
// zero as prepared-resource provenance.
struct RacLevelFoundationSourceBytesV1 {
  std::string logical_locator;
  std::span<const std::byte> bytes;
};

struct RacLevelFoundationCompileRequestV1 {
  std::uint32_t level_id = 0U;
  std::uint32_t content_api_version = 0U;
  std::string build_id;
  RacLevelFoundationSourceBytesV1 collision;
  RacLevelFoundationSourceBytesV1 gameplay;
};

struct RacLevelFoundationCompileLimitsV1 {
  RacLevelCollisionLimitsV1 collision_source;
  RacGameplayBankLimitsV1 gameplay_source;
  RacLevelCollisionCompileLimitsV1 collision_compile;
  CollisionWorldIoLimitsV1 collision_payload;
  LevelBootstrapV1Limits bootstrap_payload;
  LevelPackageV1Limits package;
};

class RacLevelFoundationCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Parses and validates both complete RAC1 source assets, converts them through
// clean-room semantic adapters, and returns a canonical base LevelPackageV1.
// The two required resources are overlay-replaceable but never removable.
// No source offsets or source-specific encodings survive in either payload.
[[nodiscard]] LevelPackageV1 compile_rac_level_foundation_package_v1(
    const RacLevelFoundationCompileRequestV1 &request,
    RacLevelFoundationCompileLimitsV1 limits);

} // namespace openrc
