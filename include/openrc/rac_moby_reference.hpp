#pragma once

#include "openrc/placement_admission.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace openrc {

// Compiler-only binding of an explicit, aligned, non-wrapping source byte
// interval to a contiguous u32 view. Neither these addresses nor source
// pointers belong in a neutral runtime package or SessionState image.
struct RacMobyReferenceWordWindowV1 {
  std::uint32_t source_address_bits = 0U;
  std::uint64_t word_count = 0U;
  std::string view_key;
  std::uint64_t first_element = 0U;
};

struct RacMobyReferenceBindingsV1 {
  PreparedContentDigestV1 state_schema_sha256{};
  std::optional<PlacementStateElementV1> current_level_word;
  std::optional<RacMobyReferenceWordWindowV1> level_base_words;
  std::optional<RacMobyReferenceWordWindowV1> reference_tokens;
};

struct RacMobyReferenceLimitsV1 {
  std::uint32_t max_key_bytes = 0U;
  std::uint64_t max_window_words = 0U;
};

struct RacMobyReferenceWriteV1 {
  PlacementStateElementV1 destination;
  std::uint32_t previous_token = 0U;
  std::uint32_t stored_token = 0U;
  [[nodiscard]] bool
  operator==(const RacMobyReferenceWriteV1 &) const = default;
};

struct RacMobyReferenceResultV1 {
  std::uint32_t level_word_bits = 0U;
  // Absent only on the source signed-level >=19 bypass.
  std::optional<std::uint32_t> base_word_bits;
  std::optional<std::uint32_t> combined_index_bits;
  std::optional<RacMobyReferenceWriteV1> write;
  [[nodiscard]] bool
  operator==(const RacMobyReferenceResultV1 &) const = default;
};

class RacMobyReferenceError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Complete compiler-side scalar store helper, distinct from the caller's
// full32(-1) skip. Reads the CURRENT canonical level and base; source signed
// negative levels pass its gate, and all source address arithmetic wraps32.
// Every reached address must resolve through an explicit whole-range binding.
// Missing reached bindings, out-of-domain or aliased source/native ownership
// fail without a write. No implicit table, clear, initial/reload state or
// source-pointer fallback is supplied. Unreached base/token bindings need not
// exist; schema and revision are checked even on the level bypass.
//
// reference_token is an explicitly resolved neutral object reference (zero is
// a resolved null); absence is missing binding, not null. Its namespace and
// object lifetime are owned by the staged level/entity owner, not EntityId.slot
// or a guessed source pointer. Token storage must have no overlapping scalar
// views. Using SessionState as the canonical byte owner does not make these
// transient references persistent save data or integrate the runtime loader.
// A reached store commits once, even for the same token, before the caller may
// advance live count. No actor is dereferenced and no callback is executed.
[[nodiscard]] RacMobyReferenceResultV1 execute_rac_moby_reference_store_v1(
    const RacMobyReferenceBindingsV1 &bindings, game::SessionStateV1 &state,
    std::uint64_t expected_revision, std::uint32_t source_index_bits,
    std::optional<std::uint32_t> reference_token,
    RacMobyReferenceLimitsV1 limits);

} // namespace openrc
