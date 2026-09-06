# OrderedSpatialIndexV1

This native, level-local service preserves the recovered ordering and storage
effects of dynamic cell membership. It is separate from `CollisionWorldV1`,
which indexes immutable terrain triangles. It does not calculate world-space
bounds, perform collision response, construct actors, or run enemy AI.

## Neutral ownership

The caller supplies a bounded grid, a pool of sixteen `u16` members per
allocation block, and allocation words with one bit per block. Cells carry
their first block, active count, and capacity. Member tokens represent a
source-compiled live ordering; they must not be implicitly equated with an
authored ID or `EntityIdV1::slot`. The level owner needs an explicit binding
to live entities, including generation and level-instance lifetime.

Each token has an optional inclusive cell rectangle. Absence means no spatial
membership, not absence of the actor itself. Rectangles use neutral cell
coordinates, not packed PS2 fields or pointers. The exact numerical conversion
from the original actor's matrix, sequence bounds and position is a separate
required input contract; host floor/ceil or approximate rotation is not
provided as a substitute.

An explicit cleared-image factory is available for callers whose initialization
contract establishes a clear. It is not the default interpretation of unknown
source memory. Restoration also accepts a complete validated snapshot, retaining
all pool values and allocation bits. Allocated bits not owned by a cell remain
opaque reservations and cannot be reused accidentally.

## Ordered updates

An update removes cells covered only by the old rectangle, then appends to
cells covered only by the new one. Both loops visit X inside Y, with inclusive
endpoints. Shared cells retain their member order. Querying a cell returns its
stored active order without sorting or deduplication.

- Removal replaces the first matching member with the last member, zeros the
  old last slot, and decrements the count.
- Shrink occurs when the new count is zero, or when capacity is not one block
  and `new_count < 8 * old_capacity_blocks - 4`. Equality does not shrink.
  Capacity is halved **once**, not repeatedly reduced to a minimum fit.
- Shrink frees the old allocation before searching for the replacement, then
  copies the new capacity's whole blocks. A zero count with an old capacity
  greater than one still leaves a smaller, nonempty allocation.
- Growth doubles capacity (zero becomes one), allocates the new block range
  **before** freeing the old one, then copies the old capacity's whole blocks.
- Allocation searches words in ascending order and aligned, capacity-sized
  groups from their least significant bits. Freeing never clears pool data.

Whole-block copying includes inactive values. Each block is fully read before
it is written, including self-copy after shrink. Reconstructing a snapshot
from sorted active members or reusing an arbitrary free list would change
later results even if the immediate set of candidates looked correct.

## Bounded execution and snapshots

The supported non-wrapping domain is explicit: dimensions 1..64, pool sizes
that are positive multiples of 32 and at most 32,768 blocks, allocation
capacities 1/2/4/8/16, and at most 255 active members in a cell. Limits also
bound total cells, blocks, and registered member tokens before copies.
An insertion that would wrap the count is an error, not a fabricated success.

The source allocator scans without a finite exhaustion check. This service
stops at the caller's verified extent rather than accessing unrelated memory
or silently selecting a different allocator. The source's observed bitmap
clear of `0x60` bytes establishes 768 cleared bits, not an independently
proven total capacity. The distance between source pool and bitmap addresses
is not permission to invent additional initialized allocation words.

Snapshots preserve the complete grid, active and stale pool entries, bitmap,
per-token rectangles, and revision. Validation rejects wrong extents, invalid
capacities, misaligned or overlapping cell allocations, free referenced blocks,
duplicate/unknown active members, and incomplete or contradictory rectangle
coverage. Unallocated cells require zero count and block index. Empty cells
with a valid retained allocation are permitted.

Each successful update advances the expected revision once, including an
unchanged rectangle. All changes are staged. Stale or exhausted revisions,
invalid bounds, count overflow, allocation exhaustion, or any later error
leave the entire previous snapshot unchanged. This is a native failure
boundary, not a claim that the PS2 trap handler rolls back original stores.

## Verification and integration boundary

Unit tests cover exact list order, overlapping movement, allocation and
relocation order, strict shrink thresholds, whole-block stale values,
reservations, coherent restoration, and late-error rollback.

A separate local probe executed the original recovered integer instructions
on explicit synthetic memory and matched all 615 operations against this
native service, comparing hashes of every cell, every pool entry and every
bitmap word after each operation. The sequence includes all supported
capacities up to 16 blocks / 255 active members and every shrink transition.
The fixture generator uses instruction decoding and branch delay slots, not
a second copy of the native high-level algorithm. This is stronger evidence
for this integer leaf, but is not a live Veldin startup trace or a PS2
floating-point oracle. Original instructions and forensic fixtures stay
under ignored `local`; they are not runtime inputs or distributed assets.

The Release portable build passes all 121 tests and PE import audits. The
published CLI also passes package-only level smoke checks for levels 0 and 1;
those preserve the existing eight-resource behavior, not new spatial-owner
integration in the graphical frontend.

Integration belongs beside the world that actually owns materialized actors,
currently `EntityGameplayRuntimeV1::LoadedStateV1`, or its eventual unified
level-actor owner. Persistent `GameSessionV1` progress and static triangle
collision are different owners. The exact numerical bounds service, ordered
actor construction, live-token mapping and frontend publication remain
unconnected; the published native profile still has eight resources.
