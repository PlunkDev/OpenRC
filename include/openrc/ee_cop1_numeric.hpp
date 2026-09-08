#pragma once

#include <cstdint>

namespace openrc {

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
