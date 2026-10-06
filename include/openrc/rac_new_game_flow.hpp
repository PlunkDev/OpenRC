#pragma once

#include "openrc/frontend_sequence.hpp"
#include "openrc/frontend_input.hpp"
#include "openrc/rac_frontend_input.hpp"

namespace openrc {

// Compiler ownership map only. Every reached source write byte must map to
// an existing canonical u8 view, in original write order. No RAM image is
// serialized and no source destination becomes a runtime key implicitly.
struct RacFrontendStateBindingV1 {
  std::uint32_t source_begin=0,byte_count=0;
  std::string view_key;
  std::uint64_t first_element=0;
};
[[nodiscard]] std::vector<game::SessionStateWriteV1> lower_rac_frontend_input_writes_v1(
    std::span<const RacFrontendInputWriteV1> writes,
    std::span<const RacFrontendStateBindingV1> bindings,
    const SessionStateSchemaV1 &schema,const SessionStateLimitsV1 &state_limits,
    std::uint64_t max_writes=1048576U);

struct RacFrontendNoSaveCompileBindingsV1 {
  std::uint32_t node_source_address=0;
  std::uint32_t screen_source_address=0;
  // Focus and previous-result are source pointer-following reads; their
  // existing owner exposes the actual live results as named neutral fields.
  FrontendInputFieldV1 focused;
  FrontendInputFieldV1 previous_result;
  // Host-issued handles corresponding to the two original previous screens.
  // Runtime previous/parent/current screens and sound objects use handles too.
  std::array<std::uint32_t,2> save_result_screen_tokens{};
};
[[nodiscard]] FrontendNoSavePlanV1 compile_rac_frontend_no_save_plan_v1(
    const RacFrontendResetTemplateV1 &reset_template,
    const RacFrontendNoSaveCompileBindingsV1 &input_bindings,
    std::span<const RacFrontendStateBindingV1> state_bindings,
    const SessionStateSchemaV1 &schema,const SessionStateLimitsV1 &state_limits,
    FrontendInputLimitsV1 limits={});

struct RacNewGamePresentationInputsV1 {
  std::uint32_t language_15ee88=0;
  std::uint32_t video_selector_15ee80=1;
  std::uint32_t time_scale_bits=0x3f555555U;
  std::int32_t movie_mode=0;
  std::uint32_t updates_per_second=50;
};
struct RacNewGameFlowInputsV1 : RacNewGamePresentationInputsV1 {
  RacFrontendNoSaveInputsV1 no_save;
};
struct RacNewGameFlowResourcesV1 {
  std::array<FrontendSequenceResourceV1,3> loading_cards;
  std::array<FrontendSequenceResourceV1,3> movies;
  // Initial, post-loading and post-movie feedback sequences. All absent is
  // accepted for the earlier compiler fixture/profile; partial binding fails.
  std::array<FrontendSequenceResourceV1,3> fades;
};
struct RacNewGameFlowSourceCueV1 {
  std::uint32_t neutral_cue=0,source_call_pc=0,source_callee=0;
};
struct RacNewGamePresentationCallV1 {
  std::uint32_t source_pc=0,source_callee=0,argument_count=0;
  std::array<std::uint32_t,5> arguments{};
  std::uint32_t current_level_word=UINT32_MAX;
};
struct RacNewGameFlowV1 {
  RacFrontendInputResultV1 input_effects;
  // Absent when the actual input callback did not request New Game. Its
  // effects still require lowering; no presentation sequence is invented.
  std::optional<FrontendSequenceV1> sequence;
  std::vector<game::SessionStateWriteV1> input_state_writes;
  std::uint32_t loading_language_slot=0,audio_channel=0;
  std::array<std::uint32_t,3> movie_toc_offsets{};
  std::vector<RacNewGameFlowSourceCueV1> source_cues;
  // Seven direct fresh-branch presentation calls, for independent original
  // instruction comparison and binding the actual prepared card pairs.
  std::vector<RacNewGamePresentationCallV1> presentation_calls;
};

// Reaches the proven fresh target0 / reset-byte13de60==0 branch of233308
// by executing224728 and the complete validated reset template. Caller owns
// native consumers listed by the resulting neutral program; their absent
// implementations remain incomplete at playback. In particular opaque
// cleanup/admission calls here are required barriers, never no-op closures.
[[nodiscard]] RacNewGameFlowV1 compile_rac_new_game_flow_v1(
    const RacNewGameFlowInputsV1 &input,
    const RacFrontendResetTemplateV1 *reset_template,
    const RacNewGameFlowResourcesV1 &resources,
    std::span<const RacFrontendStateBindingV1> bindings,
    const SessionStateSchemaV1 &schema,const SessionStateLimitsV1 &state_limits,
    FrontendSequenceLimitsV1 limits={});

// Static presentation continuation contains no input/reset cue or guessed
// callback snapshot. The host starts it only after the live neutral no-save
// adapter has committed and returned requested_new_game. Its state writes
// therefore never replay a stale copy of the user's preserved settings.
[[nodiscard]] RacNewGameFlowV1 compile_rac_new_game_continuation_v1(
    const RacNewGamePresentationInputsV1 &input,
    const RacNewGameFlowResourcesV1 &resources,
    std::span<const RacFrontendStateBindingV1> bindings,
    const SessionStateSchemaV1 &schema,const SessionStateLimitsV1 &state_limits,
    FrontendSequenceLimitsV1 limits={});

} // namespace openrc
