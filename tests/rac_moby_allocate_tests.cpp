#include "openrc/rac_moby_allocate.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace openrc;
using namespace openrc::game;

constexpr SessionStateLimitsV1 kStateLimits{
    8U, 16U, 64U, 4096U, 65536U, 262144U, 65536U, 131072U, 64U};
constexpr RacMobyAllocateLimitsV1 kLimits{64U, 256U};
constexpr std::uint32_t kBegin = 0x300080U;
constexpr std::uint32_t kPvar = 0x400004U;

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void append_word(std::vector<std::byte> &bytes, std::uint32_t value) {
  for (unsigned shift = 0U; shift < 32U; shift += 8U)
    bytes.push_back(static_cast<std::byte>((value >> shift) & 255U));
}

struct Fixture {
  SessionStateInitialV1 initial;
  RacMobyAllocateBindingsV1 bindings;
};

Fixture fixture(const std::vector<std::uint8_t> &statuses = {0xffU, 0x12U,
                                                             0x34U},
                std::uint32_t timer = 0U, std::uint32_t count = 2U) {
  expect(!statuses.empty(), "test fixture requires a physical guard");
  const auto n = statuses.size();
  Fixture f;
  f.initial.schema.identity_key = "test.allocate-staged";
  f.initial.schema.buffers = {{"status", n + 2U},
                              {"packed", n * 8U + 8U},
                              {"pvar", n * 128U + 8U},
                              {"scalars", 16U}};
  f.initial.schema.views = {
      {"status", "status", SessionStateValueTypeV1::u8, 0U, n + 2U, 1U},
      {"packed", "packed", SessionStateValueTypeV1::u32, 0U, n * 2U + 2U, 4U},
      {"pvar", "pvar", SessionStateValueTypeV1::u32, 0U, n * 32U + 2U, 4U},
      {"scalars", "scalars", SessionStateValueTypeV1::u32, 0U, 4U, 4U}};
  f.initial.buffers = {{"status", {std::byte{0xa5U}}},
                       {"packed", {}},
                       {"pvar", {}},
                       {"scalars", {}}};
  for (auto status : statuses)
    f.initial.buffers[0U].bytes.push_back(static_cast<std::byte>(status));
  f.initial.buffers[0U].bytes.push_back(std::byte{0x5aU});
  append_word(f.initial.buffers[1U].bytes, 0xcafebabeU);
  for (std::size_t i = 0U; i < n * 2U; ++i)
    append_word(f.initial.buffers[1U].bytes, 0U);
  append_word(f.initial.buffers[1U].bytes, 0xdeadbeefU);
  for (std::size_t i = 0U; i < n * 32U + 2U; ++i)
    append_word(f.initial.buffers[2U].bytes,
                0xabc00000U + static_cast<std::uint32_t>(i));
  for (auto value : std::array{0x12345678U, timer, count, 0x87654321U})
    append_word(f.initial.buffers[3U].bytes, value);
  f.bindings.state_schema_sha256 =
      hash_session_state_schema_v1(f.initial.schema, kStateLimits);
  f.bindings.cursor = {kBegin,
                       kBegin + static_cast<std::uint32_t>(n - 1U) * 256U};
  f.bindings.first_status = PlacementStateElementV1{"status", 1U};
  f.bindings.first_packed_word = PlacementStateElementV1{"packed", 1U};
  f.bindings.current_timer_word = PlacementStateElementV1{"scalars", 1U};
  f.bindings.current_count_word = PlacementStateElementV1{"scalars", 2U};
  f.bindings.first_pvar_word = PlacementStateElementV1{"pvar", 1U};
  f.bindings.all_actor_base_bits = kBegin - 3U * 256U;
  f.bindings.pvar_base_bits = kPvar;
  return f;
}

RacMobyAllocateClassV1 bound_class() {
  return {1964U, 17U, 0xabcdef01U, std::nullopt};
}

void write_word(SessionStateV1 &state, const char *key, std::uint64_t element,
                std::uint32_t bits) {
  const std::array batch{
      SessionStateWriteV1{key, element, SessionStateValueTypeV1::u32, bits}};
  state.apply_batch(batch, state.revision());
}

template <typename Callable>
void error_unchanged(SessionStateV1 &state, Callable &&callable) {
  const auto before = state.snapshot();
  bool failed = false;
  try {
    callable();
  } catch (const RacMobyAllocateError &) {
    failed = true;
  }
  expect(failed, "unsupported allocator input did not fail explicitly");
  expect(state.snapshot() == before,
         "failed allocator changed staged bytes/revision");
}

void verify_cleared_pvar(const SessionStateV1 &state, std::uint32_t count,
                         std::uint32_t selected) {
  for (std::uint32_t i = 0U; i < count * 32U + 2U; ++i) {
    const bool cleared =
        i >= 1U + selected * 32U && i < 1U + (selected + 1U) * 32U;
    expect(state.read_u32("pvar", i) == (cleared ? 0U : 0xabc00000U + i),
           "PVar clear changed the wrong word or guard");
  }
}

void test_fresh_first_ff_and_staged_effects() {
  auto f = fixture();
  SessionStateV1 state(f.initial, kStateLimits);
  const auto output = execute_rac_moby_allocate_v1(f.bindings, state, 0U,
                                                   bound_class(), kLimits);
  expect(output.examined_slots == 1U && output.timer_reads == 1U &&
             output.fresh,
         "first FF should allocate after one timer read");
  const auto &fresh = *output.fresh;
  expect(
      fresh.physical_slot_index == 0U && fresh.previous_status == 0xffU &&
          fresh.advanced_ff_marker && fresh.constructor.live_index_bits == 3U &&
          fresh.constructor.class_id_low16 == 1964U &&
          fresh.constructor.callback_reference == 0xabcdef01U &&
          fresh.constructor.flags == 5U &&
          fresh.first_pvar_word == PlacementStateElementV1{"pvar", 1U} &&
          fresh.previous_count_bits == 2U && fresh.remaining_count_bits == 1U,
      "fresh constructor, distinct base index or neutral PVar binding differs");
  expect(state.read_u8("status", 0U) == 0xa5U &&
             state.read_u8("status", 1U) == 0U &&
             state.read_u8("status", 2U) == 0xffU &&
             state.read_u8("status", 3U) == 0x34U &&
             state.read_u8("status", 4U) == 0x5aU &&
             state.read_u32("packed", 0U) == 0xcafebabeU &&
             state.read_u32("packed", 1U) == 0U &&
             state.read_u32("packed", 2U) == 0x00404040U &&
             state.read_u32("packed", 7U) == 0xdeadbeefU &&
             state.read_u32("scalars", 2U) == 1U && state.revision() == 1U,
         "FF marker, packed fields, guards or atomic revision differs");
  verify_cleared_pvar(state, 3U, 0U);
  const auto second = execute_rac_moby_allocate_v1(f.bindings, state, 1U,
                                                   bound_class(), kLimits);
  expect(second.fresh && second.fresh->physical_slot_index == 1U &&
             second.fresh->constructor.live_index_bits == 4U &&
             state.read_u8("status", 3U) == 0xffU &&
             state.read_u32("scalars", 2U) == 0U,
         "second call must read canonical marker/counter and write reserved "
         "guard");
  const auto before = state.snapshot();
  const auto exhausted = execute_rac_moby_allocate_v1(f.bindings, state, 2U,
                                                      std::nullopt, kLimits);
  expect(!exhausted.fresh && exhausted.examined_slots == 2U &&
             exhausted.timer_reads == 1U && state.snapshot() == before,
         "reserved final FF must never be selected; exhaustion is read-only");
}

void test_reused_fe_deadlines_and_timer_reads() {
  auto f = fixture({0U, 0xfeU, 0xffU, 0xfeU, 0x51U}, 0xffffffffU, 0U);
  SessionStateV1 state(f.initial, kStateLimits);
  write_word(state, "packed", 1U + 1U * 2U + 1U, 1U);
  write_word(state, "packed", 1U + 2U * 2U + 1U, 0xffffffffU);
  write_word(state, "packed", 1U + 3U * 2U, 0xffffffffU);
  const auto revision = state.revision();
  const auto result = execute_rac_moby_allocate_v1(f.bindings, state, revision,
                                                   bound_class(), kLimits);
  expect(result.fresh && result.examined_slots == 4U &&
             result.timer_reads == 3U &&
             result.fresh->physical_slot_index == 3U &&
             !result.fresh->advanced_ff_marker &&
             result.fresh->constructor.live_index_bits == 6U &&
             result.fresh->previous_count_bits == 0U &&
             result.fresh->remaining_count_bits == 0U &&
             state.read_u8("status", 5U) == 0x51U &&
             state.read_u32("scalars", 2U) == 0U &&
             state.revision() == revision + 1U,
         "full64 deadline, zeroextended timer, equality or FE marker behavior "
         "differs");
  verify_cleared_pvar(state, 5U, 3U);
  auto f2 = fixture({0xfeU, 0xffU, 0xfeU}, 4U, 9U);
  SessionStateV1 delayed(f2.initial, kStateLimits);
  write_word(delayed, "packed", 1U, 5U);
  write_word(delayed, "packed", 3U, 6U);
  const auto before = delayed.snapshot();
  const auto none = execute_rac_moby_allocate_v1(
      f2.bindings, delayed, delayed.revision(), std::nullopt, kLimits);
  expect(!none.fresh && none.examined_slots == 2U && none.timer_reads == 3U &&
             delayed.snapshot() == before,
         "ineligible FF must continue scanning then execute diagnostic timer "
         "read");
  write_word(delayed, "scalars", 1U, 5U);
  const auto current = execute_rac_moby_allocate_v1(
      f2.bindings, delayed, delayed.revision(), bound_class(), kLimits);
  expect(
      current.fresh && current.fresh->physical_slot_index == 0U,
      "a later allocation must read the updated timer, not a cached snapshot");
}

void test_all_statuses_counter_bits_and_constructor_variants() {
  for (std::uint32_t status = 0U; status <= 255U; ++status) {
    auto f = fixture({static_cast<std::uint8_t>(status), 0xfeU, 0x12U}, 0U,
                     UINT32_MAX);
    SessionStateV1 state(f.initial, kStateLimits);
    const auto r = execute_rac_moby_allocate_v1(f.bindings, state, 0U,
                                                bound_class(), kLimits);
    expect(r.fresh &&
               r.fresh->physical_slot_index == (status < 0xfeU ? 1U : 0U) &&
               r.fresh->remaining_count_bits == 0xfffffffeU,
           "status gate must be unsigned byte >=FE and counter is raw wrapping "
           "word");
  }
  for (auto flags :
       std::array<std::uint16_t, 4U>{0U, 0x4006U, 0x5000U, 0xffffU}) {
    for (auto frame_count : std::array<std::uint8_t, 3U>{0U, 1U, 2U}) {
      auto f = fixture({0xfeU, 0xffU}, 0U, 0x80000000U);
      SessionStateV1 state(f.initial, kStateLimits);
      auto cls = bound_class();
      cls.callback_reference = 0U;
      RacMobyFreshModelV1 model;
      model.reference = 0x123400U;
      model.spatial_reference = 0x123450U;
      model.auxiliary_reference = 0x76543210U;
      model.scale_bits = 0x7fc12345U;
      model.flags = flags;
      model.byte_06 = 1U;
      model.byte_0c = 1U;
      model.byte_0e = 9U;
      model.byte_0f = 1U;
      model.sequence0 =
          RacMobyFreshSequenceV1{0xfedcba98U, frame_count, 0xffU, 7U};
      cls.model = model;
      const auto r =
          execute_rac_moby_allocate_v1(f.bindings, state, 0U, cls, kLimits);
      const auto expected =
          construct_rac_moby_fresh_v1({cls.class_id_bits, cls.class_table_index,
                                       3U, cls.callback_reference, cls.model});
      expect(r.fresh && r.fresh->constructor == expected &&
                 r.fresh->remaining_count_bits == 0x7fffffffU &&
                 cls.model->flags == flags,
             "allocator must use actual complete constructor with current "
             "model inputs");
    }
  }
}

void test_signed_live_index_and_high_source_domains() {
  for (auto all_base : std::array<std::uint32_t, 4U>{
           kBegin + 1U, kBegin + 256U, kBegin + 0x80000000U, kBegin - 1U}) {
    auto f = fixture({0xfeU, 0xffU});
    f.bindings.all_actor_base_bits = all_base;
    SessionStateV1 state(f.initial, kStateLimits);
    const auto difference = kBegin - all_base;
    const auto expected =
        (difference >> 8U) | ((difference & 0x80000000U) ? 0xff000000U : 0U);
    const auto r = execute_rac_moby_allocate_v1(f.bindings, state, 0U,
                                                bound_class(), kLimits);
    expect(r.fresh && r.fresh->constructor.live_index_bits == expected &&
               r.fresh->constructor.live_index_counter_bits ==
                   (expected << 16U),
           "live index SUBU/SRA must preserve signed floor and word wrap");
  }
  auto f = fixture({0xfeU, 0xffU});
  f.bindings.cursor = {0x80000100U, 0x80000200U};
  f.bindings.all_actor_base_bits = 0x80000000U;
  f.bindings.pvar_base_bits = 0xffffff00U;
  SessionStateV1 state(f.initial, kStateLimits);
  const auto r = execute_rac_moby_allocate_v1(f.bindings, state, 0U,
                                              bound_class(), kLimits);
  expect(
      r.fresh && r.fresh->constructor.live_index_bits == 1U,
      "valid high source domains must not be confused with negative pointers");
}

void test_empty_exhausted_and_unreached_inputs() {
  for (auto cursor :
       std::array{RacMobyAllocateCursorV1{3U, 3U},
                  RacMobyAllocateCursorV1{0x80000000U, 0x7fffffffU}}) {
    auto f = fixture();
    f.bindings.cursor = cursor;
    f.bindings.first_status.reset();
    f.bindings.first_packed_word.reset();
    f.bindings.first_pvar_word.reset();
    f.bindings.current_count_word.reset();
    f.bindings.all_actor_base_bits.reset();
    f.bindings.pvar_base_bits.reset();
    SessionStateV1 state(f.initial, kStateLimits);
    const auto before = state.snapshot();
    const auto r = execute_rac_moby_allocate_v1(f.bindings, state, 0U,
                                                std::nullopt, kLimits);
    expect(!r.fresh && r.examined_slots == 0U && r.timer_reads == 1U &&
               state.snapshot() == before,
           "empty/reversed source pool requires only diagnostic timer input");
    f.bindings.current_timer_word.reset();
    error_unchanged(state, [&] {
      (void)execute_rac_moby_allocate_v1(f.bindings, state, 0U, std::nullopt,
                                         kLimits);
    });
  }
  auto f = fixture({0U, 0xfdU, 0xffU});
  f.bindings.first_packed_word.reset();
  f.bindings.first_pvar_word.reset();
  f.bindings.current_count_word.reset();
  SessionStateV1 state(f.initial, kStateLimits);
  const auto r = execute_rac_moby_allocate_v1(f.bindings, state, 0U,
                                              std::nullopt, kLimits);
  expect(!r.fresh && r.examined_slots == 2U && r.timer_reads == 1U,
         "active slots must not require packed/PVar/constructor/counter reads");
}

void test_fail_closed_ranges_aliases_limits_and_atomicity() {
  auto f = fixture();
  SessionStateV1 state(f.initial, kStateLimits);
  const auto run = [&](const RacMobyAllocateBindingsV1 &b,
                       RacMobyAllocateLimitsV1 l = kLimits) {
    error_unchanged(state, [&] {
      (void)execute_rac_moby_allocate_v1(b, state, 0U, bound_class(), l);
    });
  };
  run(f.bindings, {0U, 256U});
  run(f.bindings, {64U, 2U});
  auto b = f.bindings;
  b.state_schema_sha256[0U] ^= std::byte{1U};
  run(b);
  error_unchanged(state, [&] {
    (void)execute_rac_moby_allocate_v1(f.bindings, state, 1U, bound_class(),
                                       kLimits);
  });
  for (unsigned which = 0U; which < 7U; ++which) {
    b = f.bindings;
    if (which == 0U)
      b.first_status.reset();
    if (which == 1U)
      b.first_packed_word.reset();
    if (which == 2U)
      b.current_timer_word.reset();
    if (which == 3U)
      b.current_count_word.reset();
    if (which == 4U)
      b.first_pvar_word.reset();
    if (which == 5U)
      b.all_actor_base_bits.reset();
    if (which == 6U)
      b.pvar_base_bits.reset();
    run(b);
  }
  error_unchanged(state, [&] {
    (void)execute_rac_moby_allocate_v1(f.bindings, state, 0U, std::nullopt,
                                       kLimits);
  });
  auto bad_class = bound_class();
  bad_class.model = RacMobyFreshModelV1{};
  error_unchanged(state, [&] {
    (void)execute_rac_moby_allocate_v1(f.bindings, state, 0U, bad_class,
                                       kLimits);
  });
  for (unsigned which = 0U; which < 9U; ++which) {
    b = f.bindings;
    if (which == 0U)
      b.first_status->element_index = 3U; // selected fits, whole guard does not
    if (which == 1U)
      b.first_packed_word->element_index = 3U;
    if (which == 2U)
      b.first_pvar_word->element_index = 4U;
    if (which == 3U)
      b.first_pvar_word->element_index = UINT64_MAX;
    if (which == 4U)
      b.current_count_word = b.current_timer_word;
    if (which == 5U)
      b.first_packed_word->view_key = "status";
    if (which == 6U)
      b.first_status->view_key = "no view";
    if (which == 7U)
      b.cursor.dynamic_begin_bits += 1U;
    if (which == 8U)
      b.cursor.exclusive_end_bits += 8U;
    run(b);
  }
  for (auto pvar_base : std::array<std::uint32_t, 5U>{
           kPvar + 1U, kBegin, UINT32_MAX - 127U, 0x7fffff80U, 0x7fffffc0U}) {
    b = f.bindings;
    b.pvar_base_bits = pvar_base;
    run(b);
  }
  b = f.bindings;
  b.cursor = {0x7fffff00U,
              0x80000100U}; // last pointer increment in actor clear traps
  run(b);
  b = f.bindings;
  b.cursor = {0xfffffe08U, 0xffffff08U}; // complete final guard would wrap
  run(b);
  b = f.bindings;
  b.cursor = {
      0x160000U,
      0x160100U}; // fresh clear would change the later re-read bases/count
  run(b);
  for (auto pvar_base : std::array<std::uint32_t, 6U>{
           0x15f6b0U, 0x16007cU, 0x160098U, 0x16009cU, 0x1600a0U, 0x1600a8U}) {
    b = f.bindings;
    b.pvar_base_bits = pvar_base;
    run(b);
  }
  auto limited = kStateLimits;
  limited.max_batch_writes = 36U; // this FF + nonzero-counter path has37 writes
  SessionStateV1 small_batch(f.initial, limited);
  error_unchanged(small_batch, [&] {
    (void)execute_rac_moby_allocate_v1(f.bindings, small_batch, 0U,
                                       bound_class(), kLimits);
  });
  // Alias an unselected guard byte via a distinct view: complete ownership,
  // not just the first selected element, must fail before any mutation.
  f.initial.schema.views.push_back(
      {"guard.alias", "status", SessionStateValueTypeV1::u8, 3U, 1U, 1U});
  f.bindings.state_schema_sha256 =
      hash_session_state_schema_v1(f.initial.schema, kStateLimits);
  SessionStateV1 aliased(f.initial, kStateLimits);
  error_unchanged(aliased, [&] {
    (void)execute_rac_moby_allocate_v1(f.bindings, aliased, 0U, bound_class(),
                                       kLimits);
  });
}

} // namespace

int main() {
  try {
    test_fresh_first_ff_and_staged_effects();
    test_reused_fe_deadlines_and_timer_reads();
    test_all_statuses_counter_bits_and_constructor_variants();
    test_signed_live_index_and_high_source_domains();
    test_empty_exhausted_and_unreached_inputs();
    test_fail_closed_ranges_aliases_limits_and_atomicity();
    std::cout << "rac_moby_allocate_tests: PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rac_moby_allocate_tests: " << error.what() << '\n';
    return 1;
  }
}
