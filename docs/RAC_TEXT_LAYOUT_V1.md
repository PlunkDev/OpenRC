# Original text layout and floating glyph plan

These compiler-side helpers recover original frontend text behavior from the
owned PAL v2.00 executable. They do not introduce a replacement menu, host font,
runtime PS2 decoder, or new prepared resource. Original source fixtures remain
under ignored `local/forensics`; public tests contain synthetic data only.

## Integer layout owner

`plan_rac_text_layout_v1` recovers owner `1f7070..1f755c`, including the width
leaf `1f65b0..1f65fc`, from bounded original byte text and a 232-row metric table.
The descriptor retains all twelve original 16-bit fields and unassigned flag
bits. Output contains the modified measurements, final line spans, ordered
optional glyph-call specifications and entry/exit clip-call arguments.

Important source behavior is retained:

- Width sums signed metric byte 3, including control rows; it is not atlas cell
  width and is not the same operation as deciding where a line ends.
- Scanning uses spaces/control bytes as break opportunities, recognizes bytes
  0/1 as scan terminators and carries enabled color indices 8..15. Forced spans
  may include the next endpoint byte beyond the accumulated wrap width.
- The supplied byte limit is checked at the original line boundaries, not
  interpreted as a host substring length. Zero limit performs no text read.
- Short-last-line balancing shrinks the wrap width by 16; if the line count
  increases, it retries the original width. Neither scanned color nor the last
  recorded terminal width is reset between retries.
- Starts/ends use signed halfword stores/loads. Empty spans are retained;
  exceeded 32-line source arrays and out-of-span or unsupported metric reads
  fail explicitly. An independent byte-visit budget bounds work.
- Vertically culled lines are neither measured nor drawn, even in measure-only
  mode. Equality at both original vertical gates remains accepted. Total height
  still includes culled lines; measurement stores retain low-16-bit wrapping.
- Each drawn line overwrites palette slot 0 with the incoming color's low word,
  then loads its selected palette word with sign extension to low64. Raw TEX0
  is retained. Nested glyph calls suppress palette reseeding. The exit clears
  that suppression and requests full-screen clipping, not the previous clip.

The floating call keeps integer bases and signed subpixel operands. Its exact
coordinate expression is `ADD.S(CVT.S.W(base), MUL.S(CVT.S.W(subpixel), 1/16))`,
with separate operations and glyph scale bits `0x3f800000`. No host floating
arithmetic is evaluated. Integer glyph calls are represented as calls; their
callee implementation is not supplied by this helper. The separate
[frontend draw scope](RAC_FRONTEND_DRAW_V1.md) now emits original SCISSOR
packets; layout alone does not establish complete inherited render state.

## Floating glyph/control owner

`build_rac_float_glyph_program_v1` recovers owner `1f69f0` and its word-conversion
leaf as a small ordered source program. Input X/Y/scale remain symbolic; the
program explicitly records each signed-word conversion, single multiply/add,
and original quad-emitter call. It is not a general interpreter or evaluator.

The plan preserves accent-before-base order, source signed offsets/advances,
16-pixel ordinary cells, 24-pixel icon width, space advance without a draw,
zero-advance suppression, and original enabled/disabled inline color handling.
Palette seeding and the initial scale-times-16 operation still occur for an
empty draw. NUL and exact source byte-count termination are separate from the
caller-owned allocation/work bounds; bytes behind either stop are not read.

Ordinary/accent draws retain full low64 color and texture arguments. Color
controls preserve alpha but clear high32; icon grayscale packing sign-extends
its final word. These arguments must not be prematurely normalized to RGBA8.
Byte 1 is not a newline inside this glyph owner: layout removes it from the
span before calling this routine. A composition regression checks this boundary
and ensures selected line colors cannot reseed palette slot 0.

## Qualification and remaining work

An independent bounded tracer executed actual original layout and width-leaf
instructions, recording clip/glyph calls and symbolic COP1 expressions. The
audited native comparison matched **1,932 cases**, including 373 retry cases,
1,393 glyph-owner calls and 674 floating-coordinate expressions. Another 244
generated inputs exceeded explicit source memory/array/work domains and were
not included as successful comparisons. Source code spans are hash-qualified;
fixture identity and reproduction scripts remain in ignored local evidence.

The separate glyph tracer executed original instructions with branch delays,
signed word/byte behavior and poisoned caller-saved registers across emitter
hooks. The audited native comparison matched **2,416 cases, 108,902 ordered
operations and 14,725 emitter calls**, using all three original metric tables
and synthetic variants. No arithmetic operation was numerically evaluated and
the emitter itself was not replaced with a fake rendering result.

Synthetic regressions additionally cover signed-halfword index 32767/32768,
negative advances, high color bits, all relevant flags, source limit semantics,
bounded work, clipped measurement and layout-to-glyph composition.

**PARTIAL original frontend:** these are source-backed layout and dispatch
contracts, not a visible menu or hardware-fidelity proof. Remaining work includes
qualified EE numeric execution, inherited GS state, original font/atlas
selection and render integration, menu lifecycle/input/transitions, intro/audio
and normal New Game. The [quad emitter and callback submission scope](RAC_FRONTEND_DRAW_V1.md)
now preserve source packet construction and upload order without bypassing those
requirements. Packages still contain eight resources.
