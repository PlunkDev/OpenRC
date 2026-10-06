#pragma once

#include "openrc/session_state.hpp"
#include <optional>

namespace openrc {
namespace game {class GameSessionV1;}

// Neutral prepared presentation/control sequence. No executable addresses,
// disc records, source pointers, palette formats or game-image readers.
struct FrontendSequenceResourceV1 {
  std::string resource_id;
  std::string resource_type;
  PreparedContentDigestV1 payload_sha256{};
  bool operator==(const FrontendSequenceResourceV1 &) const = default;
};
enum class FrontendSequenceCueKindV1 : std::uint32_t {
  consumer=0, session_writes=1, current_level=2, start_level_load=3,
  fade=4, loading_overlay=5, media=6, await_level_load=7
};
struct FrontendSequenceSkipV1 {
  std::uint32_t pressed_any_mask=0;
  std::uint64_t held_all_mask=0;
  bool operator==(const FrontendSequenceSkipV1 &) const = default;
};
struct FrontendSequenceCueV1 {
  FrontendSequenceCueKindV1 kind=FrontendSequenceCueKindV1::consumer;
  // Consumer keys name required native implementations. Missing consumers
  // stop the player as incomplete; acknowledging them is never permitted.
  std::string consumer_key;
  std::uint32_t resource_index=UINT32_MAX;
  std::uint32_t updates=0;
  std::uint32_t pending_load_extension=0;
  std::uint32_t level_id=UINT32_MAX;
  FrontendSequenceSkipV1 skip;
  std::vector<game::SessionStateWriteV1> writes;
};
struct FrontendSequenceV1 {
  std::uint32_t updates_per_second=0;
  std::uint32_t max_loading_updates=1048576U;
  PreparedContentDigestV1 state_schema_sha256{};
  std::vector<FrontendSequenceResourceV1> resources;
  std::vector<FrontendSequenceCueV1> cues;
};
struct FrontendSequenceLimitsV1 {
  std::uint64_t max_bytes=16U*1024U*1024U;
  std::uint32_t max_resources=64U, max_cues=256U, max_string_bytes=256U;
  std::uint64_t max_writes=1048576U;
};
class FrontendSequenceError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
void validate_frontend_sequence_v1(const FrontendSequenceV1 &,FrontendSequenceLimitsV1={});
[[nodiscard]] std::vector<std::byte> encode_frontend_sequence_v1(
    const FrontendSequenceV1 &,FrontendSequenceLimitsV1={});
[[nodiscard]] FrontendSequenceV1 decode_frontend_sequence_v1(
    std::span<const std::byte>,FrontendSequenceLimitsV1={});

enum class FrontendSequencePhaseV1 {
  consumer, loading_gate, loading_frame, fade_frame, media_start,
  media_feed, media_drain, media_present_wait, await_level_load, complete, incomplete
};
struct FrontendSequenceCommandV1 {
  FrontendSequencePhaseV1 phase=FrontendSequencePhaseV1::incomplete;
  std::uint32_t cue_index=0, frame_index=0, duration=0;
  bool poll_level_load=false;
};
// The signals report completed host work, not a request to assume it. A
// frame must actually be presented; decoder drain and final presentation
// completion are separate. Input availability is the prepared media reader's
// end-of-input gate, independent of timestamps and audio presentation drain.
struct FrontendSequenceSignalV1 {
  bool consumer_completed=false;
  bool frame_presented=false;
  bool loading_presentation_permitted=false;
  std::optional<bool> level_load_completed;
  bool media_started=false;
  bool media_input_available=true;
  bool media_decoder_stopped=false;
  bool media_decoder_drained=false;
  bool media_presentation_drained=false;
  std::uint32_t pressed_word=0;
  std::uint64_t held_word=0;
};

class FrontendSequencePlayerV1 final {
public:
  // The resource catalog is independently decoded/hashed content admitted
  // by the host. Every referenced ID/type/hash must match before any action.
  // Consumer keys must correspond to real registered native implementations.
  FrontendSequencePlayerV1(FrontendSequenceV1 program,
      std::span<const FrontendSequenceResourceV1> admitted_resources,
      std::span<const std::string> registered_consumers,
      FrontendSequenceLimitsV1 limits={});
  [[nodiscard]] const FrontendSequenceV1 &program() const noexcept;
  [[nodiscard]] FrontendSequenceCommandV1 command() const noexcept;
  [[nodiscard]] std::vector<std::string> missing_consumers() const;
  // session_writes executes through this existing canonical shared state;
  // no private state-byte mirror is kept by the sequence player.
  void apply_session_writes(game::SessionStateV1 &state);
  void apply_session_writes(game::GameSessionV1 &session);
  void advance(const FrontendSequenceSignalV1 &signal);
private:
  void enter();
  void next();
  FrontendSequenceV1 program_;
  std::vector<std::string> consumers_;
  FrontendSequenceCommandV1 command_;
  bool load_pending_=false;
};

} // namespace openrc
