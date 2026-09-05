#pragma once

#include "openrc/game_world.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kActorBehaviorSceneSchemaVersionV1 = 1U;

inline constexpr std::uint32_t kActorBehaviorProgramKnownFlagsV1 = 0U;
inline constexpr std::uint32_t
    kActorBehaviorEntityReferenceRequiresTransformV1 = 1U << 0U;
inline constexpr std::uint32_t kActorBehaviorEntityReferenceRequiresActorV1 =
    1U << 1U;
inline constexpr std::uint32_t kActorBehaviorEntityReferenceRequiresBehaviorV1 =
    1U << 2U;
inline constexpr std::uint32_t kActorBehaviorFieldKnownFlagsV1 =
    kActorBehaviorEntityReferenceRequiresTransformV1 |
    kActorBehaviorEntityReferenceRequiresActorV1 |
    kActorBehaviorEntityReferenceRequiresBehaviorV1;
inline constexpr std::uint32_t kActorBehaviorAnimationImportKnownFlagsV1 = 0U;
inline constexpr std::uint32_t kActorBehaviorRandomImportKnownFlagsV1 = 0U;
inline constexpr std::uint32_t kActorBehaviorRandomStreamKnownFlagsV1 = 0U;
inline constexpr std::uint32_t kActorBehaviorInstanceKnownFlagsV1 = 0U;
inline constexpr std::uint32_t kActorBehaviorInitialAnimationKnownFlagsV1 = 0U;

// A field type is part of a program's stable state-layout digest. V1 carries
// only values with portable behavior semantics; source memory layouts,
// pointers, executable addresses, and game-specific numeric class IDs are not
// representable here.
enum class ActorBehaviorValueTypeV1 : std::uint32_t {
  boolean = 0U,
  signed_integer = 1U,
  unsigned_integer = 2U,
  scalar_f32 = 3U,
  vector3_f32 = 4U,
  entity_reference = 5U,
};

struct ActorBehaviorEntityReferenceV1 {
  // Absence is explicit rather than encoded as a magic authored ID.
  std::optional<std::uint32_t> authored_id;

  [[nodiscard]] bool
  operator==(const ActorBehaviorEntityReferenceV1 &) const = default;
};

using ActorBehaviorInitialValueV1 =
    std::variant<bool, std::int32_t, std::uint32_t, float,
                 std::array<float, 3U>, ActorBehaviorEntityReferenceV1>;

struct ActorBehaviorStateFieldV1 {
  std::uint32_t id = 0U;
  std::string semantic_key;
  ActorBehaviorValueTypeV1 value_type = ActorBehaviorValueTypeV1::boolean;
  // Values are flattened field-major and then element-major in each instance.
  std::uint32_t element_count = 0U;
  // Only entity-reference fields may set the requires-transform/actor/behavior
  // relationship bits. Every other V1 field requires zero flags.
  std::uint32_t flags = 0U;

  [[nodiscard]] bool
  operator==(const ActorBehaviorStateFieldV1 &) const = default;
};

// clip_key identifies one ActorAnimationBankV1 clip. The dense import ID is
// the implementation-visible binding slot local to its owning program.
struct ActorBehaviorAnimationImportV1 {
  std::uint32_t id = 0U;
  std::string binding_key;
  std::string clip_key;
  // Pins the canonical clip bytes behind clip_key; a semantic alias cannot
  // silently change timing or pose data under an exact behavior program.
  PreparedContentDigestV1 required_clip_sha256{};
  // Positive and exact. Both initial selections and runtime animation output
  // must stay in [0, required_frame_count); mount also verifies the clip size.
  std::uint32_t required_frame_count = 0U;
  std::uint32_t flags = 0U;

  [[nodiscard]] bool
  operator==(const ActorBehaviorAnimationImportV1 &) const = default;
};

enum class ActorBehaviorRandomScopeV1 : std::uint32_t {
  instance = 0U,
  level_shared = 1U,
  session_shared = 2U,
};

// A random import carries the complete state-shape and dispatch contract.
// instance imports consume state_word_count words from every owning instance;
// shared imports resolve a scene stream with an exactly matching contract.
// An instance stream_key is unique within a program, with independent identity
// in other programs. Multiple shared bindings may refer to the same stream.
struct ActorBehaviorRandomImportV1 {
  std::uint32_t id = 0U;
  std::string binding_key;
  std::string stream_key;
  // Runtime dispatch is an exact key match and never parses stream_key.
  std::string algorithm_key;
  std::uint32_t algorithm_abi_version = 0U;
  ActorBehaviorRandomScopeV1 scope = ActorBehaviorRandomScopeV1::instance;
  std::uint32_t state_word_count = 0U;
  std::uint32_t flags = 0U;

  [[nodiscard]] bool
  operator==(const ActorBehaviorRandomImportV1 &) const = default;
};

struct ActorBehaviorRandomStreamV1 {
  std::uint32_t id = 0U;
  std::string semantic_key;
  std::string algorithm_key;
  std::uint32_t algorithm_abi_version = 0U;
  // Scene streams may only be shared. Instance-scoped state lives directly on
  // ActorBehaviorInstanceV1 and cannot accidentally become global.
  ActorBehaviorRandomScopeV1 scope = ActorBehaviorRandomScopeV1::level_shared;
  // Initial words are data, not an algorithm contract and are not included in
  // a program layout digest.
  std::vector<std::uint32_t> initial_state_words;
  std::uint32_t flags = 0U;

  [[nodiscard]] bool
  operator==(const ActorBehaviorRandomStreamV1 &) const = default;
};

struct ActorBehaviorProgramV1 {
  std::uint32_t id = 0U;
  // Stable package identity for this authored behavior variant. The registry
  // dispatch identity remains implementation_key + ABI + layout digest.
  std::string semantic_key;
  std::string implementation_key;
  std::string required_rig_key;
  PreparedContentDigestV1 required_rig_sha256{};
  std::string required_model_key;
  PreparedContentDigestV1 required_model_sha256{};
  std::uint32_t implementation_abi_version = 0U;
  std::uint32_t state_count = 0U;
  // Exact source behavior cadence (PAL 50, NTSC 60). Runtime fixed ticks use
  // an accumulator rather than silently changing source update frequency.
  std::uint32_t source_updates_per_second = 0U;
  std::uint32_t animation_channel_count = 0U;

  // Hashes the complete canonical program contract described by
  // actor_behavior_program_layout_sha256_v1. A runtime implementation is
  // selected only by the exact key/ABI/layout triple.
  PreparedContentDigestV1 state_layout_sha256{};
  std::uint32_t flags = 0U;
  std::vector<ActorBehaviorStateFieldV1> fields;
  std::vector<ActorBehaviorAnimationImportV1> animation_imports;
  std::vector<ActorBehaviorRandomImportV1> random_imports;

  [[nodiscard]] bool operator==(const ActorBehaviorProgramV1 &) const = default;
};

struct ActorBehaviorInitialAnimationV1 {
  std::uint32_t channel_id = 0U;
  std::uint32_t animation_import_id = 0U;
  std::uint32_t first_frame_index = 0U;
  std::uint32_t flags = 0U;

  [[nodiscard]] bool
  operator==(const ActorBehaviorInitialAnimationV1 &) const = default;
};

struct ActorBehaviorInstanceV1 {
  // References the same sparse ID in EntitySceneV1. program_id is a dense ID
  // in this resource. initial_values is a complete positional image of every
  // program field; missing/default-by-accident fields are rejected.
  std::uint32_t authored_id = 0U;
  std::uint32_t program_id = 0U;
  std::uint32_t initial_state_id = 0U;
  std::uint32_t flags = 0U;
  std::vector<ActorBehaviorInitialValueV1> initial_values;
  // Flattened in random-import ID order, including words only for imports with
  // instance scope. Shared imports own their initial words in the scene table.
  std::vector<std::uint32_t> initial_random_words;
  // Strict channel-ID order. Missing channels start unbound; a present record
  // selects the exact imported clip and source frame before the first tick.
  std::vector<ActorBehaviorInitialAnimationV1> initial_animations;

  [[nodiscard]] bool
  operator==(const ActorBehaviorInstanceV1 &) const = default;
};

struct ActorBehaviorSceneV1 {
  std::uint32_t schema_version = kActorBehaviorSceneSchemaVersionV1;
  game::LevelIdV1 level_id = 0U;
  std::vector<ActorBehaviorProgramV1> programs;
  std::vector<ActorBehaviorRandomStreamV1> random_streams;

  // Canonical order is strictly ascending by referenced authored_id.
  std::vector<ActorBehaviorInstanceV1> instances;

  [[nodiscard]] bool operator==(const ActorBehaviorSceneV1 &) const = default;
};

// Every bound is mandatory. Aggregate limits cover the complete resource;
// per-program/per-instance limits are checked independently before allocation.
struct ActorBehaviorSceneLimitsV1 {
  std::uint32_t max_programs = 0U;
  std::uint32_t max_fields_per_program = 0U;
  std::uint32_t max_total_fields = 0U;
  std::uint32_t max_elements_per_field = 0U;
  std::uint64_t max_total_field_elements = 0U;
  std::uint32_t max_animation_imports_per_program = 0U;
  std::uint32_t max_total_animation_imports = 0U;
  std::uint32_t max_random_imports_per_program = 0U;
  std::uint32_t max_total_random_imports = 0U;
  std::uint32_t max_random_streams = 0U;
  std::uint32_t max_random_state_words_per_stream = 0U;
  std::uint64_t max_total_random_state_words = 0U;
  std::uint32_t max_instances = 0U;
  std::uint32_t max_initial_values_per_instance = 0U;
  std::uint64_t max_total_initial_values = 0U;
  std::uint32_t max_initial_random_words_per_instance = 0U;
  std::uint64_t max_total_initial_random_words = 0U;
  std::uint32_t max_initial_animations_per_instance = 0U;
  std::uint64_t max_total_initial_animations = 0U;
  std::uint32_t max_semantic_key_bytes = 0U;
  std::uint64_t max_total_semantic_key_bytes = 0U;
  std::uint32_t max_implementation_abi_version = 0U;
  std::uint32_t max_random_algorithm_abi_version = 0U;
  std::uint32_t max_states_per_program = 0U;
  std::uint32_t max_source_updates_per_second = 0U;
  std::uint32_t max_animation_channels_per_program = 0U;
  float max_absolute_initial_float = 0.0F;

  [[nodiscard]] bool
  operator==(const ActorBehaviorSceneLimitsV1 &) const = default;
};

class ActorBehaviorSceneError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

[[nodiscard]] ActorBehaviorValueTypeV1 actor_behavior_initial_value_type_v1(
    const ActorBehaviorInitialValueV1 &value) noexcept;

// Hashes ABI, required rig and model keys/digests, state count, exact source
// update cadence, animation-channel count, state field
// count/IDs/types/element counts/flags/keys, animation-import
// count/IDs/flags/binding and clip keys/digests/frame counts, and
// random-import count/IDs/flags/binding/stream/algorithm keys/algorithm ABI/
// scope/state-word count. Program ID, program semantic_key,
// implementation_key, instance state/animations/values, and actual initial
// random words are excluded because they are identities or data rather than
// implementation layout. Input must already be canonical.
[[nodiscard]] PreparedContentDigestV1
actor_behavior_program_layout_sha256_v1(const ActorBehaviorProgramV1 &program);

// Requires exact schema/version semantics, canonical dense local IDs,
// canonical semantic keys, current state-layout digests, complete typed
// initial state, resolvable shared RNG imports, and every explicit limit.
void validate_actor_behavior_scene_v1(const ActorBehaviorSceneV1 &scene,
                                      ActorBehaviorSceneLimitsV1 limits);

// Sorts ID-bearing tables, authored instances, and maps signed floating zero
// to positive zero. Zero state-layout digests are filled; non-zero stale
// digests fail. Sparse/duplicate IDs and incomplete state are never repaired.
[[nodiscard]] ActorBehaviorSceneV1
canonicalize_actor_behavior_scene_v1(ActorBehaviorSceneV1 scene,
                                     ActorBehaviorSceneLimitsV1 limits);

} // namespace openrc
