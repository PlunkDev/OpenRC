#include "openrc/runtime_actor_behavior.hpp"
#include "openrc/runtime_actor_schedule.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr std::string_view kImplementationKey = "openrc.test.synthetic-counter";
constexpr std::string_view kRandomAlgorithmKey = "openrc.test.increment-u32";
constexpr std::array<std::uint32_t, 2U> kAuthoredSchedule{10U, 20U};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_runtime_error(Callback &&callback, const std::string_view needle,
                          const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::ActorBehaviorRuntimeError &error) {
    if (std::string_view(error.what()).find(needle) != std::string_view::npos) {
      return;
    }
    throw std::runtime_error(message + ": wrong diagnostic: " + error.what());
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::PreparedContentDigestV1 digest(const std::uint8_t seed) {
  openrc::PreparedContentDigestV1 result{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = std::byte{static_cast<std::uint8_t>(seed + index)};
  }
  return result;
}

[[nodiscard]] constexpr openrc::ActorBehaviorSceneLimitsV1 scene_limits() {
  return {
      .max_programs = 4U,
      .max_fields_per_program = 8U,
      .max_total_fields = 16U,
      .max_elements_per_field = 8U,
      .max_total_field_elements = 64U,
      .max_animation_imports_per_program = 8U,
      .max_total_animation_imports = 16U,
      .max_random_imports_per_program = 8U,
      .max_total_random_imports = 16U,
      .max_random_streams = 8U,
      .max_random_state_words_per_stream = 8U,
      .max_total_random_state_words = 32U,
      .max_instances = 16U,
      .max_initial_values_per_instance = 32U,
      .max_total_initial_values = 128U,
      .max_initial_random_words_per_instance = 16U,
      .max_total_initial_random_words = 64U,
      .max_initial_animations_per_instance = 8U,
      .max_total_initial_animations = 32U,
      .max_semantic_key_bytes = 128U,
      .max_total_semantic_key_bytes = 4096U,
      .max_implementation_abi_version = 16U,
      .max_random_algorithm_abi_version = 16U,
      .max_states_per_program = 16U,
      .max_source_updates_per_second = 120U,
      .max_animation_channels_per_program = 8U,
      .max_absolute_initial_float = 10000.0F,
  };
}

[[nodiscard]] constexpr openrc::game::ActorBehaviorRuntimeLimitsV1
runtime_limits(const std::uint32_t max_journal = 16U) {
  return {scene_limits(), max_journal};
}

[[nodiscard]] openrc::ActorBehaviorSceneV1 make_scene() {
  openrc::ActorBehaviorProgramV1 program;
  program.id = 0U;
  program.semantic_key = "actors/test/synthetic-counter";
  program.implementation_key = std::string(kImplementationKey);
  program.required_rig_key = "actors/test/rig";
  program.required_rig_sha256 = digest(7U);
  program.required_model_key = "actors/test/model";
  program.required_model_sha256 = digest(23U);
  program.implementation_abi_version = 1U;
  program.state_count = 2U;
  program.source_updates_per_second = 60U;
  program.animation_channel_count = 2U;
  program.fields = {
      {0U, "counter", openrc::ActorBehaviorValueTypeV1::unsigned_integer, 1U,
       0U},
      {1U, "switches", openrc::ActorBehaviorValueTypeV1::boolean, 2U, 0U},
  };
  openrc::ActorBehaviorAnimationImportV1 entry_animation;
  entry_animation.id = 0U;
  entry_animation.binding_key = "entry";
  entry_animation.clip_key = "animations/test/entry";
  entry_animation.required_clip_sha256 = digest(41U);
  entry_animation.required_frame_count = 16U;
  openrc::ActorBehaviorAnimationImportV1 alternate_animation;
  alternate_animation.id = 1U;
  alternate_animation.binding_key = "alternate";
  alternate_animation.clip_key = "animations/test/alternate";
  alternate_animation.required_clip_sha256 = digest(42U);
  alternate_animation.required_frame_count = 16U;
  program.animation_imports = {std::move(entry_animation),
                               std::move(alternate_animation)};
  program.random_imports = {
      {0U, "shared", "random/test/level", std::string(kRandomAlgorithmKey), 1U,
       openrc::ActorBehaviorRandomScopeV1::level_shared, 1U, 0U},
      {1U, "local", "random/test/instance", std::string(kRandomAlgorithmKey),
       1U, openrc::ActorBehaviorRandomScopeV1::instance, 1U, 0U},
  };

  openrc::ActorBehaviorSceneV1 scene;
  scene.level_id = 7U;
  scene.programs = {std::move(program)};
  scene.random_streams = {
      {0U,
       "random/test/level",
       std::string(kRandomAlgorithmKey),
       1U,
       openrc::ActorBehaviorRandomScopeV1::level_shared,
       {10U},
       0U},
  };
  openrc::ActorBehaviorInstanceV1 first;
  first.authored_id = 10U;
  first.program_id = 0U;
  first.initial_state_id = 0U;
  first.initial_values = {std::uint32_t{0U}, true, false};
  first.initial_random_words = {100U};
  first.initial_animations = {{0U, 0U, 3U, 0U}};
  openrc::ActorBehaviorInstanceV1 second;
  second.authored_id = 20U;
  second.program_id = 0U;
  second.initial_state_id = 1U;
  second.initial_values = {std::uint32_t{0U}, true, false};
  second.initial_random_words = {100U};
  second.initial_animations = {
      {0U, 1U, 8U, 0U},
      {1U, 0U, 1U, 0U},
  };
  scene.instances = {std::move(first), std::move(second)};
  return openrc::canonicalize_actor_behavior_scene_v1(std::move(scene),
                                                      scene_limits());
}

[[nodiscard]] openrc::game::ActorBehaviorImplementationIdentityV1
identity_for(const openrc::ActorBehaviorProgramV1 &program) {
  return {
      program.implementation_key,
      program.implementation_abi_version,
      program.state_layout_sha256,
  };
}

[[nodiscard]] openrc::game::ActorBehaviorRuntimeRegistryV1
make_registry(const openrc::ActorBehaviorSceneV1 &scene,
              openrc::game::ActorBehaviorFixedTickCallbackV1 callback) {
  openrc::game::ActorBehaviorRuntimeRegistryV1 registry;
  registry.register_random_algorithm({
      std::string(kRandomAlgorithmKey),
      1U,
      1U,
      [](const std::span<std::uint32_t> words) {
        if (words.size() != 1U) {
          throw std::runtime_error("synthetic RNG received the wrong shape");
        }
        ++words[0U];
        return words[0U];
      },
  });
  registry.register_implementation({
      identity_for(scene.programs.front()),
      std::move(callback),
  });
  return registry;
}

[[nodiscard]] openrc::game::ActorBehaviorFixedTickCallbackV1
synthetic_behavior() {
  return [](openrc::game::ActorBehaviorInvocationV1 &invocation) {
    const auto shared = invocation.next_random_u32(0U);
    const auto local = invocation.next_random_u32(1U);
    const auto counter = std::get<std::uint32_t>(invocation.field(0U));
    invocation.set_field(0U, 0U, counter + shared + local);
    invocation.set_field(1U, 1U, !std::get<bool>(invocation.field(1U, 1U)));

    if (invocation.current_state_id() == 0U) {
      expect(invocation.state_ticks() == 0U,
             "synthetic initial state did not start at tick zero");
      invocation.emit_set_animation(0U, 0U, 3U, 7U);
      invocation.transition_to_state(1U);
    }
    invocation.emit_set_presentation_enabled(
        !invocation.presentation_enabled());
  };
}

void test_exact_registry_shared_rng_state_and_journal() {
  const auto scene = make_scene();
  auto registry = make_registry(scene, synthetic_behavior());
  const std::vector<openrc::game::ActorBehaviorInitialPresentationEnabledV1>
      presentation_enabled{
          {10U, true},
          {20U, false},
      };
  openrc::game::ActorBehaviorRuntimeV1 runtime(
      scene, registry, runtime_limits(), 60U, 4U,
      std::span<const openrc::game::ActorBehaviorInitialPresentationEnabledV1>(
          presentation_enabled));

  const auto before = runtime.snapshot();
  openrc::game::ActorBehaviorRuntimeV1 same_initialization(
      scene, registry, runtime_limits(), 60U, 4U,
      std::span<const openrc::game::ActorBehaviorInitialPresentationEnabledV1>(
          presentation_enabled));
  const auto &initialization = runtime.initialization_journal();
  expect(initialization == same_initialization.initialization_journal() &&
             initialization.size() == 5U,
         "initialization journal is not deterministic or complete");
  expect(
      initialization[0U].authored_id == 10U &&
          std::get<openrc::game::ActorBehaviorSetPresentationEnabledCommandV1>(
              initialization[0U].payload)
              .presentation_enabled &&
          initialization[1U].authored_id == 10U &&
          initialization[2U].authored_id == 20U &&
          !std::get<openrc::game::ActorBehaviorSetPresentationEnabledCommandV1>(
               initialization[2U].payload)
               .presentation_enabled,
      "initialization journal lost authored presentation order");
  const auto &second_initial_animation =
      std::get<openrc::game::ActorBehaviorSetAnimationCommandV1>(
          initialization[3U].payload);
  expect(second_initial_animation.channel_id == 0U &&
             second_initial_animation.animation_import_id == 1U &&
             second_initial_animation.clip_key == "animations/test/alternate" &&
             second_initial_animation.required_clip_sha256 == digest(42U) &&
             second_initial_animation.first_frame_index == 8U &&
             second_initial_animation.transition_source_update_count == 0U &&
             std::get<openrc::game::ActorBehaviorSetAnimationCommandV1>(
                 initialization[4U].payload)
                     .channel_id == 1U,
         "initialization journal lost clip identity, frame, or channel order");
  expect(runtime.snapshot() == before && runtime.next_tick_index() == 4U,
         "reading initialization commands advanced behavior state");
  expect(before.next_tick_index == 4U &&
             before.instances[0U].presentation_enabled &&
             !before.instances[1U].presentation_enabled &&
             before.instances[0U].current_state_id == 0U &&
             before.instances[1U].current_state_id == 1U,
         "runtime lost per-instance initial state or presentation");
  const auto tick = runtime.fixed_tick(4U, kAuthoredSchedule);
  const auto after = runtime.snapshot();

  expect(tick.tick_index == 4U && tick.journal.size() == 3U,
         "synthetic behavior did not emit its canonical command journal");
  expect(tick.journal[0U].ordinal == 0U &&
             tick.journal[0U].authored_id == 10U &&
             std::holds_alternative<
                 openrc::game::ActorBehaviorSetAnimationCommandV1>(
                 tick.journal[0U].payload) &&
             tick.journal[2U].authored_id == 20U,
         "behavior journal lost authored invocation order");
  const auto &animation =
      std::get<openrc::game::ActorBehaviorSetAnimationCommandV1>(
          tick.journal[0U].payload);
  expect(animation.clip_key == "animations/test/entry" &&
             animation.channel_id == 0U &&
             animation.animation_import_id == 0U &&
             animation.required_clip_sha256 == digest(41U) &&
             animation.first_frame_index == 3U &&
             animation.transition_source_update_count == 7U,
         "animation journal did not resolve the exact neutral import");

  expect(after.next_tick_index == 5U &&
             after.shared_random_streams[0U].state_words ==
                 std::vector<std::uint32_t>{12U} &&
             after.shared_random_streams[0U].call_count == 2U,
         "level-shared RNG was not consumed once in authored actor order");
  expect(std::get<std::uint32_t>(after.instances[0U].field_values[0U]) ==
                 112U &&
             std::get<std::uint32_t>(after.instances[1U].field_values[0U]) ==
                 113U &&
             after.instances[0U].instance_random_words ==
                 std::vector<std::uint32_t>{101U} &&
             after.instances[1U].instance_random_words ==
                 std::vector<std::uint32_t>{101U},
         "instance state or per-instance RNG did not advance independently");
  expect(after.instances[0U].current_state_id == 1U &&
             after.instances[1U].current_state_id == 1U &&
             after.instances[0U].state_ticks == 0U &&
             after.instances[1U].state_ticks == 1U &&
             after.instances[0U].random_call_count == 2U &&
             after.instances[1U].random_call_count == 2U &&
             !after.instances[0U].presentation_enabled &&
             after.instances[1U].presentation_enabled,
         "transition, trace counters, or presentation state drifted");

  static_cast<void>(runtime.fixed_tick(5U, kAuthoredSchedule));
  const auto second = runtime.snapshot();
  expect(second.instances[0U].state_ticks == 1U &&
             second.instances[1U].state_ticks == 2U &&
             second.instances[0U].random_call_count == 4U &&
             second.shared_random_streams[0U].call_count == 4U,
         "a stable behavior state did not advance its trace counters");
}

void test_snapshot_restore_and_deterministic_hash() {
  const auto scene = make_scene();
  auto registry = make_registry(scene, synthetic_behavior());
  openrc::game::ActorBehaviorRuntimeV1 first(scene, registry, runtime_limits(),
                                             60U);
  openrc::game::ActorBehaviorRuntimeV1 second(scene, registry, runtime_limits(),
                                              60U);

  expect(first.deterministic_hash() == second.deterministic_hash(),
         "identical behavior runtimes began with different hashes");
  const auto first_tick = first.fixed_tick(0U, kAuthoredSchedule);
  const auto second_tick = second.fixed_tick(0U, kAuthoredSchedule);
  expect(first_tick == second_tick && first.snapshot() == second.snapshot() &&
             first.deterministic_hash() == second.deterministic_hash(),
         "identical behavior replay produced different state or journals");

  const auto saved = first.snapshot();
  openrc::game::ActorBehaviorRuntimeV1 restored(scene, registry,
                                                runtime_limits(), 60U, saved);
  expect(restored.snapshot() == saved &&
             restored.deterministic_hash() == first.deterministic_hash() &&
             restored.initialization_journal().empty(),
         "behavior snapshot restore changed state or restarted initial "
         "presentation");
  const auto continued = first.fixed_tick(1U, kAuthoredSchedule);
  const auto restored_continued = restored.fixed_tick(1U, kAuthoredSchedule);
  expect(continued == restored_continued &&
             first.snapshot() == restored.snapshot(),
         "restored behavior replay diverged on its next tick");

  auto changed = restored.snapshot();
  ++changed.instances[0U].state_ticks;
  expect(openrc::game::hash_actor_behavior_runtime_snapshot_v1(changed) !=
             restored.deterministic_hash(),
         "behavior hash omitted per-state trace progress");
}

void test_failed_callback_rolls_back_every_staged_value() {
  const auto scene = make_scene();
  auto registry = make_registry(
      scene, [](openrc::game::ActorBehaviorInvocationV1 &invocation) {
        invocation.set_field(0U, 0U, std::uint32_t{999U});
        static_cast<void>(invocation.next_random_u32(0U));
        static_cast<void>(invocation.next_random_u32(1U));
        invocation.transition_to_state(1U);
        invocation.emit_set_presentation_enabled(false);
        if (invocation.authored_id() == 20U) {
          throw std::runtime_error("deliberate synthetic failure");
        }
      });
  openrc::game::ActorBehaviorRuntimeV1 runtime(scene, registry,
                                               runtime_limits(), 60U);
  const auto before = runtime.snapshot();
  const auto before_hash = runtime.deterministic_hash();
  expect_runtime_error(
      [&] { static_cast<void>(runtime.fixed_tick(0U, kAuthoredSchedule)); },
      "deliberate synthetic failure",
      "a failing behavior callback did not abort the tick");
  expect(runtime.snapshot() == before &&
             runtime.deterministic_hash() == before_hash,
         "a failed callback committed fields, RNG, state, presentation, or "
         "tick");
}

void test_journal_limit_and_tick_order_are_transactional() {
  const auto scene = make_scene();
  auto ordinary_registry = make_registry(scene, synthetic_behavior());
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 too_small(scene, ordinary_registry,
                                                       runtime_limits(4U), 60U);
      },
      "initialization exceeds",
      "initialization commands bypassed the runtime journal limit");

  auto registry = make_registry(
      scene, [](openrc::game::ActorBehaviorInvocationV1 &invocation) {
        for (std::uint32_t command = 0U; command < 6U; ++command) {
          invocation.emit_set_presentation_enabled((command & 1U) == 0U);
        }
      });
  openrc::game::ActorBehaviorRuntimeV1 runtime(scene, registry,
                                               runtime_limits(5U), 60U);
  const auto before = runtime.snapshot();
  expect_runtime_error(
      [&] { static_cast<void>(runtime.fixed_tick(0U, kAuthoredSchedule)); },
      "journal limit", "an oversized behavior journal was accepted");
  expect(runtime.snapshot() == before,
         "journal overflow committed a partial behavior tick");
  expect_runtime_error(
      [&] { static_cast<void>(runtime.fixed_tick(1U, kAuthoredSchedule)); },
      "replay order", "an out-of-order actor behavior tick was accepted");
  expect(runtime.snapshot() == before,
         "out-of-order behavior input changed runtime state");
}

void test_trace_counter_exhaustion_rolls_back_rng_and_fields() {
  const auto scene = make_scene();
  auto registry = make_registry(scene, synthetic_behavior());
  openrc::game::ActorBehaviorRuntimeV1 initial(scene, registry,
                                               runtime_limits(), 60U);
  auto exhausted_state = initial.snapshot();
  exhausted_state.instances[0U].current_state_id = 1U;
  exhausted_state.instances[0U].state_ticks =
      std::numeric_limits<std::uint64_t>::max();
  openrc::game::ActorBehaviorRuntimeV1 state_runtime(
      scene, registry, runtime_limits(), 60U, exhausted_state);
  const auto state_before = state_runtime.snapshot();
  expect_runtime_error(
      [&] {
        static_cast<void>(state_runtime.fixed_tick(0U, kAuthoredSchedule));
      },
      "state tick counter", "an exhausted behavior state counter advanced");
  expect(state_runtime.snapshot() == state_before,
         "state-counter exhaustion committed earlier field or RNG changes");

  auto exhausted_instance_rng = initial.snapshot();
  exhausted_instance_rng.instances[0U].random_call_count =
      std::numeric_limits<std::uint64_t>::max();
  openrc::game::ActorBehaviorRuntimeV1 instance_rng_runtime(
      scene, registry, runtime_limits(), 60U, exhausted_instance_rng);
  const auto instance_rng_before = instance_rng_runtime.snapshot();
  expect_runtime_error(
      [&] {
        static_cast<void>(
            instance_rng_runtime.fixed_tick(0U, kAuthoredSchedule));
      },
      "instance RNG call counter",
      "an exhausted per-instance RNG counter advanced");
  expect(instance_rng_runtime.snapshot() == instance_rng_before,
         "instance RNG counter exhaustion changed behavior state");

  auto exhausted_shared_rng = initial.snapshot();
  exhausted_shared_rng.shared_random_streams[0U].call_count =
      std::numeric_limits<std::uint64_t>::max();
  openrc::game::ActorBehaviorRuntimeV1 shared_rng_runtime(
      scene, registry, runtime_limits(), 60U, exhausted_shared_rng);
  const auto shared_rng_before = shared_rng_runtime.snapshot();
  expect_runtime_error(
      [&] {
        static_cast<void>(shared_rng_runtime.fixed_tick(0U, kAuthoredSchedule));
      },
      "shared RNG call counter", "an exhausted shared RNG counter advanced");
  expect(shared_rng_runtime.snapshot() == shared_rng_before,
         "shared RNG counter exhaustion changed behavior state");
}

void test_registry_identity_and_session_scope_fail_closed() {
  const auto scene = make_scene();
  openrc::game::ActorBehaviorRuntimeRegistryV1 wrong_registry;
  wrong_registry.register_random_algorithm({
      std::string(kRandomAlgorithmKey),
      1U,
      1U,
      [](const std::span<std::uint32_t> words) { return ++words[0U]; },
  });
  auto wrong_identity = identity_for(scene.programs.front());
  wrong_identity.state_layout_sha256[0U] ^= std::byte{1U};
  wrong_registry.register_implementation(
      {wrong_identity, synthetic_behavior()});
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 runtime(scene, wrong_registry,
                                                     runtime_limits(), 60U);
      },
      "No exact native implementation",
      "runtime dispatched an implementation with a different layout digest");

  auto random_abi_scene = make_scene();
  random_abi_scene.programs[0U].random_imports[0U].algorithm_abi_version = 2U;
  random_abi_scene.random_streams[0U].algorithm_abi_version = 2U;
  random_abi_scene.programs[0U].state_layout_sha256 = {};
  random_abi_scene = openrc::canonicalize_actor_behavior_scene_v1(
      std::move(random_abi_scene), scene_limits());
  auto wrong_random_registry =
      make_registry(random_abi_scene, synthetic_behavior());
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 runtime(
            random_abi_scene, wrong_random_registry, runtime_limits(), 60U);
      },
      "No exact native random algorithm",
      "runtime dispatched a random algorithm with a different ABI");

  auto session_scene = make_scene();
  session_scene.programs[0U].random_imports[0U].scope =
      openrc::ActorBehaviorRandomScopeV1::session_shared;
  session_scene.programs[0U].state_layout_sha256 = {};
  session_scene.random_streams[0U].scope =
      openrc::ActorBehaviorRandomScopeV1::session_shared;
  session_scene = openrc::canonicalize_actor_behavior_scene_v1(
      std::move(session_scene), scene_limits());
  auto session_registry = make_registry(session_scene, synthetic_behavior());
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 runtime(
            session_scene, session_registry, runtime_limits(), 60U);
      },
      "session state owner",
      "runtime silently treated a session-shared RNG as level state");
}

[[nodiscard]] openrc::ActorBehaviorSceneV1 make_reference_scene(
    const std::uint32_t required_flags,
    const openrc::ActorBehaviorEntityReferenceV1 initial_reference = {}) {
  auto scene = make_scene();
  auto &program = scene.programs[0U];
  program.fields.push_back({2U, "target",
                            openrc::ActorBehaviorValueTypeV1::entity_reference,
                            1U, required_flags});
  program.state_layout_sha256 = {};
  for (auto &instance : scene.instances) {
    instance.initial_values.push_back(initial_reference);
  }
  return openrc::canonicalize_actor_behavior_scene_v1(std::move(scene),
                                                      scene_limits());
}

void test_entity_references_require_mounted_capabilities() {
  using openrc::game::ActorBehaviorEntityCapabilitiesV1;
  constexpr auto required = openrc::kActorBehaviorFieldKnownFlagsV1;
  const auto scene = make_reference_scene(
      required, openrc::ActorBehaviorEntityReferenceV1{197U});
  const auto registry = make_registry(scene, synthetic_behavior());
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 runtime(scene, registry,
                                                     runtime_limits(), 60U);
      },
      "no mounted entity",
      "non-null initial references accepted empty capabilities");
  for (const auto missing_bit :
       {openrc::kActorBehaviorEntityReferenceRequiresTransformV1,
        openrc::kActorBehaviorEntityReferenceRequiresActorV1,
        openrc::kActorBehaviorEntityReferenceRequiresBehaviorV1}) {
    const std::vector<ActorBehaviorEntityCapabilitiesV1> incomplete{
        {197U, required & ~missing_bit}};
    expect_runtime_error(
        [&] {
          openrc::game::ActorBehaviorRuntimeV1 runtime(
              scene, registry, runtime_limits(), 60U, 0U, {}, incomplete);
        },
        "lacks required capabilities",
        "an initial entity reference accepted a missing required relationship");
  }

  const auto unrestricted_scene =
      make_reference_scene(0U, openrc::ActorBehaviorEntityReferenceV1{197U});
  const auto unrestricted_registry =
      make_registry(unrestricted_scene, synthetic_behavior());
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 runtime(
            unrestricted_scene, unrestricted_registry, runtime_limits(), 60U);
      },
      "no mounted entity",
      "an unrestricted entity-reference field accepted an unresolved entity");
  const std::vector<ActorBehaviorEntityCapabilitiesV1> bare_entity{{197U, 0U}};
  openrc::game::ActorBehaviorRuntimeV1 unrestricted(
      unrestricted_scene, unrestricted_registry, runtime_limits(), 60U, 0U, {},
      bare_entity);
  expect(std::get<openrc::ActorBehaviorEntityReferenceV1>(
             unrestricted.snapshot().instances[0U].field_values[3U])
                 .authored_id == 197U,
         "a valid entity with no additional field requirements was rejected");

  const auto null_scene = make_reference_scene(required);
  const auto null_registry = make_registry(null_scene, synthetic_behavior());
  openrc::game::ActorBehaviorRuntimeV1 null_references(
      null_scene, null_registry, runtime_limits(), 60U);
  expect(!std::get<openrc::ActorBehaviorEntityReferenceV1>(
              null_references.snapshot().instances[0U].field_values[3U])
              .authored_id,
         "an explicit null reference required a mounted entity");
  const std::vector<ActorBehaviorEntityCapabilitiesV1> duplicate{
      {197U, required}, {197U, 0U}};
  const std::vector<ActorBehaviorEntityCapabilitiesV1> unknown_flags{
      {197U, 0x80000000U}};
  for (const auto &invalid : {duplicate, unknown_flags}) {
    expect_runtime_error(
        [&] {
          openrc::game::ActorBehaviorRuntimeV1 runtime(
              null_scene, null_registry, runtime_limits(), 60U, 0U, {},
              invalid);
        },
        "duplicate IDs or unknown flags",
        "malformed mounted entity capabilities were accepted");
  }
}

void test_reference_mutation_and_restore_fail_transactionally() {
  using openrc::game::ActorBehaviorEntityCapabilitiesV1;
  constexpr auto required = openrc::kActorBehaviorFieldKnownFlagsV1;
  const auto scene = make_reference_scene(required);
  auto registry = make_registry(
      scene, [](openrc::game::ActorBehaviorInvocationV1 &invocation) {
        invocation.set_field(0U, 0U, std::uint32_t{999U});
        static_cast<void>(invocation.next_random_u32(0U));
        invocation.emit_set_presentation_enabled(false);
        invocation.set_field(2U, 0U,
                             openrc::ActorBehaviorEntityReferenceV1{197U});
      });
  std::vector<ActorBehaviorEntityCapabilitiesV1> capabilities{{197U, required},
                                                              {10U, 0U}};
  openrc::game::ActorBehaviorRuntimeV1 runtime(
      scene, registry, runtime_limits(), 60U, 0U, {}, capabilities);
  capabilities.clear();
  static_cast<void>(runtime.fixed_tick(0U, kAuthoredSchedule));
  const auto saved = runtime.snapshot();
  expect(std::get<openrc::ActorBehaviorEntityReferenceV1>(
             saved.instances[0U].field_values[3U])
                 .authored_id == 197U,
         "runtime did not retain its own copy of mounted capabilities");

  const std::vector<ActorBehaviorEntityCapabilitiesV1> valid{{197U, required}};
  openrc::game::ActorBehaviorRuntimeV1 restored(
      scene, registry, runtime_limits(), 60U, saved, valid);
  expect(restored.snapshot() == saved &&
             restored.initialization_journal().empty(),
         "restoring a valid entity reference changed saved state");
  auto unresolved = saved;
  unresolved.instances[1U].field_values[3U] =
      openrc::ActorBehaviorEntityReferenceV1{999U};
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 invalid(
            scene, registry, runtime_limits(), 60U, unresolved, valid);
      },
      "no mounted entity", "snapshot restoration accepted a dangling entity");
  const std::vector<ActorBehaviorEntityCapabilitiesV1> lacks_behavior{
      {197U,
       required & ~openrc::kActorBehaviorEntityReferenceRequiresBehaviorV1}};
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 invalid(
            scene, registry, runtime_limits(), 60U, saved, lacks_behavior);
      },
      "lacks required capabilities",
      "snapshot restoration accepted an incompatible referenced entity");
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 invalid(
            scene, registry, runtime_limits(), 60U, saved);
      },
      "no mounted entity",
      "snapshot restoration accepted no entity capabilities");

  for (const auto &invalid_capabilities :
       {std::vector<ActorBehaviorEntityCapabilitiesV1>{}, lacks_behavior}) {
    openrc::game::ActorBehaviorRuntimeV1 invalid_target(
        scene, registry, runtime_limits(), 60U, 0U, {}, invalid_capabilities);
    const auto before = invalid_target.snapshot();
    const auto before_hash = invalid_target.deterministic_hash();
    expect_runtime_error(
        [&] {
          static_cast<void>(invalid_target.fixed_tick(0U, kAuthoredSchedule));
        },
        invalid_capabilities.empty() ? "no mounted entity"
                                     : "lacks required capabilities",
        "a field mutation accepted an incompatible referenced entity");
    expect(invalid_target.snapshot() == before &&
               invalid_target.deterministic_hash() == before_hash,
           "invalid reference mutation committed earlier RNG, fields, or "
           "commands");
  }
}

void test_animation_frame_bounds_abort_before_commit() {
  const auto scene = make_scene();
  auto registry = make_registry(
      scene, [](openrc::game::ActorBehaviorInvocationV1 &invocation) {
        invocation.set_field(0U, 0U, std::uint32_t{999U});
        static_cast<void>(invocation.next_random_u32(0U));
        invocation.emit_set_presentation_enabled(false);
        invocation.emit_set_animation(
            0U, 0U, invocation.authored_id() == 20U ? 16U : 15U, 0U);
      });
  openrc::game::ActorBehaviorRuntimeV1 runtime(scene, registry,
                                               runtime_limits(), 60U);
  const auto before = runtime.snapshot();
  expect_runtime_error(
      [&] { static_cast<void>(runtime.fixed_tick(0U, kAuthoredSchedule)); },
      "invalid animation frame",
      "a behavior callback emitted a frame past the clip end");
  expect(runtime.snapshot() == before,
         "invalid animation frame committed earlier actor state or commands");
  registry = make_registry(
      scene, [](openrc::game::ActorBehaviorInvocationV1 &invocation) {
        invocation.emit_set_animation(0U, 0U, 15U, 0U);
      });
  openrc::game::ActorBehaviorRuntimeV1 final_frame(scene, registry,
                                                   runtime_limits(), 60U);
  const auto tick = final_frame.fixed_tick(0U, kAuthoredSchedule);
  expect(tick.journal.size() == 2U &&
             std::get<openrc::game::ActorBehaviorSetAnimationCommandV1>(
                 tick.journal[0U].payload)
                     .first_frame_index == 15U,
         "the last valid frame of an animation import was rejected");
}

void test_source_cadence_phase_restore_and_hash() {
  auto scene = make_scene();
  scene.programs[0U].source_updates_per_second = 50U;
  scene.programs[0U].state_layout_sha256 = {};
  scene = openrc::canonicalize_actor_behavior_scene_v1(std::move(scene),
                                                       scene_limits());
  auto registry = make_registry(
      scene, [](openrc::game::ActorBehaviorInvocationV1 &invocation) {
        const auto count = std::get<std::uint32_t>(invocation.field(0U));
        expect(invocation.state_ticks() == count,
               "state_ticks counted runtime ticks instead of source updates");
        invocation.set_field(0U, 0U, count + 1U);
        static_cast<void>(invocation.next_random_u32(0U));
        invocation.emit_set_animation(0U, 0U, count, 0U);
      });
  openrc::game::ActorBehaviorRuntimeV1 runtime(scene, registry,
                                               runtime_limits(), 60U, 100U);
  const auto idle_tick = runtime.fixed_tick(100U, kAuthoredSchedule);
  const auto saved = runtime.snapshot();
  expect(idle_tick.journal.empty() && saved.next_tick_index == 101U &&
             saved.instances[0U].source_update_accumulator == 50U &&
             saved.instances[0U].state_ticks == 0U &&
             saved.shared_random_streams[0U].call_count == 0U,
         "a no-source-update tick invoked behavior or lost its cadence phase");
  openrc::game::ActorBehaviorRuntimeV1 restored(scene, registry,
                                                runtime_limits(), 60U, saved);
  expect(restored.initialization_journal().empty(),
         "cadence snapshot restoration restarted initial animations");
  for (std::uint64_t tick = 101U; tick < 106U; ++tick) {
    expect(runtime.fixed_tick(tick, kAuthoredSchedule) ==
                   restored.fixed_tick(tick, kAuthoredSchedule) &&
               runtime.snapshot() == restored.snapshot() &&
               runtime.deterministic_hash() == restored.deterministic_hash(),
           "source cadence diverged after restoring a fractional update phase");
  }
  const auto after = runtime.snapshot();
  expect(after.instances[0U].state_ticks == 5U &&
             after.instances[1U].state_ticks == 5U &&
             after.instances[0U].source_update_accumulator == 0U &&
             std::get<std::uint32_t>(after.instances[0U].field_values[0U]) ==
                 5U &&
             after.shared_random_streams[0U].call_count == 10U,
         "50 source updates per second did not produce five updates per six 60 "
         "Hz ticks");
  auto changed_phase = saved;
  --changed_phase.instances[0U].source_update_accumulator;
  expect(openrc::game::hash_actor_behavior_runtime_snapshot_v1(changed_phase) !=
             openrc::game::hash_actor_behavior_runtime_snapshot_v1(saved),
         "snapshot hash omitted the source update phase");
  auto changed_cadence = saved;
  changed_cadence.runtime_ticks_per_second = 120U;
  expect(
      openrc::game::hash_actor_behavior_runtime_snapshot_v1(changed_cadence) !=
          openrc::game::hash_actor_behavior_runtime_snapshot_v1(saved),
      "snapshot hash omitted the runtime tick cadence");
  auto invalid_phase = saved;
  invalid_phase.instances[1U].source_update_accumulator = 60U;
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 invalid(
            scene, registry, runtime_limits(), 60U, invalid_phase);
      },
      "incompatible instance state",
      "a snapshot accepted an out-of-range update phase");
  expect_runtime_error(
      [&] {
        static_cast<void>(openrc::game::hash_actor_behavior_runtime_snapshot_v1(
            invalid_phase));
      },
      "invalid source update accumulator",
      "snapshot hashing accepted an invalid phase");
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 invalid(
            scene, registry, runtime_limits(), 120U, saved);
      },
      "different scene",
      "snapshot restoration silently changed the runtime cadence");
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 invalid(scene, registry,
                                                     runtime_limits(), 0U);
      },
      "non-zero runtime tick cadence",
      "a zero runtime update rate was accepted");
  expect_runtime_error(
      [&] {
        openrc::game::ActorBehaviorRuntimeV1 invalid(scene, registry,
                                                     runtime_limits(), 49U);
      },
      "source update cadence exceeds",
      "a slower runtime silently dropped required source behavior updates");

  auto failing_registry = make_registry(
      scene, [](openrc::game::ActorBehaviorInvocationV1 &invocation) {
        static_cast<void>(invocation.next_random_u32(0U));
        invocation.set_field(0U, 0U, std::uint32_t{999U});
        invocation.emit_set_presentation_enabled(false);
        if (invocation.authored_id() == 20U) {
          throw std::runtime_error("cadence rollback failure");
        }
      });
  openrc::game::ActorBehaviorRuntimeV1 failing(scene, failing_registry,
                                               runtime_limits(), 60U, saved);
  expect_runtime_error(
      [&] { static_cast<void>(failing.fixed_tick(101U, kAuthoredSchedule)); },
      "cadence rollback failure",
      "a source-cadence callback failure was ignored");
  expect(failing.snapshot() == saved,
         "a failing source update committed its accumulator or earlier state");

  openrc::game::ActorBehaviorRuntimeV1 wide_runtime(
      scene, registry, runtime_limits(),
      std::numeric_limits<std::uint32_t>::max());
  auto wide_saved = wide_runtime.snapshot();
  for (auto &instance : wide_saved.instances) {
    instance.source_update_accumulator =
        std::numeric_limits<std::uint32_t>::max() - 1U;
  }
  openrc::game::ActorBehaviorRuntimeV1 wide_restored(
      scene, registry, runtime_limits(),
      std::numeric_limits<std::uint32_t>::max(), wide_saved);
  static_cast<void>(wide_restored.fixed_tick(0U, kAuthoredSchedule));
  expect(wide_restored.snapshot().instances[0U].source_update_accumulator ==
                 49U &&
             wide_restored.snapshot().instances[0U].state_ticks == 1U,
         "source cadence accumulator overflowed at the uint32 boundary");
}

void test_explicit_schedule_preserves_source_order_and_selection() {
  const auto callback =
      [](openrc::game::ActorBehaviorInvocationV1 &invocation) {
        invocation.set_field(0U, 0U, invocation.next_random_u32(0U));
        invocation.emit_set_presentation_enabled(true);
      };
  const auto scene = make_scene();
  const auto registry = make_registry(scene, callback);
  constexpr std::array<std::uint32_t, 2U> reversed{20U, 10U};
  openrc::game::ActorBehaviorRuntimeV1 forward(scene, registry,
                                               runtime_limits(), 60U);
  openrc::game::ActorBehaviorRuntimeV1 reverse(scene, registry,
                                               runtime_limits(), 60U);
  static_cast<void>(forward.fixed_tick(0U, kAuthoredSchedule));
  const auto reverse_tick = reverse.fixed_tick(0U, reversed);
  const auto forward_state = forward.snapshot();
  const auto reverse_state = reverse.snapshot();
  expect(reverse_tick.journal.size() == 2U &&
             reverse_tick.journal[0U].authored_id == 20U &&
             reverse_tick.journal[0U].ordinal == 0U &&
             reverse_tick.journal[1U].authored_id == 10U &&
             reverse_tick.journal[1U].ordinal == 1U,
         "runtime sorted the caller's source invocation order");
  expect(
      reverse_state.instances[0U].authored_id == 10U &&
          reverse_state.instances[1U].authored_id == 20U &&
          std::get<std::uint32_t>(
              reverse_state.instances[0U].field_values[0U]) == 12U &&
          std::get<std::uint32_t>(
              reverse_state.instances[1U].field_values[0U]) == 11U &&
          std::get<std::uint32_t>(
              forward_state.instances[0U].field_values[0U]) == 11U &&
          std::get<std::uint32_t>(
              forward_state.instances[1U].field_values[0U]) == 12U,
      "shared RNG ignored source scheduling order or snapshot order changed");

  auto pal_scene = make_scene();
  pal_scene.programs[0U].source_updates_per_second = 50U;
  pal_scene.programs[0U].state_layout_sha256 = {};
  pal_scene = openrc::canonicalize_actor_behavior_scene_v1(std::move(pal_scene),
                                                           scene_limits());
  const auto pal_registry = make_registry(pal_scene, callback);
  openrc::game::ActorBehaviorRuntimeV1 selected(pal_scene, pal_registry,
                                                runtime_limits(), 60U);
  const auto before_invalid = selected.snapshot();
  const std::vector<std::pair<std::vector<std::uint32_t>, std::string_view>>
      invalid_schedules{{{10U, 999U}, "unknown actor"},
                        {{10U, 10U}, "duplicate actor"},
                        {{10U, 20U, 10U}, "too many actors"}};
  for (const auto &[schedule, diagnostic] : invalid_schedules) {
    expect_runtime_error(
        [&] { static_cast<void>(selected.fixed_tick(0U, schedule)); },
        diagnostic, "runtime accepted a malformed actor schedule");
    expect(
        selected.snapshot() == before_invalid,
        "invalid scheduling input committed cadence or partial actor updates");
  }
  expect(selected.fixed_tick(0U, {}).journal.empty(),
         "an empty actor schedule emitted commands");
  const auto after_empty = selected.snapshot();
  expect(after_empty.next_tick_index == 1U &&
             after_empty.instances[0U].source_update_accumulator == 50U &&
             after_empty.instances[1U].source_update_accumulator == 50U &&
             after_empty.shared_random_streams[0U].call_count == 0U,
         "an empty schedule failed to advance only the host clock and phase");
  constexpr std::array<std::uint32_t, 1U> only_second{20U};
  const auto selected_tick = selected.fixed_tick(1U, only_second);
  const auto after_selected = selected.snapshot();
  expect(selected_tick.journal.size() == 1U &&
             selected_tick.journal[0U].authored_id == 20U &&
             after_selected.instances[0U].state_ticks == 0U &&
             after_selected.instances[1U].state_ticks == 1U &&
             after_selected.instances[0U].source_update_accumulator == 40U &&
             after_selected.instances[1U].source_update_accumulator == 40U &&
             after_selected.instances[0U].random_call_count == 0U &&
             after_selected.shared_random_streams[0U].call_count == 1U,
         "an unscheduled actor consumed state ticks or shared random state");
  expect(selected.fixed_tick(2U, {}).journal.empty(),
         "an empty schedule executed actors whose source updates were due");
  const auto after_due_empty = selected.snapshot();
  expect(after_due_empty.instances[0U].source_update_accumulator == 30U &&
             after_due_empty.instances[0U].state_ticks == 0U &&
             after_due_empty.instances[1U].state_ticks == 1U &&
             after_due_empty.shared_random_streams[0U].call_count == 1U,
         "empty scheduling of a due tick altered behavior state or paused "
         "cadence");
  static_cast<void>(selected.fixed_tick(3U, reversed));
  const auto after_resumed = selected.snapshot();
  expect(
      std::get<std::uint32_t>(after_resumed.instances[0U].field_values[0U]) ==
              13U &&
          std::get<std::uint32_t>(
              after_resumed.instances[1U].field_values[0U]) == 12U &&
          after_resumed.instances[0U].state_ticks == 1U &&
          after_resumed.instances[1U].state_ticks == 2U,
      "rescheduled actors replayed skipped updates or lost explicit RNG order");
}

void test_group_selection_drives_behavior_rng_and_replay() {
  using namespace openrc::game;
  const auto scene = make_scene();
  const auto registry = make_registry(
      scene, [](ActorBehaviorInvocationV1 &invocation) {
        invocation.set_field(0U, 0U, invocation.next_random_u32(0U));
        invocation.emit_set_presentation_enabled(true);
      });
  ActorBehaviorRuntimeV1 runtime(scene, registry, runtime_limits(), 60U);
  constexpr ActorUpdateScheduleLimitsV1 limits{16U, 4U, 16U, 64U, 8U, 16U};
  std::vector<ActorUpdateCandidateV1> candidates{
      {10U, 3U, 2U, true, true, false, false},
      {20U, 3U, 2U, true, false, false, false}};
  const std::vector<ActorActivationGroupV1> groups{{2U, {20U, 10U}}};
  std::vector<ActorUpdateRangeEligibilityV1> ranges{{20U, true}};
  const auto selected = build_grouped_actor_update_schedule_v1(
      candidates, groups, ranges, limits);
  const auto before = runtime.snapshot();
  const auto first = runtime.fixed_tick(0U, selected.ordered_authored_ids);
  const auto after = runtime.snapshot();
  expect(first.journal.size() == 2U &&
             first.journal[0U].authored_id == 20U &&
             first.journal[1U].authored_id == 10U &&
             std::get<std::uint32_t>(after.instances[0U].field_values[0U]) ==
                 12U &&
             std::get<std::uint32_t>(after.instances[1U].field_values[0U]) ==
                 11U,
         "group selection did not drive shared RNG in source member order");

  // Selection inputs belong to the source-world owner. Rebuilding the same
  // list after restoring behavior state must reproduce both state and journal.
  ActorBehaviorRuntimeV1 restored(scene, registry, runtime_limits(), 60U, before);
  const auto replay_selection = build_grouped_actor_update_schedule_v1(
      candidates, groups, ranges, limits);
  expect(restored.initialization_journal().empty() &&
             restored.fixed_tick(0U, replay_selection.ordered_authored_ids) ==
                 first &&
             restored.snapshot() == after,
         "group selection and behavior snapshot replay diverged");

  ranges[0U].eligible = false;
  const auto asleep = build_grouped_actor_update_schedule_v1(
      candidates, groups, ranges, limits);
  expect(runtime.fixed_tick(1U, asleep.ordered_authored_ids).journal.empty() &&
             runtime.snapshot().shared_random_streams[0U].call_count == 2U,
         "an unactivated group consumed behavior updates or shared RNG");

  candidates[0U].live = false;
  ranges[0U].eligible = true;
  const auto awake = build_grouped_actor_update_schedule_v1(
      candidates, groups, ranges, limits);
  const auto resumed = runtime.fixed_tick(2U, awake.ordered_authored_ids);
  const auto final_state = runtime.snapshot();
  expect(resumed.journal.size() == 1U &&
             resumed.journal[0U].authored_id == 20U &&
             final_state.instances[0U].state_ticks == 1U &&
             final_state.instances[1U].state_ticks == 2U &&
             final_state.shared_random_streams[0U].call_count == 3U &&
             std::get<std::uint32_t>(
                 final_state.instances[1U].field_values[0U]) == 13U,
         "reactivated group ran an inactive sibling or replayed skipped ticks");
}

void test_mixed_source_cadences_preserve_shared_rng_order() {
  auto scene = make_scene();
  auto pal_program = scene.programs[0U];
  pal_program.id = 1U;
  pal_program.semantic_key = "actors/test/synthetic-counter-pal";
  pal_program.source_updates_per_second = 50U;
  pal_program.state_layout_sha256 = {};
  scene.programs.push_back(std::move(pal_program));
  scene.instances[1U].program_id = 1U;
  scene = openrc::canonicalize_actor_behavior_scene_v1(std::move(scene),
                                                       scene_limits());
  const auto callback =
      [](openrc::game::ActorBehaviorInvocationV1 &invocation) {
        invocation.set_field(0U, 0U, invocation.next_random_u32(0U));
        invocation.emit_set_presentation_enabled(true);
      };
  auto registry = make_registry(scene, callback);
  registry.register_implementation(
      {identity_for(scene.programs[1U]), callback});
  openrc::game::ActorBehaviorRuntimeV1 runtime(scene, registry,
                                               runtime_limits(), 60U);
  constexpr std::array<std::uint32_t, 2U> reversed{20U, 10U};
  const auto first_tick = runtime.fixed_tick(0U, reversed);
  expect(first_tick.journal.size() == 1U &&
             first_tick.journal[0U].authored_id == 10U,
         "an actor executed before its own source cadence was due");
  for (std::uint64_t tick = 1U; tick < 6U; ++tick) {
    const auto result = runtime.fixed_tick(tick, reversed);
    expect(result.journal.size() == 2U &&
               result.journal[0U].authored_id == 20U &&
               result.journal[1U].authored_id == 10U,
           "mixed-cadence actors did not retain the explicit source order");
  }
  const auto final_state = runtime.snapshot();
  expect(final_state.instances[0U].state_ticks == 6U &&
             final_state.instances[1U].state_ticks == 5U &&
             final_state.shared_random_streams[0U].call_count == 11U &&
             std::get<std::uint32_t>(
                 final_state.instances[0U].field_values[0U]) == 21U &&
             std::get<std::uint32_t>(
                 final_state.instances[1U].field_values[0U]) == 20U,
         "mixed source cadences consumed shared RNG in the wrong order or "
         "quantity");
}

} // namespace

int main() {
  try {
    test_exact_registry_shared_rng_state_and_journal();
    test_snapshot_restore_and_deterministic_hash();
    test_failed_callback_rolls_back_every_staged_value();
    test_journal_limit_and_tick_order_are_transactional();
    test_trace_counter_exhaustion_rolls_back_rng_and_fields();
    test_registry_identity_and_session_scope_fail_closed();
    test_entity_references_require_mounted_capabilities();
    test_reference_mutation_and_restore_fail_transactionally();
    test_animation_frame_bounds_abort_before_commit();
    test_source_cadence_phase_restore_and_hash();
    test_explicit_schedule_preserves_source_order_and_selection();
    test_group_selection_drives_behavior_rng_and_replay();
    test_mixed_source_cadences_preserve_shared_rng_order();
    std::cout << "runtime actor-behavior tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime actor-behavior tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
