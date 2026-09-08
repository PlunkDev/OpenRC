# Bounded source numerical recovery

These compiler/diagnostic changes support exact original actor initialization;
they are not a runtime PS2 interpreter or a completed initialization callback.
Original source bytes and diagnostic traces remain under ignored `local`.

## VU instruction semantics

The existing DVP decoder/executor now supports FMAND, FMEQ and FMOR. They read
MAC flags into the selected integer register without changing flags. Bitwise
knownness is retained; a proven mismatch settles equality, but an unresolved
comparison cannot invent a branch direction. Flag results use the existing
four-pair arithmetic publication queue and the pre-pair register snapshot.
Tests distinguish consecutive producers, same-pair reads and immediate
dependent branches, including indeterminate branches and their delay slots.

A paired LOI now updates I for the next instruction; the current upper
instruction reads the previous I. This fixes the source polynomial's constant
sequence rather than replacing that sequence with host trigonometry. Tests
cover known/unknown prior I and publication into an E delay instruction.

`dvp_vu_ftoi_bits_v1` implements FTOI0/4/12/15 with bounded integer shifts,
truncation toward zero and sign-directed saturation. It does not create host
floats, depend on their rounding mode, or interpret exponent 255 as IEEE NaN.
The executor uses the shared helper while preserving lane masks, VF0, unknown
inputs and unchanged flags. FTOI needs no arithmetic warning. ADD/SUB now use
the separately qualified integer reference model described below; ITOF,
MUL/MADD/MSUB and the remaining host-based arithmetic retain their warning.
The 524,288 rational-reference cases are specification-derived, **not newly
captured hardware measurements**.

Instruction semantics were checked against the VU register/pipeline and
individual instruction descriptions. Public encoding facts can also be
cross-checked in the [binutils DVP opcode reference](https://github.com/ps2dev/binutils-gdb/blob/dvp-v2.45.1/opcodes/dvp-opc.c).
No proprietary manual, SDK, link to a proprietary manual copy, or emulator
implementation is included in the repository.

MIN/MAX now preserve the selected raw operand instead of flushing denormals
or clamping its exponent first. Denormal selection is supported by the
[PCSX2 developer's published hardware findings](https://pcsx2.net/blog/2009/ps2-vu-vector-unit-documentation-part-1/).
Raw sign/magnitude comparison, including signed-zero and extended-exponent
ordering, is corroborated by the upstream interpreter, not certified by a new
physical capture here. The executor retains its conservative approximation
warning. Tests exercise both operand orders and preserve the input encodings.

The user also authorized removal of a pre-existing uncommitted animation
heuristic that reversed descending frame-address tables. Source frame selection
indexes the declared table directly; physical address order does not determine
logical playback order. A new regression failed with that heuristic and passes
after its removal for both Ratchet-relative and Moby-relative addressing. The
19-level Ratchet structural scan still accepts all 33,977 source frames. This
does not establish that the heuristic caused the visible Veldin animation
issues: the separately checked local Veldin directories contained no descending
table. Full animation-state/playback fidelity remains a different task.

## Integer ADD/SUB reference model

`dvp_vu_add_bits_v1` and `dvp_vu_sub_bits_v1` return raw 32-bit result words and
per-lane zero, sign, underflow and overflow flags in `DvpVuAddResultV1`.
They use bounded integer operations, not host floats or a changed host
rounding mode. The stated model covers every raw input encoding:

- Exponent-zero inputs become signed zero; exponent 255 remains finite.
- Alignment retains one guard bit and discards lower magnitude bits before
  signed addition. Normalization then truncates to the result significand.
- Overflow saturates to signed VU Fmax. Underflow forces exponent zero but
  preserves the normalized fraction, setting both U and Z even when raw
  fraction bits remain nonzero. A later arithmetic input flushes that encoding.
- Exact cancellation gives +0 except adding two negative zeros. SUB reverses
  the second operand's sign before applying the same rules.

The executor uses this model for its existing ADD/SUB vector, broadcast, I/Q
and ADDA/SUBA variants, preserving operand snapshots, masks, unknownness, VF0
and the existing four-pair flag publication. Each execution retains
`vu_add_sub_reference_model` (CLI: `vu-add-sub-reference-model`). This is not a
host-float operation, but neither is it an exhaustively hardware-qualified
operation. Writing the ADDA/SUBA result word does **not** recover the hidden
ACC overflow latch. No live-initialization readiness gate was relaxed.

Evidence levels are intentionally separate:

- The [published VU research, Note 3](https://pcsx2.net/blog/2009/ps2-vu-vector-unit-documentation-part-1/)
  describes one guard bit and explicitly qualifies the extent of its testing.
- An independent in-memory integer model agrees with all 40 ADD lane results
  and all 10 MAC/STATUS groups in [TellowKrinkle's authored test source](https://github.com/TellowKrinkle/PS2Homebrew/blob/508ee8070d740498d6bc0bec275d0defa0429a93/VUTests/mac.cpp).
  That source distinguishes the one-guard model from zero/two/three-guard
  alternatives. Its expected tables are **not** our physical-console capture:
  no attached run log, console model or per-table capture provenance was
  established. No license grant was found, and no external implementation or
  test table was copied into OpenRC.
- Four raw ADD/SUB underflow examples and simultaneous U+Z are separately
  [reported from VU0 on a PS2 90K](https://github.com/PCSX2/pcsx2/pull/12001#issuecomment-5074203305).
  Generalizing the combined rules to the entire raw domain remains a reference
  model, not exhaustive physical validation or proof across console revisions.

Five new primitive test groups use independently generated zero/denormal,
exponent-scale identity/doubling, guard/carry, underflow/reuse and 65,536 raw-pair
metamorphic cases. Four executor groups cover 16 operand-family combinations,
mask/alias/unknown lanes, U+Z timing/VF0, and separate mixed-program warnings.
The focused numeric and executor tests passed. These are contract/integration
tests, not copied public fixtures or newly measured PS2 results.

MUL's low-bit carry correction and the complete MADD/MSUB/ACC transition and
flag rules remain unresolved. The separate host-based MADD/MSUB path,
including its internal adder, was deliberately left unchanged; replacing
only that adder would not qualify the compound operation. ITOF and the other
host-based paths retain `host_float_approximation`. This VU model also does not
certify the EE COP1 arithmetic required by the floating frontend emitter.

## EE COP1 conversion values

The compiler-side `ee_cop1_numeric.hpp` now provides independently derived,
integer-only CVT.S.W and CVT.W.S value helpers. Signed words convert by keeping
the leading 24 significant bits, toward zero. Raw single encodings convert
to an integer part or the sign-selected endpoint for encoded exponents above
157. The explicit `clamped` result marks that path, including exact negative
2^31; it is not an exception or invalid-operation flag. No host rounding mode,
host float cast or hidden accumulator is used.

This value contract is finitely corroborated by the
[R5900 toolchain author's truncation report](https://sourceware.org/pipermail/binutils/2012-November/079351.html)
and all 64 value rows in the
[published ps2autotests conversion results](https://github.com/unknownbrackets/ps2autotests/blob/7655976cb25c95abcb409b360ac828e0a48356c4/tests/cpu/ee_fpu/convert.expected).
Those external results were compared independently in memory; neither their
tables nor harness code are copied into OpenRC. This is not a new console run
or exhaustive hardware qualification.

The separate CTC1 helper projects a supplied FCSR write through writable mask
`0x0083c078` and fixed bits `0x01000001`, corroborated by the public
[FCR test results](https://github.com/unknownbrackets/ps2autotests/blob/master/tests/cpu/ee_fpu/fcr.expected).
It is not a default for unknown caller state. Conversion FCSR effects remain
explicitly unqualified: the available conversion-value harness does not record
those flags, and the reference descriptions conflict on clamped conversions.

Five permanent test groups cover all signed halfwords, every exponent/sign,
precision boundaries, 65,536 generated raw values and all 512 writable FCSR
combinations. An audited original-ELF probe verifies both complete conversion
leaves, the actual startup CTC1 and **131,088 source-routing cases** with
poisoned unrelated state. The probe validates binding/control flow against the
original instructions, not a physical numerical oracle. General EE MUL,
frontend projection and VU MUL/ACC remain separate incomplete dependencies.

## EE COP1 ADD/SUB reference values and flags

`ee_cop1_add_bits_v1` and `ee_cop1_sub_bits_v1` provide integer-only raw32
reference operations over all operand encodings. The existing independently
derived VU value adder is shared privately, without exposing VU MAC flags as
EE state or changing the prior VU behavior. Magnitude alignment retains one
guard bit, normalization truncates, exp0 inputs contribute signed zero, and
exp255 remains finite. Overflow saturates; cancellation underflow retains
the normalized fraction at exponent zero with a separate event.

Separate EE corroboration includes 36 ADD and 36 SUB values from the
[primary arithmetic harness/results](https://github.com/unknownbrackets/ps2autotests/tree/master/tests/cpu/ee_fpu)
and 40 selected sums explicitly run through COP1 by the
[source-author harness](https://github.com/TellowKrinkle/PS2Homebrew/blob/508ee8070d740498d6bc0bec275d0defa0429a93/VUTests/mac.cpp).
Its sign-adjusted SUB checks and flag sequences corroborate the same narrow
contract; they do not double the number of independent rounding cases.
External code/tables are not incorporated. `physical_console_qualified=false`
remains explicit: a complete reference value domain is not exhaustive hardware
qualification or a new console capture.

`ee_cop1_add_sub_fcsr_bits_v1` replaces U/O causes, accumulates SU/SO and
preserves other actual incoming bits. It neither performs CTC1 normalization
nor invents reset state. Four new generated test groups exercise exponent and
guard boundaries, underflow reuse, 917,504 exact integer-domain operations,
65,536 raw-pair identities and 512 writable FCSR combinations. The audited
native EE and VU test executables pass with the shared helper.

This does not complete timer `1f98c0`, color interpolation `1fa8a8` or timed
color `21c6c0`: those depend on MUL/ACC or DIV as well. No ideal-product
approximation, fitted exceptional operands or host-float replacement was
added for the unresolved multiplication network.

## Source integer spatial projection

`project_rac_moby_spatial_bounds_v1` accepts explicit, already converted raw
center X/Y and radius words plus the actual old packed bounds word. It preserves
32-bit wrapping before arithmetic shift, byte truncation, packed endpoint
order, low-64-bit comparison against sign-extended old LW, and the following
minimum-only mask gate. Output is a raw source projection and a gate result,
not an automatically validated rectangle or a successful spatial update.

The preceding spatial-pointer check, earlier cache/vector/counter writes, and
the later spatial helper's bounds store/signed-maxY behavior remain separate.
This adapter does not attach a raw source structure to the native runtime or
bypass the ordered spatial index's supported-domain validation.

An independent finite tracer executed the original 18 integer instructions
at `251ef0..251f38` with explicit synthetic converted inputs, poisoned high
register halves and varied Z. The audited native comparison matched all 8,890
packed results and gate outcomes: 1,792 unchanged, 5,613 minimum rejects and
1,485 calls to the next source helper. This is an original-instruction
comparison, **not** a physical PS2 or live-level numerical capture.

## Remaining boundary

The actual 94-pair VU0 source chunk now decodes without unknown instructions.
An ignored source probe completed 18 bounded executions (six rotations and
three unknown/poisoned scratch states), checking exact initialized identity
for zero rotations and source helper/control-flow selection for nonzero axes.
That finite source probe does not certify nonzero numerical results. With the
new ADD/SUB integration, the chunk combines the integer reference model with
the remaining host-based MUL/ACC path and retains both qualifications. Full
FMAC forwarding, stalls, multiplier rounding and ACC behavior are not certified
by the probe or by the focused ADD/SUB tests.

The pure [fresh constructor](RAC_MOBY_FRESH_CONSTRUCTOR_V1.md) has now been
implemented and source-compared independently: it contains raw FP transfers
but no FP arithmetic. This does not qualify the later placement/post-step.

Next: qualify those remaining arithmetic operations, finish the complete
source-ordered placement/post-step and bind live-order tokens to native
entities. Neither the published resource count nor actual AI has been expanded
by these diagnostic/numerical components. The native compiler identity advances
to `native-eight-resource-v6-vu-addsub`: the ADD/SUB reference semantics affect
existing source-program execution during preparation, so a previously prepared
cache cannot silently certify the new compiler. The normal launcher Prepare path
rebuilds it; the runtime schemas, launcher count and save directories do not
change.
