# Source authored Moby color tail

`recover_rac_moby_authored_tail_v1` recovers a closed compiler-side step after
accepted placement's full post-step. It preserves source color/cache/light
results and the input to the next shared-reference helper. It is not a runtime
actor layout, complete placement owner, or implementation of that shared call.
The earlier [fresh constructor](RAC_MOBY_FRESH_CONSTRUCTOR_V1.md) remains a
separate operation in the original order.

## Raw parser inputs

`RacGameplayMobyInstanceV1` retains the complete original words:

| Record offsets | Compiler field | Interpretation |
| --- | --- | --- |
| `+0x64/+0x68/+0x6c` | `authored_color_words[3]` | Raw 32-bit operands, not clamped byte channels |
| `+0x70` | Existing signed `light_index` | Preserve its bits when supplying the helper's raw light word |
| `+0x74` | `authored_reference_index_bits` | Raw 32-bit argument to the subsequent reference helper |

The three color operands and reference index close four previously discarded
words. Existing parsing gates and authored model, transform, admission, mode,
occlusion and light semantics are unchanged. No floating reinterpretation,
alpha conversion, range normalization or source pointer enters a neutral
package. These parser members do not change a runtime package schema.

## Complete arithmetic and ordered results

For raw color operands `c0,c1,c2`, the source computes shifted `c2` plus shifted
`c1`, then adds `c0`. Every shift and addition has wrapping 32-bit semantics:

```text
pair = wrap32((c2 << 16) + (c1 << 8))
P = wrap32(pair + c0)
```

These are additions, not channel-wise ORs; overlapping bits can carry. The
returned named state retains all three original actor writes in order:

1. Cache `P` at source actor offset `+0x80`.
2. Store the full 64-bit value `uint64(P) << 32` at `+0x38`.
3. Replace only that store's low word with raw record `+0x70`.

Final `+0x38` is the light word, while `+0x3c` and cached `+0x80` both contain
`P`. The intermediate zero low word is explicit; a negative-looking light
value must not sign-extend over the high color word. The helper returns values
and ordered effects, without mutating source memory or publishing an entity.

`pack_rac_moby_color_words_v1` also implements the entire reusable packed-store
leaf, not only this caller's three zero operands:

```text
(high_word << 32) | low_word | (shifted_8 << 8) | (shifted_16 << 16)
```

All four inputs use full low-64-bit semantics; none is masked to a byte. This
leaf combines by OR, unlike the preceding authored color addition, and has no
floating-point operation or old-state dependency.

## Exact pending-call gate and incomplete integration

Only the complete reference word `0xffffffff` skips the next source call.
Every other value remains an explicit `reference_helper_index_bits`, including
zero, high-bit values and other words ending in `ffff`. A present value means
**a pending call**, not a successful registration or an admitted entity.

Source helper `0x24b1b0` separately reads the current signed level and base
table, applies its own gate, and may write a shared indexed reference. Resolving
that live state, destination ownership and actual mutation is still required.
The color helper neither skips it nor substitutes a runtime source pointer.
After its return, or the exact caller skip, source live-count advance and the
next admission may proceed. Later source callers can modify that same shared
structure, so this tail cannot declare its final level-wide contents.

The tail is reached by every accepted record, including genuine null models.
For a non-null model, the complete earlier post-step must first finish for all
rotations; a zero-rotation-only path is not completion. Canonical session/world
integration, publication of the ordered accepted set, class callbacks, enemy AI
and player gameplay remain outside this component. The graphical loader does
not yet execute the complete recovered placement owner.

## Verification

An independent bounded tracer executes hash-qualified original caller and
complete packed-leaf instruction ranges, including call/return delay slots.
Authored cases stop at the pending reference call or before live-count advance;
they do not claim to execute the shared registration helper. It verifies exact
write order, widths and values, unchanged unrelated bytes and guards, and the
raw sign-extended argument when that call is reached.

The CMake-built, statically linked, PE-import-audited native comparison passed
all **3,330 cases**:

- 296 original authored rows, also checking all five raw tail words through
  the updated production parser; these are not an admitted-set/live-game trace.
- 1,753 synthetic authored cases covering carries, overflow and full-word gates.
- 1,281 generic packed-leaf cases covering every input bit and full-64-bit ORs.

Repository-owned synthetic tests additionally cover partial low-word replacement,
cached/intermediate/final results, reference suppression and raw parser retention.
Original images, code and comparison fixtures remain ignored local inputs; no
proprietary bytes are distributed with these tests. This is original-instruction
versus native evidence, not a physical PS2 capture or a playable-level claim.
