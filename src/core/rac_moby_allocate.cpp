#include "openrc/rac_moby_allocate.hpp"

#include <array>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint64_t kAddressSpace = 0x100000000ULL;
constexpr std::uint32_t kSlotBytes = 256U;
constexpr std::uint32_t kPvarWords = 32U;

[[noreturn]] void fail(const std::string &message) {
  throw RacMobyAllocateError("RAC Moby allocate " + message);
}

void validate_key(const PlacementStateElementV1 &element,
                  const RacMobyAllocateLimitsV1 limits) {
  if (element.view_key.empty() ||
      element.view_key.size() > limits.max_key_bytes) {
    fail("binding key length is outside the explicit limit");
  }
  for (const unsigned char byte : element.view_key) {
    if (byte < 0x21U || byte > 0x7eU) {
      fail("binding keys must be printable non-whitespace ASCII");
    }
  }
}

std::uint64_t width(const SessionStateValueTypeV1 type) {
  switch (type) {
  case SessionStateValueTypeV1::u8:
    return 1U;
  case SessionStateValueTypeV1::u16:
    return 2U;
  case SessionStateValueTypeV1::u32:
    return 4U;
  }
  fail("invalid canonical view type");
}

struct Range {
  const SessionStateViewV1 *view = nullptr;
  std::uint64_t begin = 0U;
  std::uint64_t end = 0U;
};

bool overlaps(const Range &a, const Range &b) {
  return a.view->buffer_key == b.view->buffer_key && a.begin < b.end &&
         b.begin < a.end;
}

Range resolve_range(const game::SessionStateV1 &state,
                    const PlacementStateElementV1 &first,
                    const std::uint64_t count,
                    const SessionStateValueTypeV1 type) {
  const SessionStateViewV1 *found = nullptr;
  for (const auto &view : state.schema().views) {
    if (view.key == first.view_key) {
      found = &view;
      break;
    }
  }
  if (!found || found->value_type != type ||
      found->byte_stride != width(type) ||
      first.element_index > found->element_count ||
      count > found->element_count - first.element_index || count == 0U) {
    fail("entire reached owner must belong to a contiguous typed view");
  }
  // Complete view extents have already been validated by SessionState.
  const auto begin = found->byte_offset + first.element_index * width(type);
  return {found, begin, begin + count * width(type)};
}

// These are separately resolved scalar/slot/PVar owners, never overlays of
// source pointers with neutral data. Conservative enclosing intervals reject
// unsupported strided alias views as well as straightforward byte aliases.
void own_range(const game::SessionStateV1 &state, const Range &range,
               std::vector<Range> &owners) {
  for (const auto &owner : owners) {
    if (overlaps(range, owner)) {
      fail("reached canonical owners overlap");
    }
  }
  for (const auto &view : state.schema().views) {
    if (&view == range.view || view.buffer_key != range.view->buffer_key) {
      continue;
    }
    const Range other{&view, view.byte_offset,
                      view.byte_offset +
                          (view.element_count - 1U) * view.byte_stride +
                          width(view.value_type)};
    if (overlaps(range, other)) {
      fail("reached owner overlaps another canonical view");
    }
  }
  owners.push_back(range);
}

const PlacementStateElementV1 &
required(const std::optional<PlacementStateElementV1> &element) {
  if (!element) {
    fail("reached canonical binding is missing");
  }
  return *element;
}

std::uint32_t arithmetic_shift8(const std::uint32_t value) noexcept {
  return (value >> 8U) | ((value & 0x80000000U) ? 0xff000000U : 0U);
}

void validate_source_scalar_ownership(const std::uint64_t begin,
                                      const std::uint64_t end) {
  // Every fixed global read by the allocator or its actual constructor.
  // A clear/marker/PVar store must not invalidate a later source re-read.
  constexpr std::array scalar_addresses{0x15f6b0U, 0x16007cU, 0x160098U,
                                        0x16009cU, 0x1600a0U, 0x1600a8U};
  for (const auto address : scalar_addresses) {
    if (begin < std::uint64_t{address} + 4U && address < end) {
      fail("source pool owner overlaps a reached fixed scalar global");
    }
  }
}

void validate_clear(const std::uint32_t begin, const std::uint32_t bytes) {
  const auto end = static_cast<std::uint64_t>(begin) + bytes;
  if ((begin & 3U) != 0U || end > kAddressSpace ||
      (begin <= 0x7fffffffU && end > 0x7fffffffU)) {
    // The final pointer increment also executes after the final store.
    fail("reached source word-clear wraps ownership or traps signed ADDI");
  }
}

void append_write(std::vector<game::SessionStateWriteV1> &writes,
                  const PlacementStateElementV1 &first,
                  const std::uint64_t offset,
                  const SessionStateValueTypeV1 type,
                  const std::uint32_t value) {
  writes.push_back({first.view_key, first.element_index + offset, type, value});
}

} // namespace

RacMobyAllocateResultV1 execute_rac_moby_allocate_v1(
    const RacMobyAllocateBindingsV1 &bindings, game::SessionStateV1 &state,
    const std::uint64_t expected_revision,
    const std::optional<RacMobyAllocateClassV1> &resolved_class,
    const RacMobyAllocateLimitsV1 limits) {
  if (limits.max_key_bytes == 0U || limits.max_physical_slots == 0U) {
    fail("limits must be nonzero");
  }
  if (bindings.state_schema_sha256 != state.schema_sha256() ||
      expected_revision != state.revision()) {
    fail("staged state schema or revision mismatch");
  }
  for (const auto *element :
       std::array{&bindings.first_status, &bindings.first_packed_word,
                  &bindings.current_timer_word, &bindings.current_count_word,
                  &bindings.first_pvar_word}) {
    if (*element) {
      validate_key(**element, limits);
    }
  }
  try {
    RacMobyAllocateResultV1 result;
    std::vector<Range> owners;
    owners.reserve(5U);
    bool timer_bound = false;
    const auto read_timer = [&]() {
      const auto &timer = required(bindings.current_timer_word);
      if (!timer_bound) {
        own_range(state,
                  resolve_range(state, timer, 1U, SessionStateValueTypeV1::u32),
                  owners);
        timer_bound = true;
      }
      ++result.timer_reads;
      return state.read_u32(timer.view_key, timer.element_index);
    };
    const auto begin = bindings.cursor.dynamic_begin_bits;
    const auto end = bindings.cursor.exclusive_end_bits;
    // Both source LW operands are sign-extended to64 before SLTU. Their
    // relative unsigned ordering is exactly the ordering of their u32 bits.
    if (begin >= end) {
      (void)read_timer(); // diagnostic LW; actual callee only writes its stack
      return result;
    }
    const std::uint32_t distance = end - begin;
    if ((begin & 7U) != 0U || (distance & (kSlotBytes - 1U)) != 0U ||
        static_cast<std::uint64_t>(end) + kSlotBytes > kAddressSpace) {
      fail("dynamic pool owner is unaligned, incomplete or wrapping");
    }
    const std::uint32_t physical_count = distance / kSlotBytes + 1U;
    if (physical_count > limits.max_physical_slots) {
      fail("physical pool including reserved guard exceeds its limit");
    }
    validate_source_scalar_ownership(begin, std::uint64_t{end} + kSlotBytes);
    const auto &status = required(bindings.first_status);
    own_range(state,
              resolve_range(state, status, physical_count,
                            SessionStateValueTypeV1::u8),
              owners);
    bool packed_bound = false;
    for (std::uint32_t slot = 0U; slot + 1U < physical_count; ++slot) {
      ++result.examined_slots;
      const auto previous_status =
          state.read_u8(status.view_key, status.element_index + slot);
      if (previous_status < 0xfeU) {
        continue;
      }
      const auto timer =
          read_timer(); // each eligible-status candidate, not snapshot
      const auto &packed = required(bindings.first_packed_word);
      if (!packed_bound) {
        own_range(state,
                  resolve_range(state, packed,
                                std::uint64_t{physical_count} * 2U,
                                SessionStateValueTypeV1::u32),
                  owners);
        packed_bound = true;
      }
      const auto packed_index = packed.element_index + std::uint64_t{slot} * 2U;
      const std::uint64_t deadline =
          state.read_u32(packed.view_key, packed_index) |
          (std::uint64_t{state.read_u32(packed.view_key, packed_index + 1U)}
           << 32U);
      if (std::uint64_t{timer} < deadline) {
        continue;
      }
      if (!resolved_class || !bindings.all_actor_base_bits ||
          !bindings.pvar_base_bits) {
        fail("selected slot lacks current constructor or pool-base bindings");
      }
      const std::uint32_t selected_address = begin + slot * kSlotBytes;
      validate_clear(selected_address, kSlotBytes);
      std::vector<game::SessionStateWriteV1> writes;
      writes.reserve(37U);
      // Source BEQL delay store occurs before constructor's whole-slot clear.
      if (previous_status == 0xffU) {
        append_write(writes, status, slot + 1U, SessionStateValueTypeV1::u8,
                     0xffU);
      }
      RacMobyFreshConstructorInputV1 constructor_input;
      constructor_input.class_id_bits = resolved_class->class_id_bits;
      constructor_input.class_table_index = resolved_class->class_table_index;
      constructor_input.live_index_bits =
          arithmetic_shift8(selected_address - *bindings.all_actor_base_bits);
      constructor_input.callback_reference = resolved_class->callback_reference;
      constructor_input.model = resolved_class->model;
      RacMobyAllocatedFreshV1 fresh;
      fresh.physical_slot_index = slot;
      fresh.previous_status = previous_status;
      fresh.advanced_ff_marker = previous_status == 0xffU;
      fresh.constructor = construct_rac_moby_fresh_v1(constructor_input);
      append_write(writes, status, slot, SessionStateValueTypeV1::u8, 0U);
      append_write(writes, packed, std::uint64_t{slot} * 2U,
                   SessionStateValueTypeV1::u32,
                   static_cast<std::uint32_t>(fresh.constructor.packed_bits));
      append_write(
          writes, packed, std::uint64_t{slot} * 2U + 1U,
          SessionStateValueTypeV1::u32,
          static_cast<std::uint32_t>(fresh.constructor.packed_bits >> 32U));

      // Re-read source begin is unchanged by this constructor graph. Keep its
      // SUBU/SRA/SLL/ADDU explicitly; do not substitute unsigned slot*128.
      const std::uint32_t pvar_offset =
          arithmetic_shift8(selected_address - begin) << 7U;
      const std::uint32_t pvar_address = *bindings.pvar_base_bits + pvar_offset;
      const std::uint64_t pvar_bytes = std::uint64_t{physical_count} * 128U;
      if ((*bindings.pvar_base_bits & 3U) != 0U ||
          pvar_bytes > kAddressSpace - *bindings.pvar_base_bits ||
          pvar_address < *bindings.pvar_base_bits ||
          std::uint64_t{pvar_address - *bindings.pvar_base_bits} + 128U >
              pvar_bytes) {
        fail("entire PVar owner or reached wrapping source index is "
             "unsupported");
      }
      const std::uint64_t actor_end = std::uint64_t{end} + kSlotBytes;
      const std::uint64_t pvar_end = *bindings.pvar_base_bits + pvar_bytes;
      validate_source_scalar_ownership(*bindings.pvar_base_bits, pvar_end);
      if (std::uint64_t{begin} < pvar_end &&
          *bindings.pvar_base_bits < actor_end) {
        fail("source actor and PVar owners overlap");
      }
      validate_clear(pvar_address, 128U);
      const auto &pvar = required(bindings.first_pvar_word);
      own_range(state,
                resolve_range(state, pvar,
                              std::uint64_t{physical_count} * kPvarWords,
                              SessionStateValueTypeV1::u32),
                owners);
      const auto pvar_word = (pvar_address - *bindings.pvar_base_bits) / 4U;
      fresh.first_pvar_word = {pvar.view_key, pvar.element_index + pvar_word};
      for (std::uint32_t word = 0U; word < kPvarWords; ++word) {
        append_write(writes, pvar, pvar_word + word,
                     SessionStateValueTypeV1::u32, 0U);
      }
      const auto &counter = required(bindings.current_count_word);
      own_range(state,
                resolve_range(state, counter, 1U, SessionStateValueTypeV1::u32),
                owners);
      fresh.previous_count_bits =
          state.read_u32(counter.view_key, counter.element_index);
      fresh.remaining_count_bits = fresh.previous_count_bits;
      if (fresh.previous_count_bits != 0U) {
        fresh.remaining_count_bits = fresh.previous_count_bits - 1U;
        append_write(writes, counter, 0U, SessionStateValueTypeV1::u32,
                     fresh.remaining_count_bits);
      }
      result.fresh = std::move(fresh);
      state.apply_batch(writes, expected_revision);
      static_assert(
          std::is_nothrow_move_constructible_v<RacMobyAllocateResultV1>);
      return result;
    }
    (void)read_timer(); // exhausted path's signed LW is stack-only in callee
    return result;
  } catch (const SessionStateError &error) {
    fail(std::string("staged state access failed: ") + error.what());
  } catch (const RacMobyFreshConstructorError &error) {
    fail(std::string("fresh constructor failed: ") + error.what());
  }
}

} // namespace openrc
