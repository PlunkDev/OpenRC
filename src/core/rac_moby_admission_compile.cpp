#include "openrc/rac_moby_admission_compile.hpp"

#include <bit>
#include <limits>
#include <utility>

namespace openrc {
namespace {

std::optional<PlacementStateElementV1>
byte_element(const std::optional<RacAdmissionStateWindowV1> &window,
             const std::int32_t index, const std::uint64_t maximum) {
  if (!window || maximum == 0U || window->element_count > maximum) {
    return std::nullopt;
  }
  const auto available = static_cast<std::uint64_t>(
      static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) -
      window->first_source_index + 1);
  const auto relative =
      static_cast<std::int64_t>(index) - window->first_source_index;
  if (window->element_count > available || relative < 0 ||
      static_cast<std::uint64_t>(relative) >= window->element_count ||
      static_cast<std::uint64_t>(relative) >
          UINT64_MAX - window->first_element) {
    return std::nullopt;
  }
  return PlacementStateElementV1{window->view_key,
                                 window->first_element +
                                     static_cast<std::uint64_t>(relative)};
}

PlacementBitReadV1
bit_element(const std::optional<RacAdmissionStateWindowV1> &window,
            const std::int32_t key, const std::uint64_t maximum) {
  PlacementBitReadV1 result;
  if (key < 0 || key >= 2048 || !window || window->first_source_index != 0 ||
      window->element_count > 64U) {
    return result;
  }
  const auto bits = static_cast<std::uint32_t>(key);
  result.bit_index = static_cast<std::uint8_t>(bits & 31U);
  result.word =
      byte_element(window, static_cast<std::int32_t>(bits >> 5U), maximum);
  return result;
}

} // namespace

RacMobyAdmissionCompilationV1
compile_rac_moby_admission_v1(const RacGameplayMobyAdmissionV1 &source,
                              const RacMobyAdmissionStateBindingsV1 &bindings,
                              const PlacementAdmissionLimitsV1 plan_limits,
                              const RacMobyAdmissionLimitsV1 source_limits) {
  RacMobyAdmissionCompilationV1 result;
  auto &plan = result.plan;
  plan.state_schema_sha256 = bindings.state_schema_sha256;
  plan.initial_count_bits = static_cast<std::uint32_t>(source.primary_count);
  result.constructor_key_bits = static_cast<std::uint16_t>(source.key_index);
  result.constructor_selector_bits =
      static_cast<std::uint8_t>(source.selector_index);
  const auto flags = source.policy_bits;
  if (flags != 0U) {
    result.unregistered_slot_bits = 0xffU;
    if ((flags & 0x10U) != 0U && source.key_index >= 0) {
      PlacementReverseRegistrationV1 registration;
      registration.slot_count = 64U;
      if (bindings.registration_keys &&
          bindings.registration_keys->first_source_index == 0 &&
          bindings.registration_keys->element_count == 64U &&
          bindings.registration_keys->first_element <= UINT64_MAX - 63U) {
        registration.row =
            PlacementStateElementV1{bindings.registration_keys->view_key,
                                    bindings.registration_keys->first_element};
      }
      const auto wanted_bits =
          static_cast<std::uint32_t>(source.key_index) + 1U;
      registration.inserted_value = static_cast<std::uint16_t>(wanted_bits);
      const auto wanted = std::bit_cast<std::int32_t>(wanted_bits);
      const auto stored =
          std::bit_cast<std::int16_t>(registration.inserted_value);
      if (static_cast<std::int32_t>(stored) == wanted) {
        registration.exact_match = registration.inserted_value;
      }
      plan.registration = std::move(registration);
    }
    plan.reject_when_nonzero = PlacementByteReadV1{
        byte_element(bindings.suppression_bytes, source.key_index,
                     source_limits.max_suppression_bytes)};
    if ((flags & 3U) != 0U) {
      PlacementCountSelectionV1 selection;
      if (source.selector_index >= -1 && source.selector_index <= 15) {
        selection.selector.element =
            byte_element(bindings.selector_bytes, source.selector_index,
                         source_limits.max_selector_bytes);
      }
      selection.equal_value = 0xffU;
      if ((flags & 2U) != 0U) {
        selection.when_equal = PlacementCountAdjustmentV1{
            static_cast<std::uint32_t>(source.alternate_count),
            bit_element(bindings.alternate_bit_words, source.key_index,
                        source_limits.max_bit_words)};
      }
      if ((flags & 1U) != 0U) {
        selection.otherwise = PlacementCountAdjustmentV1{
            static_cast<std::uint32_t>(source.primary_count),
            bit_element(bindings.primary_bit_words, source.key_index,
                        source_limits.max_bit_words)};
      }
      plan.condition = std::move(selection);
    } else if ((flags & 8U) != 0U) {
      plan.condition = PlacementRequireBitClearV1{
          bit_element(bindings.alternate_bit_words, source.key_index,
                      source_limits.max_bit_words)};
    } else if ((flags & 0xcU) == 4U) {
      plan.condition = PlacementRequireBitClearV1{
          bit_element(bindings.primary_bit_words, source.key_index,
                      source_limits.max_bit_words)};
    }
  }
  validate_placement_admission_plan_v1(plan, plan_limits);
  return result;
}

} // namespace openrc
