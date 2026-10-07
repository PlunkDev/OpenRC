#include "openrc/rac_level_frame_schedule.hpp"

#include "openrc/ee_cop1_numeric.hpp"

#include <array>
#include <bit>
#include <limits>
#include <stdexcept>

namespace openrc {
namespace {
using K = RacLevelFrameStepKindV1;

constexpr std::uint32_t kPad = 0x268738;
constexpr std::uint32_t kGameplayUpdate = 0x299250;
constexpr std::uint32_t kSceneUpdate = 0x29a300;

// Table 0x1e8e50 indexed by mode+1 (arms 246950..246a40). The address is the
// call target only; no role is implied beyond the named mode constants.
struct Arm { std::array<std::uint32_t, 3> calls; std::uint8_t count; };
constexpr std::array<Arm, 9> kArms{{
    {{{kPad, 0x1f4918, 0x1f5c60}}, 3},
    {{{kPad, kGameplayUpdate, 0x1f91b0}}, 3},
    {{{0x29ad18, 0, 0}}, 1},
    {{{kPad, kSceneUpdate, 0x1f9210}}, 3},
    {{{kPad, 0x277a88, 0x1f92c0}}, 3},
    {{{kPad, 0x202e48, 0x1f9248}}, 3},
    {{{kPad, 0x29d988, 0x2a1540}}, 3},
    {{{kPad, 0x291868, 0x292fd8}}, 3},
    {{{kPad, 0x29afb8, 0x1f92e8}}, 3},
}};

// 246ab4..246b18: (unsigned)(v-3) < 4, or one of the listed values.
bool overlay_selector(std::int32_t value) {
    if (static_cast<std::uint32_t>(value) - 3U < 4U) return true;
    for (std::int32_t listed : {9, 10, 11, 12, 15, 16, 17, 18, 19, 20})
        if (value == listed) return true;
    return false;
}

void add(RacLevelFramePlanV1& plan, K kind, std::uint32_t address,
         bool catchup = false) {
    plan.steps.push_back({kind, address, catchup});
}

// Arm 0 update 299250 may open the pause gate and return early.
bool gameplay_update(RacLevelFramePlanV1& plan, RacLevelPauseObservationV1 pause,
                     std::uint32_t frames_in_mode, bool catchup) {
    add(plan, K::arm_update, kGameplayUpdate, catchup);
    const bool open = rac_level_pause_gate_open_v1(pause, frames_in_mode);
    if (open) {
        add(plan, K::pause_gate, 0x299320, catchup);
        add(plan, K::call, 0x2016e0, catchup);
    }
    return open;
}
} // namespace

RacLevelFrameStateV1 rac_level_loading_state_v1(
    std::uint64_t elapsed_timer1_ticks_15ee40) {
    return {kRacLevelModeArrivalV1, 0, 0, elapsed_timer1_ticks_15ee40};
}

std::int32_t rac_level_initial_mode_v1(RacLevelModeSelectionInputV1 input) {
    // 2901d4..29021c; slti at 2901fc/29020c is signed, so negatives give 6.
    const auto level = input.level_index_15ee84;
    if (level == 1 && input.byte_13de4b == 0) return kRacLevelModeGameplayV1;
    if (level == 0) return kRacLevelModeGameplayV1;
    if (level == 14 && input.byte_13d4f8 == 0) return kRacLevelModeGameplayV1;
    return level < 20 ? kRacLevelModeArrivalV1 : kRacLevelModeGameplayV1;
}

bool rac_level_pause_gate_open_v1(RacLevelPauseObservationV1 observation,
                                  std::uint32_t frames_in_mode) {
    // bne mode,zero; andi 0x401 == 0 skips; slti frames,8 skips; state != 29.
    return observation.mode_15f6a8 == 0 &&
        (observation.buttons_15efb4 & 0x401U) != 0 &&
        static_cast<std::int32_t>(frames_in_mode) >= 8 &&
        observation.player_state_1414d4 != 29;
}

RacLevelFramePlanV1 plan_rac_level_frame_v1(RacLevelFrameStateV1 state,
                                              const RacLevelFrameInputV1& input) {
    RacLevelFramePlanV1 plan{state, {}, false, false, false, false, false,
                             false, false, false, false, std::nullopt};
    add(plan, K::level_exit_check, 0x2468b0);
    if (input.level_exit_15f650 != 0) {
        plan.level_exit = true;
        add(plan, K::level_exit_call, 0x2940e0);
        return plan;
    }

    // 2468b8..2468e4: zero-extended count added to the 64-bit word, reset.
    plan.next.elapsed_timer1_ticks += input.timer1_at_entry;
    add(plan, K::timer1_accumulate_reset, 0x2468b8);
    add(plan, K::call, 0x1f70f0);
    add(plan, K::word_clear, 0x15f6bc);  // delay slot 2468f4
    for (auto call : {0x2a1b58U, 0x2a1ae8U, 0x200ff0U, 0x201300U, 0x200ef0U,
                      0x12ddc0U})
        add(plan, K::call, call);

    // 246928..246930: addiu mode,1; sltiu 9; beq zero -> 246a58.
    add(plan, K::mode_dispatch, 0x246930);
    const auto index = static_cast<std::uint32_t>(state.mode) + 1U;
    plan.dispatched = index < kArms.size();
    if (plan.dispatched) {
        const Arm& arm = kArms[index];
        for (std::uint8_t i = 0; i < arm.count; ++i) {
            const auto call = arm.calls[i];
            if (call == kPad) {
                add(plan, K::pad, call);
            } else if (call == kGameplayUpdate) {
                plan.pause_gate_in_arm = gameplay_update(
                    plan, input.pause_in_arm, state.frames_in_mode, false);
            } else {
                add(plan, i + 1U == arm.count && arm.count == 3 ? K::arm_render
                                                                 : K::arm_update,
                    call);
            }
        }
    }

    // 246a60..246a78: same mode -> addiu +1 (wraps); changed -> 0.
    const bool same = input.mode_after_arm == state.mode;
    plan.next.mode = input.mode_after_arm;
    plan.next.frames_in_mode = same ? state.frames_in_mode + 1U : 0U;
    add(plan, K::frames_in_mode_write, same ? 0x246a78 : 0x246a6c);

    if (input.frame_skip_15f6bc != 0) {
        plan.frame_skipped = true;
        add(plan, K::call, 0x2a1c68);
        add(plan, K::call, 0x2a1a88);
        add(plan, K::frame_skip_return, 0x246a9c);
        return plan;
    }
    add(plan, K::call, 0x24c308);
    add(plan, K::call, 0x24b638);
    plan.overlay_drawn = overlay_selector(input.overlay_selector_13d46c) &&
        input.overlay_latch_1ba1ac == 0;
    if (plan.overlay_drawn) {
        for (auto call : {0x1f93e0U, 0x2a1e38U, 0x23bba0U, 0x23be70U, 0x23bba0U,
                          0x23bc50U, 0x23cc38U, 0x1f94f8U})
            add(plan, K::call, call);
    }
    add(plan, K::word_clear, 0x1ba1ac);  // delay slot 246c14
    add(plan, K::call, 0x1f3a78);
    add(plan, K::empty_stub_call, 0x1f7118);
    add(plan, K::empty_stub_call, 0x1f7120);
    add(plan, K::call, 0x2a1c68);

    // 246c48..246c8c: cvt.s.w(count) / 9600.0 or 11520.0, stored as float.
    add(plan, K::timer1_read, 0x246c48);
    const bool pal_budget = input.video_selector_15ee80 != 0;
    const std::uint32_t budget_bits = pal_budget ? 0x46340000U : 0x46160000U;
    plan.frame_time_ratio_bits_15f6b4 = ee_cop1_div_bits_v1(
        std::bit_cast<std::uint32_t>(static_cast<float>(input.timer1_at_ratio)),
        budget_bits).bits;
    add(plan, K::frame_ratio_write, 0x246c8c);
    if (input.mode_after_post_arm == 0 && input.scene_word_16c170 != 0)
        add(plan, K::empty_stub_call, 0x1f7110);

    plan.next.mode = input.mode_after_post_arm;
    if (input.player_byte_20b1 == 0) {
        const auto mode = input.mode_after_post_arm;
        if (mode == kRacLevelModeGameplayV1 || mode == kRacLevelModeSceneV1) {
            add(plan, K::timer1_read, 0x246ce4);
            add(plan, K::catchup_test, 0x246cf4);
            const std::uint16_t budget = pal_budget
                ? kRacTimer1BudgetSelectorNonzeroV1 : kRacTimer1BudgetSelector0V1;
            // slt budget,count: strictly greater, signed (count is 16-bit).
            if (input.timer1_at_catchup > budget) {
                if (mode == kRacLevelModeGameplayV1) {
                    plan.catchup_update = true;
                    add(plan, K::pad, kPad, true);
                    plan.pause_gate_in_catchup = gameplay_update(
                        plan, input.pause_in_catchup, plan.next.frames_in_mode, true);
                } else if (input.scene_guard_16c9b0 == 0) {
                    plan.catchup_update = true;
                    add(plan, K::pad, kPad, true);
                    add(plan, K::arm_update, kSceneUpdate, true);
                }
            }
        }
        // Both present paths are identical apart from their pcs.
        plan.presented = true;
        const bool tested = mode == kRacLevelModeGameplayV1 || mode == kRacLevelModeSceneV1;
        add(plan, K::vsync, 0x122598);  // jal at 246d40 or 246d64
        plan.next.presented_frames += 1U;
        add(plan, K::presented_frames_write, tested ? 0x246d58 : 0x246d7c);
        add(plan, K::call, 0x285f18);
        add(plan, K::word_clear, 0x15f690);
    }
    add(plan, K::call, 0x1f88c0);

    // 246d90..246db0: mode 0 and nonzero +0x20b1 select the reload path.
    plan.next.mode = input.mode_at_reload_check;
    plan.reload = input.mode_at_reload_check == 0 &&
        input.player_byte_20b1_at_reload != 0;
    if (plan.reload) {
        for (auto call : {0x2a1a88U, 0x12ec40U, 0x12ddc0U, 0x28f320U, 0x266508U,
                          0x244ae0U})
            add(plan, K::call, call);
        add(plan, K::reload_mode_write, 0x246e04);
        plan.next.mode = 0;
        add(plan, K::call, 0x266210);
        add(plan, K::call, 0x235828);
    }
    add(plan, K::time_record, 0x1feed0);
    return plan;
}

std::optional<std::uint16_t> rac_level_time_record_update_v1(
    std::uint16_t stored_halfword, std::int32_t first_1feed0,
    std::int32_t second_1feed0) {
    // div by 600 (truncating), slt on zero-extended lhu, sh of the second mflo.
    if (static_cast<std::int32_t>(stored_halfword) >= first_1feed0 / 600)
        return std::nullopt;
    return static_cast<std::uint16_t>(second_1feed0 / 600);
}

std::uint64_t openrc_virtual_timer1_ticks_v1(std::uint64_t host_nanoseconds,
                                             std::uint32_t ticks_per_second) {
    if (ticks_per_second == 0)
        throw std::invalid_argument("zero virtual TIMER1 frequency");
    constexpr std::uint64_t kNanoseconds = 1000000000ULL;
    const auto seconds = host_nanoseconds / kNanoseconds;
    const auto remainder = host_nanoseconds % kNanoseconds;
    constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
    if (seconds > kMax / ticks_per_second) return kMax;
    const auto whole = seconds * ticks_per_second;
    // remainder * rate < 1e9 * 2^32, which fits in 64 bits.
    const auto part = remainder * ticks_per_second / kNanoseconds;
    return whole > kMax - part ? kMax : whole + part;
}

std::uint16_t openrc_virtual_timer1_count_v1(std::uint64_t virtual_ticks) {
    return static_cast<std::uint16_t>(virtual_ticks & 0xffffU);
}
} // namespace openrc
