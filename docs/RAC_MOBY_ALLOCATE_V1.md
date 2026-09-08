# Original dynamic Moby allocation

`execute_rac_moby_allocate_v1` implements the original scalar `24f870` allocator
as a compiler-side adapter over a caller-owned **staged** SessionState. It
reuses the [fresh constructor](RAC_MOBY_FRESH_CONSTRUCTOR_V1.md), not a second
entity world, generic replacement actor or raw PS2 actor image in the runtime.

## Source behavior

The current dynamic cursor and exclusive end delimit selectable 256-byte
source slots. A final physical guard slot exists at that end; it is not
selectable. Status values below FE are skipped. Each FE/FF candidate rereads
the zero-extended 32-bit timer and compares it with the full unsigned 64-bit
deadline stored at +38. A nonzero high deadline word therefore matters.

On selection, FF publishes FF to the **next** physical slot before the actual
fresh constructor; FE does not. The constructor's live index uses the distinct
all-actor base. The selected PVar address uses the dynamic base and the original
wrapping SUBU/SRA/SLL/ADDU sequence. Exactly 128 PVar bytes are cleared. The
current count decrements only when nonzero. Exhaustion returns no actor; the
reached diagnostic callee in this source version only writes its own stack.
No invented logging, allocation fallback, callback or gameplay effect is added.

## Canonical ownership and publication

Bindings identify explicit typed SessionState ranges: all physical statuses,
paired low/high +38 words, PVar words, timer and count. The original address
numbers are compiler-only provenance for resolving that data. The adapter
checks current schema/revision, complete reached ranges, limits, alignment,
independent ownership and source address arithmetic. Missing reached class,
model or canonical data is not an implicit zero/null actor.
Actor and PVar source owners also cannot overlap the six fixed scalar globals
read by this graph. Otherwise the source clear could change a later reread
while the canonical adapter incorrectly retained its old input.

The result returns the complete named fresh-constructor value, selected slot,
neutral PVar binding and diagnostic selection/count information. Native
metadata/PVar writes commit atomically after all reached work succeeds. That
error policy is separate from original intermediate memory visibility.

**The caller must install/lower the returned fresh actor before publishing the
staged world/session.** This adapter does not create an EntityId, establish its
lifetime, execute later placement/rotation callbacks or publish a live actor.
Those ordered integration steps remain required for original gameplay.

## Verification boundary

Permanent synthetic tests exercise every status byte, timer/deadline edges,
reserved guard, FF/FE behavior, distinct bases, exact PVar clearing, conditional
count changes, empty/exhausted pools, schema/range/alias errors and atomic
rejection. Original instruction comparisons and hardware/gameplay checks are
separate evidence; a passing allocator test is not a Veldin playthrough.

An independent tracer executed the actual allocator, fresh constructor,
word clears, frame leaf and exhaustion stub. The audited native probe matched
**1,367 cases**: 974 allocations (625 FE, 349 FF) and 393 exhausted/empty
results. It compared the entire source pool and PVar images, exact timer-read
counts, selected slot, counter changes and neutral binding. Original source
bytes and fixture data remain ignored. These are synthetic live-state inputs,
not a physical-console trace or proof of live entity publication.
