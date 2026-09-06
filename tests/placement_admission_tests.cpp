#include "openrc/placement_admission.hpp"

#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace openrc;
using namespace openrc::game;
constexpr SessionStateLimitsV1 kStateLimits{1U,  4U,  64U,  1024U, 64U,
                                            64U, 64U, 128U, 4U};
constexpr PlacementAdmissionLimitsV1 kLimits{64U, 4U};

void expect(const bool value, const std::string_view message) {
  if (!value) {
    throw std::runtime_error(std::string(message));
  }
}

template <class Callback>
void expect_error(Callback &&callback, const std::string_view fragment = {}) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const PlacementAdmissionError &error) {
    expect(std::string_view(error.what()).find(fragment) !=
               std::string_view::npos,
           "Unexpected placement admission diagnostic");
    return;
  }
  throw std::runtime_error("Malformed placement admission was accepted");
}

[[nodiscard]] SessionStateV1
make_state(const std::array<std::uint16_t, 4U> keys = {0x1111U, 0x2222U, 0U,
                                                       0x3333U}) {
  SessionStateInitialV1 initial;
  initial.schema.identity_key = "synthetic.placement/state";
  initial.schema.buffers = {{"shared", 32U}};
  initial.schema.views = {
      {"registration", "shared", SessionStateValueTypeV1::u16, 0U, 4U, 4U},
      {"bytes", "shared", SessionStateValueTypeV1::u8, 0U, 32U, 1U},
      {"words", "shared", SessionStateValueTypeV1::u32, 0U, 8U, 4U},
  };
  SessionStateBufferBytesV1 image{"shared", {}};
  for (const auto key : keys) {
    image.bytes.push_back(static_cast<std::byte>(key & 255U));
    image.bytes.push_back(static_cast<std::byte>(key >> 8U));
    image.bytes.push_back(std::byte{0xb7U});
    image.bytes.push_back(std::byte{0xa6U});
  }
  // Explicit non-registration bytes: selector alternatives, rejection byte,
  // one set-bit word, and one clear word. No default state is inferred.
  for (const auto byte : {0U, 255U, 1U, 0U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                          0x55U, 0x66U, 0x77U, 0x88U}) {
    image.bytes.push_back(static_cast<std::byte>(byte));
  }
  initial.buffers.push_back(std::move(image));
  return SessionStateV1(initial, kStateLimits);
}

[[nodiscard]] PlacementAdmissionPlanV1
plan_for(const SessionStateV1 &state, const std::uint32_t count = 7U) {
  PlacementAdmissionPlanV1 plan;
  plan.state_schema_sha256 = state.schema_sha256();
  plan.initial_count_bits = count;
  return plan;
}

[[nodiscard]] PlacementByteReadV1 byte_at(const std::uint64_t index) {
  return {PlacementStateElementV1{"bytes", index}};
}

[[nodiscard]] PlacementBitReadV1 bit_at(const std::uint64_t word,
                                        const std::uint8_t bit = 0U) {
  return {PlacementStateElementV1{"words", word}, bit};
}

[[nodiscard]] PlacementReverseRegistrationV1
registration(const std::optional<std::uint16_t> exact = std::uint16_t{0x55U},
             const std::uint16_t inserted = 0x55U) {
  return {PlacementStateElementV1{"registration", 0U}, 4U, exact, inserted};
}

[[nodiscard]] PlacementAdmissionResultV1
execute(const PlacementAdmissionPlanV1 &plan, SessionStateV1 &state) {
  return execute_placement_admission_v1(plan, state, state.revision(), kLimits);
}

void test_readonly_identity_revision_and_explicit_branches() {
  auto state = make_state();
  const auto before = state.snapshot();
  auto plan = plan_for(state, 0xffffffffU);
  const auto unchanged_plan = plan;
  expect(execute(plan, state) ==
             PlacementAdmissionResultV1{true, 0xffffffffU, 0xffffffffU, {}},
         "Unconditional step normalized count bits or rejected");
  expect(state.snapshot() == before && plan == unchanged_plan,
         "Read-only execution changed revision, bytes, or caller plan");
  expect_error(
      [&] { (void)execute_placement_admission_v1(plan, state, 1U, kLimits); },
      "stale");
  plan.state_schema_sha256[0U] ^= std::byte{1U};
  expect_error([&] { (void)execute(plan, state); }, "schema digest");
  expect(state.snapshot() == before, "Read-only entry rejection mutated state");

  plan = plan_for(state);
  plan.reject_when_nonzero = byte_at(18U);
  plan.condition = PlacementCountSelectionV1{{}, 255U, {}, {}};
  expect(
      !execute(plan, state).admitted && state.snapshot() == before,
      "Early byte rejection resolved an unreachable selector or wrote state");
  plan.reject_when_nonzero = byte_at(16U);
  plan.condition = PlacementRequireBitClearV1{bit_at(5U)};
  expect(!execute(plan, state).admitted,
         "Set bit did not reject a clear-bit condition");
  plan.condition = PlacementRequireBitClearV1{bit_at(6U)};
  expect(execute(plan, state).admitted,
         "Clear bit rejected a clear-bit condition");
  plan.condition = PlacementRequireBitClearV1{bit_at(3U, 31U)};
  expect(!execute(plan, state).admitted,
         "Unsigned word bit 31 was interpreted incorrectly");

  // An absent chosen arm is an ordinary skip. Neither its count nor another
  // arm's unavailable word is evaluated, and initial counts remain intact.
  plan.condition = PlacementCountSelectionV1{
      byte_at(17U), 255U, {}, PlacementCountAdjustmentV1{99U, {}}};
  expect(execute(plan, state) == PlacementAdmissionResultV1{false, 7U, 7U, {}},
         "Absent equal arm changed initial counts or reached the other arm");
  plan.condition = PlacementCountSelectionV1{
      byte_at(16U), 255U, PlacementCountAdjustmentV1{99U, {}}, {}};
  expect(
      execute(plan, state) == PlacementAdmissionResultV1{false, 7U, 7U, {}},
      "Absent otherwise arm changed initial counts or reached the other arm");
  plan.condition = PlacementCountSelectionV1{
      byte_at(17U), 255U, PlacementCountAdjustmentV1{22U, bit_at(5U)},
      PlacementCountAdjustmentV1{9U, {}}};
  expect(execute(plan, state) == PlacementAdmissionResultV1{true, 22U, 11U, {}},
         "Selected equal arm did not replace maximum/current independently");
  plan.condition = PlacementCountSelectionV1{
      byte_at(16U), 255U, PlacementCountAdjustmentV1{99U, {}},
      PlacementCountAdjustmentV1{9U, bit_at(6U)}};
  expect(execute(plan, state) == PlacementAdmissionResultV1{true, 9U, 9U, {}},
         "Selected otherwise arm with a clear bit changed the count");
  expect(state.snapshot() == before,
         "Conditional read-only steps mutated state");
}

void test_reverse_registration_and_no_auxiliary_reads() {
  auto state = make_state({0x44U, 0x55U, 0x66U, 0U});
  const auto before = state.snapshot();
  auto plan = plan_for(state);
  plan.registration = registration();
  const auto result = execute(plan, state);
  expect(result.registration_write ==
                 PlacementRegistrationWriteV1{3U, 0U, 0x55U} &&
             state.read_u16("registration", 3U) == 0x55U &&
             state.read_u16("registration", 1U) == 0x55U &&
             state.revision() == 1U,
         "Registration preferred an earlier exact key over the first reverse "
         "vacancy");
  expect(state.read_u32("words", 3U) == 0xa6b70055U,
         "Registration read/wrote the adjacent auxiliary halfword");
  for (std::size_t index = 0U; index < 32U; ++index) {
    if (index != 12U && index != 13U) {
      expect(state.buffer_bytes("shared")[index] ==
                 before.buffers[0U].bytes[index],
             "Registration changed bytes outside its chosen key");
    }
  }
  const auto same = execute(plan, state);
  expect(same.registration_write ==
                 PlacementRegistrationWriteV1{3U, 0x55U, 0x55U} &&
             state.revision() == 2U,
         "Repeated identical source write was suppressed instead of advancing "
         "revision");

  auto full = make_state({1U, 2U, 3U, 4U});
  plan = plan_for(full);
  plan.registration = registration(std::nullopt, 9U);
  const auto full_before = full.snapshot();
  expect(!execute(plan, full).registration_write &&
             full.snapshot() == full_before,
         "Full registration pool fabricated a slot or a write");
  plan.registration = registration(std::uint16_t{2U}, 0xffffU);
  plan.registration->row->element_index = 1U;
  plan.registration->slot_count = 3U;
  expect(execute(plan, full).registration_write ==
                 PlacementRegistrationWriteV1{0U, 2U, 0xffffU} &&
             full.read_u16("registration", 1U) == 0xffffU,
         "Registration row base or relative trace index is incorrect");
  auto zero = make_state({1U, 2U, 3U, 0U});
  plan = plan_for(zero);
  plan.registration = registration(std::uint16_t{0U}, 0U);
  expect(
      execute(plan, zero).registration_write ==
              PlacementRegistrationWriteV1{3U, 0U, 0U} &&
          zero.revision() == 1U,
      "Explicit zero exact-match/insert was treated as unavailable or a no-op");
}

void test_registration_aliases_and_skip_commit() {
  auto state = make_state();
  auto plan = plan_for(state);
  plan.registration = registration(std::nullopt, 1U);
  plan.reject_when_nonzero = byte_at(8U);
  plan.condition = PlacementRequireBitClearV1{{}};
  const auto rejected = execute(plan, state);
  expect(!rejected.admitted && rejected.maximum_bits == 7U &&
             rejected.current_bits == 7U &&
             rejected.registration_write ==
                 PlacementRegistrationWriteV1{2U, 0U, 1U} &&
             state.read_u8("bytes", 8U) == 1U && state.revision() == 1U,
         "Later rejection byte read stale pre-registration data or lost the "
         "skipped step write");

  state = make_state();
  plan = plan_for(state);
  plan.registration = registration(std::nullopt, 1U);
  plan.condition = PlacementCountSelectionV1{
      byte_at(16U), 255U, {}, PlacementCountAdjustmentV1{7U, bit_at(2U)}};
  expect(execute(plan, state).current_bits == 4U &&
             state.read_u32("words", 2U) == 0xa6b70001U,
         "Count-bit read did not see the overlapping registration key write");

  state = make_state();
  plan = plan_for(state);
  plan.registration = registration(std::nullopt, 255U);
  plan.condition = PlacementCountSelectionV1{
      byte_at(8U), 255U, PlacementCountAdjustmentV1{22U, bit_at(2U)},
      PlacementCountAdjustmentV1{7U, bit_at(6U)}};
  const auto selected = execute(plan, state);
  expect(selected.admitted && selected.maximum_bits == 22U &&
             selected.current_bits == 11U,
         "Selector-byte and then count-word did not share canonical mutated "
         "storage");

  state = make_state();
  plan = plan_for(state);
  plan.registration = registration(std::nullopt, 1U);
  plan.condition = PlacementRequireBitClearV1{bit_at(2U)};
  expect(!execute(plan, state).admitted && state.revision() == 1U,
         "Bit rejection failed to commit its preceding registration write");
}

void test_wrapping_count_bit_patterns() {
  auto state = make_state();
  const auto before = state.snapshot();
  constexpr std::array cases{
      std::pair{0U, 0U},
      std::pair{1U, 1U},
      std::pair{2U, 1U},
      std::pair{7U, 4U},
      std::pair{0x40000001U, 0x20000001U},
      std::pair{0x7ffffffeU, 0x3fffffffU},
      std::pair{0x7fffffffU, 0xc0000000U},
      std::pair{0x80000000U, 0xc0000001U},
      std::pair{0xfffffffcU, 0xffffffffU},
      std::pair{0xfffffffdU, 0xffffffffU},
      std::pair{0xfffffffeU, 0U},
      std::pair{0xffffffffU, 0U},
  };
  for (const auto [maximum, adjusted] : cases) {
    auto plan = plan_for(state, 0x12345678U);
    plan.condition = PlacementCountSelectionV1{
        byte_at(17U),
        255U,
        PlacementCountAdjustmentV1{maximum, bit_at(5U)},
        {}};
    expect(
        execute(plan, state) ==
            PlacementAdmissionResultV1{true, maximum, adjusted, {}},
        "Wrapping add/sign-adjust/arithmetic-shift count expression diverged");
    std::get<PlacementCountSelectionV1>(plan.condition)
        .when_equal->adjust_when_set = bit_at(6U);
    expect(execute(plan, state) ==
               PlacementAdmissionResultV1{true, maximum, maximum, {}},
           "Clear count bit normalized a negative or overflowing count bit "
           "pattern");
  }
  expect(state.snapshot() == before,
         "Count transformation changed session bytes/revision");
}

void test_missing_reached_inputs_and_late_error_rollback() {
  auto state = make_state();
  const auto before = state.snapshot();
  const auto before_hash = state.state_sha256();
  auto base = plan_for(state);
  base.registration = registration();
  std::vector<PlacementAdmissionPlanV1> invalid;
  auto plan = base;
  plan.registration->row.reset();
  invalid.push_back(plan);
  plan = base;
  plan.registration->row->view_key = "bytes";
  invalid.push_back(plan);
  plan = base;
  plan.registration->row->element_index = 1U;
  invalid.push_back(plan);
  plan = base;
  plan.reject_when_nonzero = PlacementByteReadV1{};
  invalid.push_back(plan);
  plan = base;
  plan.reject_when_nonzero =
      PlacementByteReadV1{PlacementStateElementV1{"missing", 0U}};
  invalid.push_back(plan);
  plan = base;
  plan.reject_when_nonzero = byte_at(32U);
  invalid.push_back(plan);
  plan = base;
  plan.reject_when_nonzero =
      PlacementByteReadV1{PlacementStateElementV1{"words", 0U}};
  invalid.push_back(plan);
  plan = base;
  plan.condition = PlacementRequireBitClearV1{{}};
  invalid.push_back(plan);
  plan = base;
  plan.condition =
      PlacementRequireBitClearV1{{PlacementStateElementV1{"bytes", 0U}, 0U}};
  invalid.push_back(plan);
  plan = base;
  plan.condition = PlacementRequireBitClearV1{bit_at(UINT64_MAX)};
  invalid.push_back(plan);
  plan = base;
  plan.condition = PlacementCountSelectionV1{{}, 255U, {}, {}};
  invalid.push_back(plan);
  plan = base;
  plan.condition = PlacementCountSelectionV1{
      byte_at(16U), 255U, {}, PlacementCountAdjustmentV1{9U, {}}};
  invalid.push_back(plan);
  for (const auto &candidate : invalid) {
    const auto original = candidate;
    validate_placement_admission_plan_v1(candidate, kLimits);
    expect_error([&] { (void)execute(candidate, state); });
    expect(state.snapshot() == before && state.state_sha256() == before_hash &&
               candidate == original,
           "Reached input error left an early write, revision, or caller-plan "
           "mutation");
  }

  // Unknown/unavailable words in the unselected arm are not dependencies.
  plan = base;
  plan.condition = PlacementCountSelectionV1{
      byte_at(17U), 255U, PlacementCountAdjustmentV1{12U, bit_at(6U)},
      PlacementCountAdjustmentV1{
          19U, {PlacementStateElementV1{"not-mounted", UINT64_MAX}, 31U}}};
  expect(execute(plan, state).current_bits == 12U && state.revision() == 1U,
         "Unselected view was resolved or bounded as a reached state access");

  auto exhausted = state.snapshot();
  exhausted.revision = UINT64_MAX;
  state.restore_snapshot(exhausted);
  auto readonly = plan_for(state);
  expect(execute(readonly, state).admitted && state.snapshot() == exhausted,
         "Read-only step rejected an exhausted write revision");
  expect_error(
      [&] {
        (void)execute_placement_admission_v1(readonly, state, UINT64_MAX - 1U,
                                             kLimits);
      },
      "stale");
  expect_error([&] { (void)execute(plan, state); }, "exhausted");
  expect(state.snapshot() == exhausted,
         "Revision overflow left a partial admission write");
}

void test_shape_validation_is_bounded_but_not_state_resolution() {
  auto state = make_state();
  const auto base = plan_for(state);
  std::vector<PlacementAdmissionPlanV1> invalid;
  auto plan = base;
  plan.schema_version = 2U;
  invalid.push_back(plan);
  plan = base;
  plan.state_schema_sha256 = {};
  invalid.push_back(plan);
  plan = base;
  plan.registration = registration();
  plan.registration->slot_count = 0U;
  invalid.push_back(plan);
  plan = base;
  plan.registration = registration();
  plan.registration->slot_count = 5U;
  invalid.push_back(plan);
  plan = base;
  plan.registration = registration();
  plan.registration->row->element_index = UINT64_MAX;
  invalid.push_back(plan);
  plan = base;
  plan.condition = PlacementRequireBitClearV1{{{}, 32U}};
  invalid.push_back(plan);
  plan = base;
  plan.condition = PlacementCountSelectionV1{
      byte_at(17U), 255U, {}, PlacementCountAdjustmentV1{0U, {{}, 255U}}};
  invalid.push_back(plan);
  for (const auto &key :
       {std::string{}, std::string{"white space"}, std::string{"line\nbreak"},
        std::string(1U, '\0'), std::string(1U, static_cast<char>(0x80)),
        std::string(65U, 'a')}) {
    plan = base;
    plan.condition = PlacementCountSelectionV1{
        byte_at(17U),
        255U,
        {},
        PlacementCountAdjustmentV1{0U, {PlacementStateElementV1{key, 0U}, 0U}}};
    invalid.push_back(plan);
  }
  const auto snapshot = state.snapshot();
  for (const auto &candidate : invalid) {
    const auto original = candidate;
    expect_error(
        [&] { validate_placement_admission_plan_v1(candidate, kLimits); });
    expect_error([&] { (void)execute(candidate, state); });
    expect(candidate == original && state.snapshot() == snapshot,
           "Malformed plan validation mutated state/input");
  }
  for (const auto limits :
       {PlacementAdmissionLimitsV1{}, PlacementAdmissionLimitsV1{0U, 4U},
        PlacementAdmissionLimitsV1{64U, 0U}}) {
    expect_error([&] { validate_placement_admission_plan_v1(base, limits); },
                 "positive");
  }
  plan = base;
  plan.registration = registration();
  plan.registration->row->element_index = UINT64_MAX;
  plan.registration->slot_count = 1U;
  validate_placement_admission_plan_v1(plan, kLimits);
  expect_error([&] { (void)execute(plan, state); }, "index");
  plan.registration->row.reset();
  validate_placement_admission_plan_v1(plan, kLimits);
  expect_error([&] { (void)execute(plan, state); }, "reached");
}

} // namespace

int main() {
  try {
    test_readonly_identity_revision_and_explicit_branches();
    test_reverse_registration_and_no_auxiliary_reads();
    test_registration_aliases_and_skip_commit();
    test_wrapping_count_bit_patterns();
    test_missing_reached_inputs_and_late_error_rollback();
    test_shape_validation_is_bounded_but_not_state_resolution();
    std::cout << "Placement admission tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Placement admission test failure: " << error.what() << '\n';
    return 1;
  }
}
