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
