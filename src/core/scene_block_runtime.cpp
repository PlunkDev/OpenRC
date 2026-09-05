#include "openrc/scene_block_runtime.hpp"

#include "openrc/disc_toc.hpp"
#include "openrc/elf.hpp"
#include "openrc/hash.hpp"
#include "openrc/wad.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openrc {
namespace {

struct OverlayLayoutV1 {
  std::uint32_t virtual_memory_address = 0U;
  std::uint32_t size = 0U;
};

struct ReferenceBuildProfileV1 {
  GameId game = GameId::unknown;
  std::string_view serial;
  std::size_t primary_extent_index = 0U;
  std::size_t scene_block_subrange_index = 0U;
  std::size_t companion_table_index = 0U;
  std::string_view overlay_name_marker;
  std::array<OverlayLayoutV1, 8U> overlay_layout{};
  std::array<std::uint16_t, 7U> program_entrypoints{};
  std::array<std::uint16_t, 6U> record_entrypoints{};
  std::uint32_t task_preamble_virtual_address = 0U;
};

inline constexpr ReferenceBuildProfileV1 kReferencePalProfile{
    GameId::ratchet_and_clank_2002,
    "SCES-50916",
    0U,
    10U,
    0U,
    ".55907.",
    {{{0x0000U, 0x0800U},
      {0x0800U, 0x0800U},
      {0x1000U, 0x0800U},
      {0x1800U, 0x0800U},
      {0x2000U, 0x0800U},
      {0x2800U, 0x0800U},
      {0x3000U, 0x0800U},
      {0x3800U, 0x0260U}}},
    {0U, 6U, 8U, 10U, 14U, 16U, 20U},
    {6U, 8U, 10U, 14U, 16U, 20U},
    0x001deac0U,
};

[[noreturn]] void fail(const std::string &message) {
  throw SceneBlockRuntimeError(message);
}

void validate_load_limits(const SceneBlockRuntimeLoadLimitsV1 limits) {
  if (limits.max_elf_bytes == 0U ||
      limits.max_primary_extent_bytes == 0U ||
      limits.max_decoded_wad_bytes == 0U ||
      limits.directory.max_input_bytes == 0U ||
      limits.directory.max_records == 0U ||
      limits.directory.max_owned_bytes == 0U ||
      limits.program.max_input_bytes == 0U ||
      limits.program.max_overlay_chunks == 0U ||
      limits.program.max_code_bytes == 0U ||
      limits.program.max_entrypoints == 0U ||
      limits.program.max_control_transfers == 0U ||
      limits.program.max_cfg_edges == 0U) {
    fail("SceneBlock runtime load limits must all be non-zero");
  }
}

void validate_execution_limits(
    const SceneBlockRuntimeExecutionLimitsV1 limits) {
  const auto &build = limits.task.build;
  if (build.max_block_bytes == 0U || build.max_dma_references == 0U ||
      build.max_reference_bytes == 0U ||
      build.vif.max_input_bytes == 0U || build.vif.max_commands == 0U ||
      build.vif.max_payload_bytes == 0U ||
      limits.task.max_vif_vector_writes == 0U ||
      limits.task.bridge.max_replayed_writes == 0U ||
      limits.task.dvp.max_instruction_pairs == 0U ||
      limits.task.dvp.max_xgkick_events == 0U ||
      limits.task.dvp.max_xgkick_tags_per_event == 0U ||
      limits.task.dvp.max_xgkick_qwords_per_event == 0U ||
      limits.gs.max_tags == 0U || limits.gs.max_packet_qwords == 0U ||
      limits.gs.max_register_writes == 0U ||
      limits.gs.max_vertices == 0U || limits.gs.max_primitives == 0U ||
      limits.gs.max_xgkick_events == 0U) {
    fail("SceneBlock runtime execution limits must all be non-zero");
  }
}

[[nodiscard]] const ReferenceBuildProfileV1 &
resolve_reference_build_profile(const DiscReport &disc) {
  if (disc.game == kReferencePalProfile.game &&
      disc.serial == kReferencePalProfile.serial && disc.supported_build) {
    return kReferencePalProfile;
  }
  fail("The disc is not a supported SceneBlock runtime build");
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left, const std::uint64_t right,
    const char *description) {
  if (left != 0U &&
      right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string("Integer overflow while calculating ") + description);
  }
  return left * right;
}

[[nodiscard]] std::vector<std::byte>
read_bounded_binary_file(const std::filesystem::path &path,
                         const std::uint64_t maximum_bytes) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    fail("Cannot open the SceneBlock runtime executable");
  }
  const auto end_position = input.tellg();
  if (end_position < 0) {
    fail("Cannot determine the SceneBlock runtime executable size");
  }
  const auto byte_count = static_cast<std::uint64_t>(end_position);
  if (byte_count > maximum_bytes ||
      byte_count > std::numeric_limits<std::size_t>::max() ||
      byte_count > static_cast<std::uint64_t>(
                       std::numeric_limits<std::streamsize>::max())) {
    fail("The SceneBlock runtime executable exceeds its byte limit");
  }

  std::vector<std::byte> bytes(static_cast<std::size_t>(byte_count));
  input.seekg(0, std::ios::beg);
  if (!bytes.empty() &&
      !input.read(reinterpret_cast<char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()))) {
    fail("Cannot read the complete SceneBlock runtime executable");
  }
  return bytes;
}

[[nodiscard]] std::vector<std::byte> read_disc_extent(
    const std::filesystem::path &image_path, const std::uint64_t logical_block,
    const std::uint64_t sector_count, const std::uint64_t maximum_bytes) {
  if (sector_count == 0U) {
    fail("The SceneBlock primary extent has zero sectors");
  }
  if (sector_count > maximum_bytes / kDiscTocSectorSize) {
    fail("The SceneBlock primary extent exceeds its byte limit");
  }
  const auto offset = checked_multiply(
      logical_block, kDiscTocSectorSize, "the SceneBlock extent offset");
  const auto byte_count = checked_multiply(
      sector_count, kDiscTocSectorSize, "the SceneBlock extent size");
  if (byte_count > std::numeric_limits<std::size_t>::max() ||
      byte_count > static_cast<std::uint64_t>(
                       std::numeric_limits<std::streamsize>::max()) ||
      offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamoff>::max())) {
    fail("The SceneBlock primary extent exceeds host I/O limits");
  }

  std::ifstream input(image_path, std::ios::binary | std::ios::ate);
  if (!input) {
    fail("Cannot open the SceneBlock runtime disc image");
  }
  const auto end_position = input.tellg();
  if (end_position < 0) {
    fail("Cannot determine the SceneBlock runtime disc image size");
  }
  const auto image_bytes = static_cast<std::uint64_t>(end_position);
  if (offset > image_bytes || byte_count > image_bytes - offset) {
    fail("The SceneBlock primary extent lies outside the disc image");
  }

  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!input) {
    fail("Cannot seek to the SceneBlock primary extent");
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(byte_count));
  input.read(reinterpret_cast<char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
    fail("Unexpected end of image while reading the SceneBlock primary extent");
  }
  return bytes;
}

[[nodiscard]] std::vector<ElfDvpOverlay> select_profile_overlays(
    const ElfReport &elf, const ReferenceBuildProfileV1 &profile) {
  if (!elf.dvp_overlay_table) {
    fail("The SceneBlock runtime executable has no DVP overlay table");
  }

  std::vector<ElfDvpOverlay> selected;
  for (const auto &overlay : elf.dvp_overlay_table->overlays) {
    if (overlay.name.find(profile.overlay_name_marker) != std::string::npos) {
      selected.push_back(overlay);
    }
  }
  std::sort(selected.begin(), selected.end(),
            [](const ElfDvpOverlay &left, const ElfDvpOverlay &right) {
              return left.virtual_memory_address < right.virtual_memory_address;
            });

  if (selected.size() != profile.overlay_layout.size()) {
    fail("The SceneBlock DVP program does not have its exact overlay chunks");
  }
  for (std::size_t index = 0U; index < selected.size(); ++index) {
    if (selected[index].virtual_memory_address !=
            profile.overlay_layout[index].virtual_memory_address ||
        selected[index].size != profile.overlay_layout[index].size) {
      fail("The SceneBlock DVP program has an unexpected VU address layout");
    }
  }
  return selected;
}

[[nodiscard]] std::span<const std::byte> find_file_backed_virtual_range(
    const std::span<const std::byte> elf_bytes, const ElfReport &elf,
    const std::uint32_t virtual_address, const std::uint64_t byte_count) {
  if (byte_count >
      std::numeric_limits<std::uint64_t>::max() - virtual_address) {
    fail("The SceneBlock task preamble virtual range overflows");
  }
  const auto range_end =
      static_cast<std::uint64_t>(virtual_address) + byte_count;
  std::optional<std::span<const std::byte>> result;
  for (const auto &segment : elf.program_headers) {
    constexpr std::uint32_t kLoadSegmentType = 1U;
    if (segment.type != kLoadSegmentType) {
      continue;
    }
    const auto segment_begin =
        static_cast<std::uint64_t>(segment.virtual_address);
    const auto segment_end =
        segment_begin + static_cast<std::uint64_t>(segment.file_size);
    if (virtual_address < segment_begin || range_end > segment_end) {
      continue;
    }
    if (result) {
      fail("The SceneBlock task preamble maps to multiple PT_LOAD segments");
    }

    const auto file_offset =
        static_cast<std::uint64_t>(segment.file_offset) +
        (static_cast<std::uint64_t>(virtual_address) - segment_begin);
    if (file_offset > elf_bytes.size() ||
        byte_count > static_cast<std::uint64_t>(elf_bytes.size()) -
                         file_offset ||
        file_offset > std::numeric_limits<std::size_t>::max() ||
        byte_count > std::numeric_limits<std::size_t>::max()) {
      fail("The SceneBlock task preamble lies outside the ELF bytes");
    }
    result = elf_bytes.subspan(static_cast<std::size_t>(file_offset),
                               static_cast<std::size_t>(byte_count));
  }
  if (!result) {
    fail("The SceneBlock task preamble VA is not file-backed by PT_LOAD");
  }
  return *result;
}

[[nodiscard]] bool is_record_entrypoint(
    const ReferenceBuildProfileV1 &profile,
    const std::uint16_t entrypoint_address) noexcept {
  return std::find(profile.record_entrypoints.begin(),
                   profile.record_entrypoints.end(),
                   entrypoint_address) != profile.record_entrypoints.end();
}

[[nodiscard]] bool program_has_entrypoint(const DvpVuProgramV1 &program,
                                          const std::uint16_t address) {
  return std::any_of(program.entrypoints.begin(), program.entrypoints.end(),
                     [address](const DvpVuEntrypointV1 &entrypoint) {
                       return entrypoint.instruction_address == address;
                     });
}

} // namespace

SceneBlockRuntimeAssetsV1 load_scene_block_runtime_assets_v1(
    const std::filesystem::path &image_path,
    const std::filesystem::path &executable_path,
    const std::uint32_t level_id,
    const SceneBlockRuntimeLoadLimitsV1 limits) {
  validate_load_limits(limits);
  if (image_path.empty()) {
    fail("The SceneBlock runtime disc image path is empty");
  }
  if (executable_path.empty()) {
    fail("The SceneBlock runtime executable path is empty");
  }
  if (level_id >= kDiscTocLevelCount) {
    fail("The SceneBlock runtime level ID is out of range");
  }

  auto disc = inspect_disc(image_path);
  if (disc.boot_sha256.empty()) {
    fail("The ISO boot executable has no SHA-256 identity");
  }
  const auto &profile = resolve_reference_build_profile(disc);

  auto elf_bytes = read_bounded_binary_file(executable_path,
                                             limits.max_elf_bytes);
  Sha256 elf_hash;
  elf_hash.update(std::span<const std::byte>(elf_bytes));
  auto executable_sha256 = hex_digest(elf_hash.finish());
  if (executable_sha256 != disc.boot_sha256) {
    fail("The supplied ELF does not match the boot executable in the ISO");
  }

  const auto elf = inspect_elf(std::span<const std::byte>(elf_bytes));
  const auto selected_overlays = select_profile_overlays(elf, profile);
  auto program = decode_dvp_vu_program_v1(
      std::span<const std::byte>(elf_bytes),
      std::span<const ElfDvpOverlay>(selected_overlays),
      std::span<const std::uint16_t>(profile.program_entrypoints),
      limits.program);

  constexpr std::uint64_t kTaskPreambleBytes =
      kSceneBlockTaskPreambleQwordCount * 16U;
  const auto preamble_bytes = find_file_backed_virtual_range(
      std::span<const std::byte>(elf_bytes), elf,
      profile.task_preamble_virtual_address, kTaskPreambleBytes);
  auto preamble = parse_scene_block_task_preamble_v1(preamble_bytes);

  const auto toc_assets = inspect_disc_toc_assets(image_path);
  const auto level_assets = std::find_if(
      toc_assets.levels.begin(), toc_assets.levels.end(),
      [level_id](const DiscTocLevelAssets &candidate) {
        return candidate.level_id == level_id;
      });
  const auto level_layout = std::find_if(
      toc_assets.layout.levels.begin(), toc_assets.layout.levels.end(),
      [level_id](const DiscTocLevelDescriptor &candidate) {
        return candidate.level_id == level_id;
      });
  if (level_assets == toc_assets.levels.end() ||
      level_layout == toc_assets.layout.levels.end()) {
    fail("The requested level is absent from DiscTocV1");
  }
  if (profile.primary_extent_index >= level_layout->primary_extents.size() ||
      profile.scene_block_subrange_index >=
          level_assets->primary_extent0.subranges.size() ||
      profile.companion_table_index >=
          level_assets->primary_extent3.tables.size()) {
    fail("The SceneBlock build profile addresses an unavailable TOC field");
  }

  const auto &subrange = level_assets->primary_extent0.subranges[
      profile.scene_block_subrange_index];
  if (subrange.byte_size == 0U || subrange.signature != DiscTocSignature::wad) {
    fail("The level's SceneBlock subrange is not a non-empty WadV1");
  }
  const auto &primary_extent =
      level_layout->primary_extents[profile.primary_extent_index];
  const auto primary_bytes = read_disc_extent(
      image_path, primary_extent.lba, primary_extent.sectors,
      limits.max_primary_extent_bytes);
  const auto subrange_offset =
      static_cast<std::uint64_t>(subrange.relative_offset);
  const auto subrange_size = static_cast<std::uint64_t>(subrange.byte_size);
  if (subrange_offset > primary_bytes.size() ||
      subrange_size >
          static_cast<std::uint64_t>(primary_bytes.size()) - subrange_offset) {
    fail("The SceneBlock WadV1 subrange lies outside primary extent 0");
  }
  const auto logical_wad = std::span<const std::byte>(primary_bytes).subspan(
      static_cast<std::size_t>(subrange_offset),
      static_cast<std::size_t>(subrange_size));
  const auto decoded =
      decode_wad_bytes(logical_wad, limits.max_decoded_wad_bytes);
  auto directory = parse_scene_block_directory_v1(
      std::span<const std::byte>(decoded.bytes), limits.directory);

  const auto companion_count = level_assets->primary_extent3
                                   .tables[profile.companion_table_index]
                                   .size();
  if (static_cast<std::uint64_t>(companion_count) !=
      directory.record_count) {
    fail("The SceneBlock record count does not match companion extent-3 table 0");
  }

  SceneBlockRuntimeAssetsV1 result;
  result.disc = std::move(disc);
  result.executable_sha256 = std::move(executable_sha256);
  result.level_id = level_id;
  result.companion_record_count =
      static_cast<std::uint64_t>(companion_count);
  result.overlay_section_indices.reserve(selected_overlays.size());
  for (const auto &overlay : selected_overlays) {
    result.overlay_section_indices.push_back(overlay.overlay_section_index);
  }
  result.preamble_virtual_address = profile.task_preamble_virtual_address;
  result.program = std::move(program);
  result.preamble = std::move(preamble);
  result.directory = std::move(directory);
  return result;
}

SceneBlockRuntimeExecutionV1 execute_scene_block_runtime_record_v1(
    const SceneBlockRuntimeAssetsV1 &assets,
    const std::uint64_t record_index,
    const std::uint16_t entrypoint_address,
    const SceneBlockTaskFrameInputV1 &frame_input,
    const SceneBlockRuntimeExecutionLimitsV1 limits) {
  validate_execution_limits(limits);
  const auto &profile = resolve_reference_build_profile(assets.disc);
  if (assets.executable_sha256.empty() ||
      assets.executable_sha256 != assets.disc.boot_sha256) {
    fail("The loaded SceneBlock ELF identity no longer matches its disc");
  }
  if (assets.directory.record_count != assets.directory.entries.size() ||
      assets.companion_record_count != assets.directory.record_count) {
    fail("The loaded SceneBlock record counts are inconsistent");
  }
  if (record_index >= assets.directory.entries.size()) {
    fail("The requested SceneBlock record index is out of range");
  }
  if (!is_record_entrypoint(profile, entrypoint_address)) {
    fail("The requested SceneBlock record entrypoint is unsupported");
  }
  if (!program_has_entrypoint(assets.program,
                              kSceneBlockTaskInitializationEntrypoint) ||
      !program_has_entrypoint(assets.program, entrypoint_address)) {
    fail("The loaded SceneBlock program does not contain the required entrypoints");
  }

  SceneBlockRuntimeExecutionV1 result;
  // A VU execution result owns a complete 32 KiB VU1 data-memory snapshot,
  // and this path temporarily produces several of them. Construct transient
  // results directly on the heap so callers with their own parsing frames do
  // not exhaust Windows' default 1 MiB thread stack.
  const auto initialization =
      std::unique_ptr<SceneBlockTaskInitializationResultV1>(
          new SceneBlockTaskInitializationResultV1(
              initialize_scene_block_task_execution_v1(
                  assets.program, assets.preamble, frame_input,
                  limits.task.dvp)));
  result.initialization = std::move(*initialization);
  if (!result.initialization.ready_state) {
    return result;
  }

  const auto record = std::unique_ptr<SceneBlockTaskRecordExecutionV1>(
      new SceneBlockTaskRecordExecutionV1(execute_scene_block_task_record_v1(
          assets.directory.entries[static_cast<std::size_t>(record_index)],
          entrypoint_address, assets.program,
          *result.initialization.ready_state, limits.task)));
  result.record.emplace(std::move(*record));
  const auto &events = result.record->vu_execution.xgkick_events;
  if (events.empty()) {
    result.gs_status = SceneBlockRuntimeGsStatusV1::no_events;
    return result;
  }
  if (!std::all_of(events.begin(), events.end(),
                   [](const DvpVuXgkickEventV1 &event) {
                     return event.packet_complete;
                   })) {
    result.gs_status = SceneBlockRuntimeGsStatusV1::incomplete_stream;
    return result;
  }

  result.gs = decode_dvp_vu_xgkick_gs_stream_v1(
      std::span<const DvpVuXgkickEventV1>(events), limits.gs);
  result.gs_status = SceneBlockRuntimeGsStatusV1::decoded;
  if (entrypoint_address == kSceneBlockSourceGeometryEntrypointV1) {
    if (result.record->vu_execution.termination !=
        DvpVuTerminationV1::program_end) {
      result.source_geometry_status =
          SceneBlockRuntimeSourceGeometryStatusV1::unavailable_layout;
      result.source_geometry_diagnostic =
          "The record did not reach normal VU E termination";
    } else {
      result.source_geometry = recover_scene_block_source_geometry_v1(
          *result.initialization.ready_state, *result.record, *result.gs,
          SceneBlockSourceGeometryLimitsV1{
              limits.gs.max_vertices,
              limits.task.bridge,
          });
      result.source_geometry_status =
          SceneBlockRuntimeSourceGeometryStatusV1::recovered;
    }
  }
  return result;
}

} // namespace openrc
