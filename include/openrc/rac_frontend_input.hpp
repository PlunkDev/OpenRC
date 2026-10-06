#pragma once

#include "openrc/ps2_save_bundle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Compiler-side source effects. Source addresses are provenance tokens, never
// host pointers or a runtime RAM image. Payloads must be lowered to neutral
// state ownership before publication.
struct RacFrontendInputWriteV1 {
  std::uint32_t source_pc = 0U;
  std::uint32_t source_address = 0U;
  std::vector<std::byte> bytes;
  bool operator==(const RacFrontendInputWriteV1 &) const = default;
};

struct RacFrontendInputSoundV1 {
  std::uint32_t source_pc = 0U;
  std::array<std::uint32_t, 3U> arguments{};
  bool operator==(const RacFrontendInputSoundV1 &) const = default;
};

struct RacFrontendInputResultV1 {
  std::int32_t return_word = 0;
  std::vector<RacFrontendInputWriteV1> writes;
  // Sound submission is an output request; this module does not play audio.
  std::vector<RacFrontendInputSoundV1> sounds;
  bool entered_dialog = false;
  bool requested_new_game = false;
  bool requested_menu = false;
};

// 1eb600/1eb6f0 input gate after the title scene update, followed by the
// complete integer callee 219e60. Requests the original menu initializer
// (root state 45); does not manufacture its 14 animated objects or draw state.
[[nodiscard]] RacFrontendInputResultV1 execute_rac_frontend_title_input_v1(
    std::uint32_t mode_15f6e8, std::uint32_t pressed_13cbe4);

struct RacFrontendAction4InputsV1 {
  // Values at 21bca0, after the list callback's independent row timers.
  bool focused = false;
  std::uint32_t global_pressed = 0U; // pad 13ca40+1c4
  std::uint32_t flags_15efb4 = 0U;
  std::uint32_t parent_screen = 0U; // current screen+38
  std::uint32_t root_124 = 0U;
  std::uint32_t node_flags = 4U;
  std::uint32_t sound_object = 0U; // node+14
  std::uint32_t target_screen = 0U; // selected row+4
  std::uint32_t mode_15f6e8 = 3U;
  std::uint32_t time_scale_bits = 0x3f800000U;
};

// The action-4 entry used by row 1d49d0 (key 20266, target 1d5008).
// Handles focus/cancel/confirm gates and executes dialog mode 3 initialization
// 1fbc80, including its exact timers. This entry is for node flags 4, mode 3;
// other node actions and the later navigation tail are separate source owners.
[[nodiscard]] RacFrontendInputResultV1 execute_rac_frontend_action4_v1(
    const RacFrontendAction4InputsV1 &input);

struct RacFrontendDialog3ReadyInputsV1 {
  std::uint32_t duration = 0U; // dialog 193400+04
  std::uint32_t draw_delay = 0U; // +2c
  std::uint32_t age = 0U; // +20
  std::uint32_t fade = 0U; // +24
  std::uint32_t card_b0 = 0U; // 13d390+b0
  std::uint32_t ui_mode_15efb0 = 0U;
  std::uint32_t target_screen = 0U; // dialog+18
  std::uint32_t previous_mode = 3U; // dialog+14
  std::uint32_t time_scale_bits = 0x3f800000U;
};

// Dialog kind 3's update from 1fd400, after audio update 22dd68. The ready
// source states 1/16 hand its target back to 21a1a0 immediately, even with a
// nonzero fade counter; they do not bypass that owner's 12-frame transition.
// Busy card_b0==1 is also complete. Other reached card-state arms are rejected.
[[nodiscard]] RacFrontendInputResultV1 execute_rac_frontend_dialog3_ready_v1(
    const RacFrontendDialog3ReadyInputsV1 &input);

struct RacFrontendResetTemplateV1 {
  // Descriptor-copy effects of 209ce8. Includes every primary/repeated tag,
  // not just the admission subset exposed by RacInitialProgressTemplateV1.
  std::vector<RacFrontendInputWriteV1> copies;
};

// Exact successful-template domain of 209ce8/20bd70: validates the original
// 47/11 tag layout, all record checksums and descriptor sizes. Descriptors are
// the owned 768/192 bytes at boot 1a05c0/1a08c0, including terminators.
// Descriptor status/changed-byte diagnostic counters are compiler bookkeeping
// and are outside the destination-state contract. No missing-tag defaults.
[[nodiscard]] RacFrontendResetTemplateV1 compile_rac_frontend_reset_template_v1(
    std::span<const std::byte> ps2d_envelope,
    std::span<const std::byte> primary_descriptors,
    std::span<const std::byte> repeated_descriptors,
    Ps2SaveBundleLimits limits);

struct RacFrontendNoSaveInputsV1 {
  bool focused = false;
  std::uint32_t node_address = 0U;
  std::uint32_t node_flags = 0U;
  std::uint32_t node_phase = 0U; // node+4c
  std::uint32_t node_selection = 0U; // node+40
  std::uint32_t sound_object = 0U;
  std::uint32_t previous_screen = 0U;
  std::uint32_t previous_result = 0U; // previous screen+84
  std::uint32_t parent_screen = 0U;
  std::uint32_t root_124 = 0U;
  std::uint32_t root_128 = 0U;
  std::uint32_t root_154 = 0U;
  std::uint32_t ui_mode_15efb0 = 0U;
  std::uint32_t card_dc = 0U;
  std::uint32_t card_e4 = 0U;
  std::uint32_t card_type = 0U;
  std::uint32_t global_pressed = 0U;
  std::uint32_t pressed = 0U; // pad+1a4
  std::uint32_t repeated = 0U; // pad+1b4, selected by node flag 1
  std::uint32_t saved_selection_15ef34 = 0U;
  std::uint32_t flags_15efb4 = 0U;
  // Actual pre-reset reads, in source save/restore order.
  std::array<std::uint32_t, 3U> preserved_eef0_eeec_eee8{};
};

// Executes 224728's input arm and no-save fresh reset 224bd0..224bf8,
// then 22f4a0(0) and the 13e15a halfword store. Reached card-save/overwrite,
// pending-save completion and preserving-reset arms throw: they are not
// silently converted into starting a fresh game. Reset bytes are required
// only when the no-save action actually reaches 209dc0.
[[nodiscard]] RacFrontendInputResultV1 execute_rac_frontend_no_save_v1(
    const RacFrontendNoSaveInputsV1 &input,
    const RacFrontendResetTemplateV1 *reset_template = nullptr);

class RacFrontendInputError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

} // namespace openrc
