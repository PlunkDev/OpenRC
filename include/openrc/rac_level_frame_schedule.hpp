#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace openrc {

// Model of the level module entry 0x2465f8 (PAL ELF SHA-256 17f8a846...b122b):
// prologue words and one iteration of the frame loop 0x2468a8..0x246e84.
// See docs/RAC_LEVEL_FRAME_SCHEDULE_V1.md for the address/status table.

inline constexpr std::int32_t kRacLevelModeGameplayV1 = 0;   // arm 246970
inline constexpr std::int32_t kRacLevelModeSceneV1 = 2;      // arm 2469a0
inline constexpr std::int32_t kRacLevelModeTransitionV1 = 3; // arm 2469c0
inline constexpr std::int32_t kRacLevelModeArrivalV1 = 6;    // arm 246a20

// Frame budgets selected by word 0x15ee80 at 246c60..246c74 and 246ce0..246cec.
inline constexpr std::uint16_t kRacTimer1BudgetSelector0V1 = 9600;
inline constexpr std::uint16_t kRacTimer1BudgetSelectorNonzeroV1 = 11520;

// Words owned by the scheduler. mode is the value of 0x15f6a8 as the host
// knows it when the loop reads it at 246924; arms own all other writes.
struct RacLevelFrameStateV1 {
    std::int32_t mode;                   // 0x15f6a8
    std::uint32_t frames_in_mode;        // 0x15f6ac, 32-bit addiu, read signed
    std::uint32_t presented_frames;      // 0x15f4f8
    std::uint64_t elapsed_timer1_ticks;  // 0x15ee40
};

// 2466d4/2466dc write frames=0, mode=6; 24674c clears 0x15f4f8 in the delay
// slot of 122598(0). 0x15ee40 is not written by the prologue, so its prior
// value is a mandatory argument.
[[nodiscard]] RacLevelFrameStateV1 rac_level_loading_state_v1(
    std::uint64_t elapsed_timer1_ticks_15ee40);

// Inputs of 2901a8. Both flag bytes are mandatory; there is no default.
struct RacLevelModeSelectionInputV1 {
    std::int32_t level_index_15ee84;
    std::uint8_t byte_13de4b;  // read only for level 1
    std::uint8_t byte_13d4f8;  // read only for level 14
};

[[nodiscard]] std::int32_t rac_level_initial_mode_v1(
    RacLevelModeSelectionInputV1 input);

// Values seen by 299250 at 2992e8 (mode), 2992f8 (mask) and 29931c (state).
struct RacLevelPauseObservationV1 {
    std::int32_t mode_15f6a8;
    std::uint32_t buttons_15efb4;
    std::int32_t player_state_1414d4;
};

// 2992e8..299324: open -> 2016e0(3,0,0) (writes mode 4) and early return.
[[nodiscard]] bool rac_level_pause_gate_open_v1(
    RacLevelPauseObservationV1 observation, std::uint32_t frames_in_mode);

// Observations at the source read points of one iteration. Opaque calls
// between read points may change these words, so each read is explicit.
struct RacLevelFrameInputV1 {
    std::uint32_t level_exit_15f650;          // 2468ac / 246e50
    std::uint16_t timer1_at_entry;            // 2468c8, before the reset
    RacLevelPauseObservationV1 pause_in_arm;  // used only by arm 0 (299250)
    std::int32_t mode_after_arm;              // 246a5c or delay slot to 246a60
    std::uint32_t frame_skip_15f6bc;          // 246a80
    std::int32_t overlay_selector_13d46c;     // 246ab8
    std::uint32_t overlay_latch_1ba1ac;       // 246b28
    std::uint16_t timer1_at_ratio;            // 246c48
    std::uint32_t video_selector_15ee80;      // 246c58, 246cd8
    std::int32_t mode_after_post_arm;         // 246c84, 246cbc
    std::uint32_t scene_word_16c170;          // 246c94
    std::uint8_t player_byte_20b1;            // 246cb4 (0x141501)
    std::uint16_t timer1_at_catchup;          // 246ce4
    std::int16_t scene_guard_16c9b0;          // 246d24
    RacLevelPauseObservationV1 pause_in_catchup;
    std::int32_t mode_at_reload_check;        // 246d94
    std::uint8_t player_byte_20b1_at_reload;  // 246da0
};

enum class RacLevelFrameStepKindV1 {
    level_exit_check,      // 2468b0 / 246e4c
    level_exit_call,       // 2940e0 at 246e88
    timer1_accumulate_reset,
    word_clear,            // address field is the cleared word
    call,                  // opaque source call
    empty_stub_call,       // callee is `jr ra; nop`
    mode_dispatch,         // 246930 table 0x1e8e50
    pad,                   // 268738
    arm_update,
    arm_render,
    pause_gate,            // inside 299250; 2016e0(3,0,0), update returns
    frames_in_mode_write,
    frame_skip_return,     // 246a9c branch to 2468b0
    timer1_read,
    frame_ratio_write,     // 0x15f6b4
    catchup_test,          // 246cf4
    vsync,                 // 122598(0)
    presented_frames_write,
    reload_mode_write,     // 246e04 mode = 0
    time_record,           // 1feed0 / 246e24..246e84
};

struct RacLevelFrameStepV1 {
    RacLevelFrameStepKindV1 kind;
    // Call target for calls/pad/arm/vsync/time_record, cleared word for
    // word_clear, otherwise the pc of the deciding or storing instruction.
    std::uint32_t source_address;
    bool catchup;
};

struct RacLevelFramePlanV1 {
    RacLevelFrameStateV1 next;
    std::vector<RacLevelFrameStepV1> steps;
    bool level_exit;
    bool dispatched;
    bool pause_gate_in_arm;
    bool frame_skipped;
    bool overlay_drawn;
    bool presented;
    bool catchup_update;
    bool pause_gate_in_catchup;
    bool reload;
    // Bit pattern stored at 246c8c: cvt.s.w(count) / budget via the PS2 FDIV
    // reference (physical_console_qualified=false). Absent when not reached.
    std::optional<std::uint32_t> frame_time_ratio_bits_15f6b4;
};

[[nodiscard]] RacLevelFramePlanV1 plan_rac_level_frame_v1(
    RacLevelFrameStateV1 state, const RacLevelFrameInputV1& input);

// 246e18..246e84: record halfword at 0x151782 + 8 * level is replaced by the
// second 1feed0 result / 600 only when (signed) stored < first result / 600.
[[nodiscard]] std::optional<std::uint16_t> rac_level_time_record_update_v1(
    std::uint16_t stored_halfword, std::int32_t first_1feed0,
    std::int32_t second_1feed0);

// --- OpenRC host policy, not source behaviour --------------------------------
// Virtual TIMER1 rate. 0x82 written to T1_MODE at 23e248 selects BUSCLK/256;
// 576000 Hz assumes the documented 147.456 MHz BUSCLK (INFERRED, not measured).
inline constexpr std::uint32_t kOpenrcVirtualTimer1HzV1 = 576000;

// Host nanoseconds -> virtual TIMER1 ticks (floor, saturating). Policy only.
[[nodiscard]] std::uint64_t openrc_virtual_timer1_ticks_v1(
    std::uint64_t host_nanoseconds, std::uint32_t ticks_per_second);

// Value the 16-bit T1_COUNT register would show (wraps; INFERRED hardware).
[[nodiscard]] std::uint16_t openrc_virtual_timer1_count_v1(
    std::uint64_t virtual_ticks);

} // namespace openrc
