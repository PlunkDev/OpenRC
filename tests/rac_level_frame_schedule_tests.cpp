#include "openrc/rac_level_frame_schedule.hpp"

#include "openrc/ee_cop1_numeric.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace openrc;
using K = RacLevelFrameStepKindV1;

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// Steady iteration: the mode word is unchanged at every read point, no pause
// buttons, no overlay, no frame skip, no player byte, work below budget.
RacLevelFrameInputV1 steady(std::int32_t mode) {
    return {
        .level_exit_15f650 = 0,
        .timer1_at_entry = 11520,
        .pause_in_arm = {mode, 0, 0},
        .mode_after_arm = mode,
        .frame_skip_15f6bc = 0,
        .overlay_selector_13d46c = 0,
        .overlay_latch_1ba1ac = 0,
        .timer1_at_ratio = 5760,
        .video_selector_15ee80 = 1,
        .mode_after_post_arm = mode,
        .scene_word_16c170 = 0,
        .player_byte_20b1 = 0,
        .timer1_at_catchup = 5760,
        .scene_guard_16c9b0 = 0,
        .pause_in_catchup = {mode, 0, 0},
        .mode_at_reload_check = mode,
        .player_byte_20b1_at_reload = 0,
    };
}

std::vector<std::uint32_t> addresses(const RacLevelFramePlanV1& plan, K kind,
                                     bool catchup = false) {
    std::vector<std::uint32_t> result;
    for (const auto& step : plan.steps)
        if (step.kind == kind && step.catchup == catchup)
            result.push_back(step.source_address);
    return result;
}

std::size_t position(const RacLevelFramePlanV1& plan, K kind,
                     std::uint32_t address, bool catchup = false) {
    for (std::size_t i = 0; i < plan.steps.size(); ++i)
        if (plan.steps[i].kind == kind && plan.steps[i].source_address == address &&
            plan.steps[i].catchup == catchup)
            return i;
    return plan.steps.size();
}

bool has(const RacLevelFramePlanV1& plan, K kind, std::uint32_t address,
         bool catchup = false) {
    return position(plan, kind, address, catchup) < plan.steps.size();
}

void prologue_and_mode_selection() {
    const auto initial = rac_level_loading_state_v1(123);
    check(initial.mode == 6 && initial.frames_in_mode == 0 &&
          initial.presented_frames == 0 && initial.elapsed_timer1_ticks == 123,
          "prologue words");
    for (auto a : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{255}})
        for (auto b : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{255}}) {
            const auto mode = [&](std::int32_t level) {
                return rac_level_initial_mode_v1({level, a, b});
            };
            check(mode(0) == 0, "level 0 -> gameplay regardless of flags");
            check(mode(1) == (a == 0 ? 0 : 6), "level 1 follows 0x13de4b");
            check(mode(14) == (b == 0 ? 0 : 6), "level 14 follows 0x13d4f8");
            for (std::int32_t level = 2; level <= 19; ++level)
                if (level != 14) check(mode(level) == 6, "levels 2..19 arrive");
            for (std::int32_t level : {20, 21, 100, std::numeric_limits<std::int32_t>::max()})
                check(mode(level) == 0, "levels >= 20 -> 0");
            check(mode(-1) == 6, "signed slti for negative level");
        }
}

void dispatch() {
    constexpr std::uint32_t expected[][3] = {
        {0x268738, 0x1f4918, 0x1f5c60}, {0x268738, 0x299250, 0x1f91b0},
        {0x29ad18, 0, 0},               {0x268738, 0x29a300, 0x1f9210},
        {0x268738, 0x277a88, 0x1f92c0}, {0x268738, 0x202e48, 0x1f9248},
        {0x268738, 0x29d988, 0x2a1540}, {0x268738, 0x291868, 0x292fd8},
        {0x268738, 0x29afb8, 0x1f92e8},
    };
    for (std::int32_t mode = -1; mode <= 7; ++mode) {
        const auto plan = plan_rac_level_frame_v1({mode, 5, 17, 100}, steady(mode));
        check(plan.dispatched, "in-range dispatch");
        std::vector<std::uint32_t> actual;
        for (const auto& step : plan.steps)
            if (!step.catchup && (step.kind == K::pad || step.kind == K::arm_update ||
                                  step.kind == K::arm_render))
                actual.push_back(step.source_address);
        const auto row = expected[mode + 1];
        const std::size_t count = mode == 1 ? 1 : 3;
        check(actual == std::vector<std::uint32_t>(row, row + count), "arm calls");
        check(plan.next.frames_in_mode == 6 && plan.next.presented_frames == 18 &&
              plan.next.elapsed_timer1_ticks == 100 + 11520, "counters per iteration");
    }
    for (std::int32_t mode : {-2, 8, 100, std::numeric_limits<std::int32_t>::min()}) {
        const auto plan = plan_rac_level_frame_v1({mode, 5, 17, 0}, steady(mode));
        check(!plan.dispatched && addresses(plan, K::pad).empty() &&
              addresses(plan, K::arm_update).empty(), "out of range skips arm");
        check(plan.next.frames_in_mode == 6 && plan.presented &&
              plan.next.presented_frames == 18, "out of range still counts and presents");
    }
}

void frames_in_mode_and_present_counter() {
    auto changed_input = steady(0);
    changed_input.mode_after_arm = changed_input.mode_after_post_arm =
        changed_input.mode_at_reload_check = 2;
    const auto changed = plan_rac_level_frame_v1({0, 42, 7, 0}, changed_input);
    check(changed.next.mode == 2 && changed.next.frames_in_mode == 0 &&
          has(changed, K::frames_in_mode_write, 0x246a6c), "mode change resets at 246a6c");
    const auto same = plan_rac_level_frame_v1({0, 42, 7, 0}, steady(0));
    check(same.next.frames_in_mode == 43 && has(same, K::frames_in_mode_write, 0x246a78),
          "same mode increments at 246a78");

    const auto wrapped = plan_rac_level_frame_v1(
        {0, UINT32_MAX, UINT32_MAX, UINT64_MAX}, steady(0));
    check(wrapped.next.frames_in_mode == 0 && wrapped.next.presented_frames == 0 &&
          wrapped.next.elapsed_timer1_ticks == 11519, "32/64-bit words wrap");

    RacLevelFrameStateV1 state = rac_level_loading_state_v1(0);
    state.mode = 0;
    for (std::uint32_t i = 1; i <= 5; ++i) {
        state = plan_rac_level_frame_v1(state, steady(0)).next;
        check(state.presented_frames == i && state.frames_in_mode == i,
              "one present per iteration");
    }
}

void pause_gate() {
    for (std::uint32_t frames : {7U, 8U, 9U}) {
        auto in = steady(0);
        in.pause_in_arm.buttons_15efb4 = 0x401;
        in.mode_after_arm = in.mode_after_post_arm = in.mode_at_reload_check =
            frames >= 8 ? 4 : 0;
        const auto plan = plan_rac_level_frame_v1({0, frames, 0, 0}, in);
        check(plan.pause_gate_in_arm == (frames >= 8), "pause threshold 8 (slti)");
        if (frames == 8) {
            check(position(plan, K::arm_update, 0x299250) < position(plan, K::pause_gate, 0x299320) &&
                  position(plan, K::pause_gate, 0x299320) < position(plan, K::call, 0x2016e0) &&
                  position(plan, K::call, 0x2016e0) < position(plan, K::arm_render, 0x1f91b0),
                  "pause gate inside 299250, before render");
            check(plan.next.mode == 4 && plan.next.frames_in_mode == 0,
                  "menu mode written by 2016e0 resets counter");
        }
    }
    for (std::int32_t mode : {-1, 1, 2, 3, 4, 6, 7}) {
        check(!rac_level_pause_gate_open_v1({mode, 0x401, 0}, 100),
              "pause only in mode 0");
        auto in = steady(mode);
        in.pause_in_arm = {0, 0x401, 0};  // arm 0 is not dispatched
        check(!plan_rac_level_frame_v1({mode, 100, 0, 0}, in).pause_gate_in_arm,
              "other arms have no pause gate");
    }
    check(rac_level_pause_gate_open_v1({0, 0x001, 0}, 8), "mask bit 0");
    check(rac_level_pause_gate_open_v1({0, 0x400, 0}, 8), "mask bit 10");
    check(!rac_level_pause_gate_open_v1({0, 0x002, 0}, 8), "other buttons");
    check(!rac_level_pause_gate_open_v1({0, 0x401, 29}, 8), "player state 29");
    check(!rac_level_pause_gate_open_v1({0, 0x401, 0}, 0x80000000U),
          "signed frames comparison");
}

void catchup() {
    for (std::uint32_t selector : {0U, 1U}) {
        const std::uint16_t budget = selector == 0 ? 9600 : 11520;
        for (std::int32_t mode : {0, 2}) {
            auto in = steady(mode);
            in.video_selector_15ee80 = selector;
            in.timer1_at_ratio = budget;
            in.timer1_at_catchup = budget;
            const auto at = plan_rac_level_frame_v1({mode, 8, 4, 0}, in);
            check(!at.catchup_update && has(at, K::catchup_test, 0x246cf4),
                  "equal to budget does not catch up (slt)");
            check(at.frame_time_ratio_bits_15f6b4 == 0x3f800000U, "ratio 1.0");

            in.timer1_at_catchup = static_cast<std::uint16_t>(budget + 1);
            const auto over = plan_rac_level_frame_v1({mode, 8, 4, 0}, in);
            check(over.catchup_update && over.presented &&
                  over.next.presented_frames == 5, "one catchup, one present");
            const auto update = mode == 0 ? 0x299250U : 0x29a300U;
            check(addresses(over, K::pad, true) == std::vector<std::uint32_t>{0x268738} &&
                  addresses(over, K::arm_update, true) == std::vector<std::uint32_t>{update} &&
                  addresses(over, K::arm_render, true).empty(), "catchup has no render");
            check(position(over, K::catchup_test, 0x246cf4) < position(over, K::pad, 0x268738, true) &&
                  position(over, K::arm_update, update, true) < position(over, K::vsync, 0x122598) &&
                  position(over, K::vsync, 0x122598) < position(over, K::presented_frames_write, 0x246d58) &&
                  position(over, K::presented_frames_write, 0x246d58) < position(over, K::call, 0x285f18) &&
                  position(over, K::call, 0x285f18) < position(over, K::word_clear, 0x15f690),
                  "catchup, vsync, counter, 285f18 order");

            in.player_byte_20b1 = 1;
            const auto blocked = plan_rac_level_frame_v1({mode, 8, 4, 0}, in);
            check(!blocked.catchup_update && !blocked.presented &&
                  blocked.next.presented_frames == 4 &&
                  addresses(blocked, K::vsync).empty() && has(blocked, K::call, 0x1f88c0),
                  "nonzero +0x20b1 skips catchup and present");
        }
    }
    for (std::int32_t mode : {-1, 1, 3, 4, 5, 6, 7, 8}) {
        auto in = steady(mode);
        in.timer1_at_catchup = 0xffff;
        const auto plan = plan_rac_level_frame_v1({mode, 8, 0, 0}, in);
        check(!plan.catchup_update && !has(plan, K::catchup_test, 0x246cf4) &&
              plan.presented && has(plan, K::presented_frames_write, 0x246d7c),
              "no catchup outside {0,2}");
    }
    auto arm_changed = steady(0);
    arm_changed.mode_after_post_arm = 3;
    arm_changed.timer1_at_catchup = 0xffff;
    check(!plan_rac_level_frame_v1({0, 8, 0, 0}, arm_changed).catchup_update,
          "catchup uses mode read at 246cbc");

    auto scene = steady(2);
    scene.timer1_at_catchup = 11521;
    scene.scene_guard_16c9b0 = -1;
    check(!plan_rac_level_frame_v1({2, 0, 0, 0}, scene).catchup_update, "scene guard");

    auto paused = steady(0);
    paused.timer1_at_catchup = 20000;
    paused.pause_in_catchup = {0, 0x400, 0};
    const auto in_arm_count = plan_rac_level_frame_v1({0, 7, 0, 0}, paused);
    check(!in_arm_count.pause_gate_in_arm && in_arm_count.pause_gate_in_catchup &&
          has(in_arm_count, K::call, 0x2016e0, true),
          "catchup update sees incremented counter");
}

void frame_skip_overlay_reload_exit() {
    auto skip = steady(0);
    skip.frame_skip_15f6bc = 1;
    const auto skipped = plan_rac_level_frame_v1({0, 8, 3, 0}, skip);
    check(skipped.frame_skipped && !skipped.frame_time_ratio_bits_15f6b4 &&
          skipped.next.presented_frames == 3 && skipped.next.frames_in_mode == 9 &&
          addresses(skipped, K::vsync).empty() &&
          skipped.steps.back().kind == K::frame_skip_return, "frame skip");
    check(position(skipped, K::word_clear, 0x15f6bc) < position(skipped, K::call, 0x2a1b58),
          "skip flag cleared before 2a1b58");

    for (std::int32_t selector : {3, 6, 9, 12, 15, 20}) {
        auto in = steady(0);
        in.overlay_selector_13d46c = selector;
        check(plan_rac_level_frame_v1({0, 8, 0, 0}, in).overlay_drawn, "overlay selector");
        in.overlay_latch_1ba1ac = 1;
        check(!plan_rac_level_frame_v1({0, 8, 0, 0}, in).overlay_drawn, "overlay latch");
    }
    for (std::int32_t selector : {-1, 0, 2, 7, 8, 13, 14, 21}) {
        auto in = steady(0);
        in.overlay_selector_13d46c = selector;
        check(!plan_rac_level_frame_v1({0, 8, 0, 0}, in).overlay_drawn, "no overlay");
    }

    auto stub = steady(0);
    stub.scene_word_16c170 = 2;
    check(has(plan_rac_level_frame_v1({0, 8, 0, 0}, stub), K::empty_stub_call, 0x1f7110),
          "1f7110 stub in mode 0");

    auto reload = steady(0);
    reload.player_byte_20b1 = 1;
    reload.player_byte_20b1_at_reload = 1;
    const auto reloaded = plan_rac_level_frame_v1({0, 8, 0, 0}, reload);
    check(reloaded.reload && has(reloaded, K::reload_mode_write, 0x246e04) &&
          reloaded.next.mode == 0 && reloaded.steps.back().kind == K::time_record,
          "reload path");
    auto no_reload = reload;
    no_reload.mode_at_reload_check = 2;
    check(!plan_rac_level_frame_v1({0, 8, 0, 0}, no_reload).reload, "reload needs mode 0");

    auto exit = steady(0);
    exit.level_exit_15f650 = 1;
    const auto exited = plan_rac_level_frame_v1({0, 8, 3, 9}, exit);
    check(exited.level_exit && exited.steps.size() == 2 &&
          has(exited, K::level_exit_call, 0x2940e0) &&
          exited.next.presented_frames == 3 && exited.next.elapsed_timer1_ticks == 9,
          "level exit before TIMER1");
}

void ratio_and_time_record() {
    auto in = steady(0);
    in.video_selector_15ee80 = 0;
    in.timer1_at_ratio = 4800;
    check(plan_rac_level_frame_v1({0, 8, 0, 0}, in).frame_time_ratio_bits_15f6b4 ==
          0x3f000000U, "4800/9600");
    in.timer1_at_ratio = 7000;
    check(plan_rac_level_frame_v1({0, 8, 0, 0}, in).frame_time_ratio_bits_15f6b4 ==
          ee_cop1_div_bits_v1(std::bit_cast<std::uint32_t>(7000.0f), 0x46160000U).bits,
          "PS2 FDIV reference");
    in.timer1_at_ratio = 0;
    check(plan_rac_level_frame_v1({0, 8, 0, 0}, in).frame_time_ratio_bits_15f6b4 == 0U,
          "zero ratio");

    check(!rac_level_time_record_update_v1(10, 6599, 6599), "below next unit");
    check(rac_level_time_record_update_v1(10, 6600, 7200) == std::uint16_t{12},
          "second call value stored");
    check(!rac_level_time_record_update_v1(0, -600, 0), "signed quotient");
}

void adapter() {
    check(openrc_virtual_timer1_ticks_v1(20'000'000, kOpenrcVirtualTimer1HzV1) == 11'520,
          "20 ms at 576 kHz");
    check(openrc_virtual_timer1_ticks_v1(16'666'667, kOpenrcVirtualTimer1HzV1) == 9'600,
          "60 Hz field at 576 kHz");
    check(openrc_virtual_timer1_ticks_v1(UINT64_MAX, UINT32_MAX) == UINT64_MAX,
          "saturation");
    check(openrc_virtual_timer1_count_v1(65536 + 11520) == 11520, "16-bit register wrap");
    try {
        (void)openrc_virtual_timer1_ticks_v1(1, 0);
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("zero clock accepted");
}
} // namespace

int main() {
    try {
        prologue_and_mode_selection();
        dispatch();
        frames_in_mode_and_present_counter();
        pause_gate();
        catchup();
        frame_skip_overlay_reload_exit();
        ratio_and_time_record();
        adapter();
    } catch (const std::exception& error) {
        std::cerr << "rac_level_frame_schedule_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
