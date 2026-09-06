# PlacementAdmissionV1

`PlacementAdmissionPlanV1` is a finite, source-compiled decision for one
authored placement. The native executor operates on canonical
[session-state bytes](SESSION_STATE_V1.md), not RAC records, PS2 addresses,
class identifiers, or executable bytecode. It decides whether construction
may proceed; it does not construct an actor or execute a whole level loader.
The published native-game profile remains at eight resources.

## One ordered decision

A plan binds to the exact session-state schema digest and contains an initial
32-bit count, optional reverse registration, optional byte suppression, and
one of three conditions: none, a clear-bit requirement, or byte-selected
count handling. References name an unsigned typed view and element index.
There are no inferred table values or implicit level-dependent lookups.

Execution preserves this order:

1. Validate the plan, state-schema digest, and expected state revision.
2. If requested, scan the registration row from its last slot to its first.
   The first zero or exact-match value wins. Write only that `u16` element.
3. Read the suppression byte, if requested. A nonzero value rejects the
   placement without undoing the earlier registration write.
4. Evaluate the reached condition. A selected absent count arm rejects;
   an allowed arm supplies its maximum and a bit-controlled current count.

For the adjusted count, all operations wrap as unsigned 32-bit arithmetic:
`t = maximum + 1`, `a = t + (t >> 31)`, then
`current = (a >> 1) | (a & 0x80000000)`. Counts remain uninterpreted bit
patterns here, not assumed health values or rewards.

An absent operation differs from a present operation with unavailable input.
Unavailable input is an error only if execution reaches that read. Unknown
views, wrong value types, and out-of-range elements are likewise checked on
access, not on untaken branches. Structural bounds and key syntax are always
validated, even for untaken branches.

All reached reads after registration observe its staged write through the
same underlying bytes, including overlapping byte and word views. Ordinary
rejection commits the write; any error leaves bytes and revision unchanged.
A same-value write still advances the revision once. A read-only step or a
full registration row does not copy the session's payload buffers.

`GameSessionV1::apply_placement_admission` executes exactly this one step on
its owned state. It rejects absent prepared state rather than manufacturing
zero tables. State changes survive level replacement and trusted-schema
snapshot restoration. The method does not grant live entity capabilities.

## Compiler-side RAC bridge

`compile_rac_moby_admission_v1` resolves the recovered
[source admission contract](RAC_MOBY_ADMISSION_V1.md) into this neutral plan.
The caller supplies explicit source-index windows mapped onto canonical
views, including the selector-cache prefix and current selected table rows.
No session contents are baked into the decision.

The compiler preserves source flag precedence, unavailable reached inputs,
the negative-key registration bypass, and the signed full-width comparison
against sign-extended registration halfwords. A truncated key that cannot
match the full source value disables exact matching, not insertion into an
empty slot. Source constructor key/selector truncations and the no-write
auxiliary sentinel are returned as separate compiler-side metadata; they do
not become runtime class dispatch or a substitute constructor.

The compiler cannot establish the caller's live table provenance. Supplying
separate buffers for source locations that actually alias would still be an
incorrect binding; canonical views must represent their shared bytes.

## Binary I/O

`ORPADMIT` format version 1 serializes one plan. Its `0x40`-byte header has
an eight-byte magic, format version, header size, exact total byte count,
plan schema version, zero reserved word, and a 32-byte digest at `0x20`.
The body begins with the state-schema digest and initial count, followed by
the optional registration, suppression, and condition fields in that order.

All integers are explicit little-endian. Presence markers are exactly 0 or
1. Condition kinds are 0 (none), 1 (clear bit), and 2 (count selection).
Keys are nonempty, non-whitespace printable ASCII, with an inline byte length
and element index. Small scalar values use `u32` fields with zero excess
bits. There are no alternate section layouts, padding, or trailing bytes.
SHA-256 covers the complete artifact with only its own digest field zeroed;
this is an integrity check, not publisher authentication.

Encoding measures the bounded result before allocating its output. Decoding
checks the complete field layout, markers, keys, slot ranges, scalar widths,
and exact end before allocating keys, then verifies the digest and constructs
the plan. Callers must provide positive byte, key, and registration limits.

## Verification and remaining connection

Synthetic source/native differential tests compare 8,612 cases through
compilation, plan binary roundtrip, and `GameSessionV1` execution. They cover
flag precedence, selector and bit choices, suppression, signed-key boundaries,
registration patterns, count overflow/sign edges, constructor scalar mapping,
and final key/adjacent-halfword bytes. Additional regressions cover missing
reached inputs, rollback, aliases, lifecycle, and schema-bound restoration.
I/O tests include 315 shape combinations, truncations, corruption, malformed
but rehashed layouts, and an independently checked minimal-artifact digest.

The local supported-ISO experiment also agrees for all 296 Veldin records
using explicitly supplied file/template state, with each plan serialized and
executed on canonical session storage. This is **not a live new-game oracle**:
it runs no constructors and proves neither the first-admission state nor a
complete live roster.

The Release portable build passes 121 tests and PE import audits. Published
package-only level smoke checks for levels 0 and 1 also pass. The latter
exercise the unchanged eight-resource profile, not admission in the graphical
frontend. No visual or full-gameplay fidelity claim follows from these tests.

The remaining connection is ordered materialization over the complete
authored catalog: admission, accepted construction and its shared effects,
then the next record. Rejected placements need absent live identities, not
merely disabled visible entities. Cached selectors have a different lifetime
from persistent rows, and explicit source loader operations must preserve
that distinction. Original constructor/post-step services, remapping,
initialization scheduling, and native resource publication are not replaced
by this single-decision API.
