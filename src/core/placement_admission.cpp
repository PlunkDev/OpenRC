#include "openrc/placement_admission.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string_view message) {
  throw PlacementAdmissionError("Placement admission " + std::string(message));
}

void validate_element(const std::optional<PlacementStateElementV1> &element,
                      const PlacementAdmissionLimitsV1 limits) {
  if (!element) {
    return;
  }
  if (element->view_key.empty() ||
      element->view_key.size() > limits.max_key_bytes) {
    fail("element key is empty or exceeds its byte limit");
  }
  for (const char character : element->view_key) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x21U || byte > 0x7eU) {
      fail(
          "element key must contain only non-whitespace printable ASCII bytes");
    }
  }
}

void validate_bit(const PlacementBitReadV1 &bit,
                  const PlacementAdmissionLimitsV1 limits) {
  if (bit.bit_index >= 32U) {
    fail("bit index exceeds an unsigned word");
  }
  validate_element(bit.word, limits);
}

[[nodiscard]] const PlacementStateElementV1 &
require_element(const std::optional<PlacementStateElementV1> &element) {
  if (!element) {
    fail("is missing a reached state element");
  }
  return *element;
}

[[nodiscard]] std::uint8_t read_byte(const PlacementByteReadV1 &read,
                                     const game::SessionStateV1 &state) {
  const auto &element = require_element(read.element);
  return state.read_u8(element.view_key, element.element_index);
}

[[nodiscard]] bool read_bit(const PlacementBitReadV1 &read,
                            const game::SessionStateV1 &state) {
  const auto &element = require_element(read.word);
  return ((state.read_u32(element.view_key, element.element_index) >>
           read.bit_index) &
          1U) != 0U;
}

[[nodiscard]] std::uint32_t
adjusted_count(const std::uint32_t maximum) noexcept {
  const std::uint32_t incremented = maximum + 1U;
  const std::uint32_t adjusted = incremented + (incremented >> 31U);
  // Unsigned shifts spell out arithmetic right shift without implementation-
  // defined signed conversion, signed overflow, or floating-point rounding.
  return (adjusted >> 1U) | (adjusted & 0x80000000U);
}

} // namespace

void validate_placement_admission_plan_v1(
    const PlacementAdmissionPlanV1 &plan,
    const PlacementAdmissionLimitsV1 limits) {
  if (limits.max_key_bytes == 0U || limits.max_registration_slots == 0U) {
    fail("limits must be explicit and positive");
  }
  if (plan.schema_version != kPlacementAdmissionSchemaVersionV1) {
    fail("plan has an unknown schema version");
  }
  if (std::ranges::all_of(plan.state_schema_sha256, [](const std::byte byte) {
        return byte == std::byte{0};
      })) {
    fail("plan is missing its state schema digest");
  }
  if (plan.registration) {
    const auto &registration = *plan.registration;
    if (registration.slot_count == 0U ||
        registration.slot_count > limits.max_registration_slots) {
      fail("registration slot count is zero or exceeds its limit");
    }
    validate_element(registration.row, limits);
    if (registration.row && registration.slot_count - 1U >
                                std::numeric_limits<std::uint64_t>::max() -
                                    registration.row->element_index) {
      fail("registration element range overflows");
    }
  }
  if (plan.reject_when_nonzero) {
    validate_element(plan.reject_when_nonzero->element, limits);
  }
  if (plan.condition.valueless_by_exception()) {
    fail("condition has no valid alternative");
  }
  if (const auto *clear =
          std::get_if<PlacementRequireBitClearV1>(&plan.condition)) {
    validate_bit(clear->bit, limits);
  } else if (const auto *selection =
                 std::get_if<PlacementCountSelectionV1>(&plan.condition)) {
    validate_element(selection->selector.element, limits);
    if (selection->when_equal) {
      validate_bit(selection->when_equal->adjust_when_set, limits);
    }
    if (selection->otherwise) {
      validate_bit(selection->otherwise->adjust_when_set, limits);
    }
  }
}

namespace game {

PlacementAdmissionResultV1
execute_placement_admission_v1(const PlacementAdmissionPlanV1 &plan,
                               SessionStateV1 &state,
                               const std::uint64_t expected_revision,
                               const PlacementAdmissionLimitsV1 limits) {
  validate_placement_admission_plan_v1(plan, limits);
  if (plan.state_schema_sha256 != state.schema_sha256()) {
    fail("state schema digest does not match the plan");
  }
  if (expected_revision != state.revision()) {
    fail("step has a stale state revision");
  }

  PlacementAdmissionResultV1 result;
  result.maximum_bits = plan.initial_count_bits;
  result.current_bits = result.maximum_bits;
  // A read-only step or a full registration row needs no payload copy.
  // Once a write is reached, all later views use that one staged storage.
  std::optional<SessionStateV1> staged;
  try {
    if (plan.registration) {
      const auto &registration = *plan.registration;
      const auto &row = require_element(registration.row);
      for (std::uint32_t cursor = registration.slot_count; cursor > 0U;
           --cursor) {
        const auto slot_index = cursor - 1U;
        const auto index = row.element_index + slot_index;
        const auto previous = state.read_u16(row.view_key, index);
        if (previous != 0U && (!registration.exact_match ||
                               previous != *registration.exact_match)) {
          continue;
        }
        staged.emplace(state);
        const std::array writes{SessionStateWriteV1{
            row.view_key, index, SessionStateValueTypeV1::u16,
            registration.inserted_value}};
        staged->apply_batch(writes, expected_revision);
        result.registration_write = PlacementRegistrationWriteV1{
            slot_index, previous, registration.inserted_value};
        break;
      }
    }

    const auto &current = staged ? *staged : state;
    if (plan.reject_when_nonzero &&
        read_byte(*plan.reject_when_nonzero, current) != 0U) {
      result.admitted = false;
    } else if (const auto *clear =
                   std::get_if<PlacementRequireBitClearV1>(&plan.condition)) {
      result.admitted = !read_bit(clear->bit, current);
    } else if (const auto *selection =
                   std::get_if<PlacementCountSelectionV1>(&plan.condition)) {
      const auto &arm =
          read_byte(selection->selector, current) == selection->equal_value
              ? selection->when_equal
              : selection->otherwise;
      if (!arm) {
        result.admitted = false;
      } else {
        result.maximum_bits = arm->maximum_bits;
        result.current_bits = read_bit(arm->adjust_when_set, current)
                                  ? adjusted_count(result.maximum_bits)
                                  : result.maximum_bits;
      }
    }
  } catch (const SessionStateError &error) {
    fail(std::string("state access failed: ") + error.what());
  }

  // Includes ordinary admission rejection. Errors above discard all staged
  // writes, including a same-value write's revision. No constructor runs here.
  if (staged) {
    static_assert(std::is_nothrow_move_assignable_v<SessionStateV1>);
    state = std::move(*staged);
  }
  return result;
}

} // namespace game
} // namespace openrc
