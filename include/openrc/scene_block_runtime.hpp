#pragma once

#include "openrc/disc.hpp"
#include "openrc/dvp_vu.hpp"
#include "openrc/gif_gs.hpp"
#include "openrc/scene_block_directory.hpp"
#include "openrc/scene_block_geometry.hpp"
#include "openrc/scene_block_task_execute.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

// Bounds every allocation and parser used while loading one reusable
// SceneBlock level. The disc image itself is read by bounded extents rather
// than copied into memory.
struct SceneBlockRuntimeLoadLimitsV1 {
  std::uint64_t max_elf_bytes = 0U;
  std::uint64_t max_primary_extent_bytes = 0U;
  std::uint64_t max_decoded_wad_bytes = 0U;
  SceneBlockDirectoryLimits directory;
  DvpVuLimits program;
};

// Owns everything needed to initialize and execute SceneBlock records from
// one level. No member borrows from the temporary ELF, extent, or decoded-WAD
// byte buffers used by load_scene_block_runtime_assets_v1.
struct SceneBlockRuntimeAssetsV1 {
  DiscReport disc;
  std::string executable_sha256;
  std::uint32_t level_id = 0U;
  std::uint64_t companion_record_count = 0U;

  // Build-layout provenance retained for diagnostics. Callers never select
  // overlay chunks or the preamble address themselves.
  std::vector<std::uint16_t> overlay_section_indices;
  std::uint32_t preamble_virtual_address = 0U;

  DvpVuProgramV1 program;
  SceneBlockTaskPreambleV1 preamble;
  SceneBlockDirectoryV1 directory;
};

enum class SceneBlockRuntimeGsStatusV1 : std::uint8_t {
  // Entry 0 did not produce resumable task state, so no record was run.
  not_attempted = 0U,
  // The record ran but did not execute XGKICK.
  no_events,
  // At least one captured XGKICK packet could not be copied through EOP.
  incomplete_stream,
  // Every XGKICK packet was decoded as one ordered GS stream.
  decoded,
};

enum class SceneBlockRuntimeSourceGeometryStatusV1 : std::uint8_t {
  // Source recovery is only attempted after a complete entry-16 GS stream
  // has been decoded.
  not_attempted = 0U,
  // The confirmed source layout was recovered and source_geometry is present.
  recovered,
  // A complete GS packet was captured before a diagnostic VU stop, but its
  // source data does not match the currently confirmed entry-16 layout. A
  // normally completed record never degrades to this status. The diagnostic
  // retains the exact reason.
  unavailable_layout,
};

struct SceneBlockRuntimeExecutionLimitsV1 {
  // task.dvp is shared by entry-0 initialization and the selected record.
  SceneBlockTaskExecutionLimitsV1 task;
  GifGsDecodeLimitsV1 gs;
};

struct SceneBlockRuntimeExecutionV1 {
  SceneBlockTaskInitializationResultV1 initialization;
  // Empty only when initialization did not expose ready_state.
  std::optional<SceneBlockTaskRecordExecutionV1> record;
  SceneBlockRuntimeGsStatusV1 gs_status =
      SceneBlockRuntimeGsStatusV1::not_attempted;
  std::optional<GifGsDecodeResultV1> gs;
  // Present for the confirmed entry-16 input layout. Vertices retain the GS
  // submission order while XYZ comes from the source data before projection.
  std::optional<SceneBlockSourceGeometryV1> source_geometry;
  SceneBlockRuntimeSourceGeometryStatusV1 source_geometry_status =
      SceneBlockRuntimeSourceGeometryStatusV1::not_attempted;
  std::optional<std::string> source_geometry_diagnostic;
};

class SceneBlockRuntimeError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Loads and validates the supported build profile, binds the supplied ELF to
// the ISO boot executable by SHA-256, decodes the profile-selected VU program
// and task preamble, and owns the requested level's SceneBlock directory.
// The returned object can be reused for any number of record executions
// without re-reading either input file.
[[nodiscard]] SceneBlockRuntimeAssetsV1 load_scene_block_runtime_assets_v1(
    const std::filesystem::path &image_path,
    const std::filesystem::path &executable_path, std::uint32_t level_id,
    SceneBlockRuntimeLoadLimitsV1 limits);

// Starts a fresh task frame from the explicit frame transform, executes entry
// 0 and then the selected record, and decodes all complete XGKICK events as
// one continuous GS stream. Diagnostic VU stops are returned in the nested
// execution results rather than converted into guessed state.
[[nodiscard]] SceneBlockRuntimeExecutionV1
execute_scene_block_runtime_record_v1(
    const SceneBlockRuntimeAssetsV1 &assets, std::uint64_t record_index,
    std::uint16_t entrypoint_address,
    const SceneBlockTaskFrameInputV1 &frame_input,
    SceneBlockRuntimeExecutionLimitsV1 limits);

} // namespace openrc
