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

} // namespace openrc
