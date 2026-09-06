#include "openrc/game_world.hpp"
#include "openrc/placement_admission_io.hpp"
#include "openrc/rac_moby_admission_compile.hpp"

#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace openrc;
using namespace openrc::game;

constexpr SessionStateLimitsV1 kStateLimits{5U,    7U,   64U,   4096U, 1024U,
                                            4096U, 256U, 2048U, 64U};
constexpr PlacementAdmissionLimitsV1 kPlanLimits{64U, 64U};
constexpr PlacementAdmissionIoLimitsV1 kIoLimits{4096U, kPlanLimits};

void expect(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void append_word(std::vector<std::byte> &bytes, const std::uint32_t value,
                 const unsigned width) {
  for (unsigned i = 0U; i < width; ++i) {
    bytes.push_back(static_cast<std::byte>((value >> (8U * i)) & 255U));
  }
}

struct Fixture {
  RacMobyAdmissionStateV1 source;
  SessionStateInitialV1 initial;
  RacMobyAdmissionStateBindingsV1 bindings;
};

Fixture fixture(const std::int32_t key, const unsigned mode,
                const unsigned registration_mode) {
  Fixture f;
  f.source.selector_bytes = {
      -1, std::vector<std::uint8_t>(
              17U, static_cast<std::uint8_t>((mode & 1U) ? 255U : 7U))};
  f.source.suppression_bytes = {key,
                                {static_cast<std::uint8_t>((mode >> 1U) & 1U)}};
  f.source.primary_bit_words.assign(64U, (mode & 4U) ? UINT32_MAX : 0U);
  f.source.alternate_bit_words.assign(64U, (mode & 8U) ? UINT32_MAX : 0U);
  f.source.registration_slots.emplace();
  const auto wanted =
      static_cast<std::uint16_t>(static_cast<std::uint32_t>(key) + 1U);
  for (std::size_t i = 0U; i < 64U; ++i) {
    auto &slot = (*f.source.registration_slots)[i];
    slot.key = registration_mode == 0U   ? 0
               : registration_mode == 1U ? std::bit_cast<std::int16_t>(wanted)
                                         : 0x1111;
    slot.auxiliary_bits = static_cast<std::uint16_t>(0x8100U + i);
  }
  if (registration_mode >= 2U) {
    (*f.source.registration_slots)[61U].key =
        std::bit_cast<std::int16_t>(wanted);
    if (registration_mode == 2U)
      (*f.source.registration_slots)[62U].key = 0;
  }
  auto &schema = f.initial.schema;
  schema.identity_key = "test.compiled-placement/state";
  schema.buffers = {{"selectors", 17U},
                    {"suppression", 1U},
                    {"primary", 256U},
                    {"alternate", 256U},
                    {"registration", 256U}};
  schema.views = {
      {"selectors", "selectors", SessionStateValueTypeV1::u8, 0U, 17U, 1U},
      {"suppression", "suppression", SessionStateValueTypeV1::u8, 0U, 1U, 1U},
      {"primary", "primary", SessionStateValueTypeV1::u32, 0U, 64U, 4U},
      {"alternate", "alternate", SessionStateValueTypeV1::u32, 0U, 64U, 4U},
      {"keys", "registration", SessionStateValueTypeV1::u16, 0U, 64U, 4U},
      {"auxiliary", "registration", SessionStateValueTypeV1::u16, 2U, 64U, 4U},
      {"registration-words", "registration", SessionStateValueTypeV1::u32, 0U,
       64U, 4U},
  };
  for (const auto &buffer : schema.buffers) {
    SessionStateBufferBytesV1 image{buffer.key, {}};
    if (buffer.key == "selectors") {
      for (const auto byte : f.source.selector_bytes.bytes)
        append_word(image.bytes, byte, 1U);
    } else if (buffer.key == "suppression") {
      append_word(image.bytes, f.source.suppression_bytes.bytes[0U], 1U);
    } else if (buffer.key == "registration") {
      for (const auto slot : *f.source.registration_slots) {
        append_word(image.bytes, static_cast<std::uint16_t>(slot.key), 2U);
        append_word(image.bytes, slot.auxiliary_bits, 2U);
      }
    } else {
      const auto &words = buffer.key == "primary"
                              ? f.source.primary_bit_words
                              : f.source.alternate_bit_words;
      for (const auto word : words)
        append_word(image.bytes, word, 4U);
    }
    f.initial.buffers.push_back(std::move(image));
  }
  f.bindings.state_schema_sha256 =
      hash_session_state_schema_v1(schema, kStateLimits);
  f.bindings.selector_bytes =
      RacAdmissionStateWindowV1{"selectors", 0U, 17U, -1};
  f.bindings.suppression_bytes =
      RacAdmissionStateWindowV1{"suppression", 0U, 1U, key};
  f.bindings.primary_bit_words =
      RacAdmissionStateWindowV1{"primary", 0U, 64U, 0};
  f.bindings.alternate_bit_words =
      RacAdmissionStateWindowV1{"alternate", 0U, 64U, 0};
  f.bindings.registration_keys = RacAdmissionStateWindowV1{"keys", 0U, 64U, 0};
  return f;
}

void compare(Fixture f, const RacGameplayMobyAdmissionV1 &input,
             const RacMobyAdmissionLimitsV1 source_limits = {}) {
  auto source = f.source;
  std::optional<RacMobyAdmissionResultV1> expected;
  try {
    expected = evaluate_rac_moby_admission_v1(input, source, source_limits);
  } catch (const RacMobyAdmissionError &) {
  }

  const auto compiled = compile_rac_moby_admission_v1(
      input, f.bindings, kPlanLimits, source_limits);
  const auto encoded =
      encode_placement_admission_plan_v1(compiled.plan, kIoLimits);
  const auto decoded = decode_placement_admission_plan_v1(encoded, kIoLimits);
  expect(decoded == compiled.plan, "neutral plan binary roundtrip differs");
  GameSessionV1 session(99U, f.initial, kStateLimits);
  const auto before = session.snapshot();
  std::optional<PlacementAdmissionResultV1> actual;
  try {
    actual = session.apply_placement_admission(decoded, 0U, kPlanLimits);
  } catch (const GameWorldError &) {
  }
  const auto context = " flags=" + std::to_string(input.policy_bits) +
                       " key=" + std::to_string(input.key_index);
  expect(expected.has_value() == actual.has_value(),
         "source/native success differs" + context);
  if (!expected) {
    expect(session.snapshot() == before,
           "late source-input failure committed native bytes" + context);
    return;
  }
  expect(actual->admitted ==
                 (expected->decision == RacMobyAdmissionDecisionV1::admitted) &&
             actual->maximum_bits == expected->maximum_bits &&
             actual->current_bits == expected->current_bits &&
             actual->registration_write.has_value() ==
                 expected->registration_write.has_value(),
         "compiled source decision/count/registration differs" + context);
  const auto native_slot =
      actual->registration_write
          ? static_cast<std::uint8_t>(actual->registration_write->slot_index)
          : compiled.unregistered_slot_bits;
  expect(native_slot == expected->auxiliary_slot_bits,
         "registration fallback/source slot differs" + context);
  if (expected->constructor_fields) {
    const auto &fields = *expected->constructor_fields;
    expect(compiled.constructor_key_bits == fields.key_bits &&
               compiled.constructor_selector_bits == fields.selector_bits &&
               static_cast<std::uint16_t>(actual->current_bits) ==
                   fields.current_count_bits &&
               static_cast<std::uint16_t>(actual->maximum_bits) ==
                   fields.maximum_count_bits,
           "source constructor scalar metadata differs" + context);
  }
  if (expected->registration_write) {
    const auto &a = *actual->registration_write;
    const auto &e = *expected->registration_write;
    expect(a.slot_index == e.slot_index &&
               a.previous_value == static_cast<std::uint16_t>(e.previous_key) &&
               a.stored_value == static_cast<std::uint16_t>(e.stored_key),
           "registration write differs" + context);
  }
  expect(session.persistent_state()->revision() ==
             (actual->registration_write ? 1U : 0U),
         "registration revision differs" + context);
  if (source.registration_slots) {
    for (std::size_t i = 0U; i < 64U; ++i) {
      const auto slot = (*source.registration_slots)[i];
      expect(session.persistent_state()->read_u16("keys", i) ==
                     static_cast<std::uint16_t>(slot.key) &&
                 session.persistent_state()->read_u16("auxiliary", i) ==
                     slot.auxiliary_bits,
             "canonical registration bytes differ from source" + context);
    }
  }
}

void differential_cases() {
  std::vector<std::uint32_t> flags;
  for (std::uint32_t i = 0U; i < 64U; ++i)
    flags.push_back(i);
  flags.insert(flags.end(), {0x80000000U, 0x8000001fU, UINT32_MAX});
  constexpr std::array<std::int32_t, 8U> keys{-1,   0,     31,    32,
                                              2047, 32767, 65535, INT32_MAX};
  constexpr std::array<std::uint32_t, 8U> counts{
      0U,          1U,          2U,          0x7ffffffeU,
      0x7fffffffU, 0x80000000U, 0xfffffffeU, UINT32_MAX};
  std::size_t cases = 0U;
  for (const auto policy : flags) {
    for (unsigned mode = 0U; mode < 16U; ++mode) {
      for (const auto key : keys) {
        const auto a = counts[cases % counts.size()];
        const auto b = counts[(cases / keys.size()) % counts.size()];
        compare(fixture(key, mode, static_cast<unsigned>(cases % 4U)),
                RacGameplayMobyAdmissionV1{(mode & 1U) ? -1 : 15, policy, key,
                                           std::bit_cast<std::int32_t>(a),
                                           std::bit_cast<std::int32_t>(b)});
        ++cases;
      }
    }
  }
  for (const auto count : counts) {
    for (const auto mode : {4U, 9U}) {
      compare(fixture(0, mode, 0U),
              {0, 0x1fU, 0, std::bit_cast<std::int32_t>(count),
               std::bit_cast<std::int32_t>(count)});
      ++cases;
    }
  }
  // In-range positive boundary and non-representable full-s32 targets whose
  // low halfword is nonzero. A truncated match must not invent a source hit.
  for (const auto key : {32766, 32767, 65535, 65536, INT32_MAX}) {
    for (unsigned registration_mode = 0U; registration_mode < 4U;
         ++registration_mode) {
      compare(fixture(key, 0U, registration_mode), {0, 0x10U, key, 7, 3});
      ++cases;
    }
  }
  std::cout << "Source/native admission comparisons: " << cases << '\n';
}

void unavailable_and_lifecycle_cases() {
  auto f = fixture(0, 0U, 0U);
  f.source.selector_bytes.bytes.clear();
  f.bindings.selector_bytes.reset();
  compare(f, {-1, 0U, 0, 7, 3});
  compare(f, {-1, 0x13U, 0, 7, 3});
  f.source.suppression_bytes.bytes[0U] = 1U;
  for (auto &image : f.initial.buffers)
    if (image.buffer_key == "suppression")
      image.bytes[0U] = std::byte{1};
  compare(f, {-1, 0x13U, 0, 7, 3}); // early skip hides unavailable selector
  f = fixture(0, 0U, 0U);
  f.source.registration_slots.reset();
  f.bindings.registration_keys.reset();
  compare(f, {0, 0x10U, 0, 7, 3});
  compare(f, {0, 0U, 0, 7, 3});
  f = fixture(-1, 0U, 0U);
  f.source.registration_slots.reset();
  f.bindings.registration_keys.reset();
  compare(f, {0, 0x10U, -1, 7, 3}); // negative key skips registration reads
  f = fixture(0, 9U, 0U);
  f.source.primary_bit_words.clear();
  f.bindings.primary_bit_words.reset();
  compare(f, {0, 0x13U, 0, 7, 3}); // allowed alternate arm never reads primary
  f = fixture(0, 0U, 0U);
  compare(f, {16, 0x11U, 0, 7, 3});
  compare(f, {0, 0x11U, 0, 7, 3}, RacMobyAdmissionLimitsV1{0U, 1024U, 64U});
  compare(f, {0, 0x11U, 0, 7, 3}, RacMobyAdmissionLimitsV1{256U, 1024U, 0U});

  const auto compiled = compile_rac_moby_admission_v1({0, 0x10U, 0, 7, 3},
                                                      f.bindings, kPlanLimits);
  GameSessionV1 session(77U, f.initial, kStateLimits);
  WorldV1 world;
  for (const auto level : {0U, 1U, 0U}) {
    world.load_level(session, session.request_level(level));
    const auto result = session.apply_placement_admission(
        compiled.plan, session.persistent_state()->revision(), kPlanLimits);
    expect(result.admitted && result.registration_write &&
               result.registration_write->slot_index == 63U,
           "live session admission lost source registration across levels");
  }
  expect(session.persistent_state()->revision() == 3U &&
             session.persistent_state()->read_u16("keys", 63U) == 1U,
         "identical source writes or level lifecycle changed progress");
  const auto saved = session.snapshot();
  const GameSessionV1 restored(saved, f.initial.schema, kStateLimits);
  expect(restored.snapshot() == saved,
         "admission-mutated session failed restore");
  GameSessionV1 absent;
  bool rejected = false;
  try {
    (void)absent.apply_placement_admission(compiled.plan, 0U, kPlanLimits);
  } catch (const GameWorldError &) {
    rejected = true;
  }
  expect(rejected && !absent.persistent_state(),
         "admission manufactured absent session state");
}
} // namespace

int main() {
  try {
    differential_cases();
    unavailable_and_lifecycle_cases();
    std::cout << "RAC neutral admission compilation tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC neutral admission compilation tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
