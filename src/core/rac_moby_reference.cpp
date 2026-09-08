#include "openrc/rac_moby_reference.hpp"

#include <array>
#include <bit>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint32_t kLevelAddress = 0x15ee84U;
constexpr std::uint32_t kBaseAddress = 0x1c47b8U;
constexpr std::uint32_t kReferenceAddress = 0x1792b8U;
constexpr std::uint64_t kAddressSpaceSize = 0x100000000ULL;

[[noreturn]] void fail(const std::string &message) {
  throw RacMobyReferenceError("RAC Moby reference " + message);
}

void validate_key(const std::string_view key,
                  const RacMobyReferenceLimitsV1 limits) {
  if (key.empty() || key.size() > limits.max_key_bytes) {
    fail("binding key length is outside the explicit limit");
  }
  for (const unsigned char byte : key) {
    if (byte < 0x21U || byte > 0x7eU) {
      fail("binding key must be printable non-whitespace ASCII");
    }
  }
}

void validate_window(const RacMobyReferenceWordWindowV1 &window,
                     const RacMobyReferenceLimitsV1 limits) {
  validate_key(window.view_key, limits);
  if ((window.source_address_bits & 3U) != 0U || window.word_count == 0U ||
      window.word_count > limits.max_window_words ||
      window.word_count >
          (kAddressSpaceSize - window.source_address_bits) / 4U ||
      window.word_count > UINT64_MAX - window.first_element) {
    fail("source word window is unaligned, wrapping or outside its limits");
  }
}

[[nodiscard]] const SessionStateViewV1 &
find_view(const game::SessionStateV1 &state, const std::string_view key) {
  for (const auto &view : state.schema().views) {
    if (view.key == key) {
      return view;
    }
  }
  fail("reached binding refers to a missing view");
}

[[nodiscard]] const SessionStateViewV1 &
validate_native_window(const game::SessionStateV1 &state,
                       const RacMobyReferenceWordWindowV1 &window) {
  const auto &view = find_view(state, window.view_key);
  if (view.value_type != SessionStateValueTypeV1::u32 ||
      view.byte_stride != 4U || window.first_element > view.element_count ||
      window.word_count > view.element_count - window.first_element) {
    fail("entire reached window must belong to a contiguous u32 view");
  }
  // SessionState already validated complete view extents against its buffer.
  return view;
}

[[nodiscard]] PlacementStateElementV1
resolve(const RacMobyReferenceWordWindowV1 &window,
        const std::uint32_t address_bits) {
  if (address_bits < window.source_address_bits ||
      static_cast<std::uint64_t>(address_bits - window.source_address_bits) >=
          window.word_count * 4U) {
    fail("reached source address is outside the declared owner");
  }
  return {window.view_key,
          window.first_element +
              (address_bits - window.source_address_bits) / 4U};
}

[[nodiscard]] bool overlaps(const std::uint64_t first, const std::uint64_t end,
                            const std::uint64_t other_first,
                            const std::uint64_t other_end) noexcept {
  return first < other_end && other_first < end;
}

[[nodiscard]] std::uint64_t width(const SessionStateValueTypeV1 type) {
  switch (type) {
  case SessionStateValueTypeV1::u8:
    return 1U;
  case SessionStateValueTypeV1::u16:
    return 2U;
  case SessionStateValueTypeV1::u32:
    return 4U;
  }
  fail("unexpected state view type");
}

void validate_reference_ownership(
    const RacMobyReferenceWordWindowV1 &references,
    const RacMobyReferenceWordWindowV1 &bases,
    const SessionStateViewV1 &reference_view,
    const game::SessionStateV1 &state) {
  const auto source_end =
      static_cast<std::uint64_t>(references.source_address_bits) +
      references.word_count * 4U;
  if (overlaps(references.source_address_bits, source_end, kLevelAddress,
               static_cast<std::uint64_t>(kLevelAddress) + 4U) ||
      overlaps(references.source_address_bits, source_end,
               bases.source_address_bits,
               static_cast<std::uint64_t>(bases.source_address_bits) +
                   bases.word_count * 4U)) {
    fail("reference source domain aliases a scalar owner");
  }
  const auto first = reference_view.byte_offset + references.first_element * 4U;
  const auto end = first + references.word_count * 4U;
  for (const auto &view : state.schema().views) {
    if (&view == &reference_view ||
        view.buffer_key != reference_view.buffer_key ||
        view.element_count == 0U) {
      continue;
    }
    // A strided view's enclosing interval is deliberately conservative:
    // unsupported aliases/holes are not silently treated as token ownership.
    const auto other_end = view.byte_offset +
                           (view.element_count - 1U) * view.byte_stride +
                           width(view.value_type);
    if (overlaps(first, end, view.byte_offset, other_end)) {
      fail("neutral reference window overlaps another state view");
    }
  }
  // Reusing the exact token view for a scalar read also violates ownership,
  // even though the schema loop above intentionally skips that same view.
  if (bases.view_key == references.view_key &&
      overlaps(references.first_element,
               references.first_element + references.word_count,
               bases.first_element, bases.first_element + bases.word_count)) {
    fail("neutral reference window aliases the base words");
  }
}

} // namespace

RacMobyReferenceResultV1 execute_rac_moby_reference_store_v1(
    const RacMobyReferenceBindingsV1 &bindings, game::SessionStateV1 &state,
    const std::uint64_t expected_revision,
    const std::uint32_t source_index_bits,
    const std::optional<std::uint32_t> reference_token,
    const RacMobyReferenceLimitsV1 limits) {
  if (limits.max_key_bytes == 0U || limits.max_window_words == 0U) {
    fail("limits must be nonzero");
  }
  if (bindings.state_schema_sha256 != state.schema_sha256() ||
      expected_revision != state.revision()) {
    fail("state schema or revision mismatch");
  }
  if (!bindings.current_level_word) {
    fail("current level word is unavailable");
  }
  validate_key(bindings.current_level_word->view_key, limits);
  if (bindings.level_base_words) {
    validate_window(*bindings.level_base_words, limits);
  }
  if (bindings.reference_tokens) {
    validate_window(*bindings.reference_tokens, limits);
  }
  try {
    RacMobyReferenceResultV1 result;
    result.level_word_bits =
        state.read_u32(bindings.current_level_word->view_key,
                       bindings.current_level_word->element_index);
    if (std::bit_cast<std::int32_t>(result.level_word_bits) >= 19) {
      return result;
    }
    if (!bindings.level_base_words) {
      fail("reached base table is unavailable");
    }
    const auto &bases = *bindings.level_base_words;
    (void)validate_native_window(state, bases);
    const std::uint32_t base_address =
        kBaseAddress + (result.level_word_bits << 2U);
    const auto base_element = resolve(bases, base_address);
    result.base_word_bits =
        state.read_u32(base_element.view_key, base_element.element_index);
    result.combined_index_bits = source_index_bits + *result.base_word_bits;
    const std::uint32_t destination_address =
        kReferenceAddress + (*result.combined_index_bits << 2U);
    if (!bindings.reference_tokens || !reference_token) {
      fail("reached reference owner or object binding is unavailable");
    }
    const auto &references = *bindings.reference_tokens;
    const auto &reference_view = validate_native_window(state, references);
    validate_reference_ownership(references, bases, reference_view, state);
    if (bindings.current_level_word->view_key == references.view_key &&
        bindings.current_level_word->element_index >=
            references.first_element &&
        bindings.current_level_word->element_index - references.first_element <
            references.word_count) {
      fail("neutral reference window aliases current level");
    }
    const auto destination = resolve(references, destination_address);
    result.write = RacMobyReferenceWriteV1{
        destination,
        state.read_u32(destination.view_key, destination.element_index),
        *reference_token};
    const std::array writes{game::SessionStateWriteV1{
        destination.view_key, destination.element_index,
        SessionStateValueTypeV1::u32, *reference_token}};
    state.apply_batch(writes, expected_revision);
    static_assert(
        std::is_nothrow_move_constructible_v<RacMobyReferenceResultV1>);
    return result;
  } catch (const SessionStateError &error) {
    fail(std::string("state access failed: ") + error.what());
  }
}

} // namespace openrc
