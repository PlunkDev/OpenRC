#pragma once

#include <array>
#include <cstdint>

namespace openrc {

enum class RacMobySpatialBoundsGateV1 : std::uint8_t {
  unchanged,
  rejected_minimum,
  call_spatial_update,
};

struct RacMobySpatialBoundsProjectionV1 {
  // Raw source bytes, in packed little-endian order. These are not a
  // validated rectangle: maxima, ordering and byte wrap are not checked
  // by this source gate.
  std::array<std::uint8_t, 4U> bound_bytes{};
  std::uint32_t packed_bits = 0U;
  RacMobySpatialBoundsGateV1 gate = RacMobySpatialBoundsGateV1::unchanged;

  [[nodiscard]] bool
  operator==(const RacMobySpatialBoundsProjectionV1 &) const = default;
};

// Compiler/source-side adapter for the integer tail of the recovered Moby
// post-step. Inputs are the raw, already converted VFTOI0 words, not floats,
// world coordinates, a guessed radius, or a parsed runtime source structure.
// old_packed_bits is the actual stored bounds word read by source LW.
//
// This pure helper preserves wrap32 C +/- radius, arithmetic shift by14,
// low-byte packing, and the ordered source gates. It does not validate grid
// membership, mutate actor state, or invoke the spatial index. The caller
// owns the preceding non-null spatial-pointer gate and all earlier source
// cache/vector/counter writes. The reached spatial update's bounds store
// and later signed-maxY gate are separate; call_spatial_update does not
// promise that any cell will be visited or that a rectangle is supported.
[[nodiscard]] RacMobySpatialBoundsProjectionV1
project_rac_moby_spatial_bounds_v1(std::uint32_t converted_center_x_bits,
                                   std::uint32_t converted_center_y_bits,
                                   std::uint32_t converted_radius_bits,
                                   std::uint32_t old_packed_bits) noexcept;

} // namespace openrc
