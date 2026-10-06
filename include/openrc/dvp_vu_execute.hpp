#pragma once

#include "openrc/dvp_vu.hpp"
#include "openrc/scene_block_vu.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::size_t kDvpVuLaneCount = 4U;
inline constexpr std::size_t kDvpVuVectorRegisterCount = 32U;
inline constexpr std::size_t kDvpVuIntegerRegisterCount = 16U;
inline constexpr std::size_t kDvpVuDataMemoryQwordCount = 1024U;

// A value may be only partly known. A set bit in known_mask means that the
// corresponding bit in bits is meaningful. This prevents unknown VIF lanes or
// missing runtime state from silently turning into zeroes during diagnostics.
struct DvpVuWordV1 {
    std::uint32_t bits = 0U;
    std::uint32_t known_mask = 0U;

    [[nodiscard]] bool operator==(const DvpVuWordV1&) const = default;
};

struct DvpVuVectorV1 {
    std::array<DvpVuWordV1, kDvpVuLaneCount> lanes{};

    [[nodiscard]] bool operator==(const DvpVuVectorV1&) const = default;
};

struct DvpVuExecutionStateV1 {
    std::array<DvpVuVectorV1, kDvpVuDataMemoryQwordCount> data_memory{};
    std::array<DvpVuVectorV1, kDvpVuVectorRegisterCount> vf{};
    std::array<DvpVuWordV1, kDvpVuIntegerRegisterCount> vi{};
    DvpVuVectorV1 accumulator;
    // One hidden overflow latch per lane. Only bit0 is architectural; unknown
    // history remains unknown until an actual ACC-writing operation replaces
    // it. A finite Fmax word alone cannot reconstruct this state.
    std::array<DvpVuWordV1,kDvpVuLaneCount> accumulator_overflow{};
    DvpVuWordV1 scalar_i;
    DvpVuWordV1 scalar_q;
    // MAC uses its architectural low 16 bits, STATUS its low 12 bits, and
    // CLIP its low 24 bits.
    DvpVuWordV1 mac_flags;
    DvpVuWordV1 status_flags;
    DvpVuWordV1 clip_flags;
    // VIF1 TOP read by XTOP. It is deliberately separate from UNPACK TOPS.
    DvpVuWordV1 xtop_qword;
    std::uint16_t pc = 0U;
};

enum class DvpVuTimingModelV1 : std::uint8_t {
    // Upper/lower halves read one pre-pair snapshot. Integer and vector
    // writes commit after the pair; confirmed STATUS/CLIP and Q latencies are
    // modeled, while the complete VU1 FMAC forwarding network is not yet.
    bounded_functional = 0,
};

struct DvpVuExecutionLimitsV1 {
    std::uint64_t max_instruction_pairs = 0U;
    std::uint64_t max_xgkick_events = 0U;
    std::uint64_t max_xgkick_tags_per_event = 0U;
    std::uint64_t max_xgkick_qwords_per_event = 0U;
};

struct DvpVuExecutionOptionsV1 {
    std::uint16_t entrypoint_address = 0U;
    bool stop_after_first_xgkick = false;
};

enum class DvpVuTerminationV1 : std::uint8_t {
    program_end = 0,
    stopped_after_xgkick,
    instruction_limit,
    xgkick_event_limit,
    xgkick_tag_limit,
    xgkick_qword_limit,
    indeterminate_control,
    indeterminate_memory_address,
    unsupported_instruction,
    unmapped_instruction,
};

enum class DvpVuExecutionWarningV1 : std::uint8_t {
    // A remaining operation uses host arithmetic or carries its older
    // qualification (for example ITOF/CLIP). FMAC/DIV have separate warnings.
    host_float_approximation = 0,
    // Q was read while a newer DIV result was still pending; the visible old
    // Q value is used, matching the lack of a Q data interlock.
    q_read_before_ready,
    // Synchronous XGKICK diagnostics commit architecturally older queued
    // stores before copying the packet instead of modeling live PATH1 reads.
    forced_store_commit_for_xgkick_snapshot,
    // The public VU manual documents a memory operation in an E delay slot as
    // undefined. Program 55907 contains one, so compatibility mode executes
    // and commits it while retaining this warning.
    documented_undefined_e_delay_memory,
    // ADD/SUB use an integer one-guard reference model derived from public
    // reports, not an exhaustive physical-console qualification. This does
    // not establish ACC overflow history or full FMAC forwarding correctness.
    vu_add_sub_reference_model,
    // Ordered MUL/MULA values use the integer Booth/carry reference model.
    // This is not exhaustive hardware qualification, hidden ACC overflow
    // history, compound MADD/MSUB semantics, or complete FMAC forwarding.
    vu_mul_reference_model,
    // Ordered product/ACC value and events, with explicit hidden overflow
    // history. This does not qualify all FMAC forwarding or physical timing.
    vu_madd_reference_model,
    vu_div_reference_model,
};

struct DvpVuGifTagV1 {
    std::uint16_t nloop = 0U;
    bool eop = false;
    bool pre = false;
    std::uint16_t prim = 0U;
    std::uint8_t format = 0U;
    std::uint8_t register_count = 0U;
    std::uint64_t payload_qword_count = 0U;
    // GIFtag REGS descriptors, in transmission order. Only the leading
    // register_count entries are active. The descriptors occupy the upper
    // 64 bits of the tag and may independently be indeterminate.
    std::array<std::uint8_t, 16U> registers{};
    bool registers_known = false;

    [[nodiscard]] bool operator==(const DvpVuGifTagV1&) const = default;
};

struct DvpVuXgkickTagV1 {
    std::uint16_t memory_qword = 0U;
    DvpVuGifTagV1 tag;
    // Index of the tag qword in DvpVuXgkickEventV1::packet_qwords.
    std::uint64_t packet_qword_index = 0U;
};

struct DvpVuXgkickEventV1 {
    std::uint16_t instruction_address = 0U;
    DvpVuWordV1 base_qword;
    // Packet qwords are immutable copies made synchronously at XGKICK. VU1
    // data-memory addressing wraps at 1024 qwords.
    std::vector<DvpVuVectorV1> packet_qwords;
    std::vector<DvpVuXgkickTagV1> tags;
    bool packet_complete = false;
    bool encountered_indeterminate_tag = false;
    bool wrapped_memory = false;
};

struct DvpVuExecutionResultV1 {
    DvpVuTimingModelV1 timing_model = DvpVuTimingModelV1::bounded_functional;
    DvpVuTerminationV1 termination = DvpVuTerminationV1::unmapped_instruction;
    DvpVuExecutionStateV1 final_state;
    std::uint64_t executed_instruction_pairs = 0U;
    std::vector<std::uint16_t> instruction_trace;
    std::vector<DvpVuXgkickEventV1> xgkick_events;
    std::vector<DvpVuExecutionWarningV1> warnings;
    std::optional<std::uint16_t> stopped_instruction_address;
};

struct SceneBlockDvpVuInvocationOptionsV1 {
    std::uint64_t write_prefix_count = 0U;
    std::uint16_t entrypoint_address = 0U;
    DvpVuWordV1 xtop_qword;
};

struct SceneBlockDvpVuBridgeLimitsV1 {
    std::uint64_t max_replayed_writes = 0U;
};

struct SceneBlockDvpVuInvocationV1 {
    DvpVuExecutionStateV1 initial_state;
    std::uint64_t applied_write_count = 0U;
    std::uint16_t entrypoint_address = 0U;
    // Diagnostic UNPACK state only; it is never substituted for XTOP.
    std::uint16_t unpack_tops_qword = 0U;
};

class DvpVuExecutionError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Returns an indeterminate state with only the architectural VI0 and VF0
// constants initialized.
[[nodiscard]] DvpVuExecutionStateV1 make_dvp_vu_execution_state_v1();

// Replays one exact ordered write range into an existing VU state. All
// metadata and limits are validated before the first destination is changed,
// so malformed input cannot leave a partially updated state.
void apply_scene_block_dvp_vu_writes_v1(
    DvpVuExecutionStateV1& state,
    const SceneBlockVuSnapshotV1& snapshot,
    std::uint64_t first_write_index,
    std::uint64_t write_count,
    SceneBlockDvpVuBridgeLimitsV1 limits);

// Replays exactly the requested prefix of VIF writes into owned VU1 memory.
// The caller still selects the MSCAL entrypoint and supplies TOP explicitly;
// no phase-to-entrypoint relationship is inferred.
[[nodiscard]] SceneBlockDvpVuInvocationV1 make_scene_block_dvp_vu_invocation_v1(
    const SceneBlockVuSnapshotV1& snapshot,
    const DvpVuProgramV1& program,
    SceneBlockDvpVuInvocationOptionsV1 options,
    SceneBlockDvpVuBridgeLimitsV1 limits);

// Executes a bounded decoded program. Indeterminate runtime inputs are
// propagated. A read through a partly unknown RAM address retains only bits
// common to every possible addressed value; unknown stores, control targets
// and XGKICK addresses still stop. Normal diagnostic stops are termination values;
// exceptions are reserved for malformed API input or zero safety limits.
[[nodiscard]] DvpVuExecutionResultV1
execute_dvp_vu_program_v1(const DvpVuProgramV1& program,
                          DvpVuExecutionStateV1 initial_state,
                          DvpVuExecutionOptionsV1 options,
                          DvpVuExecutionLimitsV1 limits);

} // namespace openrc
