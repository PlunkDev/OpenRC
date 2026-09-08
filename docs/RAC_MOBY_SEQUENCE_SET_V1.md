# Original Moby sequence and frame selection

The compiler-side `set_rac_moby_sequence_v1` implements the complete scalar
`258800` setter and uses the general `24fbf8` frame-reference leaf. The original
late player initialization calls this setter after allocation; the adapter
also retains the full argument domain rather than specializing to that call's
sequence1/frame0. No floating-point arithmetic occurs in these functions.

The first sequence-table lookup uses the original wrapping SLL2 address
calculation, represented by a resolved 30-bit word-index key. Stored sequence
fields are bytes, so the later leaf can legitimately access a different table
entry. The frame comparison uses the full signed64 incoming argument before
the original word/byte truncations. Frame count zero, negative/wide arguments
and both later byte clamps follow the actual source operations. The adapter
does not apply a new generic loop/clamp policy.

`resolve_rac_moby_frame_references_v1` resolves previous and current frames
through logical table indices, never physical address ordering. Previous
sequence FF selects the original snapshot reference and sound/trigger markers;
current sequence FF still reads the ordinary sequence table. Returning a
snapshot reference does not fabricate its contents or capture a new pose.

The setter copies the actually resolved previous frame's first word into the
animation-rate field without interpreting it as host floating point. It
clears only bit1 of the supplied current sequence-flags byte. It does not
change animation speed, reset interpolation phase, invoke callbacks or create
an actor. All changed fields are returned as a named patch for the staged
owner; other actor fields remain untouched.

## Ownership and integration boundary

Inputs are canonical sorted, unique, bounded compiler bindings for reached
sequence/frame table words and aligned frame first words, resolved from current
disjoint source owners. Unknown reached entries are errors, not implicit zero
frames. A frame byte outside the sequence's count is not silently clamped if
the original leaf actually reads that logical index. Source references remain
opaque compiler values and must be lowered before any runtime publication.

The existing fresh-constructor sequence0/frame0 specialization is retained;
the new general leaf can be compared against that established case. This
component does not complete animation playback, blending, the gameplay state
machine or later placement/rotation/spatial initialization. Those remain part
of the original actor lifecycle and Veldin fidelity work.

Seven permanent synthetic groups pass in the audited native executable. They
cover full64 frame arguments, first-lookup/low-byte sequence differences,
zero/max counts, previous/current FF asymmetry, raw rate words and every flag
byte, missing/invalid bindings and agreement with the unchanged fresh leaf.
Original-instruction comparison remains separate from animation/gameplay
fidelity and physical-console testing.

The independent original-instruction trace and audited native probe match
**1,157 complete 256-byte actor results**: 932 setter calls and 225 direct
leaf calls, including 365 previous-sequence FF cases. The tracer executes
both actual functions, checks all three current count reads and final
flag/rate store order, and verifies unchanged disjoint dependencies and guard
bytes. Direct-leaf native comparisons deliberately omit frame first-word
contents because that function never reads them. This is source equivalence
over explicit synthetic inputs, not a live animation or console capture.
