#pragma once

#include "openrc/scene_block_vu.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace openrc {

struct SceneBlockVuQwordRunV1 {
    std::uint16_t first_qword = 0;
    std::uint16_t qword_count = 0;

    [[nodiscard]] bool
    operator==(const SceneBlockVuQwordRunV1&) const = default;
};

struct SceneBlockVuCommandPhaseV1 {
    // Exact, contiguous subrange of SceneBlockVuSnapshotV1::stream.commands.
    std::uint64_t first_command_index = 0;
    std::uint64_t command_count = 0;
    // Exact byte range covered by those commands' packet ranges.
    SceneBlockVifRange stream_range;

    std::uint64_t control_command_count = 0;
    std::uint64_t unpack_command_count = 0;
    std::uint64_t nop_command_count = 0;

    // Exact, contiguous subrange of SceneBlockVuSnapshotV1::writes.
    std::uint64_t first_write_index = 0;
    std::uint64_t write_count = 0;

    SceneBlockVuStateV1 state_before;
    SceneBlockVuStateV1 state_after;

    // Sorted, disjoint coverage of wrapped VU1 qword addresses. Address
    // wrapping is represented by separate runs instead of a false min/max
    // interval across the wrap boundary.
    std::vector<SceneBlockVuQwordRunV1> unique_destination_runs;
    std::uint64_t unique_qword_count = 0;
    // A write is internal when the same phase already wrote its qword. A
    // prior-phase overwrite is the first write to that qword in this phase
    // when an earlier phase wrote it. The two counts are disjoint.
    std::uint64_t internal_overwrite_count = 0;
    std::uint64_t prior_phase_overwrite_count = 0;
};

class SceneBlockVuPhaseError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Partitions a validated executor snapshot into neutral command phases. NOPs
// do not affect the grammar. A control command following one or more UNPACKs
// starts the next phase; NOPs immediately before that control remain attached
// to the preceding phase. Leading and trailing NOPs belong to the first and
// last phase respectively. An empty command stream produces no phases.
//
// The result owns only metadata, state values, and ranges. It does not retain
// pointers into the snapshot or copy any VIF payload bytes.
[[nodiscard]] std::vector<SceneBlockVuCommandPhaseV1>
group_scene_block_vu_phases_v1(const SceneBlockVuSnapshotV1& snapshot);

} // namespace openrc
