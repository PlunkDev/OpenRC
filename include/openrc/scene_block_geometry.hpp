#pragma once

#include "openrc/gif_gs.hpp"
#include "openrc/scene_block_task_execute.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint16_t kSceneBlockSourceGeometryEntrypointV1 = 16U;
inline constexpr std::uint16_t kSceneBlockSourceIndexFirstQwordV1 = 250U;
inline constexpr std::uint8_t kSceneBlockSourcePositionPointerLaneV1 = 3U;

struct SceneBlockSourceLaneProvenanceV1 {
  std::uint16_t qword = 0U;
  std::uint8_t lane = 0U;
  std::optional<std::uint64_t> last_write_index;

  [[nodiscard]] bool
  operator==(const SceneBlockSourceLaneProvenanceV1 &) const = default;
};

struct SceneBlockSourceQwordProvenanceV1 {
  std::uint16_t qword = 0U;
  std::optional<std::uint64_t> last_write_index;

  [[nodiscard]] bool
  operator==(const SceneBlockSourceQwordProvenanceV1 &) const = default;
};

// One source-space vertex recovered from the exact entry-16 input indirection
// chain. Entries remain in one-to-one order with GifGsDecodeResultV1::vertices.
struct SceneBlockSourceVertexV1 {
  std::uint64_t gs_vertex_index = 0U;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::array<std::uint8_t, 4U> rgba{};

  // The per-GS-vertex byte in qwords 250 onward selects a descriptor qword.
  SceneBlockSourceLaneProvenanceV1 descriptor_index_source;
  // The descriptor's W lane selects the signed XYZ qword.
  SceneBlockSourceLaneProvenanceV1 position_pointer_source;
  SceneBlockSourceQwordProvenanceV1 position_source;
  // Entry 16 interleaves one V4-8 RGBA qword immediately after each position.
  SceneBlockSourceQwordProvenanceV1 color_source;
};

struct SceneBlockSourceGeometryV1 {
  std::vector<SceneBlockSourceVertexV1> vertices;
  std::uint64_t unique_descriptor_qword_count = 0U;
  std::uint64_t unique_position_qword_count = 0U;
};

struct SceneBlockSourceGeometryLimitsV1 {
  std::uint64_t max_vertices = 0U;
  SceneBlockDvpVuBridgeLimitsV1 bridge;
};

class SceneBlockSourceGeometryError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Reconstructs the record input RAM from the state produced by entry 0 and
// follows the confirmed entry-16 index -> descriptor -> position chain. This
// V1 contract is intentionally restricted to the first (TOP=0) input bank.
// Every consumed word must be fully known, and the adjacent source RGBA must
// exactly equal the corresponding decoded GS vertex color.
[[nodiscard]] SceneBlockSourceGeometryV1
recover_scene_block_source_geometry_v1(
    const SceneBlockTaskExecutionStateV1 &initialized_state,
    const SceneBlockTaskRecordExecutionV1 &record,
    const GifGsDecodeResultV1 &gs,
    SceneBlockSourceGeometryLimitsV1 limits);

} // namespace openrc
