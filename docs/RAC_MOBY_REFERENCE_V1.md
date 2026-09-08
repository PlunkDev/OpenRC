# Source indexed Moby references

`execute_rac_moby_reference_store_v1` implements the complete scalar helper
`0x24b1b0` as a bounded compiler-side adapter. It reads current canonical
[SessionState](SESSION_STATE_V1.md) values and stores an explicitly bound neutral
object token. It does not allocate an actor, execute a callback, or integrate
the full placement loader.

## Exact operation

The helper reads the current level as a signed 32-bit word. Levels **greater
than or equal to 19** bypass the store; negative levels pass this source gate.
On the reached arm it reads the level's base word, adds the raw input index,
shifts the combined index by two, forms the destination and stores one word.
All source shifts, additions and address calculations wrap to 32 bits.

The high two combined-index bits disappear during the shift. Negative-looking
indices and wrapping addresses are neither clamped nor silently skipped: their
actual addresses must resolve through explicit compiler bindings. Only the
[authored-tail caller](RAC_MOBY_AUTHORED_TAIL_V1.md) skips index `0xffffffff`;
the helper itself does not. It never dereferences the supplied actor reference.

`reference_token` is a resolved neutral identity, with explicit zero meaning
resolved null. An absent value means an unavailable binding, not null. The
result retains the level, reached base and combined index, and the destination's
previous/stored tokens. No source-pointer bits are stored in canonical state.

## Bounds and ownership

Bindings name a trusted state-schema digest, current-level element and explicit
source-to-neutral word windows. Each reached window must own its **entire**
declared range in a contiguous unsigned 32-bit view, not merely the selected
element. Source intervals are aligned and non-wrapping; native exclusive ends,
key sizes and word counts are bounded before mutation.

The reference source interval cannot overlap the declared base/level scalar
owners. Its neutral interval cannot overlap another schema view or same-view
scalar bindings. Missing reached inputs, unmapped addresses and unsupported
aliases fail without changing state. Unreached base/token bindings need not
exist; schema and revision still must match on the level bypass.

A reached store commits once, including a same-token write, advancing the
native state revision once. Bypass leaves the revision unchanged. Reads use
current canonical bytes, so later calls observe updated level/base values and
earlier registrations. No initial table, clear or reload state is inferred.

The original segment readers establish **20 boundary words for 19 level
segments, covering 121 reference slots**. The final boundary is required to
describe the last segment. This is not proof of the exact size of a larger
source allocation, and the store helper itself has no per-segment index limit.
Adjacent source-image zeros do not imply additional slots or live initialization.

## Integration still required

The authored call follows complete construction, post-processing and color/light
effects. It must return before live-count advance and the next admission.
A second, later loader call registers index zero after its actual allocation,
initialization and complete post-step. Its condition depends on source numeric
comparison and live shared state, including a reread after possible callbacks;
neither an ELF placeholder nor an assumed actor substitutes for that flow.

The staged level/entity owner must still supply token identities, bind them to
actual live entities, maintain their lifetimes and publish the canonical
session/world atomically. Tokens are not `EntityId.slot`. Reusing SessionState
byte ownership does not make transient object references persistent save data.
The later allocator/callback owners, full nonzero-rotation post-processing,
reference consumers, player gameplay and enemy AI remain separate dependencies.

## Verification

The CMake-built, statically linked, PE-import-audited native probe matched
**2,939 independent original-instruction executions**: 1,698 stores and 1,241
level bypasses. Coverage includes all 121 original segment slots, the complete
20-word boundary table, 770 synthetic edge cases and 2,048 deterministic random
cases. The source tracer checks exact read/write order, widths, guards and
return behavior; native comparison checks full canonical snapshots using
distinct source-pointer and neutral-token identities.

Repository-owned unit tests also passed, covering ordered updates, resolved
null, negative and wrapping inputs, entire-range bounds, alias rejection and
schema/revision failures. Original code, tables and comparison fixtures remain
ignored local inputs. These are source/native comparisons, not physical PS2
captures or proof of a running original loader or playable Veldin.
