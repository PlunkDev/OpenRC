#pragma once

#include "openrc/scene_block_vif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::size_t kSceneBlockVuLaneCount = 4U;
// VU1 data memory is 16 KiB addressed as 1024 128-bit qwords.
inline constexpr std::size_t kSceneBlockVuMemoryQwordCount = 1024U;

enum class SceneBlockVuValueState : std::uint8_t {
    indeterminate = 0,
    known = 1,
};

struct SceneBlockVuValueV1 {
    SceneBlockVuValueState state = SceneBlockVuValueState::indeterminate;
    // Raw 32-bit lane value. It is meaningful only when state is known.
    std::uint32_t bits = 0;

    [[nodiscard]] bool operator==(const SceneBlockVuValueV1&) const = default;
};

enum class SceneBlockVuAdditionMode : std::uint8_t {
    normal = 0,
    offset = 1,
    difference = 2,
};

enum class SceneBlockVuLaneSource : std::uint8_t {
    payload = 0,
    cycle_fill_indeterminate = 1,
    // V3-16 is fetched through the V4 unpack path on the VIF hardware. Its W
    // lane therefore comes from the 16-bit element immediately following Z.
    v3_w_lookahead = 2,
    // When that lookahead would begin at the next 128-bit source qword, VIF
    // supplies a known zero instead of fetching across the qword boundary.
    v3_w_qword_boundary_zero = 3,
    // A bounded public input can end before the otherwise-required lookahead
    // halfword. Its value cannot be reconstructed without those source bytes.
    v3_w_unavailable = 4,
};

struct SceneBlockVuLaneWriteV1 {
    SceneBlockVuLaneSource source =
        SceneBlockVuLaneSource::cycle_fill_indeterminate;
    // Exact input coordinates for a consumed payload component or V3
    // lookahead. The range is empty for generated or indeterminate values.
    SceneBlockVifRange source_range;
    SceneBlockVuValueV1 unpacked_value;
    SceneBlockVuValueV1 row_before;
    SceneBlockVuValueV1 written_value;
};

struct SceneBlockVuVectorWriteV1 {
    std::uint64_t command_index = 0;
    std::uint16_t output_vector_index = 0;
    // Empty for a STCYCL fill-generated vector.
    std::optional<std::uint16_t> input_vector_index;
    std::uint64_t unwrapped_destination_qword = 0;
    std::uint16_t destination_qword = 0;
    bool wrapped = false;
    SceneBlockVuAdditionMode addition_mode =
        SceneBlockVuAdditionMode::normal;
    std::array<SceneBlockVuLaneWriteV1, kSceneBlockVuLaneCount> lanes{};
};

struct SceneBlockVuMemoryQwordV1 {
    std::array<SceneBlockVuValueV1, kSceneBlockVuLaneCount> lanes{};
    std::uint64_t write_count = 0;
    // Index into SceneBlockVuSnapshotV1::writes, or empty when untouched.
    std::optional<std::uint64_t> last_write_index;
};

struct SceneBlockVuStateV1 {
    std::uint16_t tops_qword = 0;
    // CL retains its raw 8-bit value. WL is effective and uses 256 when its
    // raw STCYCL field is zero.
    std::uint16_t cycle_length = 1;
    std::uint16_t write_length = 1;
    SceneBlockVuAdditionMode addition_mode =
        SceneBlockVuAdditionMode::normal;
    std::array<SceneBlockVuValueV1, kSceneBlockVuLaneCount> row{};
};

struct SceneBlockVuLimits {
    SceneBlockVifLimits vif;
    std::uint64_t max_vector_writes = 0;
};

struct SceneBlockVuExecutionOptionsV1 {
    // The caller must provide the VIF1_TOPS qword address explicitly.
    std::uint16_t tops_qword;
};

struct SceneBlockVuSnapshotV1 {
    SceneBlockVifStreamV1 stream;
    SceneBlockVuStateV1 final_state;
    // Unwritten lanes remain indeterminate. Writes are also retained in
    // execution order so overlaps and address wrapping remain auditable.
    std::array<SceneBlockVuMemoryQwordV1, kSceneBlockVuMemoryQwordCount>
        memory{};
    std::vector<SceneBlockVuVectorWriteV1> writes;
    std::uint64_t total_vector_writes = 0;
    // Number of distinct qword addresses written at least once.
    std::uint64_t unique_qword_writes = 0;
    // Number of vector writes whose wrapped qword was already written by an
    // earlier vector in this execution.
    std::uint64_t overwrite_vector_writes = 0;
    // Number of writes whose unwrapped destination was at least 1024.
    std::uint64_t wrapped_vector_writes = 0;
    // Appended for source compatibility with positional V1 aggregate users.
    SceneBlockVuStateV1 initial_state;
};

class SceneBlockVuError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses and executes one bounded SceneBlock VIF1 stream. Payload bytes are
// read directly from input and are never retained or copied into the result.
[[nodiscard]] SceneBlockVuSnapshotV1 execute_scene_block_vu_v1(
    std::span<const std::byte> bytes,
    SceneBlockVuExecutionOptionsV1 options,
    SceneBlockVuLimits limits);

// Executes a VIF continuation from the complete inherited control state.
// Memory begins untouched because UNPACK values depend on ROW/mode rather
// than previous destination contents; ordered writes can then be replayed
// into a carried DVP VU state.
[[nodiscard]] SceneBlockVuSnapshotV1 execute_scene_block_vu_from_state_v1(
    std::span<const std::byte> bytes,
    SceneBlockVuStateV1 initial_state,
    SceneBlockVuLimits limits);

} // namespace openrc
