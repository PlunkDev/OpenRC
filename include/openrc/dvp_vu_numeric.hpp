#pragma once

#include <cstdint>

namespace openrc {

// Compiler-side VU numerical primitive, not a runtime source-code interpreter.
// Convert one raw VU floating-point lane to the two's-complement result bits of
// FTOI0/4/12/15. fractional_bits must be 0, 4, 12, or 15; other values throw
// std::invalid_argument. This integer-only operation is independent of the host
// floating-point format, rounding mode, and float-to-integer overflow behavior.
//
// Qualification: the value semantics follow the SCE VU User's Manual v6.0,
// sections 2.1-2.3 and FTOI0/4/12/15. Tests are specification-derived, not
// newly captured hardware results. Exponent 255 is a finite VU value, not IEEE
// NaN or infinity; exponent 0 converts to zero. Overflow saturates by sign. No
// flags change. This function does not model instruction latency, register
// masks, or certify the separate VU floating-point arithmetic implementation.
[[nodiscard]] std::uint32_t dvp_vu_ftoi_bits_v1(std::uint32_t source_bits,
                                                int fractional_bits);

// One scalar ADD/SUB result. These are per-operation lane flags, not a packed
// MAC/STATUS register, sticky history, or the hidden ACC overflow state.
struct DvpVuAddResultV1 {
  std::uint32_t bits = 0U;
  bool zero = false;
  bool sign = false;
  bool underflow = false;
  bool overflow = false;

  [[nodiscard]] bool operator==(const DvpVuAddResultV1 &) const = default;
};

// Compiler-side, integer-only reference model for every raw 32-bit encoding:
// exponent-zero inputs flush to signed zero; exponent 255 remains finite.
// Alignment retains one guard bit, with no sticky bit. The signed sum is
// normalized then truncated. Overflow saturates to sign|0x7fffffff. Underflow
// forces exponent zero but preserves the normalized fraction, with U and Z
// both set. Exact cancellation is +0 except adding two negative zeros.
// SUB reverses the second operand's sign before applying those rules.
//
// Qualification: independently implemented from the public one-guard report
// (PCSX2 devblog, cottonvibes, 2009-09-02, Note 3), public source-author numeric
// expectations, and reported VU0 underflow measurements (pstef, PCSX2 PR12001,
// comment 5074203305). This is NOT our exhaustive physical-console validation.
// It does not certify MUL/MADD/ACC, forwarding, instruction timing or flags
// publication. Callers must retain that reference-model qualification.
[[nodiscard]] DvpVuAddResultV1 dvp_vu_add_bits_v1(std::uint32_t left,
                                               std::uint32_t right) noexcept;
[[nodiscard]] DvpVuAddResultV1 dvp_vu_sub_bits_v1(std::uint32_t left,
                                               std::uint32_t right) noexcept;

} // namespace openrc
