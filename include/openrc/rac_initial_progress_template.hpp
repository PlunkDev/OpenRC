#pragma once

#include "openrc/ps2_save_bundle.hpp"
#include "openrc/rac_moby_admission.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace openrc {

inline constexpr std::size_t kRacInitialProgressLevelRowsV1 = 20U;

struct RacInitialProgressRowV1 {
    std::array<std::uint8_t, 16U> selector_bytes{};
    // Source little-endian words, without losing any of the 256 input bytes.
    std::array<std::uint32_t, 64U> primary_bit_words{};
    std::array<RacMobyAdmissionRegistrationV1, 64U> registration_slots{};
};

struct RacInitialProgressTemplateV1 {
    // Primary tag 0 as encoded, before the source wrapper's separate L=0
    // write. The supported disc template encodes -1; never replace it here.
    std::int32_t encoded_source_level = 0;
    std::array<RacInitialProgressRowV1, kRacInitialProgressLevelRowsV1> rows{};
};

class RacInitialProgressTemplateError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Compiler-only interpretation of the supported RAC1 PAL tagged template
// carried by a complete PS2D sector envelope. Requires the complete 47/11-tag
// source descriptor layout, 20 rows, and each original record checksum.
// Missing data never becomes an implicit zero-filled row. Does not execute
// startup, select a level, initialize checkpoint/suppression tables, or claim
// that these values remained unchanged until placement admission.
[[nodiscard]] RacInitialProgressTemplateV1
parse_rac_initial_progress_template_v1(
    std::span<const std::byte> ps2d_envelope,
    Ps2SaveBundleLimits limits);

} // namespace openrc
