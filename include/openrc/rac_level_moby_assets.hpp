#pragma once

#include "openrc/rac_gameplay_bank.hpp"
#include "openrc/rac_level_core.hpp"
#include "openrc/rac_moby_class.hpp"
#include "openrc/rac_moby_model_geometry.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace openrc {

enum class RacLevelMobyModelSourceV1 : std::uint8_t {
  local_level_core,
  shared_gadget,
};

struct RacLevelMobyModelV1 {
  std::uint32_t class_id = 0U;
  RacLevelMobyModelSourceV1 source =
      RacLevelMobyModelSourceV1::local_level_core;
  std::uint8_t joint_count = 0U;
  RacMobyModelGeometryV1 high_lod;
};

struct RacLevelMobyAssetLimitsV1 {
  std::uint64_t max_primary_extent_bytes = 0U;
  std::uint64_t max_decoded_wad_bytes = 0U;
  std::uint64_t max_total_shared_decoded_bytes = 0U;
  std::uint64_t max_models = 0U;
  std::uint64_t max_total_model_packets = 0U;
  std::uint64_t max_total_model_vertices = 0U;
  std::uint64_t max_total_model_triangles = 0U;
  RacLevelCoreLimitsV1 level_core;
  RacGameplayBankLimitsV1 gameplay;
  RacMobyClassLimitsV1 local_class;
  RacMobyClassLimitsV1 shared_class;
  RacMobyModelGeometryLimitsV1 model_geometry;
};

struct RacLevelMobyAssetsV1 {
  std::uint32_t level_id = 0U;
  RacGameplayBankV1 gameplay;
  std::vector<RacLevelMobyModelV1> models;
  std::uint64_t local_model_count = 0U;
  std::uint64_t shared_model_count = 0U;
  std::uint64_t external_model_count = 0U;
  std::uint64_t local_model_bytes = 0U;
  std::uint64_t shared_decoded_bytes = 0U;
  std::uint64_t total_model_packet_count = 0U;
  std::uint64_t total_model_vertex_count = 0U;
  std::uint64_t total_model_triangle_count = 0U;
};

class RacLevelMobyAssetError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Loads the independently indexed RAC1 gameplay and level-core model assets
// for one supported disc level. Static high-LOD models are fully assembled;
// animated models are retained as explicit bind-transform-required markers so
// a runtime cannot accidentally display packet-local positions as final skin.
[[nodiscard]] RacLevelMobyAssetsV1 load_rac_level_moby_assets_v1(
    const std::filesystem::path &image_path, std::uint32_t level_id,
    RacLevelMobyAssetLimitsV1 limits);

} // namespace openrc
