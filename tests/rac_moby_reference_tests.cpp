#include "openrc/rac_moby_reference.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace openrc;
using namespace openrc::game;

constexpr SessionStateLimitsV1 kStateLimits{8U,     16U,   64U,   4096U, 4096U,
                                            16384U, 1024U, 4096U, 16U};
constexpr RacMobyReferenceLimitsV1 kLimits{64U, 256U};
constexpr std::uint32_t kBases = 0x1c47b8U;
constexpr std::uint32_t kReferences = 0x1792b8U;

void expect(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void append_word(std::vector<std::byte> &bytes, const std::uint32_t value) {
  for (unsigned shift = 0U; shift < 32U; shift += 8U) {
    bytes.push_back(static_cast<std::byte>((value >> shift) & 255U));
  }
}

struct Fixture {
  SessionStateInitialV1 initial;
  RacMobyReferenceBindingsV1 bindings;
};

Fixture fixture(const std::uint32_t level = 0U,
                const std::vector<std::uint32_t> &bases = {0U, 3U, 6U, 9U},
                const std::size_t reference_count = 12U) {
  Fixture f;
  f.initial.schema.identity_key = "test.reference-state";
  f.initial.schema.buffers = {{"level", 4U},
                              {"bases", bases.size() * 4U},
                              {"references", reference_count * 4U}};
  f.initial.schema.views = {
      {"level", "level", SessionStateValueTypeV1::u32, 0U, 1U, 4U},
      {"bases", "bases", SessionStateValueTypeV1::u32, 0U, bases.size(), 4U},
      {"references", "references", SessionStateValueTypeV1::u32, 0U,
       reference_count, 4U}};
  f.initial.buffers = {{"level", {}}, {"bases", {}}, {"references", {}}};
  append_word(f.initial.buffers[0U].bytes, level);
  for (const auto value : bases) {
    append_word(f.initial.buffers[1U].bytes, value);
  }
  for (std::size_t index = 0U; index < reference_count; ++index) {
    append_word(f.initial.buffers[2U].bytes,
                0x100U + static_cast<std::uint32_t>(index));
  }
  f.bindings.state_schema_sha256 =
      hash_session_state_schema_v1(f.initial.schema, kStateLimits);
  f.bindings.current_level_word = PlacementStateElementV1{"level", 0U};
  f.bindings.level_base_words =
      RacMobyReferenceWordWindowV1{kBases, bases.size(), "bases", 0U};
  f.bindings.reference_tokens = RacMobyReferenceWordWindowV1{
      kReferences, reference_count, "references", 0U};
  return f;
}

void write_word(SessionStateV1 &state, const std::string &key,
                const std::uint64_t index, const std::uint32_t value) {
  const std::array writes{
      SessionStateWriteV1{key, index, SessionStateValueTypeV1::u32, value}};
  state.apply_batch(writes, state.revision());
}

template <typename Callable>
void expect_error_unchanged(SessionStateV1 &state, Callable &&callable) {
  const auto before = state.snapshot();
  bool failed = false;
  try {
    callable();
  } catch (const RacMobyReferenceError &) {
    failed = true;
  }
  expect(failed, "unsupported reference operation must fail explicitly");
  expect(state.snapshot() == before,
         "failed reference operation changed state/revision");
}

void test_ordered_mutation_and_current_state() {
  auto f = fixture();
  SessionStateV1 state(f.initial, kStateLimits);
  const auto first = execute_rac_moby_reference_store_v1(f.bindings, state, 0U,
                                                         1U, 0x55U, kLimits);
  expect(first.level_word_bits == 0U && first.base_word_bits == 0U &&
             first.combined_index_bits == 1U && first.write &&
             first.write->destination ==
                 PlacementStateElementV1{"references", 1U} &&
             first.write->previous_token == 0x101U &&
             first.write->stored_token == 0x55U &&
             state.read_u32("references", 1U) == 0x55U &&
             state.revision() == 1U,
         "reference source step or neutral write differs");
  // Later J=0 calls share this owner; they do not finalize/clear earlier slots.
  (void)execute_rac_moby_reference_store_v1(f.bindings, state, 1U, 0U, 0x66U,
                                            kLimits);
  expect(state.read_u32("references", 0U) == 0x66U &&
             state.read_u32("references", 1U) == 0x55U,
         "later zero-index registration must preserve other slots");
  const auto repeated = execute_rac_moby_reference_store_v1(
      f.bindings, state, 2U, 1U, 0x55U, kLimits);
  expect(repeated.write && repeated.write->previous_token == 0x55U &&
             state.revision() == 3U,
         "same-token SW must remain an executed mutation");
  write_word(state, "bases", 0U, 5U);
  const auto updated_base = execute_rac_moby_reference_store_v1(
      f.bindings, state, state.revision(), 1U, 0U, kLimits);
  expect(updated_base.base_word_bits == 5U && updated_base.write &&
             updated_base.write->destination.element_index == 6U &&
             state.read_u32("references", 6U) == 0U,
         "base reads must be current; resolved null token is a real store");
  write_word(state, "level", 0U, 2U);
  const auto updated_level = execute_rac_moby_reference_store_v1(
      f.bindings, state, state.revision(), 2U, 0x99U, kLimits);
  expect(updated_level.level_word_bits == 2U &&
             updated_level.base_word_bits == 6U &&
             state.read_u32("references", 8U) == 0x99U,
         "current level must select its actual base word");
  SessionStateV1 restored(state.schema(), state.snapshot(), kStateLimits);
  expect(restored.snapshot() == state.snapshot(),
         "reference state lost canonical snapshot bytes");
}

void test_signed_gate_and_reached_bindings() {
  for (const auto level :
       std::array<std::uint32_t, 4U>{19U, 20U, 255U, 0x7fffffffU}) {
    auto f = fixture(level);
    f.bindings.level_base_words.reset();
    f.bindings.reference_tokens.reset();
    SessionStateV1 state(f.initial, kStateLimits);
    const auto before = state.snapshot();
    const auto result = execute_rac_moby_reference_store_v1(
        f.bindings, state, 0U, 0xffffffffU, std::nullopt, kLimits);
    expect(result.level_word_bits == level && !result.base_word_bits &&
               !result.combined_index_bits && !result.write &&
               state.snapshot() == before,
           "signed level bypass must not read missing base/token bindings");
    expect_error_unchanged(state, [&] {
      (void)execute_rac_moby_reference_store_v1(f.bindings, state, 1U, 0U, 0U,
                                                kLimits);
    });
  }
  auto f = fixture(18U, std::vector<std::uint32_t>(20U, 0U));
  SessionStateV1 state(f.initial, kStateLimits);
  expect(execute_rac_moby_reference_store_v1(f.bindings, state, 0U, 0U, 7U,
                                             kLimits)
             .write.has_value(),
         "level18 must read the final level base before the trailing sentinel");
  write_word(state, "level", 0U, 0xffffffffU);
  expect_error_unchanged(state, [&] {
    (void)execute_rac_moby_reference_store_v1(
        f.bindings, state, state.revision(), 0U, 7U, kLimits);
  }); // -1 passes the gate, then fails because its actual base address is
      // unmapped.
  f.bindings.level_base_words->source_address_bits = kBases - 4U;
  expect(execute_rac_moby_reference_store_v1(f.bindings, state,
                                             state.revision(), 0U, 7U, kLimits)
                 .base_word_bits == 0U,
         "explicitly mapped negative-level read must execute, not clamp or "
         "bypass");
  f.bindings.level_base_words->source_address_bits = kBases;
  write_word(state, "level", 0U, 0x80000000U);
  expect(execute_rac_moby_reference_store_v1(f.bindings, state,
                                             state.revision(), 0U, 7U, kLimits)
             .write.has_value(),
         "negative INT_MIN level SLL wraps to the base-origin address");
}

void test_wrapping_index_and_address_arithmetic() {
  for (const auto index : std::array<std::uint32_t, 4U>{
           0U, 0x40000000U, 0x80000000U, 0xc0000000U}) {
    auto f = fixture();
    SessionStateV1 state(f.initial, kStateLimits);
    const auto result = execute_rac_moby_reference_store_v1(
        f.bindings, state, 0U, index, 8U, kLimits);
    expect(result.combined_index_bits == index && result.write &&
               result.write->destination.element_index == 0U,
           "SLL must discard high two combined-index bits before mapping");
  }
  auto f = fixture(0U, {1U});
  SessionStateV1 state(f.initial, kStateLimits);
  const auto wrapped = execute_rac_moby_reference_store_v1(
      f.bindings, state, 0U, UINT32_MAX, 9U, kLimits);
  expect(wrapped.combined_index_bits == 0U && wrapped.write &&
             wrapped.write->destination.element_index == 0U,
         "helper itself does not skip J=-1; J+base wraps32");
  write_word(state, "bases", 0U, 0U);
  f.bindings.reference_tokens->source_address_bits = kReferences - 4U;
  const auto negative = execute_rac_moby_reference_store_v1(
      f.bindings, state, state.revision(), UINT32_MAX, 10U, kLimits);
  expect(negative.write && negative.write->destination.element_index == 0U,
         "negative-looking index must map to its actual wrapped address");
  f.bindings.reference_tokens->source_address_bits = 0U;
  const std::uint32_t index_to_zero = (0U - kReferences) >> 2U;
  expect(execute_rac_moby_reference_store_v1(
             f.bindings, state, state.revision(), index_to_zero, 11U, kLimits)
                 .write->destination.element_index == 0U,
         "address addition wrapping to zero must resolve only through explicit "
         "binding");
}

void test_whole_ranges_limits_schema_and_missing_values() {
  const auto f = fixture();
  for (unsigned variant = 0U; variant < 12U; ++variant) {
    auto binding = f.bindings;
    SessionStateV1 state(f.initial, kStateLimits);
    auto limits = kLimits;
    std::optional<std::uint32_t> token = 2U;
    switch (variant) {
    case 0U:
      binding.level_base_words->word_count += 1U;
      break;
    case 1U:
      binding.reference_tokens->word_count += 1U;
      break;
    case 2U:
      binding.reference_tokens->first_element = UINT64_MAX;
      break;
    case 3U:
      binding.reference_tokens->source_address_bits = 0xfffffffcU;
      break;
    case 4U:
      binding.level_base_words->source_address_bits += 1U;
      break;
    case 5U:
      binding.level_base_words.reset();
      break;
    case 6U:
      binding.reference_tokens.reset();
      break;
    case 7U:
      token.reset();
      break;
    case 8U:
      binding.state_schema_sha256[0U] ^= std::byte{1U};
      break;
    case 9U:
      binding.current_level_word.reset();
      break;
    case 10U:
      limits.max_window_words = 3U;
      break;
    case 11U:
      limits.max_key_bytes = 0U;
      break;
    }
    expect_error_unchanged(state, [&] {
      (void)execute_rac_moby_reference_store_v1(binding, state, 0U, 0U, token,
                                                limits);
    });
  }
  auto binding = f.bindings;
  binding.level_base_words->view_key = "missing";
  SessionStateV1 state(f.initial, kStateLimits);
  expect_error_unchanged(state, [&] {
    (void)execute_rac_moby_reference_store_v1(binding, state, 0U, 0U, 4U,
                                              kLimits);
  });
  binding = f.bindings;
  binding.reference_tokens->first_element = 1U;
  binding.reference_tokens->word_count = 11U;
  const auto subrange =
      execute_rac_moby_reference_store_v1(binding, state, 0U, 10U, 5U, kLimits);
  expect(subrange.write && subrange.write->destination.element_index == 11U,
         "whole owned subrange and last destination element should succeed");
  expect_error_unchanged(state, [&] {
    (void)execute_rac_moby_reference_store_v1(binding, state, state.revision(),
                                              11U, 4U, kLimits);
  });
}

void test_aliases_and_reference_storage_contract() {
  for (unsigned variant = 0U; variant < 5U; ++variant) {
    auto f = fixture();
    if (variant == 0U) {
      f.initial.schema.views.push_back({"scalar-alias", "references",
                                        SessionStateValueTypeV1::u8, 47U, 1U,
                                        1U});
    } else if (variant == 1U) {
      f.bindings.level_base_words->view_key = "references";
      f.bindings.level_base_words->word_count = 1U;
    } else if (variant == 2U) {
      f.bindings.current_level_word = PlacementStateElementV1{"references", 0U};
      f.initial.buffers[2U].bytes[1U] =
          std::byte{0U}; // level0 instead of token0x100.
    } else if (variant == 3U) {
      f.bindings.reference_tokens->source_address_bits = kBases;
    } else {
      // A valid SessionState view, but not the helper's contiguous-word domain.
      f.initial.schema.views[2U].byte_stride = 8U;
      f.initial.schema.buffers[2U].byte_count = 96U;
      f.initial.buffers[2U].bytes.resize(96U, std::byte{0U});
    }
    f.bindings.state_schema_sha256 =
        hash_session_state_schema_v1(f.initial.schema, kStateLimits);
    SessionStateV1 state(f.initial, kStateLimits);
    expect_error_unchanged(state, [&] {
      (void)execute_rac_moby_reference_store_v1(f.bindings, state, 0U, 0U, 9U,
                                                kLimits);
    });
  }
}

} // namespace

int main() {
  try {
    test_ordered_mutation_and_current_state();
    test_signed_gate_and_reached_bindings();
    test_wrapping_index_and_address_arithmetic();
    test_whole_ranges_limits_schema_and_missing_values();
    test_aliases_and_reference_storage_contract();
    std::cout << "rac_moby_reference_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rac_moby_reference_tests: " << error.what() << '\n';
    return 1;
  }
}
