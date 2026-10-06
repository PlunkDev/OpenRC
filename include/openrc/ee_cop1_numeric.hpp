#pragma once

#include <cstdint>

namespace openrc {

// DIV.S value reference over raw EE encodings. The redundant divider's
// quotient decisions are integer-only; they are not IEEE/host division or a
// blanket one-ULP adjustment. Exp0 inputs are signed zero, exp255 is finite;
// a zero divisor and range overflow select signed Fmax, range underflow zero.
// This is reference qualification, not a new physical-console capture.
// FCSR side effects and latency remain separate and must not be inferred.
struct EeCop1DivResultV1 {
  std::uint32_t bits = 0U;
  static constexpr bool physical_console_qualified = false;
  static constexpr bool fcsr_effects_qualified = false;
  [[nodiscard]] bool operator==(const EeCop1DivResultV1 &) const = default;
};
[[nodiscard]] EeCop1DivResultV1
ee_cop1_div_bits_v1(std::uint32_t numerator, std::uint32_t denominator) noexcept;

// Ordered MUL.S value and operation-local U/O events. Integer-only reference
// model, not exhaustive console qualification or a hidden-ACC transition.
struct EeCop1MulResultV1 {
  std::uint32_t bits = 0U;
  bool underflow = false;
  bool overflow = false;
  static constexpr bool physical_console_qualified = false;
  [[nodiscard]] bool operator==(const EeCop1MulResultV1 &) const = default;
};

// Preserve operand order: the multiplier's omitted carry may subtract 2^15
// from the exact 24x24 significand product BEFORE normalization/truncation.
// Exp0 inputs flush without U; true product underflow flushes with U; exp255
// inputs are finite, overflow saturates. See SOURCE_MULTIPLIER_REFERENCE_V1.md
// for independently derived column arithmetic and finite source/reference
// evidence. No external implementation or expected table is incorporated.
[[nodiscard]] EeCop1MulResultV1
ee_cop1_mul_bits_v1(std::uint32_t left, std::uint32_t right) noexcept;

// Same FMAC U/O cause replacement and sticky accumulation as ADD/SUB;
// preserve actual prior I/D/C and all unrelated bits. MUL only, not MADD.
[[nodiscard]] std::uint32_t
ee_cop1_mul_fcsr_bits_v1(std::uint32_t prior_fcsr,
                         const EeCop1MulResultV1 &result) noexcept;

// Compiler-side ADD.S/SUB.S value and operation-local events. This is an
// integer reference model over every raw32 operand pair, not an exhaustive
// physical-console qualification or a complete FPU/ACC transition.
struct EeCop1AddSubResultV1 {
  std::uint32_t bits = 0U;
  bool underflow = false;
  bool overflow = false;
  static constexpr bool physical_console_qualified = false;

  [[nodiscard]] bool operator==(const EeCop1AddSubResultV1 &) const = default;
};

struct EeCop1AccumulatorV1 {
  std::uint32_t bits = 0U;
  bool overflow = false;
  [[nodiscard]] bool operator==(const EeCop1AccumulatorV1 &) const = default;
};
struct EeCop1MaddResultV1 {
  EeCop1AddSubResultV1 result;
  EeCop1MulResultV1 product;
  static constexpr bool physical_console_qualified = false;
  [[nodiscard]] bool operator==(const EeCop1MaddResultV1 &) const = default;
};
// EE value/operation-event boundary of the same separately rounded ordered
// product and ACC adder. Prior overflow must come from an actual ACC write.
// MADDA/MSUBA store result.bits and result.overflow; MADD/MSUB preserve ACC.
[[nodiscard]] EeCop1MaddResultV1 ee_cop1_madd_bits_v1(
    EeCop1AccumulatorV1 accumulator, std::uint32_t left,
    std::uint32_t right, bool subtract = false) noexcept;

// Restore a 24-bit significand, align magnitudes retaining one guard bit,
// discard lower alignment bits, add/subtract, normalize and truncate. Exp0
// inputs are signed zero; exp255 is finite EE data. Overflow saturates to
// signed 0x7fffffff. Cancellation underflow retains the normalized fraction
// with exponent zero; exact cancellation is +0 except two negative zeros.
//
// Qualification: independently derived rule corroborated by 36 ADD and 36
// SUB ps2autotests results, 40 source-author COP1 expected pairs (also tested
// as sign-adjusted SUB), and four separately reported EE 90K underflows.
// Neither external tables nor emulator implementations are incorporated.
// https://github.com/unknownbrackets/ps2autotests/tree/master/tests/cpu/ee_fpu
// https://github.com/TellowKrinkle/PS2Homebrew/blob/master/VUTests/mac.cpp
// https://github.com/PCSX2/pcsx2/pull/12001#issuecomment-5074203305
// Does NOT qualify MUL, MADD/MSUB, ADDA/SUBA hidden ACC state, DIV, forwarding,
// latency, conversion flags, or the original program's live execution.
[[nodiscard]] EeCop1AddSubResultV1
ee_cop1_add_bits_v1(std::uint32_t left, std::uint32_t right) noexcept;
[[nodiscard]] EeCop1AddSubResultV1
ee_cop1_sub_bits_v1(std::uint32_t left, std::uint32_t right) noexcept;

// Reference FCSR update for ADD.S/SUB.S only, using their returned events.
// Replace U/O cause bits; OR SU/SO sticky bits. All other incoming bits are
// preserved, including I/D causes, other stickies and C. prior_fcsr must be
// the actual known prior read value, not an invented reset state. This helper
// neither performs CTC1/readback normalization nor executes the instruction.
// Public source-author COP1 flag sequences corroborate this separation, not
// exhaustive console/revision behavior. No VU MAC flags enter the EE API.
[[nodiscard]] std::uint32_t
ee_cop1_add_sub_fcsr_bits_v1(std::uint32_t prior_fcsr,
                             const EeCop1AddSubResultV1 &result) noexcept;

// Compiler-side value result, not a COP1 machine-state transition. In
// particular, no caller may infer I/SI, other FCSR flags, ACC history, or an
// exception from clamped. Conversion flag effects remain unqualified.
struct EeCop1ConversionResultV1 {
  std::uint32_t bits = 0U;
  // CVT.W.S selects signed clamping when the encoded exponent exceeds 0x9d.
  // This includes the exactly representable negative endpoint -2^31.
  // CVT.S.W never takes that path.
  bool clamped = false;
  static constexpr bool fcsr_effects_qualified = false;

  [[nodiscard]] bool
  operator==(const EeCop1ConversionResultV1 &) const = default;
};

// Interpret source_word as a two's-complement signed word. Return the raw
// single-precision encoding after magnitude truncation to 24 significant bits.
// No host float conversion or ambient rounding mode is used.
[[nodiscard]] EeCop1ConversionResultV1
ee_cop1_cvt_s_w_bits_v1(std::uint32_t source_word) noexcept;

// Interpret source_single as a raw EE single-precision encoding. Truncate
// toward zero; clamp by sign for encoded exponents > 0x9d. Exponent zero
// converts to zero, while exponent 255 is finite EE data, not IEEE NaN/Inf.
//
// Qualification of both conversions: independently derived integer reference
// operations, corroborated by finite public ps2autotests conversion results
// and the R5900 toolchain author's truncation report. This is not exhaustive
// physical-console qualification, and does not qualify preceding ADD/MUL or
// conversion FCSR effects. No original instruction stream is executed here.
// https://github.com/unknownbrackets/ps2autotests/tree/master/tests/cpu/ee_fpu
// https://sourceware.org/pipermail/binutils/2012-November/079351.html
[[nodiscard]] EeCop1ConversionResultV1
ee_cop1_cvt_w_s_bits_v1(std::uint32_t source_single) noexcept;

// The value seen in FCSR after CTC1 to control register 31, given a fully
// known low source word. Writable C/I/D/O/U and sticky flags are copied;
// read-only fields return their fixed bits, including RM=01. This is not
// conversion-flag execution, a power-on/caller-state default, FCR aliasing,
// instruction latency, or an ACC-state operation. Public ps2autotests fcr
// source/results corroborate this writable-bit projection.
[[nodiscard]] std::uint32_t
ee_cop1_ctc1_fcsr_bits_v1(std::uint32_t source_word) noexcept;

} // namespace openrc
