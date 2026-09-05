#include "openrc/actor_behavior_scene.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw ActorBehaviorSceneError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

void validate_limits(const ActorBehaviorSceneLimitsV1 &limits) {
  if (limits.max_programs == 0U || limits.max_fields_per_program == 0U ||
      limits.max_total_fields == 0U || limits.max_elements_per_field == 0U ||
      limits.max_total_field_elements == 0U ||
      limits.max_animation_imports_per_program == 0U ||
      limits.max_total_animation_imports == 0U ||
      limits.max_random_imports_per_program == 0U ||
      limits.max_total_random_imports == 0U ||
      limits.max_random_streams == 0U ||
      limits.max_random_state_words_per_stream == 0U ||
      limits.max_total_random_state_words == 0U || limits.max_instances == 0U ||
      limits.max_initial_values_per_instance == 0U ||
      limits.max_total_initial_values == 0U ||
      limits.max_initial_random_words_per_instance == 0U ||
      limits.max_total_initial_random_words == 0U ||
      limits.max_initial_animations_per_instance == 0U ||
      limits.max_total_initial_animations == 0U ||
      limits.max_semantic_key_bytes == 0U ||
      limits.max_total_semantic_key_bytes == 0U ||
      limits.max_implementation_abi_version == 0U ||
      limits.max_random_algorithm_abi_version == 0U ||
      limits.max_states_per_program == 0U ||
      limits.max_source_updates_per_second == 0U ||
      limits.max_animation_channels_per_program == 0U ||
      !std::isfinite(limits.max_absolute_initial_float) ||
      !(limits.max_absolute_initial_float > 0.0F)) {
    fail("ActorBehaviorSceneV1 limits must all be positive and bounded");
  }
}

[[nodiscard]] bool key_character(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('_') ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('/');
}

void validate_key(const std::string_view value,
                  const std::uint32_t maximum_bytes,
                  const char *const description) {
  if (value.empty() || value.size() > maximum_bytes || value.front() == '/' ||
      value.back() == '/') {
    fail(std::string(description) + " is not a canonical semantic key");
  }
  for (const char character : value) {
    if (!key_character(static_cast<unsigned char>(character))) {
      fail(std::string(description) + " contains a non-canonical character");
    }
  }
  std::size_t component_begin = 0U;
  while (component_begin < value.size()) {
    const auto separator = value.find('/', component_begin);
    const auto component_end =
        separator == std::string_view::npos ? value.size() : separator;
    const auto component =
        value.substr(component_begin, component_end - component_begin);
    if (component.empty() || component == "." || component == "..") {
      fail(std::string(description) + " contains an unsafe component");
    }
    if (separator == std::string_view::npos) {
      break;
    }
    component_begin = separator + 1U;
  }
}

template <typename Item>
void require_dense_ids(const std::vector<Item> &items,
                       const char *const description) {
  for (std::size_t index = 0U; index < items.size(); ++index) {
    if (items[index].id != index) {
      fail(std::string("ActorBehaviorSceneV1 ") + description +
           " IDs are not canonical dense table indices");
    }
  }
}

template <typename Item> void sort_by_id(std::vector<Item> &items) {
  std::sort(
      items.begin(), items.end(),
      [](const Item &left, const Item &right) { return left.id < right.id; });
}

void require_count(const std::size_t count, const std::uint64_t limit,
                   const char *const description) {
  if (count > limit || count > std::numeric_limits<std::uint32_t>::max()) {
    fail(std::string("ActorBehaviorSceneV1 ") + description +
         " count exceeds its caller or format limit");
  }
}

[[nodiscard]] bool
valid_value_type(const ActorBehaviorValueTypeV1 value) noexcept {
  return value == ActorBehaviorValueTypeV1::boolean ||
         value == ActorBehaviorValueTypeV1::signed_integer ||
         value == ActorBehaviorValueTypeV1::unsigned_integer ||
         value == ActorBehaviorValueTypeV1::scalar_f32 ||
         value == ActorBehaviorValueTypeV1::vector3_f32 ||
         value == ActorBehaviorValueTypeV1::entity_reference;
}

[[nodiscard]] bool
valid_random_scope(const ActorBehaviorRandomScopeV1 value) noexcept {
  return value == ActorBehaviorRandomScopeV1::instance ||
         value == ActorBehaviorRandomScopeV1::level_shared ||
         value == ActorBehaviorRandomScopeV1::session_shared;
}

[[nodiscard]] float canonical_float(const float value,
                                    const float maximum_absolute,
                                    const char *const description) {
  if (!std::isfinite(value) || std::fabs(value) > maximum_absolute) {
    fail(std::string("ActorBehaviorSceneV1 has a non-finite or unbounded ") +
         description);
  }
  return value == 0.0F ? 0.0F : value;
}

void validate_float(const float value, const float maximum_absolute,
                    const char *const description) {
  if (!std::isfinite(value) || std::fabs(value) > maximum_absolute) {
    fail(std::string("ActorBehaviorSceneV1 has a non-finite or unbounded ") +
         description);
  }
  if (value == 0.0F && std::signbit(value)) {
    fail(std::string("ActorBehaviorSceneV1 has non-canonical signed zero in ") +
         description);
  }
}

void validate_initial_value(const ActorBehaviorInitialValueV1 &value,
                            const float maximum_absolute) {
  if (const auto scalar = std::get_if<float>(&value)) {
    validate_float(*scalar, maximum_absolute, "initial scalar");
    return;
  }
  if (const auto vector = std::get_if<std::array<float, 3U>>(&value)) {
    for (const float component : *vector) {
      validate_float(component, maximum_absolute, "initial vector component");
    }
  }
}

void canonicalize_initial_value(ActorBehaviorInitialValueV1 &value,
                                const float maximum_absolute) {
  if (auto scalar = std::get_if<float>(&value)) {
    *scalar = canonical_float(*scalar, maximum_absolute, "initial scalar");
    return;
  }
  if (auto vector = std::get_if<std::array<float, 3U>>(&value)) {
    for (float &component : *vector) {
      component = canonical_float(component, maximum_absolute,
                                  "initial vector component");
    }
  }
}

class DigestWriter final {
public:
  void append_bytes(const std::span<const std::byte> bytes) {
    hash_.update(bytes);
  }

  void append_u32(const std::uint32_t value) {
    const std::array bytes{
        static_cast<std::byte>(value & UINT32_C(0xff)),
        static_cast<std::byte>((value >> 8U) & UINT32_C(0xff)),
        static_cast<std::byte>((value >> 16U) & UINT32_C(0xff)),
        static_cast<std::byte>((value >> 24U) & UINT32_C(0xff))};
    append_bytes(bytes);
  }

  void append_string(const std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
      fail("ActorBehaviorSceneV1 layout key exceeds the digest width");
    }
    append_u32(static_cast<std::uint32_t>(value.size()));
    append_bytes(std::as_bytes(std::span(value.data(), value.size())));
  }

  [[nodiscard]] PreparedContentDigestV1 finish() { return hash_.finish(); }

private:
  Sha256 hash_;
};

void validate_program_contract_for_digest(
    const ActorBehaviorProgramV1 &program) {
  if (program.implementation_abi_version == 0U || program.state_count == 0U ||
      program.source_updates_per_second == 0U ||
      program.animation_channel_count == 0U ||
      program.fields.size() > std::numeric_limits<std::uint32_t>::max() ||
      program.animation_imports.size() >
          std::numeric_limits<std::uint32_t>::max() ||
      program.random_imports.size() >
          std::numeric_limits<std::uint32_t>::max() ||
      (program.flags & ~kActorBehaviorProgramKnownFlagsV1) != 0U ||
      is_zero_prepared_digest_v1(program.required_rig_sha256) ||
      is_zero_prepared_digest_v1(program.required_model_sha256)) {
    fail("ActorBehaviorSceneV1 program has an invalid intrinsic contract");
  }
  validate_key(program.required_rig_key, UINT32_MAX,
               "A required actor rig key");
  validate_key(program.required_model_key, UINT32_MAX,
               "A required actor model key");
  require_dense_ids(program.fields, "program field");
  require_dense_ids(program.animation_imports, "animation import");
  require_dense_ids(program.random_imports, "random import");

  std::set<std::string, std::less<>> field_keys;
  for (const auto &field : program.fields) {
    validate_key(field.semantic_key, UINT32_MAX, "A behavior field key");
    if (!valid_value_type(field.value_type) || field.element_count == 0U ||
        (field.flags & ~kActorBehaviorFieldKnownFlagsV1) != 0U ||
        (field.value_type != ActorBehaviorValueTypeV1::entity_reference &&
         field.flags != 0U) ||
        !field_keys.insert(field.semantic_key).second) {
      fail("ActorBehaviorSceneV1 program field contract is invalid");
    }
  }

  std::set<std::string, std::less<>> animation_bindings;
  for (const auto &animation : program.animation_imports) {
    validate_key(animation.binding_key, UINT32_MAX, "An animation binding key");
    validate_key(animation.clip_key, UINT32_MAX, "An animation clip key");
    if ((animation.flags & ~kActorBehaviorAnimationImportKnownFlagsV1) != 0U ||
        is_zero_prepared_digest_v1(animation.required_clip_sha256) ||
        animation.required_frame_count == 0U ||
        !animation_bindings.insert(animation.binding_key).second) {
      fail("ActorBehaviorSceneV1 animation import contract is invalid");
    }
  }

  std::set<std::string, std::less<>> random_bindings;
  std::set<std::string, std::less<>> instance_stream_keys;
  for (const auto &random : program.random_imports) {
    validate_key(random.binding_key, UINT32_MAX, "A random binding key");
    validate_key(random.stream_key, UINT32_MAX, "A random stream key");
    validate_key(random.algorithm_key, UINT32_MAX, "A random algorithm key");
    if (!valid_random_scope(random.scope) || random.state_word_count == 0U ||
        random.algorithm_abi_version == 0U ||
        (random.flags & ~kActorBehaviorRandomImportKnownFlagsV1) != 0U ||
        !random_bindings.insert(random.binding_key).second) {
      fail("ActorBehaviorSceneV1 random import contract is invalid");
    }
    if (random.scope == ActorBehaviorRandomScopeV1::instance &&
        !instance_stream_keys.insert(random.stream_key).second) {
      fail("ActorBehaviorSceneV1 program repeats an instance random stream "
           "key");
    }
  }
}

struct AggregateCounts {
  std::uint64_t fields = 0U;
  std::uint64_t field_elements = 0U;
  std::uint64_t animation_imports = 0U;
  std::uint64_t random_imports = 0U;
  std::uint64_t shared_random_words = 0U;
  std::uint64_t initial_values = 0U;
  std::uint64_t instance_random_words = 0U;
  std::uint64_t initial_animations = 0U;
  std::uint64_t semantic_key_bytes = 0U;
};

void add_key_bytes(AggregateCounts &counts, const std::string_view key,
                   const ActorBehaviorSceneLimitsV1 &limits,
                   const char *const description) {
  validate_key(key, limits.max_semantic_key_bytes, description);
  counts.semantic_key_bytes =
      checked_add(counts.semantic_key_bytes, key.size(),
                  "ActorBehaviorSceneV1 aggregate semantic-key bytes");
  if (counts.semantic_key_bytes > limits.max_total_semantic_key_bytes) {
    fail("ActorBehaviorSceneV1 exceeds its aggregate semantic-key limit");
  }
}

// Canonicalization owns its input, but must check the entire resource before
// copying tables or allocating replacement state. Validation at the end is
// deliberately stricter (IDs, types, contracts, ordering, and digests).
void validate_canonicalization_allocation_bounds(
    const ActorBehaviorSceneV1 &scene,
    const ActorBehaviorSceneLimitsV1 &limits) {
  require_count(scene.programs.size(), limits.max_programs, "program");
  require_count(scene.random_streams.size(), limits.max_random_streams,
                "random stream");
  require_count(scene.instances.size(), limits.max_instances, "instance");
  AggregateCounts counts;
  const auto add_bounded = [](std::uint64_t &total,
                              const std::uint64_t count,
                              const std::uint64_t maximum,
                              const char *const description) {
    total = checked_add(total, count, description);
    if (total > maximum) {
      fail(std::string(description) + " exceeds its caller limit");
    }
  };
  for (const auto &program : scene.programs) {
    require_count(program.fields.size(), limits.max_fields_per_program,
                  "per-program field");
    require_count(program.animation_imports.size(),
                  limits.max_animation_imports_per_program,
                  "per-program animation import");
    require_count(program.random_imports.size(),
                  limits.max_random_imports_per_program,
                  "per-program random import");
    add_bounded(counts.fields, program.fields.size(), limits.max_total_fields,
                "ActorBehaviorSceneV1 aggregate fields");
    add_bounded(counts.animation_imports, program.animation_imports.size(),
                limits.max_total_animation_imports,
                "ActorBehaviorSceneV1 aggregate animation imports");
    add_bounded(counts.random_imports, program.random_imports.size(),
                limits.max_total_random_imports,
                "ActorBehaviorSceneV1 aggregate random imports");
    for (const auto &key : {std::string_view(program.semantic_key),
                            std::string_view(program.implementation_key),
                            std::string_view(program.required_rig_key),
                            std::string_view(program.required_model_key)}) {
      add_key_bytes(counts, key, limits, "A behavior program key");
    }
    for (const auto &field : program.fields) {
      add_key_bytes(counts, field.semantic_key, limits, "A behavior field key");
      add_bounded(counts.field_elements, field.element_count,
                  limits.max_total_field_elements,
                  "ActorBehaviorSceneV1 aggregate field elements");
    }
    for (const auto &animation : program.animation_imports) {
      add_key_bytes(counts, animation.binding_key, limits,
                    "An animation binding key");
      add_key_bytes(counts, animation.clip_key, limits,
                    "An animation clip key");
    }
    for (const auto &random : program.random_imports) {
      add_key_bytes(counts, random.binding_key, limits, "A random binding key");
      add_key_bytes(counts, random.stream_key, limits, "A random stream key");
      add_key_bytes(counts, random.algorithm_key, limits,
                    "A random algorithm key");
    }
  }
  for (const auto &stream : scene.random_streams) {
    require_count(stream.initial_state_words.size(),
                  limits.max_random_state_words_per_stream,
                  "per-stream random state word");
    add_bounded(counts.shared_random_words, stream.initial_state_words.size(),
                limits.max_total_random_state_words,
                "ActorBehaviorSceneV1 aggregate shared random words");
    add_key_bytes(counts, stream.semantic_key, limits, "A random stream key");
    add_key_bytes(counts, stream.algorithm_key, limits,
                  "A random algorithm key");
  }
  for (const auto &instance : scene.instances) {
    require_count(instance.initial_values.size(),
                  limits.max_initial_values_per_instance,
                  "per-instance initial value");
    require_count(instance.initial_random_words.size(),
                  limits.max_initial_random_words_per_instance,
                  "per-instance random word");
    require_count(instance.initial_animations.size(),
                  limits.max_initial_animations_per_instance,
                  "per-instance initial animation");
    add_bounded(counts.initial_values, instance.initial_values.size(),
                limits.max_total_initial_values,
                "ActorBehaviorSceneV1 aggregate initial values");
    add_bounded(counts.instance_random_words,
                instance.initial_random_words.size(),
                limits.max_total_initial_random_words,
                "ActorBehaviorSceneV1 aggregate instance random words");
    add_bounded(counts.initial_animations, instance.initial_animations.size(),
                limits.max_total_initial_animations,
                "ActorBehaviorSceneV1 aggregate initial animations");
  }
}

[[nodiscard]] std::uint64_t
program_initial_value_count(const ActorBehaviorProgramV1 &program) {
  std::uint64_t result = 0U;
  for (const auto &field : program.fields) {
    result = checked_add(result, field.element_count,
                         "ActorBehaviorSceneV1 program field elements");
  }
  return result;
}

[[nodiscard]] std::uint64_t
program_instance_random_word_count(const ActorBehaviorProgramV1 &program) {
  std::uint64_t result = 0U;
  for (const auto &random : program.random_imports) {
    if (random.scope == ActorBehaviorRandomScopeV1::instance) {
      result =
          checked_add(result, random.state_word_count,
                      "ActorBehaviorSceneV1 per-instance random-state words");
    }
  }
  return result;
}

void canonicalize_program_tables_and_instance_state(
    ActorBehaviorSceneV1 &scene, ActorBehaviorProgramV1 &program,
    const ActorBehaviorSceneLimitsV1 &limits) {
  require_count(program.fields.size(), limits.max_fields_per_program,
                "per-program field");
  require_count(program.animation_imports.size(),
                limits.max_animation_imports_per_program,
                "per-program animation import");
  require_count(program.random_imports.size(),
                limits.max_random_imports_per_program,
                "per-program random import");
  auto old_fields = std::move(program.fields);
  auto canonical_fields = old_fields;
  sort_by_id(canonical_fields);
  require_dense_ids(canonical_fields, "program field");
  std::vector<std::size_t> canonical_field_offsets(canonical_fields.size());
  std::uint64_t value_count = 0U;
  for (const auto &field : canonical_fields) {
    if (field.element_count == 0U ||
        field.element_count > limits.max_elements_per_field) {
      fail("ActorBehaviorSceneV1 cannot canonicalize an invalid field "
           "element count");
    }
    canonical_field_offsets[field.id] = static_cast<std::size_t>(value_count);
    value_count = checked_add(value_count, field.element_count,
                              "ActorBehaviorSceneV1 program field elements");
    if (value_count > limits.max_initial_values_per_instance ||
        value_count > std::numeric_limits<std::size_t>::max()) {
      fail("ActorBehaviorSceneV1 program field elements exceed an instance "
           "limit");
    }
  }

  auto old_random_imports = std::move(program.random_imports);
  auto canonical_random_imports = old_random_imports;
  sort_by_id(canonical_random_imports);
  require_dense_ids(canonical_random_imports, "random import");
  std::vector<std::size_t> canonical_random_offsets(
      canonical_random_imports.size(), std::numeric_limits<std::size_t>::max());
  std::uint64_t random_word_count = 0U;
  for (const auto &random : canonical_random_imports) {
    if (!valid_random_scope(random.scope) || random.state_word_count == 0U ||
        random.state_word_count > limits.max_random_state_words_per_stream) {
      fail("ActorBehaviorSceneV1 cannot canonicalize an invalid random "
           "import layout");
    }
    if (random.scope == ActorBehaviorRandomScopeV1::instance) {
      canonical_random_offsets[random.id] =
          static_cast<std::size_t>(random_word_count);
      random_word_count =
          checked_add(random_word_count, random.state_word_count,
                      "ActorBehaviorSceneV1 per-instance random-state words");
      if (random_word_count > limits.max_initial_random_words_per_instance ||
          random_word_count > std::numeric_limits<std::size_t>::max()) {
        fail("ActorBehaviorSceneV1 program random state exceeds an instance "
             "limit");
      }
    }
  }

  for (auto &instance : scene.instances) {
    if (instance.program_id != program.id) {
      continue;
    }
    if (instance.initial_values.size() != value_count ||
        instance.initial_random_words.size() != random_word_count) {
      fail("ActorBehaviorSceneV1 cannot canonicalize incomplete instance "
           "state");
    }

    std::vector<ActorBehaviorInitialValueV1> canonical_values(
        static_cast<std::size_t>(value_count));
    std::size_t source_value = 0U;
    for (const auto &field : old_fields) {
      auto destination = canonical_field_offsets[field.id];
      for (std::uint32_t element = 0U; element < field.element_count;
           ++element) {
        canonical_values[destination++] =
            std::move(instance.initial_values[source_value++]);
      }
    }
    instance.initial_values = std::move(canonical_values);

    std::vector<std::uint32_t> canonical_random_words(
        static_cast<std::size_t>(random_word_count));
    std::size_t source_word = 0U;
    for (const auto &random : old_random_imports) {
      if (random.scope != ActorBehaviorRandomScopeV1::instance) {
        continue;
      }
      auto destination = canonical_random_offsets[random.id];
      for (std::uint32_t word = 0U; word < random.state_word_count; ++word) {
        canonical_random_words[destination++] =
            instance.initial_random_words[source_word++];
      }
    }
    instance.initial_random_words = std::move(canonical_random_words);
  }

  program.fields = std::move(canonical_fields);
  sort_by_id(program.animation_imports);
  program.random_imports = std::move(canonical_random_imports);
}

} // namespace

ActorBehaviorValueTypeV1 actor_behavior_initial_value_type_v1(
    const ActorBehaviorInitialValueV1 &value) noexcept {
  if (std::holds_alternative<bool>(value)) {
    return ActorBehaviorValueTypeV1::boolean;
  }
  if (std::holds_alternative<std::int32_t>(value)) {
    return ActorBehaviorValueTypeV1::signed_integer;
  }
  if (std::holds_alternative<std::uint32_t>(value)) {
    return ActorBehaviorValueTypeV1::unsigned_integer;
  }
  if (std::holds_alternative<float>(value)) {
    return ActorBehaviorValueTypeV1::scalar_f32;
  }
  if (std::holds_alternative<std::array<float, 3U>>(value)) {
    return ActorBehaviorValueTypeV1::vector3_f32;
  }
  return ActorBehaviorValueTypeV1::entity_reference;
}

PreparedContentDigestV1
actor_behavior_program_layout_sha256_v1(const ActorBehaviorProgramV1 &program) {
  validate_program_contract_for_digest(program);
  DigestWriter writer;
  writer.append_string("openrc.actor-behavior-program-layout.v1");
  writer.append_u32(kActorBehaviorSceneSchemaVersionV1);
  writer.append_u32(program.implementation_abi_version);
  writer.append_string(program.required_rig_key);
  writer.append_bytes(program.required_rig_sha256);
  writer.append_string(program.required_model_key);
  writer.append_bytes(program.required_model_sha256);
  writer.append_u32(program.state_count);
  writer.append_u32(program.source_updates_per_second);
  writer.append_u32(program.animation_channel_count);

  writer.append_u32(static_cast<std::uint32_t>(program.fields.size()));
  for (const auto &field : program.fields) {
    writer.append_u32(field.id);
    writer.append_u32(static_cast<std::uint32_t>(field.value_type));
    writer.append_u32(field.element_count);
    writer.append_u32(field.flags);
    writer.append_string(field.semantic_key);
  }

  writer.append_u32(
      static_cast<std::uint32_t>(program.animation_imports.size()));
  for (const auto &animation : program.animation_imports) {
    writer.append_u32(animation.id);
    writer.append_u32(animation.flags);
    writer.append_string(animation.binding_key);
    writer.append_string(animation.clip_key);
    writer.append_bytes(animation.required_clip_sha256);
    writer.append_u32(animation.required_frame_count);
  }

  writer.append_u32(static_cast<std::uint32_t>(program.random_imports.size()));
  for (const auto &random : program.random_imports) {
    writer.append_u32(random.id);
    writer.append_u32(random.flags);
    writer.append_string(random.binding_key);
    writer.append_string(random.stream_key);
    writer.append_string(random.algorithm_key);
    writer.append_u32(random.algorithm_abi_version);
    writer.append_u32(static_cast<std::uint32_t>(random.scope));
    writer.append_u32(random.state_word_count);
  }
  return writer.finish();
}

void validate_actor_behavior_scene_v1(const ActorBehaviorSceneV1 &scene,
                                      const ActorBehaviorSceneLimitsV1 limits) {
  validate_limits(limits);
  if (scene.schema_version != kActorBehaviorSceneSchemaVersionV1) {
    fail("ActorBehaviorSceneV1 has an unknown schema version");
  }
  require_count(scene.programs.size(), limits.max_programs, "program");
  require_count(scene.random_streams.size(), limits.max_random_streams,
                "random stream");
  require_count(scene.instances.size(), limits.max_instances, "instance");
  require_dense_ids(scene.programs, "program");
  require_dense_ids(scene.random_streams, "random stream");

  AggregateCounts counts;
  std::map<std::string, const ActorBehaviorRandomStreamV1 *, std::less<>>
      streams_by_key;
  std::map<std::string, bool, std::less<>> stream_used;
  using RandomContract = std::tuple<std::string, std::uint32_t,
                                    ActorBehaviorRandomScopeV1, std::uint32_t>;
  std::map<std::string, RandomContract, std::less<>> random_contracts;
  for (const auto &stream : scene.random_streams) {
    add_key_bytes(counts, stream.semantic_key, limits, "A random stream key");
    add_key_bytes(counts, stream.algorithm_key, limits,
                  "A random algorithm key");
    if (!streams_by_key.emplace(stream.semantic_key, &stream).second) {
      fail("ActorBehaviorSceneV1 repeats a random stream key");
    }
    if (stream.scope != ActorBehaviorRandomScopeV1::level_shared &&
        stream.scope != ActorBehaviorRandomScopeV1::session_shared) {
      fail("ActorBehaviorSceneV1 scene random stream is not shared");
    }
    if ((stream.flags & ~kActorBehaviorRandomStreamKnownFlagsV1) != 0U ||
        stream.algorithm_abi_version == 0U ||
        stream.algorithm_abi_version >
            limits.max_random_algorithm_abi_version ||
        stream.initial_state_words.empty() ||
        stream.initial_state_words.size() >
            limits.max_random_state_words_per_stream) {
      fail("ActorBehaviorSceneV1 random stream contract is invalid");
    }
    counts.shared_random_words = checked_add(
        counts.shared_random_words, stream.initial_state_words.size(),
        "ActorBehaviorSceneV1 shared random-state words");
    if (counts.shared_random_words > limits.max_total_random_state_words) {
      fail("ActorBehaviorSceneV1 exceeds its shared random-state word limit");
    }
    stream_used.emplace(stream.semantic_key, false);
  }

  std::set<std::string, std::less<>> program_keys;
  for (const auto &program : scene.programs) {
    add_key_bytes(counts, program.semantic_key, limits,
                  "A behavior program key");
    add_key_bytes(counts, program.implementation_key, limits,
                  "A behavior implementation key");
    add_key_bytes(counts, program.required_rig_key, limits,
                  "A required actor rig key");
    add_key_bytes(counts, program.required_model_key, limits,
                  "A required actor model key");
    if (!program_keys.insert(program.semantic_key).second) {
      fail("ActorBehaviorSceneV1 repeats a program semantic key");
    }
    if (program.implementation_abi_version == 0U ||
        program.implementation_abi_version >
            limits.max_implementation_abi_version ||
        program.state_count == 0U ||
        program.state_count > limits.max_states_per_program ||
        program.source_updates_per_second == 0U ||
        program.source_updates_per_second >
            limits.max_source_updates_per_second ||
        program.animation_channel_count == 0U ||
        program.animation_channel_count >
            limits.max_animation_channels_per_program ||
        (program.flags & ~kActorBehaviorProgramKnownFlagsV1) != 0U ||
        is_zero_prepared_digest_v1(program.required_rig_sha256) ||
        is_zero_prepared_digest_v1(program.required_model_sha256)) {
      fail("ActorBehaviorSceneV1 program envelope is invalid");
    }
    require_count(program.fields.size(), limits.max_fields_per_program,
                  "per-program field");
    require_count(program.animation_imports.size(),
                  limits.max_animation_imports_per_program,
                  "per-program animation import");
    require_count(program.random_imports.size(),
                  limits.max_random_imports_per_program,
                  "per-program random import");
    require_dense_ids(program.fields, "program field");
    require_dense_ids(program.animation_imports, "animation import");
    require_dense_ids(program.random_imports, "random import");

    counts.fields = checked_add(counts.fields, program.fields.size(),
                                "ActorBehaviorSceneV1 fields");
    counts.animation_imports =
        checked_add(counts.animation_imports, program.animation_imports.size(),
                    "ActorBehaviorSceneV1 animation imports");
    counts.random_imports =
        checked_add(counts.random_imports, program.random_imports.size(),
                    "ActorBehaviorSceneV1 random imports");
    if (counts.fields > limits.max_total_fields ||
        counts.animation_imports > limits.max_total_animation_imports ||
        counts.random_imports > limits.max_total_random_imports) {
      fail("ActorBehaviorSceneV1 exceeds an aggregate program-table limit");
    }

    std::set<std::string, std::less<>> field_keys;
    std::uint64_t program_field_elements = 0U;
    for (const auto &field : program.fields) {
      add_key_bytes(counts, field.semantic_key, limits, "A behavior field key");
      if (!valid_value_type(field.value_type) || field.element_count == 0U ||
          field.element_count > limits.max_elements_per_field ||
          (field.flags & ~kActorBehaviorFieldKnownFlagsV1) != 0U ||
          (field.value_type != ActorBehaviorValueTypeV1::entity_reference &&
           field.flags != 0U) ||
          !field_keys.insert(field.semantic_key).second) {
        fail("ActorBehaviorSceneV1 program field is invalid");
      }
      counts.field_elements =
          checked_add(counts.field_elements, field.element_count,
                      "ActorBehaviorSceneV1 aggregate field elements");
      program_field_elements =
          checked_add(program_field_elements, field.element_count,
                      "ActorBehaviorSceneV1 per-program field elements");
      if (counts.field_elements > limits.max_total_field_elements) {
        fail("ActorBehaviorSceneV1 exceeds its field-element limit");
      }
    }
    if (program_field_elements > limits.max_initial_values_per_instance) {
      fail("ActorBehaviorSceneV1 program state cannot fit one instance");
    }

    std::set<std::string, std::less<>> animation_bindings;
    for (const auto &animation : program.animation_imports) {
      add_key_bytes(counts, animation.binding_key, limits,
                    "An animation binding key");
      add_key_bytes(counts, animation.clip_key, limits,
                    "An animation clip key");
      if ((animation.flags & ~kActorBehaviorAnimationImportKnownFlagsV1) !=
              0U ||
          is_zero_prepared_digest_v1(animation.required_clip_sha256) ||
          animation.required_frame_count == 0U ||
          !animation_bindings.insert(animation.binding_key).second) {
        fail("ActorBehaviorSceneV1 animation import is invalid");
      }
    }

    std::set<std::string, std::less<>> random_bindings;
    std::set<std::string, std::less<>> instance_stream_keys;
    std::uint64_t program_instance_random_words = 0U;
    for (const auto &random : program.random_imports) {
      add_key_bytes(counts, random.binding_key, limits, "A random binding key");
      add_key_bytes(counts, random.stream_key, limits, "A random stream key");
      add_key_bytes(counts, random.algorithm_key, limits,
                    "A random algorithm key");
      if (!valid_random_scope(random.scope) || random.state_word_count == 0U ||
          random.state_word_count > limits.max_random_state_words_per_stream ||
          random.algorithm_abi_version == 0U ||
          random.algorithm_abi_version >
              limits.max_random_algorithm_abi_version ||
          (random.flags & ~kActorBehaviorRandomImportKnownFlagsV1) != 0U ||
          !random_bindings.insert(random.binding_key).second) {
        fail("ActorBehaviorSceneV1 random import is invalid");
      }
      const auto found = streams_by_key.find(random.stream_key);
      if (random.scope == ActorBehaviorRandomScopeV1::instance) {
        if (!instance_stream_keys.insert(random.stream_key).second) {
          fail("ActorBehaviorSceneV1 program repeats an instance random "
               "stream key");
        }
        program_instance_random_words = checked_add(
            program_instance_random_words, random.state_word_count,
            "ActorBehaviorSceneV1 per-program instance random words");
        if (program_instance_random_words >
            limits.max_initial_random_words_per_instance) {
          fail("ActorBehaviorSceneV1 random state cannot fit one instance");
        }
        if (found != streams_by_key.end()) {
          fail("ActorBehaviorSceneV1 instance random import collides with a "
               "shared stream");
        }
      } else {
        const RandomContract contract{
            random.algorithm_key, random.algorithm_abi_version, random.scope,
            random.state_word_count};
        const auto [contract_it, inserted] =
            random_contracts.emplace(random.stream_key, contract);
        if (!inserted && contract_it->second != contract) {
          fail("ActorBehaviorSceneV1 gives one shared random stream key "
               "conflicting contracts");
        }
        if (found == streams_by_key.end()) {
          fail("ActorBehaviorSceneV1 shared random import has no stream");
        }
        const auto &stream = *found->second;
        if (stream.algorithm_key != random.algorithm_key ||
            stream.algorithm_abi_version != random.algorithm_abi_version ||
            stream.scope != random.scope ||
            stream.initial_state_words.size() != random.state_word_count) {
          fail("ActorBehaviorSceneV1 shared random import contract does not "
               "match its stream");
        }
        stream_used.at(stream.semantic_key) = true;
      }
    }

    const auto expected_digest =
        actor_behavior_program_layout_sha256_v1(program);
    if (is_zero_prepared_digest_v1(program.state_layout_sha256) ||
        program.state_layout_sha256 != expected_digest) {
      fail("ActorBehaviorSceneV1 program layout digest is absent or stale");
    }
  }

  for (const auto &[key, used] : stream_used) {
    static_cast<void>(key);
    if (!used) {
      fail("ActorBehaviorSceneV1 contains an orphan shared random stream");
    }
  }

  for (std::size_t index = 0U; index < scene.instances.size(); ++index) {
    const auto &instance = scene.instances[index];
    if (index != 0U &&
        scene.instances[index - 1U].authored_id >= instance.authored_id) {
      fail("ActorBehaviorSceneV1 instance authored IDs are duplicate or out "
           "of order");
    }
    if (instance.program_id >= scene.programs.size() ||
        (instance.flags & ~kActorBehaviorInstanceKnownFlagsV1) != 0U) {
      fail("ActorBehaviorSceneV1 instance envelope is invalid");
    }
    const auto &program = scene.programs[instance.program_id];
    if (instance.initial_state_id >= program.state_count ||
        instance.initial_animations.size() >
            limits.max_initial_animations_per_instance) {
      fail("ActorBehaviorSceneV1 instance initial state is invalid");
    }
    const auto expected_values = program_initial_value_count(program);
    const auto expected_random_words =
        program_instance_random_word_count(program);
    if (instance.initial_values.size() != expected_values ||
        instance.initial_values.size() >
            limits.max_initial_values_per_instance ||
        instance.initial_random_words.size() != expected_random_words ||
        instance.initial_random_words.size() >
            limits.max_initial_random_words_per_instance) {
      fail("ActorBehaviorSceneV1 instance does not provide complete initial "
           "state");
    }
    counts.initial_values =
        checked_add(counts.initial_values, instance.initial_values.size(),
                    "ActorBehaviorSceneV1 aggregate initial values");
    counts.instance_random_words = checked_add(
        counts.instance_random_words, instance.initial_random_words.size(),
        "ActorBehaviorSceneV1 aggregate per-instance random words");
    counts.initial_animations = checked_add(
        counts.initial_animations, instance.initial_animations.size(),
        "ActorBehaviorSceneV1 aggregate initial animations");
    if (counts.initial_values > limits.max_total_initial_values ||
        counts.instance_random_words > limits.max_total_initial_random_words ||
        counts.initial_animations > limits.max_total_initial_animations) {
      fail("ActorBehaviorSceneV1 exceeds an aggregate instance-state limit");
    }

    for (std::size_t animation_index = 0U;
         animation_index < instance.initial_animations.size();
         ++animation_index) {
      const auto &animation = instance.initial_animations[animation_index];
      if ((animation_index != 0U &&
           instance.initial_animations[animation_index - 1U].channel_id >=
               animation.channel_id) ||
          animation.channel_id >= program.animation_channel_count ||
          animation.animation_import_id >= program.animation_imports.size() ||
          (animation.flags & ~kActorBehaviorInitialAnimationKnownFlagsV1) !=
              0U) {
        fail("ActorBehaviorSceneV1 initial animations are invalid or not in "
             "canonical channel order");
      }
      if (animation.first_frame_index >=
          program.animation_imports[animation.animation_import_id]
              .required_frame_count) {
        fail("ActorBehaviorSceneV1 initial animation frame exceeds its "
             "import contract");
      }
    }

    std::size_t value_index = 0U;
    for (const auto &field : program.fields) {
      for (std::uint32_t element = 0U; element < field.element_count;
           ++element) {
        static_cast<void>(element);
        const auto &value = instance.initial_values[value_index++];
        if (actor_behavior_initial_value_type_v1(value) != field.value_type) {
          fail("ActorBehaviorSceneV1 initial value type does not match its "
               "field");
        }
        validate_initial_value(value, limits.max_absolute_initial_float);
      }
    }
  }
}

ActorBehaviorSceneV1
canonicalize_actor_behavior_scene_v1(ActorBehaviorSceneV1 scene,
                                     const ActorBehaviorSceneLimitsV1 limits) {
  validate_limits(limits);
  if (scene.schema_version != kActorBehaviorSceneSchemaVersionV1) {
    fail("ActorBehaviorSceneV1 has an unknown schema version");
  }
  validate_canonicalization_allocation_bounds(scene, limits);
  sort_by_id(scene.programs);
  sort_by_id(scene.random_streams);
  std::sort(scene.instances.begin(), scene.instances.end(),
            [](const ActorBehaviorInstanceV1 &left,
               const ActorBehaviorInstanceV1 &right) {
              return left.authored_id < right.authored_id;
            });
  require_dense_ids(scene.programs, "program");
  for (auto &program : scene.programs) {
    canonicalize_program_tables_and_instance_state(scene, program, limits);
    const auto digest = actor_behavior_program_layout_sha256_v1(program);
    if (!is_zero_prepared_digest_v1(program.state_layout_sha256) &&
        program.state_layout_sha256 != digest) {
      fail("ActorBehaviorSceneV1 program supplied a stale layout digest");
    }
    program.state_layout_sha256 = digest;
  }
  for (auto &instance : scene.instances) {
    std::sort(instance.initial_animations.begin(),
              instance.initial_animations.end(),
              [](const ActorBehaviorInitialAnimationV1 &left,
                 const ActorBehaviorInitialAnimationV1 &right) {
                return left.channel_id < right.channel_id;
              });
    for (auto &value : instance.initial_values) {
      canonicalize_initial_value(value, limits.max_absolute_initial_float);
    }
  }
  validate_actor_behavior_scene_v1(scene, limits);
  return scene;
}

} // namespace openrc
