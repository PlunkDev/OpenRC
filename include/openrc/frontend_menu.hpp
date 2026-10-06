#pragma once
#include "openrc/frontend_input.hpp"
#include "openrc/screen_overlay.hpp"
#include <optional>

namespace openrc {
enum class FrontendMenuOperationV1 { bootstrap, title_input, action4_input, dialog3_update, menu_counters };
struct FrontendMenuEvaluationV1 {
  std::uint64_t expected_revision=0;
  std::vector<game::SessionStateWriteV1> writes;
  std::int32_t return_word=0;
  bool requested_menu=false,entered_dialog=false,requested_new_game=false;
  bool sound_requested=false;
  bool awaiting_resources=false,began_main_entry=false,main_entry_complete=false;
  bool frontend_exit_reached=false;
  // Present only when exit restores a different admitted display profile.
  // The host performs the real display work before acknowledging that barrier.
  std::optional<std::uint32_t> restore_display_selector;
  std::uint32_t sound_variant=0,sound_object_token=0;
  // An unsupported branch never returns a partial write batch.
  bool unsupported=false;
};
// Native callbacks on the declared neutral frontend session view names.
// Prepared timer values live in named state fields; no source arithmetic,
// executable addresses, pointer following or disc reads occur here.
[[nodiscard]] FrontendMenuEvaluationV1 evaluate_frontend_menu_v1(
    FrontendMenuOperationV1,const FrontendNoSavePlanV1 &reset_plan,
    const game::SessionStateV1 &,std::uint64_t expected_revision);

// Explicit newer-profile startup; the original bootstrap above stays usable
// with the earlier frontend schema. Call once when the actual display/audio
// owners start. Reads the compiler-qualified display selector and preserved
// volume configuration. Reverb fields retain their existing live values.
[[nodiscard]] FrontendMenuEvaluationV1 evaluate_frontend_platform_bootstrap_v1(
    const game::SessionStateV1 &,std::uint64_t expected_revision);
// The committed transition request is the original frontend return gate.
// Allocation retirement and any returned display restoration are host work;
// this state evaluator does not acknowledge or emulate those consumers.
[[nodiscard]] FrontendMenuEvaluationV1 evaluate_frontend_exit_v1(
    const game::SessionStateV1 &,std::uint64_t expected_revision);
// Call at transition/prepare after exit and sequence entry writes. This is
// exactly the prefix before the first external presentation/audio call. It
// sets collision flags, frontend mode and the progress-selected loading card.
// The caller must still execute the rest of the prepare barrier's work.
[[nodiscard]] FrontendMenuEvaluationV1 evaluate_frontend_transition_prefix_v1(
    const game::SessionStateV1 &,std::uint64_t expected_revision);

// Uses the admitted idle title's prepared60-phase pulse and192 alpha slices.
// Mutable counter/logo/prompt values stay in the same canonical SessionState.
// Call before title_input in mode0 and before main-entry work in mode3.
[[nodiscard]] FrontendMenuEvaluationV1 evaluate_frontend_title_v1(
    const ScreenOverlayV1 &prepared_idle_title,const game::SessionStateV1 &,
    std::uint64_t expected_revision);
// Draw these after the scene/main layers even in mode4, where title state is
// intentionally not advanced. Image IDs address the original title library.
[[nodiscard]] ScreenOverlayFrameV1 frontend_title_presentation_v1(const game::SessionStateV1 &);

enum class FrontendCardCommandV1 { none, poll, request_status, unsupported };
enum class FrontendCardPollStatusV1 { no_request, pending, completed_absent };
struct FrontendCardSignalV1 {
  // Supplied only after the indicated host operation actually executes.
  std::optional<FrontendCardPollStatusV1> poll;
  std::optional<bool> request_accepted;
  std::uint32_t completed_command_token=0;
  // A completed query retains its actual negative result. In particular,
  // changed-card(-1) and a failed device detection are different outcomes.
  std::optional<std::int32_t> completed_result;
};
[[nodiscard]] FrontendCardCommandV1 frontend_card_command_v1(const game::SessionStateV1 &);
// Bounded absent-card owner: source polling phases0/1/2 and UI modes0/3/4.
// Completed_absent is a backend result, never synthesized from elapsed time.
// Unsupported inserted-card/save arms are explicit and leave state unchanged.
[[nodiscard]] FrontendMenuEvaluationV1 evaluate_frontend_absent_card_v1(
    const game::SessionStateV1 &,std::uint64_t expected_revision,const FrontendCardSignalV1 &);

enum class FrontendDialogMessageV1 {
  empty, absent_fresh, absent_existing, absent_without_game, unavailable
};
enum class FrontendDialogPromptV1 { none, continue_without_save, continue_existing_game, cancel, back };
struct FrontendDialogPresentationV1 {
  // Ordered layers: backdrop, panel/body, then left/right/center prompts.
  bool backdrop=false,panel=false,unsupported=false;
  std::uint32_t backdrop_coverage=0,coverage_denominator=128;
  FrontendDialogMessageV1 message=FrontendDialogMessageV1::empty;
  FrontendDialogPromptV1 left=FrontendDialogPromptV1::none;
  FrontendDialogPromptV1 right=FrontendDialogPromptV1::none;
  FrontendDialogPromptV1 center=FrontendDialogPromptV1::none;
  // Index the compiler-prepared samples by remaining counter. These are
  // separate clocks: body uses duration, prompts use fade. Runtime must not
  // replace the prepared samples with host floating-point interpolation.
  std::uint32_t body_remaining=0,prompt_remaining=0,prepared_duration=0;
};
// Read-only absent-card dialog selection. Background coverage is emitted even
// while delayed/busy; the panel additionally requires signed age > 0.
// A non-dialog frontend mode yields no dialog layers. Unsupported kinds/card
// arms are explicit and never substitute another message.
[[nodiscard]] FrontendDialogPresentationV1 evaluate_frontend_dialog_presentation_v1(
    const game::SessionStateV1 &);

struct FrontendMainResourcesReadyV1 {
  // Supplied by the host only after the actual prepared actor library,
  // animation, timeline, camera and submission resources are established.
  // Tokens are nonzero stable handles for the14 admitted neutral instances.
  std::array<std::uint32_t,14> actor_tokens{};
};
// One menu transition stage per source update. The first call waits for real
// resource admission; then returns sample0 with a12-update gate. The twelfth
// subsequent call admits the main action node (sample12), never earlier.
// Mode3 is required. Other screens/noninitial re-entry are explicitly outside
// this bounded owner. Returned writes include derived focus on the same state.
[[nodiscard]] FrontendMenuEvaluationV1 evaluate_frontend_main_entry_v1(
    const game::SessionStateV1 &,std::uint64_t expected_revision,
    const std::optional<FrontendMainResourcesReadyV1> &ready=std::nullopt);
} // namespace openrc
