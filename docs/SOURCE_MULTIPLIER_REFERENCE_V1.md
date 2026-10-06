# Ordered PS2 multiplication reference

OpenRC implements standalone EE MUL.S and VU MUL/MULA values with bounded
integer arithmetic. This is a complete raw-input reference model, **not an
exhaustive physical-console qualification**, and not a complete ACC machine.
The VU diagnostic executor retains `vu_mul_reference_model`; MADD/MSUB still
retain their separate approximation warning and unchanged compound path.

## Evidence and provenance

The primary author's [PS2Float research](https://github.com/TellowKrinkle/PS2Float/commit/48b3ff3db3f4a310c4f738ee6f51471fec5af548)
and later explanatory reduction diagrams identify the Booth and carry-save
topology. OpenRC first derived a separate scalar low-column calculation from
those descriptions, then checked the author's small recoding definition and
bit boundaries. No external implementation, expected table or assembly is
incorporated. This is reference-based interoperability work, not a claim that
the entire process was an isolated clean-room hardware experiment.

Finite corroboration is kept separate:

- The [primary VUTests author](https://github.com/TellowKrinkle/PS2Homebrew/blob/508ee8070d740498d6bc0bec275d0defa0429a93/VUTests/mac.cpp)
  routes 48 selected MUL results through both EE COP1 and VU0 COP2. The new
  model agrees with all 48; an exact-product/truncation model agrees with 43.
  It also agrees with all 48 compact EE flag bytes, 12 VU MAC words and 12 VU
  STATUS groups, including signed zero and true-underflow events. These are
  authored expectations without a per-row console capture log.
- [Fobes' hardware experiment](https://fobes.dev/ps2/detecting-emu-vu-floats)
  reports ordered multiplication anomalies. Its linked public gist actually
  contains 122 raw result records; the model agrees with all 122. The article's
  informal larger-number description is not used as a measured-vector count.
- The separate [90K hardware report](https://github.com/PCSX2/pcsx2/pull/12001#issuecomment-5074203305)
  corroborates silent exponent-zero input flushing, true product underflow,
  and VU Z+U. Its unpublished larger FMAC corpus is not counted as our evidence.

Public accessibility does not grant permission to vendor these repositories.
The comparisons were performed in tool memory; repository tests are our own
synthetic contract tests. No console/revision equivalence is inferred.

## Derivation of the integer correction

Let A and B be the restored positive 24-bit significands, in source order, and
P=A*B the exact 48-bit integer product. Modified radix-4 recoding selects digit
`low_bit + middle_bit - 2*high_bit` from overlapping three-bit groups of B.
A negative digit contributes the shifted one's complement of its magnitude,
plus a separate positive unit at that digit's column.

Representation matters: selectors 000 and 111 produce a zero row **without**
a negative correction. That is an explicitly checked source-author rule,
not an inference from the mathematical equality of positive and negative
zero. Another representation can change carries in a truncated network.

For a column-wise three-input reduction, each output column contains the
parity of its three input bits; their majority is deferred one column left.
This identity preserves the integer sum before truncation. The described
network has two initial three-row reductions, two second-stage reductions,
then two combining stages. Three postponed correction columns and two
postponed data bits must be injected at their specified stages, not simply
added to the initial operands. The final two rows are truncated separately
below column 15 before summation.

OpenRC needs only the parity at column 15 after that separated truncation.
Rows beginning at column 16 cannot affect it. Tracking the longest carry
dependency backwards through the four levels and the final correction
injection shows that input columns below 10 cannot reach 15. The implementation
therefore retains columns 10..15 of the lowest eight recoded rows. It keeps
column 15 itself, avoiding a separate formula for the parity of the original
high row bits. Every shift and temporary fits unsigned 32 bits; P uses 64 bits.

Let H be the parity of the two final rows' column 15 bits, added **without**
the discarded lower-column carry. Comparing H with bit 15 of P detects the
reported omitted unit. The reference product is P when equal and P-2^15
otherwise. This correction is the source-backed rule, not a general theorem
that an arbitrary truncated Booth network loses at most one carry.

All lower product bits can remain those of P: the eventual significand
truncation removes at least 23 bits, so their values cannot alter the returned
word once the column 15 borrow has been applied. Normalization happens after
the correction, including a possible boundary crossing. Consequently this
is neither commutative multiplication nor a blanket one-result-ULP decrement.
With a pure power-of-two RIGHT operand, the low recoded groups vanish and
there is no correction. The same statement is not true for a LEFT identity.

## Packing and operation-local events

Exponent-zero operands supply signed zero and cause no U/O event. Exponent 255
is finite extended PS2 data. For nonzero operands, normalize the corrected
product, truncate to 24 bits and combine the biased exponents. Overflow
saturates to signed maximum and raises O; true underflow returns signed zero
and raises U. VU Z/S follow the returned encoding, including Z+U together.
EE's separate FCSR helper replaces U/O causes, accumulates their stickies and
preserves the supplied actual prior I/D/C and unrelated bits.

MULA can write the returned value and ordinary lane flags, but this does not
recover its hidden overflow latch. Compound product/ACC collisions, sticky
intermediate events, MADD/MSUB and full forwarding remain separate work. No
live-initialization gate is relaxed merely because a host operation was
replaced by this reference model.

## Independent regression checks

The test oracle uses individual bits 0..16, column population counts and
ripple carry injection. It deliberately does not call the packed production
kernel. Unnormalized product comparisons check the window reduction even
when final significand truncation would hide a mistake. These prove agreement
between two formulations of the stated model, not universal hardware truth.

Tests sweep both operand orders, every low-16 pattern, generated Booth-group
neighbors, signs, all exponents, flush/saturation and normalization edges,
right-hand powers of two, and the exact byte-times-half color domain. EE/VU
adapters and EE FCSR events are checked separately. Executor tests cover all
fourteen existing MUL/MULA variants, masks, aliases, partial unknown inputs,
VF0 protection, four-pair MAC publication and distinct mixed-program warnings.

An ignored source-input audit also follows 4,724 ordered multiplication steps
from 286 Veldin placements across 16 resolved models. Compact and wider-column
models agree for all 2,450 unique operand pairs. Nine intermediate polynomial
steps differ from ideal-product/truncation, including the authored Ratchet
rotation. These operands are derived from actual asset inputs through the
reference ADD/MUL dependencies, not captured VU execution. The audit stops
before compound ACC sums and does not establish final matrix fidelity.
