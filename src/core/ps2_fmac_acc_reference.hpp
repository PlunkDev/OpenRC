#pragma once

#include "ps2_fmac_add_reference.hpp"
#include "ps2_fmac_mul_reference.hpp"

namespace openrc::detail {

// The product is rounded by the ordered multiplier before entering the adder.
// Its range event is retained separately. A product overflow dominates the
// old ACC; otherwise the ACC overflow latch dominates an ordinary product.
// This is an explicit reference ACC transition, not fused host arithmetic.
struct Ps2FmacAccValueV1 {
  Ps2FmacAddValueV1 result;
  Ps2FmacMulValueV1 product;
};
[[nodiscard]] inline Ps2FmacAccValueV1
ps2_fmac_acc_value_v1(const std::uint32_t accumulator,
                       const bool accumulator_overflow,
                       const std::uint32_t left, const std::uint32_t right,
                       const bool subtract) noexcept {
  const auto product = ps2_fmac_mul_value_v1(left, right);
  const auto signed_product = product.bits ^ (subtract ? 0x80000000U : 0U);
  if (product.overflow) return {{signed_product, false, true}, product};
  if (accumulator_overflow)
    return {{(accumulator & 0x80000000U) | 0x7fffffffU, false, true}, product};
  return {ps2_fmac_add_value_v1(accumulator, signed_product), product};
}
} // namespace openrc::detail
