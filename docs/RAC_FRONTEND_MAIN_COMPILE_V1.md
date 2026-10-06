# Original main-list raster compilation

`rac_frontend_main_compile` composes the existing original font, text, list,
object-animation, corner and projection owners. The output is an existing
neutral `ScreenOverlayV1` containing three actual list render targets. These
layers are separate from the original class1138 object model draws and the
five scenic background actors. Source descriptors and addresses stay compiler
side; no synthetic UI tree or replacement text layout enters runtime.

`compile_rac_frontend_main_assets_v1` reads the original main descriptor
`1d4948`, its14 sequence IDs and node references, its focused node and its
three visible list records. Nodes`1d4a18/1d4a68/1d4ab8` bind callback`21c1b0`
and text flags`+30=4`, selecting font3. Their source keys remain
20266/20264/20199. The first two slots reference`1ce678`, whose general
node flag4 hides that node. General node flags and text flags remain distinct.

The original frontend text directory is TOC1528, LBA18200,308 sectors. The
compiler uses the exact numeric slot passed to`1eb300`; it does not apply the
title-image selector`max(language-1,0)+4`. Main localized text occupies slots
0,2,3,4,5. Lookup fallback is the original NUL-terminated text at`199a68`.
All three font metric tables come from`1df3d0`,2784 bytes. Atlas entries1..3
are the existing header58/5c/68 catalog's256x128 PSMT8 textures with a shared
CLUT at relative2048. Original palette alpha remains unconverted through
sampling and modulation.

`compile_rac_frontend_main_entry_geometry_v1` reuses real14-object admission,
same-screen reverse selection and the12-update screen gate. It performs the
source pre/stop, selector-resolved corner sampling, then post ordering on
each update. The returned13 samples include the first active main frame.
Source references are relocated into compiler-owned class/object/PVar ranges;
they are not presented as observed physical game allocation addresses.
Source camera219c08 and projection factory187140 execute through the qualified
numeric adapter. Projector238d90 consumes corner0 and corner3, not guessed
positions or widths based on labels. Owner21a610 adds one to the projected
X/Y before compositing.

The first active main update executes the row timer prefix of21bb90. The
focused first row's signed halfword age becomes1; the other two remain0.
Timed color uses the original1602b4 value10 and PAL scale`3f555555`, giving
duration8 through the source COP1 timer. Other ages are explicit compiler
inputs; the complete ordered numeric color helper is reused.

## Actual RTT pixel path

The source does not draw these glyphs directly over the scenic background:

1. Owner21a610 creates a power-of-two RTT, with exponent at least7.
2. Source SDK setup zeroes its owned rows. Call21aa6c→2017c8 then clears to
   live1602b0=`80100808`: RGB(8,8,16),alpha128.
3. Callback21c1b0 emits the actual measured, centered integer glyphs through
   1f6668→1f5800. Shadow offsets1602b8/bc are(2,2). Each cell maps1:1 to its
   source atlas texels; its half-pixel XY adjustment places samples at texel
   centers. Atlas bounds and full emitted packets are checked.
4. RGBA MODULATE multiplies source texel channels by vertex channels, shifts
   right7 and clamps to255. TEST=`2004b` means GEQUAL4 with failed fragments
   kept out of the framebuffer. ALPHA=`44` applies the source-alpha blend
   with a floor for negative as well as positive RGB differences.
5. The source callback returns2, selecting an identically sized UV crop from
   the RTT. Final ALPHA=`8000000064` selects FIX128, so the resulting RGB is
   copied to the projected screen rectangle independently of texture alpha.
   Neutral output therefore has coverage128 throughout the crop.

Render PSMCT32 is source initialized:1fac24 stores zero to151880+154,
1fac68 reloads it as the PSM argument of SDK1222c8 at1fac74. Dithering is off
for this format. The source SDK/2017c8 clear leaves the final RTT scanline
outside the proven clear envelope; the bounded compiler rejects a crop that
would read it. Its last column retains the SDK's zero where the later clear
does not cover it. Complete source main bounds are measured by the original
projector before this condition is checked.

No fractional filter model is required on either glyph sampling or the
return2 composite: both are unit gradients. No immediate font-retry draw is
invented; an incomplete return1 callback is rejected by this completed-frame
helper. The actual localized main labels contain no inline palette controls,
so incoming palette values are irrelevant. A new inline control dependency
is rejected instead of selecting an invented palette.

## Local verification artifacts

Original bytes and generated images remain in ignored`local/forensics`:

- `frontend-transition/frontend-text.bin`,630784 bytes, SHA256
  `f650990d2b794c87302006960769a23da858d15099e1a6742402fdcc0a54c785`.
- `frontend-transition/frontend-class1138.bin`,169664 bytes, SHA256
  `4f26a2e0938721205ea897d6c450e6dd8956ac5348cfe5a23e35c1047632e5f9`.
- Boot ELF, WAD and original owner provenance are shared with
  [title](RAC_FRONTEND_TITLE_V1.md) and [list drawing](RAC_FRONTEND_LIST_V1.md).
- `rac_frontend_main_source_probe.cpp` compiles the five original text slots,
 13 entry geometry samples, and three list RTTs per language. Neutral images
  are exported under`frontend-transition/main`.

Permanent tests use synthetic font textures and real integer glyph execution
to check GEQUAL4, source modulation, negative-delta shadow blending, clipping,
source crop ownership, incomplete callbacks, corrupted packets, projected
positions, focused-row color and neutral codec round trips.
