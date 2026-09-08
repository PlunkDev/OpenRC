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
inputs and unchanged flags. Only FTOI loses the host-arithmetic warning;
ITOF, MUL/ADD/ACC and other unrecovered arithmetic retain their qualification.
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
Nonzero numerical results remain explicitly host-float approximations; full
FMAC forwarding, stalls and MUL/ADD/ACC rounding are not certified by that test.

The pure [fresh constructor](RAC_MOBY_FRESH_CONSTRUCTOR_V1.md) has now been
implemented and source-compared independently: it contains raw FP transfers
but no FP arithmetic. This does not qualify the later placement/post-step.

Next: qualify those remaining arithmetic operations, finish the complete
source-ordered placement/post-step and bind live-order tokens to native
entities. Neither the published resource count nor actual AI has been expanded
by these diagnostic/numerical components. The native compiler identity advances
to `native-eight-resource-v5-vu-loi`: corrected LOI semantics affect existing
source-program execution during preparation, so a previously prepared cache
cannot silently certify the new compiler. The normal launcher Prepare path
rebuilds it; the runtime schemas, launcher count and save directories do not
change.
