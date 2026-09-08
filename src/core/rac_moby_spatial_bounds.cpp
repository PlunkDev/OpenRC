#include "openrc/rac_moby_spatial_bounds.hpp"

#include <cstddef>
#include <cstdint>

namespace openrc {
namespace {

[[nodiscard]] constexpr std::uint32_t
arithmetic_shift_right_14(const std::uint32_t bits) noexcept {
  // Use unsigned operations throughout, including negative source words.
  return (bits >> 14U) | ((bits & 0x80000000U) != 0U ? 0xfffc0000U : 0U);
}

} // namespace

RacMobySpatialBoundsProjectionV1 project_rac_moby_spatial_bounds_v1(
    const std::uint32_t converted_center_x_bits,
    const std::uint32_t converted_center_y_bits,
    const std::uint32_t converted_radius_bits,
    const std::uint32_t old_packed_bits) noexcept {
  // Source PADDW/PSUBW wrap before PSRAW14 and PPACH/PPACB truncate.
  const std::array<std::uint32_t, 4U> endpoints{
      converted_center_x_bits - converted_radius_bits,
      converted_center_y_bits - converted_radius_bits,
      converted_center_x_bits + converted_radius_bits,
      converted_center_y_bits + converted_radius_bits,
  };
  RacMobySpatialBoundsProjectionV1 result;
  for (std::size_t lane = 0U; lane < endpoints.size(); ++lane) {
    result.bound_bytes[lane] =
        static_cast<std::uint8_t>(arithmetic_shift_right_14(endpoints[lane]));
    result.packed_bits |= static_cast<std::uint32_t>(result.bound_bytes[lane])
                          << (8U * lane);
  }

  // Packing with ZERO leaves bits32..63 zero, but LW sign-extends the
  // stored word. The source branch compares low64, not low32 or all128.
  const auto old_loaded_bits =
      static_cast<std::uint64_t>(old_packed_bits) |
      ((old_packed_bits & 0x80000000U) != 0U ? 0xffffffff00000000ULL : 0ULL);
  if (static_cast<std::uint64_t>(result.packed_bits) == old_loaded_bits) {
    result.gate = RacMobySpatialBoundsGateV1::unchanged;
  } else if ((result.packed_bits & 0x0000c0c0U) != 0U) {
    // Only minX/minY are tested here. In particular, a negative packed
    // word is not rejected before reaching the later spatial-update store.
    result.gate = RacMobySpatialBoundsGateV1::rejected_minimum;
  } else {
    result.gate = RacMobySpatialBoundsGateV1::call_spatial_update;
  }
  return result;
}

} // namespace openrc
