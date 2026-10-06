# Original frontend list drawing

`execute_rac_frontend_list_draw_v1` executes compiler-side callback `21c1b0`.
The main screen's three list nodes use this callback. The shared heading
callback `21b298`, described in [RAC_FRONTEND_TEXT_NODE_V1.md](RAC_FRONTEND_TEXT_NODE_V1.md),
is a different owner; it does not render these list buttons.

The recovered main screen at `1d4948` retains fourteen original object IDs
`0, 1, 16, 17, 18, 5, 6, 7, 8, 9, 10, 11, 12, 13`. Its list nodes are
`1d4a18`, `1d4a68` and `1d4ab8`, initially focused on `1d4a18`. Each owns one
twelve-byte row followed by a zero-key terminator:

| Node | Text key | Action | Target screen |
| --- | --- | --- | --- |
| `1d4a18` | 20266 | 4 | `1d5008` |
| `1d4a68` | 20264 | 5 | `1d4f80` |
| `1d4ab8` | 20199 | 3 | `1d4b08` |

These source addresses identify evidence and input ownership. The compiler
function receives explicit bounded values; it does not dereference ELF
addresses or select text by guessed English labels. This implementation does
not publish a neutral `frontend/main` resource or connect a partial menu to
runtime. Runtime remains on the neutral package side of the boundary.

## Executed callback contract

Inputs include live node dimensions, flags and selection; the ordered source
rows and selected text bank; all three metric tables; original texture catalog
and payload mapping; cold GS allocator base; shadow and screen offsets;
palette/control state; and actual timer operands. A zero-key row must be owned.
The source row fields remain distinct: signed text key, signed action, target
screen, signed secondary text key and signed color age.

The callback emits ALPHA_1 `0x44` and TEST_1 `0x2004b`, then begins its batch.
Font ID 1 uses table 1 and height 12; flag `4` selects font/table 3 and height
14; flag `8` overrides both with font/table 2 and height 10. Flag `16` selects
height plus three spacing; otherwise spacing uses signed division of the live
height by row count plus one. Width, positions and advances retain source
32-bit wrapping and arithmetic shifts.

Flag `0x4000` measures the original row keys to determine common alignment.
Flag `0x20000` can request the small font when the live width is below that
maximum plus six. The reached source branch writes flag `8`, returns 1 and
leaves the already begun batch without an end call. The result records this
incomplete submission; callers must not submit it as a complete draw batch.
An ordinary completion ends the batch and returns 2.

Each rendered row preserves lookup and measurement order. Action 2 first
looks up its original key, then replaces the text with key 20308. Flag `0x40`
left alignment takes precedence over common-width alignment. Shadow and main
glyph calls remain separate, ordered font binds. A secondary line retains two
separate lookups and two further glyph calls; it uses the primary line's X
origin and advances the original Y spacing. Descriptor bindings preserve
repeated calls while uploading a given font once in the cold batch.

Flag `2` uses the original fixed color. Zero-action rows use the original
focused or unfocused color. Other actions call timed-color owner `21c6c0` with
their actual signed age and two full-64-bit `-1` sentinels. Dyadic, non-dyadic
and over-duration paths now execute through the shared integer DIV/ACC
reference. Supplied observations cannot override an executed result and an
unused list observation fails. The current numeric qualification is in
[SOURCE_DIV_ACC_REFERENCE_V1.md](SOURCE_DIV_ACC_REFERENCE_V1.md), extending
[RAC_FRONTEND_TEXT_NODE_V1.md](RAC_FRONTEND_TEXT_NODE_V1.md).

## Actual integer glyph and quad path

`execute_rac_integer_glyph_v1` executes the actual callee `1f6668`, which calls
integer quad writer `1f5800`. It does not replace the floating glyph owner used
by heading/layout callbacks. Each call owns its text bytes through NUL or the
reached byte limit. A zero byte limit performs the source palette-zero update
when enabled, then returns without reading text.

Bytes 8 through 15 select inline palette colors when controls are enabled,
preserving alpha and clearing the high color word. They do not draw or advance.
Other bytes use signed metric advances; an advance of zero produces no draw.
Space only advances. Bytes `0x80..0xa7` first draw the accent from metric row
`byte + 0x40`, with its signed X/Y offsets, then draw the base glyph. Ordinary
cells are 16 by 16; source icons below byte 32 use 24 by 16 and the source
integer grayscale average. The icon color retains source word sign extension.

Each quad retains its source call PC, byte offset, metric index, arguments and
complete 128-byte packet from `emit_rac_integer_quad_v1`. RGBAQ and TEX0 remain
raw 64-bit values. The quad reads the explicit screen-offset pair Y then X;
neither glyph nor list owner writes those globals. Fixed-point coordinates
retain wrapping before the source shifts. This entire glyph path is integer;
it needs no unresolved COP1/VU arithmetic.

## Bounds and validation

The standalone glyph defaults to 65,536 consumed bytes and 131,072 emitted
quads, with metric indices restricted to the recovered 232-row table. Text
without an owned terminator or reached byte limit fails. Accent draws count
individually against the output budget.

The list defaults to 1,024 owned rows, 65,536 bytes per text, 4 MiB aggregate
text work/output, 1,048,576 visited bank rows and 131,072 quads across all
shadow, main and secondary calls. It additionally caps configured rows and
bank entries at 65,536 and configured per-text bytes at 1 MiB. Fallback text
on a lookup miss must own a NUL within the same bound. Original terminators,
signed fields, word wrap and lookup order are not repaired or normalized.
Texture residency and payload checks use the existing
[RAC_FRONTEND_DRAW_V1.md](RAC_FRONTEND_DRAW_V1.md) / GS scope contracts.

## Source comparison and remaining integration

The two CMake test targets need no original assets for ordinary CTest. Their
optional ignored corpora execute actual instructions from boot ELF SHA-256
`17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b`:

| Comparison | Cases | Original execution | Compared result |
| --- | --- | --- | --- |
| Integer glyph | 384 | 341,929 glyph and 709,846 quad instructions | 7,318 complete quad packets; final pen, color, palette and consumed bytes |
| Main-list callback | 192 | 168,185 callback and 724,399 nested instructions | 1,024 glyph calls, 4,102 complete quads, lookup order, final flags/color/palette and 25 font retries |

Both source/native comparisons and ordinary tests pass in PE-audited portable
executables. The glyph corpus covers original and synthetic metric tables;
the list corpus uses original tables with explicit synthetic live node, row,
text and GS allocation state. Exact dyadic timed colors execute through the
original instructions. These comparisons establish the bounded compiler
contracts; they are not physical GS captures or an authentic complete screen.
Scripts, raw corpora and source hashes remain under ignored `local/forensics`.

Enclosing projection `238d90` now has an executable integer-reference helper
described in [SOURCE_DIV_ACC_REFERENCE_V1.md](SOURCE_DIV_ACC_REFERENCE_V1.md). Its inputs
come from real animated frontend objects, their camera and live matrices;
local text width/height must not be inferred from labels. The source invokes
`1f9bf0` at `238e08/238e1c` to subtract camera vector `187180`, scales by 1024, invokes
`1f9ee8` at `238e54/238e64` using matrix `187140`, and divides by projected W
at `238e7c/238ea4/238eb8`. It then uses live projection scales and display
origins to produce callback dimensions. Matrix multiply-accumulate and those
COP1 reciprocals now execute through the shared integer reference layer.
Original object transforms, complete GS state, RTT/compositing and the actual
background scene must also be preserved before emitting a neutral screen
resource. No host projection, fabricated three-label layout or movie
replacement for in-engine scenes closes that dependency.
