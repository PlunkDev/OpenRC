# Original actor post-step control and numerical boundaries

`RacMobyPostV1` reconstructs the finite control and named actor writes of
original entry `251e30`, following fresh construction, authored placement or
the late-player sequence setter. It is compiler-side code, not a runtime PS2
interpreter, a second object world or a completed original initialization path.
The alternate `251fd0` entry is not included by this contract.

The adapter retains the signed negative-state return, cached-matrix flag,
equal-sequence header cache, both different-sequence paths and the original
ordered stores. Previous sequence FF selects the separate vector snapshot
table, not the frame-snapshot data used by the sequence setter. Current FF
still uses the ordinary sequence table; equal FF with cache key FF uses the
cached header without reading the model table.

The late player after setter `(1,0)` has sequence bytes 1/1 and a cold FF
cache key, so it loads header **1**, not the earlier authored fresh path's
header 0. Its scale remains the constructor's raw model scale, without an
invented authored-scale multiplication. Matrix negation changes only the
second column's XYZ lanes using the existing reference SUB helper. Full matrix
stores precede the derived vector and the low16 counter increment; the other
counter bits and the actor's source position/rotation/scale remain untouched.

## Explicit continuation and actual execution

The finite continuation identifies a reached rotation, header blend, scale,
derived ACC chain, spatial tail or actual source return. Typed numerical
responses bind external results; accepting a response does not establish its
arithmetic correctness. A missing response is not a successful no-op. Rotation
responses enforce the proven zero magnitude of column W, allowing either zero
sign while the complete ACC sign rules remain unqualified. Cached matrices
retain their actual W bits.

`evaluate_scale_reference()` genuinely evaluates the reached scale stage:
four ordered header-lane times scale operations, then three position-lane
times 1024 operations using the shared [MUL reference](SOURCE_MULTIPLIER_REFERENCE_V1.md).
It retains operand order and raw encodings, then executes the existing FTOI0
radius conversion before leaving the derived ACC chain pending. It does not
pretend that the remaining rotation, blend or compound accumulator arithmetic
has been recovered, or publish a fabricated VU register/flag history.

After a supplied derived XYZ result, W retains the previously scaled radius.
The adapter applies FTOI0 to XYZ, writes the full derived vector and increments
only the low16 counter before the null-spatial gate. It reuses the existing
[integer bounds projection](SOURCE_NUMERIC_RECOVERY_V1.md), including the
full64 comparison against the sign-extended old word and the original minimum
mask. A reached `251b58` tail stays pending: there is no fake spatial-completion
method, new bounds store or parallel spatial index. It must be connected to
the existing ordered spatial owner with the correct live-token binding.

## Ownership and atomicity

The continuation owns copied named fields and bounded resolved header/vector
bindings. Reached source intervals must be aligned, non-wrapping and disjoint
from the actor being changed. Shared immutable intervals are allowed only when
every overlapping byte agrees, including model-table words that overlap header
or snapshot data. Only actual reads become observations; the desired-sequence
ADD in a branch delay retains its signed-overflow check without pretending to
perform the later load early.

Each resume validates its pending stage and stages changes before a no-throw
replacement. Wrong-stage/repeated responses, inconsistent owners, missing
bindings, write-budget exhaustion or allocation failure cannot partially
advance that continuation. The caller must not publish the actor while required
numerical or spatial work remains pending. Runtime materialization already
uses the [actual session-loaded world](ENTITY_SCENE_V1.md); lowering this full
ordered original lifecycle into that world remains separate integration work.

Tests cover negative states, both fresh header paths, equal/current/previous
FF distinctions, consistent and contradictory aliases, raw lane encodings,
reference scale execution, W preservation, low16 wrap, source return gates,
typed single-use responses and atomic failures. Conditional source-instruction
comparison is separate from complete numerical execution and physical-console
or normal-flow fidelity evidence.

The audited original-instruction/native comparison passes 1,008 synthetic
cases and 4,023 checkpoints, checking complete 256-byte actor projections,
ordered writes and typed request arguments. It explicitly substitutes external
rotation, blend, scale and derived results on both sides; it is conditional
scalar evidence, not proof that those numerical callees executed. Its 287
reached spatial tails remain pending. The separately tested scale method does
execute its seven reference products. Source bytes, fixtures and probes remain
under ignored `local`, not in runtime resources or repository tests.
