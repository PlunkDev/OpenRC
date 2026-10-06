#pragma once

#include "openrc/session_state.hpp"

#include <array>

namespace openrc {

[[nodiscard]] constexpr SessionStateLimitsV1 frontend_session_state_limits_v1() {
  return {384U,512U,128U,131072U,65536U,1048576U,65536U,2097152U,1048576U};
}

// All fields refer to canonical stride-one u8 views. Screen and sound object
// values are host-issued neutral tokens, never executable addresses.
struct FrontendInputFieldV1 {
  std::string view_key;
  std::uint64_t first_element = 0;
  bool operator==(const FrontendInputFieldV1 &) const = default;
};
enum class FrontendNoSaveFieldV1 : std::uint32_t {
  focused, node_flags, node_phase, node_selection, sound_object,
  previous_screen, previous_result, parent_screen, cancel_guard, pending_save,
  readiness, card_mode, card_status, card_result, card_type, global_pressed,
  pressed, repeated, saved_selection, flags, preserved_a, preserved_b,
  preserved_c, current_screen, save_requested, current_level, target_level,
  level_change_requested, transition_requested, entry_requested, count
};
inline constexpr std::size_t kFrontendNoSaveFieldCountV1 =
    static_cast<std::size_t>(FrontendNoSaveFieldV1::count);
struct FrontendNoSavePlanV1 {
  PreparedContentDigestV1 state_schema_sha256{};
  std::array<FrontendInputFieldV1, kFrontendNoSaveFieldCountV1> fields;
  // The two previous screens whose successful result requests card save.
  std::array<std::uint32_t, 2> save_result_screen_tokens{};
  // Complete compiler-lowered fresh reset, in original copy order. Preserved
  // values are read from the live state before these writes and restored after.
  std::vector<game::SessionStateWriteV1> reset_writes;
};
struct FrontendInputLimitsV1 {
  std::uint64_t max_bytes = 16U * 1024U * 1024U;
  std::uint32_t max_key_bytes = 256U;
  std::uint64_t max_reset_writes = 1048448U;
};
class FrontendInputError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
enum class FrontendNoSaveUnsupportedV1 {
  none, card_save_or_completion, slot_confirm, preserving_reset
};
struct FrontendNoSaveResultV1 {
  std::int32_t return_word = 0;
  bool requested_new_game = false;
  bool selection_sound_requested = false;
  std::uint32_t sound_object_token = 0;
  FrontendNoSaveUnsupportedV1 unsupported = FrontendNoSaveUnsupportedV1::none;
  std::uint64_t committed_revision = 0;
};
struct FrontendNoSaveEvaluationV1 {
  FrontendNoSaveResultV1 result;
  std::uint64_t expected_revision = 0;
  std::vector<game::SessionStateWriteV1> writes;
};

void validate_frontend_no_save_plan_v1(const FrontendNoSavePlanV1 &,
    const game::SessionStateV1 &, FrontendInputLimitsV1 = {});
[[nodiscard]] std::vector<std::byte> encode_frontend_no_save_plan_v1(
    const FrontendNoSavePlanV1 &, FrontendInputLimitsV1 = {});
[[nodiscard]] FrontendNoSavePlanV1 decode_frontend_no_save_plan_v1(
    std::span<const std::byte>, FrontendInputLimitsV1 = {});
// Existing session owners with const state access can evaluate here, then
// commit through their own apply_persistent_state_writes with this exact
// expected revision. A request/sound becomes actionable only after that
// commit succeeds. Unsupported results contain no writes.
[[nodiscard]] FrontendNoSaveEvaluationV1 evaluate_frontend_no_save_v1(
    const FrontendNoSavePlanV1 &, const game::SessionStateV1 &,
    std::uint64_t expected_revision, FrontendInputLimitsV1 = {});
// Reads this invocation's values directly from shared state; no initial-state
// replay or private mutable mirror. All supported effects commit as one ordered
// batch. Unsupported branches report explicitly and leave state unchanged.
[[nodiscard]] FrontendNoSaveResultV1 execute_frontend_no_save_v1(
    const FrontendNoSavePlanV1 &, game::SessionStateV1 &,
    std::uint64_t expected_revision, FrontendInputLimitsV1 = {});

} // namespace openrc
