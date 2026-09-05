#pragma once

#include "openrc/actor_behavior_scene.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace openrc::game {

struct ActorBehaviorImplementationIdentityV1 {
  std::string implementation_key;
  std::uint32_t implementation_abi_version = 0U;
  PreparedContentDigestV1 state_layout_sha256{};

  [[nodiscard]] bool
  operator==(const ActorBehaviorImplementationIdentityV1 &) const = default;
};

struct ActorBehaviorSetAnimationCommandV1 {
  std::uint32_t channel_id = 0U;
  std::uint32_t animation_import_id = 0U;
  std::string clip_key;
  PreparedContentDigestV1 required_clip_sha256{};
  std::uint32_t first_frame_index = 0U;
  // This value is already expressed in source animation updates. It is not a
  // duration in the host runtime's separately declared fixed-tick domain.
  std::uint32_t transition_source_update_count = 0U;

  [[nodiscard]] bool
  operator==(const ActorBehaviorSetAnimationCommandV1 &) const = default;
};

struct ActorBehaviorSetPresentationEnabledCommandV1 {
  bool presentation_enabled = false;

  [[nodiscard]] bool operator==(
      const ActorBehaviorSetPresentationEnabledCommandV1 &) const = default;
};

using ActorBehaviorCommandPayloadV1 =
    std::variant<ActorBehaviorSetAnimationCommandV1,
                 ActorBehaviorSetPresentationEnabledCommandV1>;

// Entries are emitted in caller-supplied actor invocation order and receive
// dense ordinals. Applying them to the world is deliberately a separate
// boundary: a failed behavior tick never partially mutates renderer or gameplay
// state.
struct ActorBehaviorJournalEntryV1 {
  std::uint32_t ordinal = 0U;
  std::uint32_t authored_id = 0U;
  ActorBehaviorCommandPayloadV1 payload;

  [[nodiscard]] bool
  operator==(const ActorBehaviorJournalEntryV1 &) const = default;
};

struct ActorBehaviorFixedTickV1 {
  std::uint64_t tick_index = 0U;
  std::vector<ActorBehaviorJournalEntryV1> journal;

  [[nodiscard]] bool
  operator==(const ActorBehaviorFixedTickV1 &) const = default;
};

class ActorBehaviorInvocationV1 final {
public:
  [[nodiscard]] std::uint64_t tick_index() const noexcept;
  [[nodiscard]] std::uint32_t authored_id() const noexcept;
  [[nodiscard]] std::uint32_t current_state_id() const noexcept;
  [[nodiscard]] std::uint64_t state_ticks() const noexcept;
  [[nodiscard]] bool presentation_enabled() const noexcept;

  [[nodiscard]] const ActorBehaviorInitialValueV1 &
  field(std::uint32_t field_id, std::uint32_t element_index = 0U) const;
  void set_field(std::uint32_t field_id, std::uint32_t element_index,
                 ActorBehaviorInitialValueV1 value);

  void transition_to_state(std::uint32_t state_id);
  [[nodiscard]] std::uint32_t next_random_u32(std::uint32_t random_import_id);

  void emit_set_animation(std::uint32_t channel_id,
                          std::uint32_t animation_import_id,
                          std::uint32_t first_frame_index,
                          std::uint32_t transition_source_update_count);
  // Presentation visibility is independent of scheduling. V1 invokes each
  // scheduled instance at its declared source cadence. The caller supplies
  // source-recovered ordering and sleep/update-distance policy.
  void emit_set_presentation_enabled(bool presentation_enabled);

private:
  struct BackingV1;

  explicit ActorBehaviorInvocationV1(BackingV1 &backing) noexcept;

  BackingV1 *backing_ = nullptr;

  friend class ActorBehaviorRuntimeV1;
};

using ActorBehaviorFixedTickCallbackV1 =
    std::function<void(ActorBehaviorInvocationV1 &)>;
using ActorBehaviorRandomStepCallbackV1 =
    std::function<std::uint32_t(std::span<std::uint32_t>)>;

struct ActorBehaviorImplementationV1 {
  ActorBehaviorImplementationIdentityV1 identity;
  // A callback is trusted native code, but its deterministic contract permits
  // mutation only through the ephemeral invocation. Captured/global writes
  // cannot participate in rollback and are therefore forbidden.
  ActorBehaviorFixedTickCallbackV1 fixed_tick;
};

struct ActorBehaviorRandomAlgorithmImplementationV1 {
  std::string algorithm_key;
  std::uint32_t algorithm_abi_version = 0U;
  std::uint32_t state_word_count = 0U;
  // The callback may mutate only the supplied staged word span. It must not
  // retain that span or use hidden/global random state.
  ActorBehaviorRandomStepCallbackV1 next_u32;
};

class ActorBehaviorRuntimeError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Trusted native implementations are registered independently of content.
// Behavior lookup uses the complete key + ABI + layout-digest tuple. Random
// lookup uses the complete algorithm key + ABI tuple and then verifies the
// declared state-word count. Neither lookup parses a semantic key.
class ActorBehaviorRuntimeRegistryV1 final {
public:
  void register_implementation(ActorBehaviorImplementationV1 implementation);
  void register_random_algorithm(
      ActorBehaviorRandomAlgorithmImplementationV1 implementation);

  [[nodiscard]] const ActorBehaviorImplementationV1 *find_implementation(
      const ActorBehaviorImplementationIdentityV1 &identity) const noexcept;
  [[nodiscard]] const ActorBehaviorRandomAlgorithmImplementationV1 *
  find_random_algorithm(std::string_view algorithm_key,
                        std::uint32_t algorithm_abi_version) const noexcept;

private:
  std::vector<ActorBehaviorImplementationV1> implementations_;
  std::vector<ActorBehaviorRandomAlgorithmImplementationV1> random_algorithms_;
};

struct ActorBehaviorSharedRandomSnapshotV1 {
  std::uint32_t stream_id = 0U;
  std::string stream_key;
  std::string algorithm_key;
  std::uint32_t algorithm_abi_version = 0U;
  ActorBehaviorRandomScopeV1 scope = ActorBehaviorRandomScopeV1::level_shared;
  std::vector<std::uint32_t> state_words;
  std::uint64_t call_count = 0U;

  [[nodiscard]] bool
  operator==(const ActorBehaviorSharedRandomSnapshotV1 &) const = default;
};

struct ActorBehaviorInstanceSnapshotV1 {
  std::uint32_t authored_id = 0U;
  std::uint32_t program_id = 0U;
  ActorBehaviorImplementationIdentityV1 implementation;
  std::uint32_t current_state_id = 0U;
  std::uint64_t state_ticks = 0U;
  std::uint32_t source_update_accumulator = 0U;
  std::uint64_t random_call_count = 0U;
  bool presentation_enabled = true;
  // Field-major, then element-major, exactly like prepared initial state.
  std::vector<ActorBehaviorInitialValueV1> field_values;
  // Random-import ID order, including only instance-scope imports.
  std::vector<std::uint32_t> instance_random_words;

  [[nodiscard]] bool
  operator==(const ActorBehaviorInstanceSnapshotV1 &) const = default;
};

struct ActorBehaviorRuntimeSnapshotV1 {
  LevelIdV1 level_id = 0U;
  std::uint32_t runtime_ticks_per_second = 0U;
  std::uint64_t next_tick_index = 0U;
  // Dense stream-ID order. V1 runtime accepts level-shared state only; a
  // session owner must be designed before session-shared streams can load.
  std::vector<ActorBehaviorSharedRandomSnapshotV1> shared_random_streams;
  // Strictly ascending authored-ID order.
  std::vector<ActorBehaviorInstanceSnapshotV1> instances;

  [[nodiscard]] bool
  operator==(const ActorBehaviorRuntimeSnapshotV1 &) const = default;
};

struct ActorBehaviorRuntimeLimitsV1 {
  ActorBehaviorSceneLimitsV1 scene;
  std::uint32_t max_journal_entries_per_tick = 0U;

  [[nodiscard]] bool
  operator==(const ActorBehaviorRuntimeLimitsV1 &) const = default;
};

struct ActorBehaviorInitialPresentationEnabledV1 {
  std::uint32_t authored_id = 0U;
  bool presentation_enabled = true;

  [[nodiscard]] bool
  operator==(const ActorBehaviorInitialPresentationEnabledV1 &) const = default;
};

// Immutable value-only view of the mounted entity relationships. Flags use
// kActorBehaviorEntityReferenceRequires{Transform,Actor,Behavior}V1 bits.
// Every non-null reference must resolve even if its field requires no bits.
struct ActorBehaviorEntityCapabilitiesV1 {
  std::uint32_t authored_id = 0U;
  std::uint32_t flags = 0U;

  [[nodiscard]] bool
  operator==(const ActorBehaviorEntityCapabilitiesV1 &) const = default;
};

// FNV-1a over a tagged, little-endian, allocation-independent snapshot
// encoding. Floating signed zero is canonicalized; non-finite values fail.
[[nodiscard]] std::uint64_t hash_actor_behavior_runtime_snapshot_v1(
    const ActorBehaviorRuntimeSnapshotV1 &snapshot);

class ActorBehaviorRuntimeV1 final {
public:
  // This is a neutral execution substrate, not an AI implementation. Source-
  // exact behavior enters only through a separately registered, audited
  // implementation whose identity matches the prepared program contract.
  ActorBehaviorRuntimeV1(
      ActorBehaviorSceneV1 scene,
      const ActorBehaviorRuntimeRegistryV1 &registry,
      ActorBehaviorRuntimeLimitsV1 limits,
      std::uint32_t runtime_ticks_per_second,
      std::uint64_t first_tick_index = 0U,
      std::span<const ActorBehaviorInitialPresentationEnabledV1>
          initial_presentation_enabled = {},
      std::span<const ActorBehaviorEntityCapabilitiesV1> entity_capabilities =
          {});
  ActorBehaviorRuntimeV1(ActorBehaviorSceneV1 scene,
                         const ActorBehaviorRuntimeRegistryV1 &registry,
                         ActorBehaviorRuntimeLimitsV1 limits,
                         std::uint32_t runtime_ticks_per_second,
                         const ActorBehaviorRuntimeSnapshotV1 &snapshot,
                         std::span<const ActorBehaviorEntityCapabilitiesV1>
                             entity_capabilities = {});

  // All instance fields, per-instance RNG words, shared RNG words, state
  // transitions, tick index, and commands are staged. Any validation,
  // implementation, random-algorithm, or allocation failure leaves the
  // committed snapshot byte-for-byte unchanged.
  // The schedule contains unique mounted behavior IDs in the exact source
  // invocation order. An empty schedule invokes no actors. Unscheduled actors
  // still advance cadence phase, but not state ticks or random streams.
  [[nodiscard]] ActorBehaviorFixedTickV1
  fixed_tick(std::uint64_t tick_index,
             std::span<const std::uint32_t> scheduled_authored_ids);

  // Fresh-load-only presentation commands in authored-ID order, with
  // presentation visibility first and initial animations in channel order.
  // A snapshot-restored runtime returns an empty journal: its presentation
  // consumer owns playback restoration and must not restart source clips.
  // Reading this immutable journal never runs behavior or advances the tick.
  [[nodiscard]] const std::vector<ActorBehaviorJournalEntryV1> &
  initialization_journal() const noexcept;
  [[nodiscard]] ActorBehaviorRuntimeSnapshotV1 snapshot() const;
  [[nodiscard]] std::uint64_t deterministic_hash() const;
  [[nodiscard]] std::uint64_t next_tick_index() const noexcept;
  [[nodiscard]] const ActorBehaviorSceneV1 &scene() const noexcept;

private:
  struct ProgramBindingV1 {
    ActorBehaviorImplementationV1 implementation;
    std::vector<ActorBehaviorRandomAlgorithmImplementationV1> random_algorithms;
    std::vector<std::size_t> field_offsets;
    std::vector<std::size_t> instance_random_offsets;
    std::vector<std::size_t> shared_random_indices;
  };

  struct InstanceStateV1 {
    std::uint32_t authored_id = 0U;
    std::uint32_t program_id = 0U;
    std::uint32_t current_state_id = 0U;
    std::uint64_t state_ticks = 0U;
    std::uint32_t source_update_accumulator = 0U;
    std::uint64_t random_call_count = 0U;
    bool presentation_enabled = true;
    std::vector<ActorBehaviorInitialValueV1> field_values;
    std::vector<std::uint32_t> instance_random_words;
  };

  struct MutableStateV1 {
    std::uint64_t next_tick_index = 0U;
    std::vector<std::vector<std::uint32_t>> shared_random_words;
    std::vector<std::uint64_t> shared_random_call_counts;
    std::vector<InstanceStateV1> instances;
  };

  void bind_registry(const ActorBehaviorRuntimeRegistryV1 &registry);
  void validate_entity_capabilities();
  void
  make_initial_state(std::uint64_t first_tick_index,
                     std::span<const ActorBehaviorInitialPresentationEnabledV1>
                         initial_presentation_enabled = {});
  void make_initialization_journal();
  void restore_snapshot(const ActorBehaviorRuntimeSnapshotV1 &snapshot);

  ActorBehaviorSceneV1 scene_;
  ActorBehaviorRuntimeLimitsV1 limits_;
  std::uint32_t runtime_ticks_per_second_ = 0U;
  std::vector<ActorBehaviorEntityCapabilitiesV1> entity_capabilities_;
  std::vector<ProgramBindingV1> programs_;
  MutableStateV1 state_;
  std::vector<ActorBehaviorJournalEntryV1> initialization_journal_;
};

} // namespace openrc::game
