# Source DIV, SQRT and compound ACC reference v1

This extends the integer ADD/SUB and ordered MUL models. It is an independently
implemented numerical **reference**, corroborated by primary authored results
and original-instruction comparisons. It is not a new physical PS2 capture or
exhaustive hardware qualification. No external implementation, expected table,
ELF bytes or extracted game data is incorporated into the repository.

## Numerical contract

`ee_cop1_div_bits_v1` and `dvp_vu_div_bits_v1` use a 25-column radix-2 redundant
remainder calculation. Quotient digits are selected using the upper-byte sum
and the OR of column 23, preserving the distinction from a fully propagated
integer quotient. Zero digits retain the preceding redundant representation
for digit selection. Modulo-32 full-adder reductions and signed quotient
updates are explicit integer operations. Exponent-zero inputs are signed zero;
exponent 255 is finite. A zero divisor or overflow selects signed Fmax; true
underflow flushes to signed zero. Host IEEE division and a blanket one-ULP
adjustment do not describe this operation.

`dvp_vu_sqrt_bits_v1` uses the same quotient selector with the changing
square-prefix correction. The argument sign is ignored, exponent-zero inputs
return positive zero, and odd/even exponent normalization is explicit. This
standalone value helper does not provide VU scheduling or I/D effects.

`dvp_vu_rsqrt_bits_v1` retains the separately rounded SQRT result as the DIV
denominator. This ordered composition agrees with all 12 published authored
[RSQRT value cases](https://github.com/TellowKrinkle/PS2Homebrew/blob/master/VUTests/div.cpp),
including irrational neighbors, exponent255 and signed exponent-zero inputs.
The comparison was evaluated in memory with the independently implemented
integer recurrence; external expected tables are not retained. This is
corroboration of the value reference, not exhaustive hardware qualification.
The radicand sign is removed even for negative zero, so the result sign is
the numerator sign. Q latency and invalid/divide flags remain separate.

`ee_cop1_madd_bits_v1` and `dvp_vu_madd_bits_v1` expose an accumulator word and
its overflow latch. They first calculate the ordered, separately rounded MUL
product, then apply the one-guard-bit accumulator ADD/SUB. Product overflow
takes precedence over an existing ACC overflow and supplies the product sign
(reversed for MSUB). Otherwise a latched ACC overflow preserves the ACC sign
and saturated magnitude. A normal product uses the existing integer adder.
The result includes separate product events; these cannot be reconstructed
from the final value alone.

VU final MAC/current events describe the final result. Sticky events also
include the original product's Z/S/U/O, before sign reversal for MSUB. Thus a
negative underflowed product can set sticky Z/S/U while a positive, normal
final accumulator value has no corresponding current event. ACC-writing
operations update the per-lane overflow latch; VF destinations preserve it.
Unknown incoming latch state remains unknown until a real writing operation
establishes it. The shared executor retains its separate flag publication and
DIV scheduling; these helpers do not silently establish caller state.

## Primary evidence and limits

The divider/square-root algorithm was studied in the primary author's
[PS2Float implementation and algorithm notes](https://github.com/TellowKrinkle/PS2Float/blob/main/ps2divsqrt.cpp),
which reference the radix-2 square-root/division work at
[DOI 10.1109/ARITH.1995.465363](https://doi.org/10.1109/ARITH.1995.465363).
The local implementation reformulates the remainder selector and correction
with scalar column arithmetic. Public reference implementation access is
disclosed; this is not a claim of an isolated clean-room process.

Published [ps2autotests EE FPU results](https://github.com/unknownbrackets/ps2autotests/tree/master/tests/cpu/ee_fpu)
and the primary author's
[VU MAC tests](https://github.com/TellowKrinkle/PS2Homebrew/blob/master/VUTests/mac.cpp)
were parsed in memory. The independently derived local reference agrees with
36 authored DIV values, 72 MADD/MSUB values, and 18 combined EE/VU flag groups.
These are published expectations, not newly measured output. External tables
and code are not retained. The ignored qualification script and its report
are `local/forensics/qualify_div_acc_reference.py` and session tool output.

Permanent tests independently verify DIV powers/sign/range (131,072 cases),
identity/self/sign with distributed mantissas (195,840 cases), SQRT integer
enclosures across all exponents (65,280 cases), sign invariance and 4,095
integer perfect squares. Compound tests cover exact integer algebra, product
versus ACC overflow precedence, intermediate underflow/sign sticky effects,
unknown latches and their subsequent source writes. This leaves unmeasured
hardware timing, forwarding/stalls and other execution-unit state outside the
value reference's qualification.

## Reached frontend consumers

Timer `1f98c0` now executes CVT.S.W, source ADDA(.25,.25), ordered MADD and
CVT.W.S for the actual caller argument and scale. PAL `3f555555` therefore
maps 180 to 150 and 12 to 10. There is no unit-scale-only admission rule.

Timed color `21c6c0` retains its source timer calls and SUBU order, then
executes DIV.S and SUB.S followed by `1fa8a8`'s VU MULAw/MADDx/FTOI0. Non-dyadic
values are executable; supplied diagnostic observations can only agree with
an executed value, never replace it. Age 1/duration 10 produces factor
`3dccccd0`; ordered rounding can make alpha 127 where host interpolation
would give 128.

`rac_frontend_numeric` lowers camera `1eb338` via identity `125358`, the
source polynomial/SQRT helper `125380`, and X/Y/Z rotation owners
`1254a0`, `125548`, `1253f8`. It preserves each MUL and ACC operation and the
COP1 NEG bit behavior when building the `1f2608` view. No host trigonometry,
square root, normalization or replacement camera basis is used.

The ignored source fixture executes 259 rotations, including the original
constant frontend camera, through 95,053 actual ELF instructions. Native raw
rotation and view matrices agree. The source render buffer is 512x448; final
PAL display is 512x512. Source near/far values 32/745472 use coordinates scaled
by 1024 at `1f28f4`, giving neutral 0.03125/728. Inverting the actual source
projection scale gives neutral tangent words `3f2147af`/`3ef3daf9`, one ULP
above nominal source parameter `3f2147ae` and its ordered vertical product.
Neutral raster rendering remains its own representation, not a physical GS
pixel/overscan capture.

Projector `238d90` now has an executable compiler helper. Its inputs are two
corners and actual live camera, matrix, scale and origin reads. `1f9bf0` is
VSUB.xyz against the camera, followed by source scale 1024, W=1, matrix helper
`1f9ee8`, three reciprocal calls, ordered XY scaling and TRUNC.W.S. It does not
read object length fields +40/+44. Its raw 187140 matrix must come from the
source camera/projection owner; a neutral-camera host reconstruction is not
a substitute. The ignored 512-case fixture executes 98,816 instructions,
including zero-W/saturation paths. It validates the consumer over synthetic
live state, not the separate production owner that supplies that state.

Original source fixture identity: SCES-50916 ELF SHA-256
`17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b`.
All original data and instruction traces remain under ignored `local`.

## Frontend framing cross-check

The first GPU inspection of the animated frontend actors showed Ratchet and
the ship near the bottom over a black background. A separate original238d90
run projects the original update100 root positions to (68,410) for class0
and (332,405) for class530 in the source512x448 render buffer. The native
512x512 presentation inside a centered720x720 rectangle on a1280x720 target
maps these roots to approximately(376,659) and(747,651), consistent with
the observed actor placements after their local mesh offsets. This is
evidence against changing camera basis, field of view or actor scale to
compensate for the black surroundings.

The original frontend WAD also contains48 TIE classes and33 shrub classes.
The initial animated-scene compiler only emitted the five Moby actors;
its class-directory pass explicitly excluded TIE/shrub geometry. The static
environment therefore requires its original placement/render compilation in
addition to this animated timeline. The ignored framing comparison is
`local/forensics/check_frontend_camera_framing.py`; the inspected capture is
`local/forensics/frontend-transition/v9-flow.frontend.png`.

The remaining geometry length tail23b5d0 now has a separate compiler adapter
in `rac_frontend_geometry`: subtract corner0 from corners1/2, separately
square XYZ, ADDA(x²,y²), MADD(1,z²), VSQRT and VADDq. It returns the actual
PVar+40/+44 values while preserving all corner words. Its512-case ignored
fixture executes26,112 original instructions, including exceptional ranges.
The full wrapper retains pre/stop→corners and lengths→post ordering.
