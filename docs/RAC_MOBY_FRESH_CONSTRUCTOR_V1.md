# Source fresh Moby constructor

`construct_rac_moby_fresh_v1` is a compiler-side recovery of the original fresh
constructor and its initial frame-pointer leaf. It is not the full accepted
placement owner, an AI callback, or a new runtime source-memory layout.

Inputs explicitly resolve the source class-table index, class word, live-index
word, callback reference, optional model and optional sequence 0. A present
model must have a nonzero reference; a missing model/sequence denotes an actual
resolved null, never a decoder failure. References are opaque compiler values
and must not enter neutral runtime packages.

The source first clears its entire 256-byte slot. The returned named value
accounts for every nonzero-capable field; all omitted bytes are therefore zero,
not inherited actor state. The constructor copies raw numeric words but performs
no floating-point arithmetic. It never calls the class callback or writes the
shared model. Initial frames use logical sequence-0/frame-0 table order.

Model flags are read from their current shared state, including earlier accepted
placements. Constructor-local changes stay local: null callback/model flags,
auxiliary/model-byte branches, sequence-count flag clearing and the exact
model-kind/signed-sound branch. Later placement must mutate the shared model at
the original ordered point; this helper must not write actor-local flags back.

## Verification

An independent tracer executes hash-qualified original constructor, clear and
frame-leaf instruction ranges, including branch delay/likely behavior and raw
COP1 transfers. Its 1,928 synthetic-binding cases verify all 64 ordered clear
stores, actor guards, input immutability and bounded actor/stack write ownership.
The audited CMake native probe matched the **entire 256-byte diagnostic result
for all 1,928 cases**. No proprietary instruction/asset fixture is committed.

Repository synthetic tests additionally exhaust 262,144 combinations of frame
count, signed sound byte and model kind; cover cumulative flags, null bindings,
raw scale bits and input immutability; and reject a contradictory present-null
model. These are instruction/native comparisons, not live Veldin or physical
PS2 captures.

## Ordered integration is still incomplete

Every accepted record still requires the complete following authored placement
and post-step before the next admission, including nonzero rotations. Qualified
MUL/ADD/ACC behavior, source field transfers, shared model mutations, derived
vector/matrix/cache writes, counter update and ordered spatial effects cannot
be skipped or represented by an empty callback. Four authored tail words still
need preservation by the placement parser. External sequence-0 resolution,
including the player, remains the compiler's responsibility.

The current graphical level loader creates authored definitions directly; it
does not yet run this owner. The eventual transaction must use the canonical
session and world, preserve schema-bound persistent state, assign distinct
source live-order tokens and native entity IDs, and publish the staged level
atomically. A disabled entity is not an absent/rejected record. Existing
[admission](PLACEMENT_ADMISSION_V1.md), [session state](SESSION_STATE_V1.md)
and [ordered spatial index](ORDERED_SPATIAL_INDEX_V1.md) remain the foundation.

No runtime resource count, gameplay behavior, save schema or compiler cache
identity changes in this component-only addition. Full constructor/post-step
integration remains **PARTIAL**; the pure fresh constructor is now implemented
and independently source-compared.
