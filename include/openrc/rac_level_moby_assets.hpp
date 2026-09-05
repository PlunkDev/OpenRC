#pragma once

#include "openrc/rac_gameplay_bank.hpp"
#include "openrc/rac_level_collision.hpp"
#include "openrc/rac_level_core.hpp"
#include "openrc/rac_level_moby_texture.hpp"
#include "openrc/rac_moby_class.hpp"
#include "openrc/rac_moby_model_geometry.hpp"
#include "openrc/rac_tie_class.hpp"

#include <array>
#include <cstddef>
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
  std::array<std::uint8_t, 16U> texture_slots{};
  std::uint8_t used_texture_slot_count = 0U;
  // Complete bounded RAC source retained only in the compiler-facing asset
  // view. Neutral packages never serialize these bytes or the source parser
  // metadata; keeping them here lets independent compiler passes build actor,
  // collision, or future semantic resources without reopening the ISO.
  RacMobyClassV1 source_class;
  std::vector<std::byte> source_bytes;
  RacMobyModelGeometryV1 high_lod;
};

struct RacLevelTieModelV1 {
  std::uint32_t class_id = 0U;
  std::array<std::uint8_t, 16U> texture_slots{};
  std::uint8_t used_texture_slot_count = 0U;
  RacTieClassV1 high_lod;
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
  RacLevelCollisionLimitsV1 collision;
  RacGameplayBankLimitsV1 gameplay;
  RacMobyClassLimitsV1 local_class;
  RacMobyClassLimitsV1 shared_class;
  RacMobyModelGeometryLimitsV1 model_geometry;
  RacLevelMobyTextureLimitsV1 textures;
  RacTieClassLimitsV1 tie_class;
  std::uint64_t max_tie_models = 0U;
  std::uint64_t max_total_tie_packets = 0U;
  std::uint64_t max_total_tie_vertices = 0U;
  std::uint64_t max_total_tie_triangles = 0U;
};

struct RacLevelMobyAssetsV1 {
  std::uint32_t level_id = 0U;
  // Complete decoded logical sources retained for the deterministic native
  // package compiler. They contain no host paths and are bounded by the same
  // source limits used for the parsed views below.
  std::vector<std::byte> collision_source_bytes;
  std::vector<std::byte> gameplay_source_bytes;
  RacLevelCollisionV1 collision;
  RacGameplayBankV1 gameplay;
  // Tfrag and Moby tables share the same RAC1 0x10-byte TextureEntry format,
  // decoded-core pixel store, and raw GS-RAM palette store. They remain
  // separate banks so their table-local indices cannot be confused.
  RacLevelMobyTextureBankV1 tfrag_textures;
  RacLevelMobyTextureBankV1 textures;
  RacLevelMobyTextureBankV1 tie_textures;
  RacLevelMobyTextureBankV1 shrub_textures;
  std::vector<RacLevelMobyModelV1> models;
  std::vector<RacLevelTieModelV1> tie_models;
  std::uint64_t local_model_count = 0U;
  std::uint64_t shared_model_count = 0U;
  std::uint64_t external_model_count = 0U;
  std::uint64_t local_model_bytes = 0U;
  std::uint64_t shared_decoded_bytes = 0U;
  std::uint64_t total_model_packet_count = 0U;
  std::uint64_t total_model_vertex_count = 0U;
  std::uint64_t total_model_triangle_count = 0U;
  std::uint64_t tie_model_bytes = 0U;
  std::uint64_t total_tie_packet_count = 0U;
  std::uint64_t total_tie_vertex_count = 0U;
  std::uint64_t total_tie_triangle_count = 0U;
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
