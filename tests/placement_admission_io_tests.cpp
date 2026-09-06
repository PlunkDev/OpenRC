#include "openrc/placement_admission_io.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

namespace {

using Bytes = std::vector<std::byte>;
using Plan = openrc::PlacementAdmissionPlanV1;
using Element = openrc::PlacementStateElementV1;
constexpr openrc::PlacementAdmissionIoLimitsV1 kLimits{4096U, {64U, 64U}};

void expect(const bool value, const std::string &message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}

template <class Operation>
void rejected(Operation operation, const std::string &description) {
  try {
    operation();
  } catch (const openrc::PlacementAdmissionIoError &) {
    return;
  }
  throw std::runtime_error("Accepted " + description);
}

void write32(Bytes &bytes, const std::size_t offset, const std::uint32_t value) {
  for (unsigned i = 0U; i < 4U; ++i) {
    bytes.at(offset + i) = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
  }
}

void write64(Bytes &bytes, const std::size_t offset, const std::uint64_t value) {
  for (unsigned i = 0U; i < 8U; ++i) {
    bytes.at(offset + i) = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
  }
}

std::uint32_t read32(const Bytes &bytes, const std::size_t offset) {
  std::uint32_t result = 0U;
  for (unsigned i = 0U; i < 4U; ++i) {
    result |= std::to_integer<std::uint32_t>(bytes.at(offset + i)) << (i * 8U);
  }
  return result;
}

// Intentionally re-sign malformed structure so tests cannot pass merely
// because every edit invalidates the integrity field.
void resign(Bytes &bytes) {
  std::fill_n(bytes.begin() + 0x20U, 32U, std::byte{0});
  const auto digest = openrc::prepared_content_sha256_v1(bytes);
  std::copy(digest.begin(), digest.end(), bytes.begin() + 0x20U);
}

Plan base_plan() {
  Plan plan;
  for (std::size_t i = 0U; i < plan.state_schema_sha256.size(); ++i) {
    plan.state_schema_sha256[i] = static_cast<std::byte>(i + 1U);
  }
  plan.initial_count_bits = 0x80000001U;
  return plan;
}

Plan full_plan() {
  auto plan = base_plan();
  plan.registration = openrc::PlacementReverseRegistrationV1{
      Element{"slots", 7U}, 64U, 0xffffU, 0x8000U};
  plan.reject_when_nonzero = openrc::PlacementByteReadV1{Element{"bytes", 11U}};
  plan.condition = openrc::PlacementCountSelectionV1{
      {Element{"bytes", 1U}}, 0xffU,
      openrc::PlacementCountAdjustmentV1{
          0xffffffffU, {Element{"words", 23U}, 31U}},
      openrc::PlacementCountAdjustmentV1{
          0x12345678U, {Element{"words", 42U}, 0U}}};
  return plan;
}

void check_roundtrip(const Plan &plan) {
  const auto saved = plan;
  const auto bytes = openrc::encode_placement_admission_plan_v1(plan, kLimits);
  expect(plan == saved, "Encoder mutated its input");
  const auto decoded = openrc::decode_placement_admission_plan_v1(bytes, kLimits);
  expect(decoded == plan, "Round trip changed a field, kind or presence state");
  expect(openrc::encode_placement_admission_plan_v1(decoded, kLimits) == bytes,
           "Repeated encoding changed canonical bytes");
  auto unsigned_bytes = bytes;
  std::fill_n(unsigned_bytes.begin() + 0x20U, 32U, std::byte{0});
  const auto digest = openrc::prepared_content_sha256_v1(unsigned_bytes);
  expect(std::equal(digest.begin(), digest.end(), bytes.begin() + 0x20U),
           "Digest is not SHA-256 of the entire zero-self-field artifact");
}

void test_all_presence_and_condition_shapes() {
  std::vector<Plan> conditions{base_plan()};
  for (const bool present : {false, true}) {
    auto plan = base_plan();
    plan.condition = openrc::PlacementRequireBitClearV1{
        {present ? std::optional<Element>(Element{"words", ~std::uint64_t{0}})
                 : std::nullopt,
         31U}};
    conditions.push_back(plan);
  }
  // Each arm can reject, require an unavailable word, or name an exact word.
  for (const bool selector_present : {false, true}) {
    for (unsigned equal = 0U; equal < 3U; ++equal) {
      for (unsigned other = 0U; other < 3U; ++other) {
        auto plan = base_plan();
        openrc::PlacementCountSelectionV1 selection;
        if (selector_present) {
          selection.selector.element = Element{"bytes", 0U};
        }
        selection.equal_value = 0xffU;
        const auto make_arm = [](const unsigned shape, const std::uint32_t count)
            -> std::optional<openrc::PlacementCountAdjustmentV1> {
          if (shape == 0U) {
            return std::nullopt;
          }
          return openrc::PlacementCountAdjustmentV1{
              count, {shape == 2U ? std::optional<Element>(Element{"words", 13U})
                                  : std::nullopt,
                      7U}};
        };
        selection.when_equal = make_arm(equal, 0U);
        selection.otherwise = make_arm(other, 0xffffffffU);
        plan.condition = selection;
        conditions.push_back(plan);
      }
    }
  }
  std::size_t checked = 0U;
  for (const auto &condition : conditions) {
    for (unsigned registration = 0U; registration < 5U; ++registration) {
      for (unsigned suppression = 0U; suppression < 3U; ++suppression) {
        auto plan = condition;
        if (registration != 0U) {
          plan.registration = openrc::PlacementReverseRegistrationV1{
              registration >= 3U ? std::optional<Element>(Element{"slots", 2U})
                                 : std::nullopt,
              64U,
              (registration & 1U) == 0U
                  ? std::optional<std::uint16_t>(0U)
                  : std::nullopt,
              0U};
        }
        if (suppression != 0U) {
          plan.reject_when_nonzero = openrc::PlacementByteReadV1{
              suppression == 2U ? std::optional<Element>(Element{"bytes", 19U})
                                : std::nullopt};
        }
        check_roundtrip(plan);
        ++checked;
      }
    }
  }
  expect(checked == 315U, "Presence/condition coverage matrix changed");
  check_roundtrip(full_plan());
  auto edge = full_plan();
  edge.registration->row->element_index = ~std::uint64_t{0};
  edge.registration->slot_count = 1U;
  check_roundtrip(edge);
}

void test_fixed_wire_and_distinct_missing_states() {
  const auto minimal = openrc::encode_placement_admission_plan_v1(base_plan(), kLimits);
  expect(minimal.size() == 112U && read32(minimal, 8U) == 1U &&
             read32(minimal, 12U) == 64U && read32(minimal, 16U) == 112U &&
             read32(minimal, 24U) == 1U && read32(minimal, 28U) == 0U &&
             read32(minimal, 96U) == 0x80000001U &&
             read32(minimal, 100U) == 0U && read32(minimal, 104U) == 0U &&
             read32(minimal, 108U) == 0U,
         "Minimal wire layout or LE fields changed");
  // Independently calculated from a manually populated 112-byte image,
  // using the platform SHA-256 implementation rather than this encoder.
  expect(openrc::hex_digest(std::span<const std::byte>(minimal).subspan(32U, 32U)) ==
             "ab6a782d34eb0902860f2c9bf0be6a08756756b779266a19a5c9e187fcbbd55e",
         "Minimal whole-plan golden digest changed");
  const auto full = openrc::encode_placement_admission_plan_v1(full_plan(), kLimits);
  expect(full.size() == 261U && read32(full, 100U) == 1U &&
             read32(full, 108U) == 5U && read32(full, 125U) == 64U &&
             read32(full, 133U) == 0xffffU && read32(full, 137U) == 0x8000U &&
             read32(full, 166U) == 2U && read32(full, 191U) == 0xffU &&
             read32(full, 199U) == 0xffffffffU && read32(full, 224U) == 31U &&
             read32(full, 232U) == 0x12345678U,
         "Full inline wire layout or scalar representation changed");
  auto missing = base_plan();
  missing.reject_when_nonzero.emplace();
  auto available = missing;
  available.reject_when_nonzero->element = Element{"bytes", 0U};
  const auto missing_bytes =
      openrc::encode_placement_admission_plan_v1(missing, kLimits);
  const auto available_bytes =
      openrc::encode_placement_admission_plan_v1(available, kLimits);
  expect(minimal != missing_bytes && missing_bytes != available_bytes &&
             minimal != available_bytes,
         "Absent operation, unavailable operand and present element collapsed");
}

template <class Mutation>
void rejects_mutation(Mutation mutation, const std::string &why,
                       const bool recompute_digest = true) {
  auto bytes = openrc::encode_placement_admission_plan_v1(full_plan(), kLimits);
  mutation(bytes);
  if (recompute_digest) {
    resign(bytes);
  }
  rejected([&] { (void)openrc::decode_placement_admission_plan_v1(bytes, kLimits); }, why);
}

void test_markers_scalar_bounds_and_keys() {
  for (const auto offset : {100U, 104U, 129U, 141U, 145U, 170U, 195U,
                            203U, 228U, 236U}) {
    rejects_mutation([offset](Bytes &bytes) { write32(bytes, offset, 2U); },
                       "non-boolean presence marker");
  }
  rejects_mutation([](Bytes &bytes) { write32(bytes, 166U, 3U); },
                     "unknown condition kind");
  for (const auto slots : {0U, 65U, 0xffffffffU}) {
    rejects_mutation([slots](Bytes &bytes) { write32(bytes, 125U, slots); },
                       "unbounded or empty registration row");
  }
  rejects_mutation([](Bytes &bytes) {
    write64(bytes, 112U, std::numeric_limits<std::uint64_t>::max() - 62U);
  }, "overflowing registration element range");
  for (const auto offset : {133U, 137U}) {
    rejects_mutation([offset](Bytes &bytes) { write32(bytes, offset, 0x10000U); },
                       "nonzero high halfword in u16 field");
  }
  rejects_mutation([](Bytes &bytes) { write32(bytes, 191U, 0x100U); },
                     "nonzero high bits in comparison byte");
  for (const auto offset : {224U, 257U}) {
    rejects_mutation([offset](Bytes &bytes) { write32(bytes, offset, 32U); },
                       "out-of-range bit index");
  }
  for (const auto offset : {108U, 149U, 174U, 207U, 240U}) {
    for (const auto size : {0U, 65U, 0xffffffffU}) {
      rejects_mutation([offset, size](Bytes &bytes) { write32(bytes, offset, size); },
                         "unbounded, empty or truncated inline key");
    }
  }
  for (const auto offset : {120U, 161U, 186U, 219U, 252U}) {
    for (const auto value : {0U, 0x20U, 0x7fU, 0x80U, 0xffU}) {
      rejects_mutation([offset, value](Bytes &bytes) {
        bytes[offset] = static_cast<std::byte>(value);
      }, "noncanonical ASCII key");
    }
  }
  // Invalid metadata is rejected even though the operand itself is unavailable.
  auto unavailable = base_plan();
  unavailable.condition = openrc::PlacementRequireBitClearV1{{std::nullopt, 31U}};
  auto bytes = openrc::encode_placement_admission_plan_v1(unavailable, kLimits);
  write32(bytes, bytes.size() - 4U, 32U);
  resign(bytes);
  rejected([&] { (void)openrc::decode_placement_admission_plan_v1(bytes, kLimits); },
             "invalid bit shape hidden behind an unavailable read");
}

void test_header_truncation_trailing_and_integrity() {
  for (const auto offset : {0U, 8U, 12U, 24U, 28U, 31U}) {
    rejects_mutation([offset](Bytes &bytes) { bytes[offset] ^= std::byte{1}; },
                       "unknown header field or nonzero reserved byte");
  }
  rejects_mutation([](Bytes &bytes) { write64(bytes, 16U, 260U); },
                     "incorrect declared length");
  rejects_mutation([](Bytes &bytes) { write64(bytes, 16U, ~std::uint64_t{0}); },
                     "overflowing declared length");
  rejects_mutation([](Bytes &bytes) {
    std::fill_n(bytes.begin() + 64U, 32U, std::byte{0});
  }, "missing state schema identity");
  rejects_mutation([](Bytes &bytes) {
    bytes.push_back(std::byte{0});
    write64(bytes, 16U, bytes.size());
  }, "trailing byte with corrected length and valid digest");
  const auto full = openrc::encode_placement_admission_plan_v1(full_plan(), kLimits);
  for (std::size_t size = 0U; size < full.size(); ++size) {
    Bytes shortened(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(size));
    if (size >= 64U) {
      write64(shortened, 16U, size);
      resign(shortened);
    }
    rejected([&] {
      (void)openrc::decode_placement_admission_plan_v1(shortened, kLimits);
    }, "truncation at each byte boundary");
  }
  for (std::size_t offset = 0U; offset < full.size(); ++offset) {
    auto changed = full;
    changed[offset] ^= std::byte{1};
    rejected([&] {
      (void)openrc::decode_placement_admission_plan_v1(changed, kLimits);
    }, "single-byte corruption across the entire artifact");
  }
  rejects_mutation([](Bytes &bytes) {
    std::fill_n(bytes.begin() + 32U, 32U, std::byte{0});
  }, "absent whole-plan digest", false);
  // A digest from another valid plan must not validate this plan's scalars.
  auto other = full_plan();
  other.initial_count_bits ^= 1U;
  const auto other_bytes = openrc::encode_placement_admission_plan_v1(other, kLimits);
  rejects_mutation([&](Bytes &bytes) {
    std::copy_n(other_bytes.begin() + 32U, 32U, bytes.begin() + 32U);
  }, "digest transplanted from a different plan", false);
}

void test_limits_and_encoder_validation() {
  const auto plan = full_plan();
  const auto bytes = openrc::encode_placement_admission_plan_v1(plan, kLimits);
  const auto check = [&](const openrc::PlacementAdmissionIoLimitsV1 limits) {
    rejected([&] { (void)openrc::encode_placement_admission_plan_v1(plan, limits); },
               "encoder limit violation");
    rejected([&] { (void)openrc::decode_placement_admission_plan_v1(bytes, limits); },
               "decoder limit violation");
  };
  check({});
  check({0U, kLimits.plan});
  check({1U, kLimits.plan});
  check({bytes.size() - 1U, kLimits.plan});
  check({kLimits.max_input_bytes, {0U, 64U}});
  check({kLimits.max_input_bytes, {64U, 0U}});
  check({kLimits.max_input_bytes, {4U, 64U}});
  check({kLimits.max_input_bytes, {64U, 63U}});
  const openrc::PlacementAdmissionIoLimitsV1 exact{bytes.size(), {5U, 64U}};
  expect(openrc::encode_placement_admission_plan_v1(plan, exact) == bytes &&
             openrc::decode_placement_admission_plan_v1(bytes, exact) == plan,
         "Exact inclusive output/key/slot limits were rejected");
  const auto bad_plan = [](auto mutate) {
    auto invalid = full_plan();
    mutate(invalid);
    rejected([&] {
      (void)openrc::encode_placement_admission_plan_v1(invalid, kLimits);
    }, "invalid plan accepted by encoder");
  };
  bad_plan([](Plan &value) { value.schema_version = 2U; });
  bad_plan([](Plan &value) { value.state_schema_sha256 = {}; });
  bad_plan([](Plan &value) { value.registration->slot_count = 0U; });
  bad_plan([](Plan &value) { value.registration->row->view_key.clear(); });
  bad_plan([](Plan &value) { value.registration->row->view_key = "bad key"; });
  bad_plan([](Plan &value) {
    value.registration->row->element_index = ~std::uint64_t{0};
  });
  bad_plan([](Plan &value) {
    std::get<openrc::PlacementCountSelectionV1>(value.condition)
        .otherwise->adjust_when_set.bit_index = 32U;
  });
}

} // namespace

int main() {
  try {
    test_all_presence_and_condition_shapes();
    test_fixed_wire_and_distinct_missing_states();
    test_markers_scalar_bounds_and_keys();
    test_header_truncation_trailing_and_integrity();
    test_limits_and_encoder_validation();
    std::cout << "Placement admission I/O tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Placement admission I/O tests failed: " << error.what() << '\n';
    return 1;
  }
}
