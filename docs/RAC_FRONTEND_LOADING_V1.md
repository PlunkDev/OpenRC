# RAC frontend loading cards v1

`rac_frontend_loading` executes the original loading-card clock, draw selection,
STQ packet construction and selected PIF2 asset ownership on the compiler side.
It is part of the existing frontend compiler. It does not play a substitute
loading screen, execute disc I/O, or introduce RAC data into the runtime.

## Source path and assets

The fresh New Game transition `233308` reaches three `232ef0` calls, each
followed by `232920` movie playback. Their first/second card indices are
`0/1`, `2/2`, `3/4`; timer arguments are `240`, `180`, `240`. These are
arguments to the existing exact `1f98c0` evaluator, not literal frame counts.
At the supported PAL scale `3f555555`, they become `200`, `150`, `200`.

`232b90` selects global WAD TOC `1388 + 8*max_signed(language-1,0)`, loads it
through `2175c8`, waits, and decompresses with the existing WAD owner. The
decoded directory contains 18 PIF2 records: a shared 64 by 64 tile at `+4`,
followed by 17 authored 512 by 64 cards at `+8+4*index`. Each uses a 32-byte
header, 1024-byte PSMCT32 CLUT and linear PSMT8 indices. The six source uploads
are tile palette/pixels, first-card palette/pixels, second-card palette/pixels.

`parse_rac_frontend_loading_assets_v1` selects these three actual records,
checks their dimensions, format, ranges and aggregate limits before allocating
decoded output, and retains raw palette/indices alongside canonical RGBA.
The shared PSMT8 CLUT permutation and alpha export helper are reused. Equal
first/second card indices preserve their source alias. Canonical alpha export
is not a claim about GS blending; a faithful raster consumer uses raw alpha.

## Clock and I/O order

`begin_rac_frontend_loading_card_v1` represents `232f4c..232fa0` after the
asset uploads have completed. A requested level load is returned even when
the later draw gate is false: original `12f4a8(level)` and the two halfword
clears `15ef48/15ef4a` precede that gate. The original loop starts only if
signed duration is positive, signed card `+dc < 3`, and signed card `+e4 < 0`.

Every rendered frame then performs original card I/O `209e68`, `209070`,
presentation and command-buffer rotation. Only after this tail does a pending
level load call `204c60`. `advance_rac_frontend_loading_card_v1` requires its
actual completed return for that frame. A zero return sets duration to at
least `frame+20`; any nonzero return ends subsequent polling. It then increments
the frame and applies the updated card-state gates. An unexpected or missing
poll result is rejected. The bounded compiler state allows up to 1048576
frames; reaching that limit fails explicitly rather than inventing completion.

When the loop ends, source calls `1f4e08(2)`: two literal fade updates, not a
scaled timer argument. The card I/O, level-load submission, presentation and
final fade must be connected by the consuming compiler/runtime lowering;
the returned request does not mean those actions have happened.

## Drawing

`execute_rac_frontend_loading_draws_v1` executes `233000..233230`. The display
value `13e600+0c` is the centre Y, not the framebuffer height. A single card is
centred at `Y-32`. Different cards draw the first band at `Y-46` and reveal the
second at `Y` starting on frame 65. The second Y is a fresh source load with
no additional subtraction. The tile receives the frame-dependent alpha;
the overlaid authored card always receives raw RGBA `80808080`.

Scrolling uses original `CVT.S.W(frame%600)`, ordered `MUL.S` by `3ada740e`
and `ADD.S` by zero / `3ecccccd`. The shared integer COP1 reference evaluators
produce these raw values; host floating-point arithmetic is not substituted.
The resulting ST endpoints are `U=0..4`, `V=scroll..scroll+0.4`, with Q=1.

`emit_rac_frontend_stq_quad_v1` completes original `232a00`: it shares the
already-qualified integer coordinate owner, preserves wrapping/sign-extension,
emits the original 128-byte packet, changes the REGLIST to ST and PRIM to `54`,
and forces Q=1 while retaining the caller's low RGBA word. It does not add a
cull or normalize unusual source coordinates. The authored-card call remains
the existing `emit_rac_integer_quad_v1` / `1f5800` owner.

These are executed source draw requests and packets. The separate
`rac_frontend_loading_raster` compiler lowers each reached band to an existing
`ScreenOverlayImageV1`, with raw GS alpha represented as coverage over128.
For Q=1 it converts the source V words exactly to fixed rational values,
applies the source half-pixel geometry, then floors affine texture positions
to four subtexel bits. CLAMP0 repeats each of four neighbours before separate
horizontal and vertical integer filtering. Alpha MODULATE follows filtering;
canonical min(255,2*a) export is never used as a raster intermediate. The
authored card's 1:1 coordinates land on exact texel centres.

This uses the same public-reference GS filtering contract as the title
compiler. It does not qualify physical GS DDA/STQ interpolation rounding.
Orchestration, frame/resource publication and asynchronous I/O are still
integration work; these compiler modules do not claim the loading cards are
already visible in the runtime.

## Validation

The permanent tests cover selected PIF/CLUT ownership, aliases and limits,
asynchronous duration extension and actual completion, signed card gates,
single/two-card reveal, Y reload, RGBA and Q. No original assets are included
in the tests.

The optional ignored fixture is generated by
`local/forensics/trace_rac_frontend_loading.py` from the hash-checked original
ELF. It executes 47123 raw instructions: 256 complete STQ packet cases,
432 clock cases and 213 draw-selection cases. The clock supplies an explicit
`204c60` return at its call boundary. Draws stop at actual draw calls and
compare their arguments; COP1 operations use the independently qualified
integer reference helpers. This is source/reference versus native comparison,
not a new physical-console capture or an execution of unimplemented I/O.

All Windows test/probe executables must follow the repository CMake, static
runtime, PE-audit and `build-portable` launch rules.
