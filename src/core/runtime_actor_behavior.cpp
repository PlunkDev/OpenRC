#include "openrc/runtime_actor_behavior.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace openrc::game {
namespace {

constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();
constexpr std::uint64_t kFnvOffset = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

[[noreturn]] void fail(const std::string &message) {
  throw ActorBehaviorRuntimeError(message);
}

[[nodiscard]] bool digest_less(const PreparedContentDigestV1 &left,
                               const PreparedContentDigestV1 &right) noexcept {
  return std::lexicographical_compare(left.begin(), left.end(), right.begin(),
                                      right.end());
}

[[nodiscard]] bool
identity_less(const ActorBehaviorImplementationIdentityV1 &left,
              const ActorBehaviorImplementationIdentityV1 &right) noexcept {
  if (left.implementation_key != right.implementation_key) {
    return left.implementation_key < right.implementation_key;
  }
  if (left.implementation_abi_version != right.implementation_abi_version) {
    return left.implementation_abi_version < right.implementation_abi_version;
  }
  return digest_less(left.state_layout_sha256, right.state_layout_sha256);
}

[[nodiscard]] ActorBehaviorImplementationIdentityV1
program_identity(const ActorBehaviorProgramV1 &program) {
  return {
      program.implementation_key,
      program.implementation_abi_version,
      program.state_layout_sha256,
  };
}

[[nodiscard]] bool random_algorithm_less(
    const ActorBehaviorRandomAlgorithmImplementationV1 &left,
    const std::pair<std::string_view, std::uint32_t> right) noexcept {
  if (left.algorithm_key != right.first) {
    return left.algorithm_key < right.first;
  }
  return left.algorithm_abi_version < right.second;
}

[[nodiscard]] ActorBehaviorInitialValueV1
normalize_runtime_value(ActorBehaviorInitialValueV1 value,
                        const ActorBehaviorStateFieldV1 &field,
                        const float maximum_absolute_float,
                        const std::span<const ActorBehaviorEntityCapabilitiesV1>
                            entity_capabilities) {
  if (actor_behavior_initial_value_type_v1(value) != field.value_type) {
    fail("An actor behavior implementation wrote the wrong field type");
  }

  if (auto *const scalar = std::get_if<float>(&value)) {
    if (!std::isfinite(*scalar) || std::abs(*scalar) > maximum_absolute_float) {
      fail("An actor behavior implementation wrote an invalid scalar");
    }
    if (*scalar == 0.0F) {
      *scalar = 0.0F;
    }
  }
  if (auto *const vector = std::get_if<std::array<float, 3U>>(&value)) {
    for (auto &component : *vector) {
      if (!std::isfinite(component) ||
          std::abs(component) > maximum_absolute_float) {
        fail("An actor behavior implementation wrote an invalid vector");
      }
      if (component == 0.0F) {
        component = 0.0F;
      }
    }
  }
  if (const auto *const reference =
          std::get_if<ActorBehaviorEntityReferenceV1>(&value);
      reference != nullptr && reference->authored_id) {
    const auto found =
        std::lower_bound(entity_capabilities.begin(), entity_capabilities.end(),
                         *reference->authored_id,
                         [](const ActorBehaviorEntityCapabilitiesV1 &candidate,
                            const std::uint32_t authored_id) {
                           return candidate.authored_id < authored_id;
                         });
    if (found == entity_capabilities.end() ||
        found->authored_id != *reference->authored_id) {
      fail("An actor behavior entity reference has no mounted entity");
    }
    if ((found->flags & field.flags) != field.flags) {
      fail("An actor behavior entity reference lacks required capabilities");
    }
  }
  return value;
}

void hash_byte(std::uint64_t &hash, const std::uint8_t value) noexcept {
  hash ^= value;
  hash *= kFnvPrime;
}

void hash_u32(std::uint64_t &hash, const std::uint32_t value) noexcept {
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
    hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

void hash_u64(std::uint64_t &hash, const std::uint64_t value) noexcept {
  for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
    hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

void hash_string(std::uint64_t &hash, const std::string_view value) noexcept {
  hash_u64(hash, static_cast<std::uint64_t>(value.size()));
  for (const char character : value) {
    hash_byte(hash, static_cast<std::uint8_t>(character));
  }
}

void hash_digest(std::uint64_t &hash,
                 const PreparedContentDigestV1 &digest) noexcept {
  for (const auto value : digest) {
    hash_byte(hash, std::to_integer<std::uint8_t>(value));
  }
}

void hash_float(std::uint64_t &hash, float value) {
  if (!std::isfinite(value)) {
    fail("An actor behavior snapshot contains a non-finite value");
  }
  if (value == 0.0F) {
    value = 0.0F;
  }
  hash_u32(hash, std::bit_cast<std::uint32_t>(value));
}

void hash_value(std::uint64_t &hash, const ActorBehaviorInitialValueV1 &value) {
  const auto type = actor_behavior_initial_value_type_v1(value);
  hash_u32(hash, static_cast<std::uint32_t>(type));
  std::visit(
      [&hash](const auto &typed) {
        using Value = std::remove_cvref_t<decltype(typed)>;
        if constexpr (std::is_same_v<Value, bool>) {
          hash_byte(hash, typed ? 1U : 0U);
        } else if constexpr (std::is_same_v<Value, std::int32_t>) {
          hash_u32(hash, static_cast<std::uint32_t>(typed));
        } else if constexpr (std::is_same_v<Value, std::uint32_t>) {
          hash_u32(hash, typed);
        } else if constexpr (std::is_same_v<Value, float>) {
          hash_float(hash, typed);
        } else if constexpr (std::is_same_v<Value, std::array<float, 3U>>) {
          for (const auto component : typed) {
            hash_float(hash, component);
          }
        } else {
          hash_byte(hash, typed.authored_id ? 1U : 0U);
          if (typed.authored_id) {
            hash_u32(hash, *typed.authored_id);
          }
        }
      },
      value);
}

} // namespace

struct ActorBehaviorInvocationV1::BackingV1 {
  std::uint64_t tick_index = 0U;
  std::uint32_t authored_id = 0U;
  const ActorBehaviorProgramV1 *program = nullptr;
  const std::vector<std::size_t> *field_offsets = nullptr;
  const std::vector<std::size_t> *instance_random_offsets = nullptr;
  const std::vector<std::size_t> *shared_random_indices = nullptr;
  const std::vector<ActorBehaviorRandomAlgorithmImplementationV1>
      *random_algorithms = nullptr;
  std::uint32_t *current_state_id = nullptr;
  std::uint64_t *state_ticks = nullptr;
  std::uint64_t *random_call_count = nullptr;
  bool *presentation_enabled = nullptr;
  std::vector<ActorBehaviorInitialValueV1> *field_values = nullptr;
  std::vector<std::uint32_t> *instance_random_words = nullptr;
  std::vector<std::vector<std::uint32_t>> *shared_random_words = nullptr;
  std::vector<std::uint64_t> *shared_random_call_counts = nullptr;
  std::vector<ActorBehaviorJournalEntryV1> *journal = nullptr;
  std::uint32_t max_journal_entries = 0U;
  float maximum_absolute_float = 0.0F;
  std::span<const ActorBehaviorEntityCapabilitiesV1> entity_capabilities;
  bool transitioned = false;
};

ActorBehaviorInvocationV1::ActorBehaviorInvocationV1(
    BackingV1 &backing) noexcept
    : backing_(&backing) {}

std::uint64_t ActorBehaviorInvocationV1::tick_index() const noexcept {
  return backing_->tick_index;
}

std::uint32_t ActorBehaviorInvocationV1::authored_id() const noexcept {
  return backing_->authored_id;
}

std::uint32_t ActorBehaviorInvocationV1::current_state_id() const noexcept {
  return *backing_->current_state_id;
}

std::uint64_t ActorBehaviorInvocationV1::state_ticks() const noexcept {
  return *backing_->state_ticks;
}

bool ActorBehaviorInvocationV1::presentation_enabled() const noexcept {
  return *backing_->presentation_enabled;
}

const ActorBehaviorInitialValueV1 &
ActorBehaviorInvocationV1::field(const std::uint32_t field_id,
                                 const std::uint32_t element_index) const {
  if (field_id >= backing_->program->fields.size()) {
    fail("An actor behavior implementation read an unknown field ID");
  }
  const auto &field = backing_->program->fields[field_id];
  if (element_index >= field.element_count) {
    fail("An actor behavior implementation read beyond a field array");
  }
  const auto offset = (*backing_->field_offsets)[field_id] + element_index;
  if (offset >= backing_->field_values->size()) {
    fail("Actor behavior field storage disagrees with its program layout");
  }
  return (*backing_->field_values)[offset];
}

void ActorBehaviorInvocationV1::set_field(const std::uint32_t field_id,
                                          const std::uint32_t element_index,
                                          ActorBehaviorInitialValueV1 value) {
  if (field_id >= backing_->program->fields.size()) {
    fail("An actor behavior implementation wrote an unknown field ID");
  }
  const auto &field = backing_->program->fields[field_id];
  if (element_index >= field.element_count) {
    fail("An actor behavior implementation wrote beyond a field array");
  }
  const auto offset = (*backing_->field_offsets)[field_id] + element_index;
  if (offset >= backing_->field_values->size()) {
    fail("Actor behavior field storage disagrees with its program layout");
  }
  (*backing_->field_values)[offset] = normalize_runtime_value(
      std::move(value), field, backing_->maximum_absolute_float,
      backing_->entity_capabilities);
}

void ActorBehaviorInvocationV1::transition_to_state(
    const std::uint32_t state_id) {
  if (state_id >= backing_->program->state_count) {
    fail("An actor behavior implementation selected an unknown state ID");
  }
  *backing_->current_state_id = state_id;
  *backing_->state_ticks = 0U;
  backing_->transitioned = true;
}

std::uint32_t ActorBehaviorInvocationV1::next_random_u32(
    const std::uint32_t random_import_id) {
  if (random_import_id >= backing_->program->random_imports.size()) {
    fail("An actor behavior implementation used an unknown random import");
  }
  const auto &import = backing_->program->random_imports[random_import_id];
  const auto &algorithm = (*backing_->random_algorithms)[random_import_id];
  if (*backing_->random_call_count ==
      std::numeric_limits<std::uint64_t>::max()) {
    fail("An actor behavior instance RNG call counter is exhausted");
  }
  std::span<std::uint32_t> state_words;
  std::uint64_t *shared_call_count = nullptr;
  switch (import.scope) {
  case ActorBehaviorRandomScopeV1::instance: {
    const auto offset = (*backing_->instance_random_offsets)[random_import_id];
    if (offset == kNoIndex ||
        offset > backing_->instance_random_words->size() ||
        import.state_word_count >
            backing_->instance_random_words->size() - offset) {
      fail("Actor behavior instance RNG storage disagrees with its import");
    }
    state_words = std::span<std::uint32_t>(*backing_->instance_random_words)
                      .subspan(offset, import.state_word_count);
    break;
  }
  case ActorBehaviorRandomScopeV1::level_shared: {
    const auto stream_index =
        (*backing_->shared_random_indices)[random_import_id];
    if (stream_index == kNoIndex ||
        stream_index >= backing_->shared_random_words->size()) {
      fail("Actor behavior shared RNG storage disagrees with its import");
    }
    auto &words = (*backing_->shared_random_words)[stream_index];
    if (words.size() != import.state_word_count) {
      fail("Actor behavior shared RNG word count changed at runtime");
    }
    shared_call_count = &(*backing_->shared_random_call_counts)[stream_index];
    if (*shared_call_count == std::numeric_limits<std::uint64_t>::max()) {
      fail("An actor behavior shared RNG call counter is exhausted");
    }
    state_words = std::span<std::uint32_t>(words);
    break;
  }
  case ActorBehaviorRandomScopeV1::session_shared:
    fail("Session-shared actor RNG requires a session state owner");
  }
  const auto result = algorithm.next_u32(state_words);
  ++*backing_->random_call_count;
  if (shared_call_count != nullptr) {
    ++*shared_call_count;
  }
  return result;
}

void ActorBehaviorInvocationV1::emit_set_animation(
    const std::uint32_t channel_id, const std::uint32_t animation_import_id,
    const std::uint32_t first_frame_index,
    const std::uint32_t transition_source_update_count) {
  if (channel_id >= backing_->program->animation_channel_count) {
    fail("An actor behavior implementation used an unknown animation channel");
  }
  if (animation_import_id >= backing_->program->animation_imports.size()) {
    fail("An actor behavior implementation used an unknown animation import");
  }
  if (backing_->journal->size() >= backing_->max_journal_entries) {
    fail("An actor behavior tick exceeded its command-journal limit");
  }
  const auto ordinal = static_cast<std::uint32_t>(backing_->journal->size());
  const auto &import =
      backing_->program->animation_imports[animation_import_id];
  if (first_frame_index >= import.required_frame_count) {
    fail(
        "An actor behavior implementation selected an invalid animation frame");
  }
  backing_->journal->push_back(ActorBehaviorJournalEntryV1{
      ordinal,
      backing_->authored_id,
      ActorBehaviorSetAnimationCommandV1{
          channel_id,
          animation_import_id,
          import.clip_key,
          import.required_clip_sha256,
          first_frame_index,
          transition_source_update_count,
      },
  });
}

void ActorBehaviorInvocationV1::emit_set_presentation_enabled(
    const bool presentation_enabled) {
  if (backing_->journal->size() >= backing_->max_journal_entries) {
    fail("An actor behavior tick exceeded its command-journal limit");
  }
  *backing_->presentation_enabled = presentation_enabled;
  const auto ordinal = static_cast<std::uint32_t>(backing_->journal->size());
  backing_->journal->push_back(ActorBehaviorJournalEntryV1{
      ordinal,
      backing_->authored_id,
      ActorBehaviorSetPresentationEnabledCommandV1{presentation_enabled},
  });
}

void ActorBehaviorRuntimeRegistryV1::register_implementation(
    ActorBehaviorImplementationV1 implementation) {
  if (implementation.identity.implementation_key.empty() ||
      implementation.identity.implementation_abi_version == 0U ||
      !implementation.fixed_tick) {
    fail("An actor behavior implementation registration is incomplete");
  }
  const auto candidate = std::lower_bound(
      implementations_.begin(), implementations_.end(), implementation.identity,
      [](const ActorBehaviorImplementationV1 &left,
         const ActorBehaviorImplementationIdentityV1 &right) {
        return identity_less(left.identity, right);
      });
  if (candidate != implementations_.end() &&
      candidate->identity == implementation.identity) {
    fail("An actor behavior implementation identity is already registered");
  }
  implementations_.insert(candidate, std::move(implementation));
}

void ActorBehaviorRuntimeRegistryV1::register_random_algorithm(
    ActorBehaviorRandomAlgorithmImplementationV1 implementation) {
  if (implementation.algorithm_key.empty() ||
      implementation.algorithm_abi_version == 0U ||
      implementation.state_word_count == 0U || !implementation.next_u32) {
    fail("An actor behavior random-algorithm registration is incomplete");
  }
  const auto identity = std::pair<std::string_view, std::uint32_t>{
      implementation.algorithm_key,
      implementation.algorithm_abi_version,
  };
  const auto candidate =
      std::lower_bound(random_algorithms_.begin(), random_algorithms_.end(),
                       identity, random_algorithm_less);
  if (candidate != random_algorithms_.end() &&
      candidate->algorithm_key == identity.first &&
      candidate->algorithm_abi_version == identity.second) {
    fail("An actor behavior random algorithm is already registered");
  }
  random_algorithms_.insert(candidate, std::move(implementation));
}

const ActorBehaviorImplementationV1 *
ActorBehaviorRuntimeRegistryV1::find_implementation(
    const ActorBehaviorImplementationIdentityV1 &identity) const noexcept {
  const auto candidate = std::lower_bound(
      implementations_.begin(), implementations_.end(), identity,
      [](const ActorBehaviorImplementationV1 &left,
         const ActorBehaviorImplementationIdentityV1 &right) {
        return identity_less(left.identity, right);
      });
  return candidate != implementations_.end() && candidate->identity == identity
             ? &*candidate
             : nullptr;
}

const ActorBehaviorRandomAlgorithmImplementationV1 *
ActorBehaviorRuntimeRegistryV1::find_random_algorithm(
    const std::string_view algorithm_key,
    const std::uint32_t algorithm_abi_version) const noexcept {
  const auto identity = std::pair{algorithm_key, algorithm_abi_version};
  const auto candidate =
      std::lower_bound(random_algorithms_.begin(), random_algorithms_.end(),
                       identity, random_algorithm_less);
  return candidate != random_algorithms_.end() &&
                 candidate->algorithm_key == algorithm_key &&
                 candidate->algorithm_abi_version == algorithm_abi_version
             ? &*candidate
             : nullptr;
}

std::uint64_t hash_actor_behavior_runtime_snapshot_v1(
    const ActorBehaviorRuntimeSnapshotV1 &snapshot) {
  std::uint64_t hash = kFnvOffset;
  hash_string(hash, "openrc.actor-behavior-runtime-snapshot/v1");
  hash_u32(hash, snapshot.level_id);
  if (snapshot.runtime_ticks_per_second == 0U) {
    fail("An actor behavior snapshot has zero runtime tick cadence");
  }
  hash_u32(hash, snapshot.runtime_ticks_per_second);
  hash_u64(hash, snapshot.next_tick_index);

  hash_u64(hash,
           static_cast<std::uint64_t>(snapshot.shared_random_streams.size()));
  for (std::size_t index = 0U; index < snapshot.shared_random_streams.size();
       ++index) {
    const auto &stream = snapshot.shared_random_streams[index];
    if (stream.stream_id != index || stream.stream_key.empty() ||
        stream.algorithm_key.empty() || stream.algorithm_abi_version == 0U ||
        (stream.scope != ActorBehaviorRandomScopeV1::level_shared &&
         stream.scope != ActorBehaviorRandomScopeV1::session_shared) ||
        stream.state_words.empty()) {
      fail("An actor behavior snapshot has invalid shared RNG identity");
    }
    hash_u32(hash, stream.stream_id);
    hash_string(hash, stream.stream_key);
    hash_string(hash, stream.algorithm_key);
    hash_u32(hash, stream.algorithm_abi_version);
    hash_u32(hash, static_cast<std::uint32_t>(stream.scope));
    hash_u64(hash, static_cast<std::uint64_t>(stream.state_words.size()));
    for (const auto word : stream.state_words) {
      hash_u32(hash, word);
    }
    hash_u64(hash, stream.call_count);
  }

  hash_u64(hash, static_cast<std::uint64_t>(snapshot.instances.size()));
  for (std::size_t index = 0U; index < snapshot.instances.size(); ++index) {
    const auto &instance = snapshot.instances[index];
    if ((index != 0U &&
         snapshot.instances[index - 1U].authored_id >= instance.authored_id) ||
        instance.implementation.implementation_key.empty() ||
        instance.implementation.implementation_abi_version == 0U) {
      fail("An actor behavior snapshot has invalid instance identity order");
    }
    hash_u32(hash, instance.authored_id);
    hash_u32(hash, instance.program_id);
    hash_string(hash, instance.implementation.implementation_key);
    hash_u32(hash, instance.implementation.implementation_abi_version);
    hash_digest(hash, instance.implementation.state_layout_sha256);
    hash_u32(hash, instance.current_state_id);
    hash_u64(hash, instance.state_ticks);
    if (instance.source_update_accumulator >=
        snapshot.runtime_ticks_per_second) {
      fail("An actor behavior snapshot has an invalid source update "
           "accumulator");
    }
    hash_u32(hash, instance.source_update_accumulator);
    hash_u64(hash, instance.random_call_count);
    hash_byte(hash, instance.presentation_enabled ? 1U : 0U);
    hash_u64(hash, static_cast<std::uint64_t>(instance.field_values.size()));
    for (const auto &value : instance.field_values) {
      hash_value(hash, value);
    }
    hash_u64(hash,
             static_cast<std::uint64_t>(instance.instance_random_words.size()));
    for (const auto word : instance.instance_random_words) {
      hash_u32(hash, word);
    }
  }
  return hash;
}

ActorBehaviorRuntimeV1::ActorBehaviorRuntimeV1(
    ActorBehaviorSceneV1 scene, const ActorBehaviorRuntimeRegistryV1 &registry,
    const ActorBehaviorRuntimeLimitsV1 limits,
    const std::uint32_t runtime_ticks_per_second,
    const std::uint64_t first_tick_index,
    const std::span<const ActorBehaviorInitialPresentationEnabledV1>
        initial_presentation_enabled,
    const std::span<const ActorBehaviorEntityCapabilitiesV1>
        entity_capabilities)
    : scene_(std::move(scene)), limits_(limits),
      runtime_ticks_per_second_(runtime_ticks_per_second),
      entity_capabilities_(entity_capabilities.begin(),
                           entity_capabilities.end()) {
  if (limits_.max_journal_entries_per_tick == 0U) {
    fail("Actor behavior runtime requires a non-zero journal limit");
  }
  try {
    validate_actor_behavior_scene_v1(scene_, limits_.scene);
  } catch (const ActorBehaviorSceneError &error) {
    fail(std::string("Actor behavior runtime rejected its scene: ") +
         error.what());
  }
  validate_entity_capabilities();
  bind_registry(registry);
  make_initial_state(first_tick_index, initial_presentation_enabled);
  make_initialization_journal();
}

ActorBehaviorRuntimeV1::ActorBehaviorRuntimeV1(
    ActorBehaviorSceneV1 scene, const ActorBehaviorRuntimeRegistryV1 &registry,
    const ActorBehaviorRuntimeLimitsV1 limits,
    const std::uint32_t runtime_ticks_per_second,
    const ActorBehaviorRuntimeSnapshotV1 &snapshot,
    const std::span<const ActorBehaviorEntityCapabilitiesV1>
        entity_capabilities)
    : scene_(std::move(scene)), limits_(limits),
      runtime_ticks_per_second_(runtime_ticks_per_second),
      entity_capabilities_(entity_capabilities.begin(),
                           entity_capabilities.end()) {
  if (limits_.max_journal_entries_per_tick == 0U) {
    fail("Actor behavior runtime requires a non-zero journal limit");
  }
  try {
    validate_actor_behavior_scene_v1(scene_, limits_.scene);
  } catch (const ActorBehaviorSceneError &error) {
    fail(std::string("Actor behavior runtime rejected its scene: ") +
         error.what());
  }
  validate_entity_capabilities();
  bind_registry(registry);
  restore_snapshot(snapshot);
}

void ActorBehaviorRuntimeV1::validate_entity_capabilities() {
  std::sort(entity_capabilities_.begin(), entity_capabilities_.end(),
            [](const ActorBehaviorEntityCapabilitiesV1 &left,
               const ActorBehaviorEntityCapabilitiesV1 &right) {
              return left.authored_id < right.authored_id;
            });
  for (std::size_t index = 0U; index < entity_capabilities_.size(); ++index) {
    const auto &capability = entity_capabilities_[index];
    if ((capability.flags & ~kActorBehaviorFieldKnownFlagsV1) != 0U ||
        (index != 0U && entity_capabilities_[index - 1U].authored_id ==
                            capability.authored_id)) {
      fail("Actor behavior entity capabilities contain duplicate IDs or "
           "unknown flags");
    }
  }
}

void ActorBehaviorRuntimeV1::bind_registry(
    const ActorBehaviorRuntimeRegistryV1 &registry) {
  if (runtime_ticks_per_second_ == 0U) {
    fail("Actor behavior runtime requires a non-zero runtime tick cadence");
  }
  std::unordered_map<std::string_view, std::size_t> shared_stream_indices;
  shared_stream_indices.reserve(scene_.random_streams.size());
  for (const auto &stream : scene_.random_streams) {
    if (stream.scope == ActorBehaviorRandomScopeV1::session_shared) {
      fail("Session-shared actor RNG requires a session state owner");
    }
    if (stream.scope != ActorBehaviorRandomScopeV1::level_shared) {
      fail("An actor behavior scene stream has a non-shared scope");
    }
    shared_stream_indices.emplace(stream.semantic_key, stream.id);
  }

  programs_.clear();
  programs_.reserve(scene_.programs.size());
  for (const auto &program : scene_.programs) {
    if (program.source_updates_per_second > runtime_ticks_per_second_) {
      fail("Actor behavior source update cadence exceeds runtime tick cadence");
    }
    ProgramBindingV1 binding;
    const auto identity = program_identity(program);
    const auto *const implementation = registry.find_implementation(identity);
    if (implementation == nullptr) {
      fail("No exact native implementation is registered for actor behavior " +
           program.semantic_key);
    }
    binding.implementation = *implementation;

    binding.field_offsets.reserve(program.fields.size());
    std::size_t field_offset = 0U;
    for (const auto &field : program.fields) {
      binding.field_offsets.push_back(field_offset);
      if (field.element_count >
          std::numeric_limits<std::size_t>::max() - field_offset) {
        fail("Actor behavior field layout exceeds the host size domain");
      }
      field_offset += field.element_count;
    }

    binding.random_algorithms.reserve(program.random_imports.size());
    binding.instance_random_offsets.assign(program.random_imports.size(),
                                           kNoIndex);
    binding.shared_random_indices.assign(program.random_imports.size(),
                                         kNoIndex);
    std::size_t instance_random_offset = 0U;
    for (const auto &import : program.random_imports) {
      const auto *const algorithm = registry.find_random_algorithm(
          import.algorithm_key, import.algorithm_abi_version);
      if (algorithm == nullptr) {
        fail("No exact native random algorithm is registered for actor "
             "behavior import " +
             import.binding_key);
      }
      if (algorithm->state_word_count != import.state_word_count) {
        fail("Actor behavior random algorithm state shape does not match its "
             "import");
      }
      binding.random_algorithms.push_back(*algorithm);

      switch (import.scope) {
      case ActorBehaviorRandomScopeV1::instance:
        binding.instance_random_offsets[import.id] = instance_random_offset;
        if (import.state_word_count >
            std::numeric_limits<std::size_t>::max() - instance_random_offset) {
          fail("Actor behavior instance RNG layout exceeds the host domain");
        }
        instance_random_offset += import.state_word_count;
        break;
      case ActorBehaviorRandomScopeV1::level_shared: {
        const auto found = shared_stream_indices.find(import.stream_key);
        const auto *const stream = found == shared_stream_indices.end()
                                       ? nullptr
                                       : &scene_.random_streams[found->second];
        if (stream == nullptr ||
            stream->algorithm_key != import.algorithm_key ||
            stream->algorithm_abi_version != import.algorithm_abi_version ||
            stream->scope != import.scope ||
            stream->initial_state_words.size() != import.state_word_count) {
          fail("Actor behavior shared RNG import has no exact scene stream");
        }
        binding.shared_random_indices[import.id] = found->second;
        break;
      }
      case ActorBehaviorRandomScopeV1::session_shared:
        fail("Session-shared actor RNG requires a session state owner");
      }
    }
    programs_.push_back(std::move(binding));
  }
}

void ActorBehaviorRuntimeV1::make_initial_state(
    const std::uint64_t first_tick_index,
    const std::span<const ActorBehaviorInitialPresentationEnabledV1>
        initial_presentation_enabled) {
  if (!initial_presentation_enabled.empty()) {
    if (initial_presentation_enabled.size() != scene_.instances.size()) {
      fail("Actor behavior initial presentation state is incomplete");
    }
    for (std::size_t index = 0U; index < initial_presentation_enabled.size();
         ++index) {
      if (initial_presentation_enabled[index].authored_id !=
          scene_.instances[index].authored_id) {
        fail("Actor behavior initial presentation state is not in canonical "
             "actor order");
      }
    }
  }
  MutableStateV1 initial;
  initial.next_tick_index = first_tick_index;
  initial.shared_random_words.reserve(scene_.random_streams.size());
  initial.shared_random_call_counts.assign(scene_.random_streams.size(), 0U);
  for (const auto &stream : scene_.random_streams) {
    initial.shared_random_words.push_back(stream.initial_state_words);
  }
  initial.instances.reserve(scene_.instances.size());
  for (std::size_t index = 0U; index < scene_.instances.size(); ++index) {
    const auto &instance = scene_.instances[index];
    const auto &program = scene_.programs[instance.program_id];
    InstanceStateV1 state;
    state.authored_id = instance.authored_id;
    state.program_id = instance.program_id;
    state.current_state_id = instance.initial_state_id;
    state.presentation_enabled =
        initial_presentation_enabled.empty()
            ? true
            : initial_presentation_enabled[index].presentation_enabled;
    state.field_values = instance.initial_values;
    state.instance_random_words = instance.initial_random_words;
    for (std::size_t field_id = 0U; field_id < program.fields.size();
         ++field_id) {
      const auto &field = program.fields[field_id];
      const auto offset = programs_[program.id].field_offsets[field_id];
      for (std::uint32_t element = 0U; element < field.element_count;
           ++element) {
        state.field_values[offset + element] = normalize_runtime_value(
            std::move(state.field_values[offset + element]), field,
            limits_.scene.max_absolute_initial_float, entity_capabilities_);
      }
    }
    initial.instances.push_back(std::move(state));
  }
  state_ = std::move(initial);
}

void ActorBehaviorRuntimeV1::make_initialization_journal() {
  std::uint64_t entry_count = scene_.instances.size();
  for (const auto &instance : scene_.instances) {
    if (instance.initial_animations.size() >
        std::numeric_limits<std::uint64_t>::max() - entry_count) {
      fail("Actor behavior initialization journal size overflows uint64_t");
    }
    entry_count += instance.initial_animations.size();
  }
  if (entry_count > limits_.max_journal_entries_per_tick) {
    fail("Actor behavior initialization exceeds its command-journal limit");
  }

  std::vector<ActorBehaviorJournalEntryV1> journal;
  journal.reserve(static_cast<std::size_t>(entry_count));
  for (std::size_t instance_index = 0U;
       instance_index < scene_.instances.size(); ++instance_index) {
    const auto &instance = scene_.instances[instance_index];
    const auto &program = scene_.programs[instance.program_id];
    const auto ordinal = static_cast<std::uint32_t>(journal.size());
    journal.push_back(ActorBehaviorJournalEntryV1{
        ordinal,
        instance.authored_id,
        ActorBehaviorSetPresentationEnabledCommandV1{
            state_.instances[instance_index].presentation_enabled},
    });
    for (const auto &initial_animation : instance.initial_animations) {
      if (initial_animation.channel_id >= program.animation_channel_count ||
          initial_animation.animation_import_id >=
              program.animation_imports.size()) {
        fail("Actor behavior initial animation disagrees with its program");
      }
      const auto &import =
          program.animation_imports[initial_animation.animation_import_id];
      if (initial_animation.first_frame_index >= import.required_frame_count) {
        fail("Actor behavior initial animation has an invalid animation frame");
      }
      journal.push_back(ActorBehaviorJournalEntryV1{
          static_cast<std::uint32_t>(journal.size()),
          instance.authored_id,
          ActorBehaviorSetAnimationCommandV1{
              initial_animation.channel_id,
              initial_animation.animation_import_id,
              import.clip_key,
              import.required_clip_sha256,
              initial_animation.first_frame_index,
              0U,
          },
      });
    }
  }
  initialization_journal_ = std::move(journal);
}

void ActorBehaviorRuntimeV1::restore_snapshot(
    const ActorBehaviorRuntimeSnapshotV1 &snapshot) {
  if (snapshot.level_id != scene_.level_id ||
      snapshot.runtime_ticks_per_second != runtime_ticks_per_second_ ||
      snapshot.shared_random_streams.size() != scene_.random_streams.size() ||
      snapshot.instances.size() != scene_.instances.size()) {
    fail("An actor behavior snapshot targets a different scene");
  }

  make_initial_state(snapshot.next_tick_index);
  for (std::size_t index = 0U; index < scene_.random_streams.size(); ++index) {
    const auto &expected = scene_.random_streams[index];
    const auto &saved = snapshot.shared_random_streams[index];
    if (saved.stream_id != expected.id ||
        saved.stream_key != expected.semantic_key ||
        saved.algorithm_key != expected.algorithm_key ||
        saved.algorithm_abi_version != expected.algorithm_abi_version ||
        saved.scope != expected.scope ||
        saved.state_words.size() != expected.initial_state_words.size()) {
      fail("An actor behavior snapshot has incompatible shared RNG state");
    }
    state_.shared_random_words[index] = saved.state_words;
    state_.shared_random_call_counts[index] = saved.call_count;
  }

  for (std::size_t index = 0U; index < scene_.instances.size(); ++index) {
    const auto &expected = scene_.instances[index];
    const auto &program = scene_.programs[expected.program_id];
    const auto &saved = snapshot.instances[index];
    if (saved.authored_id != expected.authored_id ||
        saved.program_id != expected.program_id ||
        saved.implementation != program_identity(program) ||
        saved.current_state_id >= program.state_count ||
        saved.source_update_accumulator >= runtime_ticks_per_second_ ||
        saved.field_values.size() != expected.initial_values.size() ||
        saved.instance_random_words.size() !=
            expected.initial_random_words.size()) {
      fail("An actor behavior snapshot has incompatible instance state");
    }
    auto &target = state_.instances[index];
    target.current_state_id = saved.current_state_id;
    target.state_ticks = saved.state_ticks;
    target.source_update_accumulator = saved.source_update_accumulator;
    target.random_call_count = saved.random_call_count;
    target.presentation_enabled = saved.presentation_enabled;
    target.field_values = saved.field_values;
    target.instance_random_words = saved.instance_random_words;
    for (std::size_t field_id = 0U; field_id < program.fields.size();
         ++field_id) {
      const auto &field = program.fields[field_id];
      const auto offset = programs_[program.id].field_offsets[field_id];
      for (std::uint32_t element = 0U; element < field.element_count;
           ++element) {
        target.field_values[offset + element] = normalize_runtime_value(
            std::move(target.field_values[offset + element]), field,
            limits_.scene.max_absolute_initial_float, entity_capabilities_);
      }
    }
  }
}

ActorBehaviorFixedTickV1 ActorBehaviorRuntimeV1::fixed_tick(
    const std::uint64_t tick_index,
    const std::span<const std::uint32_t> scheduled_authored_ids) {
  if (tick_index != state_.next_tick_index) {
    fail("Actor behavior ticks must execute in exact replay order");
  }
  if (state_.next_tick_index == std::numeric_limits<std::uint64_t>::max()) {
    fail("The actor behavior tick sequence is exhausted");
  }
  if (scheduled_authored_ids.size() > scene_.instances.size()) {
    fail("Actor behavior schedule contains too many actors");
  }
  std::vector<std::size_t> scheduled_indices;
  scheduled_indices.reserve(scheduled_authored_ids.size());
  for (const auto authored_id : scheduled_authored_ids) {
    const auto found = std::lower_bound(
        scene_.instances.begin(), scene_.instances.end(), authored_id,
        [](const ActorBehaviorInstanceV1 &instance, const std::uint32_t id) {
          return instance.authored_id < id;
        });
    if (found == scene_.instances.end() || found->authored_id != authored_id) {
      fail("Actor behavior schedule references an unknown actor");
    }
    scheduled_indices.push_back(
        static_cast<std::size_t>(found - scene_.instances.begin()));
  }
  auto unique_indices = scheduled_indices;
  std::sort(unique_indices.begin(), unique_indices.end());
  if (std::adjacent_find(unique_indices.begin(), unique_indices.end()) !=
      unique_indices.end()) {
    fail("Actor behavior schedule contains a duplicate actor");
  }

  auto staged = state_;
  std::vector<bool> source_update_due(staged.instances.size(), false);
  ActorBehaviorFixedTickV1 result;
  result.tick_index = tick_index;
  const auto reserve_count = std::min<std::size_t>(
      staged.instances.size(), limits_.max_journal_entries_per_tick);
  result.journal.reserve(reserve_count);

  for (std::size_t index = 0U; index < staged.instances.size(); ++index) {
    auto &instance = staged.instances[index];
    const auto &program = scene_.programs[instance.program_id];
    const auto accumulated =
        static_cast<std::uint64_t>(instance.source_update_accumulator) +
        program.source_updates_per_second;
    if (accumulated < runtime_ticks_per_second_) {
      instance.source_update_accumulator =
          static_cast<std::uint32_t>(accumulated);
      continue;
    }
    instance.source_update_accumulator =
        static_cast<std::uint32_t>(accumulated - runtime_ticks_per_second_);
    source_update_due[index] = true;
  }
  for (const auto index : scheduled_indices) {
    if (!source_update_due[index]) {
      continue;
    }
    auto &instance = staged.instances[index];
    const auto &program = scene_.programs[instance.program_id];
    const auto &binding = programs_[instance.program_id];
    ActorBehaviorInvocationV1::BackingV1 backing{
        tick_index,
        instance.authored_id,
        &program,
        &binding.field_offsets,
        &binding.instance_random_offsets,
        &binding.shared_random_indices,
        &binding.random_algorithms,
        &instance.current_state_id,
        &instance.state_ticks,
        &instance.random_call_count,
        &instance.presentation_enabled,
        &instance.field_values,
        &instance.instance_random_words,
        &staged.shared_random_words,
        &staged.shared_random_call_counts,
        &result.journal,
        limits_.max_journal_entries_per_tick,
        limits_.scene.max_absolute_initial_float,
        entity_capabilities_,
    };
    ActorBehaviorInvocationV1 invocation(backing);
    try {
      binding.implementation.fixed_tick(invocation);
    } catch (const std::bad_alloc &) {
      throw;
    } catch (const ActorBehaviorRuntimeError &) {
      throw;
    } catch (const std::exception &error) {
      fail("Actor behavior implementation failed for authored entity " +
           std::to_string(instance.authored_id) + ": " + error.what());
    } catch (...) {
      fail("Actor behavior implementation failed for authored entity " +
           std::to_string(instance.authored_id));
    }
    if (!backing.transitioned) {
      if (instance.state_ticks == std::numeric_limits<std::uint64_t>::max()) {
        fail("An actor behavior state tick counter is exhausted");
      }
      ++instance.state_ticks;
    }
  }

  ++staged.next_tick_index;
  static_assert(std::is_nothrow_move_assignable_v<MutableStateV1>);
  state_ = std::move(staged);
  return result;
}

const std::vector<ActorBehaviorJournalEntryV1> &
ActorBehaviorRuntimeV1::initialization_journal() const noexcept {
  return initialization_journal_;
}

ActorBehaviorRuntimeSnapshotV1 ActorBehaviorRuntimeV1::snapshot() const {
  ActorBehaviorRuntimeSnapshotV1 result;
  result.level_id = scene_.level_id;
  result.runtime_ticks_per_second = runtime_ticks_per_second_;
  result.next_tick_index = state_.next_tick_index;
  result.shared_random_streams.reserve(scene_.random_streams.size());
  for (std::size_t index = 0U; index < scene_.random_streams.size(); ++index) {
    const auto &stream = scene_.random_streams[index];
    result.shared_random_streams.push_back(ActorBehaviorSharedRandomSnapshotV1{
        stream.id,
        stream.semantic_key,
        stream.algorithm_key,
        stream.algorithm_abi_version,
        stream.scope,
        state_.shared_random_words[index],
        state_.shared_random_call_counts[index],
    });
  }
  result.instances.reserve(state_.instances.size());
  for (const auto &instance : state_.instances) {
    const auto &program = scene_.programs[instance.program_id];
    result.instances.push_back(ActorBehaviorInstanceSnapshotV1{
        instance.authored_id,
        instance.program_id,
        program_identity(program),
        instance.current_state_id,
        instance.state_ticks,
        instance.source_update_accumulator,
        instance.random_call_count,
        instance.presentation_enabled,
        instance.field_values,
        instance.instance_random_words,
    });
  }
  return result;
}

std::uint64_t ActorBehaviorRuntimeV1::deterministic_hash() const {
  return hash_actor_behavior_runtime_snapshot_v1(snapshot());
}

std::uint64_t ActorBehaviorRuntimeV1::next_tick_index() const noexcept {
  return state_.next_tick_index;
}

const ActorBehaviorSceneV1 &ActorBehaviorRuntimeV1::scene() const noexcept {
  return scene_;
}

} // namespace openrc::game
