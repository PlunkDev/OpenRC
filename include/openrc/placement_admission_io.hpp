#pragma once

#include "openrc/placement_admission.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kPlacementAdmissionIoFormatVersionV1 = 1U;
inline constexpr std::uint32_t kPlacementAdmissionIoHeaderBytesV1 = 0x40U;

struct PlacementAdmissionIoLimitsV1 {
  // One explicit bound for decoded input and encoded output, before copying.
  std::uint64_t max_input_bytes = 0U;
  PlacementAdmissionLimitsV1 plan;
};

class PlacementAdmissionIoError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// One finite neutral plan, not a package, runtime snapshot or source decoder.
// Fields are canonical LE with inline keys and exact presence/kind markers.
// SHA-256 covers every byte with only its own header field replaced by zeros.
[[nodiscard]] std::vector<std::byte> encode_placement_admission_plan_v1(
    const PlacementAdmissionPlanV1 &plan, PlacementAdmissionIoLimitsV1 limits);

// Preflights the whole bounded shape and digest before allocating any keys.
// Missing operations and present operations with unavailable reads remain
// distinct. Unknown versions/markers, reserved bytes and trailing data fail.
[[nodiscard]] PlacementAdmissionPlanV1 decode_placement_admission_plan_v1(
    std::span<const std::byte> bytes, PlacementAdmissionIoLimitsV1 limits);

} // namespace openrc
