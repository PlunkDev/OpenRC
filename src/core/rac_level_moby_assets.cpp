#include "openrc/rac_level_moby_assets.hpp"

#include "openrc/disc.hpp"
#include "openrc/disc_toc.hpp"
#include "openrc/wad.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::size_t kLevelCoreIndexSubrangeV1 = 2U;
constexpr std::size_t kLevelCoreGsRamSubrangeV1 = 3U;
constexpr std::size_t kLevelCoreAssetSubrangeV1 = 10U;

[[noreturn]] void fail(const std::string &message) {
  throw RacLevelMobyAssetError(message);
}

void validate_limits(const RacLevelMobyAssetLimitsV1 &limits) {
  const auto &packet = limits.model_geometry.packet_limits;
  if (limits.max_primary_extent_bytes == 0U ||
      limits.max_decoded_wad_bytes == 0U ||
      limits.max_total_shared_decoded_bytes == 0U ||
      limits.max_models == 0U || limits.max_total_model_packets == 0U ||
      limits.max_total_model_vertices == 0U ||
      limits.max_total_model_triangles == 0U ||
      limits.level_core.max_index_bytes == 0U ||
      limits.level_core.max_encoded_asset_bytes == 0U ||
      limits.level_core.max_decoded_asset_bytes == 0U ||
      limits.level_core.max_moby_classes == 0U ||
      limits.level_core.max_moby_textures == 0U ||
      limits.level_core.max_gadgets == 0U ||
      limits.level_core.max_tie_classes == 0U ||
      limits.level_core.max_shrub_classes == 0U ||
      limits.level_core.max_tie_textures == 0U ||
      limits.level_core.max_shrub_textures == 0U ||
      limits.collision.max_input_bytes == 0U ||
      limits.collision.max_z_slots == 0U ||
      limits.collision.max_y_slots == 0U ||
      limits.collision.max_x_slots == 0U ||
      limits.collision.max_octants == 0U ||
      limits.collision.max_main_vertices == 0U ||
      limits.collision.max_main_faces == 0U ||
      limits.collision.max_hero_groups == 0U ||
      limits.collision.max_hero_vertices == 0U ||
      limits.collision.max_hero_triangles == 0U ||
      limits.gameplay.max_input_bytes == 0U ||
      limits.gameplay.max_moby_classes == 0U ||
      limits.gameplay.max_static_mobies == 0U ||
      limits.gameplay.max_tie_classes == 0U ||
      limits.gameplay.max_tie_instances == 0U ||
      limits.gameplay.max_shrub_classes == 0U ||
      limits.gameplay.max_shrub_instances == 0U ||
      limits.local_class.max_input_bytes == 0U ||
      limits.shared_class.max_input_bytes == 0U ||
      packet.max_input_bytes == 0U || packet.max_vif_commands == 0U ||
      packet.max_matrix_transfers == 0U || packet.max_vertices == 0U ||
      packet.max_strip_indices == 0U ||
      packet.max_texture_primitives == 0U ||
      packet.max_triangles == 0U ||
      limits.model_geometry.max_packets == 0U ||
      limits.model_geometry.max_output_vertices == 0U ||
      limits.model_geometry.max_output_triangles == 0U ||
      limits.textures.max_texture_table_bytes == 0U ||
      limits.textures.max_decoded_core_bytes == 0U ||
      limits.textures.max_gs_ram_bytes == 0U ||
      limits.textures.max_textures == 0U ||
      limits.textures.max_width == 0U ||
      limits.textures.max_height == 0U ||
      limits.textures.max_pixels_per_texture == 0U ||
      limits.textures.max_total_pixels == 0U ||
      limits.textures.max_total_rgba_bytes == 0U ||
      limits.tie_class.max_input_bytes == 0U ||
      limits.tie_class.max_packets == 0U ||
      limits.tie_class.max_strips == 0U ||
      limits.tie_class.max_source_vertices == 0U ||
      limits.tie_class.max_output_vertices == 0U ||
      limits.tie_class.max_output_triangles == 0U ||
      limits.tie_class.max_materials == 0U ||
      limits.max_tie_models == 0U ||
      limits.max_total_tie_packets == 0U ||
      limits.max_total_tie_vertices == 0U ||
      limits.max_total_tie_triangles == 0U) {
    fail("RAC1 level Moby asset limits must all be non-zero");
  }
  if (limits.local_class.require_shared_bank_byte_b_ff ||
      !limits.shared_class.require_shared_bank_byte_b_ff) {
    fail("RAC1 level Moby class policies do not distinguish local and shared assets");
  }
}

void add_bounded_total(std::uint64_t &total, const std::uint64_t addition,
                       const std::uint64_t limit,
                       const char *const description) {
  if (total > limit || addition > limit - total) {
    fail(std::string(description) + " exceeds the caller's aggregate limit");
  }
  total += addition;
}

void add_model_geometry_totals(
    RacLevelMobyAssetsV1 &assets, const RacMobyModelGeometryV1 &geometry,
    const RacLevelMobyAssetLimitsV1 &limits) {
  add_bounded_total(assets.total_model_packet_count, geometry.packets.size(),
                    limits.max_total_model_packets,
                    "The RAC1 level Moby packet count");
  add_bounded_total(assets.total_model_vertex_count, geometry.vertices.size(),
                    limits.max_total_model_vertices,
                    "The RAC1 level Moby vertex count");
  add_bounded_total(assets.total_model_triangle_count,
                    geometry.triangles.size(),
                    limits.max_total_model_triangles,
                    "The RAC1 level Moby triangle count");
}

[[nodiscard]] std::vector<std::byte> read_disc_extent(
    const std::filesystem::path &image_path,
    const std::uint64_t logical_block, const std::uint64_t sector_count,
    const std::uint64_t maximum_bytes) {
  if (sector_count == 0U ||
      sector_count > maximum_bytes / kDiscTocSectorSize ||
      logical_block >
          std::numeric_limits<std::uint64_t>::max() / kDiscTocSectorSize) {
    fail("The RAC1 level-core primary extent has an invalid bounded size");
  }
  const auto offset = logical_block * kDiscTocSectorSize;
  const auto byte_count = sector_count * kDiscTocSectorSize;
  if (byte_count > std::numeric_limits<std::size_t>::max() ||
      byte_count > static_cast<std::uint64_t>(
                       std::numeric_limits<std::streamsize>::max()) ||
      offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamoff>::max())) {
    fail("The RAC1 level-core primary extent exceeds host I/O limits");
  }

  std::ifstream input(image_path, std::ios::binary | std::ios::ate);
  if (!input) {
    fail("Cannot open the RAC1 level Moby disc image");
  }
  const auto end_position = input.tellg();
  if (end_position < 0) {
    fail("Cannot determine the RAC1 level Moby disc image size");
  }
  const auto image_bytes = static_cast<std::uint64_t>(end_position);
  if (offset > image_bytes || byte_count > image_bytes - offset) {
    fail("The RAC1 level-core primary extent lies outside the disc image");
  }
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!input) {
    fail("Cannot seek to the RAC1 level-core primary extent");
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(byte_count));
  input.read(reinterpret_cast<char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
    fail("Unexpected end of image while reading the RAC1 level-core extent");
  }
  return bytes;
}

[[nodiscard]] std::span<const std::byte> bounded_subrange(
    const std::span<const std::byte> primary_bytes,
    const DiscTocSubrange &subrange, const char *const description) {
  const auto offset = static_cast<std::uint64_t>(subrange.relative_offset);
  const auto size = static_cast<std::uint64_t>(subrange.byte_size);
  if (offset > primary_bytes.size() || size > primary_bytes.size() - offset) {
    fail(std::string(description) + " lies outside primary extent 0");
  }
  return primary_bytes.subspan(static_cast<std::size_t>(offset),
                               static_cast<std::size_t>(size));
}

[[nodiscard]] RacMobyModelGeometryV1 make_high_geometry(
    const std::span<const std::byte> bytes, const RacMobyClassV1 &model,
    const RacMobyModelGeometryLimitsV1 limits,
    const std::string &description) {
  if (model.joint_count != 0U) {
    RacMobyModelGeometryV1 marker;
    marker.input_bytes = bytes.size();
    marker.lod = RacMobyLodV1::high;
    marker.requires_bind_transforms = true;
    return marker;
  }
  try {
    return assemble_rac_moby_model_geometry_v1(
        bytes, model, RacMobyLodV1::high, limits);
  } catch (const RacMobyModelGeometryError &error) {
    fail(description + " failed high-LOD assembly: " + error.what());
  }
}

} // namespace

RacLevelMobyAssetsV1 load_rac_level_moby_assets_v1(
    const std::filesystem::path &image_path, const std::uint32_t level_id,
    const RacLevelMobyAssetLimitsV1 limits) {
  validate_limits(limits);
  if (image_path.empty()) {
    fail("The RAC1 level Moby disc image path is empty");
  }
  if (level_id >= kDiscTocLevelCount) {
    fail("The RAC1 level Moby level ID is out of range");
  }
  const auto disc = inspect_disc(image_path);
  if (disc.game != GameId::ratchet_and_clank_2002 ||
      !disc.supported_build) {
    fail("The disc is not a supported RAC1 level Moby build");
  }

  const auto assets = inspect_disc_toc_assets(image_path);
  const auto level_assets = std::find_if(
      assets.levels.begin(), assets.levels.end(),
      [level_id](const DiscTocLevelAssets &candidate) {
        return candidate.level_id == level_id;
      });
  const auto level_layout = std::find_if(
      assets.layout.levels.begin(), assets.layout.levels.end(),
      [level_id](const DiscTocLevelDescriptor &candidate) {
        return candidate.level_id == level_id;
      });
  if (level_assets == assets.levels.end() ||
      level_layout == assets.layout.levels.end()) {
    fail("The requested RAC1 level is absent from DiscTocV1");
  }

  const auto &index_subrange = level_assets->primary_extent0
                                   .subranges[kLevelCoreIndexSubrangeV1];
  const auto &gs_ram_subrange = level_assets->primary_extent0
                                    .subranges[kLevelCoreGsRamSubrangeV1];
  const auto &asset_subrange = level_assets->primary_extent0
                                   .subranges[kLevelCoreAssetSubrangeV1];
  if (index_subrange.byte_size == 0U || gs_ram_subrange.byte_size == 0U ||
      asset_subrange.byte_size == 0U ||
      asset_subrange.signature != DiscTocSignature::wad) {
    fail("The RAC1 level-core index, GS RAM, or asset WadV1 is absent");
  }
  const auto &primary_extent = level_layout->primary_extents.front();
  const auto primary_bytes = read_disc_extent(
      image_path, primary_extent.lba, primary_extent.sectors,
      limits.max_primary_extent_bytes);
  const auto primary_span = std::span<const std::byte>(primary_bytes);
  const auto index_bytes = bounded_subrange(
      primary_span, index_subrange, "The RAC1 level-core index");
  const auto raw_gs_ram = bounded_subrange(
      primary_span, gs_ram_subrange, "The RAC1 level-core GS RAM");
  const auto encoded_assets = bounded_subrange(
      primary_span, asset_subrange, "The RAC1 level-core asset WadV1");
  const auto decoded_assets =
      decode_wad_bytes(encoded_assets, limits.max_decoded_wad_bytes);
  const auto core = parse_rac_level_core_index_v1(
      index_bytes, encoded_assets, decoded_assets.bytes, limits.level_core);
  if (core.collision_asset_range.size == 0U) {
    fail("The RAC1 level-core collision asset is absent");
  }
  const auto decoded_asset_span =
      std::span<const std::byte>(decoded_assets.bytes);
  const auto collision_bytes = decoded_asset_span.subspan(
      static_cast<std::size_t>(core.collision_asset_range.offset),
      static_cast<std::size_t>(core.collision_asset_range.size));
  RacLevelCollisionV1 collision;
  try {
    collision = parse_rac_level_collision_v1(collision_bytes, limits.collision);
  } catch (const RacLevelCollisionError &error) {
    fail("The RAC1 level collision asset failed validation: " +
         std::string(error.what()));
  }
  const auto tfrag_texture_table_bytes = index_bytes.subspan(
      static_cast<std::size_t>(core.tfrag_texture_table_range.offset),
      static_cast<std::size_t>(core.tfrag_texture_table_range.size));
  auto tfrag_textures = decode_rac_level_moby_texture_bank_v1(
      tfrag_texture_table_bytes, decoded_assets.bytes, raw_gs_ram,
      core.header.textures_base_offset, limits.textures);
  const auto moby_texture_table_bytes = index_bytes.subspan(
      static_cast<std::size_t>(core.moby_texture_table_range.offset),
      static_cast<std::size_t>(core.moby_texture_table_range.size));
  auto textures = decode_rac_level_moby_texture_bank_v1(
      moby_texture_table_bytes, decoded_assets.bytes, raw_gs_ram,
      core.header.textures_base_offset, limits.textures);
  const auto tie_texture_table_bytes = index_bytes.subspan(
      static_cast<std::size_t>(core.tie_texture_table_range.offset),
      static_cast<std::size_t>(core.tie_texture_table_range.size));
  auto tie_textures = decode_rac_level_moby_texture_bank_v1(
      tie_texture_table_bytes, decoded_assets.bytes, raw_gs_ram,
      core.header.textures_base_offset, limits.textures);
  const auto shrub_texture_table_bytes = index_bytes.subspan(
      static_cast<std::size_t>(core.shrub_texture_table_range.offset),
      static_cast<std::size_t>(core.shrub_texture_table_range.size));
  auto shrub_textures = decode_rac_level_moby_texture_bank_v1(
      shrub_texture_table_bytes, decoded_assets.bytes, raw_gs_ram,
      core.header.textures_base_offset, limits.textures);

  const auto &gameplay_ref = level_assets->primary_wads.front();
  if (gameplay_ref.signature != DiscTocSignature::wad ||
      gameplay_ref.occupied_sectors == 0U) {
    fail("The RAC1 level's primary gameplay WadV1 is absent");
  }
  const auto decoded_gameplay = decode_wad(
      image_path, gameplay_ref.lba, gameplay_ref.occupied_sectors,
      limits.max_decoded_wad_bytes);
  auto gameplay = parse_rac_gameplay_bank_v1(
      decoded_gameplay.bytes, limits.gameplay);
  if (gameplay.moby_class_ids.size() != core.moby_classes.size()) {
    fail("RAC1 gameplay and level-core Moby class counts disagree");
  }
  for (std::size_t index = 0U; index < core.moby_classes.size(); ++index) {
    if (gameplay.moby_class_ids[index] !=
        static_cast<std::uint32_t>(core.moby_classes[index].class_id)) {
      fail("RAC1 gameplay and level-core Moby class order disagrees");
    }
  }
  if (gameplay.tie_class_ids.size() != core.tie_classes.size()) {
    fail("RAC1 gameplay and level-core tie class counts disagree");
  }
  for (std::size_t index = 0U; index < core.tie_classes.size(); ++index) {
    if (gameplay.tie_class_ids[index] !=
        static_cast<std::uint32_t>(core.tie_classes[index].class_id)) {
      fail("RAC1 gameplay and level-core tie class order disagrees");
    }
  }
  if (gameplay.shrub_class_ids.size() != core.shrub_classes.size()) {
    fail("RAC1 gameplay and level-core shrub class counts disagree");
  }
  for (std::size_t index = 0U; index < core.shrub_classes.size(); ++index) {
    if (gameplay.shrub_class_ids[index] !=
        static_cast<std::uint32_t>(core.shrub_classes[index].class_id)) {
      fail("RAC1 gameplay and level-core shrub class order disagrees");
    }
  }

  RacLevelMobyAssetsV1 result;
  result.level_id = level_id;
  result.collision = std::move(collision);
  result.tfrag_textures = std::move(tfrag_textures);
  result.textures = std::move(textures);
  result.tie_textures = std::move(tie_textures);
  result.shrub_textures = std::move(shrub_textures);
  if (core.tie_classes.size() > limits.max_tie_models ||
      core.tie_classes.size() > result.tie_models.max_size()) {
    fail("The RAC1 level TIE model count exceeds its output limit");
  }
  result.tie_models.reserve(core.tie_classes.size());
  for (const auto &entry : core.tie_classes) {
    if (entry.asset_range.size == 0U) {
      continue;
    }
    const auto model_bytes = decoded_asset_span.subspan(
        static_cast<std::size_t>(entry.asset_range.offset),
        static_cast<std::size_t>(entry.asset_range.size));
    RacTieClassV1 model;
    try {
      model = parse_rac_tie_class_v1(model_bytes, limits.tie_class);
    } catch (const RacTieClassError &error) {
      fail("Local RAC1 TIE class " + std::to_string(entry.class_id) +
           " failed validation: " + error.what());
    }
    if (model.texture_count > entry.used_texture_slot_count) {
      fail("A RAC1 TIE class uses more materials than its level-core slots");
    }
    add_bounded_total(result.total_tie_packet_count, model.packets.size(),
                      limits.max_total_tie_packets,
                      "The RAC1 level TIE packet count");
    add_bounded_total(result.total_tie_vertex_count, model.vertices.size(),
                      limits.max_total_tie_vertices,
                      "The RAC1 level TIE vertex count");
    add_bounded_total(result.total_tie_triangle_count, model.triangles.size(),
                      limits.max_total_tie_triangles,
                      "The RAC1 level TIE triangle count");
    result.tie_model_bytes += entry.asset_range.size;
    result.tie_models.push_back(RacLevelTieModelV1{
        static_cast<std::uint32_t>(entry.class_id),
        entry.texture_slots,
        entry.used_texture_slot_count,
        std::move(model),
    });
  }
  if (core.moby_classes.size() > limits.max_models ||
      core.moby_classes.size() > result.models.max_size()) {
    fail("The RAC1 level Moby model count exceeds its output limit");
  }
  result.models.reserve(core.moby_classes.size());
  std::unordered_map<std::uint32_t,
                     const RacLevelCoreMobyClassEntryV1 *>
      class_entries;
  if (core.moby_classes.size() > class_entries.max_size()) {
    fail("The RAC1 level Moby class metadata lookup exceeds the host container");
  }
  class_entries.reserve(core.moby_classes.size());
  for (const auto &entry : core.moby_classes) {
    const auto class_id = static_cast<std::uint32_t>(entry.class_id);
    if (!class_entries.emplace(class_id, &entry).second) {
      fail("The RAC1 level-core contains duplicate Moby class metadata");
    }
  }
  std::unordered_set<std::uint32_t> loaded_class_ids;
  if (core.moby_classes.size() > loaded_class_ids.max_size()) {
    fail("The RAC1 level Moby class lookup exceeds the host container");
  }
  loaded_class_ids.reserve(core.moby_classes.size());

  for (const auto &entry : core.moby_classes) {
    if (entry.asset_range.size == 0U) {
      continue;
    }
    const auto model_bytes = decoded_asset_span.subspan(
        static_cast<std::size_t>(entry.asset_range.offset),
        static_cast<std::size_t>(entry.asset_range.size));
    RacMobyClassV1 model;
    try {
      model = parse_rac_moby_class_v1(model_bytes, limits.local_class);
    } catch (const RacMobyClassError &error) {
      fail("Local RAC1 Moby class " + std::to_string(entry.class_id) +
           " failed validation: " + error.what());
    }
    const auto class_id = static_cast<std::uint32_t>(entry.class_id);
    const auto description =
        "Local RAC1 Moby class " + std::to_string(entry.class_id);
    auto high_geometry = make_high_geometry(
        model_bytes, model, limits.model_geometry, description);
    add_model_geometry_totals(result, high_geometry, limits);
    if (!loaded_class_ids.insert(class_id).second) {
      fail("The RAC1 level-core contains a duplicate local Moby class");
    }
    if (result.models.size() >= limits.max_models ||
        result.models.size() >= result.models.max_size()) {
      fail("The RAC1 level Moby model count exceeds its output limit");
    }
    result.models.push_back(RacLevelMobyModelV1{
        class_id,
        RacLevelMobyModelSourceV1::local_level_core,
        model.joint_count,
        entry.texture_slots,
        entry.used_texture_slot_count,
        std::move(high_geometry)});
    ++result.local_model_count;
    result.local_model_bytes += entry.asset_range.size;
  }

  for (const auto &entry : core.gadgets) {
    if (result.models.size() >= limits.max_models ||
        result.models.size() >= core.moby_classes.size() ||
        result.models.size() >= result.models.max_size()) {
      fail("The RAC1 level Moby model count exceeds its output limit");
    }
    const auto wad_bytes = decoded_asset_span.subspan(
        static_cast<std::size_t>(entry.encoded_range.offset),
        static_cast<std::size_t>(entry.encoded_range.size));
    if (result.shared_decoded_bytes >=
        limits.max_total_shared_decoded_bytes) {
      fail("Shared RAC1 Moby models exceed their aggregate decode limit");
    }
    const auto remaining = limits.max_total_shared_decoded_bytes -
                           result.shared_decoded_bytes;
    const auto decoded = decode_wad_bytes(
        wad_bytes, std::min(remaining, limits.max_decoded_wad_bytes));
    result.shared_decoded_bytes += decoded.bytes.size();
    RacMobyClassV1 model;
    try {
      model = parse_rac_moby_class_v1(decoded.bytes, limits.shared_class);
    } catch (const RacMobyClassError &error) {
      fail("Shared RAC1 Moby class " + std::to_string(entry.class_id) +
           " failed validation: " + error.what());
    }
    const auto class_id = static_cast<std::uint32_t>(entry.class_id);
    const auto class_entry = class_entries.find(class_id);
    if (class_entry == class_entries.end()) {
      fail("A shared RAC1 Moby class has no level-core texture slots");
    }
    if (!loaded_class_ids.insert(class_id).second) {
      fail("A shared RAC1 Moby class duplicates another loaded class");
    }
    const auto description =
        "Shared RAC1 Moby class " + std::to_string(entry.class_id);
    auto high_geometry = make_high_geometry(
        decoded.bytes, model, limits.model_geometry, description);
    add_model_geometry_totals(result, high_geometry, limits);
    result.models.push_back(RacLevelMobyModelV1{
        class_id,
        RacLevelMobyModelSourceV1::shared_gadget,
        model.joint_count,
        class_entry->second->texture_slots,
        class_entry->second->used_texture_slot_count,
        std::move(high_geometry)});
    ++result.shared_model_count;
  }

  if (result.local_model_count > core.moby_classes.size() ||
      result.shared_model_count >
          core.moby_classes.size() - result.local_model_count) {
    fail("Loaded RAC1 Moby ownership exceeds the class table");
  }
  result.external_model_count = core.moby_classes.size() -
                                result.local_model_count -
                                result.shared_model_count;
  result.gameplay = std::move(gameplay);
  return result;
}

} // namespace openrc
